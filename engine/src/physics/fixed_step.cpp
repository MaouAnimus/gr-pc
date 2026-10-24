#include "hle/physics/fixed_step.h"
#include <limits>

namespace gravity::hle::physics {
namespace {
struct ReleaseGate {
    std::atomic_flag& flag;
    ~ReleaseGate() { flag.clear(std::memory_order_release); }
};
}
StepReport FixedStepDriver::advance(std::chrono::nanoseconds elapsed, StepCallback step, void* context) {
    if (active_.test_and_set(std::memory_order_acquire)) return {{Error::busy}, 0, 0};
    ReleaseGate release{active_};
    if (elapsed.count() < 0 || !step || !max_catchup_) return {{Error::invalid_argument}, 0, units_ / 1'000'000'000};
    if (failed_) return {{Error::invalid_state}, 0, units_ / 1'000'000'000};
    const auto ns = static_cast<std::uint64_t>(elapsed.count());
    if (ns > (UINT64_MAX - units_) / 60) return {{Error::overflow}, 0, units_ / 1'000'000'000};
    units_ += ns * 60;
    StepReport report;
    while (units_ >= 1'000'000'000 && report.completed < max_catchup_) {
        if (next_tick_ == UINT64_MAX) { report.status = {Error::overflow}; break; }
        try { report.status = step(context, {next_tick_, fixed_dt}); }
        catch (...) { report.status = {Error::callback_exception}; }
        if (!report.status) { failed_ = true; break; }
        ++next_tick_; ++report.completed; units_ -= 1'000'000'000;
    }
    report.pending = units_ / 1'000'000'000;
    return report;
}
Status FixedStepDriver::reset() {
    if (active_.test_and_set(std::memory_order_acquire)) return {Error::busy};
    ReleaseGate release{active_};
    units_ = next_tick_ = 0; failed_ = false; return {};
}
}
