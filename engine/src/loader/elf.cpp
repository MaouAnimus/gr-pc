#include "hle/loader/elf.h"
#include "hle/loader/reloc.h"
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace gravity::hle::loader {
namespace {
struct SelfEntry { std::uint64_t props, offset, filesz, memsz; };
struct Ehdr {
    unsigned char ident[16];
    std::uint16_t type, machine;
    std::uint32_t version;
    std::uint64_t entry, phoff, shoff;
    std::uint32_t flags;
    std::uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Phdr {
    std::uint32_t type, flags;
    std::uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
struct Segment { Phdr ph; std::size_t source, committed; };
bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}
template<class T> T read(const std::uint8_t* raw, std::size_t off) {
    T value{}; std::memcpy(&value, raw + off, sizeof(value)); return value;
}
}

LoadResult load_self(const void* image, std::size_t size,
                    gravity::memory::AddressSpace& as, std::uintptr_t guest_base,
                    bool inspection, bool bind_unresolved_traps) {
    LoadResult result{};
    auto fail = [&](const char* message) { result.error = message; return result; };
    if (!image || size < sizeof(Ehdr)) return fail("Truncated executable header");
    const auto* raw = static_cast<const std::uint8_t*>(image);
    const bool self = read<std::uint32_t>(raw, 0) == 0x1D3D154FU;
    std::size_t elf_offset = 0;
    std::vector<SelfEntry> entries;
    if (self) {
        const auto count = read<std::uint16_t>(raw, 0x18);
        if (!count || !within(0x20, std::uint64_t(count) * sizeof(SelfEntry), size))
            return fail("Invalid SELF segment table");
        elf_offset = 0x20 + count * sizeof(SelfEntry);
        for (std::size_t i = 0; i < count; ++i) {
            auto ent = read<SelfEntry>(raw, 0x20 + i * sizeof(SelfEntry));
            if (!within(ent.offset, ent.filesz, size)) return fail("Truncated SELF segment");
            entries.push_back(ent);
        }
    }
    if (!within(elf_offset, sizeof(Ehdr), size)) return fail("Missing ELF header");
    const auto eh = read<Ehdr>(raw, elf_offset);
    if (std::memcmp(eh.ident, "\x7f" "ELF", 4) || eh.ident[4] != 2 ||
        eh.ident[5] != 1 || eh.ident[6] != 1 || eh.machine != 62 || eh.version != 1 ||
        eh.ehsize != sizeof(Ehdr) || eh.phentsize != sizeof(Phdr) || !eh.phnum ||
        (eh.type != 3 && eh.type != 0xFE10))
        return fail("Unsupported ELF format");
    if (!within(eh.phoff, std::uint64_t(eh.phnum) * sizeof(Phdr), size - elf_offset))
        return fail("Invalid ELF program header table");
    std::vector<Phdr> headers;
    std::vector<Segment> segments;
    std::vector<MemoryRange> ranges;
    const auto page = memory::AddressSpace::page_size();
    const auto capacity = as.base() ? as.size() : 4ULL * 1024 * 1024 * 1024;
    std::size_t total = 0;
    bool entry_valid = false;
    auto source = [&](std::size_t index, const Phdr& ph, std::size_t& off) {
        if (!self) { off = ph.offset; return within(off, ph.filesz, size); }
        const SelfEntry* found = nullptr;
        for (const auto& ent : entries) {
            if ((ent.props & 0x800) && ((ent.props >> 20) & 0xFFF) == index) {
                if (found || ent.filesz != ph.filesz || (ent.props & 3)) return false;
                found = &ent;
            }
        }
        if (!found) return false;
        off = found->offset;
        return within(off, ph.filesz, size);
    };
    for (std::size_t i = 0; i < eh.phnum; ++i)
        headers.push_back(read<Phdr>(raw, elf_offset + eh.phoff + i * sizeof(Phdr)));
    std::size_t dynlib = 0, dynlib_size = 0;
    const Phdr* dyn_header = nullptr;
    const Phdr* dynamic = nullptr;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        const auto& ph = headers[i];
        if (ph.type == 2) {
            if (dynamic) return fail("Duplicate dynamic segment");
            dynamic = &ph;
        }
        if (ph.type == 0x61000000) {
            if (dyn_header || !source(i, ph, dynlib)) return fail("Invalid dynlib segment");
            dyn_header = &ph; dynlib_size = ph.filesz;
        }
        if (ph.type != 1) continue;
        if (!ph.memsz) {
            if (ph.filesz) return fail("File data without segment memory");
            continue;
        }
        std::size_t off = 0;
        if (ph.filesz > ph.memsz || ph.vaddr % page ||
            !within(ph.vaddr, ph.memsz, capacity) ||
            ph.memsz > std::numeric_limits<std::size_t>::max() - page + 1 ||
            (ph.flags & ~7U) || ((ph.flags & 3) == 3) ||
            (ph.align > 1 && ((ph.align & (ph.align - 1)) ||
                             ph.vaddr % ph.align != ph.offset % ph.align)) ||
            (ph.filesz && !source(i, ph, off))) return fail("Invalid load segment");
        const auto committed = (ph.memsz + page - 1) & ~(page - 1);
        if (!within(ph.vaddr, committed, capacity)) return fail("Segment exceeds address space");
        for (const auto& prior : segments)
            if (ph.vaddr < prior.ph.vaddr + prior.committed && prior.ph.vaddr < ph.vaddr + committed)
                return fail("Overlapping load segments");
        segments.push_back({ph, off, committed});
        ranges.push_back({static_cast<std::size_t>(ph.vaddr), static_cast<std::size_t>(ph.memsz)});
        total = (std::max)(total, static_cast<std::size_t>(ph.vaddr + committed));
        if ((ph.flags & 1) && eh.entry >= ph.vaddr && within(eh.entry - ph.vaddr, 16, ph.filesz))
            entry_valid = true;
    }
    if (segments.empty() || !entry_valid) return fail("Entry point is outside executable file-backed memory");
    RelocationLayout layout{};
    if (dynamic) {
        if (!dyn_header || dynamic->offset < dyn_header->offset ||
            !within(dynamic->offset - dyn_header->offset, dynamic->filesz, dynlib_size) ||
            dynamic->filesz % 16) return fail("Invalid Orbis dynamic table");
        bool terminated = false;
        std::uint64_t rela_entry = 0, sym_entry = 0, plt_type = 7;
        for (std::size_t i = 0; i < dynamic->filesz; i += 16) {
            const auto off = dynlib + dynamic->offset - dyn_header->offset + i;
            const auto tag = read<std::uint64_t>(raw, off);
            const auto val = read<std::uint64_t>(raw, off + 8);
            if (!tag) { terminated = true; break; }
            switch (tag) {
            case 0x61000035: layout.strings = val; break;
            case 0x61000037: layout.strings_size = val; break;
            case 0x61000039: layout.symbols = val; break;
            case 0x6100003F: layout.symbols_size = val; break;
            case 0x6100003B: sym_entry = val; break;
            case 0x6100002F: layout.rela = val; break;
            case 0x61000031: layout.rela_size = val; break;
            case 0x61000033: rela_entry = val; break;
            case 0x61000029: layout.plt = val; break;
            case 0x6100002D: layout.plt_size = val; break;
            case 0x6100002B: plt_type = val; break;
            default: break;
            }
        }
        if (!terminated || rela_entry != 24 || sym_entry != 24 || plt_type != 7)
            return fail("Unsupported dynamic relocation layout");
    } else if (dyn_header) return fail("Missing dynamic metadata");
    const bool own_reservation = !as.base();
    if (own_reservation && !as.reserve(guest_base)) return fail("Cannot reserve guest memory");
    std::vector<MemoryRange> committed;
    struct Rollback {
        memory::AddressSpace& as; bool own; std::vector<MemoryRange>& pages; bool done{};
        ~Rollback() {
            if (done) return;
            for (const auto& range : pages) (void)as.decommit(range.offset, range.size);
            if (own) (void)as.release();
        }
    } rollback{as, own_reservation, committed};
    committed.reserve(segments.size());
    const auto base = reinterpret_cast<std::uintptr_t>(as.base());
    for (const auto& seg : segments) {
        for (std::size_t off = 0; off < seg.committed; off += page) {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(base + seg.ph.vaddr + off), &info, sizeof(info)) ||
                info.State != MEM_RESERVE) return fail("Load would overwrite committed memory");
        }
    }
    for (const auto& seg : segments) {
        if (!as.commit(seg.ph.vaddr, seg.committed, memory::Protection::read_write))
            return fail("Cannot commit segment");
        committed.push_back({static_cast<std::size_t>(seg.ph.vaddr), seg.committed});
        if (seg.ph.filesz) std::memcpy(reinterpret_cast<void*>(base + seg.ph.vaddr), raw + seg.source, seg.ph.filesz);
    }
    RelocationReport report;
    if (dynamic && apply_orbis_relocations(raw + dynlib, dynlib_size, base, total,
                                           layout, ranges, report, inspection, bind_unresolved_traps) < 0) {
        result.unresolved_symbols = std::move(report.unresolved);
        return fail("Invalid relocation or unresolved import (strict mode)");
    }
    for (const auto& seg : segments) {
        const auto flags = seg.ph.flags;
        const auto protection = (flags & 1) ? memory::Protection::execute_read :
            (flags & 2) ? memory::Protection::read_write :
            (flags & 4) ? memory::Protection::read : memory::Protection::none;
        if (!as.protect(seg.ph.vaddr, seg.committed, protection)) return fail("Cannot protect segment");
    }
    result.base = base; result.entry = base + eh.entry; result.total_size = total;
    result.num_relocations = report.applied; result.is_self = self;
    result.unresolved_symbols = std::move(report.unresolved);
    rollback.done = true;
    return result;
}
LoadResult load_elf(const void* image, std::size_t size, std::uintptr_t base, bool inspection, bool bind_unresolved_traps) {
    static memory::AddressSpace space;
    return load_self(image, size, space, base, inspection, bind_unresolved_traps);
}
}

