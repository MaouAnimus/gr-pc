#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace gravity::hle::guest_memory {
inline bool accessible(const void* pointer, std::size_t bytes, bool write = false) noexcept {
    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    if (!start || !bytes || start > UINTPTR_MAX - bytes) return false;
    for (auto cursor = start; cursor < start + bytes;) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD)) return false;
        const auto protection = region.Protect & 0xff;
        const bool writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (write ? !writable : (!writable && protection != PAGE_READONLY && protection != PAGE_EXECUTE_READ)) return false;
        const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (end <= cursor) return false;
        cursor = end;
    }
    return true;
}
inline bool read(const void* pointer, void* output, std::size_t bytes) noexcept {
    if (!output || !accessible(pointer, bytes)) return false;
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), pointer, output, bytes, &copied) && copied == bytes;
}
inline bool write(void* pointer, const void* input, std::size_t bytes) noexcept {
    if (!input || !accessible(pointer, bytes, true)) return false;
    SIZE_T copied{};
    return WriteProcessMemory(GetCurrentProcess(), pointer, input, bytes, &copied) && copied == bytes;
}
// Reads at most capacity bytes, including the terminating NUL.
inline bool string(const char* pointer, char* output, std::size_t capacity) noexcept {
    if (!pointer || !output || reinterpret_cast<std::uintptr_t>(pointer) > UINTPTR_MAX - capacity) return false;
    for (std::size_t i = 0; i < capacity; ++i) {
        if (!read(pointer + i, output + i, 1)) return false;
        if (!output[i]) return true;
    }
    return false;
}
}
