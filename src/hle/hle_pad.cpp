#include "hle_pad.h"
#include "../core/resolver.h"
#include <windows.h>
#include <xinput.h>
#include <iostream>
#include <cstring>
#include <cstdint>

enum ScePadButtonDataOffset {
    SCE_PAD_BUTTON_L3          = 0x00000002,
    SCE_PAD_BUTTON_R3          = 0x00000004,
    SCE_PAD_BUTTON_OPTIONS     = 0x00000008,
    SCE_PAD_BUTTON_UP          = 0x00000010,
    SCE_PAD_BUTTON_RIGHT       = 0x00000020,
    SCE_PAD_BUTTON_DOWN        = 0x00000040,
    SCE_PAD_BUTTON_LEFT        = 0x00000080,
    SCE_PAD_BUTTON_L2          = 0x00000100,
    SCE_PAD_BUTTON_R2          = 0x00000200,
    SCE_PAD_BUTTON_L1          = 0x00000400,
    SCE_PAD_BUTTON_R1          = 0x00000800,
    SCE_PAD_BUTTON_TRIANGLE    = 0x00001000,
    SCE_PAD_BUTTON_CIRCLE      = 0x00002000,
    SCE_PAD_BUTTON_CROSS       = 0x00004000,
    SCE_PAD_BUTTON_SQUARE      = 0x00008000,
    SCE_PAD_BUTTON_TOUCH_PAD   = 0x00100000,
    SCE_PAD_BUTTON_INTERCEPTED = 0x80000000,
};

struct ScePadData {
    uint32_t buttons;
    uint8_t leftStickX;
    uint8_t leftStickY;
    uint8_t rightStickX;
    uint8_t rightStickY;
    uint8_t analogButtonsL2;
    uint8_t analogButtonsR2;
    uint8_t padding[2];
    uint8_t orientation[16];
    uint8_t acceleration[12];
    uint8_t angularVelocity[12];
    uint8_t touchData[24];
    uint8_t connected;
    uint64_t timestamp;
    uint8_t extensionUnitData[16];
    uint8_t connectedCount;
    uint8_t reserve[7];
};

struct ScePadControllerInformation {
    float touchPadPixelDensity;
    uint16_t touchPadPixelWidth;
    uint16_t touchPadPixelHeight;
    uint8_t stickDeadzoneLeft;
    uint8_t stickDeadzoneRight;
    uint8_t connectionType;
    uint8_t connectedCount;
    uint8_t connected;
    uint8_t deviceClass;
    uint8_t reserve[8];
};

struct ScePadVibrationParam {
    uint8_t largeMotor;
    uint8_t smallMotor;
};

static uint32_t s_last_pad_buttons = 0;

