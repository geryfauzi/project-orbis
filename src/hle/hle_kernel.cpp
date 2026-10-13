#include "hle_kernel.h"
#include "../core/resolver.h"
#include <windows.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <immintrin.h>

extern "C" int __attribute__((sysv_abi)) HLE_Mtx_lock(void* mtx);
extern "C" int __attribute__((sysv_abi)) HLE_Mtx_unlock(void* mtx);

static std::unordered_map<int, HANDLE> s_file_descriptors;
static int s_next_fd = 100;
static SRWLOCK s_vfs_lock = SRWLOCK_INIT;

static std::string TranslateGuestPath(const char* guest_path) {
    if (!guest_path) return "";
    std::string path = guest_path;

    if (path.find("/app0/") == 0) {
        path = path.substr(6);
    } else if (path.find("\\app0\\") == 0) {
        path = path.substr(6);
    } else if (path.find("app0:/") == 0) {
        path = path.substr(6);
    } else if (path.find("app0:\\") == 0) {
        path = path.substr(6);
    } else if (path.find("app0:") == 0) {
        path = path.substr(5);
    } else if (path.find("app0/") == 0) {
        path = path.substr(5);
    } else if (path.find("app0\\") == 0) {
        path = path.substr(5);
    }

    while (!path.empty() && (path[0] == '/' || path[0] == '\\')) {
        path = path.substr(1);
    }

    for (char& c : path) {
        if (c == '/') c = '\\';
    }

    std::string resolved = "extracted_game\\" + path;
    DWORD attr = GetFileAttributesA(resolved.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return resolved;
    }

    attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return path;
    }

    return resolved;
}

