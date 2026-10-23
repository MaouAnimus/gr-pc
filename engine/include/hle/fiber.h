#pragma once

#include "hle/status.h"
#include "hle/abi.h"
#include "hle/orbis_error.h"
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <exception>
#include <vector>

namespace gravity::hle {

class FiberScheduler;
using FiberEntry = void (GRAVITY_SYSV_ABI *)(std::uint64_t initialize_arg, std::uint64_t run_arg);
enum class FiberState { empty, ready, running, suspended, completed };

struct FiberOptions {
    std::size_t reserve_size{1024 * 1024};
    std::size_t commit_size{64 * 1024};
    void* supplied_stack{nullptr};
    std::size_t supplied_stack_size{0};
};

class Fiber {
public:
    Fiber() = default;
    ~Fiber();
    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;

    Status initialize(FiberScheduler& scheduler, FiberEntry entry,
                      std::uint64_t initialize_arg, FiberOptions options = {}) noexcept;
    Status finalize() noexcept;

    [[nodiscard]] FiberState state() const noexcept { return state_; }
    [[nodiscard]] std::exception_ptr exception() const noexcept { return exception_; }

    // Guest stack state (Orbis OS guest stack) & TLS block
    void* guest_stack_base{nullptr};
    std::size_t guest_stack_size{0};
    std::vector<std::uint8_t> guest_stack_shadow{};
    std::vector<std::uint8_t> tls_block{};

    void syncGuestToInternal() noexcept;
    void syncInternalToGuest() noexcept;

    [[nodiscard]] void* tls_block_ptr() const noexcept {
        return tls_block.empty() ? nullptr : const_cast<std::uint8_t*>(tls_block.data());
    }
    [[nodiscard]] std::size_t tls_size() const noexcept {
        return tls_block.size();
    }

private:
    friend class FiberScheduler;
    static void WINAPI trampoline(void* parameter) noexcept;

    FiberScheduler* scheduler_{nullptr};
    void* handle_{nullptr};
    FiberEntry entry_{nullptr};
    std::uint64_t initialize_arg_{0};
    std::uint64_t incoming_{0};
    FiberState state_{FiberState::empty};
    std::exception_ptr exception_{};
};

class FiberScheduler {
public:
    FiberScheduler() = default;
    ~FiberScheduler();
    FiberScheduler(const FiberScheduler&) = delete;
    FiberScheduler& operator=(const FiberScheduler&) = delete;

    Status attach() noexcept;
    Status detach() noexcept;

    Status run(Fiber& target, std::uint64_t argument, std::uint64_t& returned) noexcept;
    Status switch_to(Fiber& target, std::uint64_t argument, std::uint64_t& resumed) noexcept;
    Status return_to_thread(std::uint64_t argument, std::uint64_t& resumed) noexcept;

    [[nodiscard]] Fiber* current() const noexcept { return current_; }
    [[nodiscard]] Status check_thread() const noexcept;
    [[nodiscard]] bool attached() const noexcept { return attached_; }

    static FiberScheduler* for_thread() noexcept;

private:
    friend class Fiber;

    void* root_fiber_{nullptr};
    DWORD thread_id_{0};
    Fiber* current_{nullptr};
    std::uint64_t return_value_{0};
    std::size_t live_fibers_{0};
    bool attached_{false};
    bool owns_conversion_{false};
};

// C ABI / Orbis HLE functions
extern "C" {
GRAVITY_SYSV_ABI void sceFiberSetTlsTemplate(const uint8_t* data, uint64_t template_size, uint64_t total_size);
GRAVITY_SYSV_ABI void sceFiberInitTlsSlot();
GRAVITY_SYSV_ABI DWORD sceFiberGetTlsSlot();

GRAVITY_SYSV_ABI int32_t sceFiberInitialize(
    orbis::SceFiber* fiber,
    const char* name,
    FiberEntry entry,
    uint64_t arg,
    void* stack_base,
    std::size_t stack_size,
    const orbis::SceFiberOptParam* optParam
);

GRAVITY_SYSV_ABI int32_t sceFiberRun(
    orbis::SceFiber* fiber,
    uint64_t arg,
    uint64_t* pReturnedArg
);

GRAVITY_SYSV_ABI int32_t sceFiberSwitch(
    orbis::SceFiber* to_fiber,
    uint64_t run_arg,
    uint64_t* out_arg
);

GRAVITY_SYSV_ABI int32_t sceFiberReturnToThread(
    uint64_t run_arg,
    uint64_t* out_arg
);

GRAVITY_SYSV_ABI int32_t sceFiberReturn();

GRAVITY_SYSV_ABI int32_t sceFiberFinalize(
    orbis::SceFiber* fiber
);
}

} // namespace gravity::hle