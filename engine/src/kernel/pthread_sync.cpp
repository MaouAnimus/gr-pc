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

namespace {
struct CondVar {
    std::mutex state;
    std::condition_variable cv;
    unsigned waiters{0};
    bool deleted{false};
};
std::map<Opaque, std::shared_ptr<CondVar>> conds;
std::map<Opaque, int> cond_attributes;

std::shared_ptr<CondVar> lookup_cond(Opaque* slot, bool lazy) {
    std::lock_guard lock(registry_mutex);
    Opaque token{};
    if (!read_token(slot, token)) return {};
    if (!token && lazy) {
        auto cond = std::make_shared<CondVar>();
        token = next_token++;
        if (!write_token(slot, token)) return {};
        conds.emplace(token, std::move(cond));
    }
    const auto it = conds.find(token);
    return it == conds.end() ? nullptr : it->second;
}

int cond_wait_internal(Opaque* cond_slot, Opaque* mutex_slot, const std::uint64_t* micros) {
    const auto cond = lookup_cond(cond_slot, true);
    const auto mutex = lookup(mutex_slot, true);
    if (!cond || !mutex) return invalid;

    const auto thread = GetCurrentThreadId();
    unsigned saved_depth = 0;

    // Serialize waiter registration with signal/broadcast before releasing the
    // guest mutex. Otherwise a producer can signal in the gap and lose the wake.
    std::unique_lock cond_lock(cond->state);
    if (cond->deleted) return invalid;
    {
        std::lock_guard lock(mutex->state);
        if (mutex->deleted || mutex->owner != thread) return permission;
        saved_depth = mutex->depth;
        mutex->depth = 0;
        mutex->owner = 0;
        mutex->changed.notify_one();
    }

    bool timeout_occurred = false;
    ++cond->waiters;
    if (micros) {
        auto status = cond->cv.wait_for(cond_lock, std::chrono::microseconds(*micros));
        if (status == std::cv_status::timeout) timeout_occurred = true;
    } else {
        cond->cv.wait(cond_lock);
    }
    --cond->waiters;
    cond_lock.unlock();

    int err = acquire(mutex_slot, false, nullptr);
    if (err != 0) return err;

    {
        std::lock_guard lock(mutex->state);
        if (mutex->owner == thread) {
            mutex->depth = saved_depth;
        }
    }

    return timeout_occurred ? timeout : 0;
}
} // anonymous namespace

