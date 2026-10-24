#pragma once
#include "hle/abi.h"
#include <cstddef>
#include <cstdint>
namespace gravity::hle::direct_memory {
inline constexpr std::size_t pool_size = 4ULL * 1024 * 1024 * 1024;
extern "C" {
GRAVITY_SYSV_ABI std::size_t sceKernelGetDirectMemorySize();
GRAVITY_SYSV_ABI std::int32_t sceKernelAllocateDirectMemory(std::int64_t begin, std::int64_t end,
    std::size_t length, std::size_t alignment, std::int32_t type, std::int64_t* output);
GRAVITY_SYSV_ABI std::int32_t sceKernelMapDirectMemory(void** address, std::size_t length,
    std::int32_t protection, std::int32_t flags, std::int64_t offset, std::size_t alignment);
GRAVITY_SYSV_ABI std::int32_t sceKernelReleaseDirectMemory(std::int64_t offset, std::size_t length);
}
}
