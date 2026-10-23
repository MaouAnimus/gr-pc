#include "hle/fiber.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

using namespace gravity;
using hle::Error;

[[noreturn]] void fail(const char* expression, int line) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)
#define ERROR_IS(expression, expected) CHECK((expression).error == (expected))

// =========================================================================
// Test 1: Authentic SceFiber binary layout and magics
// =========================================================================

void test_fiber_layout_and_magics() {
    // Exact size check: 112 bytes (0x70)
    CHECK(sizeof(hle::orbis::SceFiber) == 112);

    // Exact offset verification against Orbis OS specification
    CHECK(offsetof(hle::orbis::SceFiber, magic_start) == 0x00);
    CHECK(offsetof(hle::orbis::SceFiber, state) == 0x04);
    CHECK(offsetof(hle::orbis::SceFiber, entry) == 0x08);
    CHECK(offsetof(hle::orbis::SceFiber, arg_on_initialize) == 0x10);
    CHECK(offsetof(hle::orbis::SceFiber, addr_context) == 0x18);
    CHECK(offsetof(hle::orbis::SceFiber, size_context) == 0x20);
    CHECK(offsetof(hle::orbis::SceFiber, name) == 0x28);
    CHECK(offsetof(hle::orbis::SceFiber, context) == 0x48);
    CHECK(offsetof(hle::orbis::SceFiber, flags) == 0x50);
    CHECK(offsetof(hle::orbis::SceFiber, reserved) == 0x54);
    CHECK(offsetof(hle::orbis::SceFiber, context_start) == 0x58);
    CHECK(offsetof(hle::orbis::SceFiber, context_end) == 0x60);
    CHECK(offsetof(hle::orbis::SceFiber, magic_end) == 0x68);
    CHECK(offsetof(hle::orbis::SceFiber, padding_end) == 0x6C);

    // Magics verification
    CHECK(hle::orbis::ORBIS_FIBER_MAGIC_START == 0xdef1649c);
    CHECK(hle::orbis::ORBIS_FIBER_MAGIC_END == 0xb37592a0);

    // OptParam size verification
    CHECK(sizeof(hle::orbis::SceFiberOptParam) == 24);

    std::puts("PASS: test_fiber_layout_and_magics");
}

// =========================================================================
// Test 2: External registry and fiber lifecycle
// =========================================================================

void GRAVITY_SYSV_ABI dummy_entry(std::uint64_t, std::uint64_t) {
    // Empty fiber entry
}

void test_fiber_lifecycle_and_registry() {
    hle::FiberScheduler scheduler;
    CHECK(scheduler.attach());

    hle::orbis::SceFiber fiber{};
    std::vector<uint8_t> dummy_stack(64 * 1024, 0);

    // 1. Uninitialized run must fail with FIBER_EINVAL
    CHECK(hle::sceFiberRun(&fiber, 0, nullptr) == static_cast<int32_t>(hle::orbis::Error::FIBER_EINVAL));

    // 2. Initialize fiber
    int32_t rc = hle::sceFiberInitialize(
        &fiber,
        "TestFiberLifecycle",
        dummy_entry,
        0x1234,
        dummy_stack.data(),
        dummy_stack.size(),
        nullptr
    );
    CHECK(rc == static_cast<int32_t>(hle::orbis::Error::OK));

    // Check authentic structure fields
    CHECK(fiber.magic_start == hle::orbis::ORBIS_FIBER_MAGIC_START);
    CHECK(fiber.magic_end == hle::orbis::ORBIS_FIBER_MAGIC_END);
    CHECK(fiber.state == static_cast<uint32_t>(hle::orbis::FiberState::ready));
    CHECK(fiber.arg_on_initialize == 0x1234);
    CHECK(fiber.addr_context == dummy_stack.data());
    CHECK(fiber.size_context == dummy_stack.size());
    CHECK(fiber.context_start == dummy_stack.data());
    CHECK(fiber.context_end == dummy_stack.data() + dummy_stack.size());
    CHECK(std::strcmp(fiber.name, "TestFiberLifecycle") == 0);

    // Check external registry lookup
    CHECK(hle::FiberRegistry::instance().get(&fiber) != nullptr);

    // 3. Double-initialization must be rejected
    rc = hle::sceFiberInitialize(
        &fiber,
        "TestFiberDuplicate",
        dummy_entry,
        0x5678,
        dummy_stack.data(),
        dummy_stack.size(),
        nullptr
    );
    CHECK(rc == static_cast<int32_t>(hle::orbis::Error::FIBER_EINVAL));

    // 4. Corrupted magics must fail operations
    fiber.magic_start = 0xDEADBEEF;
    CHECK(hle::sceFiberRun(&fiber, 0, nullptr) == static_cast<int32_t>(hle::orbis::Error::FIBER_EINVAL));
    fiber.magic_start = hle::orbis::ORBIS_FIBER_MAGIC_START; // Restore

    // 5. Run to completion
    uint64_t returned = 0;
    rc = hle::sceFiberRun(&fiber, 0, &returned);
    CHECK(rc == static_cast<int32_t>(hle::orbis::Error::OK));
    CHECK(fiber.state == static_cast<uint32_t>(hle::orbis::FiberState::completed));

    // 6. Finalize fiber
    rc = hle::sceFiberFinalize(&fiber);
    CHECK(rc == static_cast<int32_t>(hle::orbis::Error::OK));

    // Verify cleanup in guest struct and registry
    CHECK(fiber.magic_start == 0);
    CHECK(fiber.magic_end == 0);
    CHECK(fiber.state == static_cast<uint32_t>(hle::orbis::FiberState::empty));
    CHECK(hle::FiberRegistry::instance().get(&fiber) == nullptr);

    CHECK(scheduler.detach());
    std::puts("PASS: test_fiber_lifecycle_and_registry");
}

