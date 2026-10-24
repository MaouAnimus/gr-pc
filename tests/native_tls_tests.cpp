#include "hle/native_tls.h"
#include "hle/abi.h"
#include "hle/runner/runner.h"
#include "hle/loader/tls_fixups.h"
#include <array>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle;
using Reader = std::uintptr_t(GRAVITY_SYSV_ABI*)();
extern "C" GRAVITY_SYSV_ABI std::uint64_t guest_tls_access(std::uint64_t marker, std::uintptr_t reader_address) {
    const auto reader = reinterpret_cast<Reader>(reader_address);
    const auto tp = reader();
    auto* data = reinterpret_cast<std::uint64_t*>(tp - 32);
    if (data[0] != 0x12345678 || data[1] != 0 || data[2] != 0 || data[3] != 0) return 0;
    data[3] = marker;
    // A Windows call and context switches must preserve the guest FS base.
    Sleep(10);
    const auto after = reader();
    return after == tp ? reinterpret_cast<std::uint64_t*>(after - 8)[0] : 0;
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!NativeTls::supported()) return 77;
    TlsImage image{{0x78, 0x56, 0x34, 0x12}, 32, 16};
    const std::string_view suite = argv[1];
    std::array<std::uint8_t, 10> code{0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0, 0xc3};
    CHECK(NativeTls::patch_fs_load(std::span(code).first(9)));
    auto* executable = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECK(executable);
    std::memcpy(executable, code.data(), code.size());
    DWORD old{};
    CHECK(VirtualProtect(executable, 4096, PAGE_EXECUTE_READ, &old));
    CHECK(FlushInstructionCache(GetCurrentProcess(), executable, code.size()));
    const auto reader_address = reinterpret_cast<std::uintptr_t>(executable);
    const auto reader = reinterpret_cast<Reader>(reader_address);
    if (suite == "layout") {
        NativeTls tls;
        CHECK(tls.initialize(image));
        const auto before = reader();
        CHECK(tls.attach());
        CHECK(reader() == reinterpret_cast<std::uintptr_t>(tls.thread_pointer()));
        CHECK(guest_tls_access(42, reader_address) == 42);
        CHECK(tls.detach());
        CHECK(reader() == before);
    } else if (suite == "concurrent") {
        const auto run = [&](std::uint64_t marker) {
            runner::RunnerConfig config;
            config.tls = image;
            auto report = runner::execute_isolated_sysv(reinterpret_cast<std::uintptr_t>(&guest_tls_access), marker, reader_address, 0, 0, 0, 0, config);
            CHECK(report.succeeded && report.return_value == marker);
        };
        std::thread a([&] { run(17); }), b([&] { run(29); });
        a.join(); b.join();
    } else if (suite == "invalid") {
        NativeTls tls;
        CHECK(!tls.attach());
        image.alignment = 3; CHECK(!tls.initialize(image));
        image.alignment = 16; image.memory_size = 1; CHECK(!tls.initialize(image));
        image.memory_size = 32; CHECK(tls.initialize(image)); CHECK(!tls.initialize(image));
        CHECK(tls.attach()); CHECK(!tls.attach()); CHECK(tls.detach()); CHECK(!tls.detach());
        std::array<std::uint8_t, 9> invalid{0x64, 0x48, 0x8b, 0x04, 0x25, 1, 0, 0, 0};
        const auto original = invalid;
        CHECK(!NativeTls::patch_fs_load(invalid) && invalid == original);
        std::uint32_t count = 123;
        CHECK(!loader::apply_gravity_tls_fixups(code, invalid, count) && count == 0 && invalid == original);
    } else return 2;
    CHECK(VirtualFree(executable, 0, MEM_RELEASE));
    std::puts("PASS native TLS fixups");
}
