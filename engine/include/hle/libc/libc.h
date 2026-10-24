#pragma once

#include "hle/abi.h"
#include <cstdint>
#include <cstddef>
#include <ctime>

namespace gravity::hle::libc {

extern "C" {
// Math & Floats
GRAVITY_SYSV_ABI double hle_Cosh(double x);
GRAVITY_SYSV_ABI double hle_Sinh(double x);
GRAVITY_SYSV_ABI double hle_Sin(double x);
GRAVITY_SYSV_ABI double hle_Log(double x);
GRAVITY_SYSV_ABI float  hle_FLog(float x);
GRAVITY_SYSV_ABI double hle_fmax(double x, double y);
GRAVITY_SYSV_ABI float  hle_fmaxf(float x, float y);
GRAVITY_SYSV_ABI float  hle_fminf(float x, float y);
GRAVITY_SYSV_ABI double hle_frexp(double x, int* exp);
GRAVITY_SYSV_ABI double hle_ldexp(double x, int exp);
GRAVITY_SYSV_ABI double hle_modf(double x, double* iptr);
GRAVITY_SYSV_ABI double hle_difftime(std::time_t time1, std::time_t time0);

// String & Memory
GRAVITY_SYSV_ABI const char* hle_strerror(int errnum);
GRAVITY_SYSV_ABI int         hle_strcoll(const char* s1, const char* s2);
GRAVITY_SYSV_ABI std::size_t hle_strcspn(const char* s, const char* reject);
GRAVITY_SYSV_ABI char*       hle_strpbrk(const char* s, const char* accept);
GRAVITY_SYSV_ABI char*       hle_strtok(char* str, const char* delim);
GRAVITY_SYSV_ABI std::size_t hle_strnlen_s(const char* str, std::size_t maxsize);
GRAVITY_SYSV_ABI int         hle_memcpy_s(void* dest, std::size_t destsz, const void* src, std::size_t count);

// Conversions & Time
GRAVITY_SYSV_ABI double             hle_Stod(const char* s, char** endptr, long pten);
GRAVITY_SYSV_ABI long long          hle_Stoll(const char* s, char** endptr, int base);
GRAVITY_SYSV_ABI unsigned long      hle_Stoul(const char* s, char** endptr, int base);
GRAVITY_SYSV_ABI unsigned long long hle_Stoull(const char* s, char** endptr, int base);
GRAVITY_SYSV_ABI struct std::tm*    hle_gmtime(const std::time_t* timep);
GRAVITY_SYSV_ABI struct std::tm*    hle_localtime(const std::time_t* timep);
GRAVITY_SYSV_ABI std::time_t        hle_mktime(struct std::tm* tm);
GRAVITY_SYSV_ABI int                hle_clock_gettime(int clock_id, void* tp);

// Standard I/O
GRAVITY_SYSV_ABI void        hle_clearerr(void* stream);
GRAVITY_SYSV_ABI int         hle_feof(void* stream);
GRAVITY_SYSV_ABI int         hle_ferror(void* stream);
GRAVITY_SYSV_ABI int         hle_fflush(void* stream);
GRAVITY_SYSV_ABI char*       hle_fgets(char* s, int size, void* stream);
GRAVITY_SYSV_ABI int         hle_fputc(int c, void* stream);
GRAVITY_SYSV_ABI std::size_t hle_fread(void* ptr, std::size_t size, std::size_t nmemb, void* stream);
GRAVITY_SYSV_ABI void*       hle_freopen(const char* filename, const char* mode, void* stream);
GRAVITY_SYSV_ABI int         hle_fseek(void* stream, long offset, int whence);
GRAVITY_SYSV_ABI long        hle_ftell(void* stream);
GRAVITY_SYSV_ABI std::size_t hle_fwrite(const void* ptr, std::size_t size, std::size_t nmemb, void* stream);
GRAVITY_SYSV_ABI int         hle_getc(void* stream);
GRAVITY_SYSV_ABI int         hle_ungetc(int c, void* stream);
GRAVITY_SYSV_ABI void        hle_perror(const char* s);
GRAVITY_SYSV_ABI int         hle_remove(const char* pathname);
GRAVITY_SYSV_ABI int         hle_rename(const char* oldpath, const char* newpath);
GRAVITY_SYSV_ABI int         hle_setvbuf(void* stream, char* buf, int mode, std::size_t size);
GRAVITY_SYSV_ABI int         hle_fprintf(void* stream, const char* format, ...);
GRAVITY_SYSV_ABI int         hle_fscanf(void* stream, const char* format, ...);

// C++ Runtime & Exceptions
GRAVITY_SYSV_ABI void        hle_cxa_pure_virtual();
GRAVITY_SYSV_ABI int         hle_cxa_atexit(void (*func)(void*), void* arg, void* dso);
GRAVITY_SYSV_ABI void*       hle_ios_base_Init_ctor(void* self);
GRAVITY_SYSV_ABI void        hle_ios_base_Init_dtor(void* self);
GRAVITY_SYSV_ABI void*       hle_cxa_allocate_exception(std::size_t thrown_size);
GRAVITY_SYSV_ABI void        hle_cxa_throw(void* thrown_exception, void* tinfo, void (*dest)(void*));
GRAVITY_SYSV_ABI void        hle_cxa_rethrow();
GRAVITY_SYSV_ABI void*       hle_cxa_begin_catch(void* exception_object);
GRAVITY_SYSV_ABI void        hle_cxa_end_catch();
GRAVITY_SYSV_ABI void        hle_cxa_call_unexpected(void* exception_object);
GRAVITY_SYSV_ABI void        hle_Unwind_Resume(void* exception_object);
GRAVITY_SYSV_ABI int         hle_gxx_personality_v0(int version, int actions, std::uint64_t exceptionClass, void* exceptionObject, void* context);
GRAVITY_SYSV_ABI void        hle_terminate();

// Variables / Data exports
extern const std::uint8_t    hle_ZSt7nothrow;
extern const void*           hle_ZTIi;
extern const void*           hle_ZTVN10__cxxabiv117__class_type_infoE[4];
extern const void*           hle_ZTVN10__cxxabiv120__si_class_type_infoE[4];
extern std::uint8_t hle_stdin[456];
extern std::uint8_t hle_stdout[456];
extern std::uint8_t hle_stderr[456];
extern const std::uint32_t   hle_P330P3dFF68;

// Dinkumware Atomics & Locks
GRAVITY_SYSV_ABI int         hle_Atomic_fetch_add_4(volatile int* ptr, int val);
GRAVITY_SYSV_ABI int         hle_Atomic_fetch_sub_4(volatile int* ptr, int val);
GRAVITY_SYSV_ABI void        hle_Locksyslock(int lock_id);
GRAVITY_SYSV_ABI void        hle_Unlocksyslock(int lock_id);
GRAVITY_SYSV_ABI const unsigned short* hle_Getpctype();
GRAVITY_SYSV_ABI const short*          hle_Getptolower();
GRAVITY_SYSV_ABI void        hle_Locinfo_dtor(void* self);

// Mspace memory functions
GRAVITY_SYSV_ABI void*       hle_sceLibcMspaceCreate(const char* name, void* base, std::size_t size, unsigned int flags);
GRAVITY_SYSV_ABI int         hle_sceLibcMspaceDestroy(void* mspace);
GRAVITY_SYSV_ABI void*       hle_sceLibcMspaceMalloc(void* mspace, std::size_t size);
GRAVITY_SYSV_ABI void        hle_sceLibcMspaceFree(void* mspace, void* ptr);
GRAVITY_SYSV_ABI void*       hle_sceLibcMspaceRealloc(void* mspace, void* ptr, std::size_t size);
GRAVITY_SYSV_ABI void*       hle_sceLibcMspaceCalloc(void* mspace, std::size_t nmemb, std::size_t size);
GRAVITY_SYSV_ABI void*       hle_sceLibcMspaceMemalign(void* mspace, std::size_t boundary, std::size_t size);
GRAVITY_SYSV_ABI int         hle_sceLibcMspacePosixMemalign(void* mspace, void** ptr, std::size_t boundary, std::size_t size);
GRAVITY_SYSV_ABI int         hle_sceLibcMspaceMallocStats(void* mspace, void* stats);
GRAVITY_SYSV_ABI int         hle_sceLibcMspaceMallocStatsFast(void* mspace, void* stats);
GRAVITY_SYSV_ABI std::size_t hle_sceLibcMspaceMallocUsableSize(void* ptr);
GRAVITY_SYSV_ABI void*       hle_sceLibcMspaceReallocalign(void* mspace, void* ptr, std::size_t size, std::size_t boundary);

// Kernel & Module helpers
GRAVITY_SYSV_ABI std::int64_t hle_sceKernelWrite(int fd, const void* buf, std::size_t count);
GRAVITY_SYSV_ABI int          hle_sceKernelStat(const char* path, void* out_stat);
GRAVITY_SYSV_ABI int          hle_sceKernelGetdents(int fd, char* buf, int nbytes);
GRAVITY_SYSV_ABI void         hle_scePthreadExit(void* value_ptr);
GRAVITY_SYSV_ABI int          hle_sceSysmoduleIsLoaded(std::uint16_t id);
GRAVITY_SYSV_ABI void*        hle_tls_get_addr(const std::size_t ti[2]);
}

} // namespace gravity::hle::libc
