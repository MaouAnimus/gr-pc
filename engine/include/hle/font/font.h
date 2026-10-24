#pragma once

#include "hle/abi.h"
#include <cstdint>
#include <cstddef>

namespace gravity::hle::font {

constexpr std::int32_t SCE_FONT_OK = 0;
constexpr std::int32_t SCE_FONT_ERROR_INVALID_PARAMETER = -2138767359; // 0x80720001
constexpr std::int32_t SCE_FONT_ERROR_OUT_OF_MEMORY = -2138767358;

struct SceFontMemoryInit {
    void* buffer;
    std::size_t size;
};

struct SceFontScalePixel {
    float h;
    float v;
};

struct SceFontGlyphMetrics {
    std::uint32_t width;
    std::uint32_t height;
    std::int32_t  bearingX;
    std::int32_t  bearingY;
    std::int32_t  advanceX;
    std::int32_t  advanceY;
};

struct SceFontRenderSurface {
    void* buffer;
    std::uint32_t pixelFormat;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pitch;
};

struct SceFontGlyphRenderImage {
    SceFontRenderSurface* surface;
    SceFontGlyphMetrics metrics;
    std::int32_t x;
    std::int32_t y;
};

struct SceFontKerning {
    std::int32_t x;
    std::int32_t y;
};

// Font pipeline initialization & query status
bool initialize_font_system();
void shutdown_font_system();
std::size_t active_font_count();

extern "C" {

// libSceFontFt
GRAVITY_SYSV_ABI const void* sceFontSelectLibraryFt(std::uint32_t index);
GRAVITY_SYSV_ABI const void* sceFontSelectRendererFt(std::uint32_t index);

// libSceFont
GRAVITY_SYSV_ABI std::int32_t sceFontMemoryInit(void* mem, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceFontMemoryTerm();
GRAVITY_SYSV_ABI std::int32_t sceFontCreateLibraryWithEdition(const void* config, std::uint32_t edition, void** outLibrary);
GRAVITY_SYSV_ABI std::int32_t sceFontDestroyLibrary(void* library);
GRAVITY_SYSV_ABI std::int32_t sceFontCreateRendererWithEdition(void* library, const void* config, std::uint32_t edition, void** outRenderer);
GRAVITY_SYSV_ABI std::int32_t sceFontDestroyRenderer(void* renderer);
GRAVITY_SYSV_ABI std::int32_t sceFontBindRenderer(void* library, void* renderer);
GRAVITY_SYSV_ABI std::int32_t sceFontUnbindRenderer(void* library);
GRAVITY_SYSV_ABI std::int32_t sceFontSupportSystemFonts(void* library);
GRAVITY_SYSV_ABI std::int32_t sceFontSupportExternalFonts(void* library);
GRAVITY_SYSV_ABI std::int32_t sceFontAttachDeviceCacheBuffer(void* renderer, void* buffer, std::size_t size);
GRAVITY_SYSV_ABI std::int32_t sceFontOpenFontMemory(void* library, const void* memory, std::size_t size, std::uint32_t index, void** outFont);
GRAVITY_SYSV_ABI std::int32_t sceFontCloseFont(void* font);
GRAVITY_SYSV_ABI std::int32_t sceFontSetScalePixel(void* font, float scaleH, float scaleV);
GRAVITY_SYSV_ABI std::int32_t sceFontGetScalePixel(void* font, float* outH, float* outV);
GRAVITY_SYSV_ABI std::int32_t sceFontSetupRenderScalePixel(void* font, float scaleH, float scaleV);
GRAVITY_SYSV_ABI std::int32_t sceFontGetCharGlyphMetrics(void* font, std::uint32_t charCode, SceFontGlyphMetrics* outMetrics);
GRAVITY_SYSV_ABI std::int32_t sceFontRenderCharGlyphImageHorizontal(void* font, std::uint32_t charCode, SceFontRenderSurface* surface, SceFontGlyphRenderImage* outImage);
GRAVITY_SYSV_ABI std::int32_t sceFontRenderSurfaceInit(SceFontRenderSurface* surface, void* buffer, std::uint32_t pitch, std::uint32_t width, std::uint32_t height, std::uint32_t pixelFormat);
GRAVITY_SYSV_ABI std::int32_t sceFontGetKerning(void* font, std::uint32_t charCode1, std::uint32_t charCode2, SceFontKerning* outKerning);

}

} // namespace gravity::hle::font
