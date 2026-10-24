#include "hle/font/font.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)

using namespace gravity::hle::font;

int main(int argc, char* argv[]) {
    std::string suite = (argc > 1) ? argv[1] : "all";

    if (suite == "lifecycle" || suite == "all") {
        char mem_buf[1024];
        CHECK(sceFontMemoryInit(mem_buf, sizeof(mem_buf)) == 0);

        void* library = nullptr;
        CHECK(sceFontCreateLibraryWithEdition(nullptr, 0, &library) == 0);
        CHECK(library != nullptr);

        void* renderer = nullptr;
        CHECK(sceFontCreateRendererWithEdition(library, nullptr, 0, &renderer) == 0);
        CHECK(renderer != nullptr);

        CHECK(sceFontBindRenderer(library, renderer) == 0);
        CHECK(sceFontUnbindRenderer(library) == 0);

        CHECK(sceFontDestroyRenderer(renderer) == 0);
        CHECK(sceFontDestroyLibrary(library) == 0);
        CHECK(sceFontMemoryTerm() == 0);
        std::cout << "[PASS] font lifecycle tests succeeded.\n";
    }

    if (suite == "metrics_rendering" || suite == "all") {
        void* library = nullptr;
        CHECK(sceFontCreateLibraryWithEdition(nullptr, 0, &library) == 0);

        // Open dummy memory font
        const std::uint8_t dummy_font[] = { 0, 1, 0, 0 };
        void* font = nullptr;
        CHECK(sceFontOpenFontMemory(library, dummy_font, sizeof(dummy_font), 0, &font) == 0);
        CHECK(font != nullptr);

        CHECK(sceFontSetScalePixel(font, 32.0f, 32.0f) == 0);
        float h = 0.0f, v = 0.0f;
        CHECK(sceFontGetScalePixel(font, &h, &v) == 0);
        CHECK(h == 32.0f && v == 32.0f);

        SceFontGlyphMetrics metrics{};
        CHECK(sceFontGetCharGlyphMetrics(font, 'A', &metrics) == 0);
        CHECK(metrics.width > 0 && metrics.height > 0);

        // Surface rendering test
        std::vector<std::uint8_t> surface_buf(64 * 64, 0);
        SceFontRenderSurface surface{};
        CHECK(sceFontRenderSurfaceInit(&surface, surface_buf.data(), 64, 64, 64, 1) == 0);

        SceFontGlyphRenderImage glyph_img{};
        CHECK(sceFontRenderCharGlyphImageHorizontal(font, 'A', &surface, &glyph_img) == 0);

        // Verify non-zero pixels were rasterized into surface
        bool has_rendered_pixel = false;
        for (std::uint8_t px : surface_buf) {
            if (px > 0) {
                has_rendered_pixel = true;
                break;
            }
        }
        CHECK(has_rendered_pixel);

        SceFontKerning kerning{};
        CHECK(sceFontGetKerning(font, 'A', 'V', &kerning) == 0);

        CHECK(sceFontCloseFont(font) == 0);
        CHECK(sceFontDestroyLibrary(library) == 0);
        std::cout << "[PASS] font metrics_rendering tests succeeded.\n";
    }

    return 0;
}
