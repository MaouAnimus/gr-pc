#include "hle/kernel/pthread_sync.h"
#include "hle/guest_memory.h"
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>

namespace gravity::hle::pthread_sync {
namespace {
// Guest errno values, independent of the Windows C runtime's errno numbering.
constexpr int invalid = 22, busy = 16, deadlock = 11, permission = 1, timeout = 60;
struct Mutex {
    explicit Mutex(int kind) : type(kind) {}
    std::mutex state;
    std::condition_variable changed;
    DWORD owner{};
    unsigned depth{};
    unsigned waiters{};
    int type{1}; // FreeBSD ERRORCHECK=1, RECURSIVE=2, NORMAL=3.
    bool deleted{};
};
std::mutex registry_mutex;
std::map<Opaque, int> attributes;
std::map<Opaque, std::shared_ptr<Mutex>> mutexes;
Opaque next_token = 0x40000;
bool read_token(const Opaque* slot, Opaque& value) { return guest_memory::read(slot, &value, sizeof(value)); }
bool write_token(Opaque* slot, Opaque value) { return guest_memory::write(slot, &value, sizeof(value)); }

std::shared_ptr<Mutex> lookup(Opaque* slot, bool lazy) {
    std::lock_guard lock(registry_mutex);
    Opaque token{};
    if (!read_token(slot, token)) return {};
    if (!token && lazy) {
        auto mutex = std::make_shared<Mutex>(1);
        token = next_token++;
        if (!write_token(slot, token)) return {};
        mutexes.emplace(token, std::move(mutex));
    }
    const auto it = mutexes.find(token);
    return it == mutexes.end() ? nullptr : it->second;
}
int acquire(Opaque* slot, bool poll, const std::uint64_t* micros) {
    const auto mutex = lookup(slot, true);
    if (!mutex) return invalid;
    std::unique_lock lock(mutex->state);
    if (mutex->deleted) return invalid;
    const auto thread = GetCurrentThreadId();
    if (mutex->owner == thread) {
        if (mutex->type == 2) {
            if (mutex->depth == UINT_MAX) return 35; // EAGAIN.
            ++mutex->depth;
            return 0;
        }
        if (poll) return busy;
        if (mutex->type == 1) return deadlock;
    }
    if (poll && mutex->owner) return busy;
    ++mutex->waiters;
    const auto ready = [&] { return mutex->deleted || !mutex->owner; };
    bool acquired = true;
    if (micros) acquired = mutex->changed.wait_for(lock, std::chrono::microseconds(*micros), ready);
    else if (!poll) mutex->changed.wait(lock, ready);
    --mutex->waiters;
    if (mutex->deleted) return invalid;
    if (!acquired) return timeout;
    mutex->owner = thread;
    mutex->depth = 1;
    return 0;
}
}
extern "C" GRAVITY_SYSV_ABI int posix_mutexattr_init(Opaque* attr) {
    std::lock_guard lock(registry_mutex);
    const auto token = next_token++;
    if (!write_token(attr, token)) return invalid;
    attributes[token] = 1;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int posix_mutexattr_destroy(Opaque* attr) {
    std::lock_guard lock(registry_mutex);
    Opaque token{};
    if (!read_token(attr, token) || !attributes.contains(token) || !write_token(attr, 0)) return invalid;
    attributes.erase(token);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int posix_mutexattr_settype(Opaque* attr, int type) {
    if (type < 1 || type > 3) return invalid;
    std::lock_guard lock(registry_mutex);
    Opaque token{};
    if (!read_token(attr, token) || !attributes.contains(token)) return invalid;
    attributes[token] = type;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int posix_mutex_init(Opaque* slot, const Opaque* attr) {
    std::lock_guard lock(registry_mutex);
    int type = 1;
    if (attr) {
        Opaque token{};
        if (!read_token(attr, token) || !attributes.contains(token)) return invalid;
        type = attributes[token];
    }
    auto mutex = std::make_shared<Mutex>(type);
    const auto token = next_token++;
    if (!write_token(slot, token)) return invalid;
    mutexes.emplace(token, std::move(mutex));
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int posix_mutex_destroy(Opaque* slot) {
    const auto mutex = lookup(slot, false);
    if (!mutex) return invalid;
    std::lock_guard lock(mutex->state);
    if (mutex->deleted) return invalid;
    if (mutex->owner || mutex->waiters) return busy;
    std::lock_guard registry_lock(registry_mutex);
    Opaque token{};
    if (!read_token(slot, token) || !write_token(slot, UINT64_MAX)) return invalid;
    mutex->deleted = true;
    mutexes.erase(token);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int posix_mutex_lock(Opaque* mutex) { return acquire(mutex, false, nullptr); }
extern "C" GRAVITY_SYSV_ABI int posix_mutex_trylock(Opaque* mutex) { return acquire(mutex, true, nullptr); }
extern "C" GRAVITY_SYSV_ABI int posix_mutex_reltimedlock(Opaque* mutex, std::uint64_t micros) {
    if (micros > static_cast<std::uint64_t>(INT64_MAX)) return invalid;
    return acquire(mutex, false, &micros);
}
extern "C" GRAVITY_SYSV_ABI int posix_mutex_unlock(Opaque* slot) {
    const auto mutex = lookup(slot, false);
    if (!mutex) return invalid;
    std::lock_guard lock(mutex->state);
    if (mutex->deleted) return invalid;
    if (mutex->owner != GetCurrentThreadId()) return permission;
    if (--mutex->depth == 0) {
        mutex->owner = 0;
        mutex->changed.notify_one();
    }
    return 0;
}
}
