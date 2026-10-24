#include "hle/fiber.h"
#include "hle/kernel.h"
#include "hle/loader/elf.h"
#include "hle/fs.h"
#include "hle/pad.h"
#include "hle/audio.h"
#include "hle/gnm.h"
#include "hle/video.h"
#include "memory/address_space.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>
#include <xmmintrin.h>

namespace {
using namespace gravity;
using hle::Error;
[[noreturn]] void fail(const char* expression, int line) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)
#define ERROR_IS(expression, expected) CHECK((expression).error == (expected))

MEMORY_BASIC_INFORMATION query(void* address) {
    MEMORY_BASIC_INFORMATION info{};
    CHECK(VirtualQuery(address, &info, sizeof(info)) == sizeof(info));
    return info;
}

void memory_tests() {
    memory::AddressSpace address;
    const auto page = memory::AddressSpace::page_size();
    ERROR_IS(address.commit(0, page), Error::invalid_state);
    ERROR_IS(address.reserve(1, page), Error::invalid_argument);
    ERROR_IS(address.reserve(memory::AddressSpace::default_base, 1), Error::invalid_argument);
    CHECK(address.reserve());
    CHECK(reinterpret_cast<std::uintptr_t>(address.base()) == memory::AddressSpace::default_base);
    CHECK(address.size() == 4ULL * 1024 * 1024 * 1024);
    CHECK(query(address.base()).State == MEM_RESERVE);
    ERROR_IS(address.reserve(), Error::invalid_state);
    memory::AddressSpace collision;
    ERROR_IS(collision.reserve(), Error::system_error);
    CHECK(collision.base() == nullptr);
    ERROR_IS(address.commit(1, page), Error::invalid_argument);
    ERROR_IS(address.commit(0, 0), Error::invalid_argument);
    ERROR_IS(address.commit(address.size(), page), Error::out_of_range);
    ERROR_IS(address.commit(std::numeric_limits<std::size_t>::max() - page + 1, page), Error::out_of_range);
    ERROR_IS(address.commit(0, page, static_cast<memory::Protection>(42)), Error::invalid_argument);
    CHECK(address.commit(0, page));
    auto* bytes = static_cast<unsigned char*>(address.base());
    CHECK(bytes[0] == 0 && bytes[page - 1] == 0);
    bytes[0] = 0xA5;
    bytes[page - 1] = 0x5A;
    CHECK(query(bytes).Protect == PAGE_READWRITE);
    CHECK(address.protect(0, page, memory::Protection::read));
    CHECK(query(bytes).Protect == PAGE_READONLY);
    CHECK(bytes[0] == 0xA5);
    CHECK(address.protect(0, page, memory::Protection::execute_read));
    CHECK(query(bytes).Protect == PAGE_EXECUTE_READ);
    CHECK(address.protect(0, page, memory::Protection::none));
    CHECK(query(bytes).Protect == PAGE_NOACCESS);
    // Includes an uncommitted page: VirtualProtect must fail, without committing it.
    ERROR_IS(address.protect(0, page * 2, memory::Protection::read), Error::system_error);
    CHECK(query(bytes + page).State == MEM_RESERVE);
    CHECK(address.decommit(0, page));
    CHECK(query(bytes).State == MEM_RESERVE);
    CHECK(address.commit(0, page));
    CHECK(bytes[0] == 0 && bytes[page - 1] == 0);
    // Exercise a range beyond 32-bit offsets in a six-GiB reservation.
    CHECK(address.release());
    CHECK(address.release());
    CHECK(address.reserve(memory::AddressSpace::default_base, 6ULL * 1024 * 1024 * 1024));
    const auto last = address.size() - page;
    CHECK(address.commit(last, page));
    static_cast<unsigned char*>(address.base())[last] = 0x7F;
    CHECK(static_cast<unsigned char*>(address.base())[last] == 0x7F);
    void* old = address.base();
    CHECK(address.release());
    CHECK(query(old).State == MEM_FREE);
    CHECK(address.base() == nullptr && address.size() == 0);
    { memory::AddressSpace scoped; CHECK(scoped.reserve()); }
    CHECK(address.reserve()); // Destructor released its reservation.
    std::puts("memory: fixed 4/6 GiB, conflict, bounds, W/R/RX/NOACCESS, decommit, cleanup passed");
}

