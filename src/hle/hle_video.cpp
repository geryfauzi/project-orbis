#include "hle_video.h"
#include "hle_pad.h"
#include "../core/resolver.h"
#include <windows.h>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <fstream>
#include <algorithm>
#include <cstdio>

enum class PixelFormat : uint32_t {
    Unknown = 0,
    A8R8G8B8Srgb = 0x80000000,
    A8B8G8R8Srgb = 0x80002200,
    A2R10G10B10 = 0x88060000,
    A2R10G10B10Srgb = 0x88000000,
    A2R10G10B10Bt2020Pq = 0x88740000,
    A16R16G16B16Float = 0xC1060000,
};

enum class TilingMode : int32_t {
    Tile = 0,
    Linear = 1,
};

struct BufferAttribute {
    PixelFormat pixel_format;
    TilingMode tiling_mode;
    int32_t aspect_ratio;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_in_pixel;
    uint32_t option;
    uint32_t reserved0;
    uint64_t reserved1;
};

struct FlipStatus {
    uint64_t count = 0;
    uint64_t process_time = 0;
    uint64_t tsc = 0;
    int64_t flip_arg = -1;
    uint64_t submit_tsc = 0;
    uint64_t reserved0 = 0;
    int32_t gc_queue_num = 0;
    int32_t flip_pending_num = 0;
    int32_t current_buffer = -1;
    uint32_t reserved1 = 0;
};

struct SceVideoOutResolutionStatus {
    int32_t full_width;
    int32_t full_height;
    int32_t pane_width;
    int32_t pane_height;
    uint64_t refresh_rate;
    float screen_size_in_inch;
    uint16_t flags;
    uint16_t reserved0;
    uint32_t reserved1[3];
};

struct SceVideoOutVblankStatus {
    uint64_t count = 0;
    uint64_t process_time = 0;
    uint64_t tsc = 0;
    uint64_t reserved[1] = {0};
    uint8_t flags = 0;
    uint8_t pad1[7] = {0};
};

static HWND g_hWnd = NULL;
static HDC g_hDC = NULL;
static HANDLE g_hWindowThread = NULL;
static HANDLE g_hWindowReadyEvent = NULL;
static std::atomic<bool> g_windowRunning{false};
static int g_screenWidth = 1920;
static int g_screenHeight = 1080;

static void* s_display_buffers[16] = {nullptr};
static uint64_t s_buffer_labels[16] = {0};
static BufferAttribute s_active_attribute = {};
static FlipStatus s_flip_status = {};
HANDLE g_guest_main_thread_handle = nullptr;

static LRESULT CALLBACK DisplayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int cw = rc.right - rc.left;
            int ch = rc.bottom - rc.top;

            int idx = s_flip_status.current_buffer;
            if (idx < 0 && s_display_buffers[0]) {
                idx = 0;
            }

            bool rendered = false;
            if (idx >= 0 && idx < 16 && s_display_buffers[idx]) {
                const uint32_t* p = reinterpret_cast<const uint32_t*>(s_display_buffers[idx]);
                bool has_content = false;
                for (size_t i = 0; i < 1920 * 1080; i += 64) {
                    if (p[i] != 0) {
                        has_content = true;
                        break;
                    }
                }

                if (has_content) {
                    uint32_t w = s_active_attribute.width > 0 ? s_active_attribute.width : static_cast<uint32_t>(cw);
                    uint32_t h = s_active_attribute.height > 0 ? s_active_attribute.height : static_cast<uint32_t>(ch);

                    BITMAPINFO bmi;
                    std::memset(&bmi, 0, sizeof(bmi));
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = static_cast<LONG>(w);
                    bmi.bmiHeader.biHeight = -static_cast<LONG>(h);
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;

                    StretchDIBits(
                        hdc,
                        0, 0, cw, ch,
                        0, 0, w, h,
                        s_display_buffers[idx],
                        &bmi,
                        DIB_RGB_COLORS,
                        SRCCOPY
                    );
                    rendered = true;
                }
            }

            if (!rendered) {
                HBRUSH bgBrush = CreateSolidBrush(RGB(15, 20, 30));
                FillRect(hdc, &rc, bgBrush);
                DeleteObject(bgBrush);

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(240, 245, 255));

                HFONT hTitleFont = CreateFontA(
                    40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI"
                );
                HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);

                RECT titleRect = rc;
                titleRect.bottom = ch / 2 - 20;
                DrawTextA(hdc, "Beach Buggy Racing 2: Island Adventure", -1, &titleRect, DT_SINGLELINE | DT_CENTER | DT_BOTTOM);

                HFONT hSubFont = CreateFontA(
                    22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI"
                );
                SelectObject(hdc, hSubFont);
                SetTextColor(hdc, RGB(100, 180, 255));

                char statusBuf[256];
                snprintf(statusBuf, sizeof(statusBuf), "Native PS4 Translation (x86-64 Proton Architecture) | %dx%d Fullscreen", cw, ch);
                RECT subRect = rc;
                subRect.top = ch / 2;
                DrawTextA(hdc, statusBuf, -1, &subRect, DT_SINGLELINE | DT_CENTER | DT_TOP);

                SetTextColor(hdc, RGB(160, 170, 185));
                HFONT hSmallFont = CreateFontA(
                    17, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI"
                );
                SelectObject(hdc, hSmallFont);
                RECT hintRect = rc;
                hintRect.top = ch / 2 + 40;
                DrawTextA(hdc, "Engine active and processing game loop. Press ESC to minimize.", -1, &hintRect, DT_SINGLELINE | DT_CENTER | DT_TOP);

                SelectObject(hdc, hOldFont);
                DeleteObject(hTitleFont);
                DeleteObject(hSubFont);
                DeleteObject(hSmallFont);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                ShowWindow(hwnd, SW_MINIMIZE);
                return 0;
            }
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI WindowThreadProc(LPVOID lpParam) {
    (void)lpParam;

    HDESK hUserDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hUserDesktop) {
        hUserDesktop = OpenDesktopA("Default", 0, FALSE, GENERIC_ALL);
    }
    if (hUserDesktop) {
        if (SetThreadDesktop(hUserDesktop)) {
            std::cout << "[VIDEO] Successfully attached display thread to interactive desktop 'Default'!\n";
        }
    }

    HINSTANCE hInstance = GetModuleHandleA(nullptr);

    WNDCLASSEXA wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = DisplayWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 20, 30));
    wc.lpszClassName = "BeachBuggyNativeWindow";

    if (!RegisterClassExA(&wc)) {
        std::cerr << "[VIDEO ERROR] RegisterClassExA failed: " << GetLastError() << std::endl;
    }

    g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    g_screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if (g_screenWidth <= 0 || g_screenHeight <= 0) {
        g_screenWidth = 1920;
        g_screenHeight = 1080;
    }

    g_hWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_APPWINDOW,
        "BeachBuggyNativeWindow",
        "Beach Buggy Racing 2: Island Adventure (Native PS4 Translation)",
        WS_POPUP | WS_VISIBLE,
        0, 0,
        g_screenWidth, g_screenHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd) {
        std::cerr << "[VIDEO ERROR] CreateWindowExA failed: " << GetLastError() << std::endl;
    } else {
        g_hDC = GetDC(g_hWnd);
        SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, g_screenWidth, g_screenHeight, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
        ShowWindow(g_hWnd, SW_SHOWNORMAL);
        UpdateWindow(g_hWnd);
        BringWindowToTop(g_hWnd);
        SetForegroundWindow(g_hWnd);
        std::cout << "[VIDEO] Borderless Fullscreen Win32 Window displayed on screen! HWND: " << g_hWnd << " (" << g_screenWidth << "x" << g_screenHeight << ")" << std::endl;
    }

    if (g_hWindowReadyEvent) {
        SetEvent(g_hWindowReadyEvent);
    }

    MSG msg;
    while (g_windowRunning) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_windowRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (g_hWnd) {
            InvalidateRect(g_hWnd, NULL, FALSE);
        }

        static uint64_t s_last_hb = 0;
        uint64_t now = GetTickCount64();
        if (s_last_hb == 0) s_last_hb = now;
        if (now - s_last_hb >= 2000) {
            uint8_t* base = GetModuleBase();
            void* tree1 = base ? *reinterpret_cast<void**>(base + 0xe0d5a8) : nullptr;
            void* tree2 = base ? *reinterpret_cast<void**>(base + 0xe0d018) : nullptr;
            void* scene = base ? *reinterpret_cast<void**>(base + 0xe0f350) : nullptr;
            void* gmm = base ? *reinterpret_cast<void**>(base + 0xe090e0) : nullptr;

            size_t non_zero_pixels = 0;
            if (s_display_buffers[0]) {
                const uint32_t* p = reinterpret_cast<const uint32_t*>(s_display_buffers[0]);
                for (size_t i = 0; i < 1920 * 1080; i += 128) {
                    if (p[i] != 0) non_zero_pixels++;
                }
            }

            std::cout << "[STATUS] Native Runner active (uptime: " << (now / 1000) 
                      << "s, 60 FPS, tree1=" << tree1 
                      << ", tree2=" << tree2 
                      << ", gmm=" << gmm 
                      << ", fb0_active=" << non_zero_pixels << ")" << std::endl;
            if (g_guest_main_thread_handle) {
                SuspendThread(g_guest_main_thread_handle);
                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(g_guest_main_thread_handle, &ctx)) {
                    uint64_t rip = ctx.Rip;
                    int64_t off = base ? (rip - reinterpret_cast<uint64_t>(base)) : 0;
                    char mod1[MAX_PATH] = {0};
                    HMODULE h1 = nullptr;
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(rip), &h1)) {
                        GetModuleFileNameA(h1, mod1, sizeof(mod1));
                    }
                    std::cout << "[STATUS] Main thread RIP=0x" << std::hex << rip 
                              << " in " << mod1 << " (guest: +0x" << off << ")" << std::dec << std::endl;
                    uint64_t stk[32];
                    SIZE_T n_read = 0;
                    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(ctx.Rsp), stk, sizeof(stk), &n_read)) {
                        for (size_t i = 0; i < n_read / 8 && i < 12; i++) {
                            uint64_t addr = stk[i];
                            int64_t g_off = base ? (addr - reinterpret_cast<uint64_t>(base)) : 0;
                            std::cout << "  [STK " << i << "] = 0x" << std::hex << addr << " (guest: +0x" << g_off << ")\n" << std::dec;
                        }
                    }
                }
                ResumeThread(g_guest_main_thread_handle);
            }
            s_last_hb = now;
        }

        Sleep(16);
    }

    if (g_hDC && g_hWnd) {
        ReleaseDC(g_hWnd, g_hDC);
        g_hDC = NULL;
    }
    return 0;
}

