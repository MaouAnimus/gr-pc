#include "hle/libc/libc.h"
#include "hle/guest_memory.h"
#include "hle/native_tls.h"
#include "hle/fs.h"
#include <windows.h>
#include <malloc.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace gravity::hle::libc {

namespace {

FILE* resolve_stream(void* stream) {
    if (!stream) return nullptr;
    if (stream == reinterpret_cast<void*>(1) || stream == stdin || stream == hle_stdin) return stdin;
    if (stream == reinterpret_cast<void*>(2) || stream == stdout || stream == hle_stdout) return stdout;
    if (stream == reinterpret_cast<void*>(3) || stream == stderr || stream == hle_stderr) return stderr;
    return static_cast<FILE*>(stream);
}

struct AtExitEntry {
    void (*func)(void*){nullptr};
    void* arg{nullptr};
    void* dso{nullptr};
};

std::mutex g_atexit_mutex;
std::vector<AtExitEntry> g_atexit_handlers;

} // anonymous namespace

extern "C" {

// -----------------------------------------------------------------------------
// Math & Floats
// -----------------------------------------------------------------------------

GRAVITY_SYSV_ABI double hle_Cosh(double x) {
    return std::cosh(x);
}

GRAVITY_SYSV_ABI double hle_Sinh(double x) {
    return std::sinh(x);
}

GRAVITY_SYSV_ABI double hle_Sin(double x) {
    return std::sin(x);
}

GRAVITY_SYSV_ABI double hle_Log(double x) {
    return std::log(x);
}

GRAVITY_SYSV_ABI float hle_FLog(float x) {
    return std::logf(x);
}

GRAVITY_SYSV_ABI double hle_fmax(double x, double y) {
    return std::fmax(x, y);
}

GRAVITY_SYSV_ABI float hle_fmaxf(float x, float y) {
    return std::fmaxf(x, y);
}

GRAVITY_SYSV_ABI float hle_fminf(float x, float y) {
    return std::fminf(x, y);
}

GRAVITY_SYSV_ABI double hle_frexp(double x, int* exp) {
    if (exp && guest_memory::accessible(exp, sizeof(int))) {
        return std::frexp(x, exp);
    }
    int dummy = 0;
    return std::frexp(x, &dummy);
}

GRAVITY_SYSV_ABI double hle_ldexp(double x, int exp) {
    return std::ldexp(x, exp);
}

GRAVITY_SYSV_ABI double hle_modf(double x, double* iptr) {
    if (iptr && guest_memory::accessible(iptr, sizeof(double))) {
        return std::modf(x, iptr);
    }
    double dummy = 0.0;
    return std::modf(x, &dummy);
}

GRAVITY_SYSV_ABI double hle_difftime(std::time_t time1, std::time_t time0) {
    return std::difftime(time1, time0);
}

// -----------------------------------------------------------------------------
// String & Memory
// -----------------------------------------------------------------------------

GRAVITY_SYSV_ABI const char* hle_strerror(int errnum) {
    return std::strerror(errnum);
}

GRAVITY_SYSV_ABI int hle_strcoll(const char* s1, const char* s2) {
    if (!s1 || !s2) return 0;
    return std::strcmp(s1, s2);
}

GRAVITY_SYSV_ABI std::size_t hle_strcspn(const char* s, const char* reject) {
    if (!s || !reject) return 0;
    return std::strcspn(s, reject);
}

GRAVITY_SYSV_ABI char* hle_strpbrk(const char* s, const char* accept) {
    if (!s || !accept) return nullptr;
    return const_cast<char*>(std::strpbrk(s, accept));
}

GRAVITY_SYSV_ABI char* hle_strtok(char* str, const char* delim) {
    if (!delim) return nullptr;
    return std::strtok(str, delim);
}

GRAVITY_SYSV_ABI std::size_t hle_strnlen_s(const char* str, std::size_t maxsize) {
    if (!str) return 0;
    std::size_t count = 0;
    while (count < maxsize && str[count] != '\0') {
        ++count;
    }
    return count;
}

GRAVITY_SYSV_ABI int hle_memcpy_s(void* dest, std::size_t destsz, const void* src, std::size_t count) {
    if (!dest || destsz == 0) return 22; // EINVAL
    if (!src || count > destsz) {
        std::memset(dest, 0, destsz);
        return 22; // EINVAL / ERANGE
    }
    std::memcpy(dest, src, count);
    return 0;
}

// -----------------------------------------------------------------------------
// Conversions & Time
// -----------------------------------------------------------------------------

GRAVITY_SYSV_ABI double hle_Stod(const char* s, char** endptr, long pten) {
    if (!s) return 0.0;
    char* end = nullptr;
    double val = std::strtod(s, &end);
    if (pten != 0) {
        val *= std::pow(10.0, static_cast<double>(pten));
    }
    if (endptr && guest_memory::accessible(endptr, sizeof(char*))) {
        *endptr = end;
    }
    return val;
}

GRAVITY_SYSV_ABI long long hle_Stoll(const char* s, char** endptr, int base) {
    if (!s) return 0;
    char* end = nullptr;
    long long val = std::strtoll(s, &end, base);
    if (endptr && guest_memory::accessible(endptr, sizeof(char*))) {
        *endptr = end;
    }
    return val;
}

GRAVITY_SYSV_ABI unsigned long hle_Stoul(const char* s, char** endptr, int base) {
    if (!s) return 0;
    char* end = nullptr;
    unsigned long val = std::strtoul(s, &end, base);
    if (endptr && guest_memory::accessible(endptr, sizeof(char*))) {
        *endptr = end;
    }
    return val;
}

GRAVITY_SYSV_ABI unsigned long long hle_Stoull(const char* s, char** endptr, int base) {
    if (!s) return 0;
    char* end = nullptr;
    unsigned long long val = std::strtoull(s, &end, base);
    if (endptr && guest_memory::accessible(endptr, sizeof(char*))) {
        *endptr = end;
    }
    return val;
}

GRAVITY_SYSV_ABI struct std::tm* hle_gmtime(const std::time_t* timep) {
    if (!timep) return nullptr;
    return std::gmtime(timep);
}

GRAVITY_SYSV_ABI struct std::tm* hle_localtime(const std::time_t* timep) {
    if (!timep) return nullptr;
    return std::localtime(timep);
}

GRAVITY_SYSV_ABI std::time_t hle_mktime(struct std::tm* tm) {
    if (!tm) return static_cast<std::time_t>(-1);
    return std::mktime(tm);
}

GRAVITY_SYSV_ABI int hle_clock_gettime(int clock_id, void* tp) {
    (void)clock_id;
    struct Timespec { std::int64_t tv_sec; std::int64_t tv_nsec; };
    if (!tp || !guest_memory::accessible(tp, sizeof(Timespec))) return -1;

    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);

    auto* ts = static_cast<Timespec*>(tp);
    ts->tv_sec = counter.QuadPart / freq.QuadPart;
    ts->tv_nsec = ((counter.QuadPart % freq.QuadPart) * 1000000000LL) / freq.QuadPart;
    return 0;
}