void kernel_tests() {
    hle::Mutex mutex;
    ERROR_IS(mutex.unlock(), Error::permission);
    CHECK(mutex.lock());
    ERROR_IS(mutex.lock(), Error::deadlock);
    ERROR_IS(mutex.try_lock(), Error::busy);
    std::thread wrong_owner([&] {
        ERROR_IS(mutex.try_lock(), Error::busy);
        ERROR_IS(mutex.unlock(), Error::permission);
    });
    wrong_owner.join();
    CHECK(mutex.unlock());
    hle::Mutex recursive(hle::MutexKind::recursive);
    CHECK(recursive.lock());
    CHECK(recursive.try_lock());
    CHECK(recursive.lock());
    CHECK(recursive.unlock());
    CHECK(recursive.unlock());
    std::thread still_locked([&] { ERROR_IS(recursive.try_lock(), Error::busy); });
    still_locked.join();
    CHECK(recursive.unlock());
    CHECK(recursive.try_lock());
    CHECK(recursive.unlock());
    std::uint64_t counter{};
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
        for (int j = 0; j < 25000; ++j) {
            CHECK(mutex.lock()); ++counter; CHECK(mutex.unlock());
        }
    });
    for (auto& worker : workers) worker.join();
    CHECK(counter == 100000);

    hle::EventFlag flags(0xA);
    std::uint64_t observed = 0xFFFF;
    ERROR_IS(flags.wait(0, hle::WaitMode::all, hle::ClearMode::none, observed, 0), Error::invalid_argument);
    ERROR_IS(flags.wait(0x6, hle::WaitMode::all, hle::ClearMode::none, observed, 0), Error::timeout);
    CHECK(observed == 0xFFFF);
    CHECK(flags.wait(0x6, hle::WaitMode::any, hle::ClearMode::matched, observed, 0));
    CHECK(observed == 0xA);
    ERROR_IS(flags.wait(0x2, hle::WaitMode::all, hle::ClearMode::none, observed, 0), Error::timeout);
    CHECK(flags.wait(0x8, hle::WaitMode::all, hle::ClearMode::none, observed, 0));
    CHECK(flags.clear(0x8));
    ERROR_IS(flags.wait(0x8, hle::WaitMode::all, hle::ClearMode::none, observed, 0), Error::timeout);
    std::atomic<bool> started{}, finished{};
    std::thread waiter([&] {
        started = true;
        std::uint64_t got{};
        CHECK(flags.wait(3, hle::WaitMode::all, hle::ClearMode::all, got, 5000));
        CHECK((got & 3) == 3);
        finished = true;
    });
    while (!started) std::this_thread::yield();
    CHECK(flags.set(1));
    CHECK(!finished);
    CHECK(flags.set(2));
    waiter.join();
    ERROR_IS(flags.wait(3, hle::WaitMode::any, hle::ClearMode::none, observed, 0), Error::timeout);
    CHECK(flags.set(8));
    const auto before_timeout = GetTickCount64();
    // Unrelated wakeups must not restart the timeout budget.
    std::atomic<bool> stop{};
    std::thread waker([&] { while (!stop) { CHECK(flags.set(8)); Sleep(1); } });
    ERROR_IS(flags.wait(1, hle::WaitMode::all, hle::ClearMode::none, observed, 30), Error::timeout);
    stop = true;
    waker.join();
    CHECK(GetTickCount64() - before_timeout >= 20);
    hle::EventFlag canceled;
    std::array<std::thread, 3> canceled_waiters;
    for (auto& thread : canceled_waiters) thread = std::thread([&] {
        std::uint64_t got{};
        ERROR_IS(canceled.wait(1, hle::WaitMode::any, hle::ClearMode::none, got, 5000), Error::canceled);
    });
    canceled.cancel();
    for (auto& thread : canceled_waiters) thread.join();
    ERROR_IS(canceled.set(1), Error::canceled);
    ERROR_IS(canceled.clear(1), Error::canceled);

    hle::Semaphore semaphore;
    ERROR_IS(semaphore.wait(0), Error::invalid_state);
    ERROR_IS(semaphore.initialize(2, 1), Error::invalid_argument);
    CHECK(semaphore.initialize(0, 2));
    ERROR_IS(semaphore.initialize(0, 2), Error::invalid_state);
    ERROR_IS(semaphore.wait(0), Error::timeout);
    ERROR_IS(semaphore.post(0), Error::invalid_argument);
    std::thread consumer([&] { CHECK(semaphore.wait(5000)); });
    CHECK(semaphore.post());
    consumer.join();
    std::int32_t previous = -1;
    CHECK(semaphore.post(2, &previous));
    CHECK(previous == 0);
    ERROR_IS(semaphore.post(), Error::overflow);
    CHECK(semaphore.wait(0));
    CHECK(semaphore.wait(0));
    ERROR_IS(semaphore.wait(0), Error::timeout);

    hle::ProcessClock clock;
    CHECK(clock.valid());
    auto previous_time = clock.microseconds();
    for (int i = 0; i < 10000; ++i) {
        const auto now = clock.microseconds(); CHECK(now >= previous_time); previous_time = now;
    }
    Sleep(20);
    CHECK(clock.microseconds() >= 10000);
    std::puts("kernel: owner/recursion, 100000 contested increments, flags, timeout/cancel, semaphore, QPC passed");
}

