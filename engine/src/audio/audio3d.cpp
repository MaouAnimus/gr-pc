#include "hle/audio/audio3d.h"
#include "hle/guest_memory.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace gravity::hle::audio {

namespace {

constexpr std::int32_t SCE_OK = 0;
constexpr std::int32_t SCE_AUDIO3D_ERROR_INVALID_PARAM = static_cast<std::int32_t>(0x805C0001U);

struct PortInstance {
    std::uint32_t handle{0};
    std::unordered_map<std::uint32_t, Audio3DAttributes> objects;
};

std::mutex g_audio3d_mutex;
bool g_audio3d_initialized{false};
std::unordered_map<std::uint32_t, PortInstance> g_ports;
std::uint32_t g_next_port = 1;
std::uint32_t g_next_obj = 1;

Audio3DVec3 g_listener_pos{0.0f, 0.0f, 0.0f};
Audio3DVec3 g_listener_forward{0.0f, 0.0f, 1.0f};
Audio3DVec3 g_listener_up{0.0f, 1.0f, 0.0f};

} // namespace

void set_audio3d_listener(const Audio3DVec3& pos, const Audio3DVec3& forward, const Audio3DVec3& up) noexcept {
    std::lock_guard lock(g_audio3d_mutex);
    g_listener_pos = pos;
    g_listener_forward = forward;
    g_listener_up = up;
}

void get_audio3d_listener(Audio3DVec3& out_pos, Audio3DVec3& out_forward, Audio3DVec3& out_up) noexcept {
    std::lock_guard lock(g_audio3d_mutex);
    out_pos = g_listener_pos;
    out_forward = g_listener_forward;
    out_up = g_listener_up;
}

bool get_audio3d_object_attributes(
    std::uint32_t port_handle, std::uint32_t obj_handle, Audio3DAttributes& out_attrs) noexcept {
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return false;
    auto obj_it = it->second.objects.find(obj_handle);
    if (obj_it == it->second.objects.end()) return false;
    out_attrs = obj_it->second;
    return true;
}

bool compute_audio3d_spatial_gains_relative(
    std::uint32_t port_handle, std::uint32_t obj_handle,
    const Audio3DVec3& listener_pos,
    float& out_gain_left, float& out_gain_right) noexcept {
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return false;
    auto obj_it = it->second.objects.find(obj_handle);
    if (obj_it == it->second.objects.end()) return false;

    const auto& attrs = obj_it->second;
    const float dx = attrs.position.x - listener_pos.x;
    const float dy = attrs.position.y - listener_pos.y;
    const float dz = attrs.position.z - listener_pos.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Distance attenuation
    float dist_gain = 1.0f;
    if (dist <= attrs.min_dist) {
        dist_gain = 1.0f;
    } else if (dist >= attrs.max_dist) {
        dist_gain = 0.0f;
    } else {
        dist_gain = attrs.min_dist / (attrs.min_dist + (dist - attrs.min_dist) * attrs.roll_off);
    }
    const float effective_gain = attrs.gain * dist_gain;

    // Azimuth panning on horizontal plane (dx, dz)
    const float dist_xz = std::sqrt(dx * dx + dz * dz);
    float pan = 0.0f;
    if (dist_xz > 1e-4f) {
        pan = std::clamp(dx / dist_xz, -1.0f, 1.0f);
    }

    constexpr float PI = 3.14159265358979323846f;
    constexpr float SQRT2 = 1.4142135623730950488f;
    const float angle = (pan + 1.0f) * (PI * 0.25f);
    out_gain_left = std::cos(angle) * SQRT2 * effective_gain;
    out_gain_right = std::sin(angle) * SQRT2 * effective_gain;
    return true;
}

bool compute_audio3d_spatial_gains(
    std::uint32_t port_handle, std::uint32_t obj_handle,
    float& out_gain_left, float& out_gain_right) noexcept {
    Audio3DVec3 listener;
    {
        std::lock_guard lock(g_audio3d_mutex);
        listener = g_listener_pos;
    }
    return compute_audio3d_spatial_gains_relative(port_handle, obj_handle, listener, out_gain_left, out_gain_right);
}

