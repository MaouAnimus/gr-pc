#pragma once

#include <cstdint>
#include <cstddef>

namespace gravity::hle::orbis {

enum class Error : int32_t {
    OK            = 0,
    KERNEL_EINVAL = static_cast<int32_t>(0x80010016),
    KERNEL_ENOMEM = static_cast<int32_t>(0x8001000C),
    FIBER_EINVAL  = static_cast<int32_t>(0x80020001),
    FIBER_ENORES  = static_cast<int32_t>(0x80020002),
    FIBER_EILSEQ  = static_cast<int32_t>(0x80020003),
};

struct SceFiber {
    uint64_t    attr;
    void*       entry;
    uint64_t    arg;
    void*       stack_base;
    std::size_t stack_size;
    char        name[32];
    void*       host_fiber; // Puntero interno a gravity::hle::Fiber
};

struct SceFiberOptParam {
    uint64_t addr;
    uint64_t size;
    uint32_t attr;
    uint32_t reserved;
};

} // namespace gravity::hle::orbis
