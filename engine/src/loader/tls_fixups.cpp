#include "hle/loader/tls_fixups.h"
#include "hle/native_tls.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <iterator>
#include <limits>

namespace gravity::hle::loader {
namespace {
#include "gravity_tls_profile.inc"
}
bool apply_gravity_tls_fixups(std::span<const std::uint8_t> source,
                             std::span<std::uint8_t> mapped, std::uint32_t& count) {
    count = 0;
    if (source.empty() || source.size() > std::numeric_limits<ULONG>::max() || !NativeTls::supported()) return false;
    std::array<std::uint8_t, 32> hash{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(source.data()),
                   static_cast<ULONG>(source.size()), hash.data(), static_cast<ULONG>(hash.size())) < 0) return false;
    char hex[65]{};
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < hash.size(); ++i) { hex[2*i] = digits[hash[i] >> 4]; hex[2*i+1] = digits[hash[i] & 15]; }
    if (std::strcmp(hex, profile_sha256)) return false;
    // Preflight every candidate, including opcode form and range.
    for (auto offset : profile_offsets) {
        if (offset > mapped.size() || mapped.size() - offset < 9) return false;
        std::array<std::uint8_t, 9> candidate;
        std::memcpy(candidate.data(), mapped.data() + offset, candidate.size());
        if (!NativeTls::patch_fs_load(candidate)) return false;
    }
    for (auto offset : profile_offsets)
        if (!NativeTls::patch_fs_load(mapped.subspan(offset, 9))) return false;
    count = static_cast<std::uint32_t>(std::size(profile_offsets));
    return FlushInstructionCache(GetCurrentProcess(), mapped.data(), mapped.size());
}
}