struct FiberContext {
    hle::FiberScheduler* scheduler;
    hle::Fiber* a;
    hle::Fiber* b;
    int a_steps{};
    int b_steps{};
};

void GRAVITY_SYSV_ABI first(std::uint64_t initial, std::uint64_t argument) {
    auto& ctx = *reinterpret_cast<FiberContext*>(initial);
    CHECK(argument == 0x1122334455667788ULL);
    CHECK(ctx.scheduler->current() == ctx.a);
    std::uint64_t resumed{};
    ERROR_IS(ctx.scheduler->run(*ctx.b, 0, resumed), Error::invalid_state);
    ERROR_IS(ctx.scheduler->switch_to(*ctx.a, 0, resumed), Error::invalid_state);
    ERROR_IS(ctx.a->finalize(), Error::busy);
    ERROR_IS(ctx.scheduler->detach(), Error::busy);
    // The local survives A -> B -> thread -> B -> A.
    volatile std::uint64_t local = 0xFEDCBA9876543210ULL;
    ctx.a_steps = 1;
    CHECK(ctx.scheduler->switch_to(*ctx.b, 0x8877665544332211ULL, resumed));
    CHECK(resumed == 0xABCDEF0123456789ULL);
    CHECK(local == 0xFEDCBA9876543210ULL);
    CHECK(ctx.b_steps == 2);
    ctx.a_steps = 2;
    CHECK(ctx.scheduler->return_to_thread(0xF000000000000042ULL, resumed));
    CHECK(resumed == 0xF100000000000099ULL);
    ctx.a_steps = 3;
}

void GRAVITY_SYSV_ABI second(std::uint64_t initial, std::uint64_t argument) {
    auto& ctx = *reinterpret_cast<FiberContext*>(initial);
    CHECK(argument == 0x8877665544332211ULL);
    CHECK(ctx.scheduler->current() == ctx.b);
    std::uint64_t resumed{};
    ctx.b_steps = 1;
    CHECK(ctx.scheduler->return_to_thread(0x8000000000000001ULL, resumed));
    CHECK(resumed == 0x8000000000000002ULL);
    ctx.b_steps = 2;
    CHECK(ctx.scheduler->switch_to(*ctx.a, 0xABCDEF0123456789ULL, resumed));
    CHECK(resumed == 0x8000000000000003ULL);
    ctx.b_steps = 3;
}