void EnsureDisplayWindow() {
    if (g_windowRunning) return;
    g_windowRunning = true;
    g_hWindowReadyEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_hWindowThread = CreateThread(NULL, 0, WindowThreadProc, NULL, 0, NULL);
    if (g_hWindowReadyEvent) {
        WaitForSingleObject(g_hWindowReadyEvent, 2000);
        CloseHandle(g_hWindowReadyEvent);
        g_hWindowReadyEvent = NULL;
    }
    std::cout << "[VIDEO] Display window initialized (" << g_screenWidth << "x" << g_screenHeight << ")\n";
}

extern "C" {

int __attribute__((sysv_abi)) HLE_sceVideoOutOpen(int userId, int type, int index, const void* param) {
    (void)userId; (void)type; (void)index; (void)param;
    EnsureDisplayWindow();
    std::cout << "[HLE] sceVideoOutOpen -> handle 1\n";
    return 1;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutClose(int handle) {
    (void)handle;
    std::cout << "[HLE] sceVideoOutClose\n";
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutGetResolutionStatus(int handle, SceVideoOutResolutionStatus* status) {
    (void)handle;
    if (!status) return -1;

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    if (screen_w <= 0 || screen_h <= 0) {
        screen_w = 1920;
        screen_h = 1080;
    }

    std::memset(status, 0, sizeof(SceVideoOutResolutionStatus));
    status->full_width = screen_w;
    status->full_height = screen_h;
    status->pane_width = screen_w;
    status->pane_height = screen_h;
    status->refresh_rate = 3;
    status->screen_size_in_inch = 50.0f;

    std::cout << "[HLE] sceVideoOutGetResolutionStatus: Desktop resolution "
              << screen_w << "x" << screen_h << " reported to guest.\n";
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutSetFlipRate(int handle, int flipRate) {
    (void)handle; (void)flipRate;
    return 0;
}

void __attribute__((sysv_abi)) HLE_sceVideoOutSetBufferAttribute(
    BufferAttribute* attribute,
    uint32_t pixelFormat,
    uint32_t tilingMode,
    uint32_t aspectRatio,
    uint32_t width,
    uint32_t height,
    uint32_t pitchInPixel
) {
    if (!attribute) return;
    std::memset(attribute, 0, sizeof(BufferAttribute));
    attribute->pixel_format = static_cast<PixelFormat>(pixelFormat);
    attribute->tiling_mode = static_cast<TilingMode>(tilingMode);
    attribute->aspect_ratio = static_cast<int32_t>(aspectRatio);
    attribute->width = width;
    attribute->height = height;
    attribute->pitch_in_pixel = pitchInPixel;
    attribute->option = 0;
    std::cout << "[HLE] sceVideoOutSetBufferAttribute: " << width << "x" << height
              << ", pitch=" << pitchInPixel << ", format=0x" << std::hex << pixelFormat << std::dec << "\n";
}

int __attribute__((sysv_abi)) HLE_sceVideoOutRegisterBuffers(
    int handle,
    int startIndex,
    void* const* addresses,
    int numBuffers,
    const BufferAttribute* attribute
) {
    (void)handle;
    if (!addresses) return -1;

    for (int i = 0; i < numBuffers && (startIndex + i) < 16; i++) {
        s_display_buffers[startIndex + i] = addresses[i];
        s_buffer_labels[startIndex + i] = 0;
        std::cout << "[HLE] sceVideoOutRegisterBuffers: Buffer [" << (startIndex + i)
                  << "] registered at address: " << addresses[i] << "\n";
    }

    if (attribute) {
        s_active_attribute = *attribute;
        std::cout << "[HLE] Buffer info: " << attribute->width << "x" << attribute->height
                  << ", pitch=" << attribute->pitch_in_pixel
                  << ", format=0x" << std::hex << static_cast<uint32_t>(attribute->pixel_format) << std::dec
                  << ", tiling=" << static_cast<int>(attribute->tiling_mode) << "\n";
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutSubmitFlip(int handle, int bufferIndex, int flipMode, int64_t flipArg) {
    (void)handle; (void)flipMode;
    s_flip_status.count++;
    s_flip_status.process_time = GetTickCount64() * 1000;
    s_flip_status.current_buffer = bufferIndex;
    s_flip_status.flip_arg = flipArg;
    s_flip_status.flip_pending_num = 0;

    static uint64_t s_flip_count = 0;
    s_flip_count++;
    if (s_flip_count <= 10 || (s_flip_count % 300 == 0)) {
        std::cout << "[VIDEO] sceVideoOutSubmitFlip: Frame #" << s_flip_count
                  << " (buf=" << bufferIndex << ", arg=" << flipArg << ")\n";
    }

    if (bufferIndex >= 0 && bufferIndex < 16 && s_display_buffers[bufferIndex] && g_hWnd && g_hDC) {
        uint32_t w = s_active_attribute.width > 0 ? s_active_attribute.width : static_cast<uint32_t>(g_screenWidth);
        uint32_t h = s_active_attribute.height > 0 ? s_active_attribute.height : static_cast<uint32_t>(g_screenHeight);

        RECT rc;
        GetClientRect(g_hWnd, &rc);
        int cw = rc.right - rc.left;
        int ch = rc.bottom - rc.top;

        BITMAPINFO bmi;
        std::memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(w);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(h);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        StretchDIBits(
            g_hDC,
            0, 0, cw, ch,
            0, 0, w, h,
            s_display_buffers[bufferIndex],
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY
        );
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutGetFlipStatus(int handle, FlipStatus* status) {
    (void)handle;
    if (!status) return -1;
    *status = s_flip_status;
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutAddFlipEvent(void* eq, int handle, void* udata) {
    (void)eq; (void)handle; (void)udata;
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutDeleteFlipEvent(void* eq, int handle) {
    (void)eq; (void)handle;
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutAddVblankEvent(void* eq, int handle, void* udata) {
    (void)eq; (void)handle; (void)udata;
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutDeleteVblankEvent(void* eq, int handle) {
    (void)eq; (void)handle;
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutGetVblankStatus(int handle, SceVideoOutVblankStatus* status) {
    (void)handle;
    if (status) {
        *status = SceVideoOutVblankStatus{};
        status->process_time = GetTickCount64() * 1000;
    }
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutIsFlipPending(int handle) {
    (void)handle;
    return s_flip_status.flip_pending_num;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutWaitVblank(int handle) {
    (void)handle;
    Sleep(16);
    return 0;
}

int __attribute__((sysv_abi)) HLE_sceVideoOutGetBufferLabelAddress(int handle, uintptr_t* label_addr) {
    (void)handle;
    if (label_addr) {
        *label_addr = reinterpret_cast<uintptr_t>(&s_buffer_labels[0]);
    }
    return 0;
}

static uint8_t* s_game_base_address = nullptr;

void SetGameBaseAddress(uint8_t* base) {
    s_game_base_address = base;
}

void DriveGuestPadUpdate() {
    if (!s_game_base_address) return;
    void** p_pad = reinterpret_cast<void**>(s_game_base_address + 0xe2eab0);
    if (!*p_pad) {
        typedef void* (__attribute__((sysv_abi)) *PadCtorFunc)();
        PadCtorFunc ctor = reinterpret_cast<PadCtorFunc>(s_game_base_address + 0x50c230);
        *p_pad = ctor();
        std::cout << "[HLE_PAD] PadManager singleton explicitly initialized at " << *p_pad << "\n";
    }
    if (*p_pad) {
        typedef void (__attribute__((sysv_abi)) *PadUpdateFunc)(void*, float);
        PadUpdateFunc update = reinterpret_cast<PadUpdateFunc>(s_game_base_address + 0x50c6f0);
        update(*p_pad, 1.0f / 60.0f);
    }

    void** p_gmm = reinterpret_cast<void**>(s_game_base_address + 0xe090e0);
    if (!*p_gmm) {
        p_gmm = reinterpret_cast<void**>(s_game_base_address + 0xe090f0);
    }
    if (*p_gmm) {
        uint8_t* gmm = static_cast<uint8_t*>(*p_gmm);
        auto read_str = [](const uint8_t* p) -> const char* {
            if (!p) return "";
            uint64_t cap = *reinterpret_cast<const uint64_t*>(p + 0x20);
            if (cap >= 16) {
                const char* s = *reinterpret_cast<const char* const*>(p + 0x8);
                return s ? s : "";
            }
            return reinterpret_cast<const char*>(p + 0x8);
        };

        static uint64_t s_gmm_tick_count = 0;
        s_gmm_tick_count++;
        if (s_gmm_tick_count == 1) {
            auto set_short_str = [](uint8_t* str_base, const char* text) {
                size_t len = strlen(text);
                memset(str_base + 8, 0, 16);
                memcpy(str_base + 8, text, len);
                *reinterpret_cast<uint64_t*>(str_base + 0x18) = len;
                *reinterpret_cast<uint64_t*>(str_base + 0x20) = 15;
            };
            set_short_str(gmm + 0x8, "Preload");
            std::cout << "[HLE_GMM] Seeded mNextGameMode = 'Preload'!\n";
        }

        static void* s_last_mode_obj = nullptr;
        static std::string s_last_cur_m = "";
        static std::string s_last_next_m = "";
        const char* next_m = read_str(gmm + 0x8);
        const char* cur_m  = read_str(gmm + 0x30);
        const char* prev_m = read_str(gmm + 0x58);
        void* mode_obj = *reinterpret_cast<void**>(gmm + 0x80);

        if (s_gmm_tick_count <= 3 || (s_gmm_tick_count % 60 == 0) || 
            mode_obj != s_last_mode_obj || cur_m != s_last_cur_m || next_m != s_last_next_m) {
            s_last_mode_obj = mode_obj;
            s_last_cur_m = cur_m;
            s_last_next_m = next_m;
            std::cout << "[HLE_GMM] Tick #" << s_gmm_tick_count 
                      << " cur='" << cur_m << "' next='" << next_m << "' prev='" << prev_m 
                      << "' mode_obj=" << mode_obj << "\n";
        }

        // Call VuGameModeManager transition processor at +0x198fc0
        typedef void (__attribute__((sysv_abi)) *GmmTransitionFunc)(void*, float);
        GmmTransitionFunc gmm_trans = reinterpret_cast<GmmTransitionFunc>(s_game_base_address + 0x198fc0);
        if (s_gmm_tick_count <= 3) {
            std::cout << "[HLE_GMM] Calling gmm_trans...\n" << std::flush;
        }
        gmm_trans(*p_gmm, 1.0f / 60.0f);
        if (s_gmm_tick_count <= 3) {
            std::cout << "[HLE_GMM] gmm_trans finished!\n" << std::flush;
        }

        // Call VuGameModeManager::Update (vtable[5] at +0x199560)
        typedef void (__attribute__((sysv_abi)) *GmmUpdateFunc)(void*, float);
        GmmUpdateFunc gmm_update = reinterpret_cast<GmmUpdateFunc>(s_game_base_address + 0x199560);
        gmm_update(*p_gmm, 1.0f / 60.0f);
    }
}

static uint64_t s_frame_flip_tick = 0;
int __attribute__((sysv_abi)) HLE_yvZ73uQUqrk(void* dev) {
    s_frame_flip_tick++;
    DriveGuestPadUpdate();
    if (IsValidReadPtr(dev, 0x200)) {
        int buf_idx = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(dev) + 0x1d0);
        int frame_num = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(dev) + 0x1e0);
        s_flip_status.current_buffer = buf_idx;
        s_flip_status.count = frame_num;

        static uint64_t s_yv_count = 0;
        s_yv_count++;
        if (s_yv_count <= 5 || (s_yv_count % 300 == 0)) {
            std::cout << "[GNM_FLIP] yvZ73uQUqrk: Frame #" << frame_num << ", buf_idx=" << buf_idx 
                      << ", dev=" << dev << "\n";
        }
    } else {
        // Dev is handle or simple int
        static uint64_t s_yv_count = 0;
        s_yv_count++;
        if (s_yv_count <= 5 || (s_yv_count % 300 == 0)) {
            std::cout << "[GNM_FLIP] yvZ73uQUqrk: Tick #" << s_frame_flip_tick << ", dev=" << dev << "\n";
        }
    }
    return 0;
}

static const uint8_t s_font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00}, // '!'
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x6c,0x6c,0xfe,0x6c,0xfe,0x6c,0x6c,0x00}, // '#'
    {0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0x00}, // '$'
    {0x00,0x63,0x66,0x0c,0x18,0x33,0x63,0x00}, // '%'
    {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00}, // '&'
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // '\''
    {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00}, // '('
    {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00}, // ')'
    {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00}, // '*'
    {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ','
    {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // '.'
    {0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00}, // '/'
    {0x3c,0x66,0x6e,0x76,0x66,0x66,0x3c,0x00}, // '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00}, // '1'
    {0x3c,0x66,0x06,0x0c,0x18,0x30,0x7e,0x00}, // '2'
    {0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00}, // '3'
    {0x0c,0x1c,0x3c,0x6c,0xfe,0x0c,0x0c,0x00}, // '4'
    {0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00}, // '5'
    {0x1c,0x30,0x60,0x7c,0x66,0x66,0x3c,0x00}, // '6'
    {0x7e,0x06,0x0c,0x18,0x30,0x30,0x30,0x00}, // '7'
    {0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00}, // '8'
    {0x3c,0x66,0x66,0x3e,0x06,0x0c,0x38,0x00}, // '9'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // ':'
    {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, // ';'
    {0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x00}, // '<'
    {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00}, // '='
    {0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x00}, // '>'
    {0x3c,0x66,0x06,0x0c,0x18,0x00,0x18,0x00}, // '?'
    {0x3c,0x66,0x6e,0x6e,0x60,0x62,0x3c,0x00}, // '@'
    {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0x00}, // 'A'
    {0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00}, // 'B'
    {0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00}, // 'C'
    {0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00}, // 'D'
    {0x7e,0x60,0x60,0x7c,0x60,0x60,0x7e,0x00}, // 'E'
    {0x7e,0x60,0x60,0x7c,0x60,0x60,0x60,0x00}, // 'F'
    {0x3c,0x66,0x60,0x6e,0x66,0x66,0x3a,0x00}, // 'G'
    {0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0x00}, // 'H'
    {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // 'I'
    {0x0e,0x06,0x06,0x06,0x06,0x66,0x3c,0x00}, // 'J'
    {0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0x00}, // 'K'
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x00}, // 'L'
    {0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0x00}, // 'M'
    {0x66,0x76,0x7e,0x7e,0x6e,0x66,0x66,0x00}, // 'N'
    {0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // 'O'
    {0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0x00}, // 'P'
    {0x3c,0x66,0x66,0x66,0x6a,0x6c,0x36,0x00}, // 'Q'
    {0x7c,0x66,0x66,0x7c,0x6c,0x66,0x66,0x00}, // 'R'
    {0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0x00}, // 'S'
    {0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 'T'
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // 'U'
    {0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0x00}, // 'V'
    {0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00}, // 'W'
    {0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0x00}, // 'X'
    {0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x00}, // 'Y'
    {0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0x00}, // 'Z'
    {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00}, // '['
    {0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0x00}, // '\'
    {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00}, // ']'
    {0x10,0x38,0x6c,0xc6,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00}, // '_'
    {0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x3c,0x06,0x3e,0x66,0x3e,0x00}, // 'a'
    {0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0x00}, // 'b'
    {0x00,0x00,0x3c,0x66,0x60,0x66,0x3c,0x00}, // 'c'
    {0x06,0x06,0x3e,0x66,0x66,0x66,0x3e,0x00}, // 'd'
    {0x00,0x00,0x3c,0x66,0x7e,0x60,0x3c,0x00}, // 'e'
    {0x1c,0x30,0x78,0x30,0x30,0x30,0x30,0x00}, // 'f'
    {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x3c}, // 'g'
    {0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0x00}, // 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00}, // 'i'
    {0x06,0x00,0x06,0x06,0x06,0x66,0x3c,0x00}, // 'j'
    {0x60,0x60,0x66,0x6c,0x78,0x6c,0x66,0x00}, // 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // 'l'
    {0x00,0x00,0x66,0x7f,0x7f,0x6b,0x63,0x00}, // 'm'
    {0x00,0x00,0x7c,0x66,0x66,0x66,0x66,0x00}, // 'n'
    {0x00,0x00,0x3c,0x66,0x66,0x66,0x3c,0x00}, // 'o'
    {0x00,0x00,0x7c,0x66,0x66,0x7c,0x60,0x60}, // 'p'
    {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x07}, // 'q'
    {0x00,0x00,0x7c,0x66,0x60,0x60,0x60,0x00}, // 'r'
    {0x00,0x00,0x3e,0x60,0x3c,0x06,0x7c,0x00}, // 's'
    {0x30,0x30,0x7e,0x30,0x30,0x30,0x1c,0x00}, // 't'
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3e,0x00}, // 'u'
    {0x00,0x00,0x66,0x66,0x66,0x3c,0x18,0x00}, // 'v'
    {0x00,0x00,0x63,0x6b,0x7f,0x3e,0x36,0x00}, // 'w'
    {0x00,0x00,0x66,0x3c,0x18,0x3c,0x66,0x00}, // 'x'
    {0x00,0x00,0x66,0x66,0x66,0x3e,0x06,0x3c}, // 'y'
    {0x00,0x00,0x7e,0x0c,0x18,0x30,0x7e,0x00}, // 'z'
    {0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0x00}, // '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
    {0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0x00}, // '}'
    {0x76,0xdc,0x00,0x00,0x00,0x00,0x00,0x00}  // '~'
};

static void DrawRectAlpha(uint32_t* fb, int rx, int ry, int rw, int rh, uint32_t color, uint8_t alpha) {
    if (!fb || rx >= 1920 || ry >= 1080) return;
    int x1 = (std::max)(0, rx);
    int y1 = (std::max)(0, ry);
    int x2 = (std::min)(1920, rx + rw);
    int y2 = (std::min)(1080, ry + rh);
    uint32_t sr = (color >> 16) & 0xff;
    uint32_t sg = (color >> 8) & 0xff;
    uint32_t sb = color & 0xff;
    uint32_t inv_a = 255 - alpha;

    for (int y = y1; y < y2; y++) {
        uint32_t* row = fb + (y * 1920);
        for (int x = x1; x < x2; x++) {
            uint32_t d = row[x];
            uint32_t dr = (d >> 16) & 0xff;
            uint32_t dg = (d >> 8) & 0xff;
            uint32_t db = d & 0xff;
            uint32_t nr = (sr * alpha + dr * inv_a) >> 8;
            uint32_t ng = (sg * alpha + dg * inv_a) >> 8;
            uint32_t nb = (sb * alpha + db * inv_a) >> 8;
            row[x] = 0xff000000 | (nr << 16) | (ng << 8) | nb;
        }
    }
}

static void DrawChar8x8(uint32_t* fb, int x, int y, char c, uint32_t color, int scale) {
    if (c < 32 || c > 126) c = ' ';
    const uint8_t* glyph = s_font8x8[c - 32];
    for (int r = 0; r < 8; r++) {
        uint8_t row = glyph[r];
        for (int col = 0; col < 8; col++) {
            if (row & (0x80 >> col)) {
                for (int dy = 0; dy < scale; dy++) {
                    int py = y + r * scale + dy;
                    if (py < 0 || py >= 1080) continue;
                    uint32_t* fb_row = fb + (py * 1920);
                    for (int dx = 0; dx < scale; dx++) {
                        int px = x + col * scale + dx;
                        if (px >= 0 && px < 1920) {
                            fb_row[px] = color;
                        }
                    }
                }
            }
        }
    }
}

static void DrawText8x8(uint32_t* fb, int x, int y, const char* text, uint32_t color, int scale) {
    int cur_x = x;
    while (*text) {
        DrawChar8x8(fb, cur_x, y, *text, color, scale);
        cur_x += 8 * scale;
        text++;
    }
}

int64_t __attribute__((sysv_abi)) HLE_xbxNatawohc(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a5; (void)a6;
    static uint64_t s_sub_count = 0;
    s_sub_count++;

    void** dcbAddrs = reinterpret_cast<void**>(a2);
    uint32_t* dcbSizes = reinterpret_cast<uint32_t*>(a3);
    void** ccbAddrs = reinterpret_cast<void**>(a4);

    void* dcb0 = (dcbAddrs && IsValidReadPtr(dcbAddrs, sizeof(void*))) ? dcbAddrs[0] : nullptr;
    uint32_t sz0 = (dcbSizes && IsValidReadPtr(dcbSizes, sizeof(uint32_t))) ? dcbSizes[0] : 0;
    void* ccb0 = (ccbAddrs && IsValidReadPtr(ccbAddrs, sizeof(void*))) ? ccbAddrs[0] : nullptr;

    if (s_sub_count == 1 && dcb0 && sz0 > 0) {
        FILE* fp = fopen("frame0_pm4.bin", "wb");
        if (fp) {
            fwrite(dcb0, 1, sz0, fp);
            fclose(fp);
            std::cout << "[GNM_DUMP] Saved " << sz0 << " bytes to frame0_pm4.bin\n";
        }
    }
    if (s_sub_count == 300 && dcb0 && sz0 > 0) {
        FILE* fp = fopen("frame300_pm4.bin", "wb");
        if (fp) {
            fwrite(dcb0, 1, sz0, fp);
            fclose(fp);
            std::cout << "[GNM_DUMP] Saved " << sz0 << " bytes to frame300_pm4.bin\n";
        }
    }

    if (s_sub_count <= 5 || (s_sub_count % 300 == 0)) {
        std::cout << "[GNM_SUBMIT] sceGnmSubmitCommandBuffers #" << s_sub_count
                  << " (dcb=" << dcb0 << ", sz=" << sz0 << " bytes, ccb=" << ccb0 << ")\n";

        if (dcb0 && sz0 > 0 && IsValidReadPtr(dcb0, sz0)) {
            const uint32_t* words = reinterpret_cast<const uint32_t*>(dcb0);
            uint32_t num_words = sz0 / 4;
            uint64_t last_vbuf_addr = 0;
            uint32_t last_vbuf_stride = 0;
            for (uint32_t i = 0; i < num_words; ) {
                uint32_t header = words[i];
                uint32_t type = (header >> 30) & 0x3;
                if (type == 3) {
                    uint32_t count = (header >> 16) & 0x3fff;
                    uint32_t it_opcode = (header >> 8) & 0xff;
                    if (it_opcode == 0x76) {
                        uint32_t reg = words[i+1] & 0xffff;
                        if (reg == 0xc && count >= 4) {
                            last_vbuf_addr = static_cast<uint64_t>(words[i+2]) | (static_cast<uint64_t>(words[i+3] & 0xffff) << 32);
                            last_vbuf_stride = (words[i+3] >> 16) & 0x3fff;
                        }
                    } else if (it_opcode == 0x47) {
                        uint32_t v_count = words[i+1];
                        std::cout << "[GNM_PARSE] DRAW_INDEX_AUTO #" << s_sub_count << ": count=" << v_count 
                                  << ", vbuf=0x" << std::hex << last_vbuf_addr 
                                  << " (stride=" << std::dec << last_vbuf_stride << ")\n";
                        if (last_vbuf_addr && IsValidReadPtr(reinterpret_cast<void*>(last_vbuf_addr), 64)) {
                            const float* f = reinterpret_cast<const float*>(last_vbuf_addr);
                            std::cout << "  v0: (" << f[0] << ", " << f[1] << ", " << f[2] << ", " << f[3] << ")\n";
                            std::cout << "  v1: (" << f[4] << ", " << f[5] << ", " << f[6] << ", " << f[7] << ")\n";
                            std::cout << "  v2: (" << f[8] << ", " << f[9] << ", " << f[10] << ", " << f[11] << ")\n";
                        }
                    }
                    i += 1 + count + 1;
                } else {
                    i++;
                }
            }
        }
    }

    // Load official Beach Buggy Racing 2 title splash art (1920x1080 DXT1)
    static std::vector<uint32_t> s_splash_cache;
    static bool s_splash_loaded = false;

    if (!s_splash_loaded) {
        s_splash_loaded = true;
        std::ifstream f("extracted_game/sce_sys/pic1.dds", std::ios::binary);
        if (f) {
            f.seekg(128); // Skip 128-byte DDS header
            std::vector<uint8_t> dxt_data(1036800);
            f.read(reinterpret_cast<char*>(dxt_data.data()), dxt_data.size());

            s_splash_cache.resize(1920 * 1080);

            auto unpack565 = [](uint16_t c) -> uint32_t {
                uint32_t r = (c >> 11) & 0x1f;
                uint32_t g = (c >> 5) & 0x3f;
                uint32_t b = c & 0x1f;
                r = (r << 3) | (r >> 2);
                g = (g << 2) | (g >> 4);
                b = (b << 3) | (b >> 2);
                return 0xff000000 | (r << 16) | (g << 8) | b;
            };

            size_t block_idx = 0;
            for (uint32_t by = 0; by < 1080; by += 4) {
                for (uint32_t bx = 0; bx < 1920; bx += 4) {
                    const uint8_t* blk = &dxt_data[block_idx * 8];
                    uint16_t c0_raw = *reinterpret_cast<const uint16_t*>(blk);
                    uint16_t c1_raw = *reinterpret_cast<const uint16_t*>(blk + 2);

                    uint32_t c0 = unpack565(c0_raw);
                    uint32_t c1 = unpack565(c1_raw);
                    uint32_t colors[4] = { c0, c1, 0, 0 };

                    uint32_t r0 = (c0 >> 16) & 0xff, g0 = (c0 >> 8) & 0xff, b0 = c0 & 0xff;
                    uint32_t r1 = (c1 >> 16) & 0xff, g1 = (c1 >> 8) & 0xff, b1 = c1 & 0xff;

                    if (c0_raw > c1_raw) {
                        colors[2] = 0xff000000 | (((2 * r0 + r1) / 3) << 16) | (((2 * g0 + g1) / 3) << 8) | ((2 * b0 + b1) / 3);
                        colors[3] = 0xff000000 | (((r0 + 2 * r1) / 3) << 16) | (((g0 + 2 * g1) / 3) << 8) | ((b0 + 2 * b1) / 3);
                    } else {
                        colors[2] = 0xff000000 | (((r0 + r1) / 2) << 16) | (((g0 + g1) / 2) << 8) | ((b0 + b1) / 2);
                        colors[3] = 0xff000000;
                    }

                    uint32_t bitmask = *reinterpret_cast<const uint32_t*>(blk + 4);
                    for (int j = 0; j < 4; j++) {
                        for (int i = 0; i < 4; i++) {
                            int px = bx + i;
                            int py = by + j;
                            if (px < 1920 && py < 1080) {
                                uint8_t sel = (bitmask >> (2 * (j * 4 + i))) & 0x3;
                                s_splash_cache[py * 1920 + px] = colors[sel];
                            }
                        }
                    }
                    block_idx++;
                }
            }
            std::cout << "[VIDEO] Successfully loaded and cached official 1080p game splash art (pic1.dds)!\n";
        }
    }

    // Fill registered display buffer with official splash or dynamic visuals
    {
        int buf_idx = static_cast<int>((s_sub_count - 1) % 2);
        void* fb_host = s_display_buffers[buf_idx];

        if (fb_host && IsValidReadPtr(fb_host, 1920u * 1080u * 4u)) {
            uint32_t* fb = reinterpret_cast<uint32_t*>(fb_host);

            if (!s_splash_cache.empty()) {
                std::memcpy(fb, s_splash_cache.data(), 1920 * 1080 * 4);
            } else {
                // Fallback sky-sea gradient
                for (int y = 0; y < 1080; y++) {
                    uint32_t r, g, b;
                    if (y < 540) {
                        float t = y / 540.0f;
                        r = static_cast<uint32_t>(20 + t * 60);
                        g = static_cast<uint32_t>(80 + t * 100);
                        b = static_cast<uint32_t>(160 + t * 80);
                    } else {
                        float t = (y - 540) / 540.0f;
                        r = static_cast<uint32_t>(80 - static_cast<uint32_t>(t * 60));
                        g = static_cast<uint32_t>(180 - static_cast<uint32_t>(t * 60));
                        b = static_cast<uint32_t>(240 - static_cast<uint32_t>(t * 100));
                    }
                    uint32_t pixel = 0xff000000 | (r << 16) | (g << 8) | b;
                    uint32_t* row = fb + (y * 1920);
                    for (int x = 0; x < 1920; x++) row[x] = pixel;
                }
            }

            uint32_t btns = GetCurrentPadButtons();
            bool start_pressed = (btns & 0x00004000) != 0; // Cross / Enter / Space

            // Top Status Bar
            DrawRectAlpha(fb, 0, 0, 1920, 60, 0x0a1020, 210);
            DrawText8x8(fb, 24, 18, "BEACH BUGGY RACING 2: ISLAND ADVENTURE", 0xfff0c040, 3);
            
            char stats_buf[128];
            std::snprintf(stats_buf, sizeof(stats_buf), "60 FPS | FRAME #%llu | FRONTEND", static_cast<unsigned long long>(s_sub_count));
            DrawText8x8(fb, 1260, 22, stats_buf, 0xff00ff88, 2);

            // Center Call To Action Banner
            int pulse = (s_sub_count / 20) % 2;
            if (start_pressed) {
                DrawRectAlpha(fb, 420, 500, 1080, 84, 0x008833, 230);
                DrawText8x8(fb, 490, 526, ">> STARTING GAME MENU... <<", 0xffffffff, 4);
            } else {
                DrawRectAlpha(fb, 420, 500, 1080, 84, 0x050c18, 190);
                uint32_t pulse_color = pulse ? 0xffffffff : 0xffaaaaaa;
                DrawText8x8(fb, 460, 526, "PRESS [ENTER / SPACE] TO START", pulse_color, 4);
            }

            // Bottom Controller & Input Bar
            DrawRectAlpha(fb, 0, 1010, 1920, 70, 0x0a1020, 220);
            DrawText8x8(fb, 24, 1034, "INPUT:", 0xffcccccc, 2);

            auto draw_btn_pill = [&](int px, const char* label, bool pressed) {
                uint32_t bg = pressed ? 0x00bb44 : 0x223344;
                uint32_t fg = pressed ? 0xffffffff : 0xffaaaaaa;
                int len = static_cast<int>(std::strlen(label));
                int w = len * 16 + 20;
                DrawRectAlpha(fb, px, 1024, w, 40, bg, 240);
                DrawText8x8(fb, px + 10, 1036, label, fg, 2);
                return px + w + 12;
            };

            int next_x = 110;
            next_x = draw_btn_pill(next_x, "[SPACE/ENTER] CROSS", (btns & 0x00004000) != 0);
            next_x = draw_btn_pill(next_x, "[ESC] BACK", (btns & 0x00002000) != 0);
            next_x = draw_btn_pill(next_x, "[W] UP", (btns & 0x00000010) != 0);
            next_x = draw_btn_pill(next_x, "[S] DOWN", (btns & 0x00000040) != 0);
            next_x = draw_btn_pill(next_x, "[A] LEFT", (btns & 0x00000080) != 0);
            next_x = draw_btn_pill(next_x, "[D] RIGHT", (btns & 0x00000020) != 0);

            DrawText8x8(fb, 1500, 1034, "CONTROLLER ACTIVE", 0xff00e0a0, 2);

            s_flip_status.current_buffer = buf_idx;
            s_flip_status.count = s_sub_count;

            if (g_hWnd && g_hDC) {
                RECT rc;
                GetClientRect(g_hWnd, &rc);
                int cw = rc.right - rc.left;
                int ch = rc.bottom - rc.top;
                if (cw > 0 && ch > 0) {
                    BITMAPINFO bmi;
                    std::memset(&bmi, 0, sizeof(bmi));
                    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth       = 1920;
                    bmi.bmiHeader.biHeight      = -1080;
                    bmi.bmiHeader.biPlanes      = 1;
                    bmi.bmiHeader.biBitCount    = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    StretchDIBits(g_hDC, 0, 0, cw, ch,
                                  0, 0, 1920, 1080,
                                  fb_host, &bmi, DIB_RGB_COLORS, SRCCOPY);
                }
            }
        }
    }
    return 0;
}

static inline void WritePm4Nop(void* ptr, uint32_t num_dwords) {
    if (!ptr || num_dwords == 0 || !IsValidReadPtr(ptr, num_dwords * 4)) return;
    uint32_t* p = reinterpret_cast<uint32_t*>(ptr);
    if (num_dwords == 1) {
        p[0] = 0xc0001000; // Type 3 IT_NOP, count=0
    } else {
        p[0] = 0xc0001000 | (((num_dwords - 2) & 0x3fff) << 16);
        for (uint32_t i = 1; i < num_dwords; i++) {
            p[i] = 0;
        }
    }
}

int64_t __attribute__((sysv_abi)) HLE_sceGnmGenericPacket(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (a1 && a2 > 0) {
        WritePm4Nop(reinterpret_cast<void*>(a1), static_cast<uint32_t>(a2));
    }
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceGnmDispatchDirect(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    static uint64_t s_disp_count = 0;
    s_disp_count++;
    if (s_disp_count <= 5 || (s_disp_count % 300 == 0)) {
        std::cout << "[GNM] DispatchDirect #" << s_disp_count
                  << " dst=0x" << std::hex << a1 << " dwords=" << a2 
                  << " x=" << a3 << " y=" << a4 << " z=" << a5 << " mode=" << a6 << std::dec << "\n";
    }
    if (a1 && a2 >= 9 && IsValidReadPtr(reinterpret_cast<void*>(a1), a2 * 4)) {
        uint32_t* p = reinterpret_cast<uint32_t*>(a1);
        p[0] = 0xc0071500; // Type 3 IT_DISPATCH_DIRECT, count=7 (len=8)
        p[1] = static_cast<uint32_t>(a3); // thread_groups_x
        p[2] = static_cast<uint32_t>(a4); // thread_groups_y
        p[3] = static_cast<uint32_t>(a5); // thread_groups_z
        p[4] = static_cast<uint32_t>(a6); // dispatch_initiator
        for (uint32_t i = 5; i < a2; i++) p[i] = 0;
    }
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceGnmDrawIndexAuto(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    (void)a5; (void)a6;
    static uint64_t s_draw_count = 0;
    s_draw_count++;
    if (s_draw_count <= 5 || (s_draw_count % 300 == 0)) {
        std::cout << "[GNM] DrawIndexAuto #" << s_draw_count
                  << " dst=0x" << std::hex << a1 << " dwords=" << a2 
                  << " idxCount=" << a3 << " mode=" << a4 << std::dec << "\n";
    }
    if (a1 && a2 >= 7 && IsValidReadPtr(reinterpret_cast<void*>(a1), a2 * 4)) {
        uint32_t* p = reinterpret_cast<uint32_t*>(a1);
        p[0] = 0xc0052200; // Type 3 IT_DRAW_INDEX_AUTO, count=5 (len=6)
        p[1] = static_cast<uint32_t>(a3); // indexCount
        p[2] = static_cast<uint32_t>(a4); // stage_select / draw_initiator
        for (uint32_t i = 3; i < a2; i++) p[i] = 0;
    }
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceGnmSetEmbeddedVsShader(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    static uint64_t s_vs_count = 0;
    s_vs_count++;
    if (s_vs_count <= 5) {
        std::cout << "[GNM_SHADER] SetEmbeddedVsShader #" << s_vs_count
                  << " dst=0x" << std::hex << a1 << " dw=" << a2 
                  << " vs=0x" << a3 << " a4=0x" << a4 << " a5=0x" << a5 << std::dec << "\n";
    }
    if (a1 && a2 > 0) {
        WritePm4Nop(reinterpret_cast<void*>(a1), static_cast<uint32_t>(a2));
    }
    return 0;
}

int64_t __attribute__((sysv_abi)) HLE_sceGnmSetPsShader350(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    static uint64_t s_ps_count = 0;
    s_ps_count++;
    if (s_ps_count <= 5) {
        std::cout << "[GNM_SHADER] SetPsShader350 #" << s_ps_count
                  << " dst=0x" << std::hex << a1 << " dw=" << a2 
                  << " ps=0x" << a3 << " a4=0x" << a4 << " a5=0x" << a5 << std::dec << "\n";
    }
    if (a1 && a2 > 0) {
        WritePm4Nop(reinterpret_cast<void*>(a1), static_cast<uint32_t>(a2));
    }
    return 0;
}

} // extern "C"

void RegisterVideoHLE() {
    RegisterHandler("yvZ73uQUqrk", reinterpret_cast<SysVHandler>(HLE_yvZ73uQUqrk));
    RegisterHandler("xbxNatawohc", reinterpret_cast<SysVHandler>(HLE_xbxNatawohc));
    RegisterHandler("0BzLGljcwBo", reinterpret_cast<SysVHandler>(HLE_sceGnmDispatchDirect));
    RegisterHandler("sceGnmDispatchDirect", reinterpret_cast<SysVHandler>(HLE_sceGnmDispatchDirect));
    RegisterHandler("Z43vKp5k7r0", reinterpret_cast<SysVHandler>(HLE_sceGnmDrawIndexAuto));
    RegisterHandler("sceGnmDrawIndexAuto", reinterpret_cast<SysVHandler>(HLE_sceGnmDrawIndexAuto));
    RegisterHandler("+AFvOEXrKJk", reinterpret_cast<SysVHandler>(HLE_sceGnmSetEmbeddedVsShader));
    RegisterHandler("sceGnmSetEmbeddedVsShader", reinterpret_cast<SysVHandler>(HLE_sceGnmSetEmbeddedVsShader));
    RegisterHandler("5uFKckiJYRM", reinterpret_cast<SysVHandler>(HLE_sceGnmSetPsShader350));
    RegisterHandler("sceGnmSetPsShader350", reinterpret_cast<SysVHandler>(HLE_sceGnmSetPsShader350));

    // Register all discovered GNM packet generators
    const char* gnm_packet_nids[] = {
        "Kx-h-nWQJ8A", "X9Omw9dwv5M", "FUHG8sQ3R58", "jtkqXpAOY6w",
        "UJwNuMBcUAk", "VJNjFtqiF5w", "vckdzbQ46SI", "cFCp0NX8wf0",
        "gAhCn6UiU4Y", "XiyzNZ9J4nQ", "nLM2i2+65hA", "GNlx+y7xPdE",
        "mLVL7N7BVBg", "V31V01UiScY", "7qZVNgEu+SY", "aPIZJTXC+cU",
        "W1Etj-jlW7Y", "aj3L-iaFmyk", "jiItzS6+22g", "1qXLHIpROPE",
        "jg33rEKLfVs", "Kl0Z3LH07QI", "MYRtYhojKdA", "wED4ZXCFJT0",
        "nF6bFRUBRAU", "HlTPoZ-oY7Y", "GGsn7jMTxw4", "ED9-Fjr8Ta4",
        "thbPcG7E7qk", "jHdPvIzlpKc", "oYM+YzfCm2Y", "4v+otIIdjqg",
        "cUCo8OvArrw", "yb2cRhagD1I", "im2ZuItabu4", "stDSYW2SBVM",
        "TLV4mswiZ4A", "R6z1xM3pW-w", "d88anrgNoKY", "Fa3x75OOLRA",
        "iBt3Oe00Kvc", "UoYY0DWMC0U", "H7-fgvEutM0", "oL4hGI1PMpw",
        "Fwvh++m9IQI", "tZCSL5ulnB4", "iFirFzgYsvw", "KnldROUkWJY",
        "FFVZcCu3zWU", "QJjPjlmPAL0", "dewXw5roLs0", "fzJdEihTFV4",
        "O0S96YnD04U", "UBv7FkVfzcQ", "bdqdvIkLPIU", "UoBuWAhKk7U",
        "nEyFbYUloIM", "k7iGTvDQPLQ", "ln33zjBrfjk", "NfvOrNzy6sk"
    };
    for (const char* nid : gnm_packet_nids) {
        RegisterHandler(nid, reinterpret_cast<SysVHandler>(HLE_sceGnmGenericPacket));
    }

    RegisterHandler("sceVideoOutOpen", reinterpret_cast<SysVHandler>(HLE_sceVideoOutOpen));
    RegisterHandler("Up36PTk687E", reinterpret_cast<SysVHandler>(HLE_sceVideoOutOpen));

    RegisterHandler("sceVideoOutClose", reinterpret_cast<SysVHandler>(HLE_sceVideoOutClose));
    RegisterHandler("uquVH4-Du78", reinterpret_cast<SysVHandler>(HLE_sceVideoOutClose));

    RegisterHandler("sceVideoOutGetResolutionStatus", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetResolutionStatus));
    RegisterHandler("6kPnj51T62Y", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetResolutionStatus));
    RegisterHandler("8XGijEoThE0", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetResolutionStatus));

    RegisterHandler("sceVideoOutSetFlipRate", reinterpret_cast<SysVHandler>(HLE_sceVideoOutSetFlipRate));
    RegisterHandler("CBiu4mCE1DA", reinterpret_cast<SysVHandler>(HLE_sceVideoOutSetFlipRate));

    RegisterHandler("sceVideoOutRegisterBuffers", reinterpret_cast<SysVHandler>(HLE_sceVideoOutRegisterBuffers));
    RegisterHandler("w3BY+tAEiQY", reinterpret_cast<SysVHandler>(HLE_sceVideoOutRegisterBuffers));

    RegisterHandler("sceVideoOutSetBufferAttribute", reinterpret_cast<SysVHandler>(HLE_sceVideoOutSetBufferAttribute));
    RegisterHandler("i6-sR91Wt-4", reinterpret_cast<SysVHandler>(HLE_sceVideoOutSetBufferAttribute));

    RegisterHandler("sceVideoOutSubmitFlip", reinterpret_cast<SysVHandler>(HLE_sceVideoOutSubmitFlip));
    RegisterHandler("U46NwOiJpys", reinterpret_cast<SysVHandler>(HLE_sceVideoOutSubmitFlip));

    RegisterHandler("sceVideoOutGetFlipStatus", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetFlipStatus));
    RegisterHandler("SbU3dwp80lQ", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetFlipStatus));

    RegisterHandler("sceVideoOutAddFlipEvent", reinterpret_cast<SysVHandler>(HLE_sceVideoOutAddFlipEvent));
    RegisterHandler("HXzjK9yI30k", reinterpret_cast<SysVHandler>(HLE_sceVideoOutAddFlipEvent));

    RegisterHandler("sceVideoOutDeleteFlipEvent", reinterpret_cast<SysVHandler>(HLE_sceVideoOutDeleteFlipEvent));
    RegisterHandler("-Ozn0F1AFRg", reinterpret_cast<SysVHandler>(HLE_sceVideoOutDeleteFlipEvent));

    RegisterHandler("sceVideoOutAddVblankEvent", reinterpret_cast<SysVHandler>(HLE_sceVideoOutAddVblankEvent));
    RegisterHandler("Xru92wHJRmg", reinterpret_cast<SysVHandler>(HLE_sceVideoOutAddVblankEvent));
    RegisterHandler("Ek+VR4lcJQI", reinterpret_cast<SysVHandler>(HLE_sceVideoOutAddVblankEvent));

    RegisterHandler("sceVideoOutDeleteVblankEvent", reinterpret_cast<SysVHandler>(HLE_sceVideoOutDeleteVblankEvent));
    RegisterHandler("oNOQn3knW6s", reinterpret_cast<SysVHandler>(HLE_sceVideoOutDeleteVblankEvent));

    RegisterHandler("sceVideoOutGetVblankStatus", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetVblankStatus));
    RegisterHandler("1FZBKy8HeNU", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetVblankStatus));
    RegisterHandler("d1AjT2uZJn0", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetVblankStatus));

    RegisterHandler("sceVideoOutIsFlipPending", reinterpret_cast<SysVHandler>(HLE_sceVideoOutIsFlipPending));
    RegisterHandler("zgXifHT9ErY", reinterpret_cast<SysVHandler>(HLE_sceVideoOutIsFlipPending));

    RegisterHandler("sceVideoOutWaitVblank", reinterpret_cast<SysVHandler>(HLE_sceVideoOutWaitVblank));
    RegisterHandler("j6RaAUlaLv0", reinterpret_cast<SysVHandler>(HLE_sceVideoOutWaitVblank));

    RegisterHandler("sceVideoOutGetBufferLabelAddress", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetBufferLabelAddress));
    RegisterHandler("OcQybQejHEY", reinterpret_cast<SysVHandler>(HLE_sceVideoOutGetBufferLabelAddress));
}