extern "C" {

int __attribute__((sysv_abi)) HLE_scePadInit() {
    std::cout << "[HLE] scePadInit -> SCE_OK (XInput backend ready)\n";
    return 0;
}

int __attribute__((sysv_abi)) HLE_scePadOpen(int userId, int type, int index, const void* param) {
    (void)type; (void)index; (void)param;
    static bool s_logged_pad1 = false;
    if (!s_logged_pad1) {
        std::cout << "[HLE] scePadOpen: Primary controller opened for user 0x" << std::hex << userId << std::dec << "\n";
        s_logged_pad1 = true;
    }
    return 1;
}

int __attribute__((sysv_abi)) HLE_scePadClose(int handle) {
    (void)handle;
    return 0;
}

int __attribute__((sysv_abi)) HLE_scePadGetControllerInformation(int handle, ScePadControllerInformation* info) {
    (void)handle;
    if (!info) return -1;
    std::memset(info, 0, sizeof(ScePadControllerInformation));
    info->touchPadPixelDensity = 1.0f;
    info->touchPadPixelWidth = 1920;
    info->touchPadPixelHeight = 900;
    info->connected = 1;
    return 0;
}

int __attribute__((sysv_abi)) HLE_scePadReadState(int handle, ScePadData* data) {
    (void)handle;
    if (!data) return -1;

    std::memset(data, 0, sizeof(ScePadData));
    data->leftStickX = 128;
    data->leftStickY = 128;
    data->rightStickX = 128;
    data->rightStickY = 128;
    data->connected = 1;

    XINPUT_STATE xstate;
    std::memset(&xstate, 0, sizeof(xstate));
    DWORD dwResult = XInputGetState(0, &xstate);

    if (dwResult == ERROR_SUCCESS) {
        uint32_t btn = 0;
        WORD xb = xstate.Gamepad.wButtons;

        // Auto-mapping XInput to PS4 DualShock layout
        if (xb & XINPUT_GAMEPAD_A) btn |= SCE_PAD_BUTTON_CROSS;     // A -> X
        if (xb & XINPUT_GAMEPAD_B) btn |= SCE_PAD_BUTTON_CIRCLE;    // B -> Circle
        if (xb & XINPUT_GAMEPAD_X) btn |= SCE_PAD_BUTTON_SQUARE;    // X -> Square
        if (xb & XINPUT_GAMEPAD_Y) btn |= SCE_PAD_BUTTON_TRIANGLE;  // Y -> Triangle

        if (xb & XINPUT_GAMEPAD_DPAD_UP)    btn |= SCE_PAD_BUTTON_UP;
        if (xb & XINPUT_GAMEPAD_DPAD_RIGHT) btn |= SCE_PAD_BUTTON_RIGHT;
        if (xb & XINPUT_GAMEPAD_DPAD_DOWN)  btn |= SCE_PAD_BUTTON_DOWN;
        if (xb & XINPUT_GAMEPAD_DPAD_LEFT)  btn |= SCE_PAD_BUTTON_LEFT;

        if (xb & XINPUT_GAMEPAD_LEFT_SHOULDER)  btn |= SCE_PAD_BUTTON_L1;
        if (xb & XINPUT_GAMEPAD_RIGHT_SHOULDER) btn |= SCE_PAD_BUTTON_R1;

        if (xb & XINPUT_GAMEPAD_START) btn |= SCE_PAD_BUTTON_OPTIONS;
        if (xb & XINPUT_GAMEPAD_BACK)  btn |= SCE_PAD_BUTTON_TOUCH_PAD;

        if (xb & XINPUT_GAMEPAD_LEFT_THUMB)  btn |= SCE_PAD_BUTTON_L3;
        if (xb & XINPUT_GAMEPAD_RIGHT_THUMB) btn |= SCE_PAD_BUTTON_R3;

        if (xstate.Gamepad.bLeftTrigger > 30)  btn |= SCE_PAD_BUTTON_L2;
        if (xstate.Gamepad.bRightTrigger > 30) btn |= SCE_PAD_BUTTON_R2;

        data->buttons = btn;
        data->leftStickX = static_cast<uint8_t>((xstate.Gamepad.sThumbLX + 32768) >> 8);
        data->leftStickY = static_cast<uint8_t>((32767 - xstate.Gamepad.sThumbLY) >> 8);
        data->rightStickX = static_cast<uint8_t>((xstate.Gamepad.sThumbRX + 32768) >> 8);
        data->rightStickY = static_cast<uint8_t>((32767 - xstate.Gamepad.sThumbRY) >> 8);
        data->analogButtonsL2 = xstate.Gamepad.bLeftTrigger;
        data->analogButtonsR2 = xstate.Gamepad.bRightTrigger;
    }

    // Keyboard fallback mapping (PC Native)
    uint32_t kb_btn = 0;
    if (GetAsyncKeyState(VK_RETURN) & 0x8000 || GetAsyncKeyState(VK_SPACE) & 0x8000 || GetAsyncKeyState('Z') & 0x8000) {
        kb_btn |= SCE_PAD_BUTTON_CROSS;
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000 || GetAsyncKeyState(VK_BACK) & 0x8000 || GetAsyncKeyState('X') & 0x8000) {
        kb_btn |= SCE_PAD_BUTTON_CIRCLE;
    }
    if (GetAsyncKeyState('C') & 0x8000) kb_btn |= SCE_PAD_BUTTON_SQUARE;
    if (GetAsyncKeyState('V') & 0x8000) kb_btn |= SCE_PAD_BUTTON_TRIANGLE;
    if (GetAsyncKeyState(VK_UP) & 0x8000 || GetAsyncKeyState('W') & 0x8000) {
        kb_btn |= SCE_PAD_BUTTON_UP;
        data->leftStickY = 0;
    }
    if (GetAsyncKeyState(VK_DOWN) & 0x8000 || GetAsyncKeyState('S') & 0x8000) {
        kb_btn |= SCE_PAD_BUTTON_DOWN;
        data->leftStickY = 255;
    }
    if (GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000) {
        kb_btn |= SCE_PAD_BUTTON_LEFT;
        data->leftStickX = 0;
    }
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) {
        kb_btn |= SCE_PAD_BUTTON_RIGHT;
        data->leftStickX = 255;
    }
    if (GetAsyncKeyState(VK_TAB) & 0x8000) kb_btn |= SCE_PAD_BUTTON_OPTIONS;

    data->buttons |= kb_btn;

    static uint64_t s_pad_poll_count = 0;
    s_pad_poll_count++;

    // Auto-advance into FrontEnd menu after 3 seconds if untouched
    if (s_pad_poll_count >= 180 && s_pad_poll_count <= 210) {
        data->buttons |= SCE_PAD_BUTTON_CROSS;
    }

    s_last_pad_buttons = data->buttons;

    static uint32_t s_prev_btn = 0;
    if (data->buttons != s_prev_btn) {
        std::cout << "[INPUT_EVENT] Pad buttons changed: 0x" << std::hex << data->buttons 
                  << " (CROSS=" << ((data->buttons & SCE_PAD_BUTTON_CROSS) ? "1" : "0") << ")\n" << std::dec;
        s_prev_btn = data->buttons;
    }

    if (s_pad_poll_count == 1 || (s_pad_poll_count % 300 == 0)) {
        std::cout << "[HLE_PAD] scePadReadState #" << s_pad_poll_count 
                  << " (btn=0x" << std::hex << data->buttons << std::dec << ")\n";
    }

    return 0;
}

