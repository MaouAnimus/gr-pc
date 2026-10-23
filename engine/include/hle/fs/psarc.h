#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>
#include <Windows.h>

namespace gravity::hle::fs {

struct PsarcFileEntry {
    std::string path;
    std::uint32_t block_index{0};
    std::uint64_t uncompressed_size{0};
    std::uint64_t start_offset{0};
    std::uint8_t  md5[16]{};
};

class PsarcArchive {
public:
    PsarcArchive() = default;
    ~PsarcArchive();

    PsarcArchive(const PsarcArchive&) = delete;
    PsarcArchive& operator=(const PsarcArchive&) = delete;

    PsarcArchive(PsarcArchive&& other) noexcept;
    PsarcArchive& operator=(PsarcArchive&& other) noexcept;

    bool open(const std::wstring& filepath) noexcept;
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return mapping_view_ != nullptr; }
    [[nodiscard]] std::size_t file_count() const noexcept { return entries_.size(); }

    [[nodiscard]] bool has_file(std::string_view path) const noexcept;
    [[nodiscard]] const PsarcFileEntry* get_entry(std::string_view path) const noexcept;

    // Reads and decompresses file content into out_buffer
    bool read_file(const PsarcFileEntry& entry, void* out_buffer, std::size_t buffer_size) const noexcept;

    // Helper to read entire file into a vector
    std::vector<std::uint8_t> read_file(std::string_view path) const;

    // List all file entries in the archive
    [[nodiscard]] const std::vector<PsarcFileEntry>& entries() const noexcept { return entries_; }

    static std::string normalize_path(std::string_view path);

private:
    HANDLE file_handle_{INVALID_HANDLE_VALUE};
    HANDLE mapping_handle_{nullptr};
    const std::uint8_t* mapping_view_{nullptr};
    std::size_t file_size_{0};

    std::uint32_t block_size_{65536};
    std::vector<std::uint16_t> zblocks_{};
    std::vector<PsarcFileEntry> entries_{};
    std::unordered_map<std::string, std::size_t> path_to_index_{};
};

} // namespace gravity::hle::fs
