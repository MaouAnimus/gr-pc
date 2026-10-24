#include "hle/registry/adapter_registry.h"
#include "hle/fiber.h"
#include "hle/kernel.h"
#include "hle/pad.h"
#include "hle/video.h"
#include "hle/audio.h"
#include "hle/gnm.h"
#include "hle/system_service.h"
#include "hle/kernel/equeue.h"
#include "hle/kernel/pthread_sync.h"
#include "hle/kernel/pthread.h"
#include "hle/kernel/sema.h"
#include "hle/kernel/file.h"
#include "hle/kernel/rwlock.h"
#include "hle/kernel/direct_memory.h"
#include "hle/fs/fios.h"
#include "hle/playgo.h"
#include "hle/guest_memory.h"
#include <ctime>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <atomic>
#include <iostream>

namespace gravity::hle {

// -------------------------------------------------------------------------
// Forward assembly trampoline for unresolved calls
// -------------------------------------------------------------------------

extern "C" void hle_unresolved_dispatcher_c(uint64_t thunk_id, uintptr_t caller_rip, const uint64_t* arguments) {
    AdapterRegistry::instance().handle_unresolved(thunk_id, caller_rip, arguments);
}

__asm__(
    ".globl hle_unresolved_dispatcher_stub\n"
    "hle_unresolved_dispatcher_stub:\n"
    "  subq $88, %rsp\n"        // Shadow space, six original SysV arguments, alignment.
    "  movq %rdi, 32(%rsp)\n"
    "  movq %rsi, 40(%rsp)\n"
    "  movq %rdx, 48(%rsp)\n"
    "  movq %rcx, 56(%rsp)\n"
    "  movq %r8, 64(%rsp)\n"
    "  movq %r9, 72(%rsp)\n"
    "  movq 88(%rsp), %rdx\n"    // Win64 arg 2: original return address.
    "  movq %r10, %rcx\n"        // Win64 arg 1: thunk ID.
    "  leaq 32(%rsp), %r8\n"     // Win64 arg 3: original SysV arguments.
    "  call hle_unresolved_dispatcher_c\n"
    "  addq $88, %rsp\n"
    "  ud2\n"                    // Trap with STATUS_ILLEGAL_INSTRUCTION
);

extern "C" void hle_unresolved_dispatcher_stub();

// -------------------------------------------------------------------------
// Builtin HLE Adapters Implementation
// -------------------------------------------------------------------------

namespace {

// Stack Canary
uint64_t g_guest_stack_guard = 0x595A4546414C5641ULL; // Canary cookie

extern "C" GRAVITY_SYSV_ABI void hle_stack_chk_fail() {
    std::fprintf(stderr, "\n[FATAL ERROR] Guest stack check failed (__stack_chk_fail)!\n");
    std::fflush(stderr);
    __debugbreak();
    std::abort();
}

// Safe Tracked Memory allocation for guest libc
static std::mutex g_heap_mutex;
static std::unordered_map<void*, std::size_t> g_allocated_ptrs;
static std::unordered_map<void*, void*> g_aligned_to_raw;
static HANDLE g_hle_heap = nullptr;

static void ensure_hle_heap() {
    if (!g_hle_heap) {
        g_hle_heap = HeapCreate(0, 64 * 1024 * 1024, 0);
        if (!g_hle_heap) g_hle_heap = GetProcessHeap();
    }
}

extern "C" GRAVITY_SYSV_ABI void* hle_malloc(std::size_t size) {
    if (size == 0) size = 1;
    ensure_hle_heap();
    void* ptr = HeapAlloc(g_hle_heap, HEAP_ZERO_MEMORY, size);
    if (ptr) {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        g_allocated_ptrs[ptr] = size;
    }
    return ptr;
}

extern "C" GRAVITY_SYSV_ABI void* hle_memalign(std::size_t alignment, std::size_t size) {
    if (size == 0) size = 1;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    // Ensure alignment is power of two
    if ((alignment & (alignment - 1)) != 0) {
        std::size_t a = sizeof(void*);
        while (a < alignment) a <<= 1;
        alignment = a;
    }
    ensure_hle_heap();
    const std::size_t total = size + alignment + sizeof(void*);
    void* raw = HeapAlloc(g_hle_heap, HEAP_ZERO_MEMORY, total);
    if (!raw) return nullptr;

    const uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw);
    const uintptr_t aligned_addr = (raw_addr + sizeof(void*) + (alignment - 1)) & ~(alignment - 1);
    void* aligned_ptr = reinterpret_cast<void*>(aligned_addr);

