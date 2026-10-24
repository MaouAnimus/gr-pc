#include "hle/kernel/sema.h"
#include "hle/kernel/pthread.h"
#include "hle/guest_memory.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <algorithm>

namespace gravity::hle::sema {
namespace {
constexpr auto invalid = static_cast<std::int32_t>(0x80020016U);
constexpr auto missing = static_cast<std::int32_t>(0x80020003U);
constexpr auto timed_out = static_cast<std::int32_t>(0x8002003cU);
constexpr auto canceled = static_cast<std::int32_t>(0x80020055U);
struct Ticket { int priority{256}; };
struct Semaphore {
    std::mutex mutex;
    std::condition_variable changed;
    std::list<Ticket*> waiters;
    int available{}, maximum{}, initial{};
    bool priority_order{}, deleted{};
    std::uint64_t generation{};
};
std::mutex registry_mutex;
std::map<Handle, std::shared_ptr<Semaphore>> registry;
Handle next_handle = 0x100000;
std::shared_ptr<Semaphore> lookup(Handle handle) {
    std::lock_guard lock(registry_mutex);
    const auto it = registry.find(handle);
    return it == registry.end() ? nullptr : it->second;
}
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelCreateSema(Handle* output, const char* name, std::uint32_t attributes,
    std::int32_t initial, std::int32_t maximum, const void* options) {
    if (initial < 0 || maximum <= 0 || initial > maximum || attributes > 2 || options || !name ||
        !guest_memory::accessible(output, sizeof(*output), true)) return invalid;
    char safe_name[33]{};
    if (!guest_memory::string(name, safe_name, sizeof(safe_name))) return invalid;
    auto semaphore = std::make_shared<Semaphore>();
    semaphore->available = semaphore->initial = initial;
    semaphore->maximum = maximum;
    semaphore->priority_order = attributes == 2;
    std::lock_guard lock(registry_mutex);
    const auto handle = next_handle++;
    if (!guest_memory::write(output, &handle, sizeof(handle))) return invalid;
    registry.emplace(handle, std::move(semaphore));
    std::fprintf(stderr, "[Sema] Created %s handle=0x%llx initial=%d maximum=%d attr=%u\n",
        safe_name, static_cast<unsigned long long>(handle), initial, maximum, attributes);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelDeleteSema(Handle handle) {
    const auto semaphore = lookup(handle);
    if (!semaphore) return missing;
    { std::lock_guard lock(semaphore->mutex); semaphore->deleted = true; }
    semaphore->changed.notify_all();
    { std::lock_guard lock(registry_mutex); registry.erase(handle); }
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelWaitSema(Handle handle, std::int32_t count, std::uint32_t* timeout_us) {
    const auto semaphore = lookup(handle);
    if (!semaphore) return missing;
    if (count < 1 || count > semaphore->maximum) return invalid;
    std::uint32_t micros{};
    if (timeout_us && (!guest_memory::read(timeout_us, &micros, sizeof(micros)) ||
        !guest_memory::accessible(timeout_us, sizeof(micros), true))) return invalid;
    const auto start = std::chrono::steady_clock::now();
    Ticket ticket;
    (void)pthread::getprio(pthread::self(), &ticket.priority);
    std::unique_lock lock(semaphore->mutex);
    const auto generation = semaphore->generation;
    auto where = semaphore->waiters.end();
    if (semaphore->priority_order)
        where = std::find_if(semaphore->waiters.begin(), semaphore->waiters.end(),
            [&](const Ticket* existing) { return existing->priority > ticket.priority; });
    const auto position = semaphore->waiters.insert(where, &ticket);
    const auto ready = [&] { return semaphore->deleted || semaphore->generation != generation ||
        (semaphore->waiters.front() == &ticket && semaphore->available >= count); };
    bool signaled = true;
    if (!timeout_us) semaphore->changed.wait(lock, ready);
    else signaled = semaphore->changed.wait_for(lock, std::chrono::microseconds(micros), ready);
    semaphore->waiters.erase(position);
    std::int32_t result{};
    if (semaphore->deleted) result = static_cast<std::int32_t>(0x8002000dU);
    else if (semaphore->generation != generation) result = canceled;
    else if (!signaled) result = timed_out;
    else semaphore->available -= count;
    semaphore->changed.notify_all();
    if (timeout_us) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        const std::uint32_t remaining = elapsed >= micros ? 0 : micros - static_cast<std::uint32_t>(elapsed);
        if (!guest_memory::write(timeout_us, &remaining, sizeof(remaining))) return invalid;
    }
    return result;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelSignalSema(Handle handle, std::int32_t count) {
    const auto semaphore = lookup(handle);
    if (!semaphore) return missing;
    std::lock_guard lock(semaphore->mutex);
    if (semaphore->deleted) return missing;
    if (count < 1 || count > semaphore->maximum - semaphore->available) return invalid;
    semaphore->available += count;
    semaphore->changed.notify_all();
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelCancelSema(Handle handle, std::int32_t count, std::int32_t* output) {
    const auto semaphore = lookup(handle);
    if (!semaphore) return missing;
    std::lock_guard lock(semaphore->mutex);
    if (semaphore->deleted) return missing;
    if (count < -1 || count > semaphore->maximum) return invalid;
    const auto waiting = static_cast<std::int32_t>(semaphore->waiters.size());
    if (output && !guest_memory::write(output, &waiting, sizeof(waiting))) return invalid;
    semaphore->available = count == -1 ? semaphore->initial : count;
    ++semaphore->generation;
    semaphore->changed.notify_all();
    return 0;
}
}
