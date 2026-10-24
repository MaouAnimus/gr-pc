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
}
}