extern "C" {

GRAVITY_SYSV_ABI std::int32_t sceAudio3dInitialize(const void* params) {
    (void)params;
    std::lock_guard lock(g_audio3d_mutex);
    g_audio3d_initialized = true;
    std::fprintf(stderr, "[Audio3D] sceAudio3dInitialize (SCE_OK)\n");
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dTerminate() {
    std::lock_guard lock(g_audio3d_mutex);
    g_ports.clear();
    g_audio3d_initialized = false;
    std::fprintf(stderr, "[Audio3D] sceAudio3dTerminate (SCE_OK)\n");
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortOpen(const void* params, std::uint32_t* out_port_handle) {
    (void)params;
    if (!out_port_handle || !guest_memory::accessible(out_port_handle, sizeof(std::uint32_t), true)) {
        return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_audio3d_mutex);
    if (!g_audio3d_initialized) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    const std::uint32_t handle = g_next_port++;
    PortInstance inst;
    inst.handle = handle;
    g_ports[handle] = std::move(inst);
    guest_memory::write(out_port_handle, &handle, sizeof(handle));
    std::fprintf(stderr, "[Audio3D] sceAudio3dPortOpen: port=%u\n", handle);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortClose(std::uint32_t port_handle) {
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    g_ports.erase(it);
    std::fprintf(stderr, "[Audio3D] sceAudio3dPortClose: port=%u\n", port_handle);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortFlush(std::uint32_t port_handle) {
    std::lock_guard lock(g_audio3d_mutex);
    if (!g_ports.contains(port_handle)) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectReserve(std::uint32_t port_handle, std::uint32_t* out_obj_handle) {
    if (!out_obj_handle || !guest_memory::accessible(out_obj_handle, sizeof(std::uint32_t), true)) {
        return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;

    const std::uint32_t obj = g_next_obj++;
    Audio3DAttributes attrs{};
    it->second.objects[obj] = attrs;
    guest_memory::write(out_obj_handle, &obj, sizeof(obj));
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectUnreserve(std::uint32_t port_handle, std::uint32_t obj_handle) {
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    it->second.objects.erase(obj_handle);
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectSetAttributes(
    std::uint32_t port_handle, std::uint32_t obj_handle, const void* attrs) {
    if (!attrs) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    auto obj_it = it->second.objects.find(obj_handle);
    if (obj_it == it->second.objects.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;

    if (guest_memory::accessible(attrs, sizeof(float) * 3, false)) {
        float pos[3]{};
        if (guest_memory::read(attrs, pos, sizeof(pos))) {
            obj_it->second.position.x = pos[0];
            obj_it->second.position.y = pos[1];
            obj_it->second.position.z = pos[2];
        }
        if (guest_memory::accessible(attrs, sizeof(float) * 4, false)) {
            float gain = 1.0f;
            const void* gain_ptr = static_cast<const char*>(attrs) + sizeof(float) * 3;
            if (guest_memory::read(gain_ptr, &gain, sizeof(gain)) && gain >= 0.0f) {
                obj_it->second.gain = gain;
            }
        }
    }
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectGetAttributes(
    std::uint32_t port_handle, std::uint32_t obj_handle, void* out_attrs) {
    if (!out_attrs || !guest_memory::accessible(out_attrs, sizeof(float) * 4, true)) {
        return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    }
    std::lock_guard lock(g_audio3d_mutex);
    auto it = g_ports.find(port_handle);
    if (it == g_ports.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;
    auto obj_it = it->second.objects.find(obj_handle);
    if (obj_it == it->second.objects.end()) return SCE_AUDIO3D_ERROR_INVALID_PARAM;

    float vals[4] = {
        obj_it->second.position.x,
        obj_it->second.position.y,
        obj_it->second.position.z,
        obj_it->second.gain
    };
    guest_memory::write(out_attrs, vals, sizeof(vals));
    return SCE_OK;
}

} // extern "C"

} // namespace gravity::hle::audio
