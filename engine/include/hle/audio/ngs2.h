#pragma once

#include "hle/abi.h"
#include <cstdint>
#include <cstddef>

namespace gravity::hle::audio {

extern "C" {

GRAVITY_SYSV_ABI std::int32_t sceNgs2SystemCreateWithAllocator(
    const void* params, const void* alloc, std::uint32_t* out_sys_handle);

GRAVITY_SYSV_ABI std::int32_t sceNgs2SystemDestroy(std::uint32_t sys_handle);

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackQueryBufferSize(
    const void* params, std::size_t* out_size);

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackCreate(
    std::uint32_t sys_handle, const void* params, void* buffer,
    std::size_t buffer_size, std::uint32_t* out_rack_handle);

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackCreateWithAllocator(
    std::uint32_t sys_handle, const void* params, const void* alloc,
    std::uint32_t* out_rack_handle);

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackDestroy(std::uint32_t rack_handle);

GRAVITY_SYSV_ABI std::int32_t sceNgs2RackGetVoiceHandle(
    std::uint32_t rack_handle, std::uint32_t voice_index, std::uint32_t* out_voice_handle);

GRAVITY_SYSV_ABI std::int32_t sceNgs2VoiceControl(
    std::uint32_t voice_handle, std::uint32_t command_id, const void* params);

GRAVITY_SYSV_ABI std::int32_t sceNgs2VoiceGetState(
    std::uint32_t voice_handle, std::uint32_t* out_state);

GRAVITY_SYSV_ABI std::int32_t sceNgs2SystemRender(
    std::uint32_t sys_handle, void* buffer, std::uint32_t num_samples);

} // extern "C"

} // namespace gravity::hle::audio
