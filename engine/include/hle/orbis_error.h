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
    SYSTEM_SERVICE_ERROR_PARAMETER = static_cast<int32_t>(0x80A10003),
};

constexpr uint32_t ORBIS_FIBER_MAGIC_START = 0xdef1649c;
constexpr uint32_t ORBIS_FIBER_MAGIC_END   = 0xb37592a0;

enum class FiberState : uint32_t {
    empty     = 0,
    ready     = 1,
    running   = 2,
    suspended = 3,
    completed = 4
};

// SceFiber matches the authentic 112-byte (0x70) Orbis OS binary layout.
// External host fiber handle must NOT be embedded in this structure to avoid memory corruption.
struct alignas(8) SceFiber {
    uint32_t            magic_start;        // 0x00: 0xdef1649c
    uint32_t            state;              // 0x04: FiberState
    void*               entry;              // 0x08: Function pointer
    uint64_t            arg_on_initialize;  // 0x10: First parameter
    void*               addr_context;       // 0x18: Base address of context / stack
    uint64_t            size_context;       // 0x20: Size of context / stack
    char                name[32];           // 0x28: ASCII debug name (32 bytes)
    void*               context;            // 0x48: Orbis context pointer
    uint32_t            flags;              // 0x50: Option flags
    uint32_t            reserved;           // 0x54: Alignment padding / reserved
    void*               context_start;      // 0x58: Lower bound of stack
    void*               context_end;        // 0x60: Upper bound of stack
    uint32_t            magic_end;          // 0x68: 0xb37592a0
    uint32_t            padding_end;        // 0x6C: Pad to 112 bytes
};

static_assert(sizeof(SceFiber) == 112, "SceFiber must match authentic Orbis OS 112-byte layout");
static_assert(offsetof(SceFiber, magic_start) == 0x00, "magic_start offset mismatch");
static_assert(offsetof(SceFiber, state) == 0x04, "state offset mismatch");
static_assert(offsetof(SceFiber, entry) == 0x08, "entry offset mismatch");
static_assert(offsetof(SceFiber, arg_on_initialize) == 0x10, "arg_on_initialize offset mismatch");
static_assert(offsetof(SceFiber, addr_context) == 0x18, "addr_context offset mismatch");
static_assert(offsetof(SceFiber, size_context) == 0x20, "size_context offset mismatch");
static_assert(offsetof(SceFiber, name) == 0x28, "name offset mismatch");
static_assert(offsetof(SceFiber, context) == 0x48, "context offset mismatch");
static_assert(offsetof(SceFiber, flags) == 0x50, "flags offset mismatch");
static_assert(offsetof(SceFiber, context_start) == 0x58, "context_start offset mismatch");
static_assert(offsetof(SceFiber, context_end) == 0x60, "context_end offset mismatch");
static_assert(offsetof(SceFiber, magic_end) == 0x68, "magic_end offset mismatch");

struct SceFiberOptParam {
    uint64_t addr;
    uint64_t size;
    uint32_t attr;
    uint32_t reserved;
};

} // namespace gravity::hle::orbis
