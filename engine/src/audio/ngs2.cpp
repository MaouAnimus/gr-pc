#include "hle/audio/ngs2.h"
#include "hle/audio/audio3d.h"
#include "hle/audio.h"
#include "hle/guest_memory.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gravity::hle::audio {

namespace {

constexpr std::int32_t SCE_OK = 0;
constexpr std::int32_t SCE_NGS2_ERROR_INVALID_PARAM = static_cast<std::int32_t>(0x805B0001U);

constexpr double SAMPLE_RATE = 48000.0;
constexpr double TWO_PI = 6.28318530717958647692;

struct VoiceState {
    std::uint32_t state{0}; // 0 = idle/stopped, 1 = playing, 2 = paused
    float volume{1.0f};
    float volume_l{1.0f};
    float volume_r{1.0f};
    float pitch{1.0f};
    float pan{0.0f}; // -1.0 (left) .. 0.0 (center) .. +1.0 (right)
    double phase{0.0};
    double base_frequency{440.0};

    const std::int16_t* pcm_data{nullptr};
    std::size_t pcm_frames{0};
    double pcm_cursor{0.0};
    bool loop{false};

    void update_stereo_gains() noexcept {
        constexpr float PI = 3.14159265358979323846f;
        constexpr float SQRT2 = 1.4142135623730950488f;
        const float clamped_pan = std::clamp(pan, -1.0f, 1.0f);
        const float angle = (clamped_pan + 1.0f) * (PI * 0.25f);
        volume_l = std::cos(angle) * SQRT2 * volume;
        volume_r = std::sin(angle) * SQRT2 * volume;
    }
};

struct RackInstance {
    std::uint32_t sys_handle{0};
    std::vector<VoiceState> voices;
};

struct SystemInstance {
    std::uint32_t handle{0};
    std::unordered_map<std::uint32_t, RackInstance> racks;
};

std::mutex g_ngs2_mutex;
std::unordered_map<std::uint32_t, SystemInstance> g_systems;
std::uint32_t g_next_sys = 1;
std::uint32_t g_next_rack = 1;

} // namespace