// -----------------------------------------------------------------------------
// Standard I/O
// -----------------------------------------------------------------------------

GRAVITY_SYSV_ABI void hle_clearerr(void* stream) {
    if (auto* f = resolve_stream(stream)) {
        std::clearerr(f);
    }
}

GRAVITY_SYSV_ABI int hle_feof(void* stream) {
    if (auto* f = resolve_stream(stream)) {
        return std::feof(f);
    }
    return 1;
}

GRAVITY_SYSV_ABI int hle_ferror(void* stream) {
    if (auto* f = resolve_stream(stream)) {
        return std::ferror(f);
    }
    return 0;
}

GRAVITY_SYSV_ABI int hle_fflush(void* stream) {
    if (auto* f = resolve_stream(stream)) {
        return std::fflush(f);
    }
    return 0;
}

GRAVITY_SYSV_ABI char* hle_fgets(char* s, int size, void* stream) {
    if (!s || size <= 0) return nullptr;
    if (auto* f = resolve_stream(stream)) {
        return std::fgets(s, size, f);
    }
    return nullptr;
}

GRAVITY_SYSV_ABI int hle_fputc(int c, void* stream) {
    if (auto* f = resolve_stream(stream)) {
        return std::fputc(c, f);
    }
    return -1;
}

GRAVITY_SYSV_ABI std::size_t hle_fread(void* ptr, std::size_t size, std::size_t nmemb, void* stream) {
    if (!ptr || size == 0 || nmemb == 0) return 0;
    if (auto* f = resolve_stream(stream)) {
        return std::fread(ptr, size, nmemb, f);
    }
    return 0;
}

