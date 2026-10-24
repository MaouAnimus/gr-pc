#pragma once

#include "hle/abi.h"
#include <cstdint>

namespace gravity::hle::audio {

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

} // extern "C"

} // namespace gravity::hle::audio
