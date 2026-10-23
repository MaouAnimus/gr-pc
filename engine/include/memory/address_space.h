#pragma once

#include "hle/status.h"
#include <cstddef>
#include <cstdint>

namespace gravity::memory {
enum class Protection { none, read, read_write, execute_read };

// Single reservation, no ownership of guest files. Callers serialize mutations.
class AddressSpace final {
public:
    static constexpr std::uintptr_t default_base = 0x1000000000ULL;
    static constexpr std::size_t default_size = 4ULL * 1024 * 1024 * 1024;
    AddressSpace() = default;
    ~AddressSpace();
    AddressSpace(const AddressSpace&) = delete;
    AddressSpace& operator=(const AddressSpace&) = delete;

    hle::Status reserve(std::uintptr_t base = default_base,
                        std::size_t size = default_size) noexcept;
    hle::Status commit(std::size_t offset, std::size_t size,
                       Protection protection = Protection::read_write) noexcept;
    hle::Status protect(std::size_t offset, std::size_t size,
                        Protection protection) noexcept;
    hle::Status decommit(std::size_t offset, std::size_t size) noexcept;
    hle::Status release() noexcept;
    [[nodiscard]] void* base() const noexcept { return base_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] static std::size_t page_size() noexcept;

private:
    hle::Status check_range(std::size_t offset, std::size_t size) const noexcept;
    void* base_{};
    std::size_t size_{};
};
} // namespace gravity::memory
