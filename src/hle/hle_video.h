#pragma once
#include <cstdint>

extern "C" {
void RegisterVideoHLE();
void EnsureDisplayWindow();
void SetGameBaseAddress(uint8_t* base);
uint8_t* GetModuleBase();
}

