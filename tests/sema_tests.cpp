#include "hle/kernel/sema.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle::sema;
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    Handle handle{};
    CHECK(sceKernelCreateSema(&handle, "test", 0, 0, 3, nullptr) == 0 && handle);
    if (suite == "count") {
        CHECK(sceKernelSignalSema(handle, 2) == 0);
        std::uint32_t poll{};
        CHECK(sceKernelWaitSema(handle, 3, &poll) == static_cast<std::int32_t>(0x8002003cU));
        CHECK(sceKernelWaitSema(handle, 2, &poll) == 0); // Failed wait consumed no partial tokens.
        CHECK(sceKernelSignalSema(handle, 3) == 0);
        CHECK(sceKernelSignalSema(handle, 1) == static_cast<std::int32_t>(0x80020016U));
        CHECK(sceKernelWaitSema(handle, 3, &poll) == 0);
    } else if (suite == "wait") {
        std::atomic<bool> finished{};
        std::thread waiter([&] { CHECK(sceKernelWaitSema(handle, 2, nullptr) == 0); finished = true; });
        CHECK(sceKernelSignalSema(handle, 1) == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(!finished);
        CHECK(sceKernelSignalSema(handle, 1) == 0);
        waiter.join(); CHECK(finished);
        std::uint32_t micros = 1000;
        CHECK(sceKernelWaitSema(handle, 1, &micros) == static_cast<std::int32_t>(0x8002003cU) && micros == 0);
    } else if (suite == "cancel") {
        std::atomic<bool> completed{};
        std::thread waiter([&] {
            CHECK(sceKernelWaitSema(handle, 1, nullptr) == static_cast<std::int32_t>(0x80020055U));
            completed = true;
        });
        int waiting{};
        for (int i = 0; i < 1000 && !completed; ++i) {
            CHECK(sceKernelCancelSema(handle, 0, &waiting) == 0);
            if (waiting) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(waiting == 1); waiter.join(); CHECK(completed);
    } else if (suite == "invalid") {
        CHECK(sceKernelCreateSema(nullptr, "x", 0, 0, 1, nullptr) != 0);
        CHECK(sceKernelSignalSema(handle, 0) != 0);
        CHECK(sceKernelWaitSema(handle, 4, nullptr) != 0);
        CHECK(sceKernelCancelSema(handle, 4, nullptr) != 0);
    } else return 2;
    CHECK(sceKernelDeleteSema(handle) == 0);
    CHECK(sceKernelWaitSema(handle, 1, nullptr) == static_cast<std::int32_t>(0x80020003U));
    std::puts("PASS native semaphore");
}
