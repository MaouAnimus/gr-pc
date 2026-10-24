#include "hle/avplayer/avplayer.h"
#include "hle/guest_memory.h"
#include "hle/fs.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gravity::hle::avplayer {

namespace {

constexpr std::int32_t SCE_OK = 0;
constexpr std::int32_t SCE_AVPLAYER_ERROR_INVALID_PARAM = static_cast<std::int32_t>(0x80540001U);
constexpr std::int32_t SCE_AVPLAYER_ERROR_NO_DATA = static_cast<std::int32_t>(0x80540002U);

struct PlayerInstance {
    bool active{false};
    bool looping{false};
    bool video_enabled{true};
    bool audio_enabled{true};
    std::string source{};
    std::uint32_t width{1920};
    std::uint32_t height{1080};
    std::uint64_t start_time_us{0};
    std::uint64_t current_pts{0};
    std::vector<std::uint8_t> video_buffer;
    std::vector<std::uint8_t> audio_buffer;

    PlayerInstance() {
        video_buffer.resize(1920 * 1080 * 4, 0); // 1080p RGBA blank frame
        audio_buffer.resize(48000 / 60 * 4, 0);  // 1 frame of 48kHz stereo 16-bit PCM silence
    }
};

std::mutex g_player_mutex;
std::unordered_map<SceAvPlayerHandle, PlayerInstance> g_players;
std::uintptr_t g_next_handle = 0x20000;

SceAvPlayerHandle allocate_handle() {
    return reinterpret_cast<SceAvPlayerHandle>(g_next_handle += 0x10);
}

} // namespace

extern "C" {

GRAVITY_SYSV_ABI SceAvPlayerHandle sceAvPlayerInit(const SceAvPlayerInitData* initData) {
    (void)initData;
    std::lock_guard lock(g_player_mutex);
    const auto handle = allocate_handle();
    g_players[handle] = PlayerInstance{};
    std::fprintf(stderr, "[AvPlayer] sceAvPlayerInit: handle=%p\n", handle);
    return handle;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerPostInit(SceAvPlayerHandle player, const void* reserved) {
    (void)reserved;
    std::lock_guard lock(g_player_mutex);
    if (!g_players.contains(player)) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerClose(SceAvPlayerHandle player) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    std::fprintf(stderr, "[AvPlayer] sceAvPlayerClose: handle=%p\n", player);
    g_players.erase(it);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerAddSource(SceAvPlayerHandle player, const char* filename) {
    if (!filename) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    char path_buf[512]{};
    if (!guest_memory::string(filename, path_buf, sizeof(path_buf))) {
        return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    }

    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;

    it->second.source = path_buf;
    std::fprintf(stderr, "[AvPlayer] sceAvPlayerAddSource: handle=%p, source='%s'\n", player, path_buf);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerSetLooping(SceAvPlayerHandle player, std::uint32_t loop) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    it->second.looping = (loop != 0);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerStart(SceAvPlayerHandle player) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    it->second.active = true;
    const auto now = std::chrono::steady_clock::now();
    it->second.start_time_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
    it->second.current_pts = 0;
    std::fprintf(stderr, "[AvPlayer] sceAvPlayerStart: handle=%p\n", player);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerStop(SceAvPlayerHandle player) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    it->second.active = false;
    std::fprintf(stderr, "[AvPlayer] sceAvPlayerStop: handle=%p\n", player);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerPause(SceAvPlayerHandle player) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    it->second.active = false;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerResume(SceAvPlayerHandle player) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    it->second.active = true;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerIsActive(SceAvPlayerHandle player) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return 0;
    return it->second.active ? 1 : 0;
}

GRAVITY_SYSV_ABI std::uint32_t sceAvPlayerStreamCount(SceAvPlayerHandle player) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return 0;
    // 2 streams: 0 = video, 1 = audio
    return 2;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetStreamInfo(
    SceAvPlayerHandle player, std::uint32_t streamIndex, SceAvPlayerStreamInfo* info) {
    if (!info || !guest_memory::accessible(info, sizeof(SceAvPlayerStreamInfo), true)) {
        return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;

    if (streamIndex == 0) {
        // Video Stream
        info->type = 0; // video
        info->details = 0;
        info->width = it->second.width;
        info->height = it->second.height;
        info->framerate = 60.0f;
        info->channelCount = 0;
        info->sampleRate = 0;
        info->languageCode = 0;
        return SCE_OK;
    } else if (streamIndex == 1) {
        // Audio Stream
        info->type = 1; // audio
        info->details = 0;
        info->width = 0;
        info->height = 0;
        info->framerate = 0.0f;
        info->channelCount = 2;
        info->sampleRate = 48000;
        info->languageCode = 0;
        return SCE_OK;
    }
    return SCE_AVPLAYER_ERROR_INVALID_PARAM;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerEnableStream(
    SceAvPlayerHandle player, std::uint32_t streamIndex) {
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;

    if (streamIndex == 0) it->second.video_enabled = true;
    else if (streamIndex == 1) it->second.audio_enabled = true;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetVideoDataEx(
    SceAvPlayerHandle player, SceAvPlayerFrameInfoEx* frameInfo) {
    if (!frameInfo || !guest_memory::accessible(frameInfo, sizeof(SceAvPlayerFrameInfoEx), true)) {
        return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    if (!it->second.active || !it->second.video_enabled) return SCE_AVPLAYER_ERROR_NO_DATA;

    it->second.current_pts += 16666; // 60 fps step in microseconds
    frameInfo->pData = it->second.video_buffer.data();
    frameInfo->timestamp = it->second.current_pts;
    frameInfo->details = 0;
    frameInfo->width = it->second.width;
    frameInfo->height = it->second.height;
    frameInfo->pitch = it->second.width * 4;
    frameInfo->pixelFormat = 1; // RGBA
    frameInfo->aspectRatio = 0;

    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAvPlayerGetAudioData(
    SceAvPlayerHandle player, SceAvPlayerAudioInfo* audioInfo) {
    if (!audioInfo || !guest_memory::accessible(audioInfo, sizeof(SceAvPlayerAudioInfo), true)) {
        return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_player_mutex);
    auto it = g_players.find(player);
    if (it == g_players.end()) return SCE_AVPLAYER_ERROR_INVALID_PARAM;
    if (!it->second.active || !it->second.audio_enabled) return SCE_AVPLAYER_ERROR_NO_DATA;

    audioInfo->pData = it->second.audio_buffer.data();
    audioInfo->timestamp = it->second.current_pts;
    audioInfo->size = static_cast<std::uint32_t>(it->second.audio_buffer.size());
    audioInfo->channelCount = 2;
    audioInfo->sampleRate = 48000;

    return SCE_OK;
}

} // extern "C"

} // namespace gravity::hle::avplayer
