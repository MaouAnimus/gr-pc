#pragma once
#include <chrono>
#include <cstdint>

namespace gravity::hle::timing {
// Overflow-safe integer rescaling, also used by deterministic clock tests.
std::uint64_t scale_counter(std::uint64_t ticks, std::uint64_t frequency, std::uint64_t rate) noexcept;
std::uint64_t process_microseconds() noexcept;
std::uint64_t guest_tsc() noexcept;
std::uint64_t tsc_frequency() noexcept;
bool invariant_tsc() noexcept;
void sleep_until(std::chrono::steady_clock::time_point deadline) noexcept;
void sleep_microseconds(std::uint32_t duration) noexcept;

// Absolute, rational 60 Hz deadlines. Late frames skip expired presentation
// slots instead of issuing a burst of flips. Simulation catch-up is separate.
class FramePacer {
public:
    using Clock = std::chrono::steady_clock;
    void reset(Clock::time_point origin = Clock::now()) noexcept;
    Clock::time_point deadline_after(Clock::time_point now) noexcept;
    void wait() noexcept;
    static std::chrono::nanoseconds tick_offset(std::uint64_t tick) noexcept;
private:
    Clock::time_point origin_{};
    std::uint64_t next_tick_{1};
};
}
