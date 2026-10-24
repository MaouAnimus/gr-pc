#include "hle/timing.h"
#include "hle/physics/fixed_step.h"
#include "hle/physics/gravity_motion.h"
#include "hle/kernel/event_flag.h"
#include "hle/kernel/pthread.h"
#include "hle/registry/adapter_registry.h"
#include <intrin.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "Physics runtime line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)
using namespace gravity::hle;
using namespace gravity::hle::physics;
using namespace std::chrono_literals;
namespace {
void clocks() {
    CHECK(timing::scale_counter(4'800'000'000, 4'800'000'000, 1'000'000) == 1'000'000);
    CHECK(timing::scale_counter(UINT64_MAX, UINT64_MAX, 1'000'000) == 1'000'000);
    CHECK(timing::scale_counter(UINT64_MAX, 1, 60) == UINT64_MAX);
    CHECK(timing::scale_counter(10, 0, 1) == 0);
    auto& reg = AdapterRegistry::instance(); reg.initialize_builtin_adapters();
    using Counter = std::uint64_t(GRAVITY_SYSV_ABI*)();
    using Sleep = std::int32_t(GRAVITY_SYSV_ABI*)(std::uint32_t);
    const auto tsc = reinterpret_cast<Counter>(reg.resolve("-2IRUCO--PM", "libkernel"));
    const auto frequency = reinterpret_cast<Counter>(reg.resolve("1j3S3n-tTW4", "libkernel"));
    const auto micros = reinterpret_cast<Counter>(reg.resolve("4J2sUJmuHZQ", "libkernel"));
    const auto usleep = reinterpret_cast<Sleep>(reg.resolve("1jfXLRVzisc", "libkernel"));
    CHECK(tsc && frequency && micros && usleep);
    const auto hz = frequency(); CHECK(hz > 0);
    const auto raw0 = __rdtsc(); const auto imported = tsc(); const auto raw1 = __rdtsc();
    CHECK(raw0 <= imported && imported <= raw1); // shared domain with guest inline RDTSC
    const auto u0 = micros(); const auto t0 = tsc();
    CHECK(usleep(60000) == 0);
    const auto t1 = tsc(); const auto u1 = micros();
    const auto tsc_us = timing::scale_counter(t1 - t0, hz, 1'000'000);
    CHECK(u1 - u0 >= 60000 && std::abs(double(tsc_us) - double(u1 - u0)) < 5000);
    CHECK(usleep(0) == 0);
    std::printf("TSC: %llu Hz, invariant=%s, 60ms sample=%llu us, QPC=%llu us\n",
        static_cast<unsigned long long>(hz), timing::invariant_tsc() ? "yes" : "no",
        static_cast<unsigned long long>(tsc_us), static_cast<unsigned long long>(u1 - u0));
}
void deadlines() {
    using Pacer = timing::FramePacer;
    CHECK(Pacer::tick_offset(60) == 1s);
    CHECK(Pacer::tick_offset(60 * 3600) == 3600s);
    CHECK(Pacer::tick_offset(1) == 16666666ns && Pacer::tick_offset(3) == 50ms);
    const auto origin = Pacer::Clock::time_point{};
    Pacer pacer; pacer.reset(origin);
    CHECK(pacer.deadline_after(origin) == origin + 16666666ns);
    CHECK(pacer.deadline_after(origin + 20ms) == origin + 33333333ns);
    CHECK(pacer.deadline_after(origin + 1s) == origin + Pacer::tick_offset(61));
    pacer.reset(origin);
    CHECK(pacer.deadline_after(origin + Pacer::tick_offset(1)) > origin + Pacer::tick_offset(1));
    // Presentation misses do not create a burst of stale deadlines.
    for (int i = 0; i < 100; ++i) {
        const auto now = origin + std::chrono::milliseconds(i * 31);
        CHECK(pacer.deadline_after(now) > now);
    }
}
struct Timeline { std::vector<std::uint64_t> ticks; };
Status record(void* opaque, const FixedTick& tick) {
    auto& timeline = *static_cast<Timeline*>(opaque);
    CHECK(tick.dt == 1.0 / 60.0 && tick.index == timeline.ticks.size());
    timeline.ticks.push_back(tick.index); return {};
}
void fixed_steps() {
    FixedStepDriver driver; Timeline timeline;
    auto report = driver.advance(1s, record, &timeline);
    CHECK(report.status && report.completed == 8 && report.pending == 52);
    while (report.pending) { report = driver.advance(0ns, record, &timeline); CHECK(report.status && report.completed <= 8); }
    CHECK(timeline.ticks.size() == 60);
    CHECK(driver.reset()); timeline.ticks.clear();
    // Different render/update intervals integrate exactly one second to 60 ticks.
    for (int i = 0; i < 1000; ++i) { report = driver.advance(1ms, record, &timeline); CHECK(report.status); }
    CHECK(timeline.ticks.size() == 60 && !report.pending);
    CHECK(driver.advance(1ns, record, &timeline).completed == 0);
}
struct Simulation {
    SphereState body{{-10, 0, 3}, {3000, 10, -2000}, 0.5};
    std::array<Triangle, 6> world{{
        {{-100,-100,0},{100,-100,0},{100,100,0}}, {{-100,-100,0},{100,100,0},{-100,100,0}},
        {{0,-100,-100},{0,100,-100},{0,100,100}}, {{0,-100,-100},{0,100,100},{0,-100,100}},
        {{-100,-100,5},{100,-100,5},{100,100,5}}, {{-100,-100,5},{100,100,5},{-100,100,5}}
    }};
    unsigned ticks{}, contacts{};
};
Status simulate(void* opaque, const FixedTick& tick) {
    auto& scene = *static_cast<Simulation*>(opaque);
    CHECK(tick.index == scene.ticks && tick.dt == fixed_step_seconds);
    const auto gravity = tick.index < 20 ? Vec3{0,0,-30} : tick.index < 40 ? Vec3{0,0,100} : Vec3{100,0,0};
    const auto motion = step_sphere_60hz(scene.body, gravity, scene.world);
    CHECK(motion.status == MotionStatus::completed && motion.time_advanced == fixed_step_seconds);
    scene.body = motion.state; ++scene.ticks;
    scene.contacts += static_cast<unsigned>(motion.contacts.size());
    CHECK(scene.body.position.x <= -0.5 && scene.body.position.z >= 0.5 && scene.body.position.z <= 4.5);
    return {};
}
void cadence() {
    Simulation reference;
    for (unsigned i = 0; i < 60; ++i) CHECK(simulate(&reference, {i, fixed_dt}));
    for (const unsigned hz : {30U,60U,144U}) {
        Simulation scene; FixedStepDriver driver;
        for (unsigned frame = 0; frame < hz; ++frame) {
            const auto elapsed = std::chrono::nanoseconds((std::uint64_t(frame+1) * 1'000'000'000 / hz) -
                (std::uint64_t(frame) * 1'000'000'000 / hz));
            CHECK(driver.advance(elapsed, simulate, &scene).status);
        }
        CHECK(scene.ticks == 60 && scene.contacts == reference.contacts);
        CHECK(scene.body.position.x == reference.body.position.x && scene.body.position.y == reference.body.position.y &&
            scene.body.position.z == reference.body.position.z && scene.body.velocity.z == reference.body.velocity.z);
    }
    Simulation delayed; FixedStepDriver driver;
    auto report = driver.advance(1s, simulate, &delayed);
    CHECK(report.status && report.completed == 8 && report.pending == 52);
    while (report.pending) { report = driver.advance(0ns, simulate, &delayed); CHECK(report.status); }
    CHECK(delayed.ticks == 60 && delayed.contacts == reference.contacts);
    CHECK(delayed.body.position.x == reference.body.position.x && delayed.body.position.z == reference.body.position.z);
}
struct Jobs;
struct Worker { Jobs* jobs{}; unsigned id{}; pthread::Opaque thread{}; event_flag::Handle request{}; };
struct Jobs {
    std::array<Worker, 2> workers;
    event_flag::Handle finished{};
    std::atomic<bool> stopping{};
    std::atomic<std::uint64_t> tick{};
    std::array<std::atomic<std::uint64_t>, 2> result;
};
void* GRAVITY_SYSV_ABI work(void* opaque) {
    auto& worker = *static_cast<Worker*>(opaque); auto& jobs = *worker.jobs;
    for (;;) {
        CHECK(event_flag::sceKernelWaitEventFlag(worker.request, 1, event_flag::wait_all | event_flag::clear_all, nullptr, nullptr) == 0);
        if (jobs.stopping) break;
        const auto tick = jobs.tick.load();
        if (worker.id) timing::sleep_microseconds(20);
        CHECK(jobs.tick == tick); // coordinator must not start the next tick yet
        jobs.result[worker.id] = tick * 2 + worker.id;
        CHECK(event_flag::sceKernelSetEventFlag(jobs.finished, 1ULL << worker.id) == 0);
    }
    return nullptr;
}
Status dispatch(void* opaque, const FixedTick& tick) {
    auto& jobs = *static_cast<Jobs*>(opaque); jobs.tick = tick.index;
    for (auto& worker : jobs.workers) CHECK(event_flag::sceKernelSetEventFlag(worker.request, 1) == 0);
    std::uint32_t timeout = 1'000'000;
    CHECK(event_flag::sceKernelWaitEventFlag(jobs.finished, 3, event_flag::wait_all | event_flag::clear_all, nullptr, &timeout) == 0);
    CHECK(jobs.result[0] == tick.index * 2 && jobs.result[1] == tick.index * 2 + 1);
    return {};
}
void jobs() {
    Jobs jobs;
    CHECK(event_flag::sceKernelCreateEventFlag(&jobs.finished, "physics finished", event_flag::multi, 0, nullptr) == 0);
    for (unsigned i = 0; i < jobs.workers.size(); ++i) {
        auto& worker = jobs.workers[i]; worker.jobs = &jobs; worker.id = i;
        CHECK(event_flag::sceKernelCreateEventFlag(&worker.request, "physics request", event_flag::single, 0, nullptr) == 0);
        CHECK(pthread::create(&worker.thread, nullptr, work, &worker, "physics synthetic worker") == 0);
    }
    FixedStepDriver driver;
    for (int i = 0; i < 100; ++i) {
        const auto result = driver.advance(16666667ns, dispatch, &jobs);
        CHECK(result.status && result.completed == 1);
    }
    jobs.stopping = true;
    for (auto& worker : jobs.workers) CHECK(event_flag::sceKernelSetEventFlag(worker.request, 1) == 0);
    for (auto& worker : jobs.workers) {
        CHECK(pthread::join(worker.thread, nullptr) == 0);
        CHECK(event_flag::sceKernelDeleteEventFlag(worker.request) == 0);
    }
    CHECK(event_flag::sceKernelDeleteEventFlag(jobs.finished) == 0);
}
void invalid() {
    FixedStepDriver driver; Timeline timeline;
    CHECK(driver.advance(-1ns, record, &timeline).status.error == Error::invalid_argument);
    CHECK(driver.advance(1s, nullptr, nullptr).status.error == Error::invalid_argument);
    CHECK(driver.advance(std::chrono::nanoseconds::max(), record, &timeline).status.error == Error::overflow);
    CHECK(timeline.ticks.empty());
    auto throws = [](void*, const FixedTick&) -> Status { throw 1; };
    CHECK(driver.advance(20ms, throws, nullptr).status.error == Error::callback_exception);
    CHECK(driver.advance(0ns, record, &timeline).status.error == Error::invalid_state);
    CHECK(driver.reset());
    auto reenter = [](void* p, const FixedTick&) -> Status {
        auto& nested = *static_cast<FixedStepDriver*>(p);
        CHECK(nested.advance(1s, record, nullptr).status.error == Error::busy);
        CHECK(nested.reset().error == Error::busy);
        return {};
    };
    CHECK(driver.advance(20ms, reenter, &driver).status);
    CHECK(driver.reset());
    std::array<std::atomic<bool>, 2> gate{};
    auto blocking = [](void* p, const FixedTick&) -> Status {
        auto& flags = *static_cast<std::array<std::atomic<bool>, 2>*>(p);
        flags[0] = true;
        while (!flags[1]) std::this_thread::yield();
        return {};
    };
    std::thread owner([&] { CHECK(driver.advance(20ms, blocking, &gate).status); });
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!gate[0]) { CHECK(std::chrono::steady_clock::now() < deadline); std::this_thread::yield(); }
    CHECK(driver.advance(20ms, record, &timeline).status.error == Error::busy);
    CHECK(driver.reset().error == Error::busy);
    gate[1] = true; owner.join();
    CHECK(driver.reset());
    auto fails = [](void*, const FixedTick&) -> Status { return {Error::system_error, 42}; };
    const auto failed = driver.advance(20ms, fails, nullptr);
    CHECK(failed.status.error == Error::system_error && failed.status.native_error == 42 && failed.completed == 0);
    CHECK(driver.advance(0ns, record, &timeline).status.error == Error::invalid_state);
    CHECK(driver.reset());
    CHECK(driver.advance(16666666ns, record, &timeline).completed == 0);
    CHECK(driver.advance(1ns, record, &timeline).completed == 1);
}
}
int main(int argc, char** argv) {
    CHECK(argc == 2); const std::string_view suite = argv[1];
    if (suite == "clocks") clocks(); else if (suite == "deadlines") deadlines();
    else if (suite == "fixed") fixed_steps(); else if (suite == "jobs") jobs();
    else if (suite == "cadence") cadence();
    else if (suite == "invalid") invalid(); else return 2;
    std::puts("PASS physics runtime");
}
