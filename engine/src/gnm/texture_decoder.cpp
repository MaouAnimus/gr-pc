#include "hle/gnm/texture_decoder.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace gravity::hle::gnm {

TextureFormatInfo decode_gcn_format(std::uint32_t gcn_format) {
    TextureFormatInfo info{};
    switch (gcn_format) {
    case 0x08: // DATA_FORMAT_8
        info.vk_format = VK_FORMAT_R8_UNORM;
        info.bytes_per_pixel = 1;
        info.block_dim = 1;
        info.bytes_per_block = 1;
        info.is_block_compressed = false;
        break;
    case 0x0A: // DATA_FORMAT_8_8
        info.vk_format = VK_FORMAT_R8G8_UNORM;
        info.bytes_per_pixel = 2;
        info.block_dim = 1;
        info.bytes_per_block = 2;
        info.is_block_compressed = false;
        break;
    case 0x0B: // DATA_FORMAT_16
        info.vk_format = VK_FORMAT_R16_UNORM;
        info.bytes_per_pixel = 2;
        info.block_dim = 1;
        info.bytes_per_block = 2;
        info.is_block_compressed = false;
        break;
    case 0x1A: // DATA_FORMAT_8_8_8_8
        info.vk_format = VK_FORMAT_R8G8B8A8_UNORM;
        info.bytes_per_pixel = 4;
        info.block_dim = 1;
        info.bytes_per_block = 4;
        info.is_block_compressed = false;
        break;
    case 0x1B: // DATA_FORMAT_2_10_10_10
        info.vk_format = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        info.bytes_per_pixel = 4;
        info.block_dim = 1;
        info.bytes_per_block = 4;
        info.is_block_compressed = false;
        break;
    case 0x22: // DATA_FORMAT_16_16_16_16
        info.vk_format = VK_FORMAT_R16G16B16A16_SFLOAT;
        info.bytes_per_pixel = 8;
        info.block_dim = 1;
        info.bytes_per_block = 8;
        info.is_block_compressed = false;
        break;
    case 0x24: // DATA_FORMAT_32_32_32_32
        info.vk_format = VK_FORMAT_R32G32B32A32_SFLOAT;
        info.bytes_per_pixel = 16;
        info.block_dim = 1;
        info.bytes_per_block = 16;
        info.is_block_compressed = false;
        break;
    case 0x31: // DATA_FORMAT_BC1
        info.vk_format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        info.bytes_per_pixel = 4;
        info.block_dim = 4;
        info.bytes_per_block = 8;
        info.is_block_compressed = true;
        break;
    case 0x32: // DATA_FORMAT_BC2
        info.vk_format = VK_FORMAT_BC2_UNORM_BLOCK;
        info.bytes_per_pixel = 4;
        info.block_dim = 4;
        info.bytes_per_block = 16;
        info.is_block_compressed = true;
        break;
    case 0x33: // DATA_FORMAT_BC3
        info.vk_format = VK_FORMAT_BC3_UNORM_BLOCK;
        info.bytes_per_pixel = 4;
        info.block_dim = 4;
        info.bytes_per_block = 16;
        info.is_block_compressed = true;
        break;
    case 0x34: // DATA_FORMAT_BC4
        info.vk_format = VK_FORMAT_BC4_UNORM_BLOCK;
        info.bytes_per_pixel = 1;
        info.block_dim = 4;
        info.bytes_per_block = 8;
        info.is_block_compressed = true;
        break;
    case 0x35: // DATA_FORMAT_BC5
        info.vk_format = VK_FORMAT_BC5_UNORM_BLOCK;
        info.bytes_per_pixel = 2;
        info.block_dim = 4;
        info.bytes_per_block = 16;
        info.is_block_compressed = true;
        break;
    case 0x36: // DATA_FORMAT_BC6
        info.vk_format = VK_FORMAT_BC6H_UFLOAT_BLOCK;
        info.bytes_per_pixel = 4;
        info.block_dim = 4;
        info.bytes_per_block = 16;
        info.is_block_compressed = true;
        break;
    case 0x37: // DATA_FORMAT_BC7
        info.vk_format = VK_FORMAT_BC7_UNORM_BLOCK;
        info.bytes_per_pixel = 4;
        info.block_dim = 4;
        info.bytes_per_block = 16;
        info.is_block_compressed = true;
        break;
    default:
        // Default to RGBA8
        info.vk_format = VK_FORMAT_R8G8B8A8_UNORM;
        info.bytes_per_pixel = 4;
        info.block_dim = 1;
        info.bytes_per_block = 4;
        info.is_block_compressed = false;
        break;
    }
    return info;
}

