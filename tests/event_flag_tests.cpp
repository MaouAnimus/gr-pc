#include "hle/kernel/event_flag.h"
#include "hle/registry/adapter_registry.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle::event_flag;
namespace {
template<class Predicate> void until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
void pending_single(Handle handle) {
    until([&] { return sceKernelPollEventFlag(handle, 0x8000, wait_all, nullptr) == permission; });
}
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    struct { Handle value{}; std::uint64_t canary{0x123456789abcdef0ULL}; } output;
    CHECK(sceKernelCreateEventFlag(&output.value, "test", fifo | single, 0, nullptr) == 0);
    CHECK(output.value && output.canary == 0x123456789abcdef0ULL);
    const auto handle = output.value;
    if (suite == "lifecycle") {
        std::atomic<std::int32_t> result{};
        std::thread waiter([&] { result = sceKernelWaitEventFlag(handle, 1, wait_all, nullptr, nullptr); });
        pending_single(handle);
        CHECK(sceKernelDeleteEventFlag(handle) == 0);
        waiter.join(); CHECK(result == deleted);
        CHECK(sceKernelSetEventFlag(handle, 1) == missing);
        CHECK(sceKernelDeleteEventFlag(handle) == missing);
        CHECK(sceKernelCreateEventFlag(&output.value, "test", 0, 3, nullptr) == 0);
        CHECK(sceKernelCloseEventFlag(output.value) == 0);
        CHECK(sceKernelPollEventFlag(output.value, 1, wait_all, nullptr) == missing);
        return 0;
    } else if (suite == "bits") {
        std::uint64_t bits{};
        CHECK(sceKernelSetEventFlag(handle, 0x15) == 0);
        CHECK(sceKernelPollEventFlag(handle, 0x03, wait_all, &bits) == busy && bits == 0x15);
        CHECK(sceKernelPollEventFlag(handle, 0x03, wait_any | clear_pattern, &bits) == 0 && bits == 0x15);
        CHECK(sceKernelPollEventFlag(handle, 0x14, wait_all, &bits) == 0 && bits == 0x14);
        CHECK(sceKernelClearEventFlag(handle, 0x04) == 0);
        CHECK(sceKernelPollEventFlag(handle, 0x04, wait_all | clear_all, &bits) == 0 && bits == 0x04);
        CHECK(sceKernelPollEventFlag(handle, 0x04, wait_any, &bits) == busy && bits == 0);
        CHECK(sceKernelSetEventFlag(handle, 1ULL << 63) == 0);
        CHECK(sceKernelPollEventFlag(handle, 1ULL << 63, wait_all, &bits) == 0 && bits == (1ULL << 63));
    } else if (suite == "wait") {
        std::atomic<bool> completed{};
        std::uint64_t observed{};
        std::thread waiter([&] {
            CHECK(sceKernelWaitEventFlag(handle, 3, wait_all, &observed, nullptr) == 0);
            completed = true;
        });
        pending_single(handle);
        CHECK(sceKernelSetEventFlag(handle, 1) == 0);
        CHECK(!completed);
        CHECK(sceKernelSetEventFlag(handle, 2) == 0);
        waiter.join(); CHECK(completed && observed == 3);
        CHECK(sceKernelClearEventFlag(handle, 0) == 0);
        struct { std::uint32_t micros{2000}; std::uint32_t canary{0xaabbccdd}; } timeout;
        const auto start = std::chrono::steady_clock::now();
        CHECK(sceKernelWaitEventFlag(handle, 1, wait_all, &observed, &timeout.micros) == timed_out);
        CHECK(std::chrono::steady_clock::now() - start >= std::chrono::microseconds(2000));
        CHECK(timeout.micros == 0 && timeout.canary == 0xaabbccdd && observed == 0);
        CHECK(sceKernelSetEventFlag(handle, 1) == 0);
        timeout.micros = 1000000;
        CHECK(sceKernelWaitEventFlag(handle, 1, wait_all, nullptr, &timeout.micros) == 0);
        CHECK(timeout.micros > 0 && timeout.micros <= 1000000);
    } else if (suite == "cancel") {
        std::atomic<std::int32_t> result{};
        std::uint64_t observed{};
        std::uint32_t timeout = 3000000;
        std::thread waiter([&] { result = sceKernelWaitEventFlag(handle, 1, wait_all, &observed, &timeout); });
        pending_single(handle);
        std::int32_t count{};
        CHECK(sceKernelCancelEventFlag(handle, 4, &count) == 0 && count == 1);
        waiter.join(); CHECK(result == canceled && observed == 4 && timeout > 0);
        CHECK(sceKernelPollEventFlag(handle, 4, wait_all | clear_all, nullptr) == 0);
        CHECK(sceKernelSetEventFlag(handle, 1) == 0);
        CHECK(sceKernelPollEventFlag(handle, 1, wait_all, nullptr) == 0);
    } else if (suite == "concurrent") {
        Handle shared{};
        CHECK(sceKernelCreateEventFlag(&shared, "workers", priority | multi, 0, nullptr) == 0);
        std::atomic<int> completed{}, started{};
        std::vector<std::thread> workers;
        for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
            ++started;
            std::uint64_t observed{};
            std::uint32_t timeout = 3000000;
            CHECK(sceKernelWaitEventFlag(shared, 1, wait_all | clear_pattern, &observed, &timeout) == 0);
            CHECK(observed == 1);
            ++completed;
        });
        until([&] { return started == 8; });
        for (int i = 1; i <= 8; ++i) {
            CHECK(sceKernelSetEventFlag(shared, 1) == 0);
            until([&] { return completed == i; });
            CHECK(sceKernelPollEventFlag(shared, 1, wait_all, nullptr) == busy);
        }
        for (auto& worker : workers) worker.join();
        CHECK(sceKernelDeleteEventFlag(shared) == 0);
    } else if (suite == "registry") {
        auto& registry = gravity::hle::AdapterRegistry::instance();
        const auto create = reinterpret_cast<decltype(&sceKernelCreateEventFlag)>(registry.resolve("BpFoboUJoZU", "libkernel"));
        const auto wait = reinterpret_cast<decltype(&sceKernelWaitEventFlag)>(registry.resolve("JTvBflhYazQ", "libkernel"));
        const auto set = reinterpret_cast<decltype(&sceKernelSetEventFlag)>(registry.resolve("IOnSvHzqu6A", "libkernel"));
        const auto poll = reinterpret_cast<decltype(&sceKernelPollEventFlag)>(registry.resolve("9lvj5DjHZiA", "libkernel"));
        const auto destroy = reinterpret_cast<decltype(&sceKernelDeleteEventFlag)>(registry.resolve("8mql9OcQnd4", "libkernel"));
        CHECK(create && wait && set && poll && destroy);
        CHECK(registry.resolve("BpFoboUJoZU", "libSceVideoOut") == 0);
        struct { Handle value{UINT64_MAX}; std::uint64_t canary{0xaabbccdd11223344ULL}; } slot;
        CHECK(create(&slot.value, "guest abi", fifo | single, 0, nullptr) == 0);
        CHECK(slot.value && slot.value != UINT64_MAX && slot.canary == 0xaabbccdd11223344ULL);
        constexpr std::uint64_t pattern = (1ULL << 40) | 1;
        std::uint64_t observed{};
        struct { std::uint32_t micros{3000000}; std::uint32_t canary{0x11223344}; } timeout;
        std::thread guest_worker([&] {
            CHECK(wait(slot.value, pattern, wait_all | clear_pattern, &observed, &timeout.micros) == 0);
        });
        until([&] { return poll(slot.value, 0x8000, wait_all, nullptr) == permission; });
        CHECK(set(slot.value, pattern) == 0);
        guest_worker.join();
        CHECK(observed == pattern && timeout.micros > 0 && timeout.micros <= 3000000);
        CHECK(timeout.canary == 0x11223344);
        CHECK(poll(slot.value, pattern, wait_any, nullptr) == busy);
        CHECK(destroy(slot.value) == 0);
    } else if (suite == "invalid") {
        Handle ignored{};
        CHECK(sceKernelCreateEventFlag(nullptr, "x", 0, 0, nullptr) == invalid);
        CHECK(sceKernelCreateEventFlag(reinterpret_cast<Handle*>(1), "x", 0, 0, nullptr) == fault);
        CHECK(sceKernelCreateEventFlag(&ignored, reinterpret_cast<const char*>(1), 0, 0, nullptr) == fault);
        CHECK(sceKernelCreateEventFlag(&ignored, "x", 0x03, 0, nullptr) == invalid);
        CHECK(sceKernelCreateEventFlag(&ignored, "x", 0x30, 0, nullptr) == invalid);
        CHECK(sceKernelCreateEventFlag(&ignored, "x", 0, 0, &ignored) == invalid);
        CHECK(sceKernelCreateEventFlag(&ignored, "12345678901234567890123456789012", 0, 0, nullptr) == static_cast<std::int32_t>(0x8002003fU));
        CHECK(sceKernelWaitEventFlag(handle, 0, wait_all, nullptr, nullptr) == invalid);
        CHECK(sceKernelWaitEventFlag(handle, 1, 3, nullptr, nullptr) == invalid);
        CHECK(sceKernelWaitEventFlag(handle, 1, wait_all | clear_all | clear_pattern, nullptr, nullptr) == invalid);
        CHECK(sceKernelWaitEventFlag(handle, 1, wait_all, reinterpret_cast<std::uint64_t*>(1), nullptr) == fault);
        CHECK(sceKernelWaitEventFlag(handle, 1, wait_all, nullptr, reinterpret_cast<std::uint32_t*>(1)) == fault);
        CHECK(sceKernelCancelEventFlag(handle, 0, reinterpret_cast<std::int32_t*>(1)) == fault);
        CHECK(sceKernelSetEventFlag(0, 1) == missing);
        CHECK(sceKernelSetEventFlag(handle, 1) == 0);
        CHECK(sceKernelPollEventFlag(handle, 1, wait_all | clear_all, reinterpret_cast<std::uint64_t*>(1)) == fault);
        CHECK(sceKernelPollEventFlag(handle, 1, wait_all, nullptr) == 0); // Fault consumed no bits.
    } else return 2;
    CHECK(sceKernelDeleteEventFlag(handle) == 0);
    std::puts("PASS event flags");
}
