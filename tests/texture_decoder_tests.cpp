#include "hle/gnm/texture_decoder.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unordered_set>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

using namespace gravity::hle::gnm;

namespace {

void test_morton_math() {
    CHECK(morton_2d(0, 0) == 0);
    CHECK(morton_2d(1, 0) == 1);
    CHECK(morton_2d(0, 1) == 2);
    CHECK(morton_2d(1, 1) == 3);
    CHECK(morton_2d(2, 0) == 4);
    CHECK(morton_2d(0, 2) == 8);
    CHECK(morton_2d(4, 0) == 16);
    CHECK(morton_2d(0, 4) == 32);
    CHECK(morton_2d(7, 7) == 63);

    std::unordered_set<std::uint32_t> seen;
    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            std::uint32_t m = morton_2d(x, y);
            CHECK(m < 64);
            CHECK(seen.insert(m).second); // strictly unique bijection
        }
    }
    CHECK(seen.size() == 64);
}

void test_format_decode() {
    auto f_rgba8 = decode_gcn_format(0x1A);
    CHECK(f_rgba8.vk_format == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(f_rgba8.bytes_per_pixel == 4);
    CHECK(f_rgba8.block_dim == 1);
    CHECK(f_rgba8.bytes_per_block == 4);
    CHECK(!f_rgba8.is_block_compressed);

    auto f_bc1 = decode_gcn_format(0x31);
    CHECK(f_bc1.vk_format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    CHECK(f_bc1.block_dim == 4);
    CHECK(f_bc1.bytes_per_block == 8);
    CHECK(f_bc1.is_block_compressed);

    auto f_bc3 = decode_gcn_format(0x33);
    CHECK(f_bc3.vk_format == VK_FORMAT_BC3_UNORM_BLOCK);
    CHECK(f_bc3.block_dim == 4);
    CHECK(f_bc3.bytes_per_block == 16);
    CHECK(f_bc3.is_block_compressed);

    auto f_bc7 = decode_gcn_format(0x37);
    CHECK(f_bc7.vk_format == VK_FORMAT_BC7_UNORM_BLOCK);
    CHECK(f_bc7.block_dim == 4);
    CHECK(f_bc7.bytes_per_block == 16);
    CHECK(f_bc7.is_block_compressed);

    auto f_unknown = decode_gcn_format(0xFF);
    CHECK(f_unknown.vk_format == VK_FORMAT_R8G8B8A8_UNORM);
    CHECK(!f_unknown.is_block_compressed);
}

void test_detile_linear() {
    std::array<std::uint32_t, 64> src{};
    std::array<std::uint32_t, 64> dst{};
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<std::uint32_t>(0xAABB0000 + i);
    }

    TextureFormatInfo info = decode_gcn_format(0x1A); // RGBA8
    CHECK(detile_surface(src.data(), dst.data(), 8, 8, info, TextureTiling::Linear));
    CHECK(std::memcmp(src.data(), dst.data(), sizeof(src)) == 0);
}

void test_detile_morton() {
    // 8x8 image with 64 pixels
    std::array<std::uint32_t, 64> linear{};
    std::array<std::uint32_t, 64> tiled{};
    std::array<std::uint32_t, 64> detiled{};

    for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
            linear[y * 8 + x] = (y << 16) | x | 0xFF000000u;
            std::uint32_t mort = morton_2d(x, y);
            tiled[mort] = linear[y * 8 + x];
        }
    }

    TextureFormatInfo info = decode_gcn_format(0x1A);
    CHECK(detile_surface(tiled.data(), detiled.data(), 8, 8, info, TextureTiling::Tiled2D));

    for (size_t i = 0; i < 64; ++i) {
        CHECK(detiled[i] == linear[i]);
    }
}

void test_bc1_decompression() {
    // Construct a simple BC1 block:
    // Color 0: Pure red (565 = 11111 000000 00000 = 0xF800)
    // Color 1: Pure blue (565 = 00000 000000 11111 = 0x001F)
    // c0 > c1, so 4 colors available:
    // idx 0 = c0 (red)
    // idx 1 = c1 (blue)
    // idx 2 = 2/3 red + 1/3 blue
    // idx 3 = 1/3 red + 2/3 blue
    std::array<std::uint8_t, 8> block{};
    block[0] = 0x00; block[1] = 0xF8; // c0 = 0xF800
    block[2] = 0x1F; block[3] = 0x00; // c1 = 0x001F
    // Row 0: 0, 1, 2, 3 -> (00, 01, 10, 11) in 2-bit fields = 0b11100100 = 0xE4
    block[4] = 0xE4;
    block[5] = 0x00; block[6] = 0x00; block[7] = 0x00;

    std::array<std::uint32_t, 16> out{};
    decompress_bc1_block(block.data(), out.data(), 4);

    // Pixel (0,0) is idx 0 (Red)
    std::uint32_t p0 = out[0];
    std::uint8_t r0 = p0 & 0xFF;
    std::uint8_t g0 = (p0 >> 8) & 0xFF;
    std::uint8_t b0 = (p0 >> 16) & 0xFF;
    std::uint8_t a0 = (p0 >> 24) & 0xFF;
    CHECK(r0 == 255 && g0 == 0 && b0 == 0 && a0 == 255);

    // Pixel (1,0) is idx 1 (Blue)
    std::uint32_t p1 = out[1];
    std::uint8_t r1 = p1 & 0xFF;
    std::uint8_t g1 = (p1 >> 8) & 0xFF;
    std::uint8_t b1 = (p1 >> 16) & 0xFF;
    std::uint8_t a1 = (p1 >> 24) & 0xFF;
    CHECK(r1 == 0 && g1 == 0 && b1 == 255 && a1 == 255);

    // Pixel (2,0) is idx 2 (2/3 Red + 1/3 Blue)
    std::uint32_t p2 = out[2];
    std::uint8_t r2 = p2 & 0xFF;
    std::uint8_t b2 = (p2 >> 16) & 0xFF;
    CHECK(r2 == 170 && b2 == 85);
}