bool detile_surface(const void* src, void* dst,
                    std::uint32_t width, std::uint32_t height,
                    const TextureFormatInfo& info,
                    TextureTiling tiling) {
    if (!src || !dst || !width || !height) return false;

    const std::uint32_t bw = (width + info.block_dim - 1) / info.block_dim;
    const std::uint32_t bh = (height + info.block_dim - 1) / info.block_dim;
    const std::uint32_t bpb = info.bytes_per_block;

    if (tiling == TextureTiling::Linear) {
        std::memcpy(dst, src, static_cast<std::size_t>(bw) * bh * bpb);
        return true;
    }

    const std::uint32_t tiles_x = (bw + 7) / 8;
    const auto* src_bytes = static_cast<const std::uint8_t*>(src);
    auto* dst_bytes = static_cast<std::uint8_t*>(dst);

    for (std::uint32_t by = 0; by < bh; ++by) {
        const std::uint32_t tile_y = by / 8;
        const std::uint32_t in_tile_y = by % 8;
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            const std::uint32_t tile_x = bx / 8;
            const std::uint32_t in_tile_x = bx % 8;

            const std::uint32_t tile_idx = tile_y * tiles_x + tile_x;
            const std::uint32_t morton = morton_2d(in_tile_x, in_tile_y);
            const std::uint32_t src_elem = tile_idx * 64 + morton;
            const std::uint32_t dst_elem = by * bw + bx;

            std::memcpy(dst_bytes + static_cast<std::size_t>(dst_elem) * bpb,
                        src_bytes + static_cast<std::size_t>(src_elem) * bpb,
                        bpb);
        }
    }
    return true;
}

namespace {

struct ColorRGBA {
    std::uint8_t r, g, b, a;
};

ColorRGBA unpack_565(std::uint16_t c) {
    std::uint8_t r = static_cast<std::uint8_t>((c >> 11) & 0x1f);
    std::uint8_t g = static_cast<std::uint8_t>((c >> 5) & 0x3f);
    std::uint8_t b = static_cast<std::uint8_t>(c & 0x1f);
    // Expand to 8 bits
    r = static_cast<std::uint8_t>((r << 3) | (r >> 2));
    g = static_cast<std::uint8_t>((g << 2) | (g >> 4));
    b = static_cast<std::uint8_t>((b << 3) | (b >> 2));
    return {r, g, b, 255};
}

} // namespace

void decompress_bc1_block(const std::uint8_t* block, std::uint32_t* out_rgba, std::size_t out_stride_pixels) {
    std::uint16_t c0_raw = static_cast<std::uint16_t>(block[0] | (block[1] << 8));
    std::uint16_t c1_raw = static_cast<std::uint16_t>(block[2] | (block[3] << 8));
    std::uint32_t table = static_cast<std::uint32_t>(block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24));

    ColorRGBA c[4];
    c[0] = unpack_565(c0_raw);
    c[1] = unpack_565(c1_raw);

    if (c0_raw > c1_raw) {
        c[2].r = static_cast<std::uint8_t>((2 * c[0].r + c[1].r) / 3);
        c[2].g = static_cast<std::uint8_t>((2 * c[0].g + c[1].g) / 3);
        c[2].b = static_cast<std::uint8_t>((2 * c[0].b + c[1].b) / 3);
        c[2].a = 255;

        c[3].r = static_cast<std::uint8_t>((c[0].r + 2 * c[1].r) / 3);
        c[3].g = static_cast<std::uint8_t>((c[0].g + 2 * c[1].g) / 3);
        c[3].b = static_cast<std::uint8_t>((c[0].b + 2 * c[1].b) / 3);
        c[3].a = 255;
    } else {
        c[2].r = static_cast<std::uint8_t>((c[0].r + c[1].r) / 2);
        c[2].g = static_cast<std::uint8_t>((c[0].g + c[1].g) / 2);
        c[2].b = static_cast<std::uint8_t>((c[0].b + c[1].b) / 2);
        c[2].a = 255;

        c[3] = {0, 0, 0, 0}; // Transparent
    }

    for (std::size_t py = 0; py < 4; ++py) {
        for (std::size_t px = 0; px < 4; ++px) {
            std::uint32_t idx = (table >> ((py * 4 + px) * 2)) & 3u;
            const auto& col = c[idx];
            std::uint32_t packed = static_cast<std::uint32_t>(col.r) |
                                  (static_cast<std::uint32_t>(col.g) << 8) |
                                  (static_cast<std::uint32_t>(col.b) << 16) |
                                  (static_cast<std::uint32_t>(col.a) << 24);
            out_rgba[py * out_stride_pixels + px] = packed;
        }
    }
}

