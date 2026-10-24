#include "hle/system_service.h"
#include "hle/guest_memory.h"
#include <windows.h>
#include <cstdio>
#include <mutex>

namespace gravity::hle::system_service {
extern "C" GRAVITY_SYSV_ABI std::int32_t sceSystemServiceDisableMusicPlayer() {
    std::fprintf(stderr, "[SystemService] Shell music player unavailable (ENOSYS)\n");
    return static_cast<std::int32_t>(0x8002004eU);
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceSystemServiceHideSplashScreen() {
    std::fprintf(stderr, "[SystemService] Splash hidden (native runtime has no shell splash)\n");
    return 0;
}
namespace {
std::mutex settings_mutex;
Settings settings = host_settings();
}

Settings host_settings() noexcept {
    Settings result;
    const auto lang = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(lang)) {
    case LANG_JAPANESE: result.language = 0; break;
    case LANG_ENGLISH: result.language = SUBLANGID(lang) == SUBLANG_ENGLISH_UK ? 18 : 1; break;
    case LANG_FRENCH: result.language = SUBLANGID(lang) == SUBLANG_FRENCH_CANADIAN ? 22 : 2; break;
    case LANG_SPANISH: result.language = (SUBLANGID(lang) == SUBLANG_SPANISH ||
        SUBLANGID(lang) == SUBLANG_SPANISH_MODERN) ? 3 : 20; break;
    case LANG_GERMAN: result.language = 4; break;
    case LANG_ITALIAN: result.language = 5; break;
    case LANG_DUTCH: result.language = 6; break;
    case LANG_PORTUGUESE: result.language = SUBLANGID(lang) == SUBLANG_PORTUGUESE_BRAZILIAN ? 17 : 7; break;
    case LANG_RUSSIAN: result.language = 8; break;
    case LANG_KOREAN: result.language = 9; break;
    case LANG_CHINESE: result.language = (SUBLANGID(lang) == SUBLANG_CHINESE_TRADITIONAL ||
        SUBLANGID(lang) == SUBLANG_CHINESE_HONGKONG) ? 10 : 11; break;
    case LANG_FINNISH: result.language = 12; break;
    case LANG_SWEDISH: result.language = 13; break;
    case LANG_DANISH: result.language = 14; break;
    case LANG_NORWEGIAN: result.language = 15; break;
    case LANG_POLISH: result.language = 16; break;
    case LANG_TURKISH: result.language = 19; break;
    case LANG_ARABIC: result.language = 21; break;
    case LANG_CZECH: result.language = 23; break;
    case LANG_HUNGARIAN: result.language = 24; break;
    case LANG_GREEK: result.language = 25; break;
    case LANG_ROMANIAN: result.language = 26; break;
    case LANG_THAI: result.language = 27; break;
    case LANG_VIETNAMESE: result.language = 28; break;
    case LANG_INDONESIAN: result.language = 29; break;
    default: break;
    }
    return result;
}

bool configure(const Settings& candidate) noexcept {
    if (candidate.language < 0 || candidate.language > 29) return false;
    std::lock_guard lock(settings_mutex);
    settings = candidate;
    return true;
}

extern "C" GRAVITY_SYSV_ABI std::int32_t sceSystemServiceParamGetInt(
    std::int32_t parameter, std::int32_t* value) {
    if (parameter != language_parameter) {
        std::fprintf(stderr, "[SystemService] ParamGetInt id=%d rejected (0x80A10003)\n", parameter);
        return parameter_error; // Fail closed for IDs outside this adapter's observed scope.
    }
    std::int32_t answer;
    { std::lock_guard lock(settings_mutex); answer = settings.language; }
    if (!guest_memory::write(value, &answer, sizeof(answer))) return parameter_error;
    std::fprintf(stderr, "[SystemService] ParamGetInt id=%d value=%d\n", parameter, answer);
    return 0;
}
}
