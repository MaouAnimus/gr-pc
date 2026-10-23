#include "hle/loader/elf.h"
#include "hle/fs.h"
#include "hle/pad.h"
#include "hle/gnm.h"
#include "hle/video.h"
#include "hle/runner/runner.h"
#include "hle/registry/adapter_registry.h"
#include "memory/address_space.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string_view>
#include <string>

int main(int argc, char* argv[]) try {
    bool strict = false, headless = false, boot = false;
    std::uint32_t timeout_ms = 5000;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--strict") strict = true;
        else if (arg == "--headless") headless = true;
        else if (arg == "--boot" || arg == "--run") boot = true;
        else if (arg.starts_with("--timeout=")) {
            const auto val = arg.substr(10);
            timeout_ms = static_cast<std::uint32_t>(std::stoul(std::string(val)));
        } else if (arg == "--help") {
            std::cout << "Usage: launcher [--strict] [--headless] [--boot] [--timeout=<ms>]\n"
                "Inspects or boots the executable under isolated System V runner.\n"
                "--boot: Executes guest entry point with diagnostic traps.\n"
                "--timeout=<ms>: Sets execution timeout in milliseconds (default: 5000).\n"
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
    if (boot) {
        gravity::hle::AdapterRegistry::instance().initialize_builtin_adapters();
    }
    auto loaded = gravity::hle::loader::load_self(bytes.data(), bytes.size(), space,
        gravity::memory::AddressSpace::default_base, !strict, /*bind_unresolved_traps=*/boot);
    if (!loaded.base) {
        std::cerr << "Load rejected: " << (loaded.error ? loaded.error : "unknown error") << "\n";
        return 1;
    }
    std::cout << "Unresolved imports: " << loaded.unresolved_symbols.size() << "\n";
    if (!boot) {
        for (const auto& name : loaded.unresolved_symbols) std::cout << "  " << name << "\n";
        std::cout << "Mapped at 0x" << std::hex << loaded.base << ", entry 0x" << loaded.entry
                  << std::dec << ", relocation slots processed: " << loaded.num_relocations << "\n"
                  << "Inspection completed. Guest execution remains disabled.\n";
        return 0;
    }

    // N08: Instrumented Boot Execution
    std::cout << "Mapped at 0x" << std::hex << loaded.base << ", entry 0x" << loaded.entry
              << std::dec << ", relocation slots processed: " << loaded.num_relocations << "\n";
    std::cout << "Dynamic trap thunks generated: "
              << gravity::hle::AdapterRegistry::instance().unresolved_count() << "\n";

    gravity::hle::AdapterRegistry::instance().set_guest_base(loaded.base);

    gravity::hle::UnresolvedCallInfo trap_info{};
    bool trapped = false;
    gravity::hle::AdapterRegistry::instance().set_unresolved_hook([&](const gravity::hle::UnresolvedCallInfo& info) {
        trap_info = info;
        trapped = true;
    });

    struct GuestArgs {
        std::uint32_t argc;
        std::uint32_t padding;
        const char* argv[2];
    };
    static const char app_path[] = "/app0/eboot.bin";
    GuestArgs guest_args{
        .argc = 1,
        .padding = 0,
        .argv = { app_path, nullptr }
    };

    gravity::hle::runner::RunnerConfig cfg{
        .timeout_ms = timeout_ms,
        .guest_base = loaded.base,
        .guest_size = loaded.total_size,
        .guest_module_name = "eboot.bin"
    };

    std::cout << "\n============================================================\n"
              << "[N08] EXECUTING GUEST ENTRY POINT: 0x" << std::hex << loaded.entry << std::dec << "\n"
              << "============================================================\n";

    auto report = gravity::hle::runner::execute_isolated_sysv(
        loaded.entry,
        reinterpret_cast<uint64_t>(&guest_args),
        0, 0, 0, 0, 0,
        cfg
    );

    std::cout << "\n============================================================\n"
              << "           GUEST INSTRUMENTED EXECUTION REPORT              \n"
              << "============================================================\n"
              << gravity::hle::runner::format_report(report) << "\n";

    if (trapped) {
        std::cout << "\n[HLE Diagnostic Trap Intercepted]\n"
                  << "  Library:       " << trap_info.library << "\n"
                  << "  NID:           " << trap_info.nid << "\n"
                  << "  Caller RIP:    0x" << std::hex << trap_info.caller_rip << "\n"
                  << "  Module Offset: eboot.bin+0x" << trap_info.guest_offset << std::dec << "\n";
    }

    return report.succeeded ? 0 : (trapped ? 0 : 1);
} catch (const std::exception& error) {
    std::cerr << "Runtime error: " << error.what() << "\n"; return 1;
}
