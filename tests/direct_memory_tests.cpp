#include "hle/kernel/direct_memory.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle::direct_memory;
int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];
    std::int64_t first = -1, second = -1;
    CHECK(sceKernelAllocateDirectMemory(0, pool_size, 0x4000, 0x200000, 3, &first) == 0);
    CHECK(first == 0);
    CHECK(sceKernelAllocateDirectMemory(0, pool_size, 0x4000, 0x200000, 0, &second) == 0);
    CHECK(second == 0x200000);
    if (suite == "mapping") {
        void* address{};
        CHECK(sceKernelMapDirectMemory(&address, 0x4000, 0x33, 0, first, 0x200000) == 0);
        CHECK(address && reinterpret_cast<std::uintptr_t>(address) % 0x200000 == 0);
        static_cast<unsigned char*>(address)[0] = 0x12;
        static_cast<unsigned char*>(address)[0x3fff] = 0x34;
        void* alias{};
        CHECK(sceKernelMapDirectMemory(&alias, 0x4000, 3, 0, first, 0) != 0 && !alias);
        MEMORY_BASIC_INFORMATION info{};
        CHECK(VirtualQuery(address, &info, sizeof(info)) && info.State == MEM_COMMIT);
        const auto reservation = info.AllocationBase;
        CHECK(sceKernelReleaseDirectMemory(first, 0x4000) == 0);
        CHECK(VirtualQuery(reservation, &info, sizeof(info)) && info.State == MEM_FREE);
        first = -1;
    } else if (suite == "bounds") {
        std::int64_t failed = -1;
        CHECK(sceKernelAllocateDirectMemory(0, 0x4000, 0x4000, 0, 0, &failed) != 0 && failed == -1);
        CHECK(sceKernelAllocateDirectMemory(0, pool_size, 1, 0, 0, &failed) != 0);
        CHECK(sceKernelAllocateDirectMemory(0, pool_size, 0x4000, 3, 0, &failed) != 0);
        CHECK(sceKernelAllocateDirectMemory(0, pool_size, 0x4000, 0, 0, nullptr) != 0);
        CHECK(sceKernelReleaseDirectMemory(second, 0x8000) != 0);
        void* address{};
        CHECK(sceKernelMapDirectMemory(&address, 0x4000, 7, 0, second, 0) != 0 && !address);
    } else return 2;
    if (first >= 0) CHECK(sceKernelReleaseDirectMemory(first, 0x4000) == 0);
    CHECK(sceKernelReleaseDirectMemory(second, 0x4000) == 0);
    CHECK(sceKernelAllocateDirectMemory(0, 0x4000, 0x4000, 0, 0, &first) == 0 && first == 0);
    CHECK(sceKernelReleaseDirectMemory(first, 0x4000) == 0);
}