void GRAVITY_SYSV_ABI throws_entry(std::uint64_t, std::uint64_t) {
    throw std::runtime_error("fiber test exception");
}

struct Stress {
    hle::FiberScheduler* scheduler;
    std::uint64_t loops;
};
void GRAVITY_SYSV_ABI stress_entry(std::uint64_t initial, std::uint64_t argument) {
    auto& ctx = *reinterpret_cast<Stress*>(initial);
    volatile double local = 1234.5;
    CHECK(argument == 0);
    const auto old_csr = _mm_getcsr();
    const auto fiber_csr = (old_csr & ~_MM_ROUND_MASK) | _MM_ROUND_DOWN;
    _mm_setcsr(fiber_csr);
    for (std::uint64_t i = 0; i < ctx.loops; ++i) {
        CHECK(local == 1234.5);
        CHECK((_mm_getcsr() & _MM_ROUND_MASK) == _MM_ROUND_DOWN);
        std::uint64_t resumed{};
        CHECK(ctx.scheduler->return_to_thread(i, resumed));
        CHECK(resumed == i + 1);
        local = local + 0.5;
        local = local - 0.5;
    }
    _mm_setcsr(old_csr);
}

void empty_entry_native() {}
void GRAVITY_SYSV_ABI empty_entry(std::uint64_t, std::uint64_t) { empty_entry_native(); }

