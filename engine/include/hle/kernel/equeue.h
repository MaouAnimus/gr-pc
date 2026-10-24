#pragma once
#include "hle/abi.h"
#include <cstddef>
#include <cstdint>

namespace gravity::hle::equeue {
using Handle = std::uint64_t;
struct Event {
    std::uint64_t ident{};
    std::int16_t filter{};
    std::uint16_t flags{};
    std::uint32_t fflags{};
    std::int64_t data{};
    void* udata{};
};
static_assert(sizeof(Event) == 32 && offsetof(Event, data) == 16);
inline constexpr std::int32_t bad_handle = static_cast<std::int32_t>(0x80020009U);
inline constexpr std::int32_t invalid = static_cast<std::int32_t>(0x80020016U);
inline constexpr std::int32_t fault = static_cast<std::int32_t>(0x8002000eU);
inline constexpr std::int32_t timed_out = static_cast<std::int32_t>(0x8002003cU);
inline constexpr std::int32_t canceled = static_cast<std::int32_t>(0x80020055U);
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelCreateEqueue(Handle* queue, const char* name);
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelDeleteEqueue(Handle queue);
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelWaitEqueue(
    Handle queue, Event* events, std::int32_t capacity, std::int32_t* count, const std::uint32_t* timeout_us);
// Backend producers call this only for completed events; never synthesize GPU completion.
bool post(Handle queue, const Event& event);
}
