#include "memory/address_space.h"
#include <Windows.h>
#include <limits>

namespace gravity::memory {
namespace {
DWORD native_protection(Protection p) noexcept {
    switch (p) {
    case Protection::none: return PAGE_NOACCESS;
    case Protection::read: return PAGE_READONLY;
    case Protection::read_write: return PAGE_READWRITE;
    case Protection::execute_read: return PAGE_EXECUTE_READ;
    }
    return 0;
}
hle::Status system_error() noexcept { return {hle::Error::system_error, GetLastError()}; }
}

AddressSpace::~AddressSpace() { (void)release(); }

std::size_t AddressSpace::page_size() noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwPageSize;
}

hle::Status AddressSpace::reserve(std::uintptr_t base, std::size_t size) noexcept {
    if (base_) return {hle::Error::invalid_state};
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    if (!base || !size || base % info.dwAllocationGranularity || size % info.dwPageSize)
        return {hle::Error::invalid_argument};
    if (size > std::numeric_limits<std::uintptr_t>::max() - base)
        return {hle::Error::out_of_range};

    // Resolve dynamically to report unsupported systems without a loader failure.
    using Allocate2 = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, ULONG,
                                    MEM_EXTENDED_PARAMETER*, ULONG);
    const HMODULE module = GetModuleHandleW(L"KernelBase.dll");
    if (!module) return system_error();
    const auto allocate = reinterpret_cast<Allocate2>(GetProcAddress(module, "VirtualAlloc2"));
    if (!allocate) return {hle::Error::unsupported, GetLastError()};
    void* reservation = allocate(GetCurrentProcess(), reinterpret_cast<void*>(base),
                                size, MEM_RESERVE, PAGE_NOACCESS, nullptr, 0);
    if (!reservation) return system_error();
    if (reservation != reinterpret_cast<void*>(base)) {
        VirtualFree(reservation, 0, MEM_RELEASE);
        return {hle::Error::system_error, ERROR_INVALID_ADDRESS};
    }
    base_ = reservation;
    size_ = size;
    return {};
}

hle::Status AddressSpace::check_range(std::size_t offset, std::size_t size) const noexcept {
    if (!base_) return {hle::Error::invalid_state};
    if (!size || offset % page_size() || size % page_size())
        return {hle::Error::invalid_argument};
    if (offset > size_ || size > size_ - offset) return {hle::Error::out_of_range};
    return {};
}

hle::Status AddressSpace::commit(std::size_t offset, std::size_t size,
                                Protection protection) noexcept {
    const auto status = check_range(offset, size);
    if (!status) return status;
    const DWORD flags = native_protection(protection);
    if (!flags) return {hle::Error::invalid_argument};
    auto* address = static_cast<std::byte*>(base_) + offset;
    if (!VirtualAlloc(address, size, MEM_COMMIT, flags)) return system_error();
    return {};
}

hle::Status AddressSpace::protect(std::size_t offset, std::size_t size,
                                 Protection protection) noexcept {
    const auto status = check_range(offset, size);
    if (!status) return status;
    const DWORD flags = native_protection(protection);
    if (!flags) return {hle::Error::invalid_argument};
    auto* address = static_cast<std::byte*>(base_) + offset;
    DWORD old{};
    if (!VirtualProtect(address, size, flags, &old)) return system_error();
    if (protection == Protection::execute_read &&
        !FlushInstructionCache(GetCurrentProcess(), address, size)) return system_error();
    return {};
}

hle::Status AddressSpace::decommit(std::size_t offset, std::size_t size) noexcept {
    const auto status = check_range(offset, size);
    if (!status) return status;
    if (!VirtualFree(static_cast<std::byte*>(base_) + offset, size, MEM_DECOMMIT))
        return system_error();
    return {};
}

hle::Status AddressSpace::release() noexcept {
    if (!base_) return {};
    if (!VirtualFree(base_, 0, MEM_RELEASE)) return system_error();
    base_ = nullptr;
    size_ = 0;
    return {};
}
} // namespace gravity::memory
