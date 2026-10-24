#pragma once

#include <cstddef>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace gravity::hle::gnm {

enum class TextureTiling : std::uint32_t {
    Linear = 0,
    Tiled1D = 1,
    Tiled2D = 2,
};

struct TextureFormatInfo {
    VkFormat vk_format{VK_FORMAT_R8G8B8A8_UNORM};
    std::uint32_t bytes_per_pixel{4};
    std::uint32_t block_dim{1};       // 1 for uncompressed, 4 for BCn
    std::uint32_t bytes_per_block{4}; // 4 for RGBA8, 8 for BC1/4, 16 for BC2/3/5/7
    bool is_block_compressed{false};
};

// Translates GCN data format field (from T# DWORD 1 bits 25:20) to format info.
TextureFormatInfo decode_gcn_format(std::uint32_t gcn_format);

// Computes 2D Morton (Z-order) swizzle offset for an 8x8 micro-tile element.
constexpr std::uint32_t morton_2d(std::uint32_t x, std::uint32_t y) noexcept {
    return (x & 1u) |
           ((y & 1u) << 1) |
           (((x >> 1) & 1u) << 2) |
           (((y >> 1) & 1u) << 3) |
           (((x >> 2) & 1u) << 4) |
           (((y >> 2) & 1u) << 5);
}

// Detiles a texture surface from AMD GCN tiled memory layout into a linear raster buffer.
bool detile_surface(const void* src, void* dst,
                    std::uint32_t width, std::uint32_t height,
                    const TextureFormatInfo& info,
                    TextureTiling tiling);

// Decompresses a BC1 (DXT1) 4x4 block to 16 RGBA8 pixels.
void decompress_bc1_block(const std::uint8_t* block, std::uint32_t* out_rgba, std::size_t out_stride_pixels);

// Decompresses a BC3 (DXT5) 4x4 block to 16 RGBA8 pixels.
void decompress_bc3_block(const std::uint8_t* block, std::uint32_t* out_rgba, std::size_t out_stride_pixels);

// Decompresses a whole BC1 or BC3 surface to RGBA8 linear memory.
bool decompress_bc_to_rgba8(const void* src_bc, void* dst_rgba8,
                            std::uint32_t width, std::uint32_t height,
                            VkFormat bc_format);

} // namespace gravity::hle::gnm