GRAVITY_SYSV_ABI void* hle_freopen(const char* filename, const char* mode, void* stream) {
    if (auto* f = resolve_stream(stream)) {
        return std::freopen(filename, mode, f);
    }
    return nullptr;
}

GRAVITY_SYSV_ABI int hle_fseek(void* stream, long offset, int whence) {
    if (auto* f = resolve_stream(stream)) {
        return std::fseek(f, offset, whence);
    }
    return -1;
}

GRAVITY_SYSV_ABI long hle_ftell(void* stream) {
    if (auto* f = resolve_stream(stream)) {
        return std::ftell(f);
    }
    return -1;
}

GRAVITY_SYSV_ABI std::size_t hle_fwrite(const void* ptr, std::size_t size, std::size_t nmemb, void* stream) {
    if (!ptr || size == 0 || nmemb == 0) return 0;
    if (auto* f = resolve_stream(stream)) {
        return std::fwrite(ptr, size, nmemb, f);
    }
    return 0;
}

GRAVITY_SYSV_ABI int hle_getc(void* stream) {
    if (auto* f = resolve_stream(stream)) {
        return std::fgetc(f);
    }
    return -1;
}

GRAVITY_SYSV_ABI int hle_ungetc(int c, void* stream) {
    if (auto* f = resolve_stream(stream)) {
        return std::ungetc(c, f);
    }
    return -1;
}

GRAVITY_SYSV_ABI void hle_perror(const char* s) {
    std::perror(s);
}

GRAVITY_SYSV_ABI int hle_remove(const char* pathname) {
    if (!pathname) return -1;
    return std::remove(pathname);
}

GRAVITY_SYSV_ABI int hle_rename(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return -1;
    return std::rename(oldpath, newpath);
}

GRAVITY_SYSV_ABI int hle_setvbuf(void* stream, char* buf, int mode, std::size_t size) {
    if (auto* f = resolve_stream(stream)) {
        return std::setvbuf(f, buf, mode, size);
    }
    return 0;
}

GRAVITY_SYSV_ABI int hle_fprintf(void* stream, const char* format, ...) {
    FILE* f = resolve_stream(stream);
    if (!f) f = stdout;
    if (!format) return 0;
    return std::fputs(format, f);
}

GRAVITY_SYSV_ABI int hle_fscanf(void* stream, const char* format, ...) {
    (void)stream; (void)format;
    return -1;
}

// -----------------------------------------------------------------------------
// C++ Runtime & Exceptions
// -----------------------------------------------------------------------------

GRAVITY_SYSV_ABI void hle_cxa_pure_virtual() {
    std::fprintf(stderr, "[LIBC] __cxa_pure_virtual called!\n");
}

GRAVITY_SYSV_ABI int hle_cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    std::lock_guard lock(g_atexit_mutex);
    g_atexit_handlers.push_back({func, arg, dso});
    return 0;
}

GRAVITY_SYSV_ABI void* hle_ios_base_Init_ctor(void* self) {
    return self;
}

GRAVITY_SYSV_ABI void hle_ios_base_Init_dtor(void* self) {
    (void)self;
}

GRAVITY_SYSV_ABI void* hle_cxa_allocate_exception(std::size_t thrown_size) {
    return std::malloc(thrown_size + 64);
}

GRAVITY_SYSV_ABI void hle_cxa_throw(void* thrown_exception, void* tinfo, void (*dest)(void*)) {
    (void)tinfo; (void)dest;
    std::fprintf(stderr, "[LIBC] __cxa_throw: exception at %p\n", thrown_exception);
}

GRAVITY_SYSV_ABI void hle_cxa_rethrow() {
    std::fprintf(stderr, "[LIBC] __cxa_rethrow called\n");
}

GRAVITY_SYSV_ABI void* hle_cxa_begin_catch(void* exception_object) {
    return exception_object;
}

GRAVITY_SYSV_ABI void hle_cxa_end_catch() {
}

GRAVITY_SYSV_ABI void hle_cxa_call_unexpected(void* exception_object) {
    (void)exception_object;
    std::fprintf(stderr, "[LIBC] __cxa_call_unexpected called\n");
}

