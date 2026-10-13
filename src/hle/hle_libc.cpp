#include "hle_libc.h"
#include "../core/resolver.h"
#include <windows.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <iostream>

static HANDLE s_guest_heap = nullptr;

extern "C" {

void* __attribute__((sysv_abi)) HLE_malloc(size_t size) {
    if (size == 0) size = 1;
    size_t alloc_sz = (size + 63 + 15) & ~15;
    if (!s_guest_heap) s_guest_heap = HeapCreate(0, 0, 0);
    return HeapAlloc(s_guest_heap, HEAP_ZERO_MEMORY, alloc_sz);
}

void __attribute__((sysv_abi)) HLE_free(void* ptr) {
    if (!ptr || !s_guest_heap) return;
    if (HeapValidate(s_guest_heap, 0, ptr)) {
        HeapFree(s_guest_heap, 0, ptr);
    }
}

void* __attribute__((sysv_abi)) HLE_calloc(size_t num, size_t size) {
    return HLE_malloc(num * size);
}

void* __attribute__((sysv_abi)) HLE_realloc(void* ptr, size_t new_size) {
    if (!ptr) return HLE_malloc(new_size);
    if (new_size == 0) {
        HLE_free(ptr);
        return nullptr;
    }
    size_t alloc_sz = (new_size + 63 + 15) & ~15;
    if (!s_guest_heap) s_guest_heap = HeapCreate(0, 0, 0);
    if (HeapValidate(s_guest_heap, 0, ptr)) {
        return HeapReAlloc(s_guest_heap, HEAP_ZERO_MEMORY, ptr, alloc_sz);
    }
    void* new_ptr = HeapAlloc(s_guest_heap, HEAP_ZERO_MEMORY, alloc_sz);
    if (new_ptr) {
        std::memcpy(new_ptr, ptr, new_size);
    }
    return new_ptr;
}

typedef int (*SysVComparator)(const void*, const void*) __attribute__((sysv_abi));

void __attribute__((sysv_abi)) HLE_qsort(void* base, size_t num, size_t size, SysVComparator comp) {
    if (!base || num < 2 || size == 0 || !comp) return;

    char* b = static_cast<char*>(base);
    std::vector<char> temp(size);

    auto swap_elems = [&](size_t i, size_t j) {
        if (i == j) return;
        std::memcpy(temp.data(), b + i * size, size);
        std::memcpy(b + i * size, b + j * size, size);
        std::memcpy(b + j * size, temp.data(), size);
    };

    auto qsort_rec = [&](auto& self, int64_t low, int64_t high) -> void {
        if (low >= high) return;
        if (high - low < 16) {
            for (int64_t i = low + 1; i <= high; i++) {
                for (int64_t j = i; j > low; j--) {
                    if (comp(b + j * size, b + (j - 1) * size) < 0) {
                        swap_elems(j, j - 1);
                    } else {
                        break;
                    }
                }
            }
            return;
        }

        int64_t mid = low + (high - low) / 2;
        swap_elems(mid, high);
        const void* pivot = b + high * size;

        int64_t i = low;
        for (int64_t j = low; j < high; j++) {
            if (comp(b + j * size, pivot) < 0) {
                swap_elems(i, j);
                i++;
            }
        }
        swap_elems(i, high);

        self(self, low, i - 1);
        self(self, i + 1, high);
    };

    qsort_rec(qsort_rec, 0, static_cast<int64_t>(num - 1));
}

int __attribute__((sysv_abi)) HLE_sscanf(const char* str, const char* format,
                                        void* a1, void* a2, void* a3, void* a4,
                                        void* a5, void* a6, void* a7, void* a8) {
    if (!str || !format) return 0;
    return sscanf(str, format, a1, a2, a3, a4, a5, a6, a7, a8);
}

void* __attribute__((sysv_abi)) HLE_memcpy(void* dest, const void* src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    return std::memcpy(dest, src, n);
}

void* __attribute__((sysv_abi)) HLE_memmove(void* dest, const void* src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    return std::memmove(dest, src, n);
}

void* __attribute__((sysv_abi)) HLE_memset(void* s, int c, size_t n) {
    if (!s || n == 0) return s;
    return std::memset(s, c, n);
}

const void* __attribute__((sysv_abi)) HLE_memchr(const void* s, int c, size_t n) {
    if (!s) return nullptr;
    return std::memchr(s, c, n);
}

int __attribute__((sysv_abi)) HLE_memcmp(const void* s1, const void* s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    return std::memcmp(s1, s2, n);
}

size_t __attribute__((sysv_abi)) HLE_strlen(const char* s) {
    if (!s) return 0;
    return std::strlen(s);
}

int __attribute__((sysv_abi)) HLE_strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return std::strcmp(s1, s2);
}

