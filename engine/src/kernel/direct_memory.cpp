#include "hle/kernel/direct_memory.h"
#include "hle/guest_memory.h"
#include <windows.h>
#include <cstdio>
#include <map>
#include <mutex>

namespace gravity::hle::direct_memory {
namespace {
constexpr auto invalid = static_cast<std::int32_t>(0x80020016U);
constexpr auto no_memory = static_cast<std::int32_t>(0x8002000cU);
constexpr auto unsupported = static_cast<std::int32_t>(0x8002002dU);
constexpr std::size_t page = 0x4000;
struct Allocation { std::size_t size{}; void* reservation{}; void* mapped{}; };
std::mutex mutex;
std::map<std::uint64_t, Allocation> allocations;
bool valid_alignment(std::size_t value) {
    return value >= page && value <= pool_size && !(value & (value - 1));
}
std::uint64_t align_up(std::uint64_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
}
extern "C" GRAVITY_SYSV_ABI std::size_t sceKernelGetDirectMemorySize() { return pool_size; }
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelAllocateDirectMemory(std::int64_t begin, std::int64_t end,
    std::size_t length, std::size_t alignment, std::int32_t type, std::int64_t* output) {
    if (!alignment) alignment = page;
    if (begin < 0 || end <= begin || static_cast<std::uint64_t>(end) > pool_size || !length ||
        length % page || !valid_alignment(alignment) || (type != 0 && type != 3 && type != 10) ||
        !guest_memory::accessible(output, sizeof(*output), true)) return invalid;
    std::lock_guard lock(mutex);
    auto candidate = align_up(static_cast<std::uint64_t>(begin), alignment);
    for (const auto& [start, allocation] : allocations) {
        if (start + allocation.size <= candidate) continue;
        if (candidate <= start && length <= start - candidate) break;
        candidate = align_up(start + allocation.size, alignment);
    }
    if (candidate > static_cast<std::uint64_t>(end) || length > static_cast<std::uint64_t>(end) - candidate) return no_memory;
    const auto result = static_cast<std::int64_t>(candidate);
    if (!guest_memory::write(output, &result, sizeof(result))) return invalid;
    allocations.emplace(candidate, Allocation{length});
    std::fprintf(stderr, "[DirectMemory] Allocated offset=%llx length=%zx alignment=%zx\n",
        static_cast<unsigned long long>(candidate), length, alignment);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelMapDirectMemory(void** output, std::size_t length,
    std::int32_t protection, std::int32_t flags, std::int64_t offset, std::size_t alignment) {
    if (!alignment) alignment = page;
    if (!valid_alignment(alignment) || !length || length % page || offset < 0 || (flags & ~0x10) ||
        (protection & ~0x37) || (protection & 4) || !(protection & 3) ||
        !guest_memory::accessible(output, sizeof(*output), true)) return invalid;
    void* requested{};
    if (!guest_memory::read(output, &requested, sizeof(requested))) return invalid;
    std::lock_guard lock(mutex);
    const auto found = allocations.find(static_cast<std::uint64_t>(offset));
    if (found == allocations.end() || found->second.size != length) return invalid;
    if (found->second.reservation) return unsupported; // Alias/subrange mappings need shared backing.
    void* reservation{};
    void* mapped{};
    if (flags & 0x10) {
        if (!requested || reinterpret_cast<std::uintptr_t>(requested) % alignment) return invalid;
        reservation = VirtualAlloc(requested, length, MEM_RESERVE, PAGE_NOACCESS);
        if (reservation != requested) { if (reservation) VirtualFree(reservation, 0, MEM_RELEASE); return no_memory; }
        mapped = reservation;
    } else {
        using Allocate2 = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG,
                                        MEM_EXTENDED_PARAMETER*, ULONG);
        HMODULE module = GetModuleHandleW(L"kernelbase.dll");
        if (!module) module = LoadLibraryW(L"kernelbase.dll");
        const auto allocate = module ? reinterpret_cast<Allocate2>(GetProcAddress(module, "VirtualAlloc2")) : nullptr;
        if (allocate) {
            MEM_ADDRESS_REQUIREMENTS reqs{};
            reqs.LowestStartingAddress = reinterpret_cast<PVOID>(0x20000000ULL);
            reqs.HighestEndingAddress = reinterpret_cast<PVOID>(0x000000FFFFFFFFFFULL);
            reqs.Alignment = alignment;
            MEM_EXTENDED_PARAMETER param{};
            param.Type = MemExtendedParameterAddressRequirements;
            param.Pointer = &reqs;
            reservation = allocate(GetCurrentProcess(), nullptr, length, MEM_RESERVE, PAGE_NOACCESS, &param, 1);
            mapped = reservation;
        }
        if (!reservation) {
            // Keep the whole reservation so the aligned interior can be released
            // safely. Only the requested extent is committed.
            reservation = VirtualAlloc(nullptr, length + alignment, MEM_RESERVE, PAGE_NOACCESS);
            if (reservation) mapped = reinterpret_cast<void*>(align_up(reinterpret_cast<std::uintptr_t>(reservation), alignment));
        }
    }
    if (!reservation) return no_memory;
    const auto mode = (protection & 2) ? PAGE_READWRITE : PAGE_READONLY;
    if (!VirtualAlloc(mapped, length, MEM_COMMIT, mode)) {
        VirtualFree(reservation, 0, MEM_RELEASE);
        return no_memory;
    }
    if (!guest_memory::write(output, &mapped, sizeof(mapped))) { VirtualFree(reservation, 0, MEM_RELEASE); return invalid; }
    found->second.reservation = reservation;
    found->second.mapped = mapped;
    std::fprintf(stderr, "[DirectMemory] Mapped offset=%llx length=%zx alignment=%zx address=%p\n",
        static_cast<unsigned long long>(offset), length, alignment, mapped);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelReleaseDirectMemory(std::int64_t offset, std::size_t length) {
    if (offset < 0) return invalid;
    std::lock_guard lock(mutex);
    const auto found = allocations.find(static_cast<std::uint64_t>(offset));
    if (found == allocations.end() || found->second.size != length) return invalid;
    if (found->second.reservation && !VirtualFree(found->second.reservation, 0, MEM_RELEASE)) return invalid;
    allocations.erase(found);
    return 0;
}
}
