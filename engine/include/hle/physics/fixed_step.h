#pragma once
#include "hle/status.h"
#include <chrono>
#include <cstdint>
#include <atomic>

namespace gravity::hle::physics {
inline constexpr double fixed_dt = 1.0 / 60.0;
struct FixedTick {
    std::uint64_t index{};
    double dt{fixed_dt};
};
struct StepReport {
    Status status;
    std::uint32_t completed{};
    std::uint64_t pending{};
};
// The callback owns dispatch + join of the simulation's jobs. It must return
// only when the world can safely start its next tick. No world ABI is guessed.
using StepCallback = Status (*)(void* context, const FixedTick& tick);
class FixedStepDriver {
public:
    explicit FixedStepDriver(std::uint32_t max_catchup = 8) : max_catchup_(max_catchup) {}
    StepReport advance(std::chrono::nanoseconds elapsed, StepCallback step, void* context);
    Status reset();
private:
    std::atomic_flag active_ = ATOMIC_FLAG_INIT;
    std::uint64_t units_{}, next_tick_{};
    std::uint32_t max_catchup_;
    bool failed_{};
};
}
