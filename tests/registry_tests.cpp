#include "hle/registry/adapter_registry.h"
#include "hle/runner/runner.h"
#include "hle/fiber.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>
#include <ctime>

namespace {

using namespace gravity;

[[noreturn]] void fail(const char* expression, int line) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

void test_registry_lookup() {
    auto& reg = hle::AdapterRegistry::instance();

    // 1. libc lookups
    CHECK(reg.resolve("gQX+4GDQjpM") != 0); // malloc
    CHECK(reg.resolve("rcQCUr0EaRU") != 0); // free
    CHECK(reg.resolve("2X5agFjKxMc") != 0); // calloc
    CHECK(reg.resolve("LTov9gMEqCU") != 0); // strlen
    CHECK(reg.resolve("hqi8yMOCmG0") != 0); // memcpy
    CHECK(reg.resolve("uMei1W9uyNo") != 0); // exit

    // 2. libkernel lookups
    CHECK(reg.resolve("f7uOxY9mM1U") != 0); // __stack_chk_guard
    const auto* guard_sym = reg.find("f7uOxY9mM1U");
    CHECK(guard_sym != nullptr);
    CHECK(guard_sym->is_variable);
    CHECK(*reinterpret_cast<const uint64_t*>(guard_sym->func_ptr) == 0x595A4546414C5641ULL);

    CHECK(reg.resolve("Ou3iL1abvng") != 0); // __stack_chk_fail
    CHECK(reg.resolve("4J2sUJmuHZQ") != 0); // sceKernelGetProcessTime
    CHECK(reg.resolve("ejekcaNQNq0") != 0); // sceKernelGettimeofday
    CHECK(reg.resolve("-2IRUCO--PM") != 0); // sceKernelReadTsc
    CHECK(reg.resolve("1j3S3n-tTW4") != 0); // sceKernelGetTscFrequency
    CHECK(reg.resolve("1jfXLRVzisc") != 0); // sceKernelUsleep

    // 3. libSceFiber lookups
    CHECK(reg.resolve("hVYD7Ou2pCQ") != 0); // sceFiberInitialize
    CHECK(reg.resolve("a0LLrZWac0M") != 0); // sceFiberRun
    CHECK(reg.resolve("PFT2S-tJ7Uk") != 0); // sceFiberSwitch
    CHECK(reg.resolve("B0ZX2hx9DMw") != 0); // sceFiberReturnToThread
    CHECK(reg.resolve("JeNX5F-NzQU") != 0); // sceFiberFinalize

    // 4. libSceVideoOut lookups
    CHECK(reg.resolve("Up36PTk687E") != 0); // sceVideoOutOpen
    CHECK(reg.resolve("i6-sR91Wt-4") != 0); // sceVideoOutSetBufferAttribute
    CHECK(reg.resolve("w3BY+tAEiQY") != 0); // sceVideoOutRegisterBuffers
    CHECK(reg.resolve("CBiu4mCE1DA") != 0); // sceVideoOutSetFlipRate
    CHECK(reg.resolve("HXzjK9yI30k") != 0); // sceVideoOutAddFlipEvent
    CHECK(reg.resolve("U46NwOiJpys") != 0); // sceVideoOutSubmitFlip

    // 5. libScePad lookups
    CHECK(reg.resolve("hv1luiJrqQM") != 0); // scePadInit
    CHECK(reg.resolve("xk0AcarP3V4") != 0); // scePadOpen
    CHECK(reg.resolve("q1cHNfGycLI") != 0); // scePadRead

    // 6. libSceAudioOut lookups
    CHECK(reg.resolve("JfEPXVxhFqA") != 0); // sceAudioOutInit
    CHECK(reg.resolve("ekNvsT22rsY") != 0); // sceAudioOutOpen
    CHECK(reg.resolve("s1--uE9mBFw") != 0); // sceAudioOutClose

    // 7. C++ ABI & Threading lookups
    CHECK(reg.resolve("tsvEmnenz48") != 0); // __cxa_atexit
    CHECK(reg.resolve("1tKyG7RlMJo") != 0); // scePthreadGetprio
    CHECK(reg.resolve("6UgtwV+0zb4") != 0); // scePthreadCreate
    CHECK(reg.resolve("g8cM39EUZ6o") != 0); // sceSysmoduleLoadModule
    CHECK(reg.resolve("fZo48un7LK4", "libSceSystemService") != 0);
    CHECK(reg.resolve("fZo48un7LK4", "libSceVideoOut") == 0);
    reg.register_adapter("same-test-nid", "testA", "a", 123);
    reg.register_adapter("same-test-nid", "testB", "b", 456);
    CHECK(reg.resolve("same-test-nid", "testA") == 123);
    CHECK(reg.resolve("same-test-nid", "testB") == 456);
    CHECK(reg.resolve("same-test-nid") == 0);

    std::puts("PASS: test_registry_lookup");
}

void test_registry_execution_via_sysv() {
    auto& reg = hle::AdapterRegistry::instance();

    // 1. Execute malloc and free via SysV function pointers
    using MallocFn = void*(GRAVITY_SYSV_ABI*)(std::size_t);
    using FreeFn = void(GRAVITY_SYSV_ABI*)(void*);

    auto my_malloc = reinterpret_cast<MallocFn>(reg.resolve("gQX+4GDQjpM"));
    auto my_free = reinterpret_cast<FreeFn>(reg.resolve("rcQCUr0EaRU"));

    void* ptr = my_malloc(128);
    CHECK(ptr != nullptr);
    std::memset(ptr, 0xAB, 128);
    CHECK(static_cast<uint8_t*>(ptr)[0] == 0xAB);
    CHECK(static_cast<uint8_t*>(ptr)[127] == 0xAB);
    my_free(ptr);

    // 2. Execute strlen via SysV function pointer
    using StrlenFn = std::size_t(GRAVITY_SYSV_ABI*)(const char*);
    auto my_strlen = reinterpret_cast<StrlenFn>(reg.resolve("LTov9gMEqCU"));
    CHECK(my_strlen("GravityRushPC") == 13);
    using OverlayFn = int(GRAVITY_SYSV_ABI*)(const char*, int32_t, int32_t);
    auto overlay = reinterpret_cast<OverlayFn>(reg.resolve("ahHhOf+QNkQ"));
    CHECK(overlay && overlay("/app0/overlay.png", 0, 0) == static_cast<int>(0x8002004eU));
    using TimeSecondsFn = int64_t(GRAVITY_SYSV_ABI*)(int64_t*);
    const auto seconds = reinterpret_cast<TimeSecondsFn>(reg.resolve("wLlFkwG9UcQ"));
    const auto before = std::time(nullptr);
    int64_t output{};
    CHECK(seconds && seconds(&output) == output);
    CHECK(output >= before && output <= std::time(nullptr));
    CHECK(seconds(reinterpret_cast<int64_t*>(1)) == -1);
    using ModuleFn = int(GRAVITY_SYSV_ABI*)(uint32_t);
    const auto module_load = reinterpret_cast<ModuleFn>(reg.resolve("g8cM39EUZ6o"));
    CHECK(module_load(0) == static_cast<int>(0x805a1000U));
    CHECK(module_load(0x80) == static_cast<int>(0x8002004eU));
    using AttributeFn = void(GRAVITY_SYSV_ABI*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const auto set_attribute = reinterpret_cast<AttributeFn>(reg.resolve("i6-sR91Wt-4"));
    struct { uint32_t data[10]; uint64_t canary; } attribute{};
    attribute.canary = 0x12345678;
    set_attribute(attribute.data, 0x80002200, 1, 0, 1920, 1080, 2048);
    CHECK(attribute.data[0] == 0x80002200 && attribute.data[3] == 1920 && attribute.data[5] == 2048);
    CHECK(attribute.data[6] == 0 && attribute.data[9] == 0 && attribute.canary == 0x12345678);

    // 3. Execute process time via SysV
    using TimeFn = uint64_t(GRAVITY_SYSV_ABI*)();
    auto my_time = reinterpret_cast<TimeFn>(reg.resolve("4J2sUJmuHZQ"));
    uint64_t t1 = my_time();
    Sleep(10);
    uint64_t t2 = my_time();
    CHECK(t2 >= t1);

    std::puts("PASS: test_registry_execution_via_sysv");
}

void test_registry_unresolved_trap() {
    auto& reg = hle::AdapterRegistry::instance();

    // Create an unresolved thunk
    uintptr_t thunk = reg.get_unresolved_thunk("test_fake_nid_1234", "libSceFakeModule");
    CHECK(thunk != 0);

    // Hook to capture trap
    hle::UnresolvedCallInfo captured{};
    reg.set_unresolved_hook([&](const hle::UnresolvedCallInfo& info) {
        captured = info;
    });

    // Execute thunk under isolated runner
    hle::runner::RunnerConfig opt{};
    opt.timeout_ms = 1000;
    auto report = hle::runner::execute_isolated_sysv(
        thunk,
        1, 2, 3, 4, 5, 6,
        opt
    );

    // Should trap with STATUS_ILLEGAL_INSTRUCTION (ud2)
    CHECK(!report.succeeded);
    CHECK(report.crash_reason == hle::runner::CrashReason::illegal_instruction);
    CHECK(report.native_exception_code == STATUS_ILLEGAL_INSTRUCTION);

    // Hook must have received the exact NID and Library
    CHECK(captured.nid == "test_fake_nid_1234");
    CHECK(captured.library == "libSceFakeModule");
    CHECK(captured.caller_rip != 0);
    for (std::size_t i = 0; i < 6; ++i) CHECK(captured.arguments[i] == i + 1);

    std::puts("PASS: test_registry_unresolved_trap");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string_view suite = argv[1];
        if (suite == "lookup") {
            test_registry_lookup();
            return 0;
        } else if (suite == "execution") {
            test_registry_execution_via_sysv();
            return 0;
        } else if (suite == "trap") {
            test_registry_unresolved_trap();
            return 0;
        }
        std::fprintf(stderr, "Unknown test suite: %s\n", argv[1]);
        return 1;
    }

    test_registry_lookup();
    test_registry_execution_via_sysv();
    test_registry_unresolved_trap();
    std::puts("All registry tests passed successfully!");
    return 0;
}
