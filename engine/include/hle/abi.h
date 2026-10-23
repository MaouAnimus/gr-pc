#pragma once

#if !defined(__clang__) || !defined(_WIN32) || !defined(__x86_64__)
#error This backend requires Clang for Windows x86-64.
#endif

// Apply to guest-facing functions AND callback pointer types. Windows API
// callbacks retain their native WINAPI convention. No handwritten register shuffle.
#define GRAVITY_SYSV_ABI __attribute__((sysv_abi))
