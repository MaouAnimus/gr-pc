#include "hle/timing.h"
#include <Windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <limits>
#include <thread>

namespace gravity::hle::timing {
namespace {
__extension__ using Wide = unsigned __int128;
struct ClockState {
    std::uint64_t frequency{}, origin{};
    ClockState() {
        LARGE_INTEGER f{}, t{};
        if (QueryPerformanceFrequency(&f) && f.QuadPart > 0 && QueryPerformanceCounter(&t)) {
            frequency = static_cast<std::uint64_t>(f.QuadPart);
            origin = static_cast<std::uint64_t>(t.QuadPart);
        }
    }
    std::uint64_t elapsed(std::uint64_t rate) const noexcept {
        LARGE_INTEGER t{};
        if (!frequency || !QueryPerformanceCounter(&t) || static_cast<std::uint64_t>(t.QuadPart) < origin) return 0;
        return scale_counter(static_cast<std::uint64_t>(t.QuadPart) - origin, frequency, rate);
    }
};
const ClockState& clock_state() { static const ClockState state; return state; }
struct ThreadTimer {
    HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    ThreadTimer() { if (!handle) handle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS); }
    ~ThreadTimer() { if (handle) CloseHandle(handle); }
};
}
std::uint64_t scale_counter(std::uint64_t ticks, std::uint64_t frequency, std::uint64_t rate) noexcept {
    if (!frequency) return 0;
    const auto value = Wide(ticks) * rate / frequency;
    return value > UINT64_MAX ? UINT64_MAX : static_cast<std::uint64_t>(value);
}
std::uint64_t process_microseconds() noexcept { return clock_state().elapsed(1'000'000); }
std::uint64_t guest_tsc() noexcept { return __rdtsc(); }
bool invariant_tsc() noexcept {
    int registers[4]{}; __cpuid(registers, static_cast<int>(0x80000000U));
    if (static_cast<std::uint32_t>(registers[0]) < 0x80000007U) return false;
    __cpuid(registers, static_cast<int>(0x80000007U)); return (registers[3] & (1 << 8)) != 0;
}
std::uint64_t tsc_frequency() noexcept {
    // The eboot also contains inline RDTSC. Expose the same counter domain,
    // rather than scaling only the imported ReadTsc wrapper to a virtual rate.
    static const std::uint64_t frequency = [] {
        int registers[4]{}; __cpuid(registers, 0);
        if (registers[0] >= 0x15) {
            __cpuidex(registers, 0x15, 0);
            const auto denominator = static_cast<std::uint32_t>(registers[0]);
            const auto numerator = static_cast<std::uint32_t>(registers[1]);
            const auto crystal = static_cast<std::uint32_t>(registers[2]);
            if (denominator && numerator && crystal) return scale_counter(crystal, denominator, numerator);
        }
        LARGE_INTEGER qpf{}; if (!QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) return std::uint64_t{0};
        std::array<std::uint64_t, 3> samples{};
        for (auto& sample : samples) {
            LARGE_INTEGER begin{}, end{};
            QueryPerformanceCounter(&begin); _mm_lfence(); const auto t0 = __rdtsc();
            sleep_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(20));
            _mm_lfence(); const auto t1 = __rdtsc(); QueryPerformanceCounter(&end);
            sample = end.QuadPart > begin.QuadPart ? scale_counter(t1 - t0, end.QuadPart - begin.QuadPart, qpf.QuadPart) : 0;
        }
        std::sort(samples.begin(), samples.end()); return samples[1];
    }();
    return frequency;
}
void sleep_until(std::chrono::steady_clock::time_point deadline) noexcept {
    thread_local ThreadTimer timer;
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
        LARGE_INTEGER due{};
        due.QuadPart = -(ns / 100 + (ns % 100 != 0));
        if (!timer.handle || !SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer.handle, INFINITE) != WAIT_OBJECT_0) {
            std::this_thread::sleep_until(deadline); return;
        }
    }
}
void sleep_microseconds(std::uint32_t duration) noexcept {
    if (!duration) { SwitchToThread(); return; }
    sleep_until(std::chrono::steady_clock::now() + std::chrono::microseconds(duration));
}
std::chrono::nanoseconds FramePacer::tick_offset(std::uint64_t tick) noexcept {
    const auto value = Wide(tick) * 1'000'000'000 / 60;
    constexpr auto max = (std::numeric_limits<std::int64_t>::max)();
    return std::chrono::nanoseconds(value > static_cast<std::uint64_t>(max) ? max : static_cast<std::int64_t>(value));
}
void FramePacer::reset(Clock::time_point origin) noexcept { origin_ = origin; next_tick_ = 1; }
FramePacer::Clock::time_point FramePacer::deadline_after(Clock::time_point now) noexcept {
    if (now >= origin_) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - origin_).count();
        const auto expired = static_cast<std::uint64_t>(Wide(elapsed) * 60 / 1'000'000'000);
        if (next_tick_ <= expired) next_tick_ = expired + 1;
    }
    auto deadline = origin_ + tick_offset(next_tick_++);
    if (deadline <= now) deadline = origin_ + tick_offset(next_tick_++);
    return deadline;
}
void FramePacer::wait() noexcept {
    static std::atomic<unsigned> pacer_calls{0};
    if (pacer_calls.fetch_add(1) < 10) {
        std::fprintf(stderr, "[Timing] FramePacer::wait() call %u tid=0x%lx caller=%p\n",
                     pacer_calls.load(), static_cast<unsigned long>(GetCurrentThreadId()), __builtin_return_address(0));
    }
    sleep_until(deadline_after(Clock::now()));
}
}
