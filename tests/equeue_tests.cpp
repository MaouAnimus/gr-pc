#include "hle/kernel/equeue.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle::equeue;
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    Handle queue{};
    CHECK(sceKernelCreateEqueue(&queue, "test queue") == 0 && queue);
    Event output[2]{};
    int count = -1;
    const std::uint32_t poll = 0;
    if (suite == "lifecycle") {
        CHECK(sceKernelWaitEqueue(queue, output, 2, &count, &poll) == timed_out && count == 0);
        Event sent{42, -14, 0, 0x123, 99, reinterpret_cast<void*>(0x1234)};
        CHECK(post(queue, sent) && post(queue, sent));
        CHECK(sceKernelWaitEqueue(queue, output, 1, &count, &poll) == 0 && count == 1);
        CHECK(output[0].ident == 42 && output[0].filter == -14 && output[0].fflags == 0x123);
        CHECK(output[0].data == 99 && output[0].udata == sent.udata);
        CHECK(sceKernelWaitEqueue(queue, output, 2, &count, &poll) == 0 && count == 1);
        CHECK(sceKernelWaitEqueue(queue, output, 2, &count, &poll) == timed_out);
    } else if (suite == "wait") {
        std::thread producer([&] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); CHECK(post(queue, Event{7})); });
        CHECK(sceKernelWaitEqueue(queue, output, 2, &count, nullptr) == 0 && count == 1 && output[0].ident == 7);
        producer.join();
        std::atomic<bool> entered{};
        std::thread waiter([&] { entered = true; CHECK(sceKernelWaitEqueue(queue, output, 2, &count, nullptr) == canceled); });
        while (!entered) std::this_thread::yield();
        // The waiter may not yet hold its shared reference: wait briefly before deletion.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(sceKernelDeleteEqueue(queue) == 0);
        waiter.join();
        CHECK(!post(queue, Event{7}));
        return 0;
    } else if (suite == "invalid") {
        CHECK(sceKernelCreateEqueue(nullptr, "x") == invalid);
        CHECK(sceKernelWaitEqueue(0, output, 1, &count, &poll) == bad_handle);
        CHECK(sceKernelWaitEqueue(queue, output, 0, &count, &poll) == invalid);
        CHECK(sceKernelWaitEqueue(queue, nullptr, 1, &count, &poll) == fault);
        CHECK(sceKernelWaitEqueue(queue, output, 1, nullptr, &poll) == fault);
        CHECK(sceKernelWaitEqueue(queue, output, 1, &count, reinterpret_cast<const std::uint32_t*>(1)) == fault);
    } else return 2;
    CHECK(sceKernelDeleteEqueue(queue) == 0);
    CHECK(sceKernelDeleteEqueue(queue) == bad_handle);
    std::puts("PASS equeue");
}
