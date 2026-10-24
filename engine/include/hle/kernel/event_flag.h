#pragma once
#include "hle/abi.h"
#include <cstdint>

namespace gravity::hle::event_flag {
// Guest handles occupy a pointer-sized slot; they are opaque registry tokens.
using Handle = std::uint64_t;
inline constexpr std::int32_t missing = static_cast<std::int32_t>(0x80020003U);
inline constexpr std::int32_t invalid = static_cast<std::int32_t>(0x80020016U);
inline constexpr std::int32_t fault = static_cast<std::int32_t>(0x8002000eU);
inline constexpr std::int32_t busy = static_cast<std::int32_t>(0x80020010U);
inline constexpr std::int32_t permission = static_cast<std::int32_t>(0x80020001U);
inline constexpr std::int32_t deleted = static_cast<std::int32_t>(0x8002000dU);
inline constexpr std::int32_t timed_out = static_cast<std::int32_t>(0x8002003cU);
inline constexpr std::int32_t canceled = static_cast<std::int32_t>(0x80020055U);
inline constexpr std::uint32_t fifo = 0x01, priority = 0x02, single = 0x10, multi = 0x20;
inline constexpr std::uint32_t wait_all = 0x01, wait_any = 0x02, clear_all = 0x10, clear_pattern = 0x20;
extern "C" {
GRAVITY_SYSV_ABI std::int32_t sceKernelCreateEventFlag(Handle* output, const char* name,
    std::uint32_t attributes, std::uint64_t initial, const void* options);
GRAVITY_SYSV_ABI std::int32_t sceKernelDeleteEventFlag(Handle handle);
GRAVITY_SYSV_ABI std::int32_t sceKernelCloseEventFlag(Handle handle);
GRAVITY_SYSV_ABI std::int32_t sceKernelSetEventFlag(Handle handle, std::uint64_t pattern);
// The guest API retains the supplied mask: bits &= pattern.
GRAVITY_SYSV_ABI std::int32_t sceKernelClearEventFlag(Handle handle, std::uint64_t pattern);
GRAVITY_SYSV_ABI std::int32_t sceKernelWaitEventFlag(Handle handle, std::uint64_t pattern,
    std::uint32_t mode, std::uint64_t* observed, std::uint32_t* timeout_us);
GRAVITY_SYSV_ABI std::int32_t sceKernelPollEventFlag(Handle handle, std::uint64_t pattern,
    std::uint32_t mode, std::uint64_t* observed);
GRAVITY_SYSV_ABI std::int32_t sceKernelCancelEventFlag(Handle handle, std::uint64_t pattern,
    std::int32_t* waiters);
}
void reset();
}
