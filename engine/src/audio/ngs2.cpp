#include "hle/audio/ngs2.h"
#include "hle/guest_memory.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gravity::hle::audio {

namespace {

constexpr std::int32_t SCE_OK = 0;
constexpr std::int32_t SCE_NGS2_ERROR_INVALID_PARAM = static_cast<std::int32_t>(0x805B0001U);

struct VoiceState {
    std::uint32_t state{0}; // 0 = idle, 1 = playing
    float volume{1.0f};
    float pitch{1.0f};
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
    rack.voices.resize(32); // 32 voice channels per rack
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
    (void)command_id; (void)params;
    const std::uint32_t rack_handle = voice_handle >> 16;
    const std::uint32_t voice_idx = voice_handle & 0xFFFF;

    std::lock_guard lock(g_ngs2_mutex);
    for (auto& [sys_h, sys] : g_systems) {
        auto it = sys.racks.find(rack_handle);
        if (it != sys.racks.end()) {
            if (voice_idx < it->second.voices.size()) {
                // Command: 0 = play, 1 = pause, 2 = stop, etc.
                if (command_id == 0) it->second.voices[voice_idx].state = 1;
                else if (command_id == 2) it->second.voices[voice_idx].state = 0;
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
    if (!buffer || !guest_memory::accessible(buffer, num_samples * 4, true)) {
        return SCE_NGS2_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_ngs2_mutex);
    auto it = g_systems.find(sys_handle);
    if (it == g_systems.end()) return SCE_NGS2_ERROR_INVALID_PARAM;

    // Fill with silence PCM
    std::memset(buffer, 0, num_samples * 4);
    return SCE_OK;
}

} // extern "C"

} // namespace gravity::hle::audio
