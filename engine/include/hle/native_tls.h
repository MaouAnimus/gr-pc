#pragma once
#include "hle/status.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <span>

namespace gravity::hle {
struct TlsImage {
    std::vector<std::uint8_t> initial;
    std::size_t memory_size{};
    std::size_t alignment{16};
};
// AMD64 variant II: static TLS below TP, self pointer at FS:0, DTV at FS:8.
// The loader replaces audited FS:0 loads with a native GS load of our Windows TLS slot.
// Windows owns the GS base; it is never changed.
class NativeTls final {
public:
    NativeTls() = default;
    ~NativeTls();
    NativeTls(const NativeTls&) = delete;
    NativeTls& operator=(const NativeTls&) = delete;
    static bool supported() noexcept;
    static std::uint32_t slot_offset() noexcept;
    static bool patch_fs_load(std::span<std::uint8_t> instruction) noexcept;
    Status initialize(const TlsImage& image) noexcept;
    Status attach() noexcept;
    Status detach() noexcept;
    void* thread_pointer() const noexcept { return thread_pointer_; }
private:
    void* allocation_{};
    void* thread_pointer_{};
    void* saved_slot_{};
    std::uint32_t owner_thread_{};
};
}
