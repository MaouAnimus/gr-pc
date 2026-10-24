#include "hle/avplayer/avplayer.h"
#include "hle/guest_memory.h"
#include "mf_decoder.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <malloc.h>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace gravity::hle::avplayer {
namespace {
constexpr std::int32_t invalid = static_cast<std::int32_t>(0x80540001U);
constexpr std::int32_t no_data = static_cast<std::int32_t>(0x80540002U);
constexpr std::int32_t decode_error = static_cast<std::int32_t>(0x80540003U);
using Clock = std::chrono::steady_clock;
std::mutex texture_mutex;
std::unordered_map<std::uintptr_t, VideoTextureFrame> texture_frames;
std::uint64_t generation{};

struct AlignedFree { void operator()(std::uint8_t* p) const { _aligned_free(p); } };
struct Player {
    enum class State { empty, ready, playing, paused, stopped, ended, failed } state{State::empty};
    std::mutex mutex;
    bool closed{}, looping{}, video_end{}, audio_end{};
    std::unique_ptr<MediaDecoder> decoder;
    MediaMetadata metadata;
    std::future<MediaSample> video_request, audio_request;
    std::optional<MediaSample> video_pending, audio_pending;
    std::unique_ptr<std::uint8_t, AlignedFree> video_buffer;
    std::size_t video_size{};
    std::vector<std::uint8_t> audio_buffer;
    Clock::time_point anchor;
    std::uint64_t paused_us{}, end_us{};
    ~Player() { remove_texture(); }
    void remove_texture() {
        if (video_buffer) {
            std::lock_guard lock(texture_mutex);
            texture_frames.erase(reinterpret_cast<std::uintptr_t>(video_buffer.get()));
        }
    }
    std::uint64_t position() const {
        if (state != State::playing) return paused_us;
        return paused_us + std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - anchor).count();
    }
    bool restart() {
        // A seek is queued behind any pending reads on the decoder's MTA.
        if (!decoder || !decoder->rewind()) { state = State::failed; return false; }
        video_request = {}; audio_request = {}; video_pending.reset(); audio_pending.reset();
        video_end = !metadata.video; audio_end = !metadata.audio;
        end_us = metadata.duration_us;
        if (metadata.video) video_request = decoder->read(true);
        if (metadata.audio) audio_request = decoder->read(false);
        paused_us = 0; anchor = Clock::now(); state = State::playing;
        return true;
    }
    void finish() {
        if (state != State::playing || !video_end || !audio_end || position() < end_us) return;
        if (looping) restart(); else { state = State::ended; paused_us = end_us; }
    }
    std::int32_t poll(bool video) {
        finish();
        if (state != State::playing) return state == State::failed ? decode_error : no_data;
        auto& pending = video ? video_pending : audio_pending;
        auto& request = video ? video_request : audio_request;
        auto& ended = video ? video_end : audio_end;
        if (ended) return no_data;
        if (!pending) {
            if (!request.valid() || request.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return no_data;
            try { pending = request.get(); } catch (...) { state = State::failed; return decode_error; }
            if (pending->status == MediaSample::Status::error) { pending.reset(); state = State::failed; return decode_error; }
            if (pending->status == MediaSample::Status::end) {
                pending.reset(); ended = true; finish(); return no_data;
            }
            if (pending->status == MediaSample::Status::again) {
                pending.reset(); request = decoder->read(video); return no_data;
            }
        }
        return pending->timestamp_us <= position() ? 0 : no_data;
    }
};
std::mutex players_mutex;
std::unordered_map<SceAvPlayerHandle, std::shared_ptr<Player>> players;
std::uintptr_t next_handle = 0x20000;
std::shared_ptr<Player> find(SceAvPlayerHandle handle) {
    std::lock_guard lock(players_mutex);
    const auto it = players.find(handle);
    return it == players.end() ? nullptr : it->second;
}
}

std::vector<VideoTextureFrame> video_texture_frames() {
    std::lock_guard lock(texture_mutex);
    std::vector<VideoTextureFrame> frames;
    for (const auto& [address, frame] : texture_frames) { (void)address; frames.push_back(frame); }
    return frames;
}