// =========================================================================
// Test 3: Stack integrity without shadow copies
// =========================================================================

struct StackVerifyState {
    hle::orbis::SceFiber* fiber_a;
    hle::orbis::SceFiber* fiber_b;
    std::atomic<bool> a_passed{false};
    std::atomic<bool> b_passed{false};
};

void GRAVITY_SYSV_ABI fiber_stack_worker_a(std::uint64_t initial, std::uint64_t) {
    auto* state = reinterpret_cast<StackVerifyState*>(initial);

    // Allocate stack buffer and fill with known pattern
    volatile uint64_t stack_canary_array[32];
    for (size_t i = 0; i < 32; ++i) {
        stack_canary_array[i] = 0xAAAAAAAAAAAAAAAAULL ^ (i * 0x1111ULL);
    }

    // Switch to Fiber B
    uint64_t resumed = 0;
    int32_t rc = hle::sceFiberSwitch(state->fiber_b, 0x111, &resumed);
    CHECK(rc == static_cast<int32_t>(hle::orbis::Error::OK));
    CHECK(resumed == 0x222);

    // Verify stack canaries after returning from B
    for (size_t i = 0; i < 32; ++i) {
        CHECK(stack_canary_array[i] == (0xAAAAAAAAAAAAAAAAULL ^ (i * 0x1111ULL)));
    }

    // Return to main thread
    rc = hle::sceFiberReturnToThread(0x333, &resumed);
    CHECK(rc == static_cast<int32_t>(hle::orbis::Error::OK));
    CHECK(resumed == 0x444);

    // Verify stack canaries after returning from main thread
    for (size_t i = 0; i < 32; ++i) {
        CHECK(stack_canary_array[i] == (0xAAAAAAAAAAAAAAAAULL ^ (i * 0x1111ULL)));
    }

    state->a_passed = true;
}

void GRAVITY_SYSV_ABI fiber_stack_worker_b(std::uint64_t initial, std::uint64_t) {
    auto* state = reinterpret_cast<StackVerifyState*>(initial);

    // Allocate distinct stack buffer and fill with distinct pattern
    volatile uint64_t stack_canary_array[32];
    for (size_t i = 0; i < 32; ++i) {
        stack_canary_array[i] = 0xBBBBBBBBBBBBBBBBULL ^ (i * 0x2222ULL);
    }

    // Switch back to Fiber A
    uint64_t resumed = 0;
    int32_t rc = hle::sceFiberSwitch(state->fiber_a, 0x222, &resumed);
    CHECK(rc == static_cast<int32_t>(hle::orbis::Error::OK));

    // Verify stack canaries after resuming
    for (size_t i = 0; i < 32; ++i) {
        CHECK(stack_canary_array[i] == (0xBBBBBBBBBBBBBBBBULL ^ (i * 0x2222ULL)));
    }

    state->b_passed = true;
}