    {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        g_allocated_ptrs[aligned_ptr] = size;
        g_aligned_to_raw[aligned_ptr] = raw;
    }
    return aligned_ptr;
}

extern "C" GRAVITY_SYSV_ABI void hle_free(void* ptr) {
    if (!ptr) return;
    ensure_hle_heap();
    bool is_tracked = false;
    void* raw_to_free = ptr;
    {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        auto it_aligned = g_aligned_to_raw.find(ptr);
        if (it_aligned != g_aligned_to_raw.end()) {
            raw_to_free = it_aligned->second;
            g_aligned_to_raw.erase(it_aligned);
        }
        auto it = g_allocated_ptrs.find(ptr);
        if (it != g_allocated_ptrs.end()) {
            is_tracked = true;
            g_allocated_ptrs.erase(it);
        }
    }
    if (is_tracked) {
        HeapFree(g_hle_heap, 0, raw_to_free);
    }
}

extern "C" GRAVITY_SYSV_ABI void* hle_calloc(std::size_t count, std::size_t size) {
    std::size_t total = count * size;
    return hle_malloc(total);
}

extern "C" GRAVITY_SYSV_ABI void* hle_realloc(void* ptr, std::size_t size) {
    if (!ptr) return hle_malloc(size);
    if (size == 0) { hle_free(ptr); return nullptr; }
    ensure_hle_heap();
    bool is_aligned = false;
    std::size_t old_size = 0;
    {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        if (g_aligned_to_raw.find(ptr) != g_aligned_to_raw.end()) {
            is_aligned = true;
            auto it = g_allocated_ptrs.find(ptr);
            if (it != g_allocated_ptrs.end()) old_size = it->second;
        }
    }
    if (is_aligned) {
        void* new_ptr = hle_malloc(size);
        if (new_ptr) {
            std::memcpy(new_ptr, ptr, old_size < size ? old_size : size);
            hle_free(ptr);
        }
        return new_ptr;
    }
    bool is_tracked = false;
    {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        auto it = g_allocated_ptrs.find(ptr);
        if (it != g_allocated_ptrs.end()) {
            is_tracked = true;
            g_allocated_ptrs.erase(it);
        }
    }
    if (is_tracked) {
        void* new_ptr = HeapReAlloc(g_hle_heap, HEAP_ZERO_MEMORY, ptr, size);
        if (new_ptr) {
            std::lock_guard<std::mutex> lock(g_heap_mutex);
            g_allocated_ptrs[new_ptr] = size;
        }
        return new_ptr;
    } else {
        void* new_ptr = hle_malloc(size);
        return new_ptr;
    }
}

// Memory operations
extern "C" GRAVITY_SYSV_ABI void* hle_memcpy(void* dest, const void* src, std::size_t n) {
    return std::memcpy(dest, src, n);
}
extern "C" GRAVITY_SYSV_ABI void* hle_memset(void* dest, int c, std::size_t n) {
    return std::memset(dest, c, n);
}
extern "C" GRAVITY_SYSV_ABI int hle_memcmp(const void* s1, const void* s2, std::size_t n) {
    return std::memcmp(s1, s2, n);
}
extern "C" GRAVITY_SYSV_ABI void* hle_memmove(void* dest, const void* src, std::size_t n) {
    return std::memmove(dest, src, n);
}

// String operations
extern "C" GRAVITY_SYSV_ABI std::size_t hle_strlen(const char* s) {
    return s ? std::strlen(s) : 0;
}
extern "C" GRAVITY_SYSV_ABI char* hle_strcpy(char* dest, const char* src) {
    return std::strcpy(dest, src);
}
extern "C" GRAVITY_SYSV_ABI char* hle_strncpy(char* dest, const char* src, std::size_t n) {
    return std::strncpy(dest, src, n);
}
extern "C" GRAVITY_SYSV_ABI int hle_strcmp(const char* s1, const char* s2) {
    return std::strcmp(s1, s2);
}
extern "C" GRAVITY_SYSV_ABI int hle_strncmp(const char* s1, const char* s2, std::size_t n) {
    return std::strncmp(s1, s2, n);
}
extern "C" GRAVITY_SYSV_ABI char* hle_strcat(char* dest, const char* src) {
    return std::strcat(dest, src);
}
extern "C" GRAVITY_SYSV_ABI char* hle_strncat(char* dest, const char* src, std::size_t n) {
    return std::strncat(dest, src, n);
}
extern "C" GRAVITY_SYSV_ABI const char* hle_strchr(const char* s, int c) {
    return std::strchr(s, c);
}
extern "C" GRAVITY_SYSV_ABI const char* hle_strrchr(const char* s, int c) {
    return std::strrchr(s, c);
}
extern "C" GRAVITY_SYSV_ABI const char* hle_strstr(const char* haystack, const char* needle) {
    return std::strstr(haystack, needle);
}

// I/O & Formatting (Bridged without va_start to avoid Windows x64 SysV ABI conflict)
extern "C" GRAVITY_SYSV_ABI int hle_puts(const char* str) {
    return std::puts(str ? str : "");
}
extern "C" GRAVITY_SYSV_ABI int hle_fputs(const char* str, void* stream) {
    (void)stream;
    return std::fputs(str ? str : "", stdout);
}
extern "C" GRAVITY_SYSV_ABI int hle_sprintf(char* buffer, const char* format,
                                            uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                                            uint64_t a5, uint64_t a6) {
    if (!buffer || !format) return -1;
    return std::snprintf(buffer, 4096, format, a1, a2, a3, a4, a5, a6);
}
extern "C" GRAVITY_SYSV_ABI int hle_snprintf(char* buffer, std::size_t count, const char* format,
                                             uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                                             uint64_t a5, uint64_t a6) {
    if (!buffer || !format || count == 0) return 0;
    return std::snprintf(buffer, count, format, a1, a2, a3, a4, a5, a6);
}
extern "C" GRAVITY_SYSV_ABI int hle_vsprintf(char* buffer, const char* format, void* va) {
    (void)va;
    if (!buffer || !format) return -1;
    return std::strcpy(buffer, format) ? static_cast<int>(std::strlen(buffer)) : 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_vsnprintf(char* buffer, std::size_t count, const char* format, void* va) {
    (void)va;
    if (!buffer || !format || count == 0) return 0;
    std::strncpy(buffer, format, count - 1);
    buffer[count - 1] = '\0';
    return static_cast<int>(std::strlen(buffer));
}
extern "C" GRAVITY_SYSV_ABI int hle_printf(const char* format,
                                           uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                                           uint64_t a5, uint64_t a6) {
    if (!format) return 0;
    return std::printf(format, a1, a2, a3, a4, a5, a6);
}
extern "C" GRAVITY_SYSV_ABI int hle_sscanf(const char* s, const char* format,
                                           void* a1, void* a2, void* a3, void* a4) {
    if (!s || !format) return 0;
    return std::sscanf(s, format, a1, a2, a3, a4);
}

// Flow control & utilities
extern "C" GRAVITY_SYSV_ABI void hle_exit(int status) {
    std::exit(status);
}
extern "C" GRAVITY_SYSV_ABI int hle_atexit(void (*func)()) {
    (void)func;
    // Do not register raw guest pointers with host CRT std::atexit, as calling
    // guest code from host exit handlers will crash outside SysV sandbox.
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_libc_init() {
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_cxa_guard_acquire(uint64_t* guard) {
    if (!guard) return 0;
    auto* b = reinterpret_cast<uint8_t*>(guard);
    if (*b != 0) return 0;
    return 1;
}
extern "C" GRAVITY_SYSV_ABI void hle_cxa_guard_release(uint64_t* guard) {
    if (!guard) return;
    auto* b = reinterpret_cast<uint8_t*>(guard);
    *b = 1;
}
extern "C" GRAVITY_SYSV_ABI int hle_cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    (void)func;
    (void)arg;
    (void)dso;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI void hle_cxx_init_dummy(void* obj) {
    (void)obj;
}
extern "C" GRAVITY_SYSV_ABI void* hle_fopen(const char* path, const char* mode) {
    (void)path;
    (void)mode;
    return nullptr;
}
extern "C" GRAVITY_SYSV_ABI int hle_fclose(void* fp) {
    (void)fp;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI float hle_kernel_sincosf(int mode, int unused, float x) {
    (void)unused;
    return (mode == 1) ? std::sin(x) : std::cos(x);
}
int encoded_pthread_error(int error) { return error ? static_cast<int>(0x80020000U + error) : 0; }
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadGetprio(uint64_t thread, int* priority) {
    return encoded_pthread_error(pthread::getprio(thread, priority));
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadAttrInit(void* attr) {
    return encoded_pthread_error(pthread::attr_init(static_cast<pthread::Opaque*>(attr)));
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadAttrSetstacksize(void* attr, std::size_t size) {
    return encoded_pthread_error(pthread::attr_setstacksize(static_cast<pthread::Opaque*>(attr), size));
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadCreate(pthread::Opaque* thread, const pthread::Opaque* attr,
    pthread::Entry entry, void* argument, const char* name) {
    return encoded_pthread_error(pthread::create(thread, attr, entry, argument, name));
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadSetprio(pthread::Opaque thread, int priority) {
    return encoded_pthread_error(pthread::setprio(thread, priority));
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadJoin(pthread::Opaque thread, void** result) {
    return encoded_pthread_error(pthread::join(thread, result));
}
extern "C" GRAVITY_SYSV_ABI int hle_sceSysmoduleLoadModule(uint32_t id) {
    std::fprintf(stderr, "[Sysmodule] Load id=0x%x unavailable: module loader not implemented\n", id);
    return static_cast<int>(id ? 0x8002004eU : 0x805a1000U);
}
extern "C" GRAVITY_SYSV_ABI int hle_sceSysmoduleUnloadModule(uint32_t id) {
    return static_cast<int>(id ? 0x805a1001U : 0x805a1000U);
}
extern "C" GRAVITY_SYSV_ABI int hle_sceScreenShotDisable() {
    std::fprintf(stderr, "[ScreenShot] Console capture service unavailable on this backend\n");
    return static_cast<int>(0x8002004eU); // ENOSYS: no console capture service.
}
extern "C" GRAVITY_SYSV_ABI int hle_sceScreenShotSetParam(const void* param) {
    (void)param;
    return static_cast<int>(0x8002004eU);
}
extern "C" GRAVITY_SYSV_ABI int hle_sceScreenShotEnable() {
    std::fprintf(stderr, "[ScreenShot] Enable unavailable (ENOSYS)\n");
    return static_cast<int>(0x8002004eU);
}
extern "C" GRAVITY_SYSV_ABI int hle_sceScreenShotSetOverlayImage(const char* path, int32_t x, int32_t y) {
    (void)path;
    std::fprintf(stderr, "[ScreenShot] Overlay requested at (%d,%d); capture backend unavailable (ENOSYS)\n", x, y);
    return static_cast<int>(0x8002004eU);
}
extern "C" GRAVITY_SYSV_ABI int hle_rand() {
    return std::rand();
}
extern "C" GRAVITY_SYSV_ABI void hle_srand(unsigned int seed) {
    std::srand(seed);
}
extern "C" GRAVITY_SYSV_ABI void hle_qsort(void* base, std::size_t num, std::size_t size,
                                           int (*compar)(const void*, const void*)) {
    std::qsort(base, num, size, compar);
}
extern "C" GRAVITY_SYSV_ABI void* hle_bsearch(const void* key, const void* base,
                                              std::size_t num, std::size_t size,
                                              int (*compar)(const void*, const void*)) {
    return std::bsearch(key, base, num, size, compar);
}

// Standard Math
extern "C" GRAVITY_SYSV_ABI float hle_sinf(float x) { return std::sin(x); }
extern "C" GRAVITY_SYSV_ABI float hle_cosf(float x) { return std::cos(x); }
extern "C" GRAVITY_SYSV_ABI float hle_tanf(float x) { return std::tan(x); }
extern "C" GRAVITY_SYSV_ABI float hle_asinf(float x) { return std::asin(x); }
extern "C" GRAVITY_SYSV_ABI float hle_acosf(float x) { return std::acos(x); }
extern "C" GRAVITY_SYSV_ABI float hle_atanf(float x) { return std::atan(x); }
extern "C" GRAVITY_SYSV_ABI float hle_atan2f(float y, float x) { return std::atan2(y, x); }
extern "C" GRAVITY_SYSV_ABI float hle_powf(float base, float exp) { return std::pow(base, exp); }
extern "C" GRAVITY_SYSV_ABI float hle_expf(float x) { return std::exp(x); }

extern "C" GRAVITY_SYSV_ABI double hle_sin(double x) { return std::sin(x); }
extern "C" GRAVITY_SYSV_ABI double hle_cos(double x) { return std::cos(x); }
extern "C" GRAVITY_SYSV_ABI double hle_tan(double x) { return std::tan(x); }
extern "C" GRAVITY_SYSV_ABI double hle_asin(double x) { return std::asin(x); }
extern "C" GRAVITY_SYSV_ABI double hle_acos(double x) { return std::acos(x); }
extern "C" GRAVITY_SYSV_ABI double hle_atan(double x) { return std::atan(x); }
extern "C" GRAVITY_SYSV_ABI double hle_atan2(double y, double x) { return std::atan2(y, x); }
extern "C" GRAVITY_SYSV_ABI double hle_pow(double base, double exp) { return std::pow(base, exp); }
extern "C" GRAVITY_SYSV_ABI double hle_exp(double x) { return std::exp(x); }
extern "C" GRAVITY_SYSV_ABI double hle_tanh(double x) { return std::tanh(x); }
extern "C" GRAVITY_SYSV_ABI double hle_fmod(double x, double y) { return std::fmod(x, y); }

// libkernel process time & clock
extern "C" GRAVITY_SYSV_ABI uint64_t hle_sceKernelGetProcessTime() {
    static const auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now - start).count());
}

struct OrbisTimeval {
    int64_t tv_sec;
    int64_t tv_usec;
};
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelGettimeofday(OrbisTimeval* tv, void* tz) {
    (void)tz;
    if (tv) {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(epoch - seconds);
        tv->tv_sec = seconds.count();
        tv->tv_usec = micros.count();
    }
    return 0;
}

extern "C" GRAVITY_SYSV_ABI uint64_t hle_sceKernelReadTsc() {
    return __rdtsc();
}

extern "C" GRAVITY_SYSV_ABI uint64_t hle_sceKernelGetTscFrequency() {
    return 1600000000ULL; // PS4 Jaguar APU 1.6 GHz
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelUsleep(uint32_t useconds) {
    Sleep(useconds / 1000 == 0 ? 1 : useconds / 1000);
    return 0;
}

static thread_local int s_orbis_errno = 0;
extern "C" GRAVITY_SYSV_ABI int* hle_Error() {
    return &s_orbis_errno;
}
extern "C" GRAVITY_SYSV_ABI int64_t hle_time(int64_t* output) {
    static_assert(sizeof(std::time_t) == 8);
    const int64_t result = std::time(nullptr);
    if (output && !guest_memory::write(output, &result, sizeof(result))) {
        s_orbis_errno = 14;
        return -1;
    }
    return result;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_getpid() { return static_cast<int32_t>(GetCurrentProcessId()); }

extern "C" GRAVITY_SYSV_ABI uint64_t hle_posix_pthread_self() { return pthread::self(); }

extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_getthreadid_np() {
    return static_cast<int32_t>(GetCurrentThreadId());
}

// libkernel uses encoded errors; libScePosix uses positive guest errno.
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePthreadMutexInit(void* mutex, const void* attr, const char* name) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_scePthreadMutexInit\n"); }
    (void)name;
    const int result = pthread_sync::posix_mutex_init(static_cast<pthread_sync::Opaque*>(mutex),
        static_cast<const pthread_sync::Opaque*>(attr));
    return result ? static_cast<int32_t>(0x80020000U + result) : 0;
}

// libkernel EventFlags
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelCreateEventFlag(void* ef, const char* name, uint32_t attr,
                                                                 uint64_t initPattern, const void* opt) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceKernelCreateEventFlag\n"); }
    (void)ef; (void)name; (void)attr; (void)initPattern; (void)opt;
    static std::atomic<int32_t> s_next_ef{1};
    return s_next_ef.fetch_add(1);
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelDeleteEventFlag(int32_t ef) {
    (void)ef;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelSetEventFlag(int32_t ef, uint64_t pattern) {
    (void)ef; (void)pattern;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelClearEventFlag(int32_t ef, uint64_t pattern) {
    (void)ef; (void)pattern;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelWaitEventFlag(int32_t ef, uint64_t pattern, uint32_t mode,
                                                               uint64_t* resultPattern, void* timeout) {
    (void)ef; (void)mode; (void)timeout;
    if (resultPattern) *resultPattern = pattern;
    return 0;
}

// -------------------------------------------------------------------------
// libSceVideoOut
// -------------------------------------------------------------------------
namespace {
struct VideoOutState {
    std::mutex mutex;
    int32_t handle{1};
    bool opened{false};
    uint64_t flip_equeue{0};
    void* flip_udata{nullptr};
    std::array<void*, 16> registered_buffers{};
    int32_t num_buffers{0};
    int32_t flip_rate{0};
    int32_t current_buffer{0};
    uint64_t flip_count{0};

    void reset() {
        opened = false;
        flip_equeue = 0;
        flip_udata = nullptr;
        registered_buffers.fill(nullptr);
        num_buffers = 0;
        flip_rate = 0;
        current_buffer = 0;
        flip_count = 0;
    }
};
static VideoOutState g_video_out;
} // anonymous namespace

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutOpen(int32_t userId, int32_t type, int32_t index, const void* param) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceVideoOutOpen (userId=%d, type=%d, index=%d)\n", userId, type, index); }
    (void)param;
    std::lock_guard lock(g_video_out.mutex);
    g_video_out.opened = true;
    std::fprintf(stderr, "[VideoOut] Display port opened handle=%d\n", g_video_out.handle);
    return g_video_out.handle;
}

extern "C" GRAVITY_SYSV_ABI void hle_sceVideoOutSetBufferAttribute(
    void* attribute, uint32_t pixelFormat, uint32_t tilingMode, uint32_t aspectRatio,
    uint32_t width, uint32_t height, uint32_t pitchInPixel)
{
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceVideoOutSetBufferAttribute (%ux%u, fmt=0x%x, pitch=%u)\n", width, height, pixelFormat, pitchInPixel); }
    struct Attribute {
        uint32_t format, tiling, aspect, width, height, pitch;
        uint64_t reserved[2];
    };
    static_assert(sizeof(Attribute) == 40);
    const Attribute value{pixelFormat, tilingMode, aspectRatio, width, height, pitchInPixel, {0, 0}};
    if (!guest_memory::write(attribute, &value, sizeof(value)))
        std::fprintf(stderr, "[VideoOut] SetBufferAttribute: invalid output address\n");
    if (width && height) {
        (void)video::sceVideoOutSetMode(width, height, 60);
    }
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutRegisterBuffers(
    int32_t handle, int32_t startIndex, void* const* addresses, int32_t bufferNum, const void* attribute)
{
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceVideoOutRegisterBuffers (start=%d, count=%d)\n", startIndex, bufferNum); }
    (void)attribute;
    std::lock_guard lock(g_video_out.mutex);
    if (!g_video_out.opened || handle != g_video_out.handle) {
        return static_cast<int32_t>(0x8029000bU); // SCE_VIDEO_OUT_ERROR_INVALID_HANDLE
    }
    if (bufferNum <= 0 || bufferNum > 16 || startIndex < 0 || startIndex + bufferNum > 16) {
        return static_cast<int32_t>(0x80290003U); // SCE_VIDEO_OUT_ERROR_INVALID_VALUE
    }
    if (!guest_memory::accessible(addresses, static_cast<std::size_t>(bufferNum) * sizeof(void*), false)) {
        return static_cast<int32_t>(0x80290003U);
    }
    for (int32_t i = 0; i < bufferNum; ++i) {
        void* buf_ptr = nullptr;
        if (guest_memory::read(addresses + i, &buf_ptr, sizeof(buf_ptr))) {
            g_video_out.registered_buffers[static_cast<std::size_t>(startIndex + i)] = buf_ptr;
        }
    }
    g_video_out.num_buffers = (std::max)(g_video_out.num_buffers, startIndex + bufferNum);
    std::fprintf(stderr, "[VideoOut] Registered %d display buffers (total=%d)\n",
                 bufferNum, g_video_out.num_buffers);
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutSetFlipRate(int32_t handle, int32_t rate) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceVideoOutSetFlipRate (rate=%d)\n", rate); }
    std::lock_guard lock(g_video_out.mutex);
    if (!g_video_out.opened || handle != g_video_out.handle) {
        return static_cast<int32_t>(0x8029000bU);
    }
    g_video_out.flip_rate = rate;
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutAddFlipEvent(int32_t eq, int32_t handle, void* udata) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceVideoOutAddFlipEvent (eq=0x%x, handle=%d, udata=%p)\n", eq, handle, udata); }
    std::lock_guard lock(g_video_out.mutex);
    if (!g_video_out.opened || handle != g_video_out.handle) {
        return static_cast<int32_t>(0x8029000bU);
    }
    g_video_out.flip_equeue = static_cast<uint64_t>(eq);
    g_video_out.flip_udata = udata;
    std::fprintf(stderr, "[VideoOut] Registered flip event on equeue 0x%llx\n",
                 static_cast<unsigned long long>(g_video_out.flip_equeue));
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutSubmitFlip(int32_t handle, int32_t bufferIndex, int32_t flipMode, int64_t flipArg) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceVideoOutSubmitFlip (buffer=%d, mode=%d, arg=%lld)\n",
        bufferIndex, flipMode, static_cast<long long>(flipArg)); }
    uint64_t eq = 0;
    void* udata = nullptr;
    {
        std::lock_guard lock(g_video_out.mutex);
        if (!g_video_out.opened || handle != g_video_out.handle) {
            return static_cast<int32_t>(0x8029000bU);
        }
        g_video_out.current_buffer = bufferIndex;
        ++g_video_out.flip_count;
        eq = g_video_out.flip_equeue;
        udata = g_video_out.flip_udata;
    }

    if (video::video_is_initialized()) {
        video::sceVideoOutWaitVblank();
    }

    if (eq != 0) {
        equeue::Event event{
            .ident = static_cast<std::uint64_t>(handle),
            .filter = 0,
            .flags = 0,
            .fflags = 0,
            .data = flipArg,
            .udata = udata
        };
        (void)equeue::post(eq, event);
    }
    return 0;
}

// libScePad
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePadOpen(int32_t userId, int32_t type, int32_t index, const void* param) {
    (void)userId; (void)type; (void)index; (void)param;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePadRead(int32_t handle, void* data, int32_t count) {
    (void)handle;
    if (data && count > 0) std::memset(data, 0, count * 48);
    return count;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePadSetVibration(int32_t handle, const void* param) {
    (void)handle; (void)param;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePadSetLightBar(int32_t handle, const void* color) {
    (void)handle; (void)color;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePadResetOrientation(int32_t handle) {
    (void)handle;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePadSetTiltCorrectionState(int32_t handle, int32_t enable) {
    (void)handle; (void)enable;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePadGetControllerInformation(int32_t handle, void* info) {
    (void)handle;
    if (info) std::memset(info, 0, 32);
    return 0;
}

// libSceAudioOut
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutInit() {
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutOpen(int32_t userId, int32_t type, int32_t index,
                                                        uint32_t len, uint32_t freq, uint32_t mode) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceAudioOutOpen\n"); }
    (void)userId; (void)type; (void)index; (void)len; (void)freq; (void)mode;
    return 1;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutClose(int32_t handle) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceAudioOutClose\n"); }
    (void)handle;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutOutput(int32_t handle, const void* ptr) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceAudioOutOutput\n"); }
    (void)handle; (void)ptr;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutOutputs(void* params, uint32_t num) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceAudioOutOutputs\n"); }
    (void)params; (void)num;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutSetVolume(int32_t handle, int32_t flag, int32_t* vol) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceAudioOutSetVolume\n"); }
    (void)handle; (void)flag; (void)vol;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutGetPortState(int32_t handle, void* state) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceAudioOutGetPortState\n"); }
    (void)handle;
    if (state) std::memset(state, 0, 16);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutSetMixLevelPadSpk(int32_t handle, int32_t mixLevel) {
    { static std::atomic<unsigned> calls{}; if (calls.fetch_add(1) < 4) std::fprintf(stderr, "[Adapter reached] hle_sceAudioOutSetMixLevelPadSpk\n"); }
    (void)handle; (void)mixLevel;
    return 0;
}

} // anonymous namespace

// -------------------------------------------------------------------------
// AdapterRegistry implementation
// -------------------------------------------------------------------------

AdapterRegistry& AdapterRegistry::instance() noexcept {
    static AdapterRegistry registry;
    return registry;
}

AdapterRegistry::AdapterRegistry() {
    initialize_builtin_adapters();
}

AdapterRegistry::~AdapterRegistry() {
    clear();
}

void AdapterRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_.clear();
    unresolved_metadata_.clear();
    unresolved_thunks_.clear();
    for (void* page : thunk_pages_) {
        VirtualFree(page, 0, MEM_RELEASE);
    }
    thunk_pages_.clear();
    thunks_in_current_page_ = 0;
    builtins_initialized_ = false;
    {
        std::lock_guard vo_lock(g_video_out.mutex);
        g_video_out.reset();
    }
    fios::reset();
}

void AdapterRegistry::register_adapter(std::string_view nid, std::string_view library,
                                      std::string_view name, uintptr_t func_ptr,
                                      bool is_variable) {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_[std::string(library) + "#" + std::string(nid)] = AdapterSymbol{
        std::string(nid), std::string(library), std::string(name), func_ptr, is_variable
    };
}

uintptr_t AdapterRegistry::resolve(std::string_view nid, std::string_view library) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!library.empty()) {
        const auto it = registry_.find(std::string(library) + "#" + std::string(nid));
        return it == registry_.end() ? 0 : it->second.func_ptr;
    }
    uintptr_t found{};
    for (const auto& [key, symbol] : registry_) {
        (void)key;
        if (symbol.nid != nid) continue;
        if (found && found != symbol.func_ptr) return 0; // Ambiguous without a library.
        found = symbol.func_ptr;
    }
    return found;
}

const AdapterSymbol* AdapterRegistry::find(std::string_view nid) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const AdapterSymbol* found{};
    for (const auto& [key, symbol] : registry_) {
        (void)key;
        if (symbol.nid != nid) continue;
        if (found) return nullptr;
        found = &symbol;
    }
    return found;
}

std::size_t AdapterRegistry::size() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return registry_.size();
}

std::size_t AdapterRegistry::unresolved_count() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return unresolved_metadata_.size();
}

void AdapterRegistry::allocate_thunk_page() {
    constexpr std::size_t kPageSize = 4096;
    void* page = VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!page) {
        std::fprintf(stderr, "[FATAL] Failed to allocate executable page for unresolved thunks\n");
        std::abort();
    }
    thunk_pages_.push_back(page);
    thunks_in_current_page_ = 0;
}

uintptr_t AdapterRegistry::get_unresolved_thunk(std::string_view nid, std::string_view library) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = std::string(library) + "#" + std::string(nid);
    auto it = unresolved_thunks_.find(key);
    if (it != unresolved_thunks_.end()) {
        return it->second;
    }

    constexpr std::size_t kThunkSize = 32;
    constexpr std::size_t kThunksPerPage = 4096 / kThunkSize;

    if (thunk_pages_.empty() || thunks_in_current_page_ >= kThunksPerPage) {
        allocate_thunk_page();
    }

    uint64_t thunk_id = unresolved_metadata_.size();
    unresolved_metadata_.push_back(UnresolvedCallInfo{std::string(nid), std::string(library), 0, 0});

    uint8_t* page = static_cast<uint8_t*>(thunk_pages_.back());
    uint8_t* thunk_addr = page + (thunks_in_current_page_ * kThunkSize);
    thunks_in_current_page_++;

    // Emit x86_64 machine code for the thunk:
    // 49 BA <thunk_id 64-bit>          (mov r10, thunk_id)
    // 48 B8 <dispatcher_addr 64-bit>   (mov rax, dispatcher)
    // FF E0                            (jmp rax)
    // CC CC CC CC CC CC CC CC CC CC    (int 3 padding)
    thunk_addr[0] = 0x49; thunk_addr[1] = 0xBA;
    std::memcpy(&thunk_addr[2], &thunk_id, sizeof(thunk_id));

    uint64_t disp_addr = reinterpret_cast<uint64_t>(&hle_unresolved_dispatcher_stub);
    thunk_addr[10] = 0x48; thunk_addr[11] = 0xB8;
    std::memcpy(&thunk_addr[12], &disp_addr, sizeof(disp_addr));

    thunk_addr[20] = 0xFF; thunk_addr[21] = 0xE0;
    std::memset(&thunk_addr[22], 0xCC, 10);

    uintptr_t ptr = reinterpret_cast<uintptr_t>(thunk_addr);
    unresolved_thunks_[key] = ptr;
    return ptr;
}

void AdapterRegistry::set_unresolved_hook(UnresolvedHook hook) {
    std::lock_guard lock(mutex_);
    unresolved_hook_ = std::move(hook);
}
void AdapterRegistry::handle_unresolved(uint64_t thunk_id, uintptr_t caller_rip, const uint64_t* arguments) {
    UnresolvedCallInfo info{};
    UnresolvedHook hook;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (thunk_id < unresolved_metadata_.size()) {
            info = unresolved_metadata_[thunk_id];
            info.caller_rip = caller_rip;
            if (arguments) std::copy_n(arguments, info.arguments.size(), info.arguments.begin());
            if (caller_rip >= guest_base_) {
                info.guest_offset = caller_rip - guest_base_;
            }
        }
        hook = unresolved_hook_;
    }

    std::fprintf(stderr, "\n============================================================\n");
    std::fprintf(stderr, "[HLE FATAL] Call to UNRESOLVED import function!\n");
    std::fprintf(stderr, "  Library:       %s\n", info.library.c_str());
    std::fprintf(stderr, "  NID:           %s\n", info.nid.c_str());
    std::fprintf(stderr, "  Caller RIP:    0x%llX\n", (unsigned long long)caller_rip);
    std::fprintf(stderr, "  SysV args:     %llx %llx %llx %llx %llx %llx\n",
        static_cast<unsigned long long>(info.arguments[0]), static_cast<unsigned long long>(info.arguments[1]),
        static_cast<unsigned long long>(info.arguments[2]), static_cast<unsigned long long>(info.arguments[3]),
        static_cast<unsigned long long>(info.arguments[4]), static_cast<unsigned long long>(info.arguments[5]));
    if (info.guest_offset != 0) {
        std::fprintf(stderr, "  Module Offset: eboot.bin+0x%llX\n", (unsigned long long)info.guest_offset);
    }
    std::fprintf(stderr, "============================================================\n");
    std::fflush(stderr);

    if (hook) {
        hook(info);
    }
}

void AdapterRegistry::initialize_builtin_adapters() {
    if (builtins_initialized_) return;
    builtins_initialized_ = true;

    // ---------------------------------------------------------------------
    // 1. libc (Memory allocation, strings, formatted I/O, math)
    // ---------------------------------------------------------------------
    register_adapter("gQX+4GDQjpM", "libc", "malloc", reinterpret_cast<uintptr_t>(&hle_malloc));
    register_adapter("rcQCUr0EaRU", "libc", "free", reinterpret_cast<uintptr_t>(&hle_free));
    register_adapter("tIhsqj0qsFE", "libc", "free", reinterpret_cast<uintptr_t>(&hle_free));
    register_adapter("2X5agFjKxMc", "libc", "calloc", reinterpret_cast<uintptr_t>(&hle_calloc));
    register_adapter("sqWytnhYdEg", "libc", "realloc", reinterpret_cast<uintptr_t>(&hle_realloc));
    register_adapter("Y7aJ1uydPMo", "libc", "realloc", reinterpret_cast<uintptr_t>(&hle_realloc));
    register_adapter("Ujf3KzMvRmI", "libc", "memalign", reinterpret_cast<uintptr_t>(&hle_memalign));

    register_adapter("hqi8yMOCmG0", "libc", "memcpy", reinterpret_cast<uintptr_t>(&hle_memcpy));
    register_adapter("Q3VBxCXhUHs", "libc", "memcpy", reinterpret_cast<uintptr_t>(&hle_memcpy));
    register_adapter("N2f485TmJms", "libc", "memset", reinterpret_cast<uintptr_t>(&hle_memset));
    register_adapter("8zTFvBIAIN8", "libc", "memset", reinterpret_cast<uintptr_t>(&hle_memset));
    register_adapter("-L+-8F0+gBc", "libc", "memcmp", reinterpret_cast<uintptr_t>(&hle_memcmp));
    register_adapter("DfivPArhucg", "libc", "memcmp", reinterpret_cast<uintptr_t>(&hle_memcmp));
    register_adapter("+P6FRGH4LfA", "libc", "memmove", reinterpret_cast<uintptr_t>(&hle_memmove));

    register_adapter("LTov9gMEqCU", "libc", "strlen", reinterpret_cast<uintptr_t>(&hle_strlen));
    register_adapter("j4ViWNHEgww", "libc", "strlen", reinterpret_cast<uintptr_t>(&hle_strlen));
    register_adapter("FQ9NFbBHb5Y", "libc", "strcpy", reinterpret_cast<uintptr_t>(&hle_strcpy));
    register_adapter("kiZSXIWd9vg", "libc", "strcpy", reinterpret_cast<uintptr_t>(&hle_strcpy));
    register_adapter("8u8lPzUEq+U", "libc", "strncpy", reinterpret_cast<uintptr_t>(&hle_strncpy));
    register_adapter("6sJWiWSRuqk", "libc", "strncpy", reinterpret_cast<uintptr_t>(&hle_strncpy));
    register_adapter("KuOuD58hqn4", "libc", "strcmp", reinterpret_cast<uintptr_t>(&hle_strcmp));
    register_adapter("Ovb2dSJOAuE", "libc", "strcmp", reinterpret_cast<uintptr_t>(&hle_strcmp));
    register_adapter("Av3zjWi64Kw", "libc", "strncmp", reinterpret_cast<uintptr_t>(&hle_strncmp));
    register_adapter("aesyjrHVWy4", "libc", "strncmp", reinterpret_cast<uintptr_t>(&hle_strncmp));
    register_adapter("Ls4tzzhimqQ", "libc", "strcat", reinterpret_cast<uintptr_t>(&hle_strcat));
    register_adapter("kHg45qPC6f0", "libc", "strncat", reinterpret_cast<uintptr_t>(&hle_strncat));
    register_adapter("ob5xAW4ln-0", "libc", "strchr", reinterpret_cast<uintptr_t>(&hle_strchr));
    register_adapter("9yDWMxEFdJU", "libc", "strrchr", reinterpret_cast<uintptr_t>(&hle_strrchr));
    register_adapter("viiwFMaNamA", "libc", "strstr", reinterpret_cast<uintptr_t>(&hle_strstr));

    register_adapter("YQ0navp+YIc", "libc", "puts", reinterpret_cast<uintptr_t>(&hle_puts));
    register_adapter("QrZZdJ8XsX0", "libc", "fputs", reinterpret_cast<uintptr_t>(&hle_fputs));
    register_adapter("tcVi5SivF7Q", "libc", "sprintf", reinterpret_cast<uintptr_t>(&hle_sprintf));
    register_adapter("eLdDw6l0-bU", "libc", "snprintf", reinterpret_cast<uintptr_t>(&hle_snprintf));
    register_adapter("jbz9I9vkqkk", "libc", "vsprintf", reinterpret_cast<uintptr_t>(&hle_vsprintf));
    register_adapter("Q2V+iqvjgC0", "libc", "vsnprintf", reinterpret_cast<uintptr_t>(&hle_vsnprintf));
    register_adapter("hcuQgD53UxM", "libc", "printf", reinterpret_cast<uintptr_t>(&hle_printf));
    register_adapter("1Pk0qZQGeWo", "libc", "sscanf", reinterpret_cast<uintptr_t>(&hle_sscanf));

    register_adapter("uMei1W9uyNo", "libc", "exit", reinterpret_cast<uintptr_t>(&hle_exit));
    register_adapter("8G2LB+A3rzg", "libc", "atexit", reinterpret_cast<uintptr_t>(&hle_atexit));
    register_adapter("bzQExy189ZI", "libc", "libc_init", reinterpret_cast<uintptr_t>(&hle_libc_init));
    register_adapter("3GPpjQdAMTw", "libc", "__cxa_guard_acquire", reinterpret_cast<uintptr_t>(&hle_cxa_guard_acquire));
    register_adapter("9rAeANT2tyE", "libc", "__cxa_guard_release", reinterpret_cast<uintptr_t>(&hle_cxa_guard_release));
    register_adapter("tsvEmnenz48", "libc", "__cxa_atexit", reinterpret_cast<uintptr_t>(&hle_cxa_atexit));
    register_adapter("-Bl9-SZ2noc", "libc", "ios_init", reinterpret_cast<uintptr_t>(&hle_cxx_init_dummy));
    register_adapter("Uw3OTZFPNt4", "libc", "ios_fini", reinterpret_cast<uintptr_t>(&hle_cxx_init_dummy));
    register_adapter("ZtjspkJQ+vw", "libc", "__kernel_sincosf", reinterpret_cast<uintptr_t>(&hle_kernel_sincosf));
    register_adapter("xeYO4u7uyJ0", "libc", "fopen", reinterpret_cast<uintptr_t>(&hle_fopen));
    register_adapter("uodLYyUip20", "libc", "fclose", reinterpret_cast<uintptr_t>(&hle_fclose));
    register_adapter("cpCOXWMgha0", "libc", "rand", reinterpret_cast<uintptr_t>(&hle_rand));
    register_adapter("VPbJwTCgME0", "libc", "srand", reinterpret_cast<uintptr_t>(&hle_srand));
    register_adapter("AEJdIVZTEmo", "libc", "qsort", reinterpret_cast<uintptr_t>(&hle_qsort));
    register_adapter("NesIgTmfF0Q", "libc", "bsearch", reinterpret_cast<uintptr_t>(&hle_bsearch));

    register_adapter("ZE6RNL+eLbk", "libc", "tanf", reinterpret_cast<uintptr_t>(&hle_tanf));
    register_adapter("GZWjF-YIFFk", "libc", "asinf", reinterpret_cast<uintptr_t>(&hle_asinf));
    register_adapter("QI-x0SL8jhw", "libc", "acosf", reinterpret_cast<uintptr_t>(&hle_acosf));
    register_adapter("weDug8QD-lE", "libc", "atanf", reinterpret_cast<uintptr_t>(&hle_atanf));
    register_adapter("EH-x713A99c", "libc", "atan2f", reinterpret_cast<uintptr_t>(&hle_atan2f));
    register_adapter("1D0H2KNjshE", "libc", "powf", reinterpret_cast<uintptr_t>(&hle_powf));
    register_adapter("8zsu04XNsZ4", "libc", "expf", reinterpret_cast<uintptr_t>(&hle_expf));
    register_adapter("T7uyNqP7vQA", "libc", "tan", reinterpret_cast<uintptr_t>(&hle_tan));
    register_adapter("7Ly52zaL44Q", "libc", "asin", reinterpret_cast<uintptr_t>(&hle_asin));
    register_adapter("JBcgYuW8lPU", "libc", "acos", reinterpret_cast<uintptr_t>(&hle_acos));
    register_adapter("OXmauLdQ8kY", "libc", "atan", reinterpret_cast<uintptr_t>(&hle_atan));
    register_adapter("HUbZmOnT-Dg", "libc", "atan2", reinterpret_cast<uintptr_t>(&hle_atan2));
    register_adapter("9LCjpWyQ5Zc", "libc", "pow", reinterpret_cast<uintptr_t>(&hle_pow));
    register_adapter("NVadfnzQhHQ", "libc", "exp", reinterpret_cast<uintptr_t>(&hle_exp));
    register_adapter("JM4EBvWT9rc", "libc", "tanh", reinterpret_cast<uintptr_t>(&hle_tanh));
    register_adapter("pKwslsMUmSk", "libc", "fmod", reinterpret_cast<uintptr_t>(&hle_fmod));

    // ---------------------------------------------------------------------
    // 2. libkernel (Stack guard, time, direct memory, sync primitives)
    // ---------------------------------------------------------------------
    register_adapter("f7uOxY9mM1U", "libkernel", "__stack_chk_guard", reinterpret_cast<uintptr_t>(&g_guest_stack_guard), true);
    register_adapter("Ou3iL1abvng", "libkernel", "__stack_chk_fail", reinterpret_cast<uintptr_t>(&hle_stack_chk_fail));
    register_adapter("4J2sUJmuHZQ", "libkernel", "sceKernelGetProcessTime", reinterpret_cast<uintptr_t>(&hle_sceKernelGetProcessTime));
    register_adapter("ejekcaNQNq0", "libkernel", "sceKernelGettimeofday", reinterpret_cast<uintptr_t>(&hle_sceKernelGettimeofday));
    register_adapter("-2IRUCO--PM", "libkernel", "sceKernelReadTsc", reinterpret_cast<uintptr_t>(&hle_sceKernelReadTsc));
    register_adapter("1j3S3n-tTW4", "libkernel", "sceKernelGetTscFrequency", reinterpret_cast<uintptr_t>(&hle_sceKernelGetTscFrequency));
    register_adapter("1jfXLRVzisc", "libkernel", "sceKernelUsleep", reinterpret_cast<uintptr_t>(&hle_sceKernelUsleep));
    register_adapter("9BcDykPmo1I", "libkernel", "__Error", reinterpret_cast<uintptr_t>(&hle_Error));
    register_adapter("aI+OeCz8xrQ", "libkernel", "posix_pthread_self", reinterpret_cast<uintptr_t>(&hle_posix_pthread_self));
    register_adapter("EI-5-jlq2dE", "libkernel", "posix_pthread_getthreadid_np", reinterpret_cast<uintptr_t>(&hle_posix_pthread_getthreadid_np));
    register_adapter("1tKyG7RlMJo", "libkernel", "scePthreadGetprio", reinterpret_cast<uintptr_t>(&hle_scePthreadGetprio));
    register_adapter("nsYoNRywwNg", "libkernel", "scePthreadAttrInit", reinterpret_cast<uintptr_t>(&hle_scePthreadAttrInit));
    register_adapter("UTXzJbWhhTE", "libkernel", "scePthreadAttrSetstacksize", reinterpret_cast<uintptr_t>(&hle_scePthreadAttrSetstacksize));
    register_adapter("6UgtwV+0zb4", "libkernel", "scePthreadCreate", reinterpret_cast<uintptr_t>(&hle_scePthreadCreate));
    register_adapter("W0Hpm2X0uPE", "libkernel", "scePthreadSetprio", reinterpret_cast<uintptr_t>(&hle_scePthreadSetprio));

    register_adapter("pO96TwzOm5E", "libkernel", "sceKernelGetDirectMemorySize", reinterpret_cast<uintptr_t>(&direct_memory::sceKernelGetDirectMemorySize));
    register_adapter("rTXw65xmLIA", "libkernel", "sceKernelAllocateDirectMemory", reinterpret_cast<uintptr_t>(&direct_memory::sceKernelAllocateDirectMemory));
    register_adapter("L-Q3LEjIbgA", "libkernel", "sceKernelMapDirectMemory", reinterpret_cast<uintptr_t>(&direct_memory::sceKernelMapDirectMemory));
    register_adapter("MBuItvba6z8", "libkernel", "sceKernelReleaseDirectMemory", reinterpret_cast<uintptr_t>(&direct_memory::sceKernelReleaseDirectMemory));
    register_adapter("hwVSPCmp5tM", "libkernel", "sceKernelCheckedReleaseDirectMemory", reinterpret_cast<uintptr_t>(&direct_memory::sceKernelReleaseDirectMemory));

    register_adapter("F8bUHwAG284", "libkernel", "posix_pthread_mutexattr_init", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutexattr_init));
    register_adapter("smWEktiyyG0", "libkernel", "posix_pthread_mutexattr_destroy", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutexattr_destroy));
    register_adapter("iMp8QpE+XO4", "libkernel", "posix_pthread_mutexattr_settype", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutexattr_settype));
    register_adapter("cmo1RIYva9o", "libkernel", "scePthreadMutexInit", reinterpret_cast<uintptr_t>(&hle_scePthreadMutexInit));
    register_adapter("2Of0f+3mhhE", "libkernel", "posix_pthread_mutex_destroy", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_destroy));
    register_adapter("9UK1vLZQft4", "libkernel", "posix_pthread_mutex_lock", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_lock));
    register_adapter("upoVrzMHFeE", "libkernel", "posix_pthread_mutex_trylock", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_trylock));
    register_adapter("IafI2PxcPnQ", "libkernel", "posix_pthread_mutex_reltimedlock_np", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_reltimedlock));
    register_adapter("tn3VlD0hG60", "libkernel", "posix_pthread_mutex_unlock", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_unlock));

    register_adapter("188x57JYp0g", "libkernel", "sceKernelCreateSema", reinterpret_cast<uintptr_t>(&sema::sceKernelCreateSema));
    register_adapter("R1Jvn8bSCW8", "libkernel", "sceKernelDeleteSema", reinterpret_cast<uintptr_t>(&sema::sceKernelDeleteSema));
    register_adapter("Zxa0VhQVTsk", "libkernel", "sceKernelWaitSema", reinterpret_cast<uintptr_t>(&sema::sceKernelWaitSema));
    register_adapter("4czppHBiriw", "libkernel", "sceKernelSignalSema", reinterpret_cast<uintptr_t>(&sema::sceKernelSignalSema));
    register_adapter("4DM06U2BNEY", "libkernel", "sceKernelCancelSema", reinterpret_cast<uintptr_t>(&sema::sceKernelCancelSema));

    register_adapter("BpFoboUJoZU", "libkernel", "sceKernelCreateEventFlag", reinterpret_cast<uintptr_t>(&hle_sceKernelCreateEventFlag));
    register_adapter("8mql9OcQnd4", "libkernel", "sceKernelDeleteEventFlag", reinterpret_cast<uintptr_t>(&hle_sceKernelDeleteEventFlag));
    register_adapter("IOnSvHzqu6A", "libkernel", "sceKernelSetEventFlag", reinterpret_cast<uintptr_t>(&hle_sceKernelSetEventFlag));
    register_adapter("7uhBFWRAS60", "libkernel", "sceKernelClearEventFlag", reinterpret_cast<uintptr_t>(&hle_sceKernelClearEventFlag));
    register_adapter("JTvBflhYazQ", "libkernel", "sceKernelWaitEventFlag", reinterpret_cast<uintptr_t>(&hle_sceKernelWaitEventFlag));

    // ---------------------------------------------------------------------
    // 3. libSceFiber (Cooperative fibers)
    // ---------------------------------------------------------------------
    register_adapter("hVYD7Ou2pCQ", "libSceFiber", "sceFiberInitialize", reinterpret_cast<uintptr_t>(&sceFiberInitialize));
    register_adapter("a0LLrZWac0M", "libSceFiber", "sceFiberRun", reinterpret_cast<uintptr_t>(&sceFiberRun));
    register_adapter("PFT2S-tJ7Uk", "libSceFiber", "sceFiberSwitch", reinterpret_cast<uintptr_t>(&sceFiberSwitch));
    register_adapter("B0ZX2hx9DMw", "libSceFiber", "sceFiberReturnToThread", reinterpret_cast<uintptr_t>(&sceFiberReturnToThread));
    register_adapter("JeNX5F-NzQU", "libSceFiber", "sceFiberFinalize", reinterpret_cast<uintptr_t>(&sceFiberFinalize));

    // ---------------------------------------------------------------------
    // 4. libSceVideoOut (Display and presentation)
    // ---------------------------------------------------------------------
    register_adapter("Up36PTk687E", "libSceVideoOut", "sceVideoOutOpen", reinterpret_cast<uintptr_t>(&hle_sceVideoOutOpen));
    register_adapter("i6-sR91Wt-4", "libSceVideoOut", "sceVideoOutSetBufferAttribute", reinterpret_cast<uintptr_t>(&hle_sceVideoOutSetBufferAttribute));
    register_adapter("w3BY+tAEiQY", "libSceVideoOut", "sceVideoOutRegisterBuffers", reinterpret_cast<uintptr_t>(&hle_sceVideoOutRegisterBuffers));
    register_adapter("CBiu4mCE1DA", "libSceVideoOut", "sceVideoOutSetFlipRate", reinterpret_cast<uintptr_t>(&hle_sceVideoOutSetFlipRate));
    register_adapter("HXzjK9yI30k", "libSceVideoOut", "sceVideoOutAddFlipEvent", reinterpret_cast<uintptr_t>(&hle_sceVideoOutAddFlipEvent));
    register_adapter("U46NwOiJpys", "libSceVideoOut", "sceVideoOutSubmitFlip", reinterpret_cast<uintptr_t>(&hle_sceVideoOutSubmitFlip));

    // ---------------------------------------------------------------------
    // 5. libScePad (DualShock 4 controller input)
    // ---------------------------------------------------------------------
    register_adapter("hv1luiJrqQM", "libScePad", "scePadInit", reinterpret_cast<uintptr_t>(&pad::scePadInit));
    register_adapter("xk0AcarP3V4", "libScePad", "scePadOpen", reinterpret_cast<uintptr_t>(&hle_scePadOpen));
    register_adapter("q1cHNfGycLI", "libScePad", "scePadRead", reinterpret_cast<uintptr_t>(&hle_scePadRead));
    register_adapter("yFVnOdGxvZY", "libScePad", "scePadSetVibration", reinterpret_cast<uintptr_t>(&hle_scePadSetVibration));
    register_adapter("RR4novUEENY", "libScePad", "scePadSetLightBar", reinterpret_cast<uintptr_t>(&hle_scePadSetLightBar));
    register_adapter("rIZnR6eSpvk", "libScePad", "scePadResetOrientation", reinterpret_cast<uintptr_t>(&hle_scePadResetOrientation));
    register_adapter("vDLMoJLde8I", "libScePad", "scePadSetTiltCorrectionState", reinterpret_cast<uintptr_t>(&hle_scePadSetTiltCorrectionState));
    register_adapter("gjP9-KQzoUk", "libScePad", "scePadGetControllerInformation", reinterpret_cast<uintptr_t>(&hle_scePadGetControllerInformation));

    // ---------------------------------------------------------------------
    // 6. libSceAudioOut (Audio output)
    // ---------------------------------------------------------------------
    register_adapter("JfEPXVxhFqA", "libSceAudioOut", "sceAudioOutInit", reinterpret_cast<uintptr_t>(&hle_sceAudioOutInit));
    register_adapter("ekNvsT22rsY", "libSceAudioOut", "sceAudioOutOpen", reinterpret_cast<uintptr_t>(&hle_sceAudioOutOpen));
    register_adapter("s1--uE9mBFw", "libSceAudioOut", "sceAudioOutClose", reinterpret_cast<uintptr_t>(&hle_sceAudioOutClose));
    register_adapter("QOQtbeDqsT4", "libSceAudioOut", "sceAudioOutOutput", reinterpret_cast<uintptr_t>(&hle_sceAudioOutOutput));
    register_adapter("w3PdaSTSwGE", "libSceAudioOut", "sceAudioOutOutputs", reinterpret_cast<uintptr_t>(&hle_sceAudioOutOutputs));
    register_adapter("b+uAV89IlxE", "libSceAudioOut", "sceAudioOutSetVolume", reinterpret_cast<uintptr_t>(&hle_sceAudioOutSetVolume));
    register_adapter("GrQ9s4IrNaQ", "libSceAudioOut", "sceAudioOutGetPortState", reinterpret_cast<uintptr_t>(&hle_sceAudioOutGetPortState));
    register_adapter("wVwPU50pS1c", "libSceAudioOut", "sceAudioOutSetMixLevelPadSpk", reinterpret_cast<uintptr_t>(&hle_sceAudioOutSetMixLevelPadSpk));

    // ---------------------------------------------------------------------
    // 7. libSceSysmodule (System module loading)
    // ---------------------------------------------------------------------
    register_adapter("g8cM39EUZ6o", "libSceSysmodule", "sceSysmoduleLoadModule", reinterpret_cast<uintptr_t>(&hle_sceSysmoduleLoadModule));
    register_adapter("eR2bZFAAU0Q", "libSceSysmodule", "sceSysmoduleUnloadModule", reinterpret_cast<uintptr_t>(&hle_sceSysmoduleUnloadModule));

    // ---------------------------------------------------------------------
    // 8. libSceScreenShot (Screen capture parameters)
    // ---------------------------------------------------------------------
    register_adapter("tIYf0W5VTi8", "libSceScreenShot", "sceScreenShotDisable", reinterpret_cast<uintptr_t>(&hle_sceScreenShotDisable));
    register_adapter("G7KlmIYFIZc", "libSceScreenShot", "sceScreenShotSetParam", reinterpret_cast<uintptr_t>(&hle_sceScreenShotSetParam));
    register_adapter("ahHhOf+QNkQ", "libSceScreenShot", "sceScreenShotSetOverlayImage", reinterpret_cast<uintptr_t>(&hle_sceScreenShotSetOverlayImage));
    register_adapter("2xxUtuC-RzE", "libSceScreenShot", "sceScreenShotEnable", reinterpret_cast<uintptr_t>(&hle_sceScreenShotEnable));
    register_adapter("zwY0YV91TTI", "libSceGnmDriver", "sceGnmSubmitCommandBuffers",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSubmitCommandBuffers));
    register_adapter("0H2vBYbTLHI", "libSceGnmDriver", "sceGnmDrawInitDefaultHardwareState200",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmDrawInitDefaultHardwareState200));
    register_adapter("nF6bFRUBRAU", "libSceGnmDriver", "sceGnmDispatchInitDefaultHardwareState",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmDispatchInitDefaultHardwareState));
    register_adapter("b08AgtPlHPg", "libSceGnmDriver", "sceGnmAreSubmitsAllowed",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmAreSubmitsAllowed));
    register_adapter("f33OrruQYbM", "libSceGnmDriver", "sceRazorIsLoaded",
                     reinterpret_cast<uintptr_t>(&gnm::sceRazorIsLoaded));
    register_adapter("iBt3Oe00Kvc", "libSceGnmDriver", "sceGnmFlushGarlic",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmFlushGarlic));
    register_adapter("1qXLHIpROPE", "libSceGnmDriver", "sceGnmInsertWaitFlipDone",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmInsertWaitFlipDone));
    register_adapter("W1Etj-jlW7Y", "libSceGnmDriver", "sceGnmInsertPushMarker",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmInsertPushMarker));
    register_adapter("7qZVNgEu+SY", "libSceGnmDriver", "sceGnmInsertPopMarker",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmInsertPopMarker));
    register_adapter("jiItzS6+22g", "libSceGnmDriver", "sceGnmInsertSetMarker",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmInsertSetMarker));
    register_adapter("aPIZJTXC+cU", "libSceGnmDriver", "sceGnmInsertPushColorMarker",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmInsertPushColorMarker));
    register_adapter("aj3L-iaFmyk", "libSceGnmDriver", "sceGnmInsertSetColorMarker",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmInsertSetColorMarker));
    register_adapter("bQVd5YzCal0", "libSceGnmDriver", "sceGnmSetPsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSetPsShader));
    register_adapter("4MgRw-bVNQU", "libSceGnmDriver", "sceGnmUpdatePsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmUpdatePsShader));
    register_adapter("gAhCn6UiU4Y", "libSceGnmDriver", "sceGnmSetVsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSetVsShader));
    register_adapter("V31V01UiScY", "libSceGnmDriver", "sceGnmUpdateVsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmUpdateVsShader));
    register_adapter("FUHG8sQ3R58", "libSceGnmDriver", "sceGnmSetEsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSetEsShader));
    register_adapter("VJNjFtqiF5w", "libSceGnmDriver", "sceGnmSetHsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSetHsShader));
    register_adapter("GNlx+y7xPdE", "libSceGnmDriver", "sceGnmUpdateHsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmUpdateHsShader));
    register_adapter("vckdzbQ46SI", "libSceGnmDriver", "sceGnmSetLsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSetLsShader));
    register_adapter("KXltnCwEJHQ", "libSceGnmDriver", "sceGnmSetCsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSetCsShader));
    register_adapter("UJwNuMBcUAk", "libSceGnmDriver", "sceGnmSetGsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSetGsShader));
    register_adapter("nLM2i2+65hA", "libSceGnmDriver", "sceGnmUpdateGsShader",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmUpdateGsShader));
    register_adapter("xbxNatawohc", "libSceGnmDriver", "sceGnmSubmitAndFlipCommandBuffers",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSubmitAndFlipCommandBuffers));
    register_adapter("yvZ73uQUqrk", "libSceGnmDriver", "sceGnmSubmitDone",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSubmitDone));
    register_adapter("jg33rEKLfVs", "libSceGnmDriver", "sceGnmIsUserPaEnabled",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmIsUserPaEnabled));
    register_adapter("TLV4mswiZ4A", "libSceGnmDriver", "sceGnmDriverCaptureInProgress",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmDriverCaptureInProgress));
    register_adapter("d88anrgNoKY", "libSceGnmDriver", "sceGnmDriverTriggerCapture",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmDriverTriggerCapture));
    register_adapter("qpGITzPE+Zc", "libSceGnmDriver", "sceGnmDebugHardwareStatus",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmDebugHardwareStatus));
    register_adapter("iCO804ZgzdA", "libSceGnmDriver", "sceGnmValidateCommandBuffers",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmValidateCommandBuffers));
    register_adapter("ln33zjBrfjk", "libSceGnmDriver", "sceGnmGetTheTessellationFactorRingBufferBaseAddress",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmGetTheTessellationFactorRingBufferBaseAddress));
    register_adapter("26PM5Mzl8zc", "libSceGnmDriver", "sceGnmLogicalCuIndexToPhysicalCuIndex",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmLogicalCuIndexToPhysicalCuIndex));
    register_adapter("RU74kek-N0c", "libSceGnmDriver", "sceGnmLogicalCuMaskToPhysicalCuMask",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmLogicalCuMaskToPhysicalCuMask));
    register_adapter("bX5IbRvECXk", "libSceGnmDriver", "sceGnmDingDong",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmDingDong));
    register_adapter("HHo1BAljZO8", "libSceGnmDriver", "sceGnmDisableMipStatsReport",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmDisableMipStatsReport));
    register_adapter("f85orjx7qts", "libSceGnmDriver", "sceGnmRequestMipStatsReportAndReset",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmRequestMipStatsReportAndReset));
    register_adapter("+xuDhxlWRPg", "libSceGnmDriver", "sceGnmSetupMipStatsReport",
                     reinterpret_cast<uintptr_t>(&gnm::sceGnmSetupMipStatsReport));
    register_adapter("fZo48un7LK4", "libSceSystemService", "sceSystemServiceParamGetInt",
                     reinterpret_cast<uintptr_t>(&system_service::sceSystemServiceParamGetInt));
    register_adapter("wLlFkwG9UcQ", "libc", "time", reinterpret_cast<uintptr_t>(&hle_time));
    // The supplied libc's clock export at 0x2ef60 forwards directly to
    // sceKernelGetProcessTime (NID 4J2sUJmuHZQ), rather than the Windows CRT clock.
    register_adapter("QZP6I9ZZxpE", "libc", "clock", reinterpret_cast<uintptr_t>(&hle_sceKernelGetProcessTime));
    register_adapter("HoLVWNanBBc", "libkernel", "getpid", reinterpret_cast<uintptr_t>(&hle_getpid));
    register_adapter("Vo5V8KAwCmk", "libSceSystemService", "sceSystemServiceHideSplashScreen",
                     reinterpret_cast<uintptr_t>(&system_service::sceSystemServiceHideSplashScreen));
    register_adapter("x1UB9bwDSOw", "libSceSystemService", "sceSystemServiceDisableMusicPlayer",
                     reinterpret_cast<uintptr_t>(&system_service::sceSystemServiceDisableMusicPlayer));
    register_adapter("dQHWEsJtoE4", "libScePosix", "pthread_mutexattr_init", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutexattr_init));
    register_adapter("HF7lK46xzjY", "libScePosix", "pthread_mutexattr_destroy", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutexattr_destroy));
    register_adapter("mDmgMOGVUqg", "libScePosix", "pthread_mutexattr_settype", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutexattr_settype));
    register_adapter("ttHNfU+qDBU", "libScePosix", "pthread_mutex_init", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_init));
    register_adapter("ltCfaGr2JGE", "libScePosix", "pthread_mutex_destroy", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_destroy));
    register_adapter("7H0iTOciTLo", "libScePosix", "pthread_mutex_lock", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_lock));
    register_adapter("K-jXhbt2gn4", "libScePosix", "pthread_mutex_trylock", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_trylock));
    register_adapter("2Z+PpY6CaJg", "libScePosix", "pthread_mutex_unlock", reinterpret_cast<uintptr_t>(&pthread_sync::posix_mutex_unlock));
    register_adapter("zHchY8ft5pk", "libScePosix", "pthread_attr_destroy", reinterpret_cast<uintptr_t>(&pthread::attr_destroy));
    register_adapter("E+tyo3lp5Lw", "libScePosix", "pthread_attr_setdetachstate", reinterpret_cast<uintptr_t>(&pthread::attr_setdetachstate));
    register_adapter("2Q0z6rnBrTE", "libScePosix", "pthread_attr_setstacksize", reinterpret_cast<uintptr_t>(&pthread::attr_setstacksize));
    register_adapter("Jmi+9w9u0E4", "libScePosix", "pthread_create_name_np", reinterpret_cast<uintptr_t>(&pthread::create));
    register_adapter("h9CcP3J0oVM", "libScePosix", "pthread_join", reinterpret_cast<uintptr_t>(&pthread::join));
    register_adapter("EotR8a3ASf4", "libScePosix", "pthread_self", reinterpret_cast<uintptr_t>(&pthread::self));
    register_adapter("onNY9Byn-W8", "libkernel", "scePthreadJoin", reinterpret_cast<uintptr_t>(&hle_scePthreadJoin));
    register_adapter("D0OdFMjp46I", "libkernel", "sceKernelCreateEqueue", reinterpret_cast<uintptr_t>(&equeue::sceKernelCreateEqueue));
    register_adapter("yOfGg-I1ZII", "libkernel", "scePthreadRwlockattrInit", reinterpret_cast<uintptr_t>(&rwlock::scePthreadRwlockattrInit));
    register_adapter("i2ifZ3fS2fo", "libkernel", "scePthreadRwlockattrDestroy", reinterpret_cast<uintptr_t>(&rwlock::scePthreadRwlockattrDestroy));
    register_adapter("6ULAa0fq4jA", "libkernel", "scePthreadRwlockInit", reinterpret_cast<uintptr_t>(&rwlock::scePthreadRwlockInit));
    register_adapter("BB+kb08Tl9A", "libkernel", "scePthreadRwlockDestroy", reinterpret_cast<uintptr_t>(&rwlock::scePthreadRwlockDestroy));
    register_adapter("Ox9i0c7L5w0", "libkernel", "scePthreadRwlockRdlock", reinterpret_cast<uintptr_t>(&rwlock::scePthreadRwlockRdlock));
    register_adapter("mqdNorrB+gI", "libkernel", "scePthreadRwlockWrlock", reinterpret_cast<uintptr_t>(&rwlock::scePthreadRwlockWrlock));
    register_adapter("+L98PIbGttk", "libkernel", "scePthreadRwlockUnlock", reinterpret_cast<uintptr_t>(&rwlock::scePthreadRwlockUnlock));
    register_adapter("1G3lF1Gg1k8", "libkernel", "sceKernelOpen", reinterpret_cast<uintptr_t>(&kernel_file::sceKernelOpen));
    register_adapter("Cg4srZ6TKbU", "libkernel", "sceKernelRead", reinterpret_cast<uintptr_t>(&kernel_file::sceKernelRead));
    register_adapter("UK2Tl2DWUns", "libkernel", "sceKernelClose", reinterpret_cast<uintptr_t>(&kernel_file::sceKernelClose));
    register_adapter("jpFjmgAC5AE", "libkernel", "sceKernelDeleteEqueue", reinterpret_cast<uintptr_t>(&equeue::sceKernelDeleteEqueue));
    register_adapter("fzyMKs9kim0", "libkernel", "sceKernelWaitEqueue", reinterpret_cast<uintptr_t>(&equeue::sceKernelWaitEqueue));
    register_adapter("62KCwEMmzcM", "libkernel", "scePthreadAttrDestroy", reinterpret_cast<uintptr_t>(&pthread::attr_destroy));

    // ---------------------------------------------------------------------
    // 9. libSceFios2 (File I/O System)
    // ---------------------------------------------------------------------
    register_adapter("wAKZ-det+yo", "libSceFios2", "sceFiosInitialize",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosInitialize));
    register_adapter("3HAgZPl1v+4", "libSceFios2", "sceFiosTerminate",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosTerminate));
    register_adapter("lgITuBsRo2o", "libSceFios2", "sceFiosIOFilterAdd",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosIOFilterAdd));
    register_adapter("ahIXyuwF0-o", "libSceFios2", "sceFiosIOFilterRemove",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosIOFilterRemove));
    register_adapter("OIGbkgGOu6E", "libSceFios2", "sceFiosIOFilterPsarcDearchiver",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosIOFilterPsarcDearchiver));
    register_adapter("UUriaXy7G90", "libSceFios2", "sceFiosArchiveGetMountBufferSizeSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosArchiveGetMountBufferSizeSync));
    register_adapter("xutLbQdqyb4", "libSceFios2", "sceFiosArchiveMountSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosArchiveMountSync));
    register_adapter("yy6C7k7FPZY", "libSceFios2", "sceFiosArchiveUnmountSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosArchiveUnmountSync));

    // File Handle API
    register_adapter("b44anV2D7K0", "libSceFios2", "sceFiosFHOpenSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHOpenSync));
    register_adapter("er6TkQFUvp0", "libSceFios2", "sceFiosFHOpen",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHOpen));
    register_adapter("Bn2ZF4ZjeuQ", "libSceFios2", "sceFiosFHReadSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHReadSync));
    register_adapter("cg-VoPqZYss", "libSceFios2", "sceFiosFHRead",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHRead));
    register_adapter("rR8wq7YFRZs", "libSceFios2", "sceFiosFHPread",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHPread));
    register_adapter("AOujSGqU+ms", "libSceFios2", "sceFiosFHCloseSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHCloseSync));
    register_adapter("5sYNBNK+W3g", "libSceFios2", "sceFiosFHClose",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHClose));
    register_adapter("FdjoqFQOlt0", "libSceFios2", "sceFiosFHGetSize",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHGetSize));
    register_adapter("xReSebwKApA", "libSceFios2", "sceFiosFHSeek",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHSeek));
    register_adapter("MrRFrdgpsx8", "libSceFios2", "sceFiosFHTell",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHTell));
    register_adapter("EzzSJz6yuMc", "libSceFios2", "sceFiosFHSyncSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHSyncSync));
    register_adapter("urUHkGGweA4", "libSceFios2", "sceFiosFHWrite",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHWrite));
    register_adapter("Kl-TbrDU9YM", "libSceFios2", "sceFiosFHWriteSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHWriteSync));
    register_adapter("PbxGVfOvUQY", "libSceFios2", "sceFiosFHPwrite",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFHPwrite));

    // Path Queries & Direct I/O
    register_adapter("zF8-CRvRXnM", "libSceFios2", "sceFiosFileGetSizeSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFileGetSizeSync));
    register_adapter("2ZaHWy3IhKQ", "libSceFios2", "sceFiosExistsSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosExistsSync));
    register_adapter("OOuvHKTu4Oc", "libSceFios2", "sceFiosDirectoryExistsSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosDirectoryExistsSync));
    register_adapter("jayvY07C5dk", "libSceFios2", "sceFiosStatSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosStatSync));
    register_adapter("kVMxSiYD6tc", "libSceFios2", "sceFiosFileReadSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFileReadSync));
    register_adapter("YlKCkfJL+Y8", "libSceFios2", "sceFiosFileRead",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFileRead));
    register_adapter("wj-U+mMbyAM", "libSceFios2", "sceFiosFileWriteSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFileWriteSync));
    register_adapter("q8CfetImdIY", "libSceFios2", "sceFiosFileWrite",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFileWrite));
    register_adapter("bDupEgbQ6Fk", "libSceFios2", "sceFiosFileDeleteSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosFileDeleteSync));

    // Directory Handles
    register_adapter("GGqucL9F+YI", "libSceFios2", "sceFiosDHOpenSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosDHOpenSync));
    register_adapter("uCkgJOrYUL4", "libSceFios2", "sceFiosDHOpen",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosDHOpen));
    register_adapter("odjOGg8harg", "libSceFios2", "sceFiosDHReadSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosDHReadSync));
    register_adapter("0-p4O8FINmU", "libSceFios2", "sceFiosDHCloseSync",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosDHCloseSync));
    register_adapter("-ULUBK21QgE", "libSceFios2", "sceFiosDirectoryCreate",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosDirectoryCreate));

    // Operations
    register_adapter("SnoQQWnGK9I", "libSceFios2", "sceFiosOpWait",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosOpWait));
    register_adapter("X+7rIfY97Ps", "libSceFios2", "sceFiosOpGetError",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosOpGetError));
    register_adapter("+FRvKknUj1I", "libSceFios2", "sceFiosOpGetActualCount",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosOpGetActualCount));
    register_adapter("5cyEcilO-J0", "libSceFios2", "sceFiosOpDelete",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosOpDelete));

    // Debug
    register_adapter("8BLW2WiSIuI", "libSceFios2", "sceFiosDebugDumpError",
                     reinterpret_cast<uintptr_t>(&fios::sceFiosDebugDumpError));

    // ---------------------------------------------------------------------
    // 10. libScePlayGo (Chunk downloading & installation)
    // ---------------------------------------------------------------------
    register_adapter("ts6GlZOKRrE", "libScePlayGo", "scePlayGoInitialize",
                     reinterpret_cast<uintptr_t>(&playgo::scePlayGoInitialize));
    register_adapter("M1Gma1ocrGE", "libScePlayGo", "scePlayGoOpen",
                     reinterpret_cast<uintptr_t>(&playgo::scePlayGoOpen));
    register_adapter("Nn7zKwnA5q0", "libScePlayGo", "scePlayGoGetToDoList",
                     reinterpret_cast<uintptr_t>(&playgo::scePlayGoGetToDoList));
    register_adapter("rvBSfTimejE", "libScePlayGo", "scePlayGoGetInstallSpeed",
                     reinterpret_cast<uintptr_t>(&playgo::scePlayGoGetInstallSpeed));
    register_adapter("4AAcTU9R3XM", "libScePlayGo", "scePlayGoSetInstallSpeed",
                     reinterpret_cast<uintptr_t>(&playgo::scePlayGoSetInstallSpeed));
    register_adapter("uWIYLFkkwqk", "libScePlayGo", "scePlayGoGetLocus",
                     reinterpret_cast<uintptr_t>(&playgo::scePlayGoGetLocus));
    register_adapter("v6EZ-YWRdMs", "libScePlayGo", "scePlayGoGetEta",
                     reinterpret_cast<uintptr_t>(&playgo::scePlayGoGetEta));
}

} // namespace gravity::hle
