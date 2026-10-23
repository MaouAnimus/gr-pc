#pragma once

#include "hle/status.h"
#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace gravity::hle {
enum class MutexKind { error_check, recursive };

// In-place host storage. Not the layout of ScePthreadMutex.
class Mutex final {
public:
    explicit Mutex(MutexKind kind = MutexKind::error_check) noexcept : kind_(kind) {}
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Status lock() noexcept;
    Status try_lock() noexcept;
    Status unlock() noexcept;
private:
    Status relock() noexcept;
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::atomic<DWORD> owner_{};
    std::uint32_t depth_{};
    MutexKind kind_;
};

enum class WaitMode { all, any };
enum class ClearMode { none, matched, all };

// Bit-pattern conditions; a plain CreateEvent cannot implement this contract.
class EventFlag final {
public:
    explicit EventFlag(std::uint64_t initial = 0) noexcept : bits_(initial) {}
    EventFlag(const EventFlag&) = delete;
    EventFlag& operator=(const EventFlag&) = delete;
    Status set(std::uint64_t bits) noexcept;
    Status clear(std::uint64_t bits_to_clear) noexcept;
    Status wait(std::uint64_t requested, WaitMode mode, ClearMode clear,
                std::uint64_t& observed, DWORD timeout_ms = INFINITE) noexcept;
    void cancel() noexcept;
private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    CONDITION_VARIABLE changed_ = CONDITION_VARIABLE_INIT;
    std::uint64_t bits_{};
    bool canceled_{};
};

class Semaphore final {
public:
    Semaphore() = default;
    ~Semaphore();
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Status initialize(std::int32_t initial, std::int32_t maximum) noexcept;
    Status wait(DWORD timeout_ms = INFINITE) noexcept;
    Status post(std::int32_t count = 1, std::int32_t* previous = nullptr) noexcept;
private:
    HANDLE handle_{};
};

// Epoch is initialization of this clock, not CPU time and not wall-clock time.
class ProcessClock final {
public:
    ProcessClock() noexcept;
    [[nodiscard]] bool valid() const noexcept { return frequency_ > 0; }
    [[nodiscard]] std::uint64_t microseconds() const noexcept;
private:
    std::int64_t frequency_{};
    std::int64_t start_{};
};
} // namespace gravity::hle
