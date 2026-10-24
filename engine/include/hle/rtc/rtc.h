#pragma once

#include "hle/abi.h"
#include <cstdint>

namespace gravity::hle::rtc {

struct SceRtcTick {
    std::uint64_t tick;
};

struct SceRtcDateTime {
    std::uint16_t year;
    std::uint16_t month;
    std::uint16_t day;
    std::uint16_t hour;
    std::uint16_t minute;
    std::uint16_t second;
    std::uint32_t microsecond;
};

extern "C" {

GRAVITY_SYSV_ABI std::int32_t sceRtcGetCurrentTick(SceRtcTick* tick);
GRAVITY_SYSV_ABI std::int32_t sceRtcGetCurrentNetworkTick(SceRtcTick* tick);
GRAVITY_SYSV_ABI std::int32_t sceRtcParseRFC3339(SceRtcDateTime* time, const char* str);
GRAVITY_SYSV_ABI std::int32_t sceRtcParseDateTime(SceRtcDateTime* time, const char* str);

} // extern "C"

} // namespace gravity::hle::rtc