extern "C" {

GRAVITY_SYSV_ABI std::int32_t sceNgs2SystemCreateWithAllocator(
    const void* params, const void* alloc, std::uint32_t* out_sys_handle) {
    (void)params; (void)alloc;
    if (!out_sys_handle || !guest_memory::accessible(out_sys_handle, sizeof(std::uint32_t), true)) {
        return SCE_NGS2_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_ngs2_mutex);
    const std::uint32_t sys_h = g_next_sys++;
    SystemInstance sys;
    sys.handle = sys_h;
    g_systems[sys_h] = std::move(sys);
    guest_memory::write(out_sys_handle, &sys_h, sizeof(sys_h));
    std::fprintf(stderr, "[NGS2] sceNgs2SystemCreateWithAllocator: sys_handle=%u\n", sys_h);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2SystemDestroy(std::uint32_t sys_handle) {
    std::lock_guard lock(g_ngs2_mutex);
    auto it = g_systems.find(sys_handle);
    if (it == g_systems.end()) return SCE_NGS2_ERROR_INVALID_PARAM;
    g_systems.erase(it);
    std::fprintf(stderr, "[NGS2] sceNgs2SystemDestroy: sys_handle=%u\n", sys_handle);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackQueryBufferSize(
    const void* params, std::size_t* out_size) {
    (void)params;
    if (!out_size || !guest_memory::accessible(out_size, sizeof(std::size_t), true)) {
        return SCE_NGS2_ERROR_INVALID_PARAM;
    }
    const std::size_t required = 65536; // Standard 64 KB rack work buffer
    guest_memory::write(out_size, &required, sizeof(required));
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackCreate(
    std::uint32_t sys_handle, const void* params, void* buffer,
    std::size_t buffer_size, std::uint32_t* out_rack_handle) {
    (void)params; (void)buffer; (void)buffer_size;
    if (!out_rack_handle || !guest_memory::accessible(out_rack_handle, sizeof(std::uint32_t), true)) {
        return SCE_NGS2_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_ngs2_mutex);
    auto it = g_systems.find(sys_handle);
    if (it == g_systems.end()) return SCE_NGS2_ERROR_INVALID_PARAM;

    const std::uint32_t rack_h = g_next_rack++;
    RackInstance rack;
    rack.sys_handle = sys_handle;
    rack.voices.resize(32);
    for (std::size_t i = 0; i < rack.voices.size(); ++i) {
        rack.voices[i].base_frequency = 220.0 * std::pow(2.0, static_cast<double>(i % 12) / 12.0);
        rack.voices[i].update_stereo_gains();
    }
    it->second.racks[rack_h] = std::move(rack);
    guest_memory::write(out_rack_handle, &rack_h, sizeof(rack_h));
    std::fprintf(stderr, "[NGS2] sceNgs2RackCreate: rack_handle=%u in sys=%u\n", rack_h, sys_handle);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackCreateWithAllocator(
    std::uint32_t sys_handle, const void* params, const void* alloc,
    std::uint32_t* out_rack_handle) {
    (void)alloc;
    return sceNgs2RackCreate(sys_handle, params, nullptr, 0, out_rack_handle);
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackDestroy(std::uint32_t rack_handle) {
    std::lock_guard lock(g_ngs2_mutex);
    for (auto& [sys_h, sys] : g_systems) {
        auto it = sys.racks.find(rack_handle);
        if (it != sys.racks.end()) {
            sys.racks.erase(it);
            std::fprintf(stderr, "[NGS2] sceNgs2RackDestroy: rack_handle=%u\n", rack_handle);
            return SCE_OK;
        }
    }
    return SCE_NGS2_ERROR_INVALID_PARAM;
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackGetVoiceHandle(
    std::uint32_t rack_handle, std::uint32_t voice_index, std::uint32_t* out_voice_handle) {
    if (!out_voice_handle || !guest_memory::accessible(out_voice_handle, sizeof(std::uint32_t), true)) {
        return SCE_NGS2_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_ngs2_mutex);
    for (auto& [sys_h, sys] : g_systems) {
        auto it = sys.racks.find(rack_handle);
        if (it != sys.racks.end()) {
            if (voice_index >= it->second.voices.size()) return SCE_NGS2_ERROR_INVALID_PARAM;
            const std::uint32_t voice_h = (rack_handle << 16) | (voice_index & 0xFFFF);
            guest_memory::write(out_voice_handle, &voice_h, sizeof(voice_h));
            return SCE_OK;
        }
    }
    return SCE_NGS2_ERROR_INVALID_PARAM;
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2VoiceControl(
    std::uint32_t voice_handle, std::uint32_t command_id, const void* params) {
    const std::uint32_t rack_handle = voice_handle >> 16;
    const std::uint32_t voice_idx = voice_handle & 0xFFFF;

    std::lock_guard lock(g_ngs2_mutex);
    for (auto& [sys_h, sys] : g_systems) {
        auto it = sys.racks.find(rack_handle);
        if (it != sys.racks.end()) {
            if (voice_idx < it->second.voices.size()) {
                auto& v = it->second.voices[voice_idx];
                switch (command_id) {
                case SCE_NGS2_VOICE_COMMAND_PLAY: // 0: Play
                    v.state = 1;
                    break;
                case SCE_NGS2_VOICE_COMMAND_PAUSE: // 1: Pause
                    v.state = 2;
                    break;
                case SCE_NGS2_VOICE_COMMAND_STOP: // 2: Stop
                    v.state = 0;
                    v.phase = 0.0;
                    v.pcm_cursor = 0.0;
                    break;
                case SCE_NGS2_VOICE_COMMAND_SET_VOLUME: // 3: Set Volume
                    if (params && guest_memory::accessible(params, sizeof(float), false)) {
                        if (guest_memory::accessible(params, sizeof(float) * 2, false)) {
                            float vols[2]{};
                            if (guest_memory::read(params, vols, sizeof(vols))) {
                                v.volume_l = std::max(0.0f, vols[0]);
                                v.volume_r = std::max(0.0f, vols[1]);
                                v.volume = (v.volume_l + v.volume_r) * 0.5f;
                            }
                        } else {
                            float vol = 1.0f;
                            if (guest_memory::read(params, &vol, sizeof(vol))) {
                                v.volume = std::max(0.0f, vol);
                                v.update_stereo_gains();
                            }
                        }
                    }
                    break;
                case SCE_NGS2_VOICE_COMMAND_SET_PITCH: // 4: Set Pitch
                    if (params && guest_memory::accessible(params, sizeof(float), false)) {
                        float p = 1.0f;
                        if (guest_memory::read(params, &p, sizeof(p)) && p > 0.0001f && p <= 16.0f) {
                            v.pitch = p;
                        }
                    }
                    break;
                case SCE_NGS2_VOICE_COMMAND_SET_PAN: // 5: Set Pan
                    if (params && guest_memory::accessible(params, sizeof(float), false)) {
                        float p = 0.0f;
                        if (guest_memory::read(params, &p, sizeof(p))) {
                            v.pan = std::clamp(p, -1.0f, 1.0f);
                            v.update_stereo_gains();
                        }
                    }
                    break;
                case SCE_NGS2_VOICE_COMMAND_SET_BUFFER: // 6: Set Buffer
                    if (params && guest_memory::accessible(params, sizeof(SceNgs2VoiceBufferParams), false)) {
                        SceNgs2VoiceBufferParams bp{};
                        if (guest_memory::read(params, &bp, sizeof(bp))) {
                            v.pcm_data = static_cast<const std::int16_t*>(bp.data);
                            v.pcm_frames = bp.size_bytes / sizeof(std::int16_t);
                            v.loop = (bp.loop != 0);
                            v.pcm_cursor = 0.0;
                        }
                    }
                    break;
                case SCE_NGS2_VOICE_COMMAND_SET_FREQ: // 7: Set Frequency
                    if (params && guest_memory::accessible(params, sizeof(float), false)) {
                        float freq = 440.0f;
                        if (guest_memory::read(params, &freq, sizeof(freq)) && freq > 10.0f && freq <= 22000.0f) {
                            v.base_frequency = static_cast<double>(freq);
                        }
                    }
                    break;
                case SCE_NGS2_VOICE_COMMAND_SET_3D_POS: // 8: Set 3D Position
                    if (params && guest_memory::accessible(params, sizeof(float) * 3, false)) {
                        float pos[3]{};
                        if (guest_memory::read(params, pos, sizeof(pos))) {
                            Audio3DVec3 listener{};
                            Audio3DVec3 fwd{}, up{};
                            get_audio3d_listener(listener, fwd, up);

                            const float dx = pos[0] - listener.x;
                            const float dy = pos[1] - listener.y;
                            const float dz = pos[2] - listener.z;
                            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                            float dist_gain = 1.0f;
                            constexpr float MIN_DIST = 1.0f;
                            constexpr float MAX_DIST = 100.0f;
                            if (dist <= MIN_DIST) {
                                dist_gain = 1.0f;
                            } else if (dist >= MAX_DIST) {
                                dist_gain = 0.0f;
                            } else {
                                dist_gain = MIN_DIST / (MIN_DIST + (dist - MIN_DIST));
                            }
                            v.volume = dist_gain;

                            const float dist_xz = std::sqrt(dx * dx + dz * dz);
                            if (dist_xz > 1e-4f) {
                                v.pan = std::clamp(dx / dist_xz, -1.0f, 1.0f);
                            } else {
                                v.pan = 0.0f;
                            }
                            v.update_stereo_gains();
                        }
                    }
                    break;
                default:
                    break;
                }
                return SCE_OK;
            }
        }
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2VoiceGetState(
    std::uint32_t voice_handle, std::uint32_t* out_state) {
    if (!out_state || !guest_memory::accessible(out_state, sizeof(std::uint32_t), true)) {
        return SCE_NGS2_ERROR_INVALID_PARAM;
    }
    const std::uint32_t rack_handle = voice_handle >> 16;
    const std::uint32_t voice_idx = voice_handle & 0xFFFF;

    std::lock_guard lock(g_ngs2_mutex);
    for (auto& [sys_h, sys] : g_systems) {
        auto it = sys.racks.find(rack_handle);
        if (it != sys.racks.end()) {
            if (voice_idx < it->second.voices.size()) {
                const std::uint32_t st = it->second.voices[voice_idx].state;
                guest_memory::write(out_state, &st, sizeof(st));
                return SCE_OK;
            }
        }
    }
    const std::uint32_t zero = 0;
    guest_memory::write(out_state, &zero, sizeof(zero));
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceNgs2SystemRender(
    std::uint32_t sys_handle, void* buffer, std::uint32_t num_samples) {
    if (!buffer || num_samples == 0 ||
        !guest_memory::accessible(buffer, num_samples * sizeof(std::int16_t) * 2, true)) {
        return SCE_NGS2_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_ngs2_mutex);
    auto it = g_systems.find(sys_handle);
    if (it == g_systems.end()) return SCE_NGS2_ERROR_INVALID_PARAM;

    std::vector<float> mix_l(num_samples, 0.0f);
    std::vector<float> mix_r(num_samples, 0.0f);

    for (auto& [rack_h, rack] : it->second.racks) {
        for (auto& v : rack.voices) {
            if (v.state != 1) continue; // Only process playing voices

            if (v.pcm_data != nullptr && v.pcm_frames > 0) {
                // PCM buffer playback
                for (std::uint32_t i = 0; i < num_samples; ++i) {
                    const std::size_t idx = static_cast<std::size_t>(v.pcm_cursor);
                    if (idx >= v.pcm_frames) {
                        if (v.loop) {
                            v.pcm_cursor = std::fmod(v.pcm_cursor, static_cast<double>(v.pcm_frames));
                        } else {
                            v.state = 0; // finished
                            break;
                        }
                    }
                    const std::size_t cur_idx = static_cast<std::size_t>(v.pcm_cursor);
                    if (cur_idx < v.pcm_frames) {
                        const float sample = static_cast<float>(v.pcm_data[cur_idx]);
                        mix_l[i] += sample * v.volume_l;
                        mix_r[i] += sample * v.volume_r;
                    }
                    v.pcm_cursor += static_cast<double>(v.pitch);
                }
            } else {
                // Waveform synthesis
                const double d_phase = (v.base_frequency * static_cast<double>(v.pitch)) / SAMPLE_RATE;
                for (std::uint32_t i = 0; i < num_samples; ++i) {
                    const float sample = static_cast<float>(std::sin(v.phase * TWO_PI) * 12000.0);
                    mix_l[i] += sample * v.volume_l;
                    mix_r[i] += sample * v.volume_r;
                    v.phase += d_phase;
                    if (v.phase >= 1.0) {
                        v.phase -= 1.0;
                    }
                }
            }
        }
    }

    // Convert and clamp to 16-bit signed stereo PCM
    std::int16_t* out = static_cast<std::int16_t*>(buffer);
    for (std::uint32_t i = 0; i < num_samples; ++i) {
        const float clamped_l = std::clamp(mix_l[i], -32768.0f, 32767.0f);
        const float clamped_r = std::clamp(mix_r[i], -32768.0f, 32767.0f);
        out[i * 2 + 0] = static_cast<std::int16_t>(clamped_l);
        out[i * 2 + 1] = static_cast<std::int16_t>(clamped_r);
    }

    // Feed rendered audio to WASAPI host renderer
    output_master_audio(buffer, num_samples * sizeof(std::int16_t) * 2);

    return SCE_OK;
}

} // extern "C"

} // namespace gravity::hle::audio