GRAVITY_SYSV_ABI void hle_Unwind_Resume(void* exception_object) {
    (void)exception_object;
}

GRAVITY_SYSV_ABI int hle_gxx_personality_v0(int version, int actions, std::uint64_t exceptionClass, void* exceptionObject, void* context) {
    (void)version; (void)actions; (void)exceptionClass; (void)exceptionObject; (void)context;
    return 0; // _URC_CONTINUE_UNWIND
}

GRAVITY_SYSV_ABI void hle_terminate() {
    std::fprintf(stderr, "[LIBC] std::terminate called\n");
    std::exit(1);
}

// Variables / Data exports
const std::uint8_t hle_ZSt7nothrow = 0;
const void* hle_ZTIi = nullptr;
const void* hle_ZTVN10__cxxabiv117__class_type_infoE[4] = {nullptr, nullptr, nullptr, nullptr};
const void* hle_ZTVN10__cxxabiv120__si_class_type_infoE[4] = {nullptr, nullptr, nullptr, nullptr};

alignas(8) std::uint8_t hle_stdin[456] = {};
alignas(8) std::uint8_t hle_stdout[456] = {};
alignas(8) std::uint8_t hle_stderr[456] = {};
const std::uint32_t hle_P330P3dFF68 = 1;

namespace {
struct TablesInit {
    unsigned short ctype[257]{};
    short tolower[257]{};
    TablesInit() {
        for (int i = 0; i < 256; ++i) {
            unsigned short mask = 0;
            if (i >= 'A' && i <= 'Z') mask |= 0x0001;
            if (i >= 'a' && i <= 'z') mask |= 0x0002;
            if (i >= '0' && i <= '9') mask |= 0x0004;
            if (i == ' ' || (i >= 0x09 && i <= 0x0d)) mask |= 0x0008;
            if ((i >= 0x21 && i <= 0x2f) || (i >= 0x3a && i <= 0x40) ||
                (i >= 0x5b && i <= 0x60) || (i >= 0x7b && i <= 0x7e)) mask |= 0x0010;
            if ((i >= 0 && i <= 0x1f) || i == 0x7f) mask |= 0x0020;
            if (i == ' ' || i == '\t') mask |= 0x0040;
            if ((i >= '0' && i <= '9') || (i >= 'a' && i <= 'f') || (i >= 'A' && i <= 'F')) mask |= 0x0080;
            ctype[i + 1] = mask;
            tolower[i + 1] = (i >= 'A' && i <= 'Z') ? static_cast<short>(i + ('a' - 'A')) : static_cast<short>(i);
        }
        tolower[0] = -1; // EOF
    }
};
const TablesInit g_tables;
}

// -----------------------------------------------------------------------------
// Dinkumware Atomics & Locks
// -----------------------------------------------------------------------------

GRAVITY_SYSV_ABI int hle_Atomic_fetch_add_4(volatile int* ptr, int val) {
    if (!ptr) return 0;
    return InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(ptr), val);
}

GRAVITY_SYSV_ABI int hle_Atomic_fetch_sub_4(volatile int* ptr, int val) {
    if (!ptr) return 0;
    return InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(ptr), -val);
}

GRAVITY_SYSV_ABI void hle_Locksyslock(int lock_id) { (void)lock_id; }
GRAVITY_SYSV_ABI void hle_Unlocksyslock(int lock_id) { (void)lock_id; }

GRAVITY_SYSV_ABI const unsigned short* hle_Getpctype() {
    return &g_tables.ctype[1];
}

GRAVITY_SYSV_ABI const short* hle_Getptolower() {
    return &g_tables.tolower[1];
}

GRAVITY_SYSV_ABI void hle_Locinfo_dtor(void* self) { (void)self; }

// -----------------------------------------------------------------------------
// Mspace memory functions
// -----------------------------------------------------------------------------

GRAVITY_SYSV_ABI void* hle_sceLibcMspaceCreate(const char* name, void* base, std::size_t size, unsigned int flags) {
    (void)name; (void)base; (void)size; (void)flags;
    return reinterpret_cast<void*>(0x1);
}

GRAVITY_SYSV_ABI int hle_sceLibcMspaceDestroy(void* mspace) {
    (void)mspace;
    return 0;
}

GRAVITY_SYSV_ABI void* hle_sceLibcMspaceMalloc(void* mspace, std::size_t size) {
    (void)mspace;
    return std::malloc(size);
}

