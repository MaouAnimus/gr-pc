#include "hle/kernel/rwlock.h"
#include "hle/guest_memory.h"
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace gravity::hle::rwlock {
namespace {
constexpr int invalid = static_cast<int>(0x80020016U);
constexpr int busy = static_cast<int>(0x80020010U);
constexpr int deadlock = static_cast<int>(0x8002000bU);
constexpr int permission = static_cast<int>(0x80020001U);
struct Lock {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<DWORD, unsigned> readers;
    DWORD writer{};
    unsigned waiters{};
    bool deleted{};
};
std::mutex registry_mutex;
std::map<Opaque, std::shared_ptr<Lock>> locks;
std::set<Opaque> attributes;
Opaque next_token = 0x200000;
std::shared_ptr<Lock> lookup(Opaque* slot, bool lazy = false) {
    std::lock_guard guard(registry_mutex);
    Opaque token{};
    if (!guest_memory::read(slot, &token, sizeof(token))) return {};
    if (!token && lazy) {
        auto lock = std::make_shared<Lock>();
        token = next_token++;
        if (!guest_memory::write(slot, &token, sizeof(token))) return {};
        locks.emplace(token, std::move(lock));
    }
    const auto it = locks.find(token);
    return it == locks.end() ? nullptr : it->second;
}
int acquire(Opaque* slot, bool write) {
    const auto lock = lookup(slot, true);
    if (!lock) return invalid;
    const auto current = GetCurrentThreadId();
    std::unique_lock guard(lock->mutex);
    if (lock->deleted) return invalid;
    if (lock->writer == current || (write && lock->readers.contains(current))) return deadlock;
    ++lock->waiters;
    lock->changed.wait(guard, [&] { return lock->deleted || (!lock->writer && (!write || lock->readers.empty())); });
    --lock->waiters;
    if (lock->deleted) return invalid;
    if (write) lock->writer = current;
    else ++lock->readers[current];
    return 0;
}
}
extern "C" GRAVITY_SYSV_ABI int scePthreadRwlockattrInit(Opaque* slot) {
    std::lock_guard guard(registry_mutex);
    const auto token = next_token++;
    if (!guest_memory::write(slot, &token, sizeof(token))) return invalid;
    attributes.insert(token);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int scePthreadRwlockattrDestroy(Opaque* slot) {
    std::lock_guard guard(registry_mutex);
    Opaque token{}, zero{};
    if (!guest_memory::read(slot, &token, sizeof(token)) || !attributes.contains(token) ||
        !guest_memory::write(slot, &zero, sizeof(zero))) return invalid;
    attributes.erase(token);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int scePthreadRwlockInit(Opaque* slot, const Opaque* attr, const char* name) {
    (void)name;
    std::lock_guard guard(registry_mutex);
    if (attr) {
        Opaque token{};
        if (!guest_memory::read(attr, &token, sizeof(token)) || !attributes.contains(token)) return invalid;
    }
    const auto token = next_token++;
    auto lock = std::make_shared<Lock>();
    if (!guest_memory::write(slot, &token, sizeof(token))) return invalid;
    locks.emplace(token, std::move(lock));
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int scePthreadRwlockDestroy(Opaque* slot) {
    const auto lock = lookup(slot);
    if (!lock) return invalid;
    std::lock_guard guard(lock->mutex);
    if (lock->deleted) return invalid;
    if (lock->writer || !lock->readers.empty() || lock->waiters) return busy;
    std::lock_guard registry_guard(registry_mutex);
    Opaque token{};
    const Opaque destroyed = UINT64_MAX;
    if (!guest_memory::read(slot, &token, sizeof(token)) || !guest_memory::write(slot, &destroyed, sizeof(destroyed))) return invalid;
    lock->deleted = true;
    locks.erase(token);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int scePthreadRwlockRdlock(Opaque* slot) { return acquire(slot, false); }
extern "C" GRAVITY_SYSV_ABI int scePthreadRwlockWrlock(Opaque* slot) { return acquire(slot, true); }
extern "C" GRAVITY_SYSV_ABI int scePthreadRwlockUnlock(Opaque* slot) {
    const auto lock = lookup(slot);
    if (!lock) return invalid;
    const auto current = GetCurrentThreadId();
    std::lock_guard guard(lock->mutex);
    if (lock->deleted) return invalid;
    if (lock->writer == current) lock->writer = 0;
    else {
        const auto it = lock->readers.find(current);
        if (it == lock->readers.end()) return permission;
        if (--it->second == 0) lock->readers.erase(it);
    }
    lock->changed.notify_all();
    return 0;
}
}