void test_bc3_decompression() {
    // 16 bytes: 8 bytes alpha, 8 bytes BC1 color
    std::array<std::uint8_t, 16> block{};
    // Alpha: a0 = 200, a1 = 100
    block[0] = 200;
    block[1] = 100;
    // Set all pixel alpha indices to 0 (a0)
    for (int i = 2; i < 8; ++i) block[i] = 0;

    // Color: White (0xFFFF, 0xFFFF)
    block[8] = 0xFF; block[9] = 0xFF;
    block[10] = 0xFF; block[11] = 0xFF;
    block[12] = 0; block[13] = 0; block[14] = 0; block[15] = 0;

    std::array<std::uint32_t, 16> out{};
    decompress_bc3_block(block.data(), out.data(), 4);

    for (size_t i = 0; i < 16; ++i) {
        std::uint8_t r = out[i] & 0xFF;
        std::uint8_t g = (out[i] >> 8) & 0xFF;
        std::uint8_t b = (out[i] >> 16) & 0xFF;
        std::uint8_t a = (out[i] >> 24) & 0xFF;
        CHECK(r == 255 && g == 255 && b == 255 && a == 200);
    }
}

void test_surface_decompression() {
    // 8x8 surface with 4 BC1 blocks (8 bytes each = 32 bytes)
    std::vector<std::uint8_t> src(32, 0);
    // Fill each block with pure green (565 = 0x07E0)
    for (size_t b = 0; b < 4; ++b) {
        src[b * 8 + 0] = 0xE0;
        src[b * 8 + 1] = 0x07;
        src[b * 8 + 2] = 0xE0;
        src[b * 8 + 3] = 0x07;
    }

    std::vector<std::uint32_t> dst(64, 0);
    CHECK(decompress_bc_to_rgba8(src.data(), dst.data(), 8, 8, VK_FORMAT_BC1_RGBA_UNORM_BLOCK));

    for (size_t i = 0; i < 64; ++i) {
        std::uint8_t r = dst[i] & 0xFF;
        std::uint8_t g = (dst[i] >> 8) & 0xFF;
        std::uint8_t b = (dst[i] >> 16) & 0xFF;
        std::uint8_t a = (dst[i] >> 24) & 0xFF;
        CHECK(r == 0 && g == 255 && b == 0 && a == 255);
    }
}

void test_invalid_parameters() {
    std::uint32_t dummy = 0;
    TextureFormatInfo info = decode_gcn_format(0x1A);

    CHECK(!detile_surface(nullptr, &dummy, 8, 8, info, TextureTiling::Linear));
    CHECK(!detile_surface(&dummy, nullptr, 8, 8, info, TextureTiling::Linear));
    CHECK(!detile_surface(&dummy, &dummy, 0, 8, info, TextureTiling::Linear));
    CHECK(!detile_surface(&dummy, &dummy, 8, 0, info, TextureTiling::Linear));

    CHECK(!decompress_bc_to_rgba8(nullptr, &dummy, 8, 8, VK_FORMAT_BC1_RGBA_UNORM_BLOCK));
    CHECK(!decompress_bc_to_rgba8(&dummy, nullptr, 8, 8, VK_FORMAT_BC1_RGBA_UNORM_BLOCK));
    CHECK(!decompress_bc_to_rgba8(&dummy, &dummy, 8, 8, VK_FORMAT_R8G8B8A8_UNORM)); // not BC1 or BC3
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view suite = argc > 1 ? argv[1] : "all";
    bool recognized = false;

    for (const auto& [name, run] : std::array<std::pair<std::string_view, void (*)()>, 8>{{
             {"morton", test_morton_math},
             {"format", test_format_decode},
             {"linear", test_detile_linear},
             {"detile", test_detile_morton},
             {"bc1", test_bc1_decompression},
             {"bc3", test_bc3_decompression},
             {"surface", test_surface_decompression},
             {"invalid", test_invalid_parameters}}}) {
        if (suite == "all" || suite == name) {
            run();
            recognized = true;
        }
    }

    CHECK(recognized);
    std::printf("texture_decoder_%.*s: OK\n", static_cast<int>(suite.size()), suite.data());
    return 0;
}
