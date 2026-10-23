#include "hle/runner/runner.h"
#include <iostream>
#include <string_view>
#include <cstdint>

namespace {

extern "C" GRAVITY_SYSV_ABI uint64_t sysv_good_func(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    return a + b + c + d + e + f;
}

extern "C" GRAVITY_SYSV_ABI uint64_t sysv_null_read_func(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    auto* ptr = reinterpret_cast<volatile uint64_t*>(0x0);
    return *ptr; // Triggers STATUS_ACCESS_VIOLATION (Read)
}

extern "C" GRAVITY_SYSV_ABI uint64_t sysv_null_write_func(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    auto* ptr = reinterpret_cast<volatile uint64_t*>(0x1000);
    *ptr = 0xCAFEBABE; // Triggers STATUS_ACCESS_VIOLATION (Write)
    return 1;
}

extern "C" GRAVITY_SYSV_ABI uint64_t sysv_div_zero_func(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    volatile uint64_t num = 100;
    volatile uint64_t den = 0;
    return num / den; // Triggers STATUS_INTEGER_DIVIDE_BY_ZERO
}

extern "C" GRAVITY_SYSV_ABI uint64_t sysv_infinite_loop_func(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    volatile uint64_t x = 0;
    while (true) {
        x = x + 1;
    }
    return x;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::string_view suite = (argc > 1) ? argv[1] : "all";

    if (suite == "all" || suite == "success") {
        std::cout << "[RUN] runner success...\n";
        gravity::hle::runner::RunnerConfig cfg;
        cfg.timeout_ms = 2000;
        auto rep = gravity::hle::runner::execute_isolated_sysv(
            reinterpret_cast<uintptr_t>(&sysv_good_func),
            1, 2, 3, 4, 5, 6, cfg
        );
        if (!rep.succeeded || rep.return_value != 21 || rep.crash_reason != gravity::hle::runner::CrashReason::none) {
            std::cerr << "FAIL: expected success (21), got:\n" << gravity::hle::runner::format_report(rep) << "\n";
            return 1;
        }
        std::cout << "[PASS] runner success (elapsed: " << rep.execution_time.count() << " us)\n";
    }

    if (suite == "all" || suite == "read_fault") {
        std::cout << "[RUN] runner read fault trapping...\n";
        gravity::hle::runner::RunnerConfig cfg;
        cfg.timeout_ms = 2000;
        auto rep = gravity::hle::runner::execute_isolated_sysv(
            reinterpret_cast<uintptr_t>(&sysv_null_read_func),
            0, 0, 0, 0, 0, 0, cfg
        );
        if (rep.succeeded) {
            std::cerr << "FAIL: read fault unexpectedly succeeded!\n";
            return 1;
        }
        if (rep.crash_reason != gravity::hle::runner::CrashReason::access_violation_read) {
            std::cerr << "FAIL: expected access_violation_read, got: "
                      << gravity::hle::runner::crash_reason_name(rep.crash_reason) << "\n";
            return 1;
        }
        if (rep.fault_address != 0) {
            std::cerr << "FAIL: expected target address 0x0, got 0x" << std::hex << rep.fault_address << "\n";
            return 1;
        }
        if (rep.registers.rip == 0) {
            std::cerr << "FAIL: registers.rip was not recorded!\n";
            return 1;
        }
        std::cout << "[PASS] runner read fault trapped cleanly (RIP=0x" << std::hex << rep.registers.rip << std::dec << ")\n";
    }

    if (suite == "all" || suite == "write_fault") {
        std::cout << "[RUN] runner write fault trapping...\n";
        gravity::hle::runner::RunnerConfig cfg;
        cfg.timeout_ms = 2000;
        auto rep = gravity::hle::runner::execute_isolated_sysv(
            reinterpret_cast<uintptr_t>(&sysv_null_write_func),
            0, 0, 0, 0, 0, 0, cfg
        );
        if (rep.succeeded) {
            std::cerr << "FAIL: write fault unexpectedly succeeded!\n";
            return 1;
        }
        if (rep.crash_reason != gravity::hle::runner::CrashReason::access_violation_write) {
            std::cerr << "FAIL: expected access_violation_write, got: "
                      << gravity::hle::runner::crash_reason_name(rep.crash_reason) << "\n";
            return 1;
        }
        if (rep.fault_address != 0x1000) {
            std::cerr << "FAIL: expected target address 0x1000, got 0x" << std::hex << rep.fault_address << "\n";
            return 1;
        }
        std::cout << "[PASS] runner write fault trapped cleanly\n";
    }

    if (suite == "all" || suite == "div_zero") {
        std::cout << "[RUN] runner divide by zero trapping...\n";
        gravity::hle::runner::RunnerConfig cfg;
        cfg.timeout_ms = 2000;
        auto rep = gravity::hle::runner::execute_isolated_sysv(
            reinterpret_cast<uintptr_t>(&sysv_div_zero_func),
            0, 0, 0, 0, 0, 0, cfg
        );
        if (rep.succeeded) {
            std::cerr << "FAIL: divide by zero unexpectedly succeeded!\n";
            return 1;
        }
        if (rep.crash_reason != gravity::hle::runner::CrashReason::divide_by_zero) {
            std::cerr << "FAIL: expected divide_by_zero, got: "
                      << gravity::hle::runner::crash_reason_name(rep.crash_reason) << "\n";
            return 1;
        }
        std::cout << "[PASS] runner divide by zero trapped cleanly\n";
    }

    if (suite == "all" || suite == "timeout") {
        std::cout << "[RUN] runner watchdog timeout...\n";
        gravity::hle::runner::RunnerConfig cfg;
        cfg.timeout_ms = 100; // 100 ms watchdog
        auto rep = gravity::hle::runner::execute_isolated_sysv(
            reinterpret_cast<uintptr_t>(&sysv_infinite_loop_func),
            0, 0, 0, 0, 0, 0, cfg
        );
        if (rep.succeeded) {
            std::cerr << "FAIL: infinite loop unexpectedly succeeded!\n";
            return 1;
        }
        if (rep.crash_reason != gravity::hle::runner::CrashReason::timeout) {
            std::cerr << "FAIL: expected timeout, got: "
                      << gravity::hle::runner::crash_reason_name(rep.crash_reason) << "\n";
            return 1;
        }
        std::cout << "[PASS] runner watchdog timeout triggered after " << rep.execution_time.count() << " us\n";
    }

    if (suite == "all" || suite == "module_mapping") {
        std::cout << "[RUN] runner guest module offset mapping...\n";
        gravity::hle::runner::RunnerConfig cfg;
        cfg.timeout_ms = 2000;
        // Mock guest base around the faulting function
        uintptr_t func_addr = reinterpret_cast<uintptr_t>(&sysv_null_read_func);
        cfg.guest_base = func_addr & ~0xFFF; // Page boundary
        cfg.guest_size = 0x10000;
        cfg.guest_module_name = "eboot.bin";

        auto rep = gravity::hle::runner::execute_isolated_sysv(
            func_addr, 0, 0, 0, 0, 0, 0, cfg
        );
        if (rep.module_name != "eboot.bin") {
            std::cerr << "FAIL: expected module eboot.bin, got " << rep.module_name << "\n";
            return 1;
        }
        if (rep.module_offset != (rep.fault_ip - cfg.guest_base)) {
            std::cerr << "FAIL: module offset calculation mismatch\n";
            return 1;
        }
        std::cout << "[PASS] runner mapped fault to " << rep.module_name << "+0x"
                  << std::hex << rep.module_offset << std::dec << "\n";
    }

    std::cout << "All runner tests passed successfully.\n";
    return 0;
}
