#pragma once
#include "hle/abi.h"
#include <cstddef>
#include <cstdint>
namespace gravity::hle::kernel_file {
extern "C" {
GRAVITY_SYSV_ABI std::int32_t sceKernelOpen(const char* path, std::int32_t flags, std::uint16_t mode);
GRAVITY_SYSV_ABI std::int64_t sceKernelRead(std::int32_t fd, void* buffer, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceKernelClose(std::int32_t fd);
}
}
