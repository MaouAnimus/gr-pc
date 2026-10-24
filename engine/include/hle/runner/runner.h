#pragma once
#include <cstdint>
#include <string>
#include <chrono>
#include "hle/abi.h"
#include "hle/native_tls.h"

namespace gravity::hle::runner {

enum class CrashReason {
    none = 0,
    access_violation_read,
    access_violation_write,
    access_violation_execute,
    illegal_instruction,
    privileged_instruction,
    divide_by_zero,
    stack_overflow,
    breakpoint,
    timeout,
    unknown
};

struct RegisterDump {
    std::uint64_t rip{0};
    std::uint64_t rsp{0};
    std::uint64_t rbp{0};
    std::uint64_t rax{0};
    std::uint64_t rbx{0};
    std::uint64_t rcx{0};
    std::uint64_t rdx{0};
    std::uint64_t rsi{0};
    std::uint64_t rdi{0};
    std::uint64_t r8{0};
    std::uint64_t r9{0};
    std::uint64_t r10{0};
    std::uint64_t r11{0};
    std::uint64_t r12{0};
    std::uint64_t r13{0};
    std::uint64_t r14{0};
    std::uint64_t r15{0};
    std::uint32_t eflags{0};
};

struct ExecutionReport {
    bool succeeded{false};
    std::uint64_t return_value{0};
    CrashReason crash_reason{CrashReason::none};
    std::uint32_t native_exception_code{0};
    std::uint32_t thread_id{0};
    std::uintptr_t fault_address{0};
    std::uintptr_t fault_ip{0};
    std::string module_name;
    std::uintptr_t module_offset{0};
    RegisterDump registers{};
    std::string error_message;
    std::chrono::microseconds execution_time{0};
};

struct RunnerConfig {
    std::uint32_t timeout_ms{5000};
    std::uintptr_t guest_base{0};
    std::size_t guest_size{0};
    std::string guest_module_name{"eboot.bin"};
    TlsImage tls;
    bool terminate_process_on_timeout{false};
    bool terminate_process_on_exception{false};
};

// Executes a System V function pointer in a monitored context with Vectored Exception Handling
ExecutionReport execute_isolated_sysv(
    std::uintptr_t entry_point,
    std::uint64_t arg0 = 0,
    std::uint64_t arg1 = 0,
    std::uint64_t arg2 = 0,
    std::uint64_t arg3 = 0,
    std::uint64_t arg4 = 0,
    std::uint64_t arg5 = 0,
    const RunnerConfig& config = {}
);

// Formats the diagnostic report as a human-readable string
std::string format_report(const ExecutionReport& report);

const char* crash_reason_name(CrashReason reason) noexcept;
ExecutionReport execute_current_sysv(std::uintptr_t entry, std::uint64_t argument, const RunnerConfig& config);

} // namespace gravity::hle::runner
