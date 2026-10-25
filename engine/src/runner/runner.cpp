#include "hle/runner/runner.h"
#include "hle/guest_memory.h"
#include "hle/kernel/pthread.h"
#include "hle/kernel/sema.h"
#include <Windows.h>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstdlib>

namespace gravity::hle::runner {

namespace {

struct RunnerThreadContext {
    DWORD thread_id{0};
    uintptr_t entry_point{0};
    uint64_t args[6]{0, 0, 0, 0, 0, 0};
    RunnerConfig config;
    NativeTls tls;
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
} // namespace

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

namespace {

LONG WINAPI runner_vectored_exception_handler(PEXCEPTION_POINTERS p_info) {
    auto* ctx = g_active_context;
    if (!ctx || ctx->thread_id != GetCurrentThreadId() || ctx->completed.load()) {
        const auto code = p_info->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_ILLEGAL_INSTRUCTION)
            std::fprintf(stderr, "[Runner] Unmonitored exception code=0x%lx IP=%p thread=%lu\n",
                static_cast<unsigned long>(code), p_info->ExceptionRecord->ExceptionAddress,
                static_cast<unsigned long>(GetCurrentThreadId()));
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
    std::fprintf(stderr, "[Runner] Guest exception code=0x%lx IP=%p target=0x%llx\n",
        static_cast<unsigned long>(rec->ExceptionCode), rec->ExceptionAddress,
        static_cast<unsigned long long>(ctx->report.fault_address));

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

        std::fprintf(stderr, "[Runner] Stack dump (RSP=%p):\n", reinterpret_cast<void*>(crec->Rsp));
        const auto* sp = reinterpret_cast<const std::uintptr_t*>(crec->Rsp);
        for (std::size_t i = 0; i < 64; ++i) {
            if (guest_memory::accessible(sp + i, sizeof(std::uintptr_t))) {
                std::uintptr_t val = sp[i];
                std::string mod;
                std::uintptr_t off = 0;
                resolve_address_module(val, ctx->config, mod, off);
                std::fprintf(stderr, "  [+0x%02zx] 0x%016llx (%s+0x%llx)\n",
                             i * sizeof(std::uintptr_t),
                             static_cast<unsigned long long>(val),
                             mod.c_str(),
                             static_cast<unsigned long long>(off));
            }
        }
    }

    std::ostringstream ss;
    ss << crash_reason_name(ctx->report.crash_reason) << " at 0x"
       << std::hex << ctx->report.fault_ip << " (" << ctx->report.module_name
       << "+0x" << ctx->report.module_offset << ")";
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ss << ", target 0x" << ctx->report.fault_address;
    }
    ctx->report.error_message = ss.str();
    if (ctx->config.terminate_process_on_exception) {
        std::fprintf(stderr, "[Guest worker failure]\n%s", format_report(ctx->report).c_str());
        std::_Exit(5);
    }

    if (ctx->config.tls.memory_size) (void)ctx->tls.detach();

