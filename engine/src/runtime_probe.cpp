#include "hle/fiber.h"
#include "hle/kernel.h"
#include "memory/address_space.h"
#include "hle/loader/elf.h"
#include <cstdio>
#include <fstream>
#include <vector>
#include <iostream>

namespace {
bool checked(const char* action, gravity::hle::Status status) {
    std::printf("%s: %s (Win32 %lu)\n", action, gravity::hle::error_name(status.error),
                static_cast<unsigned long>(status.native_error));
    return static_cast<bool>(status);
}
void GRAVITY_SYSV_ABI entry(std::uint64_t initial, std::uint64_t argument) {
    std::printf("SysV fiber: initial=%llu, run=%llu\n",
                static_cast<unsigned long long>(initial), static_cast<unsigned long long>(argument));
    std::uint64_t resumed{};
    if (!gravity::hle::FiberScheduler::for_thread()->return_to_thread(initial + argument, resumed))
        std::terminate();
    std::printf("SysV fiber resumed: %llu\n", static_cast<unsigned long long>(resumed));
}
}

int main() {
    using namespace gravity;
    hle::ProcessClock clock;
    memory::AddressSpace memory;
    if (!checked("Reserve 4 GiB at 0x1000000000", memory.reserve())) return 1;
    if (!checked("Commit first page", memory.commit(0, memory::AddressSpace::page_size()))) return 1;
    hle::FiberScheduler scheduler;
    if (!checked("Attach scheduler", scheduler.attach())) return 1;
    hle::Fiber fiber;
    if (!checked("Initialize fiber", fiber.initialize(scheduler, entry, 7))) return 1;
    std::uint64_t returned{};
    if (!checked("Run fiber", scheduler.run(fiber, 35, returned))) return 1;
    std::printf("Returned value: %llu\n", static_cast<unsigned long long>(returned));
    if (!checked("Resume fiber to completion", scheduler.run(fiber, 99, returned))) return 1;
    if (!checked("Finalize fiber", fiber.finalize())) return 1;
    if (!checked("Detach scheduler", scheduler.detach())) return 1;
    if (!checked("Release address space", memory.release())) return 1;
    std::printf("Elapsed: %llu us\n", static_cast<unsigned long long>(clock.microseconds()));
    std::puts("Host backend ready. Now loading EBoot...");

    // Optionally load eboot.bin if available
    const char* candidates[] = {
        "../../CUSA01130/eboot.bin",
        "../CUSA01130/eboot.bin",
        "CUSA01130/eboot.bin"
    };

    std::ifstream ebootFile;
    for (const char* path : candidates) {
        ebootFile.open(path, std::ios::binary | std::ios::ate);
        if (ebootFile.is_open()) break;
    }

    if (ebootFile.is_open()) {
        size_t fileSize = static_cast<size_t>(ebootFile.tellg());
        ebootFile.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(fileSize);
        ebootFile.read(reinterpret_cast<char*>(buffer.data()), fileSize);
        ebootFile.close();
        std::printf("Loaded eboot.bin of size %zu bytes\n", fileSize);

        // Load ELF / SELF into 0x1000000000
        gravity::hle::loader::LoadResult loadRes = gravity::hle::loader::load_elf(buffer.data(), fileSize, 0x1000000000ULL, true);
        if (!loadRes.base) {
            std::puts("ERROR: Failed to load ELF/SELF");
            return 1;
        }
        std::printf("ELF loaded at base 0x%llx, entry 0x%llx, %u relocations applied\n",
                    static_cast<unsigned long long>(loadRes.base),
                    static_cast<unsigned long long>(loadRes.entry),
                    loadRes.num_relocations);

        std::printf("Inspection only: %zu unresolved imports; guest execution disabled.\n", loadRes.unresolved_symbols.size());
        {
            std::ofstream unresOut("unresolved_fresh.txt");
            for (const auto& sym : loadRes.unresolved_symbols) {
                unresOut << sym << "\n";
            }
        }
        const auto* opcodes = reinterpret_cast<const std::uint8_t*>(loadRes.entry);
        std::printf("Entry point machine code: %02x %02x %02x %02x\n",
                    opcodes[0], opcodes[1], opcodes[2], opcodes[3]);
    } else {
        std::puts("eboot.bin not found; guest load skipped.");
    }

    return clock.valid() ? 0 : 1;
}