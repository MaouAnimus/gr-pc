#include "hle/playgo.h"
#include "hle/registry/adapter_registry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)

using namespace gravity::hle::playgo;
using InitFn = std::int32_t(GRAVITY_SYSV_ABI*)(const ScePlayGoInitParams*);
using TermFn = std::int32_t(GRAVITY_SYSV_ABI*)();
using OpenFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t*, const void*);
using CloseFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t);
using ToDoListFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t, void*, std::uint32_t, std::uint32_t*);
using SpeedFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t, std::uint32_t*);
using SetSpeedFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t, std::uint32_t);
using LocusFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t, std::uint32_t, std::uint32_t*);
using EtaFn = std::int32_t(GRAVITY_SYSV_ABI*)(std::int32_t, std::uint32_t*);

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];

    auto& registry = gravity::hle::AdapterRegistry::instance();
    const auto fn_init = reinterpret_cast<InitFn>(registry.resolve("ts6GlZOKRrE", "libScePlayGo"));
    const auto fn_open = reinterpret_cast<OpenFn>(registry.resolve("M1Gma1ocrGE", "libScePlayGo"));
    const auto fn_todolist = reinterpret_cast<ToDoListFn>(registry.resolve("Nn7zKwnA5q0", "libScePlayGo"));
    const auto fn_speed = reinterpret_cast<SpeedFn>(registry.resolve("rvBSfTimejE", "libScePlayGo"));
    const auto fn_setspeed = reinterpret_cast<SetSpeedFn>(registry.resolve("4AAcTU9R3XM", "libScePlayGo"));
    const auto fn_locus = reinterpret_cast<LocusFn>(registry.resolve("uWIYLFkkwqk", "libScePlayGo"));
    const auto fn_eta = reinterpret_cast<EtaFn>(registry.resolve("v6EZ-YWRdMs", "libScePlayGo"));

    CHECK(fn_init == &scePlayGoInitialize);
    CHECK(fn_open == &scePlayGoOpen);
    CHECK(fn_todolist == &scePlayGoGetToDoList);
    CHECK(fn_speed == &scePlayGoGetInstallSpeed);
    CHECK(fn_setspeed == &scePlayGoSetInstallSpeed);
    CHECK(fn_locus == &scePlayGoGetLocus);
    CHECK(fn_eta == &scePlayGoGetEta);

    if (suite == "registry") {
        CHECK(registry.resolve("ts6GlZOKRrE", "libkernel") == 0);
        const auto* sym = registry.find("ts6GlZOKRrE");
        CHECK(sym && sym->name == "scePlayGoInitialize" && sym->library == "libScePlayGo");

        const auto* sym_open = registry.find("M1Gma1ocrGE");
        CHECK(sym_open && sym_open->name == "scePlayGoOpen" && sym_open->library == "libScePlayGo");
    } else if (suite == "lifecycle") {
        reset();
        CHECK(!is_initialized());

        // Null params
        CHECK(fn_init(nullptr) == SCE_PLAYGO_ERROR_INVALID_ARGUMENT);
        CHECK(!is_initialized());

        // Valid init
        char buf[1024]{};
        ScePlayGoInitParams params{buf, sizeof(buf), 0};
        CHECK(fn_init(&params) == SCE_PLAYGO_OK);
        CHECK(is_initialized());

        // Open handle
        CHECK(fn_open(nullptr, nullptr) == SCE_PLAYGO_ERROR_INVALID_ARGUMENT);
        std::int32_t handle = 0;
        CHECK(fn_open(&handle, nullptr) == SCE_PLAYGO_OK);
        CHECK(handle > 0);

        // To-Do list: count must be 0 for a local installed title
        CHECK(fn_todolist(handle, nullptr, 32, nullptr) == SCE_PLAYGO_ERROR_INVALID_ARGUMENT);
        std::uint32_t count = 999;
        CHECK(fn_todolist(handle, nullptr, 32, &count) == SCE_PLAYGO_OK);
        CHECK(count == 0);

        // Speed
        CHECK(fn_speed(handle, nullptr) == SCE_PLAYGO_ERROR_INVALID_ARGUMENT);
        std::uint32_t speed = 0;
        CHECK(fn_speed(handle, &speed) == SCE_PLAYGO_OK);
        CHECK(speed > 0);

        CHECK(fn_setspeed(handle, 50000000) == SCE_PLAYGO_OK);

        // Close
        CHECK(scePlayGoClose(handle) == SCE_PLAYGO_OK);

        // Terminate
        CHECK(scePlayGoTerminate() == SCE_PLAYGO_OK);
        CHECK(!is_initialized());
    } else if (suite == "locus") {
        reset();
        std::int32_t handle = 0;
        CHECK(fn_open(&handle, nullptr) == SCE_PLAYGO_OK);

        CHECK(fn_locus(handle, 0, nullptr) == SCE_PLAYGO_ERROR_INVALID_ARGUMENT);
        std::uint32_t locus = 0;
        CHECK(fn_locus(handle, 0, &locus) == SCE_PLAYGO_OK);
        CHECK(locus == SCE_PLAYGO_LOCUS_LOCAL);

        std::uint32_t eta = 999;
        CHECK(fn_eta(handle, &eta) == SCE_PLAYGO_OK);
        CHECK(eta == 0);

        CHECK(scePlayGoClose(handle) == SCE_PLAYGO_OK);
    } else {
        std::fprintf(stderr, "Unknown test suite: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
