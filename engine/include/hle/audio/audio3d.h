#pragma once

#include "hle/abi.h"
#include <cstdint>

namespace gravity::hle::audio {

struct Audio3DVec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct Audio3DAttributes {
    Audio3DVec3 position{};
    Audio3DVec3 velocity{};
    float gain{1.0f};
    float min_dist{1.0f};
    float max_dist{100.0f};
    float roll_off{1.0f};
};

// C++ API for spatial calculations and listener control
void set_audio3d_listener(const Audio3DVec3& pos, const Audio3DVec3& forward, const Audio3DVec3& up) noexcept;
void get_audio3d_listener(Audio3DVec3& out_pos, Audio3DVec3& out_forward, Audio3DVec3& out_up) noexcept;

bool get_audio3d_object_attributes(
    std::uint32_t port_handle, std::uint32_t obj_handle, Audio3DAttributes& out_attrs) noexcept;

bool compute_audio3d_spatial_gains(
    std::uint32_t port_handle, std::uint32_t obj_handle,
    float& out_gain_left, float& out_gain_right) noexcept;

bool compute_audio3d_spatial_gains_relative(
    std::uint32_t port_handle, std::uint32_t obj_handle,
    const Audio3DVec3& listener_pos,
    float& out_gain_left, float& out_gain_right) noexcept;

extern "C" {

GRAVITY_SYSV_ABI std::int32_t sceAudio3dInitialize(const void* params);
GRAVITY_SYSV_ABI std::int32_t sceAudio3dTerminate();
GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortOpen(const void* params, std::uint32_t* out_port_handle);
GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortClose(std::uint32_t port_handle);
GRAVITY_SYSV_ABI std::int32_t sceAudio3dPortFlush(std::uint32_t port_handle);
GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectReserve(std::uint32_t port_handle, std::uint32_t* out_obj_handle);
GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectUnreserve(std::uint32_t port_handle, std::uint32_t obj_handle);
GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectSetAttributes(
    std::uint32_t port_handle, std::uint32_t obj_handle, const void* attrs);
GRAVITY_SYSV_ABI std::int32_t sceAudio3dObjectGetAttributes(
    std::uint32_t port_handle, std::uint32_t obj_handle, void* out_attrs);

} // extern "C"

} // namespace gravity::hle::audio
