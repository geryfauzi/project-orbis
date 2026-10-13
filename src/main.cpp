#include "core/elf.h"
#include "core/resolver.h"
#include "core/nid_table.h"
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#include <fstream>

static LoadedModule g_module;

extern "C" uint8_t* GetModuleBase() {
    return g_module.base_address;
}

extern "C" void SetGameBaseAddress(uint8_t* base);

// Vectored exception handler for detailed crash diagnostics
#include <dbghelp.h>

static uint8_t* s_main_tls_base = nullptr;

LONG WINAPI ExceptionHandler(EXCEPTION_POINTERS* ep) {
    auto rec = ep->ExceptionRecord;
    auto ctx = ep->ContextRecord;

    // Transparently recover and re-write hardware FS_BASE if cleared by Windows kernel
    if (rec->ExceptionCode == 0xc0000005 && rec->ExceptionInformation[0] == 0 && rec->ExceptionInformation[1] == 0) {
        uint8_t* code = reinterpret_cast<uint8_t*>(ctx->Rip);
        if (IsValidReadPtr(code, 16)) {
            int off = 0;
            while (off < 4 && code[off] == 0x66) off++;
            if (code[off] == 0x64) {
                uint64_t tls = reinterpret_cast<uint64_t>(s_main_tls_base);
                _writefsbase_u64(tls);
                if (code[off+1] == 0x48 && code[off+2] == 0x8b && code[off+3] == 0x04 && code[off+4] == 0x25) {
                    ctx->Rax = tls;
                    ctx->Rip += (off + 9);
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }
        }
    }

    if (rec->ExceptionCode == 0xe06d7363 || 
        rec->ExceptionCode == 0x406d1388 || 
        rec->ExceptionCode == 0x40010006 ||
        rec->ExceptionCode == 0xc0000008) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    HANDLE hFile = CreateFileA("crash_dump.txt", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char err_buf[512];
        int len = snprintf(err_buf, sizeof(err_buf),
            "================ GUEST EXECUTION EXCEPTION ================\n"
            "Exception Code: 0x%lx\n"
            "Exception Address: %p\n"
            "Attempted Address: %p (Type: %s)\n",
            rec->ExceptionCode,
            rec->ExceptionAddress,
            (void*)(rec->NumberParameters >= 2 ? rec->ExceptionInformation[1] : 0),
            (rec->NumberParameters >= 1 ? (rec->ExceptionInformation[0] == 0 ? "Read" : rec->ExceptionInformation[0] == 1 ? "Write" : "Execute") : "Unknown")
        );
        WriteFile(hFile, err_buf, len, NULL, NULL);

        if (g_module.base_address) {
            uint64_t base = reinterpret_cast<uint64_t>(g_module.base_address);
            uint64_t rip = ctx->Rip;
            if (rip >= base && rip < base + g_module.total_size) {
                len = snprintf(err_buf, sizeof(err_buf), "Module Relative RIP: +0x%llx\n", rip - base);
                WriteFile(hFile, err_buf, len, NULL, NULL);
            }
        }

        len = snprintf(err_buf, sizeof(err_buf),
            "Registers:\n"
            "  RIP: 0x%llx  RSP: 0x%llx  RBP: 0x%llx\n"
            "  RAX: 0x%llx  RBX: 0x%llx  RCX: 0x%llx  RDX: 0x%llx\n"
            "  RSI: 0x%llx  RDI: 0x%llx  R8:  0x%llx  R9:  0x%llx\n"
            "  R10: 0x%llx  R11: 0x%llx  R12: 0x%llx  R13: 0x%llx\n"
            "  R14: 0x%llx  R15: 0x%llx\n",
            ctx->Rip, ctx->Rsp, ctx->Rbp,
            ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx,
            ctx->Rsi, ctx->Rdi, ctx->R8,  ctx->R9,
            ctx->R10, ctx->R11, ctx->R12, ctx->R13,
            ctx->R14, ctx->R15
        );
        WriteFile(hFile, err_buf, len, NULL, NULL);

        if (IsValidReadPtr(reinterpret_cast<void*>(ctx->Rip - 16), 32)) {
            WriteFile(hFile, "Code around RIP:\n  ", 19, NULL, NULL);
            uint8_t* code = reinterpret_cast<uint8_t*>(ctx->Rip - 16);
            for (int k = 0; k < 32; k++) {
                if (k == 16) WriteFile(hFile, "<*RIP*> ", 8, NULL, NULL);
                len = snprintf(err_buf, sizeof(err_buf), "%02x ", code[k]);
                WriteFile(hFile, err_buf, len, NULL, NULL);
            }
            WriteFile(hFile, "\n", 1, NULL, NULL);
        }

        if (ctx->Rsp) {
            WriteFile(hFile, "Stack Backtrace:\n", 17, NULL, NULL);
            uint64_t* stack = reinterpret_cast<uint64_t*>(ctx->Rsp);
            uint64_t base = reinterpret_cast<uint64_t>(g_module.base_address);
            for (int i = 0; i < 64; i++) {
                if (!IsValidReadPtr(&stack[i], 8)) break;
                uint64_t addr = stack[i];
                if (addr >= base && addr < base + g_module.total_size) {
                    len = snprintf(err_buf, sizeof(err_buf), "  [%d] Module Relative: +0x%llx\n", i, addr - base);
                    WriteFile(hFile, err_buf, len, NULL, NULL);
                }
            }
        }
        WriteFile(hFile, "===========================================================\n", 60, NULL, NULL);
        CloseHandle(hFile);
    }

    ExitProcess(1);
}

#include "hle/hle_libc.h"
#include "hle/hle_kernel.h"
#include "hle/hle_video.h"
#include "hle/hle_pad.h"
#include "hle/hle_audio.h"

static const char s_title_sequence_str[] = "TitleSequence";
static const char* __attribute__((sysv_abi)) HLE_GetTitleSequenceName(void* this_ptr) {
    (void)this_ptr;
    return s_title_sequence_str;
}

static int64_t __attribute__((sysv_abi)) HLE_DummyGameModeMethod(void* this_ptr) {
    (void)this_ptr;
    return 0;
}

void RegisterBasicHLE() {
    RegisterLibcHLE();
    RegisterKernelHLE();
    RegisterVideoHLE();
    RegisterPadHLE();
    RegisterAudioHLE();
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    std::cout << "=== Beach Buggy Racing 2 Native PS4 Runner (Proton Architecture) ===\n";

    AddVectoredExceptionHandler(1, ExceptionHandler);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    // Initialize Thread Local Storage (%fs register)
    size_t tcb_size = 65536;
    uint8_t* tcb = reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, tcb_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (tcb) {
        std::memset(tcb, 0, tcb_size);
        uint8_t* tls_base = tcb + 32768;
        *reinterpret_cast<uint64_t*>(tls_base) = reinterpret_cast<uint64_t>(tls_base); // %fs:0 -> self
        s_main_tls_base = tls_base;
        _writefsbase_u64(reinterpret_cast<uint64_t>(tls_base));
        std::cout << "[INIT] Thread Local Storage (FS Base) initialized at 0x" << std::hex << reinterpret_cast<uint64_t>(tls_base) << std::dec << "\n";
    }

    // Fully commit main thread stack for FreeBSD/SysV large frame allocations without __chkstk
    void* stack_base = reinterpret_cast<void*>(__readgsqword(0x08));
    void* stack_limit = reinterpret_cast<void*>(__readgsqword(0x10));
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(stack_limit, &mbi, sizeof(mbi))) {
        size_t full_sz = reinterpret_cast<uint8_t*>(stack_base) - reinterpret_cast<uint8_t*>(mbi.AllocationBase);
        void* res = VirtualAlloc(mbi.AllocationBase, full_sz, MEM_COMMIT, PAGE_READWRITE);
        std::cout << "[INIT] StackBase: " << stack_base << " Limit: " << stack_limit 
                  << " AllocBase: " << mbi.AllocationBase << " (" << (full_sz / (1024 * 1024)) << " MB), commit=" << (res != nullptr) << "\n";
    }

    const char* binary_path = (argc > 1) ? argv[1] : "extracted_game/eboot.bin";
    std::cout << "[LOAD] Loading guest binary: " << binary_path << "\n";

    if (!LoadOrbisElf(binary_path, g_module)) {
        std::cerr << "[ERROR] Failed to load guest binary!\n";
        return 1;
    }

    std::cout << "[LOAD] Base address: 0x" << std::hex << reinterpret_cast<uint64_t>(g_module.base_address) << "\n";
    std::cout << "[LOAD] Entry point:  0x" << reinterpret_cast<uint64_t>(g_module.base_address + g_module.entry_point) << std::dec << "\n";

    RegisterBasicHLE();
    EnsureDisplayWindow();
    SetGameBaseAddress(g_module.base_address);

    if (!InitResolver(g_module)) {
        std::cerr << "[ERROR] Failed to initialize resolver!\n";
        return 1;
    }

    if (!ApplyRelocations(g_module)) {
        std::cerr << "[ERROR] Failed to apply relocations!\n";
        return 1;
    }

    // Patch 0: Universal TLS %fs:0 patch
    // Replace all 'mov %fs:0, %rax' (64 48 8b 04 25 00 00 00 00) with direct rip-relative load
    void** p_tls_storage = reinterpret_cast<void**>(g_module.base_address + g_module.total_size - 0x1000);
    *p_tls_storage = s_main_tls_base;
    size_t tls_patch_count = 0;
    uint8_t* p_tls_scan = g_module.base_address;
    uint8_t* p_tls_limit = g_module.base_address + g_module.total_size - 16;
    while (p_tls_scan < p_tls_limit) {
        if (p_tls_scan[0] == 0x64 &&
            p_tls_scan[1] == 0x48 &&
            p_tls_scan[2] == 0x8b &&
            p_tls_scan[3] == 0x04 &&
            p_tls_scan[4] == 0x25 &&
            p_tls_scan[5] == 0x00 &&
            p_tls_scan[6] == 0x00 &&
            p_tls_scan[7] == 0x00 &&
            p_tls_scan[8] == 0x00) {
            int64_t diff = reinterpret_cast<uint8_t*>(p_tls_storage) - (p_tls_scan + 7);
            if (diff >= -2147483648LL && diff <= 2147483647LL) {
                DWORD op;
                VirtualProtect(p_tls_scan, 9, PAGE_EXECUTE_READWRITE, &op);
                p_tls_scan[0] = 0x48;
                p_tls_scan[1] = 0x8b;
                p_tls_scan[2] = 0x05;
                *reinterpret_cast<int32_t*>(p_tls_scan + 3) = static_cast<int32_t>(diff);
                p_tls_scan[7] = 0x90;
                p_tls_scan[8] = 0x90;
                VirtualProtect(p_tls_scan, 9, op, &op);
                tls_patch_count++;
            }
        }
        p_tls_scan++;
    }
    std::cout << "[TLS_PATCH] Universally converted " << tls_patch_count << " %fs:0 instructions to native rip-relative loads!\n";

    // Patch 1: Bypass early exit in Engine::Init (+0x50d07a)
    uint8_t* p_init_check = g_module.base_address + 0x50d07a;
    if (p_init_check[0] == 0x0f && p_init_check[1] == 0x84) {
        DWORD old_prot;
        VirtualProtect(p_init_check, 6, PAGE_EXECUTE_READWRITE, &old_prot);
        for (int k = 0; k < 6; k++) p_init_check[k] = 0x90; // NOP
        VirtualProtect(p_init_check, 6, old_prot, &old_prot);
        std::cout << "[PATCH 1] Bypassed early exit in Engine::Init at +0x50d07a.\n";
    }

    // Patch 2: Bypass init failure check in subsystem constructor (+0x3cdb)
    uint8_t* p_ctor_check = g_module.base_address + 0x3cdb;
    if (p_ctor_check[0] == 0x0f && p_ctor_check[1] == 0x84) {
        DWORD old_prot;
        VirtualProtect(p_ctor_check, 6, PAGE_EXECUTE_READWRITE, &old_prot);
        for (int k = 0; k < 6; k++) p_ctor_check[k] = 0x90; // NOP
        VirtualProtect(p_ctor_check, 6, old_prot, &old_prot);
        std::cout << "[PATCH 2] Bypassed init failure check in subsystem ctor at +0x3cdb.\n";
    }

    // Patch 3: Force jump to main loop at +0xbc3 (jne +0x1b -> jmp +0x1b)
    uint8_t* p_main_jump = g_module.base_address + 0xbc3;
    if (p_main_jump[0] == 0x75 && p_main_jump[1] == 0x1b) {
        DWORD old_prot;
        VirtualProtect(p_main_jump, 2, PAGE_EXECUTE_READWRITE, &old_prot);
        p_main_jump[0] = 0xeb; // unconditional short jmp
        VirtualProtect(p_main_jump, 2, old_prot, &old_prot);
        std::cout << "[PATCH 3] Forced entry into main game loop at +0xbc3.\n";
    }

    // Patch 4: Ensure subsystem constructor at +0x9225 is always executed (+0x921f)
    uint8_t* p_subsys_check = g_module.base_address + 0x921f;
    if (p_subsys_check[0] == 0x0f && p_subsys_check[1] == 0x84) {
        DWORD old_prot;
        VirtualProtect(p_subsys_check, 6, PAGE_EXECUTE_READWRITE, &old_prot);
        for (int k = 0; k < 6; k++) p_subsys_check[k] = 0x90; // NOP
        VirtualProtect(p_subsys_check, 6, old_prot, &old_prot);
        std::cout << "[PATCH 4] Ensured subsystem object at +0x9225 is constructed.\n";
    }

    // Patch 5: Force main game loop running condition at +0x9900 (return true)
    uint8_t* p_loop_cond = g_module.base_address + 0x9900;
    DWORD old_prot_p5;
    VirtualProtect(p_loop_cond, 3, PAGE_EXECUTE_READWRITE, &old_prot_p5);
    p_loop_cond[0] = 0xb0; // mov $1, %al
    p_loop_cond[1] = 0x01;
    p_loop_cond[2] = 0xc3; // ret
    VirtualProtect(p_loop_cond, 3, old_prot_p5, &old_prot_p5);
    std::cout << "[PATCH 5] Forced main loop condition at +0x9900 to return true (active game loop).\n";

    // Patch 6: Allow VuPreloadGameMode to transition cleanly into FrontEnd (Main Menu)
    uint8_t* p_preload_je = g_module.base_address + 0x19d297;
    if (p_preload_je[0] == 0x74 && p_preload_je[1] == 0x29) {
        DWORD old_prot_p6;
        VirtualProtect(p_preload_je, 2, PAGE_EXECUTE_READWRITE, &old_prot_p6);
        p_preload_je[0] = 0x90; // NOP
        p_preload_je[1] = 0x90; // NOP
        VirtualProtect(p_preload_je, 2, old_prot_p6, &old_prot_p6);
        std::cout << "[PATCH 6] Transition to FrontEnd enabled at +0x19d297 (je -> NOP).\n";
    }
    std::cout << "[MODE] target: " << (char*)(g_module.base_address + 0x8eb98b) << "\n";
    std::cout << "[MODE] stay:   " << (char*)(g_module.base_address + 0x8eb9ba) << "\n";


    // Patch 7: Ensure PadManager subsystem constructor at +0x4311 is always called (+0x430b)
    uint8_t* p_pad_check = g_module.base_address + 0x430b;
    if (p_pad_check[0] == 0x0f && p_pad_check[1] == 0x84) {
        DWORD old_prot_p7;
        VirtualProtect(p_pad_check, 6, PAGE_EXECUTE_READWRITE, &old_prot_p7);
        for (int k = 0; k < 6; k++) p_pad_check[k] = 0x90;
        VirtualProtect(p_pad_check, 6, old_prot_p7, &old_prot_p7);
        std::cout << "[PATCH 7] Forced PadManager subsystem constructor execution at +0x430b.\n";
    }

    // Patch 8: Ensure VuGameModeManager constructor at +0x6cdd is executed (+0x6cd7)
    uint8_t* p_gmm_check = g_module.base_address + 0x6cd7;
    if (p_gmm_check[0] == 0x0f && p_gmm_check[1] == 0x84) {
        DWORD old_prot_p8;
        VirtualProtect(p_gmm_check, 6, PAGE_EXECUTE_READWRITE, &old_prot_p8);
        for (int k = 0; k < 6; k++) p_gmm_check[k] = 0x90; // NOP
        VirtualProtect(p_gmm_check, 6, old_prot_p8, &old_prot_p8);
        std::cout << "[PATCH 8] Forced VuGameModeManager constructor execution at +0x6cd7.\n";
    } else {
        std::printf("[PATCH 8] Warning: bytes at +0x6cd7 are %02x %02x %02x %02x %02x %02x\n",
                    p_gmm_check[0], p_gmm_check[1], p_gmm_check[2], p_gmm_check[3], p_gmm_check[4], p_gmm_check[5]);
    }

    uint8_t* p_gmm_loop = g_module.base_address + 0x6d54;
    if (p_gmm_loop[0] == 0x74 && p_gmm_loop[1] == 0x6f) {
        DWORD old_prot_p8b;
        VirtualProtect(p_gmm_loop, 2, PAGE_EXECUTE_READWRITE, &old_prot_p8b);
        p_gmm_loop[0] = 0x90;
        p_gmm_loop[1] = 0x90;
        VirtualProtect(p_gmm_loop, 2, old_prot_p8b, &old_prot_p8b);
        std::cout << "[PATCH 8B] Ensured subsystem initialization loop runs at +0x6d54.\n";
    } else {
        std::printf("[PATCH 8B] Warning: bytes at +0x6d54 are %02x %02x\n", p_gmm_loop[0], p_gmm_loop[1]);
    }

    // Patch 9: Bypass background task wait loop in Asset/Task Manager at +0x3dce47 (jne -> jmp)
    uint8_t* p_task_wait = g_module.base_address + 0x3dce47;
    if (p_task_wait[0] == 0x0f && p_task_wait[1] == 0x85) {
        DWORD old_prot_p9;
        VirtualProtect(p_task_wait, 6, PAGE_EXECUTE_READWRITE, &old_prot_p9);
        p_task_wait[0] = 0xe9;
        p_task_wait[1] = 0x24;
        p_task_wait[2] = 0x00;
        p_task_wait[3] = 0x00;
        p_task_wait[4] = 0x00;
        p_task_wait[5] = 0x90; // NOP
        VirtualProtect(p_task_wait, 6, old_prot_p9, &old_prot_p9);
        std::cout << "[PATCH 9] Bypassed background task wait loop at +0x3dce47 (jne -> jmp).\n";
    } else {
        std::printf("[PATCH 9] Warning: bytes at +0x3dce47 are %02x %02x %02x %02x %02x %02x\n",
                    p_task_wait[0], p_task_wait[1], p_task_wait[2], p_task_wait[3], p_task_wait[4], p_task_wait[5]);
    }

    // Patch 10: Bypass crash in task sync function at +0x3de5f0 (ret)
    uint8_t* p_task_sync = g_module.base_address + 0x3de5f0;
    if (p_task_sync[0] == 0x55) {
        DWORD old_prot_p10;
        VirtualProtect(p_task_sync, 1, PAGE_EXECUTE_READWRITE, &old_prot_p10);
        p_task_sync[0] = 0xc3; // RET
        VirtualProtect(p_task_sync, 1, old_prot_p10, &old_prot_p10);
        std::cout << "[PATCH 10] Bypassed task stat/sync crash at +0x3de5f0 (ret).\n";
    } else {
        std::printf("[PATCH 10] Warning: byte at +0x3de5f0 is %02x\n", p_task_sync[0]);
    }

    // Patch 11: Protect against null viewport/camera in Preload UI update at +0x47cc64
    uint8_t* p_ui_update = g_module.base_address + 0x47cc64;
    if (p_ui_update[0] == 0xc5 && p_ui_update[1] == 0xf8 && p_ui_update[2] == 0x10) {
        DWORD old_prot_p11;
        VirtualProtect(p_ui_update, 15, PAGE_EXECUTE_READWRITE, &old_prot_p11);
        p_ui_update[0] = 0xc5; p_ui_update[1] = 0xf8; p_ui_update[2] = 0x11; p_ui_update[3] = 0x04; p_ui_update[4] = 0x24; // vmovups %xmm0, (%rsp)
        p_ui_update[5] = 0x48; p_ui_update[6] = 0x85; p_ui_update[7] = 0xff;                                               // test %rdi, %rdi
        p_ui_update[8] = 0x74; p_ui_update[9] = 0x05;                                                                     // jz +5 (to 0x47cc73)
        p_ui_update[10] = 0xe8; p_ui_update[11] = 0xbd; p_ui_update[12] = 0x78; p_ui_update[13] = 0xe9; p_ui_update[14] = 0xff; // call 0x314530
        VirtualProtect(p_ui_update, 15, old_prot_p11, &old_prot_p11);
        std::cout << "[PATCH 11] Protected null viewport object in UI update at +0x47cc64.\n";
    } else {
        std::printf("[PATCH 11] Warning: bytes at +0x47cc64 are %02x %02x %02x\n",
                    p_ui_update[0], p_ui_update[1], p_ui_update[2]);
    }

    // Patch 12: Protect against null UI component in +0x313aa0 (using 8-byte NOP padding at +0x313a98)
    uint8_t* p_ui_comp = g_module.base_address + 0x313a98;
    if (p_ui_comp[0] == 0x90 && p_ui_comp[8] == 0x55) {
        DWORD old_prot_p12;
        VirtualProtect(p_ui_comp, 16, PAGE_EXECUTE_READWRITE, &old_prot_p12);
        p_ui_comp[0] = 0x55; p_ui_comp[1] = 0x48; p_ui_comp[2] = 0x89; p_ui_comp[3] = 0xe5;
        p_ui_comp[4] = 0x41; p_ui_comp[5] = 0x57; p_ui_comp[6] = 0x41; p_ui_comp[7] = 0x56;
        p_ui_comp[8]  = 0x48; p_ui_comp[9]  = 0x85; p_ui_comp[10] = 0xff; // test %rdi, %rdi
        p_ui_comp[11] = 0x74; p_ui_comp[12] = 0x02;                     // jz +2 (to ret)
        p_ui_comp[13] = 0xeb; p_ui_comp[14] = 0xf1;                     // jmp -15 (to 0x313a98)
        p_ui_comp[15] = 0xc3;                                           // ret
        VirtualProtect(p_ui_comp, 16, old_prot_p12, &old_prot_p12);
        std::cout << "[PATCH 12] Protected null component in UI function at +0x313aa0.\n";
    } else {
        std::printf("[PATCH 12] Warning: bytes at +0x313a98 are %02x ... %02x\n", p_ui_comp[0], p_ui_comp[8]);
    }

    // Scan for all references to VuGameModeManager (0xe0f350)
    std::cout << "[GMM_SCAN] Scanning module for references to 0xe0f350...\n";
    for (size_t off = 0; off < g_module.total_size - 8; off++) {
        int32_t disp = *reinterpret_cast<int32_t*>(g_module.base_address + off);
        uint64_t target = off + 4 + disp;
        if (target == 0xe0f350) {
            std::cout << "  [REF] rip-relative to 0xe0f350 at +0x" << std::hex << off << std::dec << "\n";
        }
    }

    // Scan for calls to 0x19f020 (VuGameModeManager ctor)
    // Search for 0x19d1f0 in all relocated pointers
    uint64_t target_preload = reinterpret_cast<uint64_t>(g_module.base_address + 0x19d1f0);
    std::cout << "[VTABLE_SCAN] Searching for pointers to 0x19d1f0 (" << (void*)target_preload << ")...\n";
    for (size_t off = 0; off < g_module.total_size - 8; off += 8) {
        uint64_t val = *reinterpret_cast<uint64_t*>(g_module.base_address + off);
        if (val == target_preload) {
            std::cout << "  [VTABLE_PRELOAD] 0x19d1f0 found at vtable +0x" << std::hex << off << std::dec << "\n";
            // Print surrounding vtable entries
            for (int k = -4; k <= 8; k++) {
                uint64_t entry = *reinterpret_cast<uint64_t*>(g_module.base_address + off + k * 8);
                uint64_t rel = entry - reinterpret_cast<uint64_t>(g_module.base_address);
                std::cout << "    vtable[" << k << "] = +0x" << std::hex << rel << std::dec << "\n";
            }
        }
    }


    // Dump diagnostic functions for disassembly analysis
    {
        std::ofstream f("dump_subsys.bin", std::ios::binary);
        if (f) {
            f.write(reinterpret_cast<const char*>(g_module.base_address + 0x40b900), 0x400);
        }
        std::ofstream f3("dump_subsys2.bin", std::ios::binary);
        if (f3) {
            f3.write(reinterpret_cast<const char*>(g_module.base_address + 0x422540), 0x400);
        }
        std::ofstream f4("dump_mainloop.bin", std::ios::binary);
        if (f4) {
            f4.write(reinterpret_cast<const char*>(g_module.base_address + 0xc00), 0x200);
        }
        std::ofstream f7("dump_plt.bin", std::ios::binary);
        if (f7) {
            f7.write(reinterpret_cast<const char*>(g_module.base_address + 0x8ce500), 0x80);
        }
        std::ofstream f9("dump_plt_2e0.bin", std::ios::binary);
        if (f9) {
            f9.write(reinterpret_cast<const char*>(g_module.base_address + 0x8ce2c0), 0x40);
        }
        std::ofstream f10("dump_9900.bin", std::ios::binary);
        if (f10) {
            f10.write(reinterpret_cast<const char*>(g_module.base_address + 0x9900), 0x100);
        }
        std::ofstream f11("dump_99c0.bin", std::ios::binary);
        if (f11) {
            f11.write(reinterpret_cast<const char*>(g_module.base_address + 0x99c0), 0x200);
        }
        std::ofstream f_worker("dump_worker.bin", std::ios::binary);
        if (f_worker) {
            f_worker.write(reinterpret_cast<const char*>(g_module.base_address + 0x509140), 0x200);
        }
        std::ofstream f_tick1("dump_tick1.bin", std::ios::binary);
        if (f_tick1) {
            f_tick1.write(reinterpret_cast<const char*>(g_module.base_address + 0x40cba0), 0x200);
        }
        std::ofstream f_tick2("dump_tick2.bin", std::ios::binary);
        if (f_tick2) {
            f_tick2.write(reinterpret_cast<const char*>(g_module.base_address + 0x3d3a50), 0x200);
        }
        std::ofstream f_video("dump_video_init.bin", std::ios::binary);
        if (f_video) {
            f_video.write(reinterpret_cast<const char*>(g_module.base_address + 0x4f4100), 0x300);
        }
        std::ofstream f_queue("dump_queue.bin", std::ios::binary);
        if (f_queue) {
            f_queue.write(reinterpret_cast<const char*>(g_module.base_address + 0x3dcd00), 0x300);
        }
        std::ofstream f_pad("dump_pad.bin", std::ios::binary);
        if (f_pad) {
            f_pad.write(reinterpret_cast<const char*>(g_module.base_address + 0x50c200), 0xa00);
        }
        std::ofstream f_eng("dump_engine_init.bin", std::ios::binary);
        if (f_eng) {
            f_eng.write(reinterpret_cast<const char*>(g_module.base_address + 0x50d000), 0x200);
        }
        std::ofstream f_1ff("dump_1ff.bin", std::ios::binary);
        if (f_1ff) {
            f_1ff.write(reinterpret_cast<const char*>(g_module.base_address + 0x1ffa00), 0x200);
        }
        std::ofstream f_3c00("dump_3c00.bin", std::ios::binary);
        if (f_3c00) {
            f_3c00.write(reinterpret_cast<const char*>(g_module.base_address + 0x3c00), 0x400);
        }
        std::ofstream f_4000("dump_4000.bin", std::ios::binary);
        if (f_4000) {
            f_4000.write(reinterpret_cast<const char*>(g_module.base_address + 0x4000), 0x500);
        }
        std::ofstream f_5c0("dump_5c0.bin", std::ios::binary);
        if (f_5c0) {
            f_5c0.write(reinterpret_cast<const char*>(g_module.base_address + 0x500), 0x300);
        }
        std::ofstream f_b00("dump_b00.bin", std::ios::binary);
        if (f_b00) {
            f_b00.write(reinterpret_cast<const char*>(g_module.base_address + 0xb00), 0x180);
        }
        std::ofstream f_1ae("dump_1ae.bin", std::ios::binary);
        if (f_1ae) {
            f_1ae.write(reinterpret_cast<const char*>(g_module.base_address + 0x1ae400), 0x300);
        }
        std::ofstream f_218("dump_218.bin", std::ios::binary);
        if (f_218) {
            f_218.write(reinterpret_cast<const char*>(g_module.base_address + 0x218400), 0x400);
        }
        std::ofstream f_btn("dump_btn.bin", std::ios::binary);
        if (f_btn) {
            f_btn.write(reinterpret_cast<const char*>(g_module.base_address + 0x3fd300), 0x200);
        }
        std::ofstream f_pre("dump_preload.bin", std::ios::binary);
        if (f_pre) {
            f_pre.write(reinterpret_cast<const char*>(g_module.base_address + 0x19d200), 0x200);
        }
        std::ofstream f_6dc5("dump_6dc5.bin", std::ios::binary);
        if (f_6dc5) {
            f_6dc5.write(reinterpret_cast<const char*>(g_module.base_address + 0x6d80), 0x100);
        }
        std::ofstream f_6c7c("dump_6c7c.bin", std::ios::binary);
        if (f_6c7c) {
            f_6c7c.write(reinterpret_cast<const char*>(g_module.base_address + 0x6c7c00), 0x200);
        }
        std::ofstream f_6e00("dump_6e00.bin", std::ios::binary);
        if (f_6e00) {
            f_6e00.write(reinterpret_cast<const char*>(g_module.base_address + 0x6dc0), 0x300);
        }
    }

    // Initialize dummy subsystem pool
    static uint8_t s_dummy_pool[128][1024];
    static size_t s_dummy_pool_idx = 0;
    uint64_t* dummy_vtable = GetDummyVtable();

    auto create_dummy_subsystem = [&]() -> void* {
        if (s_dummy_pool_idx >= 128) s_dummy_pool_idx = 127;
        uint8_t* obj = s_dummy_pool[s_dummy_pool_idx++];
        std::memset(obj, 0, 1024);
        *reinterpret_cast<uint64_t*>(obj) = reinterpret_cast<uint64_t>(dummy_vtable);
        // Container 1 head & sentinel
        *reinterpret_cast<uint64_t*>(&obj[0x10]) = reinterpret_cast<uint64_t>(&obj[0x20]);
        *reinterpret_cast<uint64_t*>(&obj[0x20]) = reinterpret_cast<uint64_t>(&obj[0x20]);
        // Container 2 head & sentinel
        *reinterpret_cast<uint64_t*>(&obj[0x28]) = reinterpret_cast<uint64_t>(&obj[0x30]);
        *reinterpret_cast<uint64_t*>(&obj[0x30]) = reinterpret_cast<uint64_t>(&obj[0x30]);
        // Container 3 head & sentinel
        *reinterpret_cast<uint64_t*>(&obj[0x40]) = reinterpret_cast<uint64_t>(&obj[0x50]);
        *reinterpret_cast<uint64_t*>(&obj[0x50]) = reinterpret_cast<uint64_t>(&obj[0x50]);
        // Default scale / aspect ratio floats
        *reinterpret_cast<float*>(&obj[0xbc]) = 1.0f;
        *reinterpret_cast<float*>(&obj[0xb0]) = 1.0f;
        *reinterpret_cast<float*>(&obj[0xb4]) = 1.0f;
        *reinterpret_cast<float*>(&obj[0xb8]) = 1.0f;
        return obj;
    };

    // Explicitly link initial known subsystems
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0d568)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe0d568) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0d728)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe0d728) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0d5a8)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe0d5a8) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe2eab8)) {
        static uint8_t s_dummy_e2eab8[64] = {0};
        *reinterpret_cast<void**>(g_module.base_address + 0xe2eab8) = s_dummy_e2eab8;
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0e770)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe0e770) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0d018)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe0d018) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe137d8)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe137d8) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe13998)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe13998) = create_dummy_subsystem();
    }
    // Note: 0xe0f350 is VuGameModeManager, constructed natively by Patch 8 at +0xcf4d
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe34d28)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe34d28) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0a0e8)) {
        void* obj = create_dummy_subsystem();
        static_cast<uint8_t*>(obj)[0x110] = 1;
        *reinterpret_cast<void**>(g_module.base_address + 0xe0a0e8) = obj;
    } else {
        static_cast<uint8_t*>(*reinterpret_cast<void**>(g_module.base_address + 0xe0a0e8))[0x110] = 1;
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0a628)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe0a628) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0d620)) {
        void* obj = create_dummy_subsystem();
        static_cast<uint8_t*>(obj)[0x5c] = 1;
        *reinterpret_cast<void**>(g_module.base_address + 0xe0d620) = obj;
    } else {
        static_cast<uint8_t*>(*reinterpret_cast<void**>(g_module.base_address + 0xe0d620))[0x5c] = 1;
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe10358)) {
        void* obj = create_dummy_subsystem();
        static_cast<uint8_t*>(obj)[0x110] = 1;
        *reinterpret_cast<void**>(g_module.base_address + 0xe10358) = obj;
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe10898)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe10898) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe13990)) {
        void* obj = create_dummy_subsystem();
        static_cast<uint8_t*>(obj)[0x5c] = 1;
        *reinterpret_cast<void**>(g_module.base_address + 0xe13990) = obj;
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0e8a0)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe0e8a0) = create_dummy_subsystem();
    }
    if (!*reinterpret_cast<void**>(g_module.base_address + 0xe0d768)) {
        *reinterpret_cast<void**>(g_module.base_address + 0xe0d768) = create_dummy_subsystem();
    }

    // Initialize TitleSequence vtable GetName() (slot 5 at +0xc49170)
    *reinterpret_cast<void**>(g_module.base_address + 0xc49170) = reinterpret_cast<void*>(&HLE_GetTitleSequenceName);
    *reinterpret_cast<void**>(g_module.base_address + 0xc49178) = reinterpret_cast<void*>(&HLE_DummyGameModeMethod);
    std::cout << "[FIX] Initialized TitleSequence vtable GetName() at +0xc49170.\n";

    // Universal scan and patch of Subsystem Constructor (0x3c30..0x6dc5)
    uint8_t* p_scan = g_module.base_address + 0x3c30;
    uint8_t* p_end  = g_module.base_address + 0x6dc5;
    size_t abort_patch_count = 0;
    size_t subsys_link_count = 0;

    while (p_scan < p_end) {
        // Check for 'lea disp(%rip), %rcx' (48 8d 0d <disp32>)
        if (p_scan[0] == 0x48 && p_scan[1] == 0x8d && p_scan[2] == 0x0d) {
            int32_t disp = *reinterpret_cast<int32_t*>(p_scan + 3);
            uint8_t* var_addr = p_scan + 7 + disp;
            if (var_addr != g_module.base_address + 0xe2eab0 &&
                var_addr != g_module.base_address + 0xe090e0 &&
                var_addr != g_module.base_address + 0xe090f0 &&
                var_addr >= g_module.base_address && var_addr < g_module.base_address + g_module.total_size) {
                void** p_obj = reinterpret_cast<void**>(var_addr);
                if (!*p_obj) {
                    *p_obj = create_dummy_subsystem();
                    subsys_link_count++;
                }
            }
        }
        // Check for 'je 0x6dc5' (0f 84 <disp32>)
        if (p_scan <= p_end - 6 && p_scan[0] == 0x0f && p_scan[1] == 0x84) {
            int32_t disp = *reinterpret_cast<int32_t*>(p_scan + 2);
            uint8_t* target = p_scan + 6 + disp;
            if (target == p_end) {
                DWORD old_prot;
                VirtualProtect(p_scan, 6, PAGE_EXECUTE_READWRITE, &old_prot);
                for (int k = 0; k < 6; k++) p_scan[k] = 0x90; // NOP
                VirtualProtect(p_scan, 6, old_prot, &old_prot);
                abort_patch_count++;
            }
        }
        p_scan++;
    }
    std::cout << "[SUBSYS] Linked " << subsys_link_count << " null subsystem pointers to unique dummy instances.\n";
    std::cout << "[SUBSYS] Bypassed " << abort_patch_count << " failure abort checks (je 0x6dc5 -> NOP).\n";
    std::cout << "[CHECK] 0xe0d5a8: " << *reinterpret_cast<void**>(g_module.base_address + 0xe0d5a8) << "\n";
    std::cout << "[CHECK] 0xe0d768: " << *reinterpret_cast<void**>(g_module.base_address + 0xe0d768) << "\n";
    std::cout << "[CHECK] 0xe0e770: " << *reinterpret_cast<void**>(g_module.base_address + 0xe0e770) << "\n";
    std::cout << "[CHECK] 0xe0e8a0: " << *reinterpret_cast<void**>(g_module.base_address + 0xe0e8a0) << "\n";
    std::cout << "[CHECK] 0xe0d018 (Tick Tree 2): " << *reinterpret_cast<void**>(g_module.base_address + 0xe0d018) << "\n";
    std::cout << "[CHECK] 0xe137d8 (Draw Mgr 1): " << *reinterpret_cast<void**>(g_module.base_address + 0xe137d8) << "\n";
    std::cout << "[CHECK] 0xe13998 (Draw Mgr 2): " << *reinterpret_cast<void**>(g_module.base_address + 0xe13998) << "\n";
    std::cout << "[CHECK] 0xe090e0 (GameMode Mgr): " << *reinterpret_cast<void**>(g_module.base_address + 0xe090e0) << "\n";
    std::cout << "[CHECK] 0xe090f0: " << *reinterpret_cast<void**>(g_module.base_address + 0xe090f0) << "\n";

    std::cout << "[RUN] Preparing guest execution bootstrap...\n";

    struct StackParams {
        int64_t argc;
        const char* argv[2];
    };

    StackParams guest_args;
    guest_args.argc = 1;
    guest_args.argv[0] = "/app0/eboot.bin";
    guest_args.argv[1] = nullptr;

    typedef void (__attribute__((sysv_abi)) *GuestEntry)(void* rdi_args, void* rsi_argv, void* rdx_envp);
    GuestEntry entry = reinterpret_cast<GuestEntry>(g_module.base_address + g_module.entry_point);

    HANDLE g_main_thread_handle = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main_thread_handle, 0, FALSE, DUPLICATE_SAME_ACCESS);
    extern HANDLE g_guest_main_thread_handle;
    g_guest_main_thread_handle = g_main_thread_handle;

    std::cout << "[RUN] Jumping into guest entry point natively...\n" << std::flush;

    entry(&guest_args, &guest_args.argv[0], nullptr);

    std::cout << "[RUN] Guest execution exited cleanly.\n";
    return 0;
}
