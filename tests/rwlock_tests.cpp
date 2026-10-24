#include "hle/kernel/rwlock.h"
#include <atomic>
#include <barrier>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle::rwlock;
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    Opaque lock{}, attr{};
    CHECK(scePthreadRwlockattrInit(&attr) == 0);
    CHECK(scePthreadRwlockInit(&lock, &attr, "test") == 0);
    CHECK(scePthreadRwlockattrDestroy(&attr) == 0);
    if (suite == "concurrent") {
        std::barrier rendezvous(4);
        std::atomic<int> readers{};
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
            CHECK(scePthreadRwlockRdlock(&lock) == 0); ++readers;
            rendezvous.arrive_and_wait(); CHECK(readers == 4);
            rendezvous.arrive_and_wait();
            CHECK(scePthreadRwlockUnlock(&lock) == 0);
        });
        for (auto& worker : workers) worker.join();
        int counter{};
        workers.clear();
        for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
            for (int j = 0; j < 500; ++j) {
                CHECK(scePthreadRwlockWrlock(&lock) == 0); ++counter;
                CHECK(scePthreadRwlockUnlock(&lock) == 0);
            }
        });
        for (auto& worker : workers) worker.join();
        CHECK(counter == 2000);
    } else if (suite == "ownership") {
        CHECK(scePthreadRwlockRdlock(&lock) == 0);
        CHECK(scePthreadRwlockRdlock(&lock) == 0);
        CHECK(scePthreadRwlockWrlock(&lock) == static_cast<int>(0x8002000bU));
        CHECK(scePthreadRwlockDestroy(&lock) == static_cast<int>(0x80020010U));
        std::thread other([&] { CHECK(scePthreadRwlockUnlock(&lock) == static_cast<int>(0x80020001U)); });
        other.join();
        CHECK(scePthreadRwlockUnlock(&lock) == 0); CHECK(scePthreadRwlockUnlock(&lock) == 0);
        CHECK(scePthreadRwlockWrlock(&lock) == 0);
        CHECK(scePthreadRwlockRdlock(&lock) == static_cast<int>(0x8002000bU));
        CHECK(scePthreadRwlockUnlock(&lock) == 0);
    } else return 2;
    CHECK(scePthreadRwlockDestroy(&lock) == 0);
    CHECK(scePthreadRwlockRdlock(&lock) != 0);
}
