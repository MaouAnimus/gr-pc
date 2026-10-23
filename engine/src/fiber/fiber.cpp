#include "hle/fiber.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <new>

namespace gravity::hle {

static uint8_t* g_tls_template = nullptr;
static uint64_t g_tls_template_size = 0;
static uint64_t g_tls_total_size = 0;
static DWORD g_tls_slot = TLS_OUT_OF_INDEXES;

static thread_local FiberScheduler* s_current_scheduler = nullptr;

extern "C" GRAVITY_SYSV_ABI void
sceFiberSetTlsTemplate(const uint8_t* data, uint64_t template_size, uint64_t total_size) {
    if (g_tls_template) {
        std::free(g_tls_template);
        g_tls_template = nullptr;
    }
    if (data && template_size > 0) {
        g_tls_template = static_cast<uint8_t*>(std::malloc(template_size));
        if (g_tls_template) {
            std::memcpy(g_tls_template, data, template_size);
            g_tls_template_size = template_size;
            g_tls_total_size = total_size;
        } else {
            g_tls_template_size = 0;
            g_tls_total_size = 0;
        }
    } else {
        g_tls_template_size = 0;
        g_tls_total_size = total_size;
    }
}

extern "C" GRAVITY_SYSV_ABI void
sceFiberInitTlsSlot() {
    if (g_tls_slot == TLS_OUT_OF_INDEXES) {
        g_tls_slot = TlsAlloc();
    }
}

extern "C" GRAVITY_SYSV_ABI DWORD
sceFiberGetTlsSlot() {
    return g_tls_slot;
}

// -------------------------------------------------------------------------
// FiberScheduler implementation
// -------------------------------------------------------------------------

FiberScheduler* FiberScheduler::for_thread() noexcept {
    return s_current_scheduler;
}

Status FiberScheduler::check_thread() const noexcept {
    if (thread_id_ != 0 && thread_id_ != GetCurrentThreadId()) {
        return {Error::wrong_thread};
    }
    return {};
}

Status FiberScheduler::attach() noexcept {
    if (attached_ || for_thread() != nullptr) {
        return {Error::invalid_state};
    }
    if (IsThreadAFiber()) {
        root_fiber_ = GetCurrentFiber();
        owns_conversion_ = false;
    } else {
        root_fiber_ = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
        if (!root_fiber_) {
            return {Error::system_error, static_cast<std::uint32_t>(GetLastError())};
        }
        owns_conversion_ = true;
    }
    thread_id_ = GetCurrentThreadId();
    attached_ = true;
    s_current_scheduler = this;
    return {};
}

Status FiberScheduler::detach() noexcept {
    if (!attached_) {
        return {Error::invalid_state};
    }
    const auto status = check_thread();
    if (!status) return status;
    if (live_fibers_ > 0 || current_ != nullptr) {
        return {Error::busy};
    }
    if (owns_conversion_) {
        if (!ConvertFiberToThread()) {
            return {Error::system_error, static_cast<std::uint32_t>(GetLastError())};
        }
        owns_conversion_ = false;
    }
    root_fiber_ = nullptr;
    thread_id_ = 0;
    attached_ = false;
    s_current_scheduler = nullptr;
    return {};
}

FiberScheduler::~FiberScheduler() {
    if (attached_) {
        if (live_fibers_ > 0 || current_ != nullptr) {
            std::terminate();
        }
        (void)detach();
    }
}

Status FiberScheduler::run(Fiber& target, std::uint64_t argument, std::uint64_t& returned) noexcept {
    if (!attached_) {
        return {Error::invalid_state};
    }
    const auto st = check_thread();
    if (!st) return st;
    if (current_ != nullptr) {
        return {Error::invalid_state};
    }
    if (target.state_ == FiberState::empty) {
        return {Error::invalid_argument};
    }
    if (target.state_ != FiberState::ready && target.state_ != FiberState::suspended) {
        return {Error::invalid_state};
    }
    if (target.scheduler_ != this) {
        return {Error::invalid_argument};
    }

    target.incoming_ = argument;
    target.state_ = FiberState::running;
    current_ = &target;

    target.syncGuestToInternal();
    if (g_tls_slot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(g_tls_slot, target.tls_block.empty() ? nullptr : target.tls_block.data());
    }

    SwitchToFiber(target.handle_);

    if (target.exception_) {
        return {Error::callback_exception};
    }
    returned = return_value_;
    return {};
}

Status FiberScheduler::switch_to(Fiber& target, std::uint64_t argument, std::uint64_t& resumed) noexcept {
    const auto st = check_thread();
    if (!st) return st;
    if (current_ == nullptr) {
        return {Error::invalid_state};
    }
    if (&target == current_) {
        return {Error::invalid_state};
    }
    if (target.state_ != FiberState::ready && target.state_ != FiberState::suspended) {
        return {Error::invalid_state};
    }
    if (target.scheduler_ != this) {
        return {Error::invalid_argument};
    }

    Fiber* prev = current_;
    prev->state_ = FiberState::suspended;
    prev->syncInternalToGuest();

    target.incoming_ = argument;
    target.state_ = FiberState::running;
    current_ = &target;

    target.syncGuestToInternal();
    if (g_tls_slot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(g_tls_slot, target.tls_block.empty() ? nullptr : target.tls_block.data());
    }

    SwitchToFiber(target.handle_);

    resumed = prev->incoming_;
    return {};
}

Status FiberScheduler::return_to_thread(std::uint64_t argument, std::uint64_t& resumed) noexcept {
    const auto st = check_thread();
    if (!st) return st;
    if (current_ == nullptr) {
        return {Error::invalid_state};
    }

    Fiber* fib = current_;
    fib->state_ = FiberState::suspended;
    fib->syncInternalToGuest();

    return_value_ = argument;
    current_ = nullptr;

    SwitchToFiber(root_fiber_);

    resumed = fib->incoming_;
    return {};
}

// -------------------------------------------------------------------------
// Fiber implementation
// -------------------------------------------------------------------------

void Fiber::syncGuestToInternal() noexcept {
    if (guest_stack_base && guest_stack_size > 0) {
        if (guest_stack_shadow.size() < guest_stack_size) {
            guest_stack_shadow.resize(guest_stack_size);
        }
        std::memcpy(guest_stack_shadow.data(), guest_stack_base, guest_stack_size);
    }
}

void Fiber::syncInternalToGuest() noexcept {
    if (guest_stack_base && guest_stack_size > 0 && !guest_stack_shadow.empty()) {
        std::memcpy(guest_stack_base, guest_stack_shadow.data(), std::min(guest_stack_size, guest_stack_shadow.size()));
    }
}

void WINAPI Fiber::trampoline(void* parameter) noexcept {
    auto* fiber = static_cast<Fiber*>(parameter);
    auto* scheduler = fiber->scheduler_;
    const auto initial = fiber->initialize_arg_;
    const auto entry = fiber->entry_;
    const auto incoming = fiber->incoming_;

    try {
        entry(initial, incoming);
        fiber->state_ = FiberState::completed;
        scheduler->return_value_ = 0;
    } catch (...) {
        fiber->exception_ = std::current_exception();
        fiber->state_ = FiberState::completed;
        scheduler->return_value_ = 0;
    }

    fiber->syncInternalToGuest();
    scheduler->current_ = nullptr;
    SwitchToFiber(scheduler->root_fiber_);
}

Fiber::~Fiber() {
    if (state_ == FiberState::running || state_ == FiberState::suspended) {
        std::terminate();
    }
    if (handle_) {
        if (!finalize()) {
            std::terminate();
        }
    }
}

Status Fiber::initialize(FiberScheduler& scheduler, FiberEntry entry,
                         std::uint64_t initialize_arg, FiberOptions options) noexcept {
    if (handle_) return {Error::invalid_state};
    const auto status = scheduler.check_thread();
    if (!status) return status;
    if (!entry || !options.reserve_size || !options.commit_size ||
        options.commit_size > options.reserve_size) return {Error::invalid_argument};
    if (options.supplied_stack) return {Error::unsupported};

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    if (options.reserve_size < info.dwAllocationGranularity ||
        options.reserve_size % info.dwAllocationGranularity ||
        options.commit_size % info.dwPageSize) return {Error::invalid_argument};

    handle_ = CreateFiberEx(options.commit_size, options.reserve_size,
                            FIBER_FLAG_FLOAT_SWITCH, &Fiber::trampoline, this);
    if (!handle_) return {Error::system_error, static_cast<std::uint32_t>(GetLastError())};

    scheduler_ = &scheduler;
    entry_ = entry;
    initialize_arg_ = initialize_arg;
    incoming_ = 0;
    exception_ = nullptr;
    state_ = FiberState::ready;
    ++scheduler_->live_fibers_;

    if (g_tls_template && g_tls_total_size > 0) {
        tls_block.resize(static_cast<std::size_t>(g_tls_total_size), 0);
        if (g_tls_template_size > 0) {
            std::memcpy(tls_block.data(), g_tls_template,
                        std::min<std::size_t>(static_cast<std::size_t>(g_tls_template_size),
                                              static_cast<std::size_t>(g_tls_total_size)));
        }
    } else {
        tls_block.clear();
    }

    if (guest_stack_base && guest_stack_size > 0) {
        guest_stack_shadow.resize(guest_stack_size, 0);
    } else {
        guest_stack_shadow.clear();
    }

    return {};
}

Status Fiber::finalize() noexcept {
    if (!handle_) return {};
    if (scheduler_) {
        const auto status = scheduler_->check_thread();
        if (!status) return status;
    }
    if (state_ == FiberState::running || state_ == FiberState::suspended) {
        return {Error::busy};
    }
    DeleteFiber(handle_);
    if (scheduler_) {
        --scheduler_->live_fibers_;
        scheduler_ = nullptr;
    }
    handle_ = nullptr;
    entry_ = nullptr;
    state_ = FiberState::empty;
    exception_ = nullptr;
    guest_stack_base = nullptr;
    guest_stack_size = 0;
    guest_stack_shadow.clear();
    tls_block.clear();
    return {};
}

// -------------------------------------------------------------------------
// Orbis HLE C ABI Wrappers
// -------------------------------------------------------------------------

extern "C" GRAVITY_SYSV_ABI int32_t sceFiberInitialize(
    orbis::SceFiber* fiber,
    const char* name,
    FiberEntry entry,
    uint64_t arg,
    void* stack_base,
    std::size_t stack_size,
    const orbis::SceFiberOptParam* optParam
) {
    (void)optParam;
    if (!fiber || !entry) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto* scheduler = FiberScheduler::for_thread();
    if (!scheduler) {
        return static_cast<int32_t>(orbis::Error::FIBER_ENORES);
    }

    auto* host = new (std::nothrow) Fiber();
    if (!host) {
        return static_cast<int32_t>(orbis::Error::KERNEL_ENOMEM);
    }

    host->guest_stack_base = stack_base;
    host->guest_stack_size = stack_size;
    if (stack_base && stack_size > 0) {
        host->guest_stack_shadow.resize(stack_size, 0);
    }

    auto status = host->initialize(*scheduler, entry, arg);
    if (!status) {
        delete host;
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }

    fiber->attr = 0;
    fiber->entry = reinterpret_cast<void*>(entry);
    fiber->arg = arg;
    fiber->stack_base = stack_base;
    fiber->stack_size = stack_size;
    if (name) {
        std::strncpy(fiber->name, name, sizeof(fiber->name) - 1);
        fiber->name[sizeof(fiber->name) - 1] = '\0';
    } else {
        fiber->name[0] = '\0';
    }
    fiber->host_fiber = host;

    return static_cast<int32_t>(orbis::Error::OK);
}

extern "C" GRAVITY_SYSV_ABI int32_t sceFiberRun(
    orbis::SceFiber* fiber,
    uint64_t arg,
    uint64_t* pReturnedArg
) {
    if (!fiber || !fiber->host_fiber) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto* host = static_cast<Fiber*>(fiber->host_fiber);
    auto* scheduler = FiberScheduler::for_thread();
    if (!scheduler) {
        return static_cast<int32_t>(orbis::Error::FIBER_ENORES);
    }

    uint64_t returned = 0;
    auto status = scheduler->run(*host, arg, returned);
    if (!status) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    if (pReturnedArg) {
        *pReturnedArg = returned;
    }
    return static_cast<int32_t>(orbis::Error::OK);
}

extern "C" GRAVITY_SYSV_ABI int32_t sceFiberSwitch(
    orbis::SceFiber* to_fiber,
    uint64_t run_arg,
    uint64_t* out_arg
) {
    if (!to_fiber || !to_fiber->host_fiber) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto* host = static_cast<Fiber*>(to_fiber->host_fiber);
    auto* scheduler = FiberScheduler::for_thread();
    if (!scheduler) {
        return static_cast<int32_t>(orbis::Error::FIBER_ENORES);
    }

    uint64_t resumed = 0;
    auto status = scheduler->switch_to(*host, run_arg, resumed);
    if (!status) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    if (out_arg) {
        *out_arg = resumed;
    }
    return static_cast<int32_t>(orbis::Error::OK);
}

extern "C" GRAVITY_SYSV_ABI int32_t sceFiberReturnToThread(
    uint64_t run_arg,
    uint64_t* out_arg
) {
    auto* scheduler = FiberScheduler::for_thread();
    if (!scheduler) {
        return static_cast<int32_t>(orbis::Error::FIBER_ENORES);
    }

    uint64_t resumed = 0;
    auto status = scheduler->return_to_thread(run_arg, resumed);
    if (!status) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    if (out_arg) {
        *out_arg = resumed;
    }
    return static_cast<int32_t>(orbis::Error::OK);
}

extern "C" GRAVITY_SYSV_ABI int32_t sceFiberReturn() {
    return sceFiberReturnToThread(0, nullptr);
}

extern "C" GRAVITY_SYSV_ABI int32_t sceFiberFinalize(
    orbis::SceFiber* fiber
) {
    if (!fiber || !fiber->host_fiber) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto* host = static_cast<Fiber*>(fiber->host_fiber);
    auto status = host->finalize();
    delete host;
    fiber->host_fiber = nullptr;
    if (!status) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    return static_cast<int32_t>(orbis::Error::OK);
}

} // namespace gravity::hle