#pragma once

#include "elf.h"
#include <string>
#include <cstdint>

typedef int64_t (__attribute__((sysv_abi)) *SysVHandler)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

bool InitResolver(LoadedModule& module);
bool ApplyRelocations(LoadedModule& module);
void RegisterHandler(const std::string& name, SysVHandler handler);
bool IsValidReadPtr(const void* ptr, size_t size);
uint64_t* GetDummyVtable();
extern "C" int64_t __attribute__((sysv_abi)) DummyVMethod();