void fiber_tests() {
    hle::FiberScheduler scheduler;
    hle::Fiber a, b;
    std::uint64_t result = 0xFFFF;
    ERROR_IS(scheduler.run(a, 0, result), Error::invalid_state);
    CHECK(scheduler.attach());
    CHECK(IsThreadAFiber());
    CHECK(hle::FiberScheduler::for_thread() == &scheduler);
    hle::FiberScheduler duplicate;
    ERROR_IS(duplicate.attach(), Error::invalid_state);
    ERROR_IS(scheduler.return_to_thread(0, result), Error::invalid_state);
    ERROR_IS(scheduler.switch_to(a, 0, result), Error::invalid_state);
    ERROR_IS(scheduler.run(a, 0, result), Error::invalid_argument);
    ERROR_IS(a.initialize(scheduler, nullptr, 0), Error::invalid_argument);
    hle::FiberOptions options;
    options.supplied_stack = &options;
    ERROR_IS(a.initialize(scheduler, empty_entry, 0, options), Error::unsupported);
    options.supplied_stack = nullptr;
    options.reserve_size = 1;
    ERROR_IS(a.initialize(scheduler, empty_entry, 0, options), Error::invalid_argument);
    FiberContext ctx{&scheduler, &a, &b};
    CHECK(a.initialize(scheduler, first, reinterpret_cast<std::uintptr_t>(&ctx)));
    CHECK(b.initialize(scheduler, second, reinterpret_cast<std::uintptr_t>(&ctx)));
    ERROR_IS(a.initialize(scheduler, first, 0), Error::invalid_state);
    ERROR_IS(scheduler.detach(), Error::busy);
    std::thread wrong_thread([&] {
        std::uint64_t ignored{};
        ERROR_IS(scheduler.run(a, 0, ignored), Error::wrong_thread);
        ERROR_IS(a.finalize(), Error::wrong_thread);
    });
    wrong_thread.join();
    CHECK(scheduler.run(a, 0x1122334455667788ULL, result));
    CHECK(result == 0x8000000000000001ULL);
    CHECK(a.state() == hle::FiberState::suspended && b.state() == hle::FiberState::suspended);
    ERROR_IS(a.finalize(), Error::busy);
    CHECK(scheduler.run(b, 0x8000000000000002ULL, result));
    CHECK(result == 0xF000000000000042ULL);
    CHECK(ctx.a_steps == 2 && ctx.b_steps == 2);
    CHECK(scheduler.run(a, 0xF100000000000099ULL, result));
    CHECK(a.state() == hle::FiberState::completed && ctx.a_steps == 3 && result == 0);
    ERROR_IS(scheduler.run(a, 0, result), Error::invalid_state);
    CHECK(scheduler.run(b, 0x8000000000000003ULL, result));
    CHECK(b.state() == hle::FiberState::completed && ctx.b_steps == 3);
    CHECK(a.finalize()); CHECK(b.finalize());
    // A returned or throwing callback must never return through Win32 FiberProc.
    CHECK(a.initialize(scheduler, throws_entry, 0));
    ERROR_IS(scheduler.run(a, 0, result), Error::callback_exception);
    CHECK(a.state() == hle::FiberState::completed && a.exception());
    try { std::rethrow_exception(a.exception()); }
    catch (const std::runtime_error& error) { CHECK(std::strcmp(error.what(), "fiber test exception") == 0); }
    CHECK(a.finalize());
    CHECK(a.initialize(scheduler, empty_entry, 0));
    CHECK(a.finalize()); // Never-run fibers can be released.

    Stress stress{&scheduler, 100000};
    const auto root_csr = _mm_getcsr();
    _mm_setcsr((root_csr & ~_MM_ROUND_MASK) | _MM_ROUND_UP);
    CHECK(a.initialize(scheduler, stress_entry, reinterpret_cast<std::uintptr_t>(&stress)));
    for (std::uint64_t i = 0; i < stress.loops; ++i) {
        CHECK(scheduler.run(a, i, result));
        CHECK(result == i);
        CHECK((_mm_getcsr() & _MM_ROUND_MASK) == _MM_ROUND_UP);
    }
    CHECK(scheduler.run(a, stress.loops, result));
    CHECK(a.state() == hle::FiberState::completed);
    CHECK(a.finalize());
    _mm_setcsr(root_csr);
    CHECK(scheduler.detach());
    CHECK(!IsThreadAFiber());
    CHECK(hle::FiberScheduler::for_thread() == nullptr);
    ERROR_IS(scheduler.detach(), Error::invalid_state);

    // Borrow an already-converted thread, and do not undo someone else's conversion.
    void* external = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
    CHECK(external);
    CHECK(scheduler.attach());
    CHECK(a.initialize(scheduler, empty_entry, 0));
    CHECK(scheduler.run(a, 0, result));
    CHECK(a.finalize());
    CHECK(scheduler.detach());
    CHECK(IsThreadAFiber() && GetCurrentFiber() == external);
    CHECK(ConvertFiberToThread());
    std::puts("fibers: SysV uint64, A/B/thread, lifecycle, exceptions, 200000+ switches and MXCSR passed");
}

