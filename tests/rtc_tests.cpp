#include "hle/rtc/rtc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)

using namespace gravity::hle::rtc;

static void test_ticks() {
    SceRtcTick tick1{}, tick2{};
    CHECK(sceRtcGetCurrentTick(&tick1) == 0);
    CHECK(tick1.tick > 62135596800000000ULL); // Must be past Jan 1, 1970
    CHECK(sceRtcGetCurrentNetworkTick(&tick2) == 0);
    CHECK(tick2.tick >= tick1.tick);
    CHECK(sceRtcGetCurrentTick(nullptr) != 0);
    std::printf("[PASS] rtc_ticks\n");
}

static void test_parse_rfc3339() {
    SceRtcDateTime dt{};
    const char iso[] = "2026-10-24T08:30:00.123456Z";
    CHECK(sceRtcParseRFC3339(&dt, iso) == 0);
    CHECK(dt.year == 2026);
    CHECK(dt.month == 10);
    CHECK(dt.day == 24);
    CHECK(dt.hour == 8);
    CHECK(dt.minute == 30);
    CHECK(dt.second == 0);

    // Invalid format
    CHECK(sceRtcParseRFC3339(&dt, "invalid-date") != 0);
    CHECK(sceRtcParseRFC3339(nullptr, iso) != 0);
    std::printf("[PASS] rtc_parse_rfc3339\n");
}

static void test_parse_datetime() {
    SceRtcDateTime dt{};
    const char standard[] = "2026/10/24 12:45:30";
    CHECK(sceRtcParseDateTime(&dt, standard) == 0);
    CHECK(dt.year == 2026);
    CHECK(dt.month == 10);
    CHECK(dt.day == 24);
    CHECK(dt.hour == 12);
    CHECK(dt.minute == 45);
    CHECK(dt.second == 30);
    std::printf("[PASS] rtc_parse_datetime\n");
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        const std::string_view suite(argv[1]);
        if (suite == "ticks") { test_ticks(); return 0; }
        if (suite == "rfc3339") { test_parse_rfc3339(); return 0; }
        if (suite == "datetime") { test_parse_datetime(); return 0; }
    }
    test_ticks();
    test_parse_rfc3339();
    test_parse_datetime();
    return 0;
}