void decompress_bc3_block(const std::uint8_t* block, std::uint32_t* out_rgba, std::size_t out_stride_pixels) {
    // First decompress RGB using BC1 logic from block offset + 8
    decompress_bc1_block(block + 8, out_rgba, out_stride_pixels);

    // Alpha block is in first 8 bytes
    std::uint8_t a0 = block[0];
    std::uint8_t a1 = block[1];
    std::uint64_t alpha_indices = 0;
    for (int i = 0; i < 6; ++i) {
        alpha_indices |= static_cast<std::uint64_t>(block[2 + i]) << (i * 8);
    }

    std::uint8_t alphas[8];
    alphas[0] = a0;
    alphas[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            alphas[1 + i] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            alphas[1 + i] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
        }
        alphas[6] = 0;
        alphas[7] = 255;
    }

    for (std::size_t py = 0; py < 4; ++py) {
        for (std::size_t px = 0; px < 4; ++px) {
            std::uint32_t aidx = static_cast<std::uint32_t>((alpha_indices >> ((py * 4 + px) * 3)) & 7u);
            std::uint8_t a = alphas[aidx];
            std::uint32_t cur = out_rgba[py * out_stride_pixels + px];
            out_rgba[py * out_stride_pixels + px] = (cur & 0x00FFFFFFu) | (static_cast<std::uint32_t>(a) << 24);
        }
    }
}

bool decompress_bc_to_rgba8(const void* src_bc, void* dst_rgba8,
                            std::uint32_t width, std::uint32_t height,
                            VkFormat bc_format) {
    if (!src_bc || !dst_rgba8 || !width || !height) return false;

    const bool is_bc1 = (bc_format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || bc_format == VK_FORMAT_BC1_RGB_UNORM_BLOCK);
    const bool is_bc3 = (bc_format == VK_FORMAT_BC3_UNORM_BLOCK);
    if (!is_bc1 && !is_bc3) return false;

    const std::uint32_t bw = (width + 3) / 4;
    const std::uint32_t bh = (height + 3) / 4;
    const std::size_t block_bytes = is_bc1 ? 8 : 16;
    const auto* src_blocks = static_cast<const std::uint8_t*>(src_bc);
    auto* dst_pixels = static_cast<std::uint32_t*>(dst_rgba8);

    std::array<std::uint32_t, 16> block_temp{};

    for (std::uint32_t by = 0; by < bh; ++by) {
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            const auto* block_ptr = src_blocks + (by * bw + bx) * block_bytes;
            if (is_bc1) decompress_bc1_block(block_ptr, block_temp.data(), 4);
            else decompress_bc3_block(block_ptr, block_temp.data(), 4);

            // Copy 4x4 block into destination buffer with clipping at boundary
            const std::uint32_t start_x = bx * 4;
            const std::uint32_t start_y = by * 4;
            for (std::uint32_t py = 0; py < 4 && (start_y + py) < height; ++py) {
                for (std::uint32_t px = 0; px < 4 && (start_x + px) < width; ++px) {
                    dst_pixels[(start_y + py) * width + (start_x + px)] = block_temp[py * 4 + px];
                }
            }
        }
    }
    return true;
}

} // namespace gravity::hle::gnm
