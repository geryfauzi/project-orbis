#include <iostream>
#include <fstream>
#include <cstdint>

int main() {
    std::fstream f("extracted_game/eboot.bin", std::ios::in | std::ios::out | std::ios::binary);
    if (!f) {
        std::cerr << "Cannot open extracted_game/eboot.bin\n";
        return 1;
    }

    uint8_t orig_bytes[9] = { 0x80, 0x7f, 0x08, 0x00, 0x74, 0x03, 0x31, 0xc0, 0xc3 };

    f.seekp(0x1880a0);
    f.write((char*)orig_bytes, sizeof(orig_bytes));
    f.flush();
    f.close();

    std::cout << "Reverted file offset 0x1880a0 to original bytes successfully.\n";
    return 0;
}
