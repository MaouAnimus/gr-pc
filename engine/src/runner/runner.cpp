#include "hle/runner/runner.h"
#include <Windows.h>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <mutex>

namespace gravity::hle::runner {

namespace {

struct RunnerThreadContext {
    DWORD thread_id{0};
    uintptr_t entry_point{0};
    uint64_t args[6]{0, 0, 0, 0, 0, 0};
    RunnerConfig config;
    ExecutionReport report;
    std::atomic<bool> completed{false};
    std::atomic<bool> crashed{false};
};

thread_local RunnerThreadContext* g_active_context = nullptr;
std::mutex g_veh_mutex;
PVOID g_veh_handle = nullptr;
std::atomic<int> g_active_runners{0};

CrashReason map_exception_code(DWORD code, ULONG_PTR access_type) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        if (access_type == 0) return CrashReason::access_violation_read;
        if (access_type == 1) return CrashReason::access_violation_write;
        if (access_type == 8) return CrashReason::access_violation_execute;
        return CrashReason::access_violation_read;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return CrashReason::illegal_instruction;
    case EXCEPTION_PRIV_INSTRUCTION:
        return CrashReason::privileged_instruction;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return CrashReason::divide_by_zero;
    case EXCEPTION_STACK_OVERFLOW:
        return CrashReason::stack_overflow;
    case EXCEPTION_BREAKPOINT:
        return CrashReason::breakpoint;
    default:
        return CrashReason::unknown;
    }
}

void resolve_address_module(uintptr_t ip, const RunnerConfig& config,
                            std::string& out_module, uintptr_t& out_offset) {
    if (config.guest_base != 0 && ip >= config.guest_base &&
        ip < config.guest_base + config.guest_size) {
        out_module = config.guest_module_name;
        out_offset = ip - config.guest_base;
        return;
    }

    HMODULE h_mod = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(ip), &h_mod) && h_mod != nullptr) {
        char path_buf[MAX_PATH] = {0};
        if (GetModuleFileNameA(h_mod, path_buf, MAX_PATH) > 0) {
            std::string full_path(path_buf);
            const auto last_slash = full_path.find_last_of("\\/");
            out_module = (last_slash != std::string::npos) ? full_path.substr(last_slash + 1) : full_path;
            out_offset = ip - reinterpret_cast<uintptr_t>(h_mod);
            return;
        }
    }

    out_module = "unknown_module";
    out_offset = ip;
}

LONG WINAPI runner_vectored_exception_handler(PEXCEPTION_POINTERS p_info) {
    auto* ctx = g_active_context;
    if (!ctx || ctx->thread_id != GetCurrentThreadId() || ctx->completed.load()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

#undef exception_code
    ctx->crashed.store(true);
    auto* rec = p_info->ExceptionRecord;
    auto* crec = p_info->ContextRecord;

    ctx->report.succeeded = false;
    ctx->report.native_exception_code = rec->ExceptionCode;
    ctx->report.thread_id = ctx->thread_id;
    ctx->report.fault_ip = reinterpret_cast<uintptr_t>(rec->ExceptionAddress);

    ULONG_PTR access_type = 0;
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        access_type = rec->ExceptionInformation[0];
        ctx->report.fault_address = static_cast<uintptr_t>(rec->ExceptionInformation[1]);
    } else {
        ctx->report.fault_address = ctx->report.fault_ip;
    }

    ctx->report.crash_reason = map_exception_code(rec->ExceptionCode, access_type);

    resolve_address_module(ctx->report.fault_ip, ctx->config,
                           ctx->report.module_name, ctx->report.module_offset);

    // Capture registers
    if (crec) {
        ctx->report.registers.rip = crec->Rip;
        ctx->report.registers.rsp = crec->Rsp;
        ctx->report.registers.rbp = crec->Rbp;
        ctx->report.registers.rax = crec->Rax;
        ctx->report.registers.rbx = crec->Rbx;
        ctx->report.registers.rcx = crec->Rcx;
        ctx->report.registers.rdx = crec->Rdx;
        ctx->report.registers.rsi = crec->Rsi;
        ctx->report.registers.rdi = crec->Rdi;
        ctx->report.registers.r8 = crec->R8;
        ctx->report.registers.r9 = crec->R9;
        ctx->report.registers.r10 = crec->R10;
        ctx->report.registers.r11 = crec->R11;
        ctx->report.registers.r12 = crec->R12;
        ctx->report.registers.r13 = crec->R13;
        ctx->report.registers.r14 = crec->R14;
        ctx->report.registers.r15 = crec->R15;
        ctx->report.registers.eflags = crec->EFlags;
    }

    std::ostringstream ss;
    ss << crash_reason_name(ctx->report.crash_reason) << " at 0x"
       << std::hex << ctx->report.fault_ip << " (" << ctx->report.module_name
       << "+0x" << ctx->report.module_offset << ")";
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ss << ", target 0x" << ctx->report.fault_address;
    }
    ctx->report.error_message = ss.str();

    // Clean exit of the guest thread without crashing host process
    ExitThread(rec->ExceptionCode);
}