extern "C" {
GRAVITY_SYSV_ABI SceAvPlayerHandle sceAvPlayerInit(const SceAvPlayerInitData* init) {
    if (init && !guest_memory::accessible(init, sizeof(*init), false)) return nullptr;
    auto player = std::make_shared<Player>();
    std::lock_guard lock(players_mutex);
    const auto handle = reinterpret_cast<SceAvPlayerHandle>(next_handle += 0x10);
    players.emplace(handle, std::move(player)); return handle;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerPostInit(SceAvPlayerHandle handle, const void*) {
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex); return p->closed ? invalid : 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerClose(SceAvPlayerHandle handle) {
    std::shared_ptr<Player> p;
    {
        std::lock_guard lock(players_mutex);
        const auto it = players.find(handle); if (it == players.end()) return invalid;
        p = std::move(it->second); players.erase(it);
    }
    std::lock_guard lock(p->mutex);
    p->closed = true; p->state = Player::State::stopped;
    p->decoder.reset(); p->remove_texture(); return 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerAddSource(SceAvPlayerHandle handle, const char* filename) {
    char path[1024]{};
    if (!guest_memory::string(filename, path, sizeof(path))) return invalid;
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex);
    if (p->closed || p->state == Player::State::playing || p->state == Player::State::paused) return invalid;
    auto decoder = std::make_unique<MediaDecoder>();
    MediaMetadata metadata;
    if (!decoder->open(path, metadata)) return decode_error;
    p->decoder = std::move(decoder); p->metadata = metadata;
    p->video_request = {}; p->audio_request = {}; p->video_pending.reset(); p->audio_pending.reset();
    p->remove_texture(); p->video_buffer.reset(); p->video_size = 0; p->audio_buffer.clear();
    p->state = Player::State::ready; return 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerSetLooping(SceAvPlayerHandle handle, std::uint32_t loop) {
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex); if (p->closed) return invalid;
    p->looping = loop != 0; return 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerStart(SceAvPlayerHandle handle) {
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex); if (p->closed || !p->decoder) return invalid;
    return p->restart() ? 0 : decode_error;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerStop(SceAvPlayerHandle handle) {
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex); if (p->closed) return invalid;
    p->state = p->decoder ? Player::State::stopped : Player::State::empty; p->paused_us = 0; return 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerPause(SceAvPlayerHandle handle) {
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex);
    if (p->closed || p->state != Player::State::playing) return invalid;
    p->paused_us = p->position(); p->state = Player::State::paused; return 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerResume(SceAvPlayerHandle handle) {
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex);
    if (p->closed || p->state != Player::State::paused) return invalid;
    p->anchor = Clock::now(); p->state = Player::State::playing; return 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerIsActive(SceAvPlayerHandle handle) {
    auto p = find(handle); if (!p) return 0;
    std::lock_guard lock(p->mutex); p->finish();
    return !p->closed && p->state == Player::State::playing ? 1 : 0;
}
GRAVITY_SYSV_ABI std::uint32_t sceAvPlayerStreamCount(SceAvPlayerHandle handle) {
    auto p = find(handle); if (!p) return 0;
    std::lock_guard lock(p->mutex);
    return p->closed ? 0 : unsigned(p->metadata.video) + unsigned(p->metadata.audio);
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetStreamInfo(SceAvPlayerHandle handle, std::uint32_t index, SceAvPlayerStreamInfo* info) {
    if (!guest_memory::accessible(info, sizeof(*info), true)) return invalid;
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex);
    if (p->closed || index >= unsigned(p->metadata.video) + unsigned(p->metadata.audio)) return invalid;
    *info = {};
    if (index == 0 && p->metadata.video) {
        info->width = p->metadata.width; info->height = p->metadata.height; info->framerate = p->metadata.frame_rate;
    } else {
        info->type = 1; info->channelCount = p->metadata.channels; info->sampleRate = p->metadata.sample_rate;
    }
    return 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerEnableStream(SceAvPlayerHandle handle, std::uint32_t index) {
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex);
    return !p->closed && index < unsigned(p->metadata.video) + unsigned(p->metadata.audio) ? 0 : invalid;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetVideoDataEx(SceAvPlayerHandle handle, SceAvPlayerFrameInfoEx* info) {
    if (!guest_memory::accessible(info, sizeof(*info), true)) return invalid;
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex); if (p->closed) return invalid;
    const auto status = p->poll(true); if (status) return status;
    auto& sample = *p->video_pending;
    if (p->video_size != sample.bytes.size()) {
        auto* buffer = static_cast<std::uint8_t*>(_aligned_malloc(sample.bytes.size(), 256));
        if (!buffer) { p->state = Player::State::failed; return decode_error; }
        p->remove_texture(); p->video_buffer.reset(buffer); p->video_size = sample.bytes.size();
    }
    std::memcpy(p->video_buffer.get(), sample.bytes.data(), sample.bytes.size());
    *info = {}; info->pData = p->video_buffer.get(); info->timestamp = sample.timestamp_us;
    info->width = sample.width; info->height = sample.height; info->pitch = sample.width * 4; info->pixelFormat = 1;
    p->metadata.width = sample.width; p->metadata.height = sample.height;
    auto pixels = std::make_shared<const std::vector<std::uint8_t>>(std::move(sample.bytes));
    {
        std::lock_guard texture_lock(texture_mutex);
        const auto address = reinterpret_cast<std::uintptr_t>(info->pData);
        texture_frames[address] = {address, ++generation, sample.width, sample.height, std::move(pixels)};
    }
    p->end_us = (std::max)(p->end_us, sample.timestamp_us + sample.duration_us);
    p->video_pending.reset();
    p->video_request = p->decoder->read(true);
    return 0;
}
GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetAudioData(SceAvPlayerHandle handle, SceAvPlayerAudioInfo* info) {
    if (!guest_memory::accessible(info, sizeof(*info), true)) return invalid;
    auto p = find(handle); if (!p) return invalid;
    std::lock_guard lock(p->mutex); if (p->closed) return invalid;
    const auto status = p->poll(false); if (status) return status;
    auto& sample = *p->audio_pending;
    p->audio_buffer = std::move(sample.bytes);
    *info = {}; info->pData = p->audio_buffer.data(); info->timestamp = sample.timestamp_us;
    info->size = static_cast<std::uint32_t>(p->audio_buffer.size());
    info->channelCount = p->metadata.channels; info->sampleRate = p->metadata.sample_rate;
    p->end_us = (std::max)(p->end_us, sample.timestamp_us + sample.duration_us);
    p->audio_pending.reset();
    p->audio_request = p->decoder->read(false);
    return 0;
}
}
}