void loader_tests() {
    using namespace gravity::hle::loader;
    // 1. Invalid / null test
    LoadResult res = load_elf(nullptr, 0);
    CHECK(res.base == 0);
    CHECK(res.entry == 0);

    // 2. Too small buffer
    std::uint8_t dummy[64] = {0};
    res = load_elf(dummy, sizeof(dummy));
    CHECK(res.base == 0);

    // 3. Test with eboot.bin if available
    const char* paths[] = { "CUSA01130/eboot.bin", "../CUSA01130/eboot.bin", "../../CUSA01130/eboot.bin" };
    FILE* f = nullptr;
    for (const char* p : paths) {
        f = std::fopen(p, "rb");
        if (f) break;
    }
    if (f) {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        CHECK(sz > 0x10000);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
        CHECK(std::fread(buf.data(), 1, buf.size(), f) == buf.size());
        std::fclose(f);

        memory::AddressSpace as;
        CHECK(as.reserve(0x2000000000ULL)); // use alternate base to test custom guest_base
        const auto strict = load_self(buf.data(), buf.size(), as, 0x2000000000ULL);
        CHECK(!strict.base && strict.error && !strict.unresolved_symbols.empty());
        CHECK(query(as.base()).State == MEM_RESERVE);
        res = load_self(buf.data(), buf.size(), as, 0x2000000000ULL, true);
        CHECK(res.unresolved_symbols == strict.unresolved_symbols);
        CHECK(res.base == 0x2000000000ULL);
        CHECK(res.entry == 0x2000000000ULL + 0x2F0);
        CHECK(res.is_self == true);
        CHECK(res.num_relocations > 300000);
        CHECK(res.tls.memory_size == 0x140 && res.tls.initial.size() == 4);
        CHECK(res.tls.initial[0] == 0xff);

        // Verify entry point machine code bytes: push rbp; mov rbp, rsp
        const auto* code = reinterpret_cast<const std::uint8_t*>(res.entry);
        CHECK(code[0] == 0x55);
        CHECK(code[1] == 0x48 && code[2] == 0x89 && code[3] == 0xE5);

        // Verify memory protection of code and data
        CHECK(query(reinterpret_cast<void*>(res.base)).Protect == PAGE_EXECUTE_READ);
        CHECK(query(reinterpret_cast<void*>(res.base + 0x15E8000)).Protect == PAGE_READWRITE);

        CHECK(as.release());
        res = load_self(buf.data(), buf.size(), as, 0x2000000000ULL, true, false, true);
        CHECK(res.base && res.tls_fixup_count == 2752);
        CHECK(*reinterpret_cast<const std::uint8_t*>(res.base + 0x60dd) == 0x65);
        CHECK(as.release());
        buf.back() ^= 1; // Metadata outside mapped code still changes the profile identity.
        CHECK(as.reserve(0x2000000000ULL));
        res = load_self(buf.data(), buf.size(), as, 0x2000000000ULL, true, false, true);
        CHECK(!res.base && res.error);
        CHECK(query(as.base()).State == MEM_RESERVE);
        CHECK(as.release());
        std::puts("loader: SELF parsing, segment commit, 300,000+ relocations, opcode check, protection passed");
    } else {
        std::puts("loader: basic validation passed (eboot.bin skipped)");
    }
}

