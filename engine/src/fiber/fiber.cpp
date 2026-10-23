#include "hle/fiber.h"
#include <windows.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <new>
#include <mutex>

namespace gravity::hle {

// -------------------------------------------------------------------------
// FiberRegistry implementation (External thread-safe map)
// -------------------------------------------------------------------------

FiberRegistry& FiberRegistry::instance() noexcept {
    static FiberRegistry instance;
    return instance;
}

void FiberRegistry::register_fiber(const orbis::SceFiber* guest, std::unique_ptr<Fiber> host) noexcept {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_[guest] = std::move(host);
}

Fiber* FiberRegistry::get(const orbis::SceFiber* guest) const noexcept {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(guest);
    if (it != map_.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::unique_ptr<Fiber> FiberRegistry::unregister_fiber(const orbis::SceFiber* guest) noexcept {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = map_.find(guest);
    if (it != map_.end()) {
        auto host = std::move(it->second);
        map_.erase(it);
        return host;
    }
    return nullptr;
}

std::size_t FiberRegistry::count() const noexcept {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return map_.size();
}

void FiberRegistry::clear() noexcept {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    map_.clear();
}

// -------------------------------------------------------------------------
// ThreadTls implementation (Per-thread guest TLS)
// -------------------------------------------------------------------------

static DWORD g_thread_tls_slot = TLS_OUT_OF_INDEXES;
static std::once_flag g_thread_tls_slot_once;

void ThreadTls::initialize() noexcept {
    std::call_once(g_thread_tls_slot_once, []() {
        if (g_thread_tls_slot == TLS_OUT_OF_INDEXES) {
            g_thread_tls_slot = TlsAlloc();
        }
    });
}

void ThreadTls::set_current_thread_tls(void* tls_ptr) noexcept {
    initialize();
    if (g_thread_tls_slot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(g_thread_tls_slot, tls_ptr);
    }
}

void* ThreadTls::get_current_thread_tls() noexcept {
    initialize();
    if (g_thread_tls_slot != TLS_OUT_OF_INDEXES) {
        return TlsGetValue(g_thread_tls_slot);
    }
    return nullptr;
}

DWORD ThreadTls::get_thread_tls_slot() noexcept {
    initialize();
    return g_thread_tls_slot;
}

// -------------------------------------------------------------------------
// Fiber TLS template and slot
// -------------------------------------------------------------------------

static std::mutex g_tls_template_mutex;
static std::vector<uint8_t> g_tls_template;
static uint64_t g_tls_total_size = 0;
static DWORD g_tls_slot = TLS_OUT_OF_INDEXES;
static std::once_flag g_tls_slot_once;

static thread_local FiberScheduler* s_current_scheduler = nullptr;

extern "C" GRAVITY_SYSV_ABI void
sceFiberSetTlsTemplate(const uint8_t* data, uint64_t template_size, uint64_t total_size) {
    sceFiberInitTlsSlot();
    std::lock_guard<std::mutex> lock(g_tls_template_mutex);
    if (data && template_size > 0) {
        g_tls_template.assign(data, data + template_size);
    } else {
        g_tls_template.clear();
    }
    g_tls_total_size = total_size;
}

extern "C" GRAVITY_SYSV_ABI void
sceFiberInitTlsSlot() {
    std::call_once(g_tls_slot_once, []() {
        if (g_tls_slot == TLS_OUT_OF_INDEXES) {
            g_tls_slot = TlsAlloc();
        }
    });
}

extern "C" GRAVITY_SYSV_ABI DWORD
sceFiberGetTlsSlot() {
    sceFiberInitTlsSlot();
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

    DWORD slot = sceFiberGetTlsSlot();
    if (slot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(slot, target.tls_block_ptr());
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

    target.incoming_ = argument;
    target.state_ = FiberState::running;
    current_ = &target;

    DWORD slot = sceFiberGetTlsSlot();
    if (slot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(slot, target.tls_block_ptr());
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

    return_value_ = argument;
    current_ = nullptr;

    DWORD slot = sceFiberGetTlsSlot();
    if (slot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(slot, nullptr);
    }

    SwitchToFiber(root_fiber_);

    resumed = fib->incoming_;
    return {};
}

// -------------------------------------------------------------------------
// Fiber implementation
// -------------------------------------------------------------------------

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

    DWORD slot = sceFiberGetTlsSlot();
    if (slot != TLS_OUT_OF_INDEXES) {
        TlsSetValue(slot, nullptr);
    }
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

    {
        std::lock_guard<std::mutex> lock(g_tls_template_mutex);
        if (g_tls_total_size > 0) {
            tls_block.resize(static_cast<std::size_t>(g_tls_total_size), 0);
            if (!g_tls_template.empty()) {
                std::memcpy(tls_block.data(), g_tls_template.data(),
                            std::min<std::size_t>(g_tls_template.size(), tls_block.size()));
            }
        } else {
            tls_block.clear();
        }
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
    sceFiberInitTlsSlot();
    if (!fiber || !entry) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    // Prevent double initialization
    if (fiber->magic_start == orbis::ORBIS_FIBER_MAGIC_START &&
        fiber->magic_end == orbis::ORBIS_FIBER_MAGIC_END) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }

    auto* scheduler = FiberScheduler::for_thread();
    if (!scheduler) {
        return static_cast<int32_t>(orbis::Error::FIBER_ENORES);
    }

    auto host = std::make_unique<Fiber>();
    host->guest_stack_base = stack_base;
    host->guest_stack_size = stack_size;

    auto status = host->initialize(*scheduler, entry, arg);
    if (!status) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }

    fiber->magic_start = orbis::ORBIS_FIBER_MAGIC_START;
    fiber->state = static_cast<uint32_t>(orbis::FiberState::ready);
    fiber->entry = reinterpret_cast<void*>(entry);
    fiber->arg_on_initialize = arg;
    fiber->addr_context = stack_base;
    fiber->size_context = stack_size;
    if (name) {
        std::strncpy(fiber->name, name, sizeof(fiber->name) - 1);
        fiber->name[sizeof(fiber->name) - 1] = '\0';
    } else {
        fiber->name[0] = '\0';
    }
    fiber->context = nullptr;
    fiber->flags = optParam ? optParam->attr : 0;
    fiber->reserved = 0;
    fiber->context_start = stack_base;
    fiber->context_end = static_cast<char*>(stack_base) + stack_size;
    fiber->magic_end = orbis::ORBIS_FIBER_MAGIC_END;
    fiber->padding_end = 0;

    FiberRegistry::instance().register_fiber(fiber, std::move(host));
    return static_cast<int32_t>(orbis::Error::OK);
}

extern "C" GRAVITY_SYSV_ABI int32_t sceFiberRun(
    orbis::SceFiber* fiber,
    uint64_t arg,
    uint64_t* pReturnedArg
) {
    if (!fiber || fiber->magic_start != orbis::ORBIS_FIBER_MAGIC_START ||
        fiber->magic_end != orbis::ORBIS_FIBER_MAGIC_END) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto* host = FiberRegistry::instance().get(fiber);
    if (!host) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto* scheduler = FiberScheduler::for_thread();
    if (!scheduler) {
        return static_cast<int32_t>(orbis::Error::FIBER_ENORES);
    }

    fiber->state = static_cast<uint32_t>(orbis::FiberState::running);
    uint64_t returned = 0;
    auto status = scheduler->run(*host, arg, returned);
    if (!status) {
        fiber->state = static_cast<uint32_t>(orbis::FiberState::suspended);
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    if (host->state() == FiberState::completed) {
        fiber->state = static_cast<uint32_t>(orbis::FiberState::completed);
    } else {
        fiber->state = static_cast<uint32_t>(orbis::FiberState::suspended);
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
    if (!to_fiber || to_fiber->magic_start != orbis::ORBIS_FIBER_MAGIC_START ||
        to_fiber->magic_end != orbis::ORBIS_FIBER_MAGIC_END) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto* host = FiberRegistry::instance().get(to_fiber);
    if (!host) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto* scheduler = FiberScheduler::for_thread();
    if (!scheduler) {
        return static_cast<int32_t>(orbis::Error::FIBER_ENORES);
    }

    to_fiber->state = static_cast<uint32_t>(orbis::FiberState::running);
    uint64_t resumed = 0;
    auto status = scheduler->switch_to(*host, run_arg, resumed);
    if (!status) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    if (host->state() == FiberState::completed) {
        to_fiber->state = static_cast<uint32_t>(orbis::FiberState::completed);
    } else {
        to_fiber->state = static_cast<uint32_t>(orbis::FiberState::suspended);
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
    if (!fiber || fiber->magic_start != orbis::ORBIS_FIBER_MAGIC_START ||
        fiber->magic_end != orbis::ORBIS_FIBER_MAGIC_END) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto host = FiberRegistry::instance().unregister_fiber(fiber);
    if (!host) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    auto status = host->finalize();
    fiber->magic_start = 0;
    fiber->magic_end = 0;
    fiber->state = static_cast<uint32_t>(orbis::FiberState::empty);
    if (!status) {
        return static_cast<int32_t>(orbis::Error::FIBER_EINVAL);
    }
    return static_cast<int32_t>(orbis::Error::OK);
}

} // namespace gravity::hle