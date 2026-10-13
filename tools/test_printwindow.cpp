#include <windows.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <cstdint>

#pragma pack(push, 1)
struct BMPHeader {
    uint16_t bfType{0x4D42};
    uint32_t bfSize{0};
    uint16_t bfReserved1{0};
    uint16_t bfReserved2{0};
    uint32_t bfOffBits{54};
    uint32_t biSize{40};
    int32_t  biWidth{0};
    int32_t  biHeight{0};
    uint16_t biPlanes{1};
    uint16_t biBitCount{24};
    uint32_t biCompression{0};
    uint32_t biSizeImage{0};
    int32_t  biXPelsPerMeter{0};
    int32_t  biYPelsPerMeter{0};
    uint32_t biClrUsed{0};
    uint32_t biClrImportant{0};
};
#pragma pack(pop)

void save_bmp(const char* filename, int w, int h, const std::vector<uint8_t>& pixels, int row_stride) {
    BMPHeader hdr;
    hdr.biWidth = w;
    hdr.biHeight = h;
    hdr.biSizeImage = row_stride * h;
    hdr.bfSize = 54 + hdr.biSizeImage;

    std::ofstream f(filename, std::ios::binary);
    f.write((char*)&hdr, sizeof(hdr));
    f.write((char*)pixels.data(), pixels.size());
    f.close();
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lParam) {
        char title[256] = {0};
        GetWindowTextA(hwnd, title, 255);
        if (strlen(title) > 0 && IsWindowVisible(hwnd) && strstr(title, "Beach Buggy")) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            POINT pt = {0, 0};
            ClientToScreen(hwnd, &pt);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            std::cout << "Window: \"" << title << "\" HWND=" << hwnd 
                      << " Pos=(" << pt.x << "," << pt.y << ") Size=" << w << "x" << h << "\n";

            // Method 1: PrintWindow PW_RENDERFULLCONTENT (0x2)
            HDC hdcScreen = GetDC(NULL);
            HDC hdcMem = CreateCompatibleDC(hdcScreen);
            HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
            SelectObject(hdcMem, hbm);

            BOOL pw_res = PrintWindow(hwnd, hdcMem, 2);
            std::cout << "PrintWindow(PW_RENDERFULLCONTENT) res=" << pw_res << "\n";

            BITMAPINFO bmi = {0};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = w;
            bmi.bmiHeader.biHeight = h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 24;
            bmi.bmiHeader.biCompression = BI_RGB;

            int row_stride = ((w * 3 + 3) / 4) * 4;
            std::vector<uint8_t> pixels(row_stride * h, 0);
            GetDIBits(hdcMem, hbm, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);

            int non_zero = 0;
            for (auto p : pixels) if (p != 0) non_zero++;
            std::cout << "PrintWindow non-zero bytes: " << non_zero << "\n";
            save_bmp("tools/pw_capture.bmp", w, h, pixels, row_stride);

            // Method 2: BitBlt from screen
            BitBlt(hdcMem, 0, 0, w, h, hdcScreen, pt.x, pt.y, SRCCOPY);
            GetDIBits(hdcMem, hbm, 0, h, pixels.data(), &bmi, DIB_RGB_COLORS);
            non_zero = 0;
            for (auto p : pixels) if (p != 0) non_zero++;
            std::cout << "BitBlt non-zero bytes: " << non_zero << "\n";
            save_bmp("tools/bitblt_capture.bmp", w, h, pixels, row_stride);

            DeleteObject(hbm);
            DeleteDC(hdcMem);
            ReleaseDC(NULL, hdcScreen);
        }
    }
    return TRUE;
}

int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    DWORD pid = atoi(argv[1]);
    EnumWindows(EnumWindowsProc, (LPARAM)pid);
    return 0;
}
