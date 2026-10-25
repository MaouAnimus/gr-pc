#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace gravity::hle::gnm {
// Validate complete modules against Vulkan 1.2, including types, interfaces,
// capabilities, and control flow. Cache checksums alone are not validation.
bool validate_spirv(std::span<const std::uint32_t> words, std::string& error);
}