int __attribute__((sysv_abi)) HLE_strncmp(const char* s1, const char* s2, size_t n) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return std::strncmp(s1, s2, n);
}

char* __attribute__((sysv_abi)) HLE_strcpy(char* dest, const char* src) {
    if (!dest || !src) return dest;
    return std::strcpy(dest, src);
}

char* __attribute__((sysv_abi)) HLE_strncpy(char* dest, const char* src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    return std::strncpy(dest, src, n);
}

char* __attribute__((sysv_abi)) HLE_strcat(char* dest, const char* src) {
    if (!dest || !src) return dest;
    return std::strcat(dest, src);
}

char* __attribute__((sysv_abi)) HLE_strncat(char* dest, const char* src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    return std::strncat(dest, src, n);
}

char* __attribute__((sysv_abi)) HLE_strchr(const char* s, int c) {
    if (!s) return nullptr;
    return const_cast<char*>(std::strchr(s, c));
}

char* __attribute__((sysv_abi)) HLE_strrchr(const char* s, int c) {
    if (!s) return nullptr;
    return const_cast<char*>(std::strrchr(s, c));
}

char* __attribute__((sysv_abi)) HLE_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return nullptr;
    return const_cast<char*>(std::strstr(haystack, needle));
}

int __attribute__((sysv_abi)) HLE_strcasecmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return _stricmp(s1, s2);
}

int __attribute__((sysv_abi)) HLE_strncasecmp(const char* s1, const char* s2, size_t n) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    return _strnicmp(s1, s2, n);
}

uint64_t __attribute__((sysv_abi)) HLE_strtoull(const char* nptr, char** endptr, int base) {
    if (!nptr) return 0;
    return std::strtoull(nptr, endptr, base);
}

int64_t __attribute__((sysv_abi)) HLE_strtol(const char* nptr, char** endptr, int base) {
    if (!nptr) return 0;
    return std::strtol(nptr, endptr, base);
}

uint64_t __attribute__((sysv_abi)) HLE_strtoul(const char* nptr, char** endptr, int base) {
    if (!nptr) return 0;
    return std::strtoul(nptr, endptr, base);
}

int64_t __attribute__((sysv_abi)) HLE_time(int64_t* tloc) {
    time_t t = std::time(nullptr);
    if (tloc) *tloc = static_cast<int64_t>(t);
    return static_cast<int64_t>(t);
}

int __attribute__((sysv_abi)) HLE_localtime_s(struct tm* tm_out, const time_t* timep) {
    if (!tm_out || !timep) return -1;
    return localtime_s(tm_out, timep);
}

int __attribute__((sysv_abi)) HLE_gmtime_s(struct tm* tm_out, const time_t* timep) {
    if (!tm_out || !timep) return -1;
    return gmtime_s(tm_out, timep);
}

