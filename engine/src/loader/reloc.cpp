#include "hle/loader/reloc.h"
#include "hle/abi.h"
#include "hle/registry/adapter_registry.h"
#include <Windows.h>
#include <cstring>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <iostream>
#include <algorithm>
#include <limits>

namespace gravity::hle::loader {

namespace {

struct Elf64_Sym {
    std::uint32_t st_name;
    std::uint8_t  st_info;
    std::uint8_t  st_other;
    std::uint16_t st_shndx;
    std::uint64_t st_value;
    std::uint64_t st_size;
};

struct Elf64_Rela {
    std::uint64_t r_offset;
    std::uint64_t r_info;
    std::int64_t  r_addend;
};

constexpr std::uint32_t R_X86_64_64       = 1;
constexpr std::uint32_t R_X86_64_GLOB_DAT = 6;
constexpr std::uint32_t R_X86_64_JUMP_SLOT= 7;
constexpr std::uint32_t R_X86_64_RELATIVE = 8;

} // anonymous namespace

std::int64_t apply_orbis_relocations(
    const std::uint8_t* data, std::size_t size, std::uintptr_t base,
    std::size_t memory_size, const RelocationLayout& layout,
    std::span<const MemoryRange> mapped, RelocationReport& report,
    bool inspection, bool bind_unresolved_traps)
{
    report = {};
    AdapterRegistry::instance().set_guest_base(base);
    auto within = [](std::uint64_t off, std::uint64_t len, std::uint64_t limit) {
        return off <= limit && len <= limit - off;
    };
    if (!data || !base || memory_size > std::numeric_limits<std::uintptr_t>::max() - base ||
        !within(layout.strings, layout.strings_size, size) ||
        !within(layout.symbols, layout.symbols_size, size) ||
        !within(layout.rela, layout.rela_size, size) ||
        !within(layout.plt, layout.plt_size, size) ||
        layout.symbols_size % sizeof(Elf64_Sym) ||
        layout.rela_size % sizeof(Elf64_Rela) || layout.plt_size % sizeof(Elf64_Rela)) return -1;
    struct Write { std::uintptr_t target; std::uint64_t value; };
    std::vector<Write> writes;
    MEMORY_BASIC_INFORMATION info{};
    const MemoryRange tables[] = {{layout.rela, layout.rela_size}, {layout.plt, layout.plt_size}};
    for (const auto& table : tables) {
        for (std::size_t off = 0; off < table.size; off += sizeof(Elf64_Rela)) {
            Elf64_Rela rel{};
            std::memcpy(&rel, data + table.offset + off, sizeof(rel));
            const auto type = static_cast<std::uint32_t>(rel.r_info);
            const auto sym_index = rel.r_info >> 32;
            if (type == 0) continue;
            if (type != R_X86_64_RELATIVE && type != R_X86_64_64 &&
                type != R_X86_64_GLOB_DAT && type != R_X86_64_JUMP_SLOT) return -1;
            if (!within(rel.r_offset, 8, memory_size)) return -1;
            bool in_segment = false;
            for (const auto& range : mapped)
                if (rel.r_offset >= range.offset && within(rel.r_offset - range.offset, 8, range.size))
                    in_segment = true;
            if (!in_segment) return -1;
            // Public callers must supply committed, writable host pages.
            const auto address = base + rel.r_offset;
            const auto region = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            if (!(address >= region && within(address - region, 8, info.RegionSize)) &&
                !VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info))) return -1;
            if (
                info.State != MEM_COMMIT || (info.Protect != PAGE_READWRITE &&
                info.Protect != PAGE_EXECUTE_READWRITE) ||
                !within(base + rel.r_offset - reinterpret_cast<std::uintptr_t>(info.BaseAddress), 8, info.RegionSize))
                return -1;
            std::uint64_t value = 0;
            if (type == R_X86_64_RELATIVE) {
                if (sym_index) return -1;
                value = base + static_cast<std::uint64_t>(rel.r_addend);
            } else {
                if (sym_index >= layout.symbols_size / sizeof(Elf64_Sym)) return -1;
                Elf64_Sym sym{};
                std::memcpy(&sym, data + layout.symbols + sym_index * sizeof(sym), sizeof(sym));
                if (sym.st_shndx == 0xFFF1) value = sym.st_value; // SHN_ABS
                else if (sym.st_shndx != 0) {
                    if (sym.st_value >= memory_size) return -1;
                    value = base + sym.st_value;
                } else {
                    if (sym.st_name >= layout.strings_size) return -1;
                    const auto* name = reinterpret_cast<const char*>(data + layout.strings + sym.st_name);
                    const auto* end = static_cast<const char*>(std::memchr(name, 0, layout.strings_size - sym.st_name));
                    if (!end) return -1;
                    const std::string_view view(name, end - name);
                    std::string_view nid = view.substr(0, view.find('#'));
                    std::string_view lib_name = {};
                    auto first_hash = view.find('#');
                    if (first_hash != std::string_view::npos) {
                        auto second_hash = view.find('#', first_hash + 1);
                        if (second_hash != std::string_view::npos) {
                            lib_name = view.substr(first_hash + 1, second_hash - first_hash - 1);
                        } else {
                            lib_name = view.substr(first_hash + 1);
                        }
                    }

                    value = AdapterRegistry::instance().resolve(nid, lib_name);
                    if (!value) {
                        if (std::find(report.unresolved.begin(), report.unresolved.end(), view) == report.unresolved.end())
                            report.unresolved.emplace_back(view);
                        if (bind_unresolved_traps) {
                            value = AdapterRegistry::instance().get_unresolved_thunk(nid, lib_name);
                        } else {
                            // Inspection leaves unresolved slots null, never an executable success stub.
                            writes.push_back({base + rel.r_offset, 0});
                            continue;
                        }
                    }
                }
                value += static_cast<std::uint64_t>(rel.r_addend);
            }
            writes.push_back({base + rel.r_offset, value});
        }
    }
    if (!inspection && !report.unresolved.empty()) return -1;
    for (const auto& write : writes) std::memcpy(reinterpret_cast<void*>(write.target), &write.value, 8);
    report.applied = static_cast<std::uint32_t>(writes.size());
    return report.applied;
}
bool apply_relocations(const std::uint8_t*, std::size_t, std::uintptr_t) noexcept {
    return false; // Legacy API cannot describe relocation metadata safely.
}
} // namespace gravity::hle::loader
