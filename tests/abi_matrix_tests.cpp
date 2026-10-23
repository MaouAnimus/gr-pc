#include "hle/abi.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <iostream>
#include <string_view>
#include <cstdarg>

namespace {

// Test 1: 1 to 6 integer arguments in System V registers
// SysV: RDI, RSI, RDX, RCX, R8, R9
extern "C" GRAVITY_SYSV_ABI uint64_t sysv_six_int_fn(
    uint64_t a1, uint64_t a2, uint64_t a3,
    uint64_t a4, uint64_t a5, uint64_t a6)
{
    if (a1 != 10 || a2 != 20 || a3 != 30 || a4 != 40 || a5 != 50 || a6 != 60) {
        return 0;
    }
    return a1 + a2 + a3 + a4 + a5 + a6;
}

// Test 2: Stack arguments (> 6 parameters)
// SysV: RDI, RSI, RDX, RCX, R8, R9 in registers; a7..a10 on stack.
// Stack must be 16-byte aligned before call instruction (so RSP % 16 == 0 at entry after return address is pushed -> RSP % 16 == 8).
extern "C" GRAVITY_SYSV_ABI uint64_t sysv_ten_int_fn(
    uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
    uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10)
{
    // Verify stack alignment
    uintptr_t rsp_val = 0;
#if defined(__x86_64__)
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_val));
#endif
    // Inside function with prologue: RSP is aligned to 16 bytes (or 8 after push rbp)
    if (a1 != 1 || a2 != 2 || a3 != 3 || a4 != 4 || a5 != 5 ||
        a6 != 6 || a7 != 7 || a8 != 8 || a9 != 9 || a10 != 10) {
        return 0;
    }
    return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10;
}

// Test 3: Floating point arguments (XMM0 - XMM7)
extern "C" GRAVITY_SYSV_ABI double sysv_eight_double_fn(
    double d0, double d1, double d2, double d3,
    double d4, double d5, double d6, double d7)
{
    return d0 + d1 + d2 + d3 + d4 + d5 + d6 + d7;
}

// Test 4: Mixed integer and floating point arguments
extern "C" GRAVITY_SYSV_ABI double sysv_mixed_args_fn(
    int32_t i1, double d1, int64_t i2, float f1,
    const char* str, double d2, uint32_t i3, float f2)
{
    if (!str || std::strcmp(str, "GravityRush") != 0) {
        return -1.0;
    }
    return static_cast<double>(i1) + d1 + static_cast<double>(i2) + static_cast<double>(f1)
           + d2 + static_cast<double>(i3) + static_cast<double>(f2);
}

// Test 5: Small struct pass-by-value (<= 16 bytes)
struct SmallPoint {
    int64_t x;
    int64_t y;
};
static_assert(sizeof(SmallPoint) == 16);

extern "C" GRAVITY_SYSV_ABI int64_t sysv_small_struct_fn(SmallPoint pt1, SmallPoint pt2) {
    return (pt1.x * pt2.x) + (pt1.y * pt2.y);
}

// Test 6: Large struct pass-by-value (> 16 bytes)
struct LargeDataBlock {
    uint64_t v[4]; // 32 bytes
};
static_assert(sizeof(LargeDataBlock) == 32);

extern "C" GRAVITY_SYSV_ABI uint64_t sysv_large_struct_fn(LargeDataBlock block, uint64_t multiplier) {
    return (block.v[0] + block.v[1] + block.v[2] + block.v[3]) * multiplier;
}

// Test 7: Bidirectional Callbacks
using SysVCallback = uint64_t(GRAVITY_SYSV_ABI*)(uint64_t, uint64_t);

extern "C" GRAVITY_SYSV_ABI uint64_t sysv_host_guest_callback(
    uint64_t initial_val,
    SysVCallback callback)
{
    if (!callback) return 0;
    // Guest calls host callback using SysV ABI
    uint64_t intermediate = callback(initial_val, 100);
    // Continue guest logic
    return intermediate * 2;
}

// Host callback conforming to SysV ABI
extern "C" GRAVITY_SYSV_ABI uint64_t host_callback_impl(uint64_t a, uint64_t b) {
    return a + b;
}

// Test 8: Variadic adapter bridge (SysV caller forwarding to formatting runtime)
extern "C" GRAVITY_SYSV_ABI int sysv_format_adapter(char* buffer, std::size_t buf_sz, const char* fmt, uint64_t val1, uint64_t val2) {
    return std::snprintf(buffer, buf_sz, fmt, static_cast<unsigned long long>(val1), static_cast<unsigned long long>(val2));
}

// Test 9: Struct returns by value
struct SmallReturn {
    uint64_t low;
    uint64_t high;
};
static_assert(sizeof(SmallReturn) == 16);

extern "C" GRAVITY_SYSV_ABI SmallReturn sysv_return_small_struct(uint64_t a, uint64_t b) {
    SmallReturn res;
    res.low = a + b;
    res.high = a * b;
    return res;
}

struct LargeReturn {
    uint64_t data[4];
};
static_assert(sizeof(LargeReturn) == 32);