void psarc_tests() {
    using namespace gravity::hle::fs;

    // Test direct archive opening
    const wchar_t* arc_paths[] = {
        L"CUSA01130/arc/arc_0_ep_0_0.psarc",
        L"../CUSA01130/arc/arc_0_ep_0_0.psarc",
        L"../../CUSA01130/arc/arc_0_ep_0_0.psarc"
    };
    PsarcArchive arc;
    bool opened = false;
    for (const wchar_t* p : arc_paths) {
        if (arc.open(p)) {
            opened = true;
            break;
        }
    }
    if (opened) {
        CHECK(arc.is_open());
        CHECK(arc.file_count() == 1951); // 1952 entries minus entry 0 manifest

        // Test single-block decompression
        CHECK(arc.has_file("xml/area_post_az1_bos.xml"));
        auto xml_data = arc.read_file("xml/area_post_az1_bos.xml");
        CHECK(xml_data.size() == 3271);
        std::string_view xml_header(reinterpret_cast<const char*>(xml_data.data()), 14);
        CHECK(xml_header == "<?xml version=");

        // Test multi-block decompression (> 65536 bytes)
        CHECK(arc.has_file("gcd/deco_fukidashi02.gnf"));
        auto gnf_data = arc.read_file("gcd/deco_fukidashi02.gnf");
        CHECK(gnf_data.size() == 512256);
        std::string_view gnf_header(reinterpret_cast<const char*>(gnf_data.data()), 4);
        CHECK(gnf_header == "GNF ");

        arc.close();
        CHECK(!arc.is_open());
    }

    // Test VFS and libSceFios2 APIs
    CHECK(initialize_vfs());
    if (vfs_mounted_archives_count() > 0) {
        CHECK(vfs_mounted_archives_count() == 5);
        CHECK(vfs_total_files_count() > 1000);

        // Open via sceFios2Open
        int32_t fd = sceFios2Open("/app0/xml/area_post_az1_bos.xml", 0, 0);
        CHECK(fd >= 0);

        char buf[32] = {0};
        int32_t bytes_read = sceFios2Read(fd, buf, 14);
        CHECK(bytes_read == 14);
        CHECK(std::string_view(buf, 14) == "<?xml version=");

        // Test lseek SEEK_SET
        int64_t pos = sceFios2Lseek(fd, 0, 0);
        CHECK(pos == 0);
        std::memset(buf, 0, sizeof(buf));
        bytes_read = sceFios2Read(fd, buf, 5);
        CHECK(bytes_read == 5);
        CHECK(std::string_view(buf, 5) == "<?xml");

        // Test lseek SEEK_END
        pos = sceFios2Lseek(fd, 0, 2);
        CHECK(pos == 3271);

        CHECK(sceFios2Close(fd) == 0);

        // Invalid fd
        CHECK(sceFios2Read(fd, buf, 10) == -1);
        CHECK(sceFios2Close(fd) == -1);

        // Non-existent file
        CHECK(sceFios2Open("/app0/non_existent_file.xyz", 0, 0) == -1);

        shutdown_vfs();
        std::puts("psarc: manifest parsing, single/multi-chunk zlib, VFS mount, fios2 read/lseek passed");
    } else {
        shutdown_vfs();
        std::puts("psarc: basic VFS validation passed (archives not found)");
    }
}

void pad_tests() {
    using namespace gravity::hle::pad;

    CHECK(scePadInit() == 0);
    // Double init is safe
    CHECK(scePadInit() == 0);

    ScePadState state{};
    CHECK(scePadGetState(0, &state) == 0);
    // This integration test polls the user's live keyboard; WASD may be held.
    CHECK(state.lx == 0 || state.lx == 128 || state.lx == 255);
    CHECK(state.ly == 0 || state.ly == 128 || state.ly == 255);
    CHECK(state.rx == 128);
    CHECK(state.ry == 128);

    // Verify quaternion is unit length
    float qlen2 = state.motion.quat[0]*state.motion.quat[0] +
                  state.motion.quat[1]*state.motion.quat[1] +
                  state.motion.quat[2]*state.motion.quat[2] +
                  state.motion.quat[3]*state.motion.quat[3];
    CHECK(std::abs(qlen2 - 1.0f) < 1e-4f);

    // Verify gravity vector acceleration magnitude ~ 1.0
    float alen = std::sqrt(state.motion.accel[0]*state.motion.accel[0] +
                           state.motion.accel[1]*state.motion.accel[1] +
                           state.motion.accel[2]*state.motion.accel[2]);
    CHECK(std::abs(alen - 1.0f) < 1e-4f);

    ScePadData data{};
    CHECK(scePadGetState2(0, &data) == 0);
    CHECK(data.l2 == 0 || data.l2 == 255);
    CHECK(data.r2 == 0 || data.r2 == 255);

    ScePadMotionData motion{};
    CHECK(scePadGetMotionData(0, &motion) == 0);
    CHECK(motion.timestamp > 0);

    uint8_t battery = 0;
    bool charging = true;
    CHECK(scePadGetBatteryInfo(0, &battery, &charging) == 0);
    CHECK(battery == 100);
    CHECK(!charging);

    CHECK(scePadEnd() == 0);
    // After end, calls return error
    CHECK(scePadGetState(0, &state) == -1);

    std::puts("pad: init/end lifecycle, quaternion normalization, gravity vector, sticks/buttons passed");
}

