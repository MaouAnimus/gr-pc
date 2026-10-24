#pragma once
#include "hle/abi.h"
#include <cstdint>
namespace gravity::hle::rwlock {
using Opaque = std::uint64_t;
extern "C" {
GRAVITY_SYSV_ABI int scePthreadRwlockattrInit(Opaque* attribute);
GRAVITY_SYSV_ABI int scePthreadRwlockattrDestroy(Opaque* attribute);
GRAVITY_SYSV_ABI int scePthreadRwlockInit(Opaque* lock, const Opaque* attribute, const char* name);
GRAVITY_SYSV_ABI int scePthreadRwlockDestroy(Opaque* lock);
GRAVITY_SYSV_ABI int scePthreadRwlockRdlock(Opaque* lock);
GRAVITY_SYSV_ABI int scePthreadRwlockWrlock(Opaque* lock);
GRAVITY_SYSV_ABI int scePthreadRwlockUnlock(Opaque* lock);
}
}