extern "C" {

int64_t __attribute__((sysv_abi)) HLE_sceKernelGetProcessTimeCounter() {
    return static_cast<int64_t>(GetTickCount64() * 1000);
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelGetProcessTimeCounterFrequency() {
    return 1000000;
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelGetCurrentThreadId() {
    return static_cast<int64_t>(GetCurrentThreadId());
}

int __attribute__((sysv_abi)) HLE_sceKernelUsleep(uint32_t microseconds) {
    Sleep(microseconds / 1000);
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelAllocateDirectMemory(int64_t searchStart, int64_t searchEnd, uint64_t length, uint64_t alignment, int32_t type, uint64_t* destOffsetOut) {
    (void)searchStart; (void)searchEnd; (void)alignment; (void)type;
    size_t alloc_sz = (length + 0xffff) & ~0xffff;
    void* ptr = VirtualAlloc(nullptr, alloc_sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!ptr) return -1;
    if (destOffsetOut) *destOffsetOut = reinterpret_cast<uint64_t>(ptr);
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelMapDirectMemory(uint64_t* addrOut, uint64_t length, int32_t memoryType, uint64_t dmemOffset, uint64_t alignment, int32_t flags) {
    (void)length; (void)memoryType; (void)alignment; (void)flags;
    if (addrOut) *addrOut = dmemOffset;
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelMapDirectMemory_LQ3(void** addrOut, int64_t p2, int64_t p3, int64_t p4, int64_t p5, size_t length) {
    (void)p3; (void)p4; (void)p5;
    if (p2 > static_cast<int64_t>(length)) length = static_cast<size_t>(p2);
    if (length < 0x20000000) length = 0x20000000; // 512 MB direct memory pool minimum
    size_t alloc_sz = (length + 0xffff) & ~0xffff;
    void* ptr = VirtualAlloc(nullptr, alloc_sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (addrOut) *addrOut = ptr;
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelGetDirectMemorySize() {
    return 0x80000000; // 2 GB available
}

// File I/O
int __attribute__((sysv_abi)) HLE_sceKernelOpen(const char* path, int flags, int mode) {
    (void)flags; (void)mode;
    if (!path) return -1;

    std::string host_path = TranslateGuestPath(path);
    HANDLE hFile = CreateFileA(
        host_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cout << "[HLE] sceKernelOpen: FAILED \"" << path << "\" -> " << host_path << std::endl;
        return -1;
    }

    AcquireSRWLockExclusive(&s_vfs_lock);
    int fd = ++s_next_fd;
    s_file_descriptors[fd] = hFile;
    ReleaseSRWLockExclusive(&s_vfs_lock);
    std::cout << "[HLE] sceKernelOpen: OK \"" << path << "\" -> fd " << fd << " (" << host_path << ")" << std::endl;
    return fd;
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelRead(int fd, void* buf, size_t nbytes) {
    if (!buf) return -1;
    AcquireSRWLockShared(&s_vfs_lock);
    auto it = s_file_descriptors.find(fd);
    HANDLE hFile = (it != s_file_descriptors.end()) ? it->second : INVALID_HANDLE_VALUE;
    ReleaseSRWLockShared(&s_vfs_lock);

    if (hFile == INVALID_HANDLE_VALUE) return -1;

    DWORD bytes_read = 0;
    if (!ReadFile(hFile, buf, static_cast<DWORD>(nbytes), &bytes_read, nullptr)) {
        std::cout << "[HLE] sceKernelRead(fd=" << fd << ", nbytes=" << nbytes << ") -> FAILED (" << GetLastError() << ")\n";
        return -1;
    }
    extern uint8_t* GetModuleBase();
    uint8_t* base = GetModuleBase();
    void* c0 = __builtin_return_address(0);
    void* c1 = __builtin_return_address(1);
    void* c2 = __builtin_return_address(2);
    uint64_t r0 = (base && (uint8_t*)c0 >= base) ? ((uint8_t*)c0 - base) : reinterpret_cast<uint64_t>(c0);
    uint64_t r1 = (base && (uint8_t*)c1 >= base) ? ((uint8_t*)c1 - base) : reinterpret_cast<uint64_t>(c1);
    uint64_t r2 = (base && (uint8_t*)c2 >= base) ? ((uint8_t*)c2 - base) : reinterpret_cast<uint64_t>(c2);
    std::cout << "[HLE] sceKernelRead(fd=" << fd << ", nbytes=" << nbytes << ") -> " << bytes_read << " bytes (callstack: +0x" << std::hex << r0 << " <= +0x" << r1 << " <= +0x" << r2 << std::dec << ")\n";
    return static_cast<int64_t>(bytes_read);
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelWrite(int fd, const void* buf, size_t nbytes) {
    if (!buf) return -1;
    AcquireSRWLockShared(&s_vfs_lock);
    auto it = s_file_descriptors.find(fd);
    HANDLE hFile = (it != s_file_descriptors.end()) ? it->second : INVALID_HANDLE_VALUE;
    ReleaseSRWLockShared(&s_vfs_lock);

    if (hFile == INVALID_HANDLE_VALUE) return -1;

    DWORD bytes_written = 0;
    if (!WriteFile(hFile, buf, static_cast<DWORD>(nbytes), &bytes_written, nullptr)) {
        return -1;
    }
    return static_cast<int64_t>(bytes_written);
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelLseek(int fd, int64_t offset, int whence) {
    AcquireSRWLockShared(&s_vfs_lock);
    auto it = s_file_descriptors.find(fd);
    HANDLE hFile = (it != s_file_descriptors.end()) ? it->second : INVALID_HANDLE_VALUE;
    ReleaseSRWLockShared(&s_vfs_lock);

    if (hFile == INVALID_HANDLE_VALUE) return -1;

    DWORD move_method = FILE_BEGIN;
    if (whence == 1) move_method = FILE_CURRENT;
    else if (whence == 2) move_method = FILE_END;

    LARGE_INTEGER li_offset, li_new;
    li_offset.QuadPart = offset;
    if (!SetFilePointerEx(hFile, li_offset, &li_new, move_method)) {
        return -1;
    }
    std::cout << "[HLE] sceKernelLseek(fd=" << fd << ", offset=" << offset << ", whence=" << whence << ") -> pos " << li_new.QuadPart << "\n";
    return li_new.QuadPart;
}

int __attribute__((sysv_abi)) HLE_sceKernelClose(int fd) {
    AcquireSRWLockExclusive(&s_vfs_lock);
    auto it = s_file_descriptors.find(fd);
    HANDLE hFile = INVALID_HANDLE_VALUE;
    if (it != s_file_descriptors.end()) {
        hFile = it->second;
        s_file_descriptors.erase(it);
    }
    ReleaseSRWLockExclusive(&s_vfs_lock);

    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        std::cout << "[HLE] sceKernelClose(fd=" << fd << ")\n";
        return 0;
    }
    return -1;
}

int __attribute__((sysv_abi)) HLE_sceKernelFstat(int fd, void* stat_buf) {
    if (!stat_buf) return -1;
    AcquireSRWLockShared(&s_vfs_lock);
    auto it = s_file_descriptors.find(fd);
    HANDLE hFile = (it != s_file_descriptors.end()) ? it->second : INVALID_HANDLE_VALUE;
    ReleaseSRWLockShared(&s_vfs_lock);

    if (hFile == INVALID_HANDLE_VALUE) return -1;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(hFile, &sz)) return -1;

    std::memset(stat_buf, 0, 128);
    *reinterpret_cast<uint16_t*>(reinterpret_cast<char*>(stat_buf) + 0x08) = 0100644; // S_IFREG
    *reinterpret_cast<int64_t*>(reinterpret_cast<char*>(stat_buf) + 0x48) = sz.QuadPart;
    return 0;
}

int __attribute__((sysv_abi)) HLE_stat(const char* path, void* stat_buf) {
    if (!path || !stat_buf) return -1;

    std::string host_path = TranslateGuestPath(path);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(host_path.c_str(), GetFileExInfoStandard, &fad)) {
        std::cout << "[HLE] stat: FAILED \"" << path << "\" -> " << host_path << std::endl;
        return -1;
    }

    std::memset(stat_buf, 0, 128);
    uint16_t mode = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0040755 : 0100644;
    *reinterpret_cast<uint16_t*>(reinterpret_cast<char*>(stat_buf) + 0x08) = mode;
    int64_t size = (static_cast<int64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    *reinterpret_cast<int64_t*>(reinterpret_cast<char*>(stat_buf) + 0x48) = size;
    std::cout << "[HLE] stat: OK \"" << path << "\" -> " << host_path << " (" << size << " bytes)" << std::endl;
    return 0;
}

// User Service
int __attribute__((sysv_abi)) HLE_sceUserServiceGetInitialUser(int32_t* userId) {
    if (userId) *userId = 0x10000005;
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceUserServiceGetUserName(int32_t userId, char* userName, size_t size) {
    (void)userId;
    if (userName && size > 0) {
        std::strncpy(userName, "Player", size);
        userName[size - 1] = '\0';
    }
    return 0;
}

struct SceUserServiceLoginUserIdList {
    int32_t userId[4];
};

int __attribute__((sysv_abi)) HLE_sceUserServiceGetLoginUserIdList(SceUserServiceLoginUserIdList* list) {
    if (list) {
        list->userId[0] = 0x10000005;
        list->userId[1] = -1;
        list->userId[2] = -1;
        list->userId[3] = -1;
        static bool s_logged = false;
        if (!s_logged) {
            std::cout << "[HLE] sceUserServiceGetLoginUserIdList -> primary user 0x10000005 active\n";
            s_logged = true;
        }
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceUserServiceInitialize(void* param) {
    (void)param;
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceUserServiceTerminate() {
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceKernelLoadStartModule(const char* module_path, size_t args, const void* argp, uint32_t flags, const void* opt, int* res) {
    (void)args; (void)argp; (void)flags; (void)opt; (void)res;
    static int s_next_handle = 0x1000;
    int handle = ++s_next_handle;
    std::cout << "[HLE] sceKernelLoadStartModule(\"" << (module_path ? module_path : "") << "\") -> handle 0x" << std::hex << handle << std::dec << "\n";
    return handle;
}

int64_t __attribute__((sysv_abi)) HLE_sceNetPoolCreate(const char* name, size_t size, int flags) {
    (void)flags;
    std::cout << "[HLE] sceNetPoolCreate(\"" << (name ? name : "") << "\", size 0x" << std::hex << size << std::dec << ") -> 1\n";
    return 1;
}

int64_t __attribute__((sysv_abi)) HLE_sceNetResolverCreate(const char* name, void* opt, int flags) {
    (void)opt; (void)flags;
    std::cout << "[HLE] sceNetResolverCreate(\"" << (name ? name : "") << "\") -> 1\n";
    return 1;
}

int __attribute__((sysv_abi)) HLE_sceKernelCreateEqueue(void* eq, const char* name) {
    (void)eq; (void)name;
    return 1; // valid equeue handle
}

struct ThreadStartContext {
    void* (*start_routine)(void*);
    void* arg;
};

static DWORD WINAPI GuestThreadProxy(LPVOID param) {
    auto* ctx = reinterpret_cast<ThreadStartContext*>(param);
    auto routine = reinterpret_cast<void* (__attribute__((sysv_abi)) *)(void*)>(ctx->start_routine);
    void* arg = ctx->arg;
    delete ctx;

    size_t tcb_size = 65536;
    uint8_t* tcb = reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, tcb_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (tcb) {
        std::memset(tcb, 0, tcb_size);
        uint8_t* tls_base = tcb + 32768;
        *reinterpret_cast<uint64_t*>(tls_base) = reinterpret_cast<uint64_t>(tls_base);
        _writefsbase_u64(reinterpret_cast<uint64_t>(tls_base));
    }

    NT_TIB* tib = reinterpret_cast<NT_TIB*>(__readgsqword(0x30));
    if (tib && tib->StackBase) {
        size_t commit_sz = 14 * 1024 * 1024;
        uint8_t* bottom = reinterpret_cast<uint8_t*>(tib->StackBase) - commit_sz;
        VirtualAlloc(bottom, commit_sz, MEM_COMMIT, PAGE_READWRITE);
    }

    std::cout << "[THREAD] Worker thread started: routine=" << (void*)routine << ", arg=" << arg << std::endl;
    routine(arg);
    std::cout << "[THREAD] Worker thread exited: routine=" << (void*)routine << std::endl;
    return 0;
}

int __attribute__((sysv_abi)) HLE_scePthreadCreate(void* thread, const void* attr, void* (*start_routine)(void*), void* arg) {
    (void)attr;
    std::cout << "[HLE] scePthreadCreate: routine=" << (void*)start_routine << ", arg=" << arg << std::endl;
    if (!start_routine) return -1;

    auto* ctx = new ThreadStartContext{ start_routine, arg };
    DWORD tid = 0;
    HANDLE hThread = CreateThread(nullptr, 0, GuestThreadProxy, ctx, 0, &tid);
    if (!hThread) {
        delete ctx;
        return -1;
    }

    if (thread) {
        *reinterpret_cast<HANDLE*>(thread) = hThread;
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_scePthreadAttrDestroy(void* attr) {
    (void)attr;
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceSystemServiceParamGetInt(int paramId, int* value) {
    if (value) {
        if (paramId == 1) {
            *value = 0; // Cross button
        } else {
            *value = 0;
        }
    }
    return 0; // SCE_OK
}

int __attribute__((sysv_abi)) HLE_sceSystemServiceHideSplashScreen() {
    return 0;
}

extern void DriveGuestPadUpdate();
static bool s_event_delivered = false;

int __attribute__((sysv_abi)) HLE_sceSystemServiceGetStatus(void* status) {
    DriveGuestPadUpdate();
    if (status) {
        std::memset(status, 0, 32);
        uint8_t* p = reinterpret_cast<uint8_t*>(status);
        p[4] = 0; // isAppSuspended = false
        p[5] = 1; // isAppFocused = true
        if (!s_event_delivered) {
            *reinterpret_cast<int32_t*>(status) = 1; // 1 event pending
        }
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceSystemServiceReceiveEvent(void* event) {
    if (!s_event_delivered && event) {
        s_event_delivered = true;
        std::memset(event, 0, 0x2004);
        *reinterpret_cast<uint32_t*>(event) = 0x10000003; // SCE_SYSTEM_SERVICE_EVENT_RESUME
        std::cout << "[HLE] Delivered SCE_SYSTEM_SERVICE_EVENT_RESUME (0x10000003) to game loop.\n";
        return 0;
    }
    return -1;
}

struct SceKernelTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

int __attribute__((sysv_abi)) HLE_sceKernelClockGettime(int clock_id, SceKernelTimespec* tp) {
    (void)clock_id;
    if (!tp) return -1;
    static LARGE_INTEGER freq;
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    int64_t total_ns = (counter.QuadPart * 1000000000LL) / freq.QuadPart;
    tp->tv_sec = total_ns / 1000000000LL;
    tp->tv_nsec = total_ns % 1000000000LL;
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_DummySuccess() {
    return 0;
}

void* __attribute__((sysv_abi)) HLE_FMOD_Create(void* p1, void* p2) {
    (void)p1; (void)p2;
    void* obj = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return obj;
}

void __attribute__((sysv_abi)) HLE_exit(int code) {
    std::cout << "[APP] Game requested exit with code: " << code << "\n";
    uint64_t* stack;
    asm volatile("mov %%rsp, %0" : "=r"(stack));
    std::cout << "[BACKTRACE] Exit caller stack:\n";
    for (int i = 0; i < 24; i++) {
        std::cout << "  [" << i << "] 0x" << std::hex << stack[i] << std::dec << "\n";
    }
    ExitProcess(code);
}

} // extern "C"

void RegisterKernelHLE() {
    RegisterHandler("sceKernelGetProcessTimeCounter", reinterpret_cast<SysVHandler>(HLE_sceKernelGetProcessTimeCounter));
    RegisterHandler("sceKernelGetProcessTimeCounterFrequency", reinterpret_cast<SysVHandler>(HLE_sceKernelGetProcessTimeCounterFrequency));
    RegisterHandler("sceKernelClockGettime", reinterpret_cast<SysVHandler>(HLE_sceKernelClockGettime));
    RegisterHandler("QBi7HCK03hw", reinterpret_cast<SysVHandler>(HLE_sceKernelClockGettime));
    RegisterHandler("sceKernelGetCurrentThreadId", reinterpret_cast<SysVHandler>(HLE_sceKernelGetCurrentThreadId));
    RegisterHandler("sceKernelUsleep", reinterpret_cast<SysVHandler>(HLE_sceKernelUsleep));
    RegisterHandler("1jfXLRVzisc", reinterpret_cast<SysVHandler>(HLE_sceKernelUsleep));

    RegisterHandler("sceKernelAllocateDirectMemory", reinterpret_cast<SysVHandler>(HLE_sceKernelAllocateDirectMemory));
    RegisterHandler("sceKernelMapDirectMemory", reinterpret_cast<SysVHandler>(HLE_sceKernelMapDirectMemory));
    RegisterHandler("L-Q3LEjIbgA", reinterpret_cast<SysVHandler>(HLE_sceKernelMapDirectMemory_LQ3));
    RegisterHandler("pO96TwzOm5E", reinterpret_cast<SysVHandler>(HLE_sceKernelGetDirectMemorySize));

    RegisterHandler("sceKernelOpen", reinterpret_cast<SysVHandler>(HLE_sceKernelOpen));
    RegisterHandler("1G3lF1Gg1k8", reinterpret_cast<SysVHandler>(HLE_sceKernelOpen));
    RegisterHandler("open", reinterpret_cast<SysVHandler>(HLE_sceKernelOpen));
    RegisterHandler("QrZZdJ8XsX0", reinterpret_cast<SysVHandler>(HLE_sceKernelOpen));      // libc open NID

    RegisterHandler("sceKernelRead", reinterpret_cast<SysVHandler>(HLE_sceKernelRead));
    RegisterHandler("Cg4srZ6TKbU", reinterpret_cast<SysVHandler>(HLE_sceKernelRead));
    RegisterHandler("read", reinterpret_cast<SysVHandler>(HLE_sceKernelRead));
    RegisterHandler("eLdDw6l0-bU", reinterpret_cast<SysVHandler>(HLE_sceKernelRead));      // libc read NID

    RegisterHandler("sceKernelWrite", reinterpret_cast<SysVHandler>(HLE_sceKernelWrite));
    RegisterHandler("write", reinterpret_cast<SysVHandler>(HLE_sceKernelWrite));
    RegisterHandler("NL836gOLANs", reinterpret_cast<SysVHandler>(HLE_sceKernelWrite));     // libc write NID

    RegisterHandler("sceKernelLseek", reinterpret_cast<SysVHandler>(HLE_sceKernelLseek));
    RegisterHandler("oib76F-12fk", reinterpret_cast<SysVHandler>(HLE_sceKernelLseek));
    RegisterHandler("lseek", reinterpret_cast<SysVHandler>(HLE_sceKernelLseek));
    RegisterHandler("aZK8lNei-Qw", reinterpret_cast<SysVHandler>(HLE_sceKernelLseek));     // libc lseek NID

    RegisterHandler("sceKernelClose", reinterpret_cast<SysVHandler>(HLE_sceKernelClose));
    RegisterHandler("UK2Tl2DWUns", reinterpret_cast<SysVHandler>(HLE_sceKernelClose));
    RegisterHandler("close", reinterpret_cast<SysVHandler>(HLE_sceKernelClose));
    RegisterHandler("Q8pvJimUWis", reinterpret_cast<SysVHandler>(HLE_sceKernelClose));     // libc close NID

    RegisterHandler("sceKernelFstat", reinterpret_cast<SysVHandler>(HLE_sceKernelFstat));
    RegisterHandler("kBwCPsYX-m4", reinterpret_cast<SysVHandler>(HLE_sceKernelFstat));
    RegisterHandler("fstat", reinterpret_cast<SysVHandler>(HLE_sceKernelFstat));
    RegisterHandler("SRI6S9B+-a4", reinterpret_cast<SysVHandler>(HLE_sceKernelFstat));     // libc fstat NID

    RegisterHandler("stat", reinterpret_cast<SysVHandler>(HLE_stat));
    RegisterHandler("E6ao34wPw+U", reinterpret_cast<SysVHandler>(HLE_stat));

    RegisterHandler("sceUserServiceGetInitialUser", reinterpret_cast<SysVHandler>(HLE_sceUserServiceGetInitialUser));
    RegisterHandler("CdWp0oHWGr0", reinterpret_cast<SysVHandler>(HLE_sceUserServiceGetInitialUser));
    RegisterHandler("sceUserServiceGetUserName", reinterpret_cast<SysVHandler>(HLE_sceUserServiceGetUserName));
    RegisterHandler("1xxcMiGu2fo", reinterpret_cast<SysVHandler>(HLE_sceUserServiceGetUserName));

    RegisterHandler("sceUserServiceGetLoginUserIdList", reinterpret_cast<SysVHandler>(HLE_sceUserServiceGetLoginUserIdList));
    RegisterHandler("fPhymKNvK-A", reinterpret_cast<SysVHandler>(HLE_sceUserServiceGetLoginUserIdList));

    RegisterHandler("sceUserServiceInitialize", reinterpret_cast<SysVHandler>(HLE_sceUserServiceInitialize));
    RegisterHandler("j3YMu1MVNNo", reinterpret_cast<SysVHandler>(HLE_sceUserServiceInitialize));

    RegisterHandler("sceUserServiceTerminate", reinterpret_cast<SysVHandler>(HLE_sceUserServiceTerminate));
    RegisterHandler("bwFjS+bX9mA", reinterpret_cast<SysVHandler>(HLE_sceUserServiceTerminate));

    RegisterHandler("wzvqT4UqKX8", reinterpret_cast<SysVHandler>(HLE_sceKernelLoadStartModule));
    RegisterHandler("dgJBaeJnGpo", reinterpret_cast<SysVHandler>(HLE_sceNetPoolCreate));
    RegisterHandler("C4UgDHHPvdw", reinterpret_cast<SysVHandler>(HLE_sceNetResolverCreate));
    RegisterHandler("D0OdFMjp46I", reinterpret_cast<SysVHandler>(HLE_sceKernelCreateEqueue));
    RegisterHandler("6UgtwV+0zb4", reinterpret_cast<SysVHandler>(HLE_scePthreadCreate));
    RegisterHandler("62KCwEMmzcM", reinterpret_cast<SysVHandler>(HLE_scePthreadAttrDestroy));
    RegisterHandler("pthread_self", reinterpret_cast<SysVHandler>(HLE_sceKernelGetCurrentThreadId));
    RegisterHandler("EotR8a3ASf4", reinterpret_cast<SysVHandler>(HLE_sceKernelGetCurrentThreadId));
    RegisterHandler("pthread_mutex_lock", reinterpret_cast<SysVHandler>(HLE_Mtx_lock));
    RegisterHandler("7H0iTOciTLo", reinterpret_cast<SysVHandler>(HLE_Mtx_lock));
    RegisterHandler("pthread_mutex_unlock", reinterpret_cast<SysVHandler>(HLE_Mtx_unlock));
    RegisterHandler("2Z+PpY6CaJg", reinterpret_cast<SysVHandler>(HLE_Mtx_unlock));
    RegisterHandler("sched_yield", reinterpret_cast<SysVHandler>(HLE_DummySuccess));
    RegisterHandler("6XG4B33N09g", reinterpret_cast<SysVHandler>(HLE_DummySuccess));
    RegisterHandler("2QOSvz5hvAg", reinterpret_cast<SysVHandler>(HLE_FMOD_Create));
    RegisterHandler("XKRegsFpEpk", reinterpret_cast<SysVHandler>(HLE_exit));
    RegisterHandler("uMei1W9uyNo", reinterpret_cast<SysVHandler>(HLE_exit));

    // SystemService
    RegisterHandler("sceSystemServiceParamGetInt", reinterpret_cast<SysVHandler>(HLE_sceSystemServiceParamGetInt));
    RegisterHandler("fZo48un7LK4", reinterpret_cast<SysVHandler>(HLE_sceSystemServiceParamGetInt));
    RegisterHandler("sceSystemServiceHideSplashScreen", reinterpret_cast<SysVHandler>(HLE_sceSystemServiceHideSplashScreen));
    RegisterHandler("Vo5V8KAwCmk", reinterpret_cast<SysVHandler>(HLE_sceSystemServiceHideSplashScreen));
    RegisterHandler("sceSystemServiceGetStatus", reinterpret_cast<SysVHandler>(HLE_sceSystemServiceGetStatus));
    RegisterHandler("rPo6tV8D9bM", reinterpret_cast<SysVHandler>(HLE_sceSystemServiceGetStatus));
    RegisterHandler("sceSystemServiceReceiveEvent", reinterpret_cast<SysVHandler>(HLE_sceSystemServiceReceiveEvent));
    RegisterHandler("656LMQSrg6U", reinterpret_cast<SysVHandler>(HLE_sceSystemServiceReceiveEvent));

    // Gnm event queue
    RegisterHandler("b0xyllnVY-I", reinterpret_cast<SysVHandler>(HLE_DummySuccess));
}
