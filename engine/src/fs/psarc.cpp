#include "hle/fs/psarc.h"
#include <zlib.h>
#include <algorithm>
#include <cstring>
#include <sstream>

namespace gravity::hle::fs {

namespace {

inline std::uint16_t read_be16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

inline std::uint32_t read_be32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8)  |
            static_cast<std::uint32_t>(p[3]);
}

inline std::uint64_t read_be40(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint64_t>(p[0]) << 32) |
           (static_cast<std::uint64_t>(p[1]) << 24) |
           (static_cast<std::uint64_t>(p[2]) << 16) |
           (static_cast<std::uint64_t>(p[3]) << 8)  |
            static_cast<std::uint64_t>(p[4]);
}

} // anonymous namespace

PsarcArchive::~PsarcArchive() {
    close();
}

PsarcArchive::PsarcArchive(PsarcArchive&& other) noexcept
    : file_handle_(other.file_handle_),
      mapping_handle_(other.mapping_handle_),
      mapping_view_(other.mapping_view_),
      file_size_(other.file_size_),
      block_size_(other.block_size_),
      zblocks_(std::move(other.zblocks_)),
      entries_(std::move(other.entries_)),
      path_to_index_(std::move(other.path_to_index_))
{
    other.file_handle_ = INVALID_HANDLE_VALUE;
    other.mapping_handle_ = nullptr;
    other.mapping_view_ = nullptr;
    other.file_size_ = 0;
}

PsarcArchive& PsarcArchive::operator=(PsarcArchive&& other) noexcept {
    if (this != &other) {
        close();
        file_handle_ = other.file_handle_;
        mapping_handle_ = other.mapping_handle_;
        mapping_view_ = other.mapping_view_;
        file_size_ = other.file_size_;
        block_size_ = other.block_size_;
        zblocks_ = std::move(other.zblocks_);
        entries_ = std::move(other.entries_);
        path_to_index_ = std::move(other.path_to_index_);

        other.file_handle_ = INVALID_HANDLE_VALUE;
        other.mapping_handle_ = nullptr;
        other.mapping_view_ = nullptr;
        other.file_size_ = 0;
    }
    return *this;
}

void PsarcArchive::close() noexcept {
    if (mapping_view_) {
        UnmapViewOfFile(mapping_view_);
        mapping_view_ = nullptr;
    }
    if (mapping_handle_) {
        CloseHandle(mapping_handle_);
        mapping_handle_ = nullptr;
    }
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
        file_handle_ = INVALID_HANDLE_VALUE;
    }
    file_size_ = 0;
    zblocks_.clear();
    entries_.clear();
    path_to_index_.clear();
}

