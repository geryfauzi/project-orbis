#include "resolver.h"
#include "nid_table.h"
#include <windows.h>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstring>

static uint8_t* s_trampoline_base = nullptr;
static size_t s_sym_count = 0;
static std::vector<SysVHandler> s_handlers;
static std::unordered_map<std::string, SysVHandler> s_named_handlers;

bool IsValidReadPtr(const void* ptr, size_t size) {
    if (!ptr || reinterpret_cast<uintptr_t>(ptr) < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if ((mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    uintptr_t offset = reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    return (offset + size <= mbi.RegionSize);
}

static LoadedModule* s_module = nullptr;

struct SymMeta {
    std::string clean_name;
    std::string raw_nid;
    std::string full_name;
};
static std::vector<SymMeta> s_sym_meta;

extern "C" int64_t __attribute__((sysv_abi)) MasterDispatcher(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    register int64_t r10_val asm("r10");
    uint32_t sym_idx = static_cast<uint32_t>(r10_val);

    const char* sym_name = "unknown";
    const char* sym_nid = "unknown";
    if (sym_idx < s_sym_meta.size()) {
        sym_name = s_sym_meta[sym_idx].clean_name.c_str();
        sym_nid = s_sym_meta[sym_idx].raw_nid.c_str();
    }

    static uint8_t s_sym_logged[1024] = {0};
    static SRWLOCK s_dispatch_lock = SRWLOCK_INIT;
    if (sym_idx < 1024 && !s_sym_logged[sym_idx]) {
        AcquireSRWLockExclusive(&s_dispatch_lock);
        if (!s_sym_logged[sym_idx]) {
            s_sym_logged[sym_idx] = 1;
            uint64_t caller_rip = reinterpret_cast<uint64_t>(__builtin_return_address(0));
            uint64_t rel_rip = 0;
            if (s_module && s_module->base_address) {
                uint64_t base = reinterpret_cast<uint64_t>(s_module->base_address);
                if (caller_rip >= base && caller_rip < base + s_module->total_size) {
                    rel_rip = caller_rip - base;
                }
            }
            std::cout << "[CALL] #" << sym_idx << " " << sym_name << " (" << sym_nid << ") from +0x" << std::hex << rel_rip << std::dec << std::endl;
        }
        ReleaseSRWLockExclusive(&s_dispatch_lock);
    }

    if (sym_idx < s_handlers.size() && s_handlers[sym_idx]) {
        return s_handlers[sym_idx](a1, a2, a3, a4, a5, a6);
    }

    // Fast stub return SCE_OK
    return 0;
}

void RegisterHandler(const std::string& name, SysVHandler handler) {
    s_named_handlers[name] = handler;
}

extern "C" int64_t __attribute__((sysv_abi)) DummyVMethod() {
    return 1;
}

static uint64_t* s_dummy_vtable = nullptr;

uint64_t* GetDummyVtable() {
    return s_dummy_vtable;
}
static uint8_t*  s_object_arena = nullptr;
static size_t    s_arena_offset = 0;
static char*     s_dummy_environ[2] = { nullptr, nullptr };

bool InitResolver(LoadedModule& module) {
    s_module = &module;
    s_sym_count = module.symcount;
    s_handlers.assign(s_sym_count, nullptr);

    // Dedicated 16MB arena for guest global objects and vtables
    s_object_arena = reinterpret_cast<uint8_t*>(VirtualAlloc(
        nullptr,
        16 * 1024 * 1024,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    ));
    s_dummy_vtable = reinterpret_cast<uint64_t*>(s_object_arena);
    for (int i = 0; i < 256; i++) {
        s_dummy_vtable[i] = reinterpret_cast<uint64_t>(&DummyVMethod);
    }
    s_arena_offset = 4096;

    // Allocate executable memory for trampolines (32 bytes per symbol)
    size_t tramp_size = (s_sym_count * 32 + 4095) & ~4095;
    s_trampoline_base = reinterpret_cast<uint8_t*>(VirtualAlloc(
        nullptr,
        tramp_size,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE
    ));

    if (!s_trampoline_base) {
        std::cerr << "[RESOLVER] Failed to allocate trampoline memory!" << std::endl;
        return false;
    }

    std::unordered_map<std::string, std::string> nid_to_clean;
    for (size_t k = 0; k < sizeof(g_NidTable) / sizeof(g_NidTable[0]); k++) {
        if (g_NidTable[k].nid && g_NidTable[k].nid[0]) {
            nid_to_clean[g_NidTable[k].nid] = g_NidTable[k].name;
        }
        if (g_NidTable[k].full_symbol && g_NidTable[k].full_symbol[0]) {
            nid_to_clean[g_NidTable[k].full_symbol] = g_NidTable[k].name;
        }
    }

    nid_to_clean["6ncge5+l5Qs"] = "scePadClose";
    nid_to_clean["gjP9-KQzoUk"] = "scePadGetControllerInformation";
    nid_to_clean["hv1luiJrqQM"] = "scePadInit";
    nid_to_clean["xk0AcarP3V4"] = "scePadOpen";
    nid_to_clean["YndgXqQVV7c"] = "scePadReadState";
    nid_to_clean["yFVnOdGxvZY"] = "scePadSetVibration";
    nid_to_clean["fPhymKNvK-A"] = "sceUserServiceGetLoginUserIdList";
    nid_to_clean["j3YMu1MVNNo"] = "sceUserServiceInitialize";
    nid_to_clean["bwFjS+bX9mA"] = "sceUserServiceTerminate";

    s_sym_meta.resize(s_sym_count);

    size_t bound_count = 0;

    // Generate trampolines for each symbol
    for (size_t i = 0; i < s_sym_count; i++) {
        uint8_t* tramp = s_trampoline_base + (i * 32);

        // mov r10, i
        tramp[0] = 0x49; tramp[1] = 0xba;
        uint64_t imm = i;
        std::memcpy(tramp + 2, &imm, 8);

        // mov r11, MasterDispatcher
        tramp[10] = 0x49; tramp[11] = 0xbb;
        uint64_t target = reinterpret_cast<uint64_t>(&MasterDispatcher);
        std::memcpy(tramp + 12, &target, 8);

        // jmp r11
        tramp[20] = 0x41; tramp[21] = 0xff; tramp[22] = 0xe3;
        // nop fill
        for (int p = 23; p < 32; p++) tramp[p] = 0x90;

        const char* elf_name = (module.strtab && module.symtab) ? (module.strtab + module.symtab[i].st_name) : "";
        std::string full_name = elf_name ? elf_name : "";
        std::string raw_nid = full_name;
        size_t hash_pos = raw_nid.find('#');
        if (hash_pos != std::string::npos) {
            raw_nid = raw_nid.substr(0, hash_pos);
        }

        std::string clean_name = raw_nid;
        auto nit = nid_to_clean.find(full_name);
        if (nit != nid_to_clean.end()) {
            clean_name = nit->second;
        } else {
            nit = nid_to_clean.find(raw_nid);
            if (nit != nid_to_clean.end()) {
                clean_name = nit->second;
            }
        }

        s_sym_meta[i] = { clean_name, raw_nid, full_name };

        // Match handler by clean name, raw NID, or full ELF name
        SysVHandler h = nullptr;
        auto hit = s_named_handlers.find(clean_name);
        if (hit != s_named_handlers.end()) {
            h = hit->second;
        } else {
            hit = s_named_handlers.find(raw_nid);
            if (hit != s_named_handlers.end()) {
                h = hit->second;
            } else {
                hit = s_named_handlers.find(full_name);
                if (hit != s_named_handlers.end()) {
                    h = hit->second;
                }
            }
        }

        if (h) {
            s_handlers[i] = h;
            bound_count++;
            if (clean_name.find("Pad") != std::string::npos || raw_nid.find("xk0Acar") != std::string::npos) {
                std::cout << "[RESOLVER] Bound #" << i << " " << clean_name << " (" << raw_nid << ")\n";
            }
        }
    }

    std::cout << "[RESOLVER] Successfully resolved and bound " << bound_count << " / " << s_sym_count << " ELF imported symbols to native HLE handlers.\n";
    return true;
}

uint64_t GetSymbolAddress(uint32_t sym_idx) {
    if (sym_idx >= s_sym_count) return 0;

    // Check symbol type
    if (s_module && sym_idx < s_module->symcount) {
        if ((s_module->symtab[sym_idx].st_info & 0xf) == 1) { // STT_OBJECT
            if (sym_idx == 495) { // environ
                return reinterpret_cast<uint64_t>(&s_dummy_environ);
            }
            if (sym_idx >= 299 && sym_idx <= 301) {
                return reinterpret_cast<uint64_t>(s_dummy_vtable);
            }
            static std::unordered_map<uint32_t, uint64_t> s_sym_objects;
            auto it = s_sym_objects.find(sym_idx);
            if (it != s_sym_objects.end()) {
                return it->second;
            }

            size_t alloc_sz = 65536;
            if (s_arena_offset + alloc_sz <= 16 * 1024 * 1024) {
                uint8_t* obj_mem = s_object_arena + s_arena_offset;
                s_arena_offset += alloc_sz;
                *reinterpret_cast<uint64_t*>(obj_mem) = reinterpret_cast<uint64_t>(s_dummy_vtable);
                uint64_t* holder = reinterpret_cast<uint64_t*>(obj_mem + 32768);
                *holder = reinterpret_cast<uint64_t>(obj_mem);
                s_sym_objects[sym_idx] = reinterpret_cast<uint64_t>(holder);
                return reinterpret_cast<uint64_t>(holder);
            }
        }
    }

    if (sym_idx < s_handlers.size() && s_handlers[sym_idx]) {
        return reinterpret_cast<uint64_t>(s_handlers[sym_idx]);
    }

    return reinterpret_cast<uint64_t>(s_trampoline_base + (sym_idx * 32));
}

bool ApplyRelocations(LoadedModule& module) {
    size_t relative_count = 0;
    size_t jump_slot_count = 0;
    size_t glob_dat_count = 0;
    size_t abs64_count = 0;
    size_t other_count = 0;

    // Process RELA table
    for (size_t i = 0; i < module.relasz; i++) {
        const auto& rel = module.rela[i];
        uint32_t type = ELF64_R_TYPE(rel.r_info);
        uint32_t sym = ELF64_R_SYM(rel.r_info);
        uint8_t* target = module.base_address + rel.r_offset;

        switch (type) {
            case R_X86_64_RELATIVE:
                *reinterpret_cast<uint64_t*>(target) = reinterpret_cast<uint64_t>(module.base_address + rel.r_addend);
                relative_count++;
                break;
            case R_X86_64_64: {
                uint64_t sym_addr = GetSymbolAddress(sym);
                *reinterpret_cast<uint64_t*>(target) = sym_addr + rel.r_addend;
                abs64_count++;
                break;
            }
            case R_X86_64_GLOB_DAT: {
                uint64_t sym_addr = GetSymbolAddress(sym);
                *reinterpret_cast<uint64_t*>(target) = sym_addr;
                glob_dat_count++;
                break;
            }
            case 33: // R_X86_64_TLS_TPOFF64
                *reinterpret_cast<uint64_t*>(target) = rel.r_addend;
                other_count++;
                break;
            default:
                other_count++;
                break;
        }
    }

    // Process JMPREL table (PLT)
    for (size_t i = 0; i < module.jmprelsz; i++) {
        const auto& rel = module.jmprel[i];
        uint32_t type = ELF64_R_TYPE(rel.r_info);
        uint32_t sym = ELF64_R_SYM(rel.r_info);
        uint8_t* target = module.base_address + rel.r_offset;

        if (type == R_X86_64_JUMP_SLOT) {
            uint64_t sym_addr = GetSymbolAddress(sym);
            *reinterpret_cast<uint64_t*>(target) = sym_addr;
            jump_slot_count++;
            if (sym >= 210 && sym <= 215) {
                const char* name = (sym < s_sym_meta.size()) ? s_sym_meta[sym].clean_name.c_str() : "unknown";
                std::cout << "[PLT_MAP] GOT 0x" << std::hex << rel.r_offset << " -> sym #" << std::dec << sym << " (" << name << ")\n";
            }
        }
    }

    auto print_plt_target = [&](uint64_t plt_offset) {
        uint8_t* p = module.base_address + plt_offset;
        if (p[0] == 0xff && p[1] == 0x25) {
            int32_t rip_off = *reinterpret_cast<int32_t*>(p + 2);
            uint64_t got_offset = plt_offset + 6 + rip_off;
            bool found = false;
            for (size_t k = 0; k < module.jmprelsz; k++) {
                if (module.jmprel[k].r_offset == got_offset) {
                    uint32_t sym_id = ELF64_R_SYM(module.jmprel[k].r_info);
                    const char* sym_name = (sym_id < sizeof(g_NidTable)/sizeof(g_NidTable[0])) ? g_NidTable[sym_id].name : "unknown";
                    std::cout << "[PLT_INFO] PLT +0x" << std::hex << plt_offset << " -> GOT +0x" << got_offset << " => sym #" << std::dec << sym_id << " (" << sym_name << ")\n";
                    found = true;
                    break;
                }
            }
            if (!found) {
                for (size_t k = 0; k < module.relasz; k++) {
                    if (module.rela[k].r_offset == got_offset) {
                        uint32_t sym_id = ELF64_R_SYM(module.rela[k].r_info);
                        const char* sym_name = (sym_id < sizeof(g_NidTable)/sizeof(g_NidTable[0])) ? g_NidTable[sym_id].name : "unknown";
                        std::cout << "[PLT_INFO] (RELA) PLT +0x" << std::hex << plt_offset << " -> GOT +0x" << got_offset << " => sym #" << std::dec << sym_id << " (" << sym_name << ")\n";
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                std::cout << "[PLT_INFO] PLT +0x" << std::hex << plt_offset << " -> GOT +0x" << got_offset << " => UNRESOLVED GOT\n";
            }
        } else {
            std::cout << "[PLT_INFO] PLT +0x" << std::hex << plt_offset << " => NOT JMP* DISP (bytes: "
                      << (int)p[0] << " " << (int)p[1] << " " << (int)p[2] << " " << (int)p[3] << ")\n" << std::dec;
        }
    };
    for (uint64_t plt_off = 0x800000; plt_off < 0x900000; plt_off += 0x10) {
        uint8_t* p = module.base_address + plt_off;
        if (p[0] == 0xff && p[1] == 0x25) {
            int32_t rip_off = *reinterpret_cast<int32_t*>(p + 2);
            uint64_t got_offset = plt_off + 6 + rip_off;
            if (got_offset == 0xc58148) {
                std::cout << "[PLT_FOUND] scePadOpen PLT is at +0x" << std::hex << plt_off << std::dec << "\n";
            }
            if (got_offset == 0xc58158) {
                std::cout << "[PLT_FOUND] scePadReadState PLT is at +0x" << std::hex << plt_off << std::dec << "\n";
            }
            if (got_offset == 0xc577c0) {
                std::cout << "[PLT_FOUND] scePadInit PLT is at +0x" << std::hex << plt_off << std::dec << "\n";
            }
        }
    }

    print_plt_target(0x8cf100);

    uint64_t pad_update_addr = reinterpret_cast<uint64_t>(module.base_address + 0x50c6f0);
    for (size_t seg_off = 0; seg_off < module.total_size - 8; seg_off += 8) {
        uint64_t val = *reinterpret_cast<uint64_t*>(module.base_address + seg_off);
        if (val == pad_update_addr) {
            std::cout << "[VTABLE_FOUND] 0x50c6f0 is referenced at +0x" << std::hex << seg_off << std::dec << "\n";
        }
    }

    uint8_t* p4300 = module.base_address + 0x4300;
    std::cout << "[SUBSYS_PAD] bytes at +0x4300:";
    for (int k = 0; k < 32; k++) {
        std::cout << " " << std::hex << (int)p4300[k];
    }
    std::cout << std::dec << "\n";
    print_plt_target(0x8d46c0);
    print_plt_target(0x8d46d0);
    print_plt_target(0x8d4710);
    print_plt_target(0x8d47a0);
    print_plt_target(0x8d45d0);

    std::cout << "[RESOLVER] Relocations applied: "
              << relative_count << " RELATIVE, "
              << jump_slot_count << " JUMP_SLOT, "
              << abs64_count << " 64-BIT, "
              << glob_dat_count << " GLOB_DAT, "
              << other_count << " other." << std::endl;

    return true;
}