void test_fiber_stack_integrity() {
    hle::FiberScheduler scheduler;
    CHECK(scheduler.attach());

    hle::orbis::SceFiber fiber_a{};
    hle::orbis::SceFiber fiber_b{};
    std::vector<uint8_t> stack_a(64 * 1024, 0);
    std::vector<uint8_t> stack_b(64 * 1024, 0);

    StackVerifyState state{&fiber_a, &fiber_b, false, false};

    CHECK(hle::sceFiberInitialize(
        &fiber_a, "FiberA", fiber_stack_worker_a,
        reinterpret_cast<uint64_t>(&state), stack_a.data(), stack_a.size(), nullptr
    ) == 0);

    CHECK(hle::sceFiberInitialize(
        &fiber_b, "FiberB", fiber_stack_worker_b,
        reinterpret_cast<uint64_t>(&state), stack_b.data(), stack_b.size(), nullptr
    ) == 0);

    uint64_t ret = 0;
    // 1. Run Fiber A (which sets up stack, switches to B, B sets up stack, switches back to A, A returns to thread)
    CHECK(hle::sceFiberRun(&fiber_a, 0, &ret) == 0);
    CHECK(ret == 0x333);

    // 2. Resume Fiber A to completion
    CHECK(hle::sceFiberRun(&fiber_a, 0x444, &ret) == 0);
    CHECK(state.a_passed.load());

    // 3. Resume Fiber B to completion
    CHECK(hle::sceFiberRun(&fiber_b, 0x555, &ret) == 0);
    CHECK(state.b_passed.load());

    CHECK(hle::sceFiberFinalize(&fiber_a) == 0);
    CHECK(hle::sceFiberFinalize(&fiber_b) == 0);

    CHECK(scheduler.detach());
    std::puts("PASS: test_fiber_stack_integrity");
}

// =========================================================================
// Test 4: Fiber TLS isolation on single thread
// =========================================================================

struct FiberTlsContext {
    hle::orbis::SceFiber* fiber_1;
    hle::orbis::SceFiber* fiber_2;
    std::atomic<bool> success{false};
};

void GRAVITY_SYSV_ABI tls_fiber_entry_1(std::uint64_t initial, std::uint64_t) {
    auto* ctx = reinterpret_cast<FiberTlsContext*>(initial);
    DWORD slot = hle::sceFiberGetTlsSlot();
    CHECK(slot != TLS_OUT_OF_INDEXES);

    auto* tls_block = static_cast<uint8_t*>(TlsGetValue(slot));
    CHECK(tls_block != nullptr);

    // Verify template data copied
    CHECK(std::memcmp(tls_block, "ORBIS_TLS_TEMPLATE_DATA_TEST_PATTERN", 36) == 0);

    // Write fiber 1 marker at offset 64 (BSS area)
    *reinterpret_cast<uint64_t*>(tls_block + 64) = 0x1111222233334444ULL;

    // Switch to fiber 2
    uint64_t resumed = 0;
    CHECK(hle::sceFiberSwitch(ctx->fiber_2, 10, &resumed) == 0);
    CHECK(resumed == 20);

    // Ensure slot still points to fiber 1's TLS block and value preserved
    auto* active_tls = static_cast<uint8_t*>(TlsGetValue(slot));
    CHECK(active_tls == tls_block);
    CHECK(*reinterpret_cast<uint64_t*>(active_tls + 64) == 0x1111222233334444ULL);

    ctx->success = true;
}

void GRAVITY_SYSV_ABI tls_fiber_entry_2(std::uint64_t initial, std::uint64_t) {
    auto* ctx = reinterpret_cast<FiberTlsContext*>(initial);
    DWORD slot = hle::sceFiberGetTlsSlot();
    CHECK(slot != TLS_OUT_OF_INDEXES);

    auto* tls_block = static_cast<uint8_t*>(TlsGetValue(slot));
    CHECK(tls_block != nullptr);

    // Verify template data copied
    CHECK(std::memcmp(tls_block, "ORBIS_TLS_TEMPLATE_DATA_TEST_PATTERN", 36) == 0);

    // Verify offset 64 is 0 (isolated from fiber 1's write)
    CHECK(*reinterpret_cast<uint64_t*>(tls_block + 64) == 0ULL);

    // Write fiber 2 marker
    *reinterpret_cast<uint64_t*>(tls_block + 64) = 0x5555666677778888ULL;

    // Switch back to fiber 1
    uint64_t resumed = 0;
    CHECK(hle::sceFiberSwitch(ctx->fiber_1, 20, &resumed) == 0);

    // Verify offset 64 is still fiber 2 marker
    CHECK(*reinterpret_cast<uint64_t*>(tls_block + 64) == 0x5555666677778888ULL);
}

