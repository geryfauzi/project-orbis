#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <string>

int main(int argc, char* argv[]) {
    std::string mode = "scan";
    if (argc >= 2) mode = argv[1];

    std::fstream f("extracted_game/eboot.bin", std::ios::in | std::ios::out | std::ios::binary);
    if (!f) {
        std::cerr << "Cannot open extracted_game/eboot.bin\n";
        return 1;
    }
    f.seekg(0, std::ios::end);
    size_t sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(sz);
    f.read((char*)data.data(), sz);

    uint32_t fail_exit = 0xd035;
    std::vector<size_t> jumps;

    for (size_t i = 0x9ea0; i < 0xd035; i++) {
        if (data[i] == 0x0f && data[i+1] == 0x84) {
            int32_t disp = *(int32_t*)&data[i+2];
            uint32_t dest = (uint32_t)(i + 6 + disp);
            if (dest == fail_exit) {
                jumps.push_back(i);
            }
        }
    }

    std::cout << "Found " << jumps.size() << " conditional jumps to 0xd035 in range [0x9ea0..0xd035].\n";

    if (mode == "scan") {
        for (size_t off : jumps) {
            std::cout << "  File 0x" << std::hex << off << " (vaddr 0x" << (off - 0x4000) << ")\n";
        }
    } else if (mode == "patch5") {
        std::cout << "Patching first 5 jumps before VuGfx::Create (0xa1eb)...\n";
        int patched = 0;
        for (size_t off : jumps) {
            if (off <= 0xa1e5) {
                uint8_t nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
                f.seekp(off);
                f.write((char*)nops, 6);
                std::cout << "  Patched file 0x" << std::hex << off << " with 6 NOPs\n";
                patched++;
            }
        }
        std::cout << "Patched " << std::dec << patched << " jumps.\n";
    } else if (mode == "patch_all") {
        std::cout << "Patching ALL " << jumps.size() << " failure jumps to 0xd035...\n";
        for (size_t off : jumps) {
            uint8_t nops[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
            f.seekp(off);
            f.write((char*)nops, 6);
        }
        std::cout << "Done patching all jumps.\n";
    } else if (mode == "revert") {
        std::ifstream bak("extracted_game/eboot.bin.bak_before_subsys_patch", std::ios::binary);
        if (!bak) {
            std::cerr << "Backup file not found.\n";
            return 1;
        }
        std::vector<uint8_t> bak_data(sz);
        bak.read((char*)bak_data.data(), sz);
        f.seekp(0);
        f.write((char*)bak_data.data(), sz);
        std::cout << "Reverted eboot.bin from backup.\n";
    }

    return 0;
}
