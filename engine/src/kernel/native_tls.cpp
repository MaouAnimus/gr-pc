#include "hle/native_tls.h"
#include <windows.h>
#include <winternl.h>
#include <mutex>
#include <algorithm>
#include <cstring>

namespace gravity::hle {
namespace {
DWORD guest_slot() noexcept {
    static const DWORD slot = TlsAlloc();
    return slot;
}
std::uintptr_t read_slot(std::uint32_t offset) noexcept {
    std::uintptr_t value;
    __asm__ volatile("movq %%gs:(%1), %0" : "=r"(value) : "r"(static_cast<std::uintptr_t>(offset)));
    return value;
}
}
bool NativeTls::supported() noexcept {
    static const bool verified = [] {
        const auto slot = guest_slot();
        if (slot >= 64) return false;
        int marker{};
        auto* previous = TlsGetValue(slot);
        if (!TlsSetValue(slot, &marker)) return false;
        const auto actual = read_slot(static_cast<std::uint32_t>(offsetof(TEB, TlsSlots) + slot * sizeof(void*)));
        TlsSetValue(slot, previous);
        return actual == reinterpret_cast<std::uintptr_t>(&marker);
    }();
    return verified;
}
std::uint32_t NativeTls::slot_offset() noexcept {
    return supported() ? static_cast<std::uint32_t>(offsetof(TEB, TlsSlots) + guest_slot() * sizeof(void*)) : 0;
}
bool NativeTls::patch_fs_load(std::span<std::uint8_t> instruction) noexcept {
    if (instruction.size() != 9 || instruction[0] != 0x64 ||
        (instruction[1] != 0x48 && instruction[1] != 0x4c) || instruction[2] != 0x8b ||
        (instruction[3] & 0xc7) != 4 || instruction[4] != 0x25 ||
        instruction[5] || instruction[6] || instruction[7] || instruction[8]) return false;
    const auto offset = slot_offset();
    if (!offset) return false;
    instruction[0] = 0x65;
    std::memcpy(instruction.data() + 5, &offset, sizeof(offset));
    return true;
}
NativeTls::~NativeTls() {
    if (owner_thread_ == GetCurrentThreadId()) (void)detach();
    if (allocation_) VirtualFree(allocation_, 0, MEM_RELEASE);
}
Status NativeTls::initialize(const TlsImage& image) noexcept {
    if (allocation_) return {Error::invalid_state};
    if (!supported()) return {Error::unsupported};
    if (!image.memory_size || image.memory_size > 16 * 1024 * 1024 ||
        image.initial.size() > image.memory_size || !image.alignment ||
        (image.alignment & (image.alignment - 1)) || image.alignment > 65536) return {Error::invalid_argument};
    const auto alignment = (std::max)(image.alignment, std::size_t(16));
    const auto size = (image.memory_size + alignment - 1) & ~(alignment - 1);
    allocation_ = VirtualAlloc(nullptr, size + alignment + 6 * sizeof(std::uintptr_t),
                               MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!allocation_) return {Error::system_error, GetLastError()};
    const auto start = reinterpret_cast<std::uintptr_t>(allocation_);
    const auto tp = (start + size + alignment - 1) & ~(alignment - 1);
    thread_pointer_ = reinterpret_cast<void*>(tp);
    if (!image.initial.empty()) std::memcpy(reinterpret_cast<void*>(tp - size), image.initial.data(), image.initial.size());
    auto* control = reinterpret_cast<std::uintptr_t*>(tp);
    control[0] = tp;
    control[1] = tp + 3 * sizeof(std::uintptr_t);
    control[2] = 0; // No guest pthread object is published by this static TLS context.
    control[3] = 1; // DTV generation.
    control[4] = 1; // One module.
    control[5] = tp - size;
    return {};
}
Status NativeTls::attach() noexcept {
    if (!allocation_ || owner_thread_) return {Error::invalid_state};
    saved_slot_ = TlsGetValue(guest_slot());
    owner_thread_ = GetCurrentThreadId();
    if (!TlsSetValue(guest_slot(), thread_pointer_)) { owner_thread_ = 0; return {Error::system_error, GetLastError()}; }
    return {};
}
Status NativeTls::detach() noexcept {
    if (!owner_thread_ || owner_thread_ != GetCurrentThreadId()) return {Error::invalid_state};
    if (!TlsSetValue(guest_slot(), saved_slot_)) return {Error::system_error, GetLastError()};
    owner_thread_ = 0;
    return {};
}
}