void test_fiber_tls_isolation() {
    hle::FiberScheduler scheduler;
    CHECK(scheduler.attach());

    // Setup TLS template
    const char template_data[] = "ORBIS_TLS_TEMPLATE_DATA_TEST_PATTERN";
    hle::sceFiberSetTlsTemplate(
        reinterpret_cast<const uint8_t*>(template_data),
        sizeof(template_data),
        128 // 128 bytes total
    );

    hle::orbis::SceFiber fiber_1{};
    hle::orbis::SceFiber fiber_2{};
    std::vector<uint8_t> stack_1(64 * 1024, 0);
    std::vector<uint8_t> stack_2(64 * 1024, 0);

    FiberTlsContext ctx{&fiber_1, &fiber_2, false};

    CHECK(hle::sceFiberInitialize(
        &fiber_1, "TlsFiber1", tls_fiber_entry_1,
        reinterpret_cast<uint64_t>(&ctx), stack_1.data(), stack_1.size(), nullptr
    ) == 0);

    CHECK(hle::sceFiberInitialize(
        &fiber_2, "TlsFiber2", tls_fiber_entry_2,
        reinterpret_cast<uint64_t>(&ctx), stack_2.data(), stack_2.size(), nullptr
    ) == 0);

    uint64_t ret = 0;
    CHECK(hle::sceFiberRun(&fiber_1, 0, &ret) == 0);
    CHECK(ctx.success.load());

    // When returning to thread, TLS slot should be cleared
    DWORD slot = hle::sceFiberGetTlsSlot();
    CHECK(TlsGetValue(slot) == nullptr);

    // Complete fiber 2
    CHECK(hle::sceFiberRun(&fiber_2, 0, &ret) == 0);

    CHECK(hle::sceFiberFinalize(&fiber_1) == 0);
    CHECK(hle::sceFiberFinalize(&fiber_2) == 0);

    CHECK(scheduler.detach());
    std::puts("PASS: test_fiber_tls_isolation");
}

// =========================================================================
// Test 5: Concurrent thread and fiber TLS isolation (4 threads in parallel)
// =========================================================================

struct ConcurrentThreadContext {
    int thread_id;
    std::atomic<bool> passed{false};
};

struct ConcurrentFiberPair {
    ConcurrentThreadContext* thread_ctx;
    hle::orbis::SceFiber* fib_a;
    hle::orbis::SceFiber* fib_b;
};

void GRAVITY_SYSV_ABI concurrent_fiber_a(std::uint64_t initial, std::uint64_t) {
    auto* pair = reinterpret_cast<ConcurrentFiberPair*>(initial);
    int tid = pair->thread_ctx->thread_id;

    DWORD fiber_slot = hle::sceFiberGetTlsSlot();
    auto* fib_tls = static_cast<uint8_t*>(TlsGetValue(fiber_slot));
    CHECK(fib_tls != nullptr);

    // Write thread-specific tag in fiber TLS
    uint64_t tag_a = 0xA000000000000000ULL | (static_cast<uint64_t>(tid) << 32);
    *reinterpret_cast<uint64_t*>(fib_tls + 64) = tag_a;

    // Verify Thread TLS
    auto* thread_tls = static_cast<uint64_t*>(hle::ThreadTls::get_current_thread_tls());
    CHECK(thread_tls != nullptr && *thread_tls == (0x77770000ULL | tid));

    for (int iter = 0; iter < 10; ++iter) {
        uint64_t resumed = 0;
        CHECK(hle::sceFiberSwitch(pair->fib_b, iter, &resumed) == 0);
        CHECK(resumed == static_cast<uint64_t>(iter));

        // Re-verify both thread TLS and fiber TLS
        CHECK(TlsGetValue(fiber_slot) == fib_tls);
        CHECK(*reinterpret_cast<uint64_t*>(fib_tls + 64) == tag_a);
        CHECK(*static_cast<uint64_t*>(hle::ThreadTls::get_current_thread_tls()) == (0x77770000ULL | tid));
    }
}

void GRAVITY_SYSV_ABI concurrent_fiber_b(std::uint64_t initial, std::uint64_t) {
    auto* pair = reinterpret_cast<ConcurrentFiberPair*>(initial);
    int tid = pair->thread_ctx->thread_id;

    DWORD fiber_slot = hle::sceFiberGetTlsSlot();
    auto* fib_tls = static_cast<uint8_t*>(TlsGetValue(fiber_slot));
    CHECK(fib_tls != nullptr);

    uint64_t tag_b = 0xB000000000000000ULL | (static_cast<uint64_t>(tid) << 32);
    *reinterpret_cast<uint64_t*>(fib_tls + 64) = tag_b;

    for (int iter = 0; iter < 10; ++iter) {
        CHECK(TlsGetValue(fiber_slot) == fib_tls);
        CHECK(*reinterpret_cast<uint64_t*>(fib_tls + 64) == tag_b);
        CHECK(*static_cast<uint64_t*>(hle::ThreadTls::get_current_thread_tls()) == (0x77770000ULL | tid));

        uint64_t resumed = 0;
        CHECK(hle::sceFiberSwitch(pair->fib_a, iter, &resumed) == 0);
    }
}

