#include "hle/loader/elf.h"
#include "hle/fs.h"
#include "hle/pad.h"
#include "hle/gnm.h"
#include "hle/video.h"
#include "memory/address_space.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string_view>

int main(int argc, char* argv[]) try {
    bool strict = false, headless = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--strict") strict = true;
        else if (arg == "--headless") headless = true;
        else if (arg == "--help") {
            std::cout << "Usage: launcher [--strict] [--headless]\n"
                "Inspects the executable; guest execution is not implemented.\n"
                "--strict rejects unresolved imports. --headless explicitly disables graphics.\n";
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n"; return 2;
        }
    }
    std::cout << "Gravity Rush experimental runtime - inspection only\n";
    struct Cleanup {
        ~Cleanup() {
            gravity::hle::video::sceVideoOutShutdown();
            gravity::hle::gnm::sceGnmShutdown();
            gravity::hle::pad::scePadEnd();
            gravity::hle::fs::shutdown_vfs();
        }
    } cleanup;
    if (!gravity::hle::fs::initialize_vfs()) {
        std::cerr << "VFS initialization failed\n"; return 1;
    }
    std::cout << "VFS: " << gravity::hle::fs::vfs_mounted_archives_count()
              << " archives, " << gravity::hle::fs::vfs_total_files_count() << " files\n";
    if (gravity::hle::pad::scePadInit() != 0 || !gravity::hle::gnm::sceGnmInitialize()) {
        std::cerr << "Input or GNM state initialization failed\n"; return 1;
    }
    const auto video = headless ? gravity::hle::video::initialize_headless()
                              : gravity::hle::video::sceVideoOutInitialize();
    if (!video) {
        std::cerr << "Video initialization failed: " << gravity::hle::error_name(video.error)
                  << ". Use --headless for inspection without graphics.\n"; return 1;
    }
    std::cout << (headless ? "Video: explicit headless mode\n" :
        "Video: Vulkan presentation backend (game geometry and shader translation pending)\n");
    std::ifstream file;
    for (const auto* path : {"CUSA01130/eboot.bin", "../CUSA01130/eboot.bin", "../../CUSA01130/eboot.bin"}) {
        file.open(path, std::ios::binary | std::ios::ate);
        if (file.is_open()) break;
    }
    if (!file.is_open()) { std::cerr << "Cannot find eboot.bin\n"; return 1; }
    const auto length = file.tellg();
    if (length <= 0 || length > 1024LL * 1024 * 1024) {
        std::cerr << "Invalid executable size (limit: 1 GiB)\n"; return 1;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        std::cerr << "Incomplete executable read\n"; return 1;
    }
    gravity::memory::AddressSpace space;
    auto loaded = gravity::hle::loader::load_self(bytes.data(), bytes.size(), space,
        gravity::memory::AddressSpace::default_base, !strict);
    if (!loaded.base) std::cerr << "Load rejected: " << (loaded.error ? loaded.error : "unknown error") << "\n";
    std::cout << "Unresolved imports: " << loaded.unresolved_symbols.size() << "\n";
    for (const auto& name : loaded.unresolved_symbols) std::cout << "  " << name << "\n";
    if (!loaded.base) return 1;
    std::cout << "Mapped at 0x" << std::hex << loaded.base << ", entry 0x" << loaded.entry
              << std::dec << ", relocation slots processed: " << loaded.num_relocations << "\n"
              << "Inspection completed. Guest execution remains disabled.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Runtime error: " << error.what() << "\n"; return 1;
}