extern "C" GRAVITY_SYSV_ABI int posix_condattr_init(Opaque* attr) {
    std::lock_guard lock(registry_mutex);
    const auto token = next_token++;
    if (!write_token(attr, token)) return invalid;
    cond_attributes[token] = 1;
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int posix_condattr_destroy(Opaque* attr) {
    std::lock_guard lock(registry_mutex);
    Opaque token{};
    if (!read_token(attr, token) || !cond_attributes.contains(token) || !write_token(attr, 0)) return invalid;
    cond_attributes.erase(token);
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int posix_cond_init(Opaque* slot, const Opaque* attr) {
    (void)attr;
    std::lock_guard lock(registry_mutex);
    auto cond = std::make_shared<CondVar>();
    const auto token = next_token++;
    if (!write_token(slot, token)) return invalid;
    conds.emplace(token, std::move(cond));
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int posix_cond_destroy(Opaque* slot) {
    const auto cond = lookup_cond(slot, false);
    if (!cond) return invalid;
    std::lock_guard lock(cond->state);
    if (cond->deleted) return invalid;
    if (cond->waiters > 0) return busy;
    std::lock_guard registry_lock(registry_mutex);
    Opaque token{};
    if (!read_token(slot, token) || !write_token(slot, UINT64_MAX)) return invalid;
    cond->deleted = true;
    conds.erase(token);
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int posix_cond_signal(Opaque* slot) {
    const auto cond = lookup_cond(slot, true);
    if (!cond) return invalid;
    std::lock_guard lock(cond->state);
    if (cond->deleted) return invalid;
    cond->cv.notify_one();
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int posix_cond_broadcast(Opaque* slot) {
    const auto cond = lookup_cond(slot, true);
    if (!cond) return invalid;
    std::lock_guard lock(cond->state);
    if (cond->deleted) return invalid;
    cond->cv.notify_all();
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int posix_cond_wait(Opaque* cond, Opaque* mutex) {
    return cond_wait_internal(cond, mutex, nullptr);
}

extern "C" GRAVITY_SYSV_ABI int posix_cond_reltimedwait(Opaque* cond, Opaque* mutex, std::uint64_t micros) {
    if (micros > static_cast<std::uint64_t>(INT64_MAX)) return invalid;
    return cond_wait_internal(cond, mutex, &micros);
}

extern "C" GRAVITY_SYSV_ABI int posix_cond_timedwait(Opaque* cond, Opaque* mutex, const void* abstime) {
    if (!abstime) return invalid;
    struct GuestTimespec {
        std::int64_t tv_sec;
        std::int64_t tv_nsec;
    };
    if (!guest_memory::accessible(abstime, sizeof(GuestTimespec))) return invalid;
    const auto* ts = static_cast<const GuestTimespec*>(abstime);
    if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000) return invalid;

    std::uint64_t now_us = 0;
    if (ts->tv_sec >= 1000000000LL) {
        // Absolute calendar time (CLOCK_REALTIME)
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        constexpr uint64_t epoch_diff = 116444736000000000ULL;
        const uint64_t ft_unix = uli.QuadPart >= epoch_diff ? uli.QuadPart - epoch_diff : 0;
        now_us = ft_unix / 10;
    } else {
        // Absolute monotonic uptime (CLOCK_MONOTONIC)
        LARGE_INTEGER freq, counter;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&counter);
        now_us = static_cast<uint64_t>((counter.QuadPart * 1000000ULL) / freq.QuadPart);
    }

    const std::uint64_t target_us = static_cast<std::uint64_t>(ts->tv_sec) * 1000000ULL + static_cast<std::uint64_t>(ts->tv_nsec / 1000);

    if (target_us <= now_us) {
        return 60; // ETIMEDOUT
    }
    const std::uint64_t rel_us = target_us - now_us;
    return posix_cond_reltimedwait(cond, mutex, rel_us);
}

namespace {
int kernel_result(int result) {
    return result ? static_cast<std::int32_t>(0x80020000U + static_cast<std::uint32_t>(result)) : 0;
}
}
extern "C" GRAVITY_SYSV_ABI int scePthreadCondInit(Opaque* cond, const Opaque* attr, const char* name) {
    (void)name;
    return kernel_result(posix_cond_init(cond, attr));
}

extern "C" GRAVITY_SYSV_ABI int scePthreadCondDestroy(Opaque* cond) {
    return kernel_result(posix_cond_destroy(cond));
}

extern "C" GRAVITY_SYSV_ABI int scePthreadCondSignal(Opaque* cond) {
    return kernel_result(posix_cond_signal(cond));
}

extern "C" GRAVITY_SYSV_ABI int scePthreadCondBroadcast(Opaque* cond) {
    return kernel_result(posix_cond_broadcast(cond));
}

extern "C" GRAVITY_SYSV_ABI int scePthreadCondWait(Opaque* cond, Opaque* mutex) {
    return kernel_result(posix_cond_wait(cond, mutex));
}

extern "C" GRAVITY_SYSV_ABI int scePthreadCondTimedwait(Opaque* cond, Opaque* mutex, std::uint64_t usec) {
    return kernel_result(posix_cond_reltimedwait(cond, mutex, usec));
}

extern "C" GRAVITY_SYSV_ABI int scePthreadCondattrInit(Opaque* attr) {
    return kernel_result(posix_condattr_init(attr));
}

extern "C" GRAVITY_SYSV_ABI int scePthreadCondattrDestroy(Opaque* attr) {
    return kernel_result(posix_condattr_destroy(attr));
}
}

