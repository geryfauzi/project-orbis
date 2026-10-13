#include "hle_audio.h"
#include "../core/resolver.h"
#include <iostream>
#include <cstdint>
#include <cstring>
#include <windows.h>

#include "../core/nid_table.h"

static uint8_t s_dummy_studio_system[4096];
static uint8_t s_dummy_core_system[4096];
static uint8_t s_dummy_fmod_object[4096];

extern "C" {

int64_t __attribute__((sysv_abi)) HLE_FMOD_Studio_System_Create(void** system, uint32_t version) {
    std::cout << "[HLE_AUDIO] FMOD_Studio_System_Create(version: 0x" << std::hex << version << std::dec << ")\n";
    if (system) {
        std::memset(s_dummy_studio_system, 0, sizeof(s_dummy_studio_system));
        *system = s_dummy_studio_system;
    }
    return 0; // FMOD_OK
}

int64_t __attribute__((sysv_abi)) HLE_FMOD_Studio_System_GetCoreSystem(void* studio, void** core) {
    (void)studio;
    std::cout << "[HLE_AUDIO] FMOD_Studio_System_GetCoreSystem\n";
    if (core) {
        std::memset(s_dummy_core_system, 0, sizeof(s_dummy_core_system));
        *core = s_dummy_core_system;
    }
    return 0; // FMOD_OK
}

int64_t __attribute__((sysv_abi)) HLE_FMOD_System_SetSoftwareFormat(void* core, int32_t a2, int32_t a3, int32_t a4) {
    (void)core; (void)a2; (void)a3; (void)a4;
    std::cout << "[HLE_AUDIO] FMOD_System_SetSoftwareFormat\n";
    return 0; // FMOD_OK
}

int64_t __attribute__((sysv_abi)) HLE_FMOD_GenericSuccess(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0; // FMOD_OK
}

} // extern "C"

void RegisterAudioHLE() {
    for (const auto& entry : g_NidTable) {
        std::string full(entry.full_symbol);
        if (full.find("#p#l") != std::string::npos || full.find("#q#m") != std::string::npos) {
            RegisterHandler(entry.nid, reinterpret_cast<SysVHandler>(HLE_FMOD_GenericSuccess));
            if (entry.name && entry.name[0] && entry.name != entry.nid) {
                RegisterHandler(entry.name, reinterpret_cast<SysVHandler>(HLE_FMOD_GenericSuccess));
            }
        }
    }

    // Specific overrides
    RegisterHandler("MjQ5oH6b620", reinterpret_cast<SysVHandler>(HLE_FMOD_Studio_System_Create));
    RegisterHandler("7hd4bRJuLMg", reinterpret_cast<SysVHandler>(HLE_FMOD_Studio_System_GetCoreSystem));
    RegisterHandler("S9bYVNGDosY", reinterpret_cast<SysVHandler>(HLE_FMOD_System_SetSoftwareFormat));
}
