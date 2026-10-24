#include "hle/kernel/pthread.h"
#include "hle/kernel/pthread_sync.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle;
struct Work { std::atomic<bool> called{}; DWORD native_thread{}; pthread::Opaque token{}; };
void* GRAVITY_SYSV_ABI callback(void* argument) {
    auto& work = *static_cast<Work*>(argument);
    work.native_thread = GetCurrentThreadId(); work.token = pthread::self(); work.called = true;
    Sleep(5);
    return reinterpret_cast<void*>(0x12345678);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    if (suite == "callback") {
        Work work;
        pthread::Opaque thread{};
        CHECK(pthread::create(&thread, nullptr, callback, &work, "callback test") == 0 && thread);
        void* result{};
        CHECK(pthread::join(thread, &result) == 0);
        CHECK(work.called && work.token == thread && work.native_thread != GetCurrentThreadId());
        CHECK(result == reinterpret_cast<void*>(0x12345678));
        CHECK(pthread::join(thread, &result) == 3);
    } else if (suite == "attributes") {
        struct { pthread::Opaque value{}; std::uint64_t canary{0xabcdef}; } attr;
        CHECK(pthread::attr_init(&attr.value) == 0 && attr.canary == 0xabcdef);
        CHECK(pthread::attr_setstacksize(&attr.value, 1) == 22);
        CHECK(pthread::attr_setstacksize(&attr.value, 2 * 1024 * 1024) == 0);
        CHECK(pthread::attr_setdetachstate(&attr.value, 2) == 22);
        Work work;
        pthread::Opaque thread{};
        CHECK(pthread::create(&thread, &attr.value, callback, &work, "attributes") == 0);
        CHECK(pthread::attr_destroy(&attr.value) == 0);
        CHECK(pthread::join(thread, nullptr) == 0 && work.called);
    } else if (suite == "invalid") {
        CHECK(pthread::create(nullptr, nullptr, callback, nullptr, "x") == 22);
        pthread::Opaque thread{};
        CHECK(pthread::create(&thread, nullptr, nullptr, nullptr, "x") == 22);
        CHECK(pthread::attr_init(nullptr) == 22);
        CHECK(pthread::join(pthread::self(), nullptr) == 11);
        CHECK(pthread::join(123, nullptr) == 3);
        int priority{};
        CHECK(pthread::getprio(pthread::self(), &priority) == 0 && priority == 256);
        CHECK(pthread::setprio(pthread::self(), 1) == 22);
        CHECK(pthread::getprio(123, &priority) == 3);
    } else return 2;
    std::puts("PASS native pthread");
}
