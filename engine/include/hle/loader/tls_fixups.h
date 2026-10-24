#pragma once
#include <cstdint>
#include <span>
namespace gravity::hle::loader {
// Rejects any executable other than the audited CUSA01130 build. Validates all
// instruction bytes before changing anything in the writable mapped image.
bool apply_gravity_tls_fixups(std::span<const std::uint8_t> source,
                             std::span<std::uint8_t> mapped, std::uint32_t& count);
}
