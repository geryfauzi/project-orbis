#include <windows.h>
#include <iostream>
#include <iomanip>
#include <cstdint>

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    DWORD pid = atoi(argv[1]);
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return 1;

    // Find eboot_base dynamically
    uint64_t eboot_base = 0;
    uint64_t addr = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (VirtualQueryEx(hProcess, (void*)addr, &mbi, sizeof(mbi))) {
        if (mbi.RegionSize == 0xb30000 && (mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            eboot_base = (uint64_t)mbi.BaseAddress;
            break;
        }
        addr = (uint64_t)mbi.BaseAddress + mbi.RegionSize;
        if (addr == 0) break;
    }

    std::cout << "Real eboot_base: 0x" << std::hex << eboot_base << std::dec << "\n";
    if (!eboot_base) {
        CloseHandle(hProcess);
        return 1;
    }

    SIZE_T br = 0;
    auto read_u64 = [&](uint64_t va) -> uint64_t {
        uint64_t v = 0;
        ReadProcessMemory(hProcess, (void*)(eboot_base + va), &v, 8, &br);
        return v;
    };

    uint64_t e0b4f8 = read_u64(0xe0b4f8);
    uint64_t vugfx  = read_u64(0xe0f288);
    uint64_t sg     = read_u64(0xe0f818);
    uint64_t game   = read_u64(0xe0c8d8);
    uint64_t e0b518 = read_u64(0xe0b518);
    uint64_t e108d8 = read_u64(0xe108d8);

    std::cout << "Display (0xe0b4f8)    = 0x" << std::hex << e0b4f8 << "\n";
    std::cout << "VuGfx (0xe0f288)      = 0x" << vugfx << "\n";
    std::cout << "SceneGraph (0xe0f818) = 0x" << sg << "\n";
    std::cout << "Game (0xe0c8d8)       = 0x" << game << "\n";
    std::cout << "ExpMgr (0xe0b518)     = 0x" << e0b518 << "\n";
    std::cout << "Singleton(0xe108d8)   = 0x" << e108d8 << std::dec << "\n";

    if (e0b518) {
        uint8_t f8 = 0;
        ReadProcessMemory(hProcess, (void*)(e0b518 + 0x8), &f8, 1, &br);
        std::cout << "  ExpMgr->flag_0x8 = " << (int)f8 << "\n";
    }

    if (e0b4f8) {
        uint8_t f170 = 0;
        ReadProcessMemory(hProcess, (void*)(e0b4f8 + 0x170), &f170, 1, &br);
        std::cout << "  Display->flag_0x170 = " << (int)f170 << "\n";
    }

    if (vugfx) {
        uint32_t fc = 0;
        float dt = 0.0f;
        uint8_t f18 = 0;
        ReadProcessMemory(hProcess, (void*)(vugfx + 0x174), &fc, 4, &br);
        ReadProcessMemory(hProcess, (void*)(vugfx + 0x170), &dt, 4, &br);
        ReadProcessMemory(hProcess, (void*)(vugfx + 0x18), &f18, 1, &br);
        std::cout << "  VuGfx->frameCount = " << fc 
                  << ", dt = " << dt 
                  << ", flag_0x18 (pending frame) = " << (int)f18 << "\n";
    }

    if (game) {
        uint64_t sub1e0 = 0;
        ReadProcessMemory(hProcess, (void*)(game + 0x1e0), &sub1e0, 8, &br);
        std::cout << "  Game->subsystem_0x1e0 = 0x" << std::hex << sub1e0 << std::dec << "\n";
    }

    CloseHandle(hProcess);
    return 0;
}