void run_thread_worker(ConcurrentThreadContext* ctx) {
    hle::FiberScheduler scheduler;
    CHECK(scheduler.attach());

    // Setup thread-local TLS for this thread
    uint64_t thread_tls_val = 0x77770000ULL | ctx->thread_id;
    hle::ThreadTls::set_current_thread_tls(&thread_tls_val);
    CHECK(hle::ThreadTls::get_current_thread_tls() == &thread_tls_val);

    hle::orbis::SceFiber fiber_a{};
    hle::orbis::SceFiber fiber_b{};
    std::vector<uint8_t> stack_a(64 * 1024, 0);
    std::vector<uint8_t> stack_b(64 * 1024, 0);

    ConcurrentFiberPair pair{ctx, &fiber_a, &fiber_b};

    char name_a[32];
    char name_b[32];
    std::snprintf(name_a, sizeof(name_a), "Th%d_FibA", ctx->thread_id);
    std::snprintf(name_b, sizeof(name_b), "Th%d_FibB", ctx->thread_id);

    CHECK(hle::sceFiberInitialize(
        &fiber_a, name_a, concurrent_fiber_a,
        reinterpret_cast<uint64_t>(&pair), stack_a.data(), stack_a.size(), nullptr
    ) == 0);

    CHECK(hle::sceFiberInitialize(
        &fiber_b, name_b, concurrent_fiber_b,
        reinterpret_cast<uint64_t>(&pair), stack_b.data(), stack_b.size(), nullptr
    ) == 0);

    uint64_t ret = 0;
    CHECK(hle::sceFiberRun(&fiber_a, 0, &ret) == 0);
    CHECK(hle::sceFiberRun(&fiber_b, 0, &ret) == 0);

    CHECK(hle::sceFiberFinalize(&fiber_a) == 0);
    CHECK(hle::sceFiberFinalize(&fiber_b) == 0);

    // Verify thread TLS was untouched by fiber operations
    CHECK(*static_cast<uint64_t*>(hle::ThreadTls::get_current_thread_tls()) == thread_tls_val);

    CHECK(scheduler.detach());
    ctx->passed = true;
}

void test_thread_concurrent_tls_isolation() {
    const char template_data[] = "ORBIS_CONCURRENT_THREAD_TLS_TEMPLATE";
    hle::sceFiberSetTlsTemplate(
        reinterpret_cast<const uint8_t*>(template_data),
        sizeof(template_data),
        128
    );

    constexpr int kThreadCount = 4;
    std::vector<ConcurrentThreadContext> contexts(kThreadCount);
    std::vector<std::thread> threads;

    for (int i = 0; i < kThreadCount; ++i) {
        contexts[i].thread_id = i + 1;
        contexts[i].passed = false;
        threads.emplace_back(run_thread_worker, &contexts[i]);
    }

    for (auto& th : threads) {
        th.join();
    }

    for (int i = 0; i < kThreadCount; ++i) {
        CHECK(contexts[i].passed.load());
    }

    std::puts("PASS: test_thread_concurrent_tls_isolation");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string_view test_name = argv[1];
        if (test_name == "layout") {
            test_fiber_layout_and_magics();
            return 0;
        } else if (test_name == "lifecycle") {
            test_fiber_lifecycle_and_registry();
            return 0;
        } else if (test_name == "stack") {
            test_fiber_stack_integrity();
            return 0;
        } else if (test_name == "tls") {
            test_fiber_tls_isolation();
            return 0;
        } else if (test_name == "concurrent") {
            test_thread_concurrent_tls_isolation();
            return 0;
        }
        std::fprintf(stderr, "Unknown test name: %s\n", argv[1]);
        return 1;
    }

    // Default: run all tests
    test_fiber_layout_and_magics();
    test_fiber_lifecycle_and_registry();
    test_fiber_stack_integrity();
    test_fiber_tls_isolation();
    test_thread_concurrent_tls_isolation();

    std::puts("All fiber and TLS tests passed successfully!");
    return 0;
}
