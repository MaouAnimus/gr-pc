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
    } else return 2;
    std::puts("PASS pthread mutex");
}
