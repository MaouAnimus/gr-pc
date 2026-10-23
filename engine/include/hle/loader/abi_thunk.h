#pragma once
#include <cstdint>
#include "hle/abi.h"

namespace gravity::hle::loader {

    // Clang receives System V arguments and emits a normal Win64 call.
    // Argument order is preserved; this is a six-integer demonstration only.
    extern "C" GRAVITY_SYSV_ABI uint64_t sysv_to_win64_thunk(
        uint64_t arg0, // RDI
        uint64_t arg1, // RSI
        uint64_t arg2, // RDX
        uint64_t arg3, // RCX
        uint64_t arg4, // R8
        uint64_t arg5  // R9
    );

} // namespace gravity::hle::loader