std::string PsarcArchive::normalize_path(std::string_view path) {
    std::string s;
    s.reserve(path.size());

    // Strip leading /app0/ or /
    if (path.starts_with("/app0/")) {
        path.remove_prefix(6);
    } else if (path.starts_with("app0/")) {
        path.remove_prefix(5);
    } else if (path.starts_with("/")) {
        path.remove_prefix(1);
    }

    for (char c : path) {
        if (c == '\\') {
            s.push_back('/');
        } else {
            s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return s;
}

bool PsarcArchive::open(const std::wstring& filepath) noexcept {
    close();

    file_handle_ = CreateFileW(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER li_size{};
    if (!GetFileSizeEx(file_handle_, &li_size) || li_size.QuadPart < 32) {
        close();
        return false;
    }
    file_size_ = static_cast<std::size_t>(li_size.QuadPart);

    mapping_handle_ = CreateFileMappingW(file_handle_, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping_handle_) {
        close();
        return false;
    }

    mapping_view_ = static_cast<const std::uint8_t*>(MapViewOfFile(mapping_handle_, FILE_MAP_READ, 0, 0, 0));
    if (!mapping_view_) {
        close();
        return false;
    }

    // Header validation: 'PSAR' (0x50, 0x53, 0x41, 0x52)
    if (std::memcmp(mapping_view_, "PSAR", 4) != 0) {
        close();
        return false;
    }

    std::uint32_t toc_length  = read_be32(mapping_view_ + 12);
    std::uint32_t entry_size  = read_be32(mapping_view_ + 16);
    std::uint32_t num_entries = read_be32(mapping_view_ + 20);
    block_size_               = read_be32(mapping_view_ + 24);

    if (entry_size != 30 || num_entries == 0 || toc_length > file_size_ || block_size_ == 0) {
        close();
        return false;
    }

    // Parse block sizes table
    std::size_t bnum_offset = 32 + static_cast<std::size_t>(entry_size) * num_entries;
    if (bnum_offset > toc_length) {
        close();
        return false;
    }

    std::size_t bnum_count = (toc_length - bnum_offset) / sizeof(std::uint16_t);
    zblocks_.resize(bnum_count);
    for (std::size_t i = 0; i < bnum_count; ++i) {
        zblocks_[i] = read_be16(mapping_view_ + bnum_offset + i * sizeof(std::uint16_t));
    }

    // Decompress Entry 0 (Manifest containing filenames separated by newline)
    const std::uint8_t* ent0_ptr = mapping_view_ + 32;
    std::uint32_t ent0_bidx      = read_be32(ent0_ptr + 16);
    std::uint64_t ent0_uncomp_sz = read_be40(ent0_ptr + 20);
    std::uint64_t ent0_start_off = read_be40(ent0_ptr + 25);

    PsarcFileEntry manifest_entry{};
    manifest_entry.path = "$manifest";
    manifest_entry.block_index = ent0_bidx;
    manifest_entry.uncompressed_size = ent0_uncomp_sz;
    manifest_entry.start_offset = ent0_start_off;

    std::vector<std::uint8_t> manifest_data(static_cast<std::size_t>(ent0_uncomp_sz));
    if (!read_file(manifest_entry, manifest_data.data(), manifest_data.size())) {
        close();
        return false;
    }

    // Split manifest lines
    std::vector<std::string> file_paths;
    file_paths.reserve(num_entries);

    std::string_view manifest_sv(reinterpret_cast<const char*>(manifest_data.data()), manifest_data.size());
    std::size_t line_start = 0;
    while (line_start < manifest_sv.size()) {
        std::size_t line_end = manifest_sv.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = manifest_sv.size();

        std::size_t actual_end = line_end;
        if (actual_end > line_start && manifest_sv[actual_end - 1] == '\r') {
            --actual_end;
        }

        if (actual_end > line_start) {
            file_paths.emplace_back(manifest_sv.substr(line_start, actual_end - line_start));
        }

        line_start = line_end + 1;
    }

    // Parse all entries (1 to num_entries - 1)
    entries_.reserve(num_entries);
    path_to_index_.reserve(num_entries);

    for (std::uint32_t i = 1; i < num_entries; ++i) {
        const std::uint8_t* ent_ptr = mapping_view_ + 32 + i * entry_size;
        PsarcFileEntry fe{};
        std::memcpy(fe.md5, ent_ptr, 16);
        fe.block_index       = read_be32(ent_ptr + 16);
        fe.uncompressed_size = read_be40(ent_ptr + 20);
        fe.start_offset      = read_be40(ent_ptr + 25);

        if (i - 1 < file_paths.size()) {
            fe.path = file_paths[i - 1];
        }

        entries_.push_back(fe);
        std::size_t idx = entries_.size() - 1;
        path_to_index_[normalize_path(fe.path)] = idx;
    }

    return true;
}

bool PsarcArchive::has_file(std::string_view path) const noexcept {
    return get_entry(path) != nullptr;
}

const PsarcFileEntry* PsarcArchive::get_entry(std::string_view path) const noexcept {
    auto it = path_to_index_.find(normalize_path(path));
    if (it != path_to_index_.end()) {
        return &entries_[it->second];
    }
    return nullptr;
}

bool PsarcArchive::read_file(const PsarcFileEntry& entry, void* out_buffer, std::size_t buffer_size) const noexcept {
    if (!mapping_view_ || !out_buffer || buffer_size < entry.uncompressed_size) {
        return false;
    }

    if (entry.uncompressed_size == 0) {
        return true;
    }

    std::uint64_t curr_off = entry.start_offset;
    std::size_t remaining = static_cast<std::size_t>(entry.uncompressed_size);
    std::size_t block_idx = entry.block_index;
    std::uint8_t* out_ptr = static_cast<std::uint8_t*>(out_buffer);

    while (remaining > 0) {
        if (block_idx >= zblocks_.size()) {
            return false;
        }

        std::uint16_t bsz = zblocks_[block_idx++];
        std::size_t expected_chunk = (std::min)(remaining, static_cast<std::size_t>(block_size_));

        if (bsz == 0 || bsz == expected_chunk) {
            // A full raw block uses zero; a partial raw block stores its length.
            if (curr_off + expected_chunk > file_size_) return false;
            std::memcpy(out_ptr, mapping_view_ + curr_off, expected_chunk);
            curr_off += expected_chunk;
        } else {
            // Compressed with zlib
            if (curr_off + bsz > file_size_) return false;
            uLongf dest_len = static_cast<uLongf>(expected_chunk);
            int res = uncompress(out_ptr, &dest_len, mapping_view_ + curr_off, bsz);
            if (res != Z_OK || dest_len != expected_chunk) {
                return false;
            }
            curr_off += bsz;
        }

        out_ptr += expected_chunk;
        remaining -= expected_chunk;
    }

    return true;
}

std::vector<std::uint8_t> PsarcArchive::read_file(std::string_view path) const {
    const auto* entry = get_entry(path);
    if (!entry) return {};

    std::vector<std::uint8_t> data(static_cast<std::size_t>(entry->uncompressed_size));
    if (!read_file(*entry, data.data(), data.size())) {
        return {};
    }
    return data;
}

} // namespace gravity::hle::fs
