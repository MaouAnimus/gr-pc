#include "hle/rtc/rtc.h"
#include "hle/guest_memory.h"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace gravity::hle::rtc {

namespace {
// Orbis RTC epoch starts at 0001-01-01 00:00:00 UTC.
// Delta between 0001-01-01 and UNIX epoch 1970-01-01 in microseconds:
// 62,135,596,800 seconds * 1,000,000 us/s = 62,135,596,800,000,000 us.
constexpr std::uint64_t kOrbisEpochDeltaUs = 62135596800000000ULL;

constexpr std::int32_t SCE_OK = 0;
constexpr std::int32_t SCE_RTC_ERROR_INVALID_POINTER = static_cast<std::int32_t>(0x80800001U);
constexpr std::int32_t SCE_RTC_ERROR_INVALID_VALUE = static_cast<std::int32_t>(0x80800002U);
}

extern "C" {

GRAVITY_SYSV_ABI std::int32_t sceRtcGetCurrentTick(SceRtcTick* tick) {
    if (!tick || !guest_memory::accessible(tick, sizeof(SceRtcTick), true)) {
        return SCE_RTC_ERROR_INVALID_POINTER;
    }
    const auto now = std::chrono::system_clock::now();
    const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    const std::uint64_t orbis_tick = static_cast<std::uint64_t>(now_us) + kOrbisEpochDeltaUs;
    tick->tick = orbis_tick;
    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceRtcGetCurrentNetworkTick(SceRtcTick* tick) {
    return sceRtcGetCurrentTick(tick);
}

GRAVITY_SYSV_ABI std::int32_t sceRtcParseRFC3339(SceRtcDateTime* time, const char* str) {
    if (!time || !str) return SCE_RTC_ERROR_INVALID_POINTER;
    if (!guest_memory::accessible(time, sizeof(SceRtcDateTime), true)) return SCE_RTC_ERROR_INVALID_POINTER;

    char safe_str[128]{};
    if (!guest_memory::string(str, safe_str, sizeof(safe_str))) {
        return SCE_RTC_ERROR_INVALID_POINTER;
    }

    int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0, us = 0;
    // Format: YYYY-MM-DDTHH:MM:SS[.fraction]Z or +/-HH:MM
    int matched = std::sscanf(safe_str, "%d-%d-%dT%d:%d:%d.%d", &y, &m, &d, &h, &min, &s, &us);
    if (matched < 6) {
        matched = std::sscanf(safe_str, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &s);
        if (matched < 6) return SCE_RTC_ERROR_INVALID_VALUE;
        us = 0;
    }

    time->year = static_cast<std::uint16_t>(y);
    time->month = static_cast<std::uint16_t>(m);
    time->day = static_cast<std::uint16_t>(d);
    time->hour = static_cast<std::uint16_t>(h);
    time->minute = static_cast<std::uint16_t>(min);
    time->second = static_cast<std::uint16_t>(s);
    time->microsecond = static_cast<std::uint32_t>(us);

    return SCE_OK;
}

GRAVITY_SYSV_ABI std::int32_t sceRtcParseDateTime(SceRtcDateTime* time, const char* str) {
    if (!time || !str) return SCE_RTC_ERROR_INVALID_POINTER;
    if (!guest_memory::accessible(time, sizeof(SceRtcDateTime), true)) return SCE_RTC_ERROR_INVALID_POINTER;

    char safe_str[128]{};
    if (!guest_memory::string(str, safe_str, sizeof(safe_str))) {
        return SCE_RTC_ERROR_INVALID_POINTER;
    }

    int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0;
    int matched = std::sscanf(safe_str, "%d/%d/%d %d:%d:%d", &y, &m, &d, &h, &min, &s);
    if (matched < 6) {
        matched = std::sscanf(safe_str, "%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &s);
        if (matched < 6) return SCE_RTC_ERROR_INVALID_VALUE;
    }

    time->year = static_cast<std::uint16_t>(y);
    time->month = static_cast<std::uint16_t>(m);
    time->day = static_cast<std::uint16_t>(d);
    time->hour = static_cast<std::uint16_t>(h);
    time->minute = static_cast<std::uint16_t>(min);
    time->second = static_cast<std::uint16_t>(s);
    time->microsecond = 0;

    return SCE_OK;
}

} // extern "C"

} // namespace gravity::hle::rtc
