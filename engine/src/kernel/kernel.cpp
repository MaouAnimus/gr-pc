#include "hle/kernel.h"
#include <limits>

namespace gravity::hle {
Status Mutex::relock() noexcept {
    if (kind_ != MutexKind::recursive) return {Error::deadlock};
    if (depth_ == std::numeric_limits<std::uint32_t>::max()) return {Error::overflow};
    ++depth_;
    return {};
}

Status Mutex::lock() noexcept {
    const DWORD thread = GetCurrentThreadId();
    if (owner_.load(std::memory_order_relaxed) == thread) return relock();
    AcquireSRWLockExclusive(&lock_);
    depth_ = 1;
    owner_.store(thread, std::memory_order_relaxed);
    return {};
}

Status Mutex::try_lock() noexcept {
    const DWORD thread = GetCurrentThreadId();
    if (owner_.load(std::memory_order_relaxed) == thread) {
        if (kind_ == MutexKind::error_check) return {Error::busy};
        return relock();
    }
    if (!TryAcquireSRWLockExclusive(&lock_)) return {Error::busy};
    depth_ = 1;
    owner_.store(thread, std::memory_order_relaxed);
    return {};
}

Status Mutex::unlock() noexcept {
    if (owner_.load(std::memory_order_relaxed) != GetCurrentThreadId())
        return {Error::permission};
    if (--depth_ == 0) {
        owner_.store(0, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&lock_);
    }
    return {};
}

Status EventFlag::set(std::uint64_t bits) noexcept {
    AcquireSRWLockExclusive(&lock_);
    if (canceled_) { ReleaseSRWLockExclusive(&lock_); return {Error::canceled}; }
    bits_ |= bits;
    WakeAllConditionVariable(&changed_);
    ReleaseSRWLockExclusive(&lock_);
    return {};
}

Status EventFlag::clear(std::uint64_t bits_to_clear) noexcept {
    AcquireSRWLockExclusive(&lock_);
    if (canceled_) { ReleaseSRWLockExclusive(&lock_); return {Error::canceled}; }
    bits_ &= ~bits_to_clear;
    ReleaseSRWLockExclusive(&lock_);
    return {};
}

Status EventFlag::wait(std::uint64_t requested, WaitMode mode, ClearMode clear,
                       std::uint64_t& observed, DWORD timeout_ms) noexcept {
    if (!requested || (mode != WaitMode::all && mode != WaitMode::any) ||
        (clear != ClearMode::none && clear != ClearMode::matched && clear != ClearMode::all))
        return {Error::invalid_argument};
    const auto start = GetTickCount64();
    AcquireSRWLockExclusive(&lock_);
    Status result{};
    for (;;) {
        if (canceled_) { result = {Error::canceled}; break; }
        const auto matched = bits_ & requested;
        if ((mode == WaitMode::all && matched == requested) ||
            (mode == WaitMode::any && matched != 0)) {
            observed = bits_;
            if (clear == ClearMode::matched) bits_ &= ~matched;
            if (clear == ClearMode::all) bits_ = 0;
            break;
        }
        DWORD remaining = INFINITE;
        if (timeout_ms != INFINITE) {
            const auto elapsed = GetTickCount64() - start;
            if (elapsed >= timeout_ms) { result = {Error::timeout}; break; }
            remaining = timeout_ms - static_cast<DWORD>(elapsed);
        }
        if (!SleepConditionVariableSRW(&changed_, &lock_, remaining, 0)) {
            const DWORD error = GetLastError();
            if (error != ERROR_TIMEOUT) { result = {Error::system_error, error}; break; }
            // Recheck the predicate under the lock, including a concurrent set.
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return result;
}

void EventFlag::cancel() noexcept {
    AcquireSRWLockExclusive(&lock_);
    canceled_ = true;
    WakeAllConditionVariable(&changed_);
    ReleaseSRWLockExclusive(&lock_);
}

Semaphore::~Semaphore() { if (handle_) CloseHandle(handle_); }

Status Semaphore::initialize(std::int32_t initial, std::int32_t maximum) noexcept {
    if (handle_) return {Error::invalid_state};
    if (initial < 0 || maximum <= 0 || initial > maximum) return {Error::invalid_argument};
    handle_ = CreateSemaphoreW(nullptr, initial, maximum, nullptr);
    return handle_ ? Status{} : Status{Error::system_error, GetLastError()};
}

Status Semaphore::wait(DWORD timeout_ms) noexcept {
    if (!handle_) return {Error::invalid_state};
    switch (WaitForSingleObject(handle_, timeout_ms)) {
    case WAIT_OBJECT_0: return {};
    case WAIT_TIMEOUT: return {Error::timeout};
    default: return {Error::system_error, GetLastError()};
    }
}

Status Semaphore::post(std::int32_t count, std::int32_t* previous) noexcept {
    if (!handle_) return {Error::invalid_state};
    if (count <= 0) return {Error::invalid_argument};
    LONG old{};
    if (!ReleaseSemaphore(handle_, count, &old)) {
        const DWORD error = GetLastError();
        return {error == ERROR_TOO_MANY_POSTS ? Error::overflow : Error::system_error, error};
    }
    if (previous) *previous = old;
    return {};
}

ProcessClock::ProcessClock() noexcept {
    LARGE_INTEGER frequency{}, start{};
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 &&
        QueryPerformanceCounter(&start)) {
        frequency_ = frequency.QuadPart;
        start_ = start.QuadPart;
    }
}

std::uint64_t ProcessClock::microseconds() const noexcept {
    if (!valid()) return 0;
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now) || now.QuadPart < start_) return 0;
    const auto ticks = static_cast<std::uint64_t>(now.QuadPart - start_);
    const auto frequency = static_cast<std::uint64_t>(frequency_);
    // Split before scaling to avoid overflow after only days of uptime.
    return ticks / frequency * 1'000'000 + ticks % frequency * 1'000'000 / frequency;
}
} // namespace gravity::hle