int __attribute__((sysv_abi)) HLE_scePadSetVibration(int handle, const ScePadVibrationParam* param) {
    (void)handle;
    if (param) {
        XINPUT_VIBRATION vib;
        vib.wLeftMotorSpeed = static_cast<WORD>(param->largeMotor * 257);
        vib.wRightMotorSpeed = static_cast<WORD>(param->smallMotor * 257);
        XInputSetState(0, &vib);
    }
    return 0;
}

} // extern "C"

uint32_t GetCurrentPadButtons() {
    uint32_t kb = 0;
    if (GetAsyncKeyState(VK_RETURN) & 0x8000 || GetAsyncKeyState(VK_SPACE) & 0x8000) kb |= SCE_PAD_BUTTON_CROSS;
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000 || GetAsyncKeyState(VK_BACK) & 0x8000)  kb |= SCE_PAD_BUTTON_CIRCLE;
    if (GetAsyncKeyState(VK_UP) & 0x8000 || GetAsyncKeyState('W') & 0x8000)          kb |= SCE_PAD_BUTTON_UP;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000 || GetAsyncKeyState('S') & 0x8000)        kb |= SCE_PAD_BUTTON_DOWN;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000)        kb |= SCE_PAD_BUTTON_LEFT;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000)       kb |= SCE_PAD_BUTTON_RIGHT;
    return s_last_pad_buttons | kb;
}

void RegisterPadHLE() {
    RegisterHandler("scePadInit", reinterpret_cast<SysVHandler>(HLE_scePadInit));
    RegisterHandler("hv1luiJrqQM", reinterpret_cast<SysVHandler>(HLE_scePadInit));

    RegisterHandler("scePadOpen", reinterpret_cast<SysVHandler>(HLE_scePadOpen));
    RegisterHandler("xk0AcarP3V4", reinterpret_cast<SysVHandler>(HLE_scePadOpen));

    RegisterHandler("scePadClose", reinterpret_cast<SysVHandler>(HLE_scePadClose));
    RegisterHandler("6ncge5+l5Qs", reinterpret_cast<SysVHandler>(HLE_scePadClose));

    RegisterHandler("scePadGetControllerInformation", reinterpret_cast<SysVHandler>(HLE_scePadGetControllerInformation));
    RegisterHandler("gjP9-KQzoUk", reinterpret_cast<SysVHandler>(HLE_scePadGetControllerInformation));

    RegisterHandler("scePadReadState", reinterpret_cast<SysVHandler>(HLE_scePadReadState));
    RegisterHandler("YndgXqQVV7c", reinterpret_cast<SysVHandler>(HLE_scePadReadState));

    RegisterHandler("scePadSetVibration", reinterpret_cast<SysVHandler>(HLE_scePadSetVibration));
    RegisterHandler("yFVnOdGxvZY", reinterpret_cast<SysVHandler>(HLE_scePadSetVibration));
}