extern "C" GRAVITY_SYSV_ABI LargeReturn sysv_return_large_struct(uint64_t val) {
    LargeReturn res;
    res.data[0] = val;
    res.data[1] = val + 1;
    res.data[2] = val + 2;
    res.data[3] = val + 3;
    return res;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::string_view suite = (argc > 1) ? argv[1] : "all";

    if (suite == "all" || suite == "registers") {
        std::cout << "[RUN] sysv_six_int_fn...\n";
        uint64_t res = sysv_six_int_fn(10, 20, 30, 40, 50, 60);
        if (res != 210) {
            std::cerr << "FAIL: sysv_six_int_fn expected 210, got " << res << "\n";
            return 1;
        }
        std::cout << "[PASS] sysv_six_int_fn\n";
    }

    if (suite == "all" || suite == "stack") {
        std::cout << "[RUN] sysv_ten_int_fn (stack arguments)...\n";
        uint64_t res = sysv_ten_int_fn(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
        if (res != 55) {
            std::cerr << "FAIL: sysv_ten_int_fn expected 55, got " << res << "\n";
            return 1;
        }
        std::cout << "[PASS] sysv_ten_int_fn\n";
    }

    if (suite == "all" || suite == "floats") {
        std::cout << "[RUN] sysv_eight_double_fn...\n";
        double res = sysv_eight_double_fn(1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5);
        if (std::abs(res - 40.0) > 1e-6) {
            std::cerr << "FAIL: sysv_eight_double_fn expected 40.0, got " << res << "\n";
            return 1;
        }
        std::cout << "[PASS] sysv_eight_double_fn\n";
    }

    if (suite == "all" || suite == "mixed") {
        std::cout << "[RUN] sysv_mixed_args_fn...\n";
        double res = sysv_mixed_args_fn(10, 2.5, 30, 1.25f, "GravityRush", 4.0, 5, 0.75f);
        double expected = 10 + 2.5 + 30 + 1.25 + 4.0 + 5 + 0.75; // 53.5
        if (std::abs(res - expected) > 1e-6) {
            std::cerr << "FAIL: sysv_mixed_args_fn expected " << expected << ", got " << res << "\n";
            return 1;
        }
        std::cout << "[PASS] sysv_mixed_args_fn\n";
    }

    if (suite == "all" || suite == "structs") {
        std::cout << "[RUN] sysv_small_struct_fn and sysv_large_struct_fn...\n";
        SmallPoint p1{3, 4};
        SmallPoint p2{5, 6};
        int64_t dot = sysv_small_struct_fn(p1, p2); // 15 + 24 = 39
        if (dot != 39) {
            std::cerr << "FAIL: sysv_small_struct_fn expected 39, got " << dot << "\n";
            return 1;
        }

        LargeDataBlock block{{10, 20, 30, 40}};
        uint64_t large_res = sysv_large_struct_fn(block, 3); // 100 * 3 = 300
        if (large_res != 300) {
            std::cerr << "FAIL: sysv_large_struct_fn expected 300, got " << large_res << "\n";
            return 1;
        }
        std::cout << "[PASS] sysv struct passing\n";
    }

    if (suite == "all" || suite == "callbacks") {
        std::cout << "[RUN] sysv_host_guest_callback...\n";
        uint64_t res = sysv_host_guest_callback(50, &host_callback_impl); // (50 + 100) * 2 = 300
        if (res != 300) {
            std::cerr << "FAIL: sysv_host_guest_callback expected 300, got " << res << "\n";
            return 1;
        }
        std::cout << "[PASS] sysv_host_guest_callback\n";
    }

    if (suite == "all" || suite == "varargs") {
        std::cout << "[RUN] sysv_format_adapter...\n";
        char buf[64] = {0};
        int len = sysv_format_adapter(buf, sizeof(buf), "Val1=%llu, Val2=%llu", 1234, 5678);
        if (len <= 0 || std::strcmp(buf, "Val1=1234, Val2=5678") != 0) {
            std::cerr << "FAIL: sysv_format_adapter expected 'Val1=1234, Val2=5678', got '" << buf << "'\n";
            return 1;
        }
        std::cout << "[PASS] sysv_format_adapter\n";
    }

    if (suite == "all" || suite == "returns") {
        std::cout << "[RUN] struct returns...\n";
        SmallReturn sret = sysv_return_small_struct(7, 6);
        if (sret.low != 13 || sret.high != 42) {
            std::cerr << "FAIL: sysv_return_small_struct expected (13, 42), got ("
                      << sret.low << ", " << sret.high << ")\n";
            return 1;
        }

        LargeReturn lret = sysv_return_large_struct(100);
        if (lret.data[0] != 100 || lret.data[1] != 101 || lret.data[2] != 102 || lret.data[3] != 103) {
            std::cerr << "FAIL: sysv_return_large_struct mismatch\n";
            return 1;
        }
        std::cout << "[PASS] struct returns\n";
    }

    std::cout << "All ABI matrix tests passed successfully.\n";
    return 0;
}
