#pragma once
#include "hle/abi.h"
#include <cstdint>

namespace gravity::hle::system_service {
inline constexpr std::int32_t parameter_error = static_cast<std::int32_t>(0x80A10003U);
inline constexpr std::int32_t language_parameter = 1;
struct Settings {
    std::int32_t language{1}; // English (US); used if the host locale has no mapping.
};
Settings host_settings() noexcept;
// OpenOrbis language values 0..29. Invalid profiles leave the current one intact.
bool configure(const Settings& settings) noexcept;
// This runtime does not create a shell splash window. Hiding it is idempotent.
extern "C" GRAVITY_SYSV_ABI std::int32_t sceSystemServiceHideSplashScreen();
extern "C" GRAVITY_SYSV_ABI std::int32_t sceSystemServiceDisableMusicPlayer();
// Only language_parameter is supported. Other IDs and invalid destinations return
// parameter_error without intentionally writing the destination.
extern "C" GRAVITY_SYSV_ABI std::int32_t sceSystemServiceParamGetInt(
    std::int32_t parameter, std::int32_t* value);
}
