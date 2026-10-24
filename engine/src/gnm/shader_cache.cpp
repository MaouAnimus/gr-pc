#include "hle/gnm/shader_cache.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <system_error>

namespace gravity::hle::gnm {
namespace {

static_assert(std::endian::native == std::endian::little);
using Digest = std::array<std::byte, 32>;
constexpr std::size_t header_size = 88;
constexpr std::array<std::byte, 8> file_magic{
    std::byte{'G'}, std::byte{'R'}, std::byte{'S'}, std::byte{'P'},
    std::byte{'V'}, std::byte{'C'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::uint32_t cache_format = 1;

class Sha256 {
public:
    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    bool initialize() {
        if (BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
        ULONG size{}, returned{};
        if (BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
                              sizeof(size), &returned, 0) < 0 || returned != sizeof(size) ||
            size == 0 || size > 1024U * 1024U) return false;
        object_.resize(size);
        return BCryptCreateHash(algorithm_, &hash_, object_.data(), size, nullptr, 0, 0) >= 0;
    }
    bool update(std::span<const std::byte> bytes) {
        while (!bytes.empty()) {
            const auto size = static_cast<ULONG>(std::min<std::size_t>(bytes.size(),
                                                       std::numeric_limits<ULONG>::max()));
            if (BCryptHashData(hash_, reinterpret_cast<PUCHAR>(const_cast<std::byte*>(bytes.data())), size, 0) < 0)
                return false;
            bytes = bytes.subspan(size);
        }
        return true;
    }
    bool finish(Digest& digest) {
        return BCryptFinishHash(hash_, reinterpret_cast<PUCHAR>(digest.data()),
                                static_cast<ULONG>(digest.size()), 0) >= 0;
    }
    Sha256() = default;
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
private:
    BCRYPT_ALG_HANDLE algorithm_{};
    BCRYPT_HASH_HANDLE hash_{};
    std::vector<UCHAR> object_;
};

class File {
public:
    explicit File(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
    ~File() { close(); }
    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE get() const { return handle_; }
    [[nodiscard]] bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
private:
    HANDLE handle_;
};

// Declared before the open file, so all handles close before deletion on errors.
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile() { if (!path.empty()) DeleteFileW(path.c_str()); }
};

template <class T>
void put(std::span<std::byte> out, std::size_t offset, T value) {
    std::memcpy(out.data() + offset, &value, sizeof(value));
}
template <class T>
T get(std::span<const std::byte> in, std::size_t offset) {
    T value{};
    std::memcpy(&value, in.data() + offset, sizeof(value));
    return value;
}

std::string hexadecimal(std::span<const std::byte> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto value = std::to_integer<unsigned>(bytes[i]);
        result[2 * i] = digits[value >> 4];
        result[2 * i + 1] = digits[value & 15];
    }
    return result;
}

std::optional<Digest> decode_key(std::string_view key) {
    if (key.size() != 64) return std::nullopt;
    Digest digest{};
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < digest.size(); ++i) {
        const int high = nibble(key[2 * i]), low = nibble(key[2 * i + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        digest[i] = static_cast<std::byte>(high * 16 + low);
    }
    return digest;
}

bool framed_spirv(std::span<const std::uint32_t> words) {
    if (words.size() < 5 || words.size_bytes() > ShaderCache::max_spirv_bytes ||
        words[0] != 0x07230203U || words[1] < 0x00010000U || words[1] > 0x00010600U ||
        (words[1] & 0xffU) != 0 || words[3] == 0 || words[3] > 0x003fffffU || words[4] != 0)
        return false;
    for (std::size_t i = 5; i < words.size();) {
        const std::size_t length = words[i] >> 16;
        if (length == 0 || length > words.size() - i) return false;
        i += length;
    }
    return true;
}

bool transfer(HANDLE file, std::span<std::byte> bytes, bool write) {
    while (!bytes.empty()) {
        const auto size = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1024U * 1024U));
        DWORD transferred{};
        const BOOL ok = write ? WriteFile(file, bytes.data(), size, &transferred, nullptr)
                              : ReadFile(file, bytes.data(), size, &transferred, nullptr);
        if (!ok || transferred == 0 || transferred > size) return false;
        bytes = bytes.subspan(transferred);
    }
    return true;
}

bool hash_payload(std::span<const std::byte> bytes, Digest& digest) {
    Sha256 hash;
    return hash.initialize() && hash.update(bytes) && hash.finish(digest);
}

} // namespace

std::string ShaderCache::key_for(std::span<const std::byte> gcn, std::string_view canonical_options) noexcept {
    try {
        // Length prefixes prevent ambiguous concatenations and embedded NULs are
        // preserved. This domain changes when the cache key contract changes.
        constexpr std::string_view domain = "GravityRush/GCN-SPIRV-cache/key/v1";
        std::array<std::byte, 16> lengths{};
        put<std::uint64_t>(lengths, 0, gcn.size());
        put<std::uint64_t>(lengths, 8, canonical_options.size());
        Sha256 hash;
        Digest digest{};
        if (!hash.initialize() || !hash.update(std::as_bytes(std::span(domain))) ||
            !hash.update(lengths) || !hash.update(gcn) ||
            !hash.update(std::as_bytes(std::span(canonical_options))) || !hash.finish(digest)) return {};
        return hexadecimal(digest);
    } catch (...) { return {}; }
}

std::optional<std::vector<std::uint32_t>> ShaderCache::load(std::string_view key) const noexcept {
    try {
        const auto expected_key = decode_key(key);
        if (!expected_key) return std::nullopt;
        const auto path = root_ / (std::string(key) + ".spv");
        File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        LARGE_INTEGER size{};
        if (!file.valid() || !GetFileSizeEx(file.get(), &size) || size.QuadPart < static_cast<LONGLONG>(header_size) ||
            size.QuadPart > static_cast<LONGLONG>(header_size + max_spirv_bytes)) return std::nullopt;
        std::array<std::byte, header_size> header{};
        if (!transfer(file.get(), header, false) || !std::equal(file_magic.begin(), file_magic.end(), header.begin()) ||
            get<std::uint32_t>(header, 8) != cache_format || get<std::uint32_t>(header, 12) != header_size ||
            !std::equal(expected_key->begin(), expected_key->end(), header.begin() + 24)) return std::nullopt;
        const auto payload_size = get<std::uint64_t>(header, 16);
        if (payload_size < 5 * sizeof(std::uint32_t) || payload_size > max_spirv_bytes ||
            payload_size % sizeof(std::uint32_t) != 0 ||
            payload_size != static_cast<std::uint64_t>(size.QuadPart) - header_size) return std::nullopt;
        std::vector<std::uint32_t> words(static_cast<std::size_t>(payload_size / sizeof(std::uint32_t)));
        if (!transfer(file.get(), std::as_writable_bytes(std::span(words)), false)) return std::nullopt;
        LARGE_INTEGER final_size{};
        Digest digest{};
        if (!GetFileSizeEx(file.get(), &final_size) || final_size.QuadPart != size.QuadPart ||
            !hash_payload(std::as_bytes(std::span(words)), digest) ||
            !std::equal(digest.begin(), digest.end(), header.begin() + 56) || !framed_spirv(words)) return std::nullopt;
        return words;
    } catch (...) { return std::nullopt; }
}

bool ShaderCache::store(std::string_view key, std::span<const std::uint32_t> words) const noexcept {
    try {
        const auto key_digest = decode_key(key);
        if (!key_digest || !framed_spirv(words)) return false;
        Digest checksum{};
        if (!hash_payload(std::as_bytes(words), checksum)) return false;
        std::array<std::byte, header_size> header{};
        std::copy(file_magic.begin(), file_magic.end(), header.begin());
        put<std::uint32_t>(header, 8, cache_format);
        put<std::uint32_t>(header, 12, header_size);
        put<std::uint64_t>(header, 16, words.size_bytes());
        std::copy(key_digest->begin(), key_digest->end(), header.begin() + 24);
        std::copy(checksum.begin(), checksum.end(), header.begin() + 56);

        std::error_code error;
        std::filesystem::create_directories(root_, error);
        if (error) return false;
        const auto destination = root_ / (std::string(key) + ".spv");
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            std::array<std::byte, 16> random{};
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(random.data()),
                                static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
            TemporaryFile temporary{root_ / (std::string(key) + "." + hexadecimal(random) + ".tmp")};
            File file(CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
            if (!file.valid()) {
                if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) {
                    // Never delete a file created by a different writer.
                    temporary.path.clear();
                    continue;
                }
                temporary.path.clear();
                return false;
            }
            // transfer does not modify the source buffer when writing.
            auto payload = std::span<std::byte>(reinterpret_cast<std::byte*>(const_cast<std::uint32_t*>(words.data())),
                                                words.size_bytes());
            if (!transfer(file.get(), header, true) || !transfer(file.get(), payload, true) ||
                !FlushFileBuffers(file.get())) return false;
            file.close();
            for (unsigned retry = 0; retry < 64; ++retry) {
                if (MoveFileExW(temporary.path.c_str(), destination.c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    temporary.path.clear();
                    return true;
                }
                const DWORD failure = GetLastError();
                if (failure != ERROR_SHARING_VIOLATION && failure != ERROR_LOCK_VIOLATION && failure != ERROR_ACCESS_DENIED)
                    return false;
                SwitchToThread();
            }
            return false;
        }
        return false;
    } catch (...) { return false; }
}

} // namespace gravity::hle::gnm
