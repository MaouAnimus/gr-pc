#include "hle/libc/libc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string_view>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(false)

using namespace gravity::hle::libc;

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string_view suite = argv[1];

    if (suite == "math") {
        CHECK(std::abs(hle_Cosh(0.0) - 1.0) < 1e-6);
        CHECK(std::abs(hle_Sinh(0.0)) < 1e-6);
        CHECK(std::abs(hle_Sin(0.0)) < 1e-6);
        CHECK(std::abs(hle_Log(1.0)) < 1e-6);
        CHECK(std::abs(hle_FLog(1.0f)) < 1e-5f);
        CHECK(hle_fmax(2.5, 3.5) == 3.5);
        CHECK(hle_fmaxf(2.5f, 3.5f) == 3.5f);
        CHECK(hle_fminf(2.5f, 3.5f) == 2.5f);

        int exp = 0;
        double frac = hle_frexp(8.0, &exp);
        CHECK(frac == 0.5 && exp == 4);

        double ld = hle_ldexp(0.5, 4);
        CHECK(ld == 8.0);

        double iptr = 0.0;
        double mfrac = hle_modf(3.75, &iptr);
        CHECK(iptr == 3.0 && std::abs(mfrac - 0.75) < 1e-6);

        CHECK(hle_difftime(100, 40) == 60.0);
    } else if (suite == "string_memory") {
        const char* err = hle_strerror(0);
        CHECK(err != nullptr);

        CHECK(hle_strcoll("abc", "abc") == 0);
        CHECK(hle_strcspn("hello world", " ") == 5);

        const char* pbrk = hle_strpbrk("abcdef", "dxz");
        CHECK(pbrk != nullptr && *pbrk == 'd');

        char str_tok_buf[] = "foo,bar,baz";
        char* token = hle_strtok(str_tok_buf, ",");
        CHECK(token != nullptr && std::strcmp(token, "foo") == 0);
        token = hle_strtok(nullptr, ",");
        CHECK(token != nullptr && std::strcmp(token, "bar") == 0);

        const char test_str[] = "12345";
        CHECK(hle_strnlen_s(test_str, 10) == 5);
        CHECK(hle_strnlen_s(test_str, 3) == 3);

        char dst[16] = {};
        CHECK(hle_memcpy_s(dst, sizeof(dst), "hello", 6) == 0);
        CHECK(std::strcmp(dst, "hello") == 0);
        // Overrun check
        CHECK(hle_memcpy_s(dst, 4, "hello", 6) != 0);
    } else if (suite == "conversions") {
        char* end = nullptr;
        double d = hle_Stod("123.456", &end, 0);
        CHECK(std::abs(d - 123.456) < 1e-4);
        CHECK(end != nullptr);

        long long ll = hle_Stoll("-1234567890", &end, 10);
        CHECK(ll == -1234567890LL);

        unsigned long ul = hle_Stoul("0x100", &end, 0);
        CHECK(ul == 256);

        unsigned long long ull = hle_Stoull("18446744073709551615", &end, 10);
        CHECK(ull == 18446744073709551615ULL);

        std::time_t t = 1600000000;
        struct std::tm* gm = hle_gmtime(&t);
        CHECK(gm != nullptr);
        struct std::tm* loc = hle_localtime(&t);
        CHECK(loc != nullptr);
        std::time_t back = hle_mktime(loc);
        CHECK(back != static_cast<std::time_t>(-1));
    } else if (suite == "cxx_runtime") {
        void* ex = hle_cxa_allocate_exception(32);
        CHECK(ex != nullptr);
        void* caught = hle_cxa_begin_catch(ex);
        CHECK(caught == ex);
        hle_cxa_end_catch();
        std::free(ex);

        void* dummy_ios[4] = {};
        CHECK(hle_ios_base_Init_ctor(dummy_ios) == dummy_ios);
        hle_ios_base_Init_dtor(dummy_ios);
    } else if (suite == "mspace") {
        void* ms = hle_sceLibcMspaceCreate("test", nullptr, 1024*1024, 0);
        CHECK(ms != nullptr);
        void* p = hle_sceLibcMspaceMalloc(ms, 128);
        CHECK(p != nullptr);
        CHECK(hle_sceLibcMspaceMallocUsableSize(p) >= 128);
        p = hle_sceLibcMspaceRealloc(ms, p, 256);
        CHECK(p != nullptr);
        void* c = hle_sceLibcMspaceCalloc(ms, 4, 32);
        CHECK(c != nullptr);
        void* aligned = hle_sceLibcMspaceMemalign(ms, 64, 128);
        CHECK(aligned != nullptr);
        CHECK((reinterpret_cast<std::uintptr_t>(aligned) & 63) == 0);
        void* posix_aligned = nullptr;
        CHECK(hle_sceLibcMspacePosixMemalign(ms, &posix_aligned, 64, 128) == 0);
        CHECK(posix_aligned != nullptr);
        CHECK((reinterpret_cast<std::uintptr_t>(posix_aligned) & 63) == 0);
        hle_sceLibcMspaceFree(ms, p);
        hle_sceLibcMspaceFree(ms, c);
        _aligned_free(aligned);
        _aligned_free(posix_aligned);
        CHECK(hle_sceLibcMspaceDestroy(ms) == 0);
    } else if (suite == "atomics_tables") {
        volatile int counter = 10;
        CHECK(hle_Atomic_fetch_add_4(&counter, 5) == 10);
        CHECK(counter == 15);
        CHECK(hle_Atomic_fetch_sub_4(&counter, 3) == 15);
        CHECK(counter == 12);

        hle_Locksyslock(0);
        hle_Unlocksyslock(0);

        const unsigned short* ctype = hle_Getpctype();
        CHECK(ctype != nullptr);
        CHECK((ctype['A'] & 0x0001) != 0); // _UPPER
        CHECK((ctype['a'] & 0x0002) != 0); // _LOWER
        CHECK((ctype['5'] & 0x0004) != 0); // _DIGIT

        const short* tolower = hle_Getptolower();
        CHECK(tolower != nullptr);
        CHECK(tolower['A'] == 'a');
        CHECK(tolower['b'] == 'b');
    } else return 2;

    std::puts("PASS libc");
    return 0;
}
