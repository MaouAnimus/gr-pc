#include "hle/loader/abi_thunk.h"

// Example HLE function that follows Win64 ABI (first four args in RCX,RDX,R8,R9, rest on stack).
// For demonstration we just sum all six arguments.
extern "C" uint64_t hle_example_function(uint64_t a0,
                                         uint64_t a1,
                                         uint64_t a2,
                                         uint64_t a3,
                                         uint64_t a4,
                                         uint64_t a5)
{
    return a0 + a1 + a2 + a3 + a4 + a5;
}

extern "C" GRAVITY_SYSV_ABI uint64_t sysv_to_win64_thunk(
    uint64_t arg0, // RDI
    uint64_t arg1, // RSI
    uint64_t arg2, // RDX
    uint64_t arg3, // RCX
    uint64_t arg4, // R8
    uint64_t arg5  // R9
)
{
    return hle_example_function(arg0, arg1, arg2, arg3, arg4, arg5);
}
