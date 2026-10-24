#include "hle/kernel/event_flag.h"
#include "hle/guest_memory.h"
#include "hle/kernel/pthread.h"
#include <chrono>
#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>

namespace gravity::hle::event_flag {
namespace {
struct Waiter {
    std::condition_variable changed;
    std::uint64_t pattern{}, observed{};
    std::uint32_t mode{};
    int priority{256};
    std::int32_t result{};
    bool done{};
};
struct Flag {
    std::mutex mutex;
    std::list<Waiter*> pending;
    std::uint64_t bits{};
    bool multiple{}, priority_order{}, removed{};
};
std::mutex registry_mutex;
std::map<Handle, std::shared_ptr<Flag>> registry;
Handle next_handle = 0x200000;
std::shared_ptr<Flag> find(Handle handle) {
    std::lock_guard lock(registry_mutex);
    const auto it = registry.find(handle);
    return it == registry.end() ? nullptr : it->second;
}
bool valid_mode(std::uint32_t mode) {
    const auto condition = mode & 0x0fU, clear = mode & 0xf0U;
    return !(mode & ~0x33U) && (condition == wait_all || condition == wait_any) &&
        (clear == 0 || clear == clear_all || clear == clear_pattern);
}
bool matches(const Flag& flag, const Waiter& waiter) {
    const auto matched = flag.bits & waiter.pattern;
    return (waiter.mode & 0x0fU) == wait_all ? matched == waiter.pattern : matched != 0;
}
void consume(Flag& flag, Waiter& waiter) {
    waiter.observed = flag.bits;
    if (waiter.mode & clear_all) flag.bits = 0;
    else if (waiter.mode & clear_pattern) flag.bits &= ~waiter.pattern;
    waiter.done = true;
}
void release_waiters(Flag& flag, std::int32_t result) {
    for (auto* waiter : flag.pending) {
        waiter->observed = flag.bits;
        waiter->result = result;
        waiter->done = true;
        waiter->changed.notify_one();
    }
    flag.pending.clear();
}
std::int32_t wait(Handle handle, std::uint64_t pattern, std::uint32_t mode,
    std::uint64_t* observed, std::uint32_t* timeout_us, bool poll) {
    const auto flag = find(handle);
    if (!flag) return missing;
    if (!pattern || !valid_mode(mode)) return invalid;
    if (observed && !guest_memory::accessible(observed, sizeof(*observed), true)) return fault;
    std::uint32_t micros{};
    if (timeout_us && (!guest_memory::read(timeout_us, &micros, sizeof(micros)) ||
        !guest_memory::accessible(timeout_us, sizeof(micros), true))) return fault;
    const auto start = std::chrono::steady_clock::now();
    Waiter waiter;
    waiter.pattern = pattern;
    waiter.mode = mode;
    (void)pthread::getprio(pthread::self(), &waiter.priority);
    std::unique_lock lock(flag->mutex);
    if (flag->removed) return deleted;
    if (!flag->multiple && !flag->pending.empty()) return permission;
    if (matches(*flag, waiter)) {
        // Validate/write outputs before consuming bits on the immediate path.
        waiter.observed = flag->bits;
        if (observed && !guest_memory::write(observed, &waiter.observed, sizeof(waiter.observed))) return fault;
        consume(*flag, waiter);
    } else if (poll || (timeout_us && !micros)) {
        waiter.observed = flag->bits;
        waiter.result = poll ? busy : timed_out;
    } else {
        auto where = flag->pending.end();
        if (flag->priority_order) {
            for (auto it = flag->pending.begin(); it != flag->pending.end(); ++it) {
                if ((*it)->priority > waiter.priority) { where = it; break; }
            }
        }
        flag->pending.insert(where, &waiter);
        const auto ready = [&] { return waiter.done; };
        if (!timeout_us) waiter.changed.wait(lock, ready);
        else if (!waiter.changed.wait_until(lock, start + std::chrono::microseconds(micros), ready)) {
            waiter.result = timed_out;
            waiter.observed = flag->bits;
        }
        flag->pending.remove(&waiter);
    }
    if (observed && !guest_memory::write(observed, &waiter.observed, sizeof(waiter.observed))) return fault;
    if (timeout_us) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        const std::uint32_t remaining = elapsed >= micros ? 0 : micros - static_cast<std::uint32_t>(elapsed);
        if (!guest_memory::write(timeout_us, &remaining, sizeof(remaining))) return fault;
    }
    return waiter.result;
}
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelCreateEventFlag(Handle* output, const char* name,
    std::uint32_t attributes, std::uint64_t initial, const void* options) {
    if (!output || !name || options || (attributes & ~0x33U) ||
        (attributes & 0x0fU) > priority || (attributes & 0xf0U) == 0x30U) return invalid;
    if (!guest_memory::accessible(output, sizeof(*output), true)) return fault;
    bool terminated = false;
    for (std::size_t i = 0; i < 32; ++i) {
        char value{};
        if (!guest_memory::read(name + i, &value, 1)) return fault;
        if (!value) { terminated = true; break; }
    }
    if (!terminated) return static_cast<std::int32_t>(0x8002003fU); // ENAMETOOLONG.
    auto flag = std::make_shared<Flag>();
    flag->bits = initial;
    flag->multiple = (attributes & multi) != 0;
    flag->priority_order = (attributes & priority) != 0;
    std::lock_guard lock(registry_mutex);
    const auto handle = next_handle++;
    if (!guest_memory::write(output, &handle, sizeof(handle))) return fault;
    registry.emplace(handle, std::move(flag));
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelDeleteEventFlag(Handle handle) {
    std::shared_ptr<Flag> flag;
    {
        std::lock_guard lock(registry_mutex);
        const auto it = registry.find(handle);
        if (it == registry.end()) return missing;
        flag = it->second;
        registry.erase(it);
    }
    std::lock_guard lock(flag->mutex);
    flag->removed = true;
    release_waiters(*flag, deleted);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelCloseEventFlag(Handle handle) {
    return sceKernelDeleteEventFlag(handle);
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelSetEventFlag(Handle handle, std::uint64_t pattern) {
    const auto flag = find(handle);
    if (!flag) return missing;
    std::lock_guard lock(flag->mutex);
    if (flag->removed) return deleted;
    flag->bits |= pattern;
    // Resolve every eligible waiter while holding the lock. A later set/clear
    // cannot overwrite an already completed wait's observed pattern.
    for (auto it = flag->pending.begin(); it != flag->pending.end();) {
        auto* waiter = *it;
        if (!matches(*flag, *waiter)) { ++it; continue; }
        consume(*flag, *waiter);
        waiter->changed.notify_one();
        it = flag->pending.erase(it);
    }
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelClearEventFlag(Handle handle, std::uint64_t pattern) {
    const auto flag = find(handle);
    if (!flag) return missing;
    std::lock_guard lock(flag->mutex);
    if (flag->removed) return deleted;
    flag->bits &= pattern;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelWaitEventFlag(Handle handle, std::uint64_t pattern,
    std::uint32_t mode, std::uint64_t* observed, std::uint32_t* timeout_us) {
    return wait(handle, pattern, mode, observed, timeout_us, false);
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelPollEventFlag(Handle handle, std::uint64_t pattern,
    std::uint32_t mode, std::uint64_t* observed) {
    return wait(handle, pattern, mode, observed, nullptr, true);
}
extern "C" GRAVITY_SYSV_ABI std::int32_t sceKernelCancelEventFlag(Handle handle, std::uint64_t pattern,
    std::int32_t* waiters) {
    const auto flag = find(handle);
    if (!flag) return missing;
    if (waiters && !guest_memory::accessible(waiters, sizeof(*waiters), true)) return fault;
    std::lock_guard lock(flag->mutex);
    if (flag->removed) return deleted;
    const auto count = static_cast<std::int32_t>(flag->pending.size());
    if (waiters && !guest_memory::write(waiters, &count, sizeof(count))) return fault;
    flag->bits = pattern;
    release_waiters(*flag, canceled);
    return 0;
}
void reset() {
    std::map<Handle, std::shared_ptr<Flag>> previous;
    { std::lock_guard lock(registry_mutex); previous.swap(registry); }
    for (auto& [handle, flag] : previous) {
        (void)handle;
        std::lock_guard lock(flag->mutex);
        flag->removed = true;
        release_waiters(*flag, deleted);
    }
}
}
