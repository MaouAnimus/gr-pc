#pragma once
#include "hle/abi.h"
#include <cstdint>
namespace gravity::hle::sema {
using Handle = std::uint64_t;
extern "C" {
GRAVITY_SYSV_ABI std::int32_t sceKernelCreateSema(Handle* output, const char* name, std::uint32_t attributes,
    std::int32_t initial, std::int32_t maximum, const void* options);
GRAVITY_SYSV_ABI std::int32_t sceKernelDeleteSema(Handle handle);
GRAVITY_SYSV_ABI std::int32_t sceKernelWaitSema(Handle handle, std::int32_t count, std::uint32_t* timeout_us);
GRAVITY_SYSV_ABI std::int32_t sceKernelSignalSema(Handle handle, std::int32_t count);
GRAVITY_SYSV_ABI std::int32_t sceKernelCancelSema(Handle handle, std::int32_t count, std::int32_t* waiters);
}
}
