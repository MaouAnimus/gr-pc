#include "hle/font/font.h"
#include "hle/guest_memory.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstring>
#include <cmath>
#include <algorithm>

#if __has_include(<ft2build.h>)
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#define GRAVITY_HAS_FREETYPE 1
#else
#define GRAVITY_HAS_FREETYPE 0
#endif

namespace gravity::hle::font {

namespace {

// Standard built-in 8x16 font table for fallback glyph rasterization
static const std::uint8_t fallback_glyph_data[128][16] = {
    // 0-31: non-printable default dot/space
    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
    {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
    // 32: ' '
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 33: '!'
    {0x00, 0x00, 0x18, 0x3c, 0x3c, 0x3c, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00},
    // 34: '"'
    {0x00, 0x66, 0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 35: '#'
    {0x00, 0x00, 0x6c, 0x6c, 0xfe, 0x6c, 0x6c, 0x6c, 0xfe, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 36: '$'
    {0x18, 0x18, 0x7c, 0xc6, 0xc2, 0xc0, 0x7c, 0x06, 0x06, 0x86, 0xc6, 0x7c, 0x18, 0x18, 0x00, 0x00},
    // 37: '%'
    {0x00, 0x00, 0xc2, 0xc6, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0xc6, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 38: '&'
    {0x00, 0x38, 0x6c, 0x64, 0x68, 0x30, 0x76, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 39: '\''
    {0x00, 0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 40: '('
    {0x00, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x18, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 41: ')'
    {0x00, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 42: '*'
    {0x00, 0x00, 0x00, 0x66, 0x3c, 0xff, 0x3c, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 43: '+'
    {0x00, 0x00, 0x18, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 44: ','
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00},
    // 45: '-'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 46: '.'
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 47: '/'
    {0x00, 0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 48: '0'
    {0x00, 0x3c, 0x66, 0xc3, 0xc3, 0xdb, 0xc3, 0xc3, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 49: '1'
    {0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 50: '2'
    {0x00, 0x7c, 0xc6, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 51: '3'
    {0x00, 0x7c, 0xc6, 0x06, 0x06, 0x3c, 0x06, 0x06, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 52: '4'
    {0x00, 0x0c, 0x1c, 0x3c, 0x6c, 0xcc, 0xfe, 0x0c, 0x0c, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 53: '5'
    {0x00, 0xfe, 0xc0, 0xc0, 0xfc, 0x06, 0x06, 0x06, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 54: '6'
    {0x00, 0x38, 0x60, 0xc0, 0xc0, 0xfc, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 55: '7'
    {0x00, 0xfe, 0xc6, 0x06, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 56: '8'
    {0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0x7c, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 57: '9'
    {0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0x7e, 0x06, 0x06, 0x0c, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 58: ':'
    {0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 59: ';'
    {0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 60: '<'
    {0x00, 0x06, 0x18, 0x60, 0xc0, 0x60, 0x18, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 61: '='
    {0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 62: '>'
    {0x00, 0x60, 0x18, 0x06, 0x03, 0x06, 0x18, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 63: '?'
    {0x00, 0x7c, 0xc6, 0x06, 0x0c, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 64: '@'
    {0x00, 0x7c, 0xc6, 0xc6, 0xde, 0xde, 0xde, 0xdc, 0xc0, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 65: 'A'
    {0x00, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 66: 'B'
    {0x00, 0xfc, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x66, 0x66, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 67: 'C'
    {0x00, 0x3c, 0x66, 0xc2, 0xc0, 0xc0, 0xc0, 0xc2, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 68: 'D'
    {0x00, 0xf8, 0x6c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x6c, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 69: 'E'
    {0x00, 0xfe, 0x66, 0x62, 0x68, 0x78, 0x68, 0x60, 0x62, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 70: 'F'
    {0x00, 0xfe, 0x66, 0x62, 0x68, 0x78, 0x68, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 71: 'G'
    {0x00, 0x3c, 0x66, 0xc2, 0xc0, 0xc0, 0xde, 0xc6, 0x66, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 72: 'H'
    {0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 73: 'I'
    {0x00, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 74: 'J'
    {0x00, 0x1e, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0xcc, 0xcc, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 75: 'K'
    {0x00, 0xe6, 0x66, 0x6c, 0x78, 0x78, 0x6c, 0x66, 0x66, 0xe7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 76: 'L'
    {0x00, 0xf0, 0x60, 0x60, 0x60, 0x60, 0x60, 0x62, 0x66, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 77: 'M'
    {0x00, 0xc3, 0xe7, 0xff, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 78: 'N'
    {0x00, 0xc7, 0xe7, 0xf7, 0xdf, 0xdf, 0xcf, 0xc7, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 79: 'O'
    {0x00, 0x3c, 0x66, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 80: 'P'
    {0x00, 0xfc, 0x66, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 81: 'Q'
    {0x00, 0x3c, 0x66, 0xc3, 0xc3, 0xc3, 0xdb, 0xc7, 0x66, 0x3c, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 82: 'R'
    {0x00, 0xfc, 0x66, 0x66, 0x66, 0x7c, 0x6c, 0x66, 0x66, 0xe7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 83: 'S'
    {0x00, 0x7c, 0xc6, 0xc0, 0x60, 0x38, 0x0c, 0x06, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 84: 'T'
    {0x00, 0xff, 0xdb, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 85: 'U'
    {0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 86: 'V'
    {0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66, 0x66, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 87: 'W'
    {0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xdb, 0xff, 0xe7, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 88: 'X'
    {0x00, 0xc3, 0xc3, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 89: 'Y'
    {0x00, 0xc3, 0xc3, 0x66, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 90: 'Z'
    {0x00, 0xff, 0xc3, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc3, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

struct FontObject {
    std::uint32_t handle{1};
    float scaleH{16.0f};
    float scaleV{16.0f};
#if GRAVITY_HAS_FREETYPE
    FT_Face face{nullptr};
#endif
    std::vector<std::uint8_t> memory_copy{};
};

struct FontSystem {
    std::mutex mutex;
    bool initialized{false};
#if GRAVITY_HAS_FREETYPE
    FT_Library ft_library{nullptr};
#endif
    std::uint32_t next_font_id{1};
    std::unordered_map<std::uint32_t, FontObject> fonts{};
};

FontSystem& sys() {
    static FontSystem fs;
    return fs;
}

} // anonymous namespace

bool initialize_font_system() {
    std::lock_guard<std::mutex> lock(sys().mutex);
    if (sys().initialized) return true;
#if GRAVITY_HAS_FREETYPE
    if (FT_Init_FreeType(&sys().ft_library) != 0) {
        sys().ft_library = nullptr;
    }
#endif
    sys().initialized = true;
    return true;
}

void shutdown_font_system() {
    std::lock_guard<std::mutex> lock(sys().mutex);
#if GRAVITY_HAS_FREETYPE
    for (auto& [id, font] : sys().fonts) {
        if (font.face) {
            FT_Done_Face(font.face);
            font.face = nullptr;
        }
    }
    if (sys().ft_library) {
        FT_Done_FreeType(sys().ft_library);
        sys().ft_library = nullptr;
    }
#endif
    sys().fonts.clear();
    sys().initialized = false;
}

std::size_t active_font_count() {
    std::lock_guard<std::mutex> lock(sys().mutex);
    return sys().fonts.size();
}

extern "C" {

GRAVITY_SYSV_ABI const void* sceFontSelectLibraryFt(std::uint32_t index) {
    (void)index;
    initialize_font_system();
    static const int dummy_lib_ft = 1;
    return &dummy_lib_ft;
}

GRAVITY_SYSV_ABI const void* sceFontSelectRendererFt(std::uint32_t index) {
    (void)index;
    static const int dummy_renderer_ft = 1;
    return &dummy_renderer_ft;
}

GRAVITY_SYSV_ABI std::int32_t sceFontMemoryInit(void* mem, std::size_t size) {
    (void)mem; (void)size;
    initialize_font_system();
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontMemoryTerm() {
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontCreateLibraryWithEdition(const void* config, std::uint32_t edition, void** outLibrary) {
    (void)config; (void)edition;
    if (!outLibrary || !guest_memory::accessible(outLibrary, sizeof(*outLibrary))) {
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }
    initialize_font_system();
    static int library_handle = 100;
    *outLibrary = &library_handle;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontDestroyLibrary(void* library) {
    (void)library;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontCreateRendererWithEdition(void* library, const void* config, std::uint32_t edition, void** outRenderer) {
    (void)library; (void)config; (void)edition;
    if (!outRenderer || !guest_memory::accessible(outRenderer, sizeof(*outRenderer))) {
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }
    static int renderer_handle = 200;
    *outRenderer = &renderer_handle;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontDestroyRenderer(void* renderer) {
    (void)renderer;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontBindRenderer(void* library, void* renderer) {
    (void)library; (void)renderer;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontUnbindRenderer(void* library) {
    (void)library;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontSupportSystemFonts(void* library) {
    (void)library;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontSupportExternalFonts(void* library) {
    (void)library;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontAttachDeviceCacheBuffer(void* renderer, void* buffer, std::size_t size) {
    (void)renderer; (void)buffer; (void)size;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontOpenFontMemory(void* library, const void* memory, std::size_t size, std::uint32_t index, void** outFont) {
    (void)library; (void)index;
    if (!memory || size == 0 || !outFont || !guest_memory::accessible(outFont, sizeof(*outFont))) {
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }

    initialize_font_system();
    std::lock_guard<std::mutex> lock(sys().mutex);

    FontObject font;
    font.handle = sys().next_font_id++;
    font.memory_copy.assign(reinterpret_cast<const std::uint8_t*>(memory),
                            reinterpret_cast<const std::uint8_t*>(memory) + size);

#if GRAVITY_HAS_FREETYPE
    if (sys().ft_library) {
        FT_New_Memory_Face(sys().ft_library,
                           font.memory_copy.data(),
                           static_cast<FT_Long>(font.memory_copy.size()),
                           static_cast<FT_Long>(index),
                           &font.face);
    }
#endif

    sys().fonts[font.handle] = std::move(font);
    *outFont = reinterpret_cast<void*>(static_cast<std::uintptr_t>(sys().fonts.find(sys().next_font_id - 1)->first));
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontCloseFont(void* font) {
    if (!font) return SCE_FONT_OK;
    std::uint32_t id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(font));
    std::lock_guard<std::mutex> lock(sys().mutex);
    auto it = sys().fonts.find(id);
    if (it != sys().fonts.end()) {
#if GRAVITY_HAS_FREETYPE
        if (it->second.face) {
            FT_Done_Face(it->second.face);
        }
#endif
        sys().fonts.erase(it);
    }
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontSetScalePixel(void* font, float scaleH, float scaleV) {
    if (!font) return SCE_FONT_ERROR_INVALID_PARAMETER;
    std::uint32_t id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(font));
    std::lock_guard<std::mutex> lock(sys().mutex);
    auto it = sys().fonts.find(id);
    if (it == sys().fonts.end()) return SCE_FONT_ERROR_INVALID_PARAMETER;
    it->second.scaleH = scaleH;
    it->second.scaleV = scaleV;
#if GRAVITY_HAS_FREETYPE
    if (it->second.face) {
        FT_Set_Pixel_Sizes(it->second.face, static_cast<FT_UInt>(scaleH), static_cast<FT_UInt>(scaleV));
    }
#endif
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontGetScalePixel(void* font, float* outH, float* outV) {
    if (!font || !outH || !outV || !guest_memory::accessible(outH, sizeof(*outH)) || !guest_memory::accessible(outV, sizeof(*outV))) {
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }
    std::uint32_t id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(font));
    std::lock_guard<std::mutex> lock(sys().mutex);
    auto it = sys().fonts.find(id);
    if (it == sys().fonts.end()) {
        *outH = 16.0f;
        *outV = 16.0f;
        return SCE_FONT_OK;
    }
    *outH = it->second.scaleH;
    *outV = it->second.scaleV;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontSetupRenderScalePixel(void* font, float scaleH, float scaleV) {
    return sceFontSetScalePixel(font, scaleH, scaleV);
}

GRAVITY_SYSV_ABI std::int32_t sceFontGetCharGlyphMetrics(void* font, std::uint32_t charCode, SceFontGlyphMetrics* outMetrics) {
    if (!outMetrics || !guest_memory::accessible(outMetrics, sizeof(*outMetrics))) {
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }

    std::uint32_t id = font ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(font)) : 0;
    std::lock_guard<std::mutex> lock(sys().mutex);
    auto it = sys().fonts.find(id);

#if GRAVITY_HAS_FREETYPE
    if (it != sys().fonts.end() && it->second.face) {
        if (FT_Load_Char(it->second.face, charCode, FT_LOAD_DEFAULT) == 0) {
            auto* glyph = it->second.face->glyph;
            outMetrics->width = static_cast<std::uint32_t>(glyph->metrics.width >> 6);
            outMetrics->height = static_cast<std::uint32_t>(glyph->metrics.height >> 6);
            outMetrics->bearingX = static_cast<std::int32_t>(glyph->metrics.horiBearingX >> 6);
            outMetrics->bearingY = static_cast<std::int32_t>(glyph->metrics.horiBearingY >> 6);
            outMetrics->advanceX = static_cast<std::int32_t>(glyph->metrics.horiAdvance >> 6);
            outMetrics->advanceY = static_cast<std::int32_t>(glyph->metrics.vertAdvance >> 6);
            return SCE_FONT_OK;
        }
    }
#endif

    // Fallback metrics
    float scale = (it != sys().fonts.end()) ? it->second.scaleH : 16.0f;
    if (scale <= 0.0f) scale = 16.0f;
    outMetrics->width = static_cast<std::uint32_t>(scale * 0.5f);
    outMetrics->height = static_cast<std::uint32_t>(scale);
    outMetrics->bearingX = 0;
    outMetrics->bearingY = static_cast<std::int32_t>(scale);
    outMetrics->advanceX = static_cast<std::int32_t>(scale * 0.55f);
    outMetrics->advanceY = 0;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontRenderSurfaceInit(SceFontRenderSurface* surface, void* buffer, std::uint32_t pitch, std::uint32_t width, std::uint32_t height, std::uint32_t pixelFormat) {
    if (!surface || !guest_memory::accessible(surface, sizeof(*surface))) {
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }
    surface->buffer = buffer;
    surface->pitch = pitch;
    surface->width = width;
    surface->height = height;
    surface->pixelFormat = pixelFormat;
    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontRenderCharGlyphImageHorizontal(void* font, std::uint32_t charCode, SceFontRenderSurface* surface, SceFontGlyphRenderImage* outImage) {
    if (!surface || !surface->buffer || surface->width == 0 || surface->height == 0) {
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }

    std::uint32_t id = font ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(font)) : 0;
    std::lock_guard<std::mutex> lock(sys().mutex);
    auto it = sys().fonts.find(id);

#if GRAVITY_HAS_FREETYPE
    if (it != sys().fonts.end() && it->second.face) {
        if (FT_Load_Char(it->second.face, charCode, FT_LOAD_RENDER) == 0) {
            auto* glyph = it->second.face->glyph;
            auto& bmp = glyph->bitmap;
            auto* dst = static_cast<std::uint8_t*>(surface->buffer);

            std::uint32_t copy_w = (std::min)(static_cast<std::uint32_t>(bmp.width), surface->width);
            std::uint32_t copy_h = (std::min)(static_cast<std::uint32_t>(bmp.rows), surface->height);

            for (std::uint32_t y = 0; y < copy_h; ++y) {
                const auto* src_row = bmp.buffer + y * bmp.pitch;
                auto* dst_row = dst + y * surface->pitch;
                for (std::uint32_t x = 0; x < copy_w; ++x) {
                    dst_row[x] = (std::max)(dst_row[x], src_row[x]);
                }
            }

            if (outImage && guest_memory::accessible(outImage, sizeof(*outImage))) {
                outImage->surface = surface;
                outImage->metrics.width = bmp.width;
                outImage->metrics.height = bmp.rows;
                outImage->metrics.bearingX = glyph->bitmap_left;
                outImage->metrics.bearingY = glyph->bitmap_top;
                outImage->metrics.advanceX = glyph->advance.x >> 6;
                outImage->metrics.advanceY = glyph->advance.y >> 6;
                outImage->x = 0;
                outImage->y = 0;
            }
            return SCE_FONT_OK;
        }
    }
#endif

    // Fallback 8x16 bitmap rasterization
    std::uint8_t ch = (charCode < 128) ? static_cast<std::uint8_t>(charCode) : 63; // '?'
    const auto* src_glyph = fallback_glyph_data[ch];
    auto* dst = static_cast<std::uint8_t*>(surface->buffer);

    std::uint32_t draw_w = (std::min)(8u, surface->width);
    std::uint32_t draw_h = (std::min)(16u, surface->height);

    for (std::uint32_t y = 0; y < draw_h; ++y) {
        std::uint8_t bits = src_glyph[y];
        auto* dst_row = dst + y * surface->pitch;
        for (std::uint32_t x = 0; x < draw_w; ++x) {
            if ((bits >> (7 - x)) & 1) {
                dst_row[x] = 0xFF; // Full opacity white pixel
            }
        }
    }

    if (outImage && guest_memory::accessible(outImage, sizeof(*outImage))) {
        outImage->surface = surface;
        outImage->metrics.width = 8;
        outImage->metrics.height = 16;
        outImage->metrics.bearingX = 0;
        outImage->metrics.bearingY = 16;
        outImage->metrics.advanceX = 9;
        outImage->metrics.advanceY = 0;
        outImage->x = 0;
        outImage->y = 0;
    }

    return SCE_FONT_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceFontGetKerning(void* font, std::uint32_t charCode1, std::uint32_t charCode2, SceFontKerning* outKerning) {
    if (!outKerning || !guest_memory::accessible(outKerning, sizeof(*outKerning))) {
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }
    (void)font; (void)charCode1; (void)charCode2;
    outKerning->x = 0;
    outKerning->y = 0;
    return SCE_FONT_OK;
}

} // extern "C"

} // namespace gravity::hle::font
