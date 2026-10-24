#include "hle/system_service.h"
#include "hle/registry/adapter_registry.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle::system_service;
using Fn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t, std::int32_t*);
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    auto& registry = gravity::hle::AdapterRegistry::instance();
    const auto fn = reinterpret_cast<Fn>(registry.resolve("fZo48un7LK4", "libSceSystemService"));
    CHECK(fn == &sceSystemServiceParamGetInt);
    CHECK(registry.resolve("fZo48un7LK4", "libSceVideoOut") == 0);
    const auto* symbol = registry.find("fZo48un7LK4");
    CHECK(symbol && symbol->name == "sceSystemServiceParamGetInt" && !symbol->is_variable);
    if (suite == "parameters") {
        // The observed boot requests ID 1; language 20 is Spanish (Latin America).
        CHECK(configure(Settings{20}));
        struct Output { std::int32_t before, value, after; } output{123, -999, 456};
        CHECK(fn(1, &output.value) == 0);
        CHECK(output.value == 20 && output.before == 123 && output.after == 456);
        for (std::int32_t language = 0; language <= 29; ++language) {
            CHECK(configure(Settings{language}));
            CHECK(fn(1, &output.value) == 0 && output.value == language);
        }
        CHECK(configure(Settings{20}));
        for (auto bad : {-1, 30, std::numeric_limits<std::int32_t>::min(),
                         std::numeric_limits<std::int32_t>::max()}) {
            CHECK(!configure(Settings{bad}));
            CHECK(fn(1, &output.value) == 0 && output.value == 20);
        }
    } else if (suite == "invalid") {
        CHECK(parameter_error == static_cast<std::int32_t>(0x80A10003U));
        std::int32_t value = 0x12345678;
        for (auto id : {-1, 0, 2, 3, 4, 5, 6, 7, 999, 1000,
                        std::numeric_limits<std::int32_t>::max()}) {
            CHECK(fn(id, &value) == parameter_error);
            CHECK(value == 0x12345678);
        }
        CHECK(fn(1, nullptr) == parameter_error);
        CHECK(fn(1, reinterpret_cast<std::int32_t*>(1)) == parameter_error);
        CHECK(fn(1, reinterpret_cast<std::int32_t*>(UINTPTR_MAX - 1)) == parameter_error);
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const auto size = info.dwPageSize;
        auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, 2 * size,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        CHECK(pages);
        std::memset(pages, 0x5a, 2 * size);
        DWORD old{};
        CHECK(VirtualProtect(pages, size, PAGE_READONLY, &old));
        CHECK(fn(1, reinterpret_cast<std::int32_t*>(pages)) == parameter_error);
        CHECK(pages[0] == 0x5a && pages[3] == 0x5a);
        // First page is writable: this specifically tests validating all four bytes.
        CHECK(VirtualProtect(pages, size, PAGE_READWRITE, &old));
        CHECK(VirtualProtect(pages + size, size, PAGE_NOACCESS, &old));
        CHECK(fn(1, reinterpret_cast<std::int32_t*>(pages + size - 2)) == parameter_error);
        CHECK(pages[size - 2] == 0x5a && pages[size - 1] == 0x5a);
        CHECK(VirtualProtect(pages + size, size, PAGE_READWRITE, &old));
        CHECK(pages[size] == 0x5a && pages[size + 1] == 0x5a);
        CHECK(VirtualProtect(pages, size, PAGE_READWRITE | PAGE_GUARD, &old));
        CHECK(fn(1, reinterpret_cast<std::int32_t*>(pages)) == parameter_error);
        MEMORY_BASIC_INFORMATION region{};
        CHECK(VirtualQuery(pages, &region, sizeof(region)));
        CHECK(region.Protect & PAGE_GUARD); // Validation must not consume the guard.
        CHECK(VirtualFree(pages, size, MEM_DECOMMIT));
        CHECK(fn(1, reinterpret_cast<std::int32_t*>(pages)) == parameter_error);
        CHECK(VirtualFree(pages, 0, MEM_RELEASE));
    } else if (suite == "host") {
        const auto profile = host_settings();
        CHECK(configure(profile));
        CHECK(profile.language >= 0 && profile.language <= 29);
        std::int32_t value = -1;
        CHECK(fn(1, &value) == 0 && value == profile.language);
    } else return 2;
    std::puts("PASS system service");
}
