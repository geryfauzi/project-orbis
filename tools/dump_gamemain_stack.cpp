#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>

typedef HRESULT (WINAPI *pfnGetThreadDescription)(HANDLE, PWSTR*);

bool is_call(HANDLE hProcess, uint64_t ret_addr) {
    uint8_t code[8] = {0};
    SIZE_T br = 0;
    if (!ReadProcessMemory(hProcess, (void*)(ret_addr - 8), code, 8, &br) || br != 8) return false;
    // e8 rel32
    if (code[3] == 0xe8) return true;
    // ff /2 call
    if (code[6] == 0xff && (code[7] & 0x38) == 0x10) return true;
    if (code[5] == 0xff && (code[6] & 0x38) == 0x10) return true;
    if (code[2] == 0xff && (code[3] & 0x38) == 0x10) return true;
    if (code[2] == 0x41 && code[3] == 0xff) return true;
    if (code[1] == 0x41 && code[2] == 0xff) return true;
    return false;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    DWORD pid = atoi(argv[1]);
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return 1;

    uint64_t eboot_base = 0;
    uint64_t cur_addr = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (VirtualQueryEx(hProcess, (void*)cur_addr, &mbi, sizeof(mbi))) {
        if (mbi.RegionSize == 0xb30000 && (mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            eboot_base = (uint64_t)mbi.BaseAddress;
            break;
        }
        cur_addr = (uint64_t)mbi.BaseAddress + mbi.RegionSize;
        if (cur_addr == 0) break;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    pfnGetThreadDescription GetThreadDesc = (pfnGetThreadDescription)GetProcAddress(hKernel32, "GetThreadDescription");

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te; te.dwSize = sizeof(te);
    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                if (hThread) {
                    std::wstring name = L"";
                    if (GetThreadDesc) {
                        PWSTR desc = NULL;
                        if (SUCCEEDED(GetThreadDesc(hThread, &desc)) && desc) {
                            name = desc;
                            LocalFree(desc);
                        }
                    }
                    if (name == L"Game:Main") {
                        SuspendThread(hThread);
                        CONTEXT ctx = {0}; ctx.ContextFlags = CONTEXT_ALL;
                        GetThreadContext(hThread, &ctx);
                        std::wcout << L"Found Game:Main (TID " << te.th32ThreadID << L") RIP=0x" 
                                   << std::hex << ctx.Rip << L" RSP=0x" << ctx.Rsp << L"\n";
                        if (eboot_base && ctx.Rip >= eboot_base && ctx.Rip < eboot_base + 0xb2d7a8) {
                            uint64_t rip_vaddr = ctx.Rip - eboot_base;
                            std::cout << "  RIP is in EBOOT: vaddr 0x" << std::hex << rip_vaddr 
                                      << " (file 0x" << (rip_vaddr + 0x4000) << ")\n";
                        } else {
                            MEMORY_BASIC_INFORMATION rip_mbi;
                            if (VirtualQueryEx(hProcess, (void*)ctx.Rip, &rip_mbi, sizeof(rip_mbi))) {
                                char mod_path[MAX_PATH] = {0};
                                GetMappedFileNameA(hProcess, (void*)ctx.Rip, mod_path, sizeof(mod_path));
                                std::cout << "  RIP is in Module [" << mod_path << "] Base=0x" << std::hex << (uint64_t)rip_mbi.BaseAddress
                                          << " Size=0x" << rip_mbi.RegionSize << " Protect=0x" << rip_mbi.Protect << "\n";
                            }
                        }
                        uint8_t rip_bytes[16] = {0};
                        SIZE_T rip_br = 0;
                        if (ReadProcessMemory(hProcess, (void*)ctx.Rip, rip_bytes, 16, &rip_br)) {
                            std::cout << "  RIP bytes: ";
                            for (int k = 0; k < 16; k++) {
                                std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)rip_bytes[k] << " ";
                            }
                            std::cout << "\n";
                        }
                        uint64_t stack[1024] = {0};
                        SIZE_T br = 0;
                        ReadProcessMemory(hProcess, (void*)ctx.Rsp, stack, sizeof(stack), &br);
                        std::cout << "Valid CALL Return Addresses on Stack:\n";
                        for (size_t i = 0; i < br / 8; i++) {
                            uint64_t val = stack[i];
                            if (eboot_base && val >= eboot_base && val < eboot_base + 0xb2d7a8) {
                                if (is_call(hProcess, val)) {
                                    uint64_t vaddr = val - eboot_base;
                                    uint64_t file_off = vaddr + 0x4000;
                                    std::cout << "  [+0x" << std::hex << (i*8) << "] RET: eboot+0x" 
                                              << vaddr << " (file 0x" << file_off << ")\n";
                                }
                            }
                        }
                        ResumeThread(hThread);
                        CloseHandle(hThread);
                        break;
                    }
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
    CloseHandle(hProcess);
    return 0;
}
