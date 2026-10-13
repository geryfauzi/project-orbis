#include <windows.h>
#include <string>

// Configuration: Game Title and Executable Name
static const char* kGameTitle = "Crash Team Racing Nitro-Fueled";
static const char* kRuntimeRelativePath = "\\runtime\\shadPS4.exe";
static const char* kEbootRelativePath = "\\game\\eboot.bin";

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir = exePath;
    size_t lastSlash = dir.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        dir = dir.substr(0, lastSlash);
    }

    std::string shadExe = dir + kRuntimeRelativePath;
    std::string ebootPath = dir + kEbootRelativePath;
    std::string cmd = "\"" + shadExe + "\" -g \"" + ebootPath + "\" -f false";

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.lpDesktop = (char*)"WinSta0\\default";

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessA(
        NULL,
        (LPSTR)cmd.c_str(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        dir.c_str(),
        &si,
        &pi
    );

    if (!success) {
        si.lpDesktop = NULL;
        success = CreateProcessA(
            NULL,
            (LPSTR)cmd.c_str(),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            dir.c_str(),
            &si,
            &pi
        );
    }

    if (!success) {
        MessageBoxA(NULL, "Gagal meluncurkan runtime Crash Team Racing!", kGameTitle, MB_ICONERROR | MB_OK);
        return 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