    // Clean exit of the guest thread without crashing host process
    ExitThread(rec->ExceptionCode);
}

DWORD WINAPI guest_worker_thread(LPVOID param) {
    auto* ctx = static_cast<RunnerThreadContext*>(param);
    ctx->thread_id = GetCurrentThreadId();
    g_active_context = ctx;
    // ELF code does not emit Windows __chkstk probes. Commit the reserved
    // native stack before entering it, retaining a bottom guard page. Otherwise
    // a large guest frame can skip the guard and leave no stack for the VEH.
    MEMORY_BASIC_INFORMATION stack_region{};
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    const auto page = static_cast<std::uintptr_t>(system.dwPageSize);
    if (!VirtualQuery(&stack_region, &stack_region, sizeof(stack_region))) {
        ctx->report.error_message = "Cannot query native guest stack";
        g_active_context = nullptr;
        return 1;
    }
    const auto low = reinterpret_cast<std::uintptr_t>(stack_region.AllocationBase) + page;
    const auto high = reinterpret_cast<std::uintptr_t>(tib->StackBase);
    DWORD previous{};
    if (high <= low + page || high - low > 64 * 1024 * 1024 ||
        !VirtualAlloc(reinterpret_cast<void*>(low), high - low, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualProtect(reinterpret_cast<void*>(low), high - low, PAGE_READWRITE, &previous) ||
        !VirtualProtect(reinterpret_cast<void*>(low), page, PAGE_READWRITE | PAGE_GUARD, &previous)) {
        ctx->report.error_message = "Cannot commit native guest stack";
        g_active_context = nullptr;
        return 1;
    }
    tib->StackLimit = reinterpret_cast<void*>(low + page);
    ULONG exception_stack = 32768;
    (void)SetThreadStackGuarantee(&exception_stack);
    if (ctx->config.tls.memory_size) {
        const auto status = ctx->tls.attach();
        if (!status) {
            ctx->report.error_message = "Cannot attach native guest TLS";
            g_active_context = nullptr;
            return 1;
        }
    }

    const auto start = std::chrono::steady_clock::now();

    using GuestSysVFn = uint64_t(GRAVITY_SYSV_ABI*)(
        uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t
    );

    auto fn = reinterpret_cast<GuestSysVFn>(ctx->entry_point);
    const uint64_t ret = fn(
        ctx->args[0], ctx->args[1], ctx->args[2],
        ctx->args[3], ctx->args[4], ctx->args[5]
    );
    if (ctx->config.tls.memory_size) (void)ctx->tls.detach();

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
    if (config.tls.memory_size) {
        const auto status = ctx.tls.initialize(config.tls);
        if (!status) {
            ctx.report.error_message = "Cannot initialize native guest TLS: " + std::string(error_name(status.error));
            return ctx.report;
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_veh_mutex);
        if (g_active_runners.fetch_add(1) == 0) {
            g_veh_handle = AddVectoredExceptionHandler(1, runner_vectored_exception_handler);
        }
    }

    const auto start = std::chrono::steady_clock::now();

    HANDLE h_thread = CreateThread(nullptr, 0, guest_worker_thread, &ctx, 0, &ctx.thread_id);
    if (!h_thread) {
        ctx.report.succeeded = false;
        ctx.report.crash_reason = CrashReason::unknown;
        ctx.report.error_message = "Failed to spawn isolated worker thread (Win32 " + std::to_string(GetLastError()) + ")";
        std::lock_guard lock(g_veh_mutex);
        if (g_active_runners.fetch_sub(1) == 1 && g_veh_handle) {
            RemoveVectoredExceptionHandler(g_veh_handle);
            g_veh_handle = nullptr;
        }
        return ctx.report;
    }

    const DWORD wait_time = (config.timeout_ms == 0) ? INFINITE : config.timeout_ms;
    const DWORD wait_res = WaitForSingleObject(h_thread, wait_time);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start
    );

    if (wait_res == WAIT_TIMEOUT && config.timeout_ms > 0) {
        if (config.terminate_process_on_timeout) {
            CONTEXT thread_ctx{};
            thread_ctx.ContextFlags = CONTEXT_ALL;
            SuspendThread(h_thread);
            if (GetThreadContext(h_thread, &thread_ctx)) {
                std::fprintf(stderr, "[Timeout Main Thread] TID=0x%lx RIP=0x%llx (guest+0x%llx) RSP=0x%llx RAX=0x%llx RBX=0x%llx RCX=0x%llx RDX=0x%llx RSI=0x%llx RDI=0x%llx\n",
                             static_cast<unsigned long>(ctx.thread_id),
                             static_cast<unsigned long long>(thread_ctx.Rip),
                             static_cast<unsigned long long>(thread_ctx.Rip >= 0x1000000000ULL ? thread_ctx.Rip - 0x1000000000ULL : 0),
                             static_cast<unsigned long long>(thread_ctx.Rsp),
                             static_cast<unsigned long long>(thread_ctx.Rax),
                             static_cast<unsigned long long>(thread_ctx.Rbx),
                             static_cast<unsigned long long>(thread_ctx.Rcx),
                             static_cast<unsigned long long>(thread_ctx.Rdx),
                             static_cast<unsigned long long>(thread_ctx.Rsi),
                             static_cast<unsigned long long>(thread_ctx.Rdi));
                for (int i = 0; i < 64; ++i) {
                    uintptr_t stack_val = 0;
                    if (guest_memory::read(reinterpret_cast<const void*>(thread_ctx.Rsp + i * 8), &stack_val, sizeof(stack_val))) {
                        if (!stack_val) continue;
                        std::string mod;
                        uintptr_t off = 0;
                        resolve_address_module(stack_val, config, mod, off);
                        if (mod == "unknown_module") continue;
                        std::fprintf(stderr, "  [Stack+%03d] 0x%llx (%s+0x%llx)\n", i * 8,
                                     static_cast<unsigned long long>(stack_val),
                                     mod.c_str(),
                                     static_cast<unsigned long long>(off));
                    }
                }
            }
            gravity::hle::pthread::dump_threads();
            gravity::hle::sema::dump_semaphores();
            ExecutionReport timeout_report;
            timeout_report.crash_reason = CrashReason::timeout;
            timeout_report.execution_time = elapsed;
            timeout_report.error_message = "Boot process deadline reached; terminating process without unsafe thread teardown";
            std::fprintf(stderr, "[Boot timeout]\n%s", format_report(timeout_report).c_str());
            std::_Exit(4);
        }
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

ExecutionReport execute_current_sysv(std::uintptr_t entry, std::uint64_t argument, const RunnerConfig& config) {
    RunnerThreadContext ctx;
    ctx.entry_point = entry;
    ctx.args[0] = argument;
    ctx.config = config;
    if (config.tls.memory_size) {
        const auto status = ctx.tls.initialize(config.tls);
        if (!status) { ctx.report.error_message = "Cannot initialize callback TLS"; return ctx.report; }
    }
    {
        std::lock_guard lock(g_veh_mutex);
        if (g_active_runners.fetch_add(1) == 0)
            g_veh_handle = AddVectoredExceptionHandler(1, runner_vectored_exception_handler);
    }
    guest_worker_thread(&ctx);
    {
        std::lock_guard lock(g_veh_mutex);
        if (g_active_runners.fetch_sub(1) == 1 && g_veh_handle) {
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
