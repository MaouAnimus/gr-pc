#pragma once
#include "hle/abi.h"
#include <cstdint>
namespace gravity::hle::pthread_sync {
using Opaque = std::uint64_t;
extern "C" {
GRAVITY_SYSV_ABI int posix_mutexattr_init(Opaque* attr);
GRAVITY_SYSV_ABI int posix_mutexattr_destroy(Opaque* attr);
GRAVITY_SYSV_ABI int posix_mutexattr_settype(Opaque* attr, int type);
GRAVITY_SYSV_ABI int posix_mutex_init(Opaque* mutex, const Opaque* attr);
GRAVITY_SYSV_ABI int posix_mutex_destroy(Opaque* mutex);
GRAVITY_SYSV_ABI int posix_mutex_lock(Opaque* mutex);
GRAVITY_SYSV_ABI int posix_mutex_trylock(Opaque* mutex);
GRAVITY_SYSV_ABI int posix_mutex_reltimedlock(Opaque* mutex, std::uint64_t micros);
GRAVITY_SYSV_ABI int posix_mutex_unlock(Opaque* mutex);

// Condition variables (POSIX & Sony libkernel)
GRAVITY_SYSV_ABI int posix_condattr_init(Opaque* attr);
GRAVITY_SYSV_ABI int posix_condattr_destroy(Opaque* attr);
GRAVITY_SYSV_ABI int posix_cond_init(Opaque* cond, const Opaque* attr);
GRAVITY_SYSV_ABI int posix_cond_destroy(Opaque* cond);
GRAVITY_SYSV_ABI int posix_cond_signal(Opaque* cond);
GRAVITY_SYSV_ABI int posix_cond_broadcast(Opaque* cond);
GRAVITY_SYSV_ABI int posix_cond_wait(Opaque* cond, Opaque* mutex);
GRAVITY_SYSV_ABI int posix_cond_reltimedwait(Opaque* cond, Opaque* mutex, std::uint64_t micros);
GRAVITY_SYSV_ABI int posix_cond_timedwait(Opaque* cond, Opaque* mutex, const void* abstime);

GRAVITY_SYSV_ABI int scePthreadCondInit(Opaque* cond, const Opaque* attr, const char* name);
GRAVITY_SYSV_ABI int scePthreadCondDestroy(Opaque* cond);
GRAVITY_SYSV_ABI int scePthreadCondSignal(Opaque* cond);
GRAVITY_SYSV_ABI int scePthreadCondBroadcast(Opaque* cond);
GRAVITY_SYSV_ABI int scePthreadCondWait(Opaque* cond, Opaque* mutex);
GRAVITY_SYSV_ABI int scePthreadCondTimedwait(Opaque* cond, Opaque* mutex, std::uint64_t usec);
GRAVITY_SYSV_ABI int scePthreadCondattrInit(Opaque* attr);
GRAVITY_SYSV_ABI int scePthreadCondattrDestroy(Opaque* attr);
}
}
