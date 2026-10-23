#include "hle/registry/adapter_registry.h"
#include "hle/fiber.h"
#include "hle/kernel.h"
#include "hle/pad.h"
#include "hle/video.h"
#include "hle/audio.h"
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

extern "C" void hle_unresolved_dispatcher_c(uint64_t thunk_id, uintptr_t caller_rip) {
    AdapterRegistry::instance().handle_unresolved(thunk_id, caller_rip);
}

__asm__(
    ".globl hle_unresolved_dispatcher_stub\n"
    "hle_unresolved_dispatcher_stub:\n"
    "  movq (%rsp), %rdx\n"      // Win64 arg 2: caller_rip (return address on stack)
    "  movq %r10, %rcx\n"        // Win64 arg 1: thunk_id (passed in r10)
    "  subq $40, %rsp\n"         // Win64 shadow space
    "  call hle_unresolved_dispatcher_c\n"
    "  addq $40, %rsp\n"
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

extern "C" GRAVITY_SYSV_ABI void hle_free(void* ptr) {
    if (!ptr) return;
    ensure_hle_heap();
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
        HeapFree(g_hle_heap, 0, ptr);
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
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadGetprio(uint64_t thread, int* prio) {
    (void)thread;
    if (prio) *prio = 256;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadAttrInit(void* attr) {
    if (attr) std::memset(attr, 0, 64);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadAttrSetstacksize(void* attr, std::size_t size) {
    (void)attr;
    (void)size;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadCreate(void** thread, const void* attr,
                                                     void* (*start_routine)(void*),
                                                     void* arg, const char* name) {
    (void)attr;
    (void)start_routine;
    (void)arg;
    (void)name;
    static uint64_t next_tid = 0x2000;
    if (thread) *thread = reinterpret_cast<void*>(++next_tid);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_scePthreadSetprio(void* thread, int prio) {
    (void)thread;
    (void)prio;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_sceSysmoduleLoadModule(uint32_t id) {
    (void)id;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_sceSysmoduleUnloadModule(uint32_t id) {
    (void)id;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_sceScreenShotDisable() {
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int hle_sceScreenShotSetParam(const void* param) {
    (void)param;
    return 0;
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

extern "C" GRAVITY_SYSV_ABI uint64_t hle_posix_pthread_self() {
    return static_cast<uint64_t>(GetCurrentThreadId());
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_getthreadid_np() {
    return static_cast<int32_t>(GetCurrentThreadId());
}

// libkernel Direct Memory
extern "C" GRAVITY_SYSV_ABI size_t hle_sceKernelGetDirectMemorySize() {
    return 4ULL * 1024 * 1024 * 1024;
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelAllocateDirectMemory(
    int64_t searchStart, int64_t searchEnd, size_t length, size_t alignment,
    int32_t type, int64_t* physAddrOut)
{
    (void)searchStart; (void)searchEnd; (void)alignment; (void)type;
    static std::atomic<int64_t> s_next_direct_addr{0x200000000ULL};
    int64_t addr = s_next_direct_addr.fetch_add((length + 0xFFFF) & ~0xFFFF);
    if (physAddrOut) *physAddrOut = addr;
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelMapDirectMemory(
    void** addr, size_t length, int32_t prot, int32_t flags, int64_t directMemoryStart, size_t alignment)
{
    (void)prot; (void)flags; (void)directMemoryStart; (void)alignment;
    void* ptr = VirtualAlloc(addr ? *addr : nullptr, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (addr && ptr) *addr = ptr;
    return ptr ? 0 : 0x8001000C; // KERNEL_ENOMEM
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelReleaseDirectMemory(int64_t start, size_t length) {
    (void)start; (void)length;
    return 0;
}

extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelCheckedReleaseDirectMemory(int64_t start, size_t length) {
    (void)start; (void)length;
    return 0;
}

// libkernel Posix Mutex / Sync wrappers
extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_mutexattr_init(void* attr) {
    if (attr) std::memset(attr, 0, 16);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_mutexattr_destroy(void* attr) {
    (void)attr;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_mutexattr_settype(void* attr, int32_t type) {
    (void)attr; (void)type;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_scePthreadMutexInit(void* mutex, const void* attr, const char* name) {
    (void)attr; (void)name;
    if (mutex) {
        auto* srw = reinterpret_cast<SRWLOCK*>(mutex);
        InitializeSRWLock(srw);
    }
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_mutex_destroy(void* mutex) {
    (void)mutex;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_mutex_lock(void* mutex) {
    if (mutex) {
        AcquireSRWLockExclusive(reinterpret_cast<SRWLOCK*>(mutex));
    }
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_mutex_trylock(void* mutex) {
    if (mutex) {
        return TryAcquireSRWLockExclusive(reinterpret_cast<SRWLOCK*>(mutex)) ? 0 : 16; // EBUSY
    }
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_mutex_reltimedlock_np(void* mutex, uint64_t usec) {
    (void)usec;
    return hle_posix_pthread_mutex_lock(mutex);
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_posix_pthread_mutex_unlock(void* mutex) {
    if (mutex) {
        ReleaseSRWLockExclusive(reinterpret_cast<SRWLOCK*>(mutex));
    }
    return 0;
}

// libkernel Semaphores
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelCreateSema(void* sema, const char* name, uint32_t attr,
                                                            int32_t initVal, int32_t maxVal, const void* opt) {
    (void)sema; (void)name; (void)attr; (void)initVal; (void)maxVal; (void)opt;
    static std::atomic<int32_t> s_next_sema{1};
    return s_next_sema.fetch_add(1);
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelDeleteSema(int32_t sema) {
    (void)sema;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelWaitSema(int32_t sema, int32_t count, void* timeout) {
    (void)sema; (void)count; (void)timeout;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelSignalSema(int32_t sema, int32_t count) {
    (void)sema; (void)count;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelCancelSema(int32_t sema, int32_t count, int32_t* numWaitThreads) {
    (void)sema; (void)count;
    if (numWaitThreads) *numWaitThreads = 0;
    return 0;
}

// libkernel EventFlags
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceKernelCreateEventFlag(void* ef, const char* name, uint32_t attr,
                                                                 uint64_t initPattern, const void* opt) {
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

// libSceVideoOut
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutOpen(int32_t userId, int32_t type, int32_t index, const void* param) {
    (void)userId; (void)type; (void)index; (void)param;
    return 1;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutSetBufferAttribute(
    void* attribute, uint32_t pixelFormat, uint32_t tilingMode, uint32_t aspectRatio,
    uint32_t width, uint32_t height, uint32_t pitchInPixel)
{
    (void)attribute; (void)pixelFormat; (void)tilingMode; (void)aspectRatio;
    (void)width; (void)height; (void)pitchInPixel;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutRegisterBuffers(
    int32_t handle, int32_t startIndex, void* const* addresses, int32_t bufferNum, const void* attribute)
{
    (void)handle; (void)startIndex; (void)addresses; (void)bufferNum; (void)attribute;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutSetFlipRate(int32_t handle, int32_t rate) {
    (void)handle; (void)rate;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutAddFlipEvent(int32_t eq, int32_t handle, void* udata) {
    (void)eq; (void)handle; (void)udata;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceVideoOutSubmitFlip(int32_t handle, int32_t bufferIndex, int32_t flipMode, int64_t flipArg) {
    (void)handle; (void)bufferIndex; (void)flipMode; (void)flipArg;
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
    (void)userId; (void)type; (void)index; (void)len; (void)freq; (void)mode;
    return 1;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutClose(int32_t handle) {
    (void)handle;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutOutput(int32_t handle, const void* ptr) {
    (void)handle; (void)ptr;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutOutputs(void* params, uint32_t num) {
    (void)params; (void)num;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutSetVolume(int32_t handle, int32_t flag, int32_t* vol) {
    (void)handle; (void)flag; (void)vol;
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutGetPortState(int32_t handle, void* state) {
    (void)handle;
    if (state) std::memset(state, 0, 16);
    return 0;
}
extern "C" GRAVITY_SYSV_ABI int32_t hle_sceAudioOutSetMixLevelPadSpk(int32_t handle, int32_t mixLevel) {
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
}

void AdapterRegistry::register_adapter(std::string_view nid, std::string_view library,
                                      std::string_view name, uintptr_t func_ptr,
                                      bool is_variable) {
    std::lock_guard<std::mutex> lock(mutex_);
    registry_[std::string(nid)] = AdapterSymbol{
        std::string(nid), std::string(library), std::string(name), func_ptr, is_variable
    };
}

uintptr_t AdapterRegistry::resolve(std::string_view nid, std::string_view library) const noexcept {
    (void)library;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registry_.find(std::string(nid));
    if (it != registry_.end()) {
        return it->second.func_ptr;
    }
    return 0;
}

const AdapterSymbol* AdapterRegistry::find(std::string_view nid) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registry_.find(std::string(nid));
    if (it != registry_.end()) {
        return &it->second;
    }
    return nullptr;
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
    std::string key = std::string(nid);
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

void AdapterRegistry::handle_unresolved(uint64_t thunk_id, uintptr_t caller_rip) {
    UnresolvedCallInfo info{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (thunk_id < unresolved_metadata_.size()) {
            info = unresolved_metadata_[thunk_id];
            info.caller_rip = caller_rip;
            if (caller_rip >= guest_base_) {
                info.guest_offset = caller_rip - guest_base_;
            }
        }
    }

    std::fprintf(stderr, "\n============================================================\n");
    std::fprintf(stderr, "[HLE FATAL] Call to UNRESOLVED import function!\n");
    std::fprintf(stderr, "  Library:       %s\n", info.library.c_str());
    std::fprintf(stderr, "  NID:           %s\n", info.nid.c_str());
    std::fprintf(stderr, "  Caller RIP:    0x%llX\n", (unsigned long long)caller_rip);
    if (info.guest_offset != 0) {
        std::fprintf(stderr, "  Module Offset: eboot.bin+0x%llX\n", (unsigned long long)info.guest_offset);
    }
    std::fprintf(stderr, "============================================================\n");
    std::fflush(stderr);

    if (unresolved_hook_) {
        unresolved_hook_(info);
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

    register_adapter("pO96TwzOm5E", "libkernel", "sceKernelGetDirectMemorySize", reinterpret_cast<uintptr_t>(&hle_sceKernelGetDirectMemorySize));
    register_adapter("rTXw65xmLIA", "libkernel", "sceKernelAllocateDirectMemory", reinterpret_cast<uintptr_t>(&hle_sceKernelAllocateDirectMemory));
    register_adapter("L-Q3LEjIbgA", "libkernel", "sceKernelMapDirectMemory", reinterpret_cast<uintptr_t>(&hle_sceKernelMapDirectMemory));
    register_adapter("MBuItvba6z8", "libkernel", "sceKernelReleaseDirectMemory", reinterpret_cast<uintptr_t>(&hle_sceKernelReleaseDirectMemory));
    register_adapter("hwVSPCmp5tM", "libkernel", "sceKernelCheckedReleaseDirectMemory", reinterpret_cast<uintptr_t>(&hle_sceKernelCheckedReleaseDirectMemory));

    register_adapter("F8bUHwAG284", "libkernel", "posix_pthread_mutexattr_init", reinterpret_cast<uintptr_t>(&hle_posix_pthread_mutexattr_init));
    register_adapter("smWEktiyyG0", "libkernel", "posix_pthread_mutexattr_destroy", reinterpret_cast<uintptr_t>(&hle_posix_pthread_mutexattr_destroy));
    register_adapter("iMp8QpE+XO4", "libkernel", "posix_pthread_mutexattr_settype", reinterpret_cast<uintptr_t>(&hle_posix_pthread_mutexattr_settype));
    register_adapter("cmo1RIYva9o", "libkernel", "scePthreadMutexInit", reinterpret_cast<uintptr_t>(&hle_scePthreadMutexInit));
    register_adapter("2Of0f+3mhhE", "libkernel", "posix_pthread_mutex_destroy", reinterpret_cast<uintptr_t>(&hle_posix_pthread_mutex_destroy));
    register_adapter("9UK1vLZQft4", "libkernel", "posix_pthread_mutex_lock", reinterpret_cast<uintptr_t>(&hle_posix_pthread_mutex_lock));
    register_adapter("upoVrzMHFeE", "libkernel", "posix_pthread_mutex_trylock", reinterpret_cast<uintptr_t>(&hle_posix_pthread_mutex_trylock));
    register_adapter("IafI2PxcPnQ", "libkernel", "posix_pthread_mutex_reltimedlock_np", reinterpret_cast<uintptr_t>(&hle_posix_pthread_mutex_reltimedlock_np));
    register_adapter("tn3VlD0hG60", "libkernel", "posix_pthread_mutex_unlock", reinterpret_cast<uintptr_t>(&hle_posix_pthread_mutex_unlock));

    register_adapter("188x57JYp0g", "libkernel", "sceKernelCreateSema", reinterpret_cast<uintptr_t>(&hle_sceKernelCreateSema));
    register_adapter("R1Jvn8bSCW8", "libkernel", "sceKernelDeleteSema", reinterpret_cast<uintptr_t>(&hle_sceKernelDeleteSema));
    register_adapter("Zxa0VhQVTsk", "libkernel", "sceKernelWaitSema", reinterpret_cast<uintptr_t>(&hle_sceKernelWaitSema));
    register_adapter("4czppHBiriw", "libkernel", "sceKernelSignalSema", reinterpret_cast<uintptr_t>(&hle_sceKernelSignalSema));
    register_adapter("4DM06U2BNEY", "libkernel", "sceKernelCancelSema", reinterpret_cast<uintptr_t>(&hle_sceKernelCancelSema));

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
}

} // namespace gravity::hle
