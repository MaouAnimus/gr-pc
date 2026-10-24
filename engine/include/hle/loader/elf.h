#pragma once

#include "hle/status.h"
#include "memory/address_space.h"
#include "hle/native_tls.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gravity::hle::loader {

struct LoadResult {
    std::uintptr_t base{};            // Base address where the executable is mapped
    std::uintptr_t entry{};           // Entry point address after relocation
    std::size_t total_size{};         // Total mapped size
    std::uint32_t num_relocations{};  // Relocations applied
    bool is_self{};                   // True if SELF container was detected and unpacked
    const char* error{};
    std::vector<std::string> unresolved_symbols;
    TlsImage tls;
    std::uint32_t tls_fixup_count{};
};

// Loads uncompressed SELF or relocatable x86-64 ELF. Strict by default.
// Inspection may leave unresolved slots null; successful mapping never certifies
// guest execution readiness. Failed loads roll back their own committed pages.
LoadResult load_self(const void* file_image,
                     std::size_t file_size,
                     gravity::memory::AddressSpace& as,
                     std::uintptr_t guest_base = 0x1000000000ULL,
                     bool inspection = false,
                     bool bind_unresolved_traps = false,
                     bool native_tls_fixups = false);

// Legacy / convenience wrapper
LoadResult load_elf(const void* file_image,
                    std::size_t file_size,
                    std::uintptr_t guest_base = 0x1000000000ULL,
                    bool inspection = false,
                    bool bind_unresolved_traps = false);

} // namespace gravity::hle::loader