DWORD WINAPI guest_worker_thread(LPVOID param) {
    auto* ctx = static_cast<RunnerThreadContext*>(param);
    ctx->thread_id = GetCurrentThreadId();
    g_active_context = ctx;

    const auto start = std::chrono::steady_clock::now();

    using GuestSysVFn = uint64_t(GRAVITY_SYSV_ABI*)(
        uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t
    );

    auto fn = reinterpret_cast<GuestSysVFn>(ctx->entry_point);
    const uint64_t ret = fn(
        ctx->args[0], ctx->args[1], ctx->args[2],
        ctx->args[3], ctx->args[4], ctx->args[5]
    );

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start
    );

    ctx->report.succeeded = true;
    ctx->report.return_value = ret;
    ctx->report.execution_time = elapsed;
    ctx->completed.store(true);
    g_active_context = nullptr;
    return 0;
}

} // anonymous namespace

const char* crash_reason_name(CrashReason reason) noexcept {
    switch (reason) {
    case CrashReason::none: return "No Crash";
    case CrashReason::access_violation_read: return "Access Violation (Read)";
    case CrashReason::access_violation_write: return "Access Violation (Write)";
    case CrashReason::access_violation_execute: return "Access Violation (Execute)";
    case CrashReason::illegal_instruction: return "Illegal Instruction";
    case CrashReason::privileged_instruction: return "Privileged Instruction";
    case CrashReason::divide_by_zero: return "Divide by Zero";
    case CrashReason::stack_overflow: return "Stack Overflow";
    case CrashReason::breakpoint: return "Breakpoint Trap";
    case CrashReason::timeout: return "Timeout";
    case CrashReason::unknown: return "Unknown Exception";
    }
    return "Unknown";
}

ExecutionReport execute_isolated_sysv(
    std::uintptr_t entry_point,
    std::uint64_t arg0,
    std::uint64_t arg1,
    std::uint64_t arg2,
    std::uint64_t arg3,
    std::uint64_t arg4,
    std::uint64_t arg5,
    const RunnerConfig& config)
{
    RunnerThreadContext ctx;
    ctx.entry_point = entry_point;
    ctx.args[0] = arg0;
    ctx.args[1] = arg1;
    ctx.args[2] = arg2;
    ctx.args[3] = arg3;
    ctx.args[4] = arg4;
    ctx.args[5] = arg5;
    ctx.config = config;

    {
        std::lock_guard<std::mutex> lock(g_veh_mutex);
        if (g_active_runners.fetch_add(1) == 0) {
            g_veh_handle = AddVectoredExceptionHandler(1, runner_vectored_exception_handler);
        }
    }

    const auto start = std::chrono::steady_clock::now();

    HANDLE h_thread = CreateThread(nullptr, 0, guest_worker_thread, &ctx, 0, nullptr);
    if (!h_thread) {
        ctx.report.succeeded = false;
        ctx.report.crash_reason = CrashReason::unknown;
        ctx.report.error_message = "Failed to spawn isolated worker thread (Win32 " + std::to_string(GetLastError()) + ")";
        return ctx.report;
    }

    const DWORD wait_res = WaitForSingleObject(h_thread, config.timeout_ms);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start
    );

    if (wait_res == WAIT_TIMEOUT) {
        TerminateThread(h_thread, static_cast<DWORD>(CrashReason::timeout));
        CloseHandle(h_thread);
        ctx.report.succeeded = false;
        ctx.report.crash_reason = CrashReason::timeout;
        ctx.report.execution_time = elapsed;
        ctx.report.error_message = "Execution timed out after " + std::to_string(config.timeout_ms) + " ms";
    } else {
        CloseHandle(h_thread);
        ctx.report.execution_time = elapsed;
    }

    {
        std::lock_guard<std::mutex> lock(g_veh_mutex);
        if (g_active_runners.fetch_sub(1) == 1 && g_veh_handle != nullptr) {
            RemoveVectoredExceptionHandler(g_veh_handle);
            g_veh_handle = nullptr;
        }
    }

    return ctx.report;
}