GRAVITY_SYSV_ABI void hle_sceLibcMspaceFree(void* mspace, void* ptr) {
    (void)mspace;
    if (ptr) std::free(ptr);
}

GRAVITY_SYSV_ABI void* hle_sceLibcMspaceRealloc(void* mspace, void* ptr, std::size_t size) {
    (void)mspace;
    return std::realloc(ptr, size);
}

GRAVITY_SYSV_ABI void* hle_sceLibcMspaceCalloc(void* mspace, std::size_t nmemb, std::size_t size) {
    (void)mspace;
    return std::calloc(nmemb, size);
}

GRAVITY_SYSV_ABI void* hle_sceLibcMspaceMemalign(void* mspace, std::size_t boundary, std::size_t size) {
    (void)mspace;
    if (boundary < sizeof(void*)) boundary = sizeof(void*);
    return _aligned_malloc(size, boundary);
}

GRAVITY_SYSV_ABI int hle_sceLibcMspacePosixMemalign(void* mspace, void** ptr, std::size_t boundary, std::size_t size) {
    (void)mspace;
    if (!ptr) return 22; // EINVAL
    if (boundary < sizeof(void*)) boundary = sizeof(void*);
    *ptr = _aligned_malloc(size, boundary);
    return *ptr ? 0 : 12; // ENOMEM
}

GRAVITY_SYSV_ABI int hle_sceLibcMspaceMallocStats(void* mspace, void* stats) {
    (void)mspace;
    if (stats) std::memset(stats, 0, 64);
    return 0;
}

GRAVITY_SYSV_ABI int hle_sceLibcMspaceMallocStatsFast(void* mspace, void* stats) {
    (void)mspace;
    if (stats) std::memset(stats, 0, 64);
    return 0;
}

GRAVITY_SYSV_ABI std::size_t hle_sceLibcMspaceMallocUsableSize(void* ptr) {
    if (!ptr) return 0;
    return _msize(ptr);
}

GRAVITY_SYSV_ABI void* hle_sceLibcMspaceReallocalign(void* mspace, void* ptr, std::size_t size, std::size_t boundary) {
    (void)mspace;
    if (boundary < sizeof(void*)) boundary = sizeof(void*);
    return _aligned_realloc(ptr, size, boundary);
}

// -----------------------------------------------------------------------------
// Kernel & Module helpers
// -----------------------------------------------------------------------------

GRAVITY_SYSV_ABI std::int64_t hle_sceKernelWrite(int fd, const void* buf, std::size_t count) {
    if (fd == 1 || fd == 2) {
        if (buf && count > 0) {
            FILE* out = (fd == 2) ? stderr : stdout;
            std::fwrite(buf, 1, count, out);
            std::fflush(out);
            return static_cast<std::int64_t>(count);
        }
        return 0;
    }
    return -1;
}

GRAVITY_SYSV_ABI int hle_sceKernelStat(const char* path, void* out_stat) {
    if (!path || !out_stat) return -1;
    return fs::vfs_stat(path, out_stat);
}

GRAVITY_SYSV_ABI int hle_sceKernelGetdents(int fd, char* buf, int nbytes) {
    if (fd < 0 || !buf || nbytes <= 0) return -1;
    return fs::vfs_readdir(fd, buf);
}

GRAVITY_SYSV_ABI void hle_scePthreadExit(void* value_ptr) {
    (void)value_ptr;
    std::fprintf(stderr, "[PTHREAD] scePthreadExit called\n");
    ExitThread(0);
}

GRAVITY_SYSV_ABI int hle_sceSysmoduleIsLoaded(std::uint16_t id) {
    (void)id;
    return 0;
}

GRAVITY_SYSV_ABI void* hle_tls_get_addr(const std::size_t ti[2]) {
    if (!ti) return nullptr;
    void* tp = NativeTls::current_thread_pointer();
    if (!tp) return nullptr;
    auto* control = static_cast<std::uintptr_t*>(tp);
    auto* dtv = reinterpret_cast<std::uintptr_t*>(control[1]);
    if (ti[0] == 1 && dtv) {
        auto base = dtv[2];
        return reinterpret_cast<void*>(base + ti[1]);
    }
    return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(tp) + ti[1]);
}

} // extern "C"

} // namespace gravity::hle::libc