void audio_tests() {
    using namespace gravity::hle::audio;

    // Test AudioOut open / query / release
    uint64_t handle = 0;
    auto status = sceAudioOutOpen(48000, 2, 16, 0, &handle);
    if (status) {
        CHECK(handle != 0);
        uint32_t writeable = 0;
        CHECK(sceAudioOutGetWriteableSize(handle, &writeable));
        CHECK(writeable > 0);

        uint64_t delay = 0;
        CHECK(sceAudioOutGetDelay(handle, &delay));

        std::vector<std::uint8_t> oversized(writeable + 4);
        ERROR_IS(sceAudioOutOutput(handle, oversized.data(), static_cast<uint32_t>(oversized.size())), hle::Error::busy);
        ERROR_IS(sceAudioOutOutput(handle, oversized.data(), 3), hle::Error::invalid_argument);
        CHECK(sceAudioOutOutput(handle, oversized.data(), 64));
        CHECK(sceAudioOutRelease(handle));
        std::puts("audio: WASAPI 48kHz audio out open/query/release passed");
    } else {
        std::puts("SKIP: WASAPI device unavailable; playback was not verified");
        std::exit(77);
    }
}

void gnm_tests() {
    using namespace gravity::hle::gnm;

    CHECK(sceGnmInitialize());
    // Test MRT configuration including unbound slot (0)
    sceGnmSetRenderTarget(0x1000000000ULL, 1920, 1080, 1920 * 4, 1, 0);
    sceGnmSetRenderTarget(0, 0, 0, 0, 0, 1);
    sceGnmSetDepthStencilTarget(0x1000100000ULL, 1920, 1080, 1920 * 4, 2);

    sceGnmSetClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    sceGnmSetViewport(0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 1.0f);
    sceGnmSetScissor(0, 0, 1920, 1080);

    sceGnmSetBlendState(1, 1, 0, 0, 1, 0, 0);
    sceGnmSetDepthStencilState(1, 1, 1, 0, 0, 0, 0, 0, 0);

    // Test primitive draw and commit
    sceGnmDrawPrimitive(1, 36, 1, 0, 0, 0);
    CHECK(gnm_total_draw_calls() == 1);
    sceGnmCommitCommands();

    sceGnmShutdown();
    CHECK(!gnm_is_initialized());
    std::puts("gnm: host state and draw request counter passed; geometry rendering pending");
}

void video_tests() {
    using namespace gravity::hle::video;

    const auto initialized = sceVideoOutInitialize();
    if (initialized.error == hle::Error::unsupported) {
        std::puts("SKIP: Vulkan presentation unavailable");
        std::exit(77);
    }
    CHECK(initialized);
    CHECK(!video_is_headless());
    CHECK(video_is_initialized());

    CHECK(sceVideoOutSetMode(1920, 1080, 60));
    uint32_t w = 0, h = 0, r = 0;
    sceVideoOutGetMode(&w, &h, &r);
    CHECK(w == 1920 && h == 1080 && r == 60);

    sceVideoOutSetFrameBuf(0x1000000000ULL, 1920, 1080, 1920 * 4, 1);
    sceVideoOutWaitVblank();
    CHECK(video_presented_frames() == 1);

    sceVideoOutShutdown();
    CHECK(!video_is_initialized());
    std::puts("video: SDL3/Vulkan initialization, display modes, framebuf, VBlank presentation passed");
}

} // anonymous namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view suite(argv[1]);
    if (suite == "memory") memory_tests();
    else if (suite == "kernel") kernel_tests();
    else if (suite == "fibers") fiber_tests();
    else if (suite == "loader") loader_tests();
    else if (suite == "psarc") psarc_tests();
    else if (suite == "pad") pad_tests();
    else if (suite == "audio") audio_tests();
    else if (suite == "gnm") gnm_tests();
    else if (suite == "video") video_tests();
    else return 2;
    return 0;
}
