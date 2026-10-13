# PS4 Native Core ELF Loader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a native C++ ELF loader on Windows x64 that loads decrypted PS4 `eboot.bin` (Beach Buggy Racing 2), binds its 626 imported NID symbols to host stubs, and executes the game's entry point natively.

**Architecture:** Following `bbhost` architecture, the loader acts as a native translation runtime. It allocates virtual memory for PS4 `PT_LOAD` segments, parses `PT_DYNAMIC` and `PT_SCE_DYNLIBDATA`, patches relocations, and transitions from Windows x64 ABI to FreeBSD System V AMD64 ABI before jumping to the game's entry point.

**Tech Stack:** C++20, Clang / MinGW-w64 (`sysv_abi` calling convention support), Win32 API (`VirtualAlloc`, `VirtualProtect`).

**Spec:** Native HLE runner for Beach Buggy Racing 2 (`CUSA26789`) matching Proton / `bbhost` specification.

## Global Constraints
- Target platform: Windows 10/11 x86-64.
- Guest binary: `extracted_game/eboot.bin` (ELF at offset `0x160`, entry point `0x2d58c0`).
- Calling convention: Guest code expects System V AMD64 ABI; host code bridges via `__attribute__((sysv_abi))` or assembly thunk.
- Minimalist engineering (Ponytail): No heavyweight third-party frameworks. Standard Win32 virtual memory and C++ stdlib.

## Review Focus
- Memory allocation failure or overlapping segments if base slide is not properly aligned.
- Calling convention mismatch corrupting stack or register state upon entry.
- TLS (Thread Local Storage) register (`fs` / `gs`) access if guest code attempts to dereference TLS before initialization.
- Undefined NID symbol crashing the process on first call rather than triggering a graceful stub fallback.
- Relocation offsets pointing outside mapped memory boundaries.

---

### Task 1: Environment & Toolchain Setup

**Files:**
- Create: `tools/build_check.cpp`
- Create: `build.bat`

- [x] Step 1: Write a minimal test `tools/build_check.cpp` verifying `__attribute__((sysv_abi))` and 64-bit compilation.
- [x] Step 2: Download or configure a portable toolchain (w64devkit or Clang) in `tools/` that supports `sysv_abi`.
- [x] Step 3: Run compilation check and verify output passes cleanly.

### Task 2: ELF Loader & Virtual Memory Mapping

**Files:**
- Create: `src/core/elf.h`
- Create: `src/core/elf.cpp`
- Create: `tests/test_loader.cpp`

- [x] Step 1: Define ELF64 header structs (`Elf64_Ehdr`, `Elf64_Phdr`, `Elf64_Dyn`, `Elf64_Rela`, `Elf64_Sym`).
- [x] Step 2: Implement `LoadOrbisElf(const char* path)` to parse FSELF segments and locate ELF header at `0x160`.
- [x] Step 3: Allocate contiguous guest virtual memory using `VirtualAlloc` and copy `PT_LOAD` segments.
- [x] Step 4: Write unit test in `tests/test_loader.cpp` verifying segment sizes, memory addresses, and permissions.

### Task 3: NID Symbol Resolver & Relocations

**Files:**
- Create: `src/core/nid_table.h`
- Create: `src/core/resolver.h`
- Create: `src/core/resolver.cpp`

- [x] Step 1: Generate master NID lookup table mapping 626 Beach Buggy NIDs to symbol names.
- [x] Step 2: Implement generic stub dispatcher (`CommonStub`) that logs called function name/NID and returns 0 (`SCE_OK`).
- [x] Step 3: Parse `PT_DYNAMIC` relocation entries (`R_X86_64_JUMP_SLOT`, `R_X86_64_GLOB_DAT`, `R_X86_64_RELATIVE`, `R_X86_64_64`) and patch function pointers to point to host stubs.
- [x] Step 4: Test relocation resolver against `extracted_game/eboot.bin` to ensure 0 unresolved mandatory relocations.

### Task 4: SysV Entry Point Execution & Bootstrap Test

**Files:**
- Create: `src/main.cpp`
- Modify: `build.bat`

- [x] Step 1: Implement `RunGuestEntry` assembly thunk aligning stack to 16-byte boundary and passing initial parameters.
- [x] Step 2: Initialize basic `libkernel` and `libc` stubs (memory allocation, thread self ID).
- [x] Step 3: Build the executable runner `bb_runner.exe`.
- [x] Step 4: Run `bb_runner.exe` and capture execution log confirming successful jump into game code and initial stub calls.