double __attribute__((sysv_abi)) HLE_cos(double x) { return std::cos(x); }
float  __attribute__((sysv_abi)) HLE_cosf(float x) { return std::cosf(x); }
double __attribute__((sysv_abi)) HLE_sin(double x) { return std::sin(x); }
float  __attribute__((sysv_abi)) HLE_sinf(float x) { return std::sinf(x); }
float  __attribute__((sysv_abi)) HLE_expf(float x) { return std::expf(x); }
double __attribute__((sysv_abi)) HLE_pow(double b, double e) { return std::pow(b, e); }
float  __attribute__((sysv_abi)) HLE_powf(float b, float e) { return std::powf(b, e); }
float  __attribute__((sysv_abi)) HLE_fmodf(float x, float y) { return std::fmodf(x, y); }
float  __attribute__((sysv_abi)) HLE_atan2f(float y, float x) { return std::atan2f(y, x); }
float  __attribute__((sysv_abi)) HLE_atanf(float x) { return std::atanf(x); }
float  __attribute__((sysv_abi)) HLE_tanf(float x) { return std::tanf(x); }
float  __attribute__((sysv_abi)) HLE_acosf(float x) { return std::acosf(x); }
float  __attribute__((sysv_abi)) HLE_asinf(float x) { return std::asinf(x); }
float  __attribute__((sysv_abi)) HLE_sqrtf(float x) { return std::sqrtf(x); }
float  __attribute__((sysv_abi)) HLE_fabsf(float x) { return std::fabsf(x); }
float  __attribute__((sysv_abi)) HLE_ceilf(float x) { return std::ceilf(x); }
float  __attribute__((sysv_abi)) HLE_floorf(float x) { return std::floorf(x); }

int __attribute__((sysv_abi)) HLE_printf(const char* fmt, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    if (!fmt) return 0;
    char buf[1024];
    snprintf(buf, sizeof(buf), fmt, a2, a3, a4, a5, a6);
    std::cout << "[GUEST_PRINT] " << buf;
    return 0;
}