std::string format_report(const ExecutionReport& report) {
    std::ostringstream ss;
    ss << "=== Execution Report ===\n";
    ss << "Status: " << (report.succeeded ? "SUCCESS" : "FAILED") << "\n";
    ss << "Execution Time: " << report.execution_time.count() << " us\n";
    if (report.succeeded) {
        ss << "Return Value: 0x" << std::hex << report.return_value
           << " (" << std::dec << report.return_value << ")\n";
    } else {
        ss << "Crash Reason: " << crash_reason_name(report.crash_reason) << "\n";
        ss << "Exception Code: 0x" << std::hex << report.native_exception_code << std::dec << "\n";
        ss << "Thread ID: " << report.thread_id << "\n";
        ss << "Fault IP: 0x" << std::hex << report.fault_ip << " ("
           << report.module_name << "+0x" << report.module_offset << ")\n";
        if (report.crash_reason == CrashReason::access_violation_read ||
            report.crash_reason == CrashReason::access_violation_write ||
            report.crash_reason == CrashReason::access_violation_execute) {
            ss << "Target Address: 0x" << std::hex << report.fault_address << std::dec << "\n";
        }
        ss << "Message: " << report.error_message << "\n";
        ss << "\nRegisters:\n";
        ss << "  RIP: 0x" << std::hex << std::setw(16) << std::setfill('0') << report.registers.rip
           << "  RSP: 0x" << std::setw(16) << report.registers.rsp << "\n";
        ss << "  RAX: 0x" << std::setw(16) << report.registers.rax
           << "  RBX: 0x" << std::setw(16) << report.registers.rbx << "\n";
        ss << "  RCX: 0x" << std::setw(16) << report.registers.rcx
           << "  RDX: 0x" << std::setw(16) << report.registers.rdx << "\n";
        ss << "  RSI: 0x" << std::setw(16) << report.registers.rsi
           << "  RDI: 0x" << std::setw(16) << report.registers.rdi << "\n";
        ss << "  RBP: 0x" << std::setw(16) << report.registers.rbp
           << "  R8 : 0x" << std::setw(16) << report.registers.r8 << "\n";
        ss << "  R9 : 0x" << std::setw(16) << report.registers.r9
           << "  R10: 0x" << std::setw(16) << report.registers.r10 << "\n";
        ss << "  R11: 0x" << std::setw(16) << report.registers.r11
           << "  R12: 0x" << std::setw(16) << report.registers.r12 << "\n";
        ss << "  R13: 0x" << std::setw(16) << report.registers.r13
           << "  R14: 0x" << std::setw(16) << report.registers.r14 << "\n";
        ss << "  R15: 0x" << std::setw(16) << report.registers.r15
           << "  EFL: 0x" << std::hex << report.registers.eflags << std::dec << "\n";
    }
    return ss.str();
}

} // namespace gravity::hle::runner
