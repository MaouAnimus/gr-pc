#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace gravity::hle::loader {

struct MemoryRange { std::size_t offset{}, size{}; };
struct RelocationLayout {
    std::size_t strings{}, strings_size{}, symbols{}, symbols_size{};
    std::size_t rela{}, rela_size{}, plt{}, plt_size{};
};
struct RelocationReport {
    std::uint32_t applied{};
    std::vector<std::string> unresolved;
};

// Validates every record before writing. Unresolved imports are left null only
// in explicit inspection mode; they are never replaced with success stubs.
std::int64_t apply_orbis_relocations(
    const std::uint8_t* dynlib_data,
    std::size_t dynlib_size,
    std::uintptr_t load_base,
    std::size_t memory_size,
    const RelocationLayout& layout,
    std::span<const MemoryRange> mapped,
    RelocationReport& report,
    bool inspection = false);

// Legacy interface for backwards compatibility
bool apply_relocations(const std::uint8_t* elf_image,
                       std::size_t elf_size,
                       std::uintptr_t load_base) noexcept;

} // namespace gravity::hle::loader