int __attribute__((sysv_abi)) HLE_puts(const char* s) {
    if (s) std::cout << "[GUEST_PUTS] " << s << "\n";
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_cxa_atexit(void* func, void* arg, void* dso) {
    (void)func; (void)arg; (void)dso;
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_cxa_guard_acquire(int64_t* guard) {
    if (*reinterpret_cast<uint8_t*>(guard) == 0) return 1;
    return 0;
}

void __attribute__((sysv_abi)) HLE_cxa_guard_release(int64_t* guard) {
    *reinterpret_cast<uint8_t*>(guard) = 1;
}

int32_t __attribute__((sysv_abi)) HLE_Atomic_fetch_sub_4(volatile int32_t* ptr, int32_t val) {
    return InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(ptr), -val);
}

struct InternalMtx {
    SRWLOCK srw;
    DWORD owner_tid;
    int32_t recurse_cnt;
};

struct InternalCnd {
    CONDITION_VARIABLE cv;
};

int __attribute__((sysv_abi)) HLE_Mtx_init(void* mtx, int type) {
    (void)mtx; (void)type;
    return 0;
}

int __attribute__((sysv_abi)) HLE_Mtx_destroy(void* mtx) {
    (void)mtx;
    return 0;
}

int __attribute__((sysv_abi)) HLE_Mtx_lock(void* mtx) {
    (void)mtx;
    return 0;
}

int __attribute__((sysv_abi)) HLE_Mtx_unlock(void* mtx) {
    (void)mtx;
    return 0;
}

int __attribute__((sysv_abi)) HLE_Cnd_init(void* cnd) {
    if (cnd) {
        InitializeConditionVariable(&reinterpret_cast<InternalCnd*>(cnd)->cv);
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_Cnd_destroy(void* cnd) {
    (void)cnd;
    return 0;
}

int __attribute__((sysv_abi)) HLE_Cnd_signal(void* cnd) {
    if (cnd) {
        WakeConditionVariable(&reinterpret_cast<InternalCnd*>(cnd)->cv);
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_Cnd_broadcast(void* cnd) {
    if (cnd) {
        WakeAllConditionVariable(&reinterpret_cast<InternalCnd*>(cnd)->cv);
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_Cnd_wait(void* cnd, void* mtx) {
    (void)cnd;
    if (mtx) HLE_Mtx_unlock(mtx);
    SwitchToThread();
    if (mtx) HLE_Mtx_lock(mtx);
    return 0;
}

static uint16_t s_pctype_table[512];
static bool s_pctype_init = false;

static void InitPctypeTable() {
    if (s_pctype_init) return;
    for (int i = 0; i < 512; i++) {
        int c = ((i - 128) % 256 + 256) % 256;
        uint16_t flags = 0;
        if (std::isupper(c)) flags |= (0x0001 | 0x0100 | 0x0200);
        if (std::islower(c)) flags |= (0x0002 | 0x0100);
        if (std::isdigit(c)) flags |= (0x0004 | 0x0080);
        if (std::isspace(c)) flags |= 0x0008;
        if (std::ispunct(c)) flags |= 0x0010;
        if (std::iscntrl(c)) flags |= 0x0020;
        if (c == ' ' || c == '\t') flags |= 0x0040;
        if (std::isxdigit(c)) flags |= 0x0080;
        s_pctype_table[i] = flags;
    }
    s_pctype_init = true;
}

const uint16_t* __attribute__((sysv_abi)) HLE_Getpctype() {
    InitPctypeTable();
    return &s_pctype_table[128];
}

static short s_tolower_table[512];
static short s_toupper_table[512];
static bool s_ctype_tables_init = false;

static void InitCtypeTables() {
    if (s_ctype_tables_init) return;
    for (int i = 0; i < 512; i++) {
        int c = i - 128;
        s_tolower_table[i] = static_cast<short>(std::tolower(static_cast<unsigned char>(c & 0xff)));
        s_toupper_table[i] = static_cast<short>(std::toupper(static_cast<unsigned char>(c & 0xff)));
    }
    s_ctype_tables_init = true;
}

const short* __attribute__((sysv_abi)) HLE_Getptolower() {
    InitCtypeTables();
    return &s_tolower_table[128];
}

const short* __attribute__((sysv_abi)) HLE_Getptoupper() {
    InitCtypeTables();
    return &s_toupper_table[128];
}

int64_t __attribute__((sysv_abi)) HLE_Facet_Getcat(void** ppfacet, void* ploc) {
    (void)ploc;
    static uint64_t s_facet_vtable[64];
    static uint8_t s_facet_obj[256];
    static bool s_facet_init = false;
    if (!s_facet_init) {
        for (int i = 0; i < 64; i++) s_facet_vtable[i] = reinterpret_cast<uint64_t>(&DummyVMethod);
        std::memset(s_facet_obj, 0, sizeof(s_facet_obj));
        *reinterpret_cast<uint64_t*>(s_facet_obj) = reinterpret_cast<uint64_t>(s_facet_vtable);
        s_facet_init = true;
    }
    if (ppfacet) {
        *ppfacet = s_facet_obj;
    }
    return static_cast<int64_t>(-1);
}

int64_t __attribute__((sysv_abi)) HLE_Locksyslock(int lock_idx) {
    (void)lock_idx;
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_Unlocksyslock(int lock_idx) {
    (void)lock_idx;
    return 0;
}

int __attribute__((sysv_abi)) HLE_Cnd_timedwait(void* cnd, void* mtx, const void* ts) {
    (void)ts;
    return HLE_Cnd_wait(cnd, mtx);
}

} // extern "C"

void RegisterLibcHLE() {
    if (!s_guest_heap) {
        s_guest_heap = HeapCreate(0, 0, 0);
    }

    // Memory
    RegisterHandler("malloc", reinterpret_cast<SysVHandler>(HLE_malloc));
    RegisterHandler("gQX+4GDQjpM", reinterpret_cast<SysVHandler>(HLE_malloc));      // malloc NID
    RegisterHandler("free", reinterpret_cast<SysVHandler>(HLE_free));
    RegisterHandler("tIhsqj0qsFE", reinterpret_cast<SysVHandler>(HLE_free));        // free NID
    RegisterHandler("calloc", reinterpret_cast<SysVHandler>(HLE_calloc));
    RegisterHandler("2X5agFjKxMc", reinterpret_cast<SysVHandler>(HLE_calloc));      // calloc NID
    RegisterHandler("realloc", reinterpret_cast<SysVHandler>(HLE_realloc));
    RegisterHandler("aaDMGGkXFxo", reinterpret_cast<SysVHandler>(HLE_realloc));     // realloc NID
    RegisterHandler("lbB+UlZqVG0", reinterpret_cast<SysVHandler>(HLE_realloc));     // realloc NID
    RegisterHandler("rQFVBXp-Cxg", reinterpret_cast<SysVHandler>(HLE_realloc));     // realloc NID
    RegisterHandler("fJnpuVVBbKk", reinterpret_cast<SysVHandler>(HLE_malloc));      // operator new
    RegisterHandler("hdm0YfMa7TQ", reinterpret_cast<SysVHandler>(HLE_malloc));      // operator new
    RegisterHandler("ryUxD-60bKM", reinterpret_cast<SysVHandler>(HLE_free));        // operator delete
    RegisterHandler("z+P+xCnWLBk", reinterpret_cast<SysVHandler>(HLE_free));        // operator delete
    RegisterHandler("MLWl90SFWNE", reinterpret_cast<SysVHandler>(HLE_free));        // operator delete[]
    RegisterHandler("qsort", reinterpret_cast<SysVHandler>(HLE_qsort));
    RegisterHandler("AEJdIVZTEmo", reinterpret_cast<SysVHandler>(HLE_qsort));       // qsort NID
    RegisterHandler("sscanf", reinterpret_cast<SysVHandler>(HLE_sscanf));
    RegisterHandler("1Pk0qZQGeWo", reinterpret_cast<SysVHandler>(HLE_sscanf));      // sscanf NID

    RegisterHandler("memcpy", reinterpret_cast<SysVHandler>(HLE_memcpy));
    RegisterHandler("Q3VBxCXhUHs", reinterpret_cast<SysVHandler>(HLE_memcpy));      // memcpy NID
    RegisterHandler("memmove", reinterpret_cast<SysVHandler>(HLE_memmove));
    RegisterHandler("+P6FRGH4LfA", reinterpret_cast<SysVHandler>(HLE_memmove));     // memmove NID
    RegisterHandler("memset", reinterpret_cast<SysVHandler>(HLE_memset));
    RegisterHandler("8zTFvBIAIN8", reinterpret_cast<SysVHandler>(HLE_memset));      // memset NID
    RegisterHandler("memchr", reinterpret_cast<SysVHandler>(HLE_memchr));
    RegisterHandler("8u8lPzUEq+U", reinterpret_cast<SysVHandler>(HLE_memchr));      // memchr NID
    RegisterHandler("memcmp", reinterpret_cast<SysVHandler>(HLE_memcmp));
    RegisterHandler("DfivPArhucg", reinterpret_cast<SysVHandler>(HLE_memcmp));      // memcmp NID

    // Strings
    RegisterHandler("strlen", reinterpret_cast<SysVHandler>(HLE_strlen));
    RegisterHandler("j4ViWNHEgww", reinterpret_cast<SysVHandler>(HLE_strlen));      // strlen NID
    RegisterHandler("strcmp", reinterpret_cast<SysVHandler>(HLE_strcmp));
    RegisterHandler("Ovb2dSJOAuE", reinterpret_cast<SysVHandler>(HLE_strcmp));      // strcmp NID
    RegisterHandler("strncmp", reinterpret_cast<SysVHandler>(HLE_strncmp));
    RegisterHandler("aesyjrHVWy4", reinterpret_cast<SysVHandler>(HLE_strncmp));     // strncmp NID
    RegisterHandler("strcpy", reinterpret_cast<SysVHandler>(HLE_strcpy));
    RegisterHandler("kiZSXIWd9vg", reinterpret_cast<SysVHandler>(HLE_strcpy));      // strcpy NID
    RegisterHandler("strncpy", reinterpret_cast<SysVHandler>(HLE_strncpy));
    RegisterHandler("6sJWiWSRuqk", reinterpret_cast<SysVHandler>(HLE_strncpy));     // strncpy NID
    RegisterHandler("strcat", reinterpret_cast<SysVHandler>(HLE_strcat));
    RegisterHandler("Ls4tzzhimqQ", reinterpret_cast<SysVHandler>(HLE_strcat));      // strcat NID
    RegisterHandler("strncat", reinterpret_cast<SysVHandler>(HLE_strncat));
    RegisterHandler("kHg45qPC6f0", reinterpret_cast<SysVHandler>(HLE_strncat));     // strncat NID
    RegisterHandler("strchr", reinterpret_cast<SysVHandler>(HLE_strchr));
    RegisterHandler("ob5xAW4ln-0", reinterpret_cast<SysVHandler>(HLE_strchr));      // strchr NID
    RegisterHandler("strrchr", reinterpret_cast<SysVHandler>(HLE_strrchr));
    RegisterHandler("viiwFMaNamA", reinterpret_cast<SysVHandler>(HLE_strrchr));     // strrchr NID
    RegisterHandler("strstr", reinterpret_cast<SysVHandler>(HLE_strstr));
    RegisterHandler("mXlxhmLNMPg", reinterpret_cast<SysVHandler>(HLE_strstr));      // strstr NID
    RegisterHandler("strcasecmp", reinterpret_cast<SysVHandler>(HLE_strcasecmp));
    RegisterHandler("AV6ipCNa4Rw", reinterpret_cast<SysVHandler>(HLE_strcasecmp));  // strcasecmp NID
    RegisterHandler("strncasecmp", reinterpret_cast<SysVHandler>(HLE_strncasecmp));
    RegisterHandler("pXvbDfchu6k", reinterpret_cast<SysVHandler>(HLE_strncasecmp)); // strncasecmp NID

    // Conversions & Time
    RegisterHandler("strtoull", reinterpret_cast<SysVHandler>(HLE_strtoull));
    RegisterHandler("5OqszGpy7Mg", reinterpret_cast<SysVHandler>(HLE_strtoull));    // strtoull NID
    RegisterHandler("strtol", reinterpret_cast<SysVHandler>(HLE_strtol));
    RegisterHandler("HNnWdT43ues", reinterpret_cast<SysVHandler>(HLE_strtol));      // strtol NID
    RegisterHandler("strtoul", reinterpret_cast<SysVHandler>(HLE_strtoul));
    RegisterHandler("tanf", reinterpret_cast<SysVHandler>(HLE_tanf));
    RegisterHandler("ZE6RNL+eLbk", reinterpret_cast<SysVHandler>(HLE_tanf));
    RegisterHandler("atanf", reinterpret_cast<SysVHandler>(HLE_atanf));
    RegisterHandler("weDug8QD-lE", reinterpret_cast<SysVHandler>(HLE_atanf));
    RegisterHandler("time", reinterpret_cast<SysVHandler>(HLE_time));
    RegisterHandler("wLlFkwG9UcQ", reinterpret_cast<SysVHandler>(HLE_time));        // time NID
    RegisterHandler("localtime_s", reinterpret_cast<SysVHandler>(HLE_localtime_s));
    RegisterHandler("fiiNDnNBKVY", reinterpret_cast<SysVHandler>(HLE_localtime_s)); // localtime_s NID
    RegisterHandler("gmtime_s", reinterpret_cast<SysVHandler>(HLE_gmtime_s));
    RegisterHandler("5bBacGLyLOs", reinterpret_cast<SysVHandler>(HLE_gmtime_s));    // gmtime_s NID

    // Math
    RegisterHandler("cos", reinterpret_cast<SysVHandler>(HLE_cos));
    RegisterHandler("2WE3BTYVwKM", reinterpret_cast<SysVHandler>(HLE_cos));
    RegisterHandler("cosf", reinterpret_cast<SysVHandler>(HLE_cosf));
    RegisterHandler("-P6FNMzk2Kc", reinterpret_cast<SysVHandler>(HLE_cosf));
    RegisterHandler("sin", reinterpret_cast<SysVHandler>(HLE_sin));
    RegisterHandler("sinf", reinterpret_cast<SysVHandler>(HLE_sinf));
    RegisterHandler("expf", reinterpret_cast<SysVHandler>(HLE_expf));
    RegisterHandler("8zsu04XNsZ4", reinterpret_cast<SysVHandler>(HLE_expf));
    RegisterHandler("pow", reinterpret_cast<SysVHandler>(HLE_pow));
    RegisterHandler("9LCjpWyQ5Zc", reinterpret_cast<SysVHandler>(HLE_pow));
    RegisterHandler("powf", reinterpret_cast<SysVHandler>(HLE_powf));
    RegisterHandler("1D0H2KNjshE", reinterpret_cast<SysVHandler>(HLE_powf));
    RegisterHandler("fmodf", reinterpret_cast<SysVHandler>(HLE_fmodf));
    RegisterHandler("88Vv-AzHVj8", reinterpret_cast<SysVHandler>(HLE_fmodf));
    RegisterHandler("atan2f", reinterpret_cast<SysVHandler>(HLE_atan2f));
    RegisterHandler("EH-x713A99c", reinterpret_cast<SysVHandler>(HLE_atan2f));
    RegisterHandler("acosf", reinterpret_cast<SysVHandler>(HLE_acosf));
    RegisterHandler("GZWjF-YIFFk", reinterpret_cast<SysVHandler>(HLE_acosf));
    RegisterHandler("asinf", reinterpret_cast<SysVHandler>(HLE_asinf));
    RegisterHandler("QI-x0SL8jhw", reinterpret_cast<SysVHandler>(HLE_asinf));
    RegisterHandler("sqrtf", reinterpret_cast<SysVHandler>(HLE_sqrtf));
    RegisterHandler("fabsf", reinterpret_cast<SysVHandler>(HLE_fabsf));
    RegisterHandler("ceilf", reinterpret_cast<SysVHandler>(HLE_ceilf));
    RegisterHandler("floorf", reinterpret_cast<SysVHandler>(HLE_floorf));

    // Runtime Primitives
    RegisterHandler("tsvEmnenz48", reinterpret_cast<SysVHandler>(HLE_cxa_atexit));
    RegisterHandler("__cxa_guard_acquire", reinterpret_cast<SysVHandler>(HLE_cxa_guard_acquire));
    RegisterHandler("3GPpjQdAMTw", reinterpret_cast<SysVHandler>(HLE_cxa_guard_acquire));
    RegisterHandler("__cxa_guard_release", reinterpret_cast<SysVHandler>(HLE_cxa_guard_release));
    RegisterHandler("9rAeANT2tyE", reinterpret_cast<SysVHandler>(HLE_cxa_guard_release));
    RegisterHandler("_Atomic_fetch_sub_4", reinterpret_cast<SysVHandler>(HLE_Atomic_fetch_sub_4));
    RegisterHandler("2HnmKiLmV6s", reinterpret_cast<SysVHandler>(HLE_Atomic_fetch_sub_4));
    RegisterHandler("YaHc3GS7y7g", reinterpret_cast<SysVHandler>(HLE_Mtx_init));
    RegisterHandler("_Mtx_destroy", reinterpret_cast<SysVHandler>(HLE_Mtx_destroy));
    RegisterHandler("5Lf51jvohTQ", reinterpret_cast<SysVHandler>(HLE_Mtx_destroy));
    RegisterHandler("iS4aWbUonl0", reinterpret_cast<SysVHandler>(HLE_Mtx_lock));
    RegisterHandler("gTuXQwP9rrs", reinterpret_cast<SysVHandler>(HLE_Mtx_unlock));
    RegisterHandler("SreZybSRWpU", reinterpret_cast<SysVHandler>(HLE_Cnd_init));
    RegisterHandler("_Cnd_destroy", reinterpret_cast<SysVHandler>(HLE_Cnd_destroy));
    RegisterHandler("7yMFgcS8EPA", reinterpret_cast<SysVHandler>(HLE_Cnd_destroy));
    RegisterHandler("_Cnd_signal", reinterpret_cast<SysVHandler>(HLE_Cnd_signal));
    RegisterHandler("0uuqgRz9qfo", reinterpret_cast<SysVHandler>(HLE_Cnd_signal));
    RegisterHandler("_Cnd_broadcast", reinterpret_cast<SysVHandler>(HLE_Cnd_broadcast));
    RegisterHandler("P330P3dFF68", reinterpret_cast<SysVHandler>(HLE_Cnd_broadcast));
    RegisterHandler("iPBqs+YUUFw", reinterpret_cast<SysVHandler>(HLE_Cnd_broadcast));
    RegisterHandler("_Cnd_wait", reinterpret_cast<SysVHandler>(HLE_Cnd_wait));
    RegisterHandler("vEaqE-7IZYc", reinterpret_cast<SysVHandler>(HLE_Cnd_wait));
    RegisterHandler("_Getpctype", reinterpret_cast<SysVHandler>(HLE_Getpctype));
    RegisterHandler("sUP1hBaouOw", reinterpret_cast<SysVHandler>(HLE_Getpctype));

    // I/O & Formatting
    RegisterHandler("printf", reinterpret_cast<SysVHandler>(HLE_printf));
    RegisterHandler("hcuQgD53UxM", reinterpret_cast<SysVHandler>(HLE_printf));
    RegisterHandler("puts", reinterpret_cast<SysVHandler>(HLE_puts));
    RegisterHandler("YQ0navp+YIc", reinterpret_cast<SysVHandler>(HLE_puts));

    // Ctype & Locale Facets
    RegisterHandler("_Getptolower", reinterpret_cast<SysVHandler>(HLE_Getptolower));
    RegisterHandler("1uJgoVq3bQU", reinterpret_cast<SysVHandler>(HLE_Getptolower));
    RegisterHandler("_Getptoupper", reinterpret_cast<SysVHandler>(HLE_Getptoupper));
    RegisterHandler("rcQCUr0EaRU", reinterpret_cast<SysVHandler>(HLE_Getptoupper));

    RegisterHandler("_Locksyslock", reinterpret_cast<SysVHandler>(HLE_Locksyslock));
    RegisterHandler("kALvdgEv5ME", reinterpret_cast<SysVHandler>(HLE_Locksyslock));
    RegisterHandler("_Unlocksyslock", reinterpret_cast<SysVHandler>(HLE_Unlocksyslock));
    RegisterHandler("9nf8joUTSaQ", reinterpret_cast<SysVHandler>(HLE_Unlocksyslock));

    RegisterHandler("hqi8yMOCmG0", reinterpret_cast<SysVHandler>(HLE_Facet_Getcat));
    RegisterHandler("p6LrHjIQMdk", reinterpret_cast<SysVHandler>(HLE_Facet_Getcat));
    RegisterHandler("BSVJqITGCyI", reinterpret_cast<SysVHandler>(HLE_Facet_Getcat));
}
