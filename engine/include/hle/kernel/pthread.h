#pragma once
#include "hle/abi.h"
#include "hle/runner/runner.h"
#include <cstddef>
#include <cstdint>
namespace gravity::hle::pthread {
using Opaque = std::uint64_t;
using Entry = void*(GRAVITY_SYSV_ABI*)(void*);
void configure(const runner::RunnerConfig& config);
void dump_threads();
extern "C" {
GRAVITY_SYSV_ABI int attr_init(Opaque* attr);
GRAVITY_SYSV_ABI int attr_destroy(Opaque* attr);
GRAVITY_SYSV_ABI int attr_setstacksize(Opaque* attr, std::size_t size);
GRAVITY_SYSV_ABI int attr_setdetachstate(Opaque* attr, int state);
GRAVITY_SYSV_ABI int create(Opaque* thread, const Opaque* attr, Entry entry, void* argument, const char* name);
GRAVITY_SYSV_ABI int join(Opaque thread, void** result);
GRAVITY_SYSV_ABI Opaque self();
GRAVITY_SYSV_ABI int getprio(Opaque thread, int* priority);
GRAVITY_SYSV_ABI int setprio(Opaque thread, int priority);
}
}
