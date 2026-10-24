#include "hle/kernel/pthread_sync.h"
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle::pthread_sync;
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    if (suite == "attributes" || suite == "recursive") {
        struct { Opaque value{}; Opaque canary{0x123456789abcdef0ULL}; } attr;
        CHECK(posix_mutexattr_init(&attr.value) == 0);
        CHECK(attr.canary == 0x123456789abcdef0ULL);
        CHECK(posix_mutexattr_settype(&attr.value, 0) == 22);
        CHECK(posix_mutexattr_settype(&attr.value, suite == "recursive" ? 2 : 1) == 0);
        Opaque mutex{};
        CHECK(posix_mutex_init(&mutex, &attr.value) == 0);
        CHECK(posix_mutexattr_destroy(&attr.value) == 0);
        CHECK(posix_mutex_lock(&mutex) == 0);
        CHECK(posix_mutex_destroy(&mutex) == 16);
        if (suite == "recursive") {
            CHECK(posix_mutex_lock(&mutex) == 0);
            CHECK(posix_mutex_trylock(&mutex) == 0);
            CHECK(posix_mutex_unlock(&mutex) == 0);
            CHECK(posix_mutex_unlock(&mutex) == 0);
        } else {
            CHECK(posix_mutex_lock(&mutex) == 11);
            CHECK(posix_mutex_trylock(&mutex) == 16);
        }
        CHECK(posix_mutex_unlock(&mutex) == 0);
        CHECK(posix_mutex_unlock(&mutex) == 1);
        CHECK(posix_mutex_destroy(&mutex) == 0);
        CHECK(posix_mutex_lock(&mutex) == 22);
    } else if (suite == "concurrent") {
        Opaque mutex{}; // Static initialization must be safe across competing callers.
        int total{};
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
            for (int j = 0; j < 1000; ++j) {
                CHECK(posix_mutex_lock(&mutex) == 0); ++total; CHECK(posix_mutex_unlock(&mutex) == 0);
            }
        });
        for (auto& worker : workers) worker.join();
        CHECK(total == 4000);
        CHECK(posix_mutex_lock(&mutex) == 0);
        std::thread timed([&] {
            CHECK(posix_mutex_unlock(&mutex) == 1);
            CHECK(posix_mutex_trylock(&mutex) == 16);
            CHECK(posix_mutex_reltimedlock(&mutex, 1000) == 60);
        });
        timed.join();
        CHECK(posix_mutex_unlock(&mutex) == 0);
        CHECK(posix_mutex_destroy(&mutex) == 0);
    } else if (suite == "invalid") {
        CHECK(posix_mutexattr_init(nullptr) == 22);
        CHECK(posix_mutex_lock(reinterpret_cast<Opaque*>(1)) == 22);
        Opaque invalid = 123;
        CHECK(posix_mutex_unlock(&invalid) == 22);
        CHECK(posix_mutexattr_destroy(&invalid) == 22);
        CHECK(posix_condattr_init(nullptr) == 22);
        CHECK(posix_cond_signal(reinterpret_cast<Opaque*>(1)) == 22);
        constexpr auto kernel_invalid = static_cast<std::int32_t>(0x80020016U);
        CHECK(scePthreadCondInit(nullptr, nullptr, "invalid") == kernel_invalid);
        CHECK(scePthreadCondDestroy(&invalid) == kernel_invalid);
        CHECK(scePthreadCondSignal(reinterpret_cast<Opaque*>(1)) == kernel_invalid);
        CHECK(scePthreadCondBroadcast(reinterpret_cast<Opaque*>(1)) == kernel_invalid);
        CHECK(scePthreadCondattrInit(nullptr) == kernel_invalid);
        CHECK(scePthreadCondattrDestroy(&invalid) == kernel_invalid);
    } else if (suite == "cond_handoff") {
        Opaque cond{}, mutex{};
        CHECK(posix_mutex_init(&mutex, nullptr) == 0);
        CHECK(posix_cond_init(&cond, nullptr) == 0);
        int turn = 0;
        constexpr int rounds = 10000;
        std::thread worker([&] {
            CHECK(posix_mutex_lock(&mutex) == 0);
            for (int i = 0; i < rounds; ++i) {
                while (turn != 2 * i + 1)
                    CHECK(posix_cond_reltimedwait(&cond, &mutex, 1000000) == 0);
                ++turn;
                CHECK(posix_cond_signal(&cond) == 0);
            }
            CHECK(posix_mutex_unlock(&mutex) == 0);
        });
        CHECK(posix_mutex_lock(&mutex) == 0);
        for (int i = 0; i < rounds; ++i) {
            CHECK(turn == 2 * i);
            ++turn;
            CHECK(posix_cond_signal(&cond) == 0);
            while (turn != 2 * i + 2)
                CHECK(posix_cond_reltimedwait(&cond, &mutex, 1000000) == 0);
        }
        CHECK(posix_mutex_unlock(&mutex) == 0);
        worker.join();
        CHECK(turn == 2 * rounds);
        CHECK(posix_cond_destroy(&cond) == 0);
        CHECK(posix_mutex_destroy(&mutex) == 0);
    } else if (suite == "cond") {
        Opaque cond_attr{};
        CHECK(posix_condattr_init(&cond_attr) == 0);
        CHECK(posix_condattr_destroy(&cond_attr) == 0);

        Opaque cond{};
        Opaque mutex{};
        CHECK(posix_mutex_init(&mutex, nullptr) == 0);
        CHECK(posix_cond_init(&cond, nullptr) == 0);

        bool signaled = false;
        std::thread waiter([&] {
            CHECK(posix_mutex_lock(&mutex) == 0);
            while (!signaled) {
                CHECK(posix_cond_wait(&cond, &mutex) == 0);
            }
            CHECK(posix_mutex_unlock(&mutex) == 0);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(posix_mutex_lock(&mutex) == 0);
        signaled = true;
        CHECK(posix_cond_signal(&cond) == 0);
        CHECK(posix_mutex_unlock(&mutex) == 0);
        waiter.join();

        // Timed wait should time out
        CHECK(posix_mutex_lock(&mutex) == 0);
        int rc = posix_cond_reltimedwait(&cond, &mutex, 2000);
        CHECK(rc == 60); // ETIMEDOUT
        CHECK(scePthreadCondTimedwait(&cond, &mutex, 2000) == static_cast<std::int32_t>(0x8002003cU));
        CHECK(posix_mutex_unlock(&mutex) == 0);
        CHECK(scePthreadCondWait(&cond, &mutex) == static_cast<std::int32_t>(0x80020001U));
        CHECK(scePthreadCondTimedwait(&cond, &mutex, 1) == static_cast<std::int32_t>(0x80020001U));

        // scePthreadCond wrappers
        CHECK(scePthreadCondSignal(&cond) == 0);
        CHECK(scePthreadCondBroadcast(&cond) == 0);

        CHECK(posix_cond_destroy(&cond) == 0);
        CHECK(posix_mutex_destroy(&mutex) == 0);
    } else return 2;
    std::puts("PASS pthread sync");
}
