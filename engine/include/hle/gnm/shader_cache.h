#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gravity::hle::gnm {

// An integrity-checked cache of successfully compiled SPIR-V. The caller must
// validate the module for its Vulkan target before storing it. Checks here cover
// file integrity and SPIR-V framing, not instruction semantics or capabilities.
class ShaderCache {
public:
    static constexpr std::size_t max_spirv_bytes = 64U * 1024U * 1024U;

    explicit ShaderCache(std::filesystem::path root = ".spv_cache")
        : root_(std::move(root)) {}

    // canonical_options must contain translator/compiler version, shader stage,
    // target environment, all flags, and the complete interface/resource layout.
    // Empty means the platform SHA-256 provider failed; it is never a cache key.
    [[nodiscard]] static std::string key_for(std::span<const std::byte> gcn,
                                             std::string_view canonical_options) noexcept;
    [[nodiscard]] static std::string key_for(std::span<const std::uint8_t> gcn,
                                             std::string_view canonical_options) noexcept {
        return key_for(std::as_bytes(gcn), canonical_options);
    }

    [[nodiscard]] std::optional<std::vector<std::uint32_t>> load(std::string_view key) const noexcept;
    [[nodiscard]] bool store(std::string_view key, std::span<const std::uint32_t> words) const noexcept;
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    std::filesystem::path root_;
};

} // namespace gravity::hle::gnm
