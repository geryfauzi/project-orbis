# Project Orbis: PS4 to PC Native Conversion Knowledge Base

> **Target Audience:** AI Coding Assistants, Reverse Engineers, and Systems Programmers.
> **Scope:** Architecture, methodology, debugging protocols, and binary patching strategies for running Sony PlayStation 4 (Orbis OS) software natively on Windows x86-64.

---

## 1. Executive Summary & Core Philosophy

### 1.1 The x86-64 Native Execution Paradigm
Traditional console emulation (e.g., PS3 Cell, PS2 Emotion Engine) requires heavy CPU interpreters or dynamic binary recompilation (JIT). 
The PlayStation 4, however, runs on an **AMD Jaguar x86-64** processor. Host PC hardware (Intel Core / AMD Ryzen) also utilizes the **x86-64** architecture.

Therefore:
* **Zero CPU Emulation:** Game machine code (`eboot.bin`) is executed directly by the host CPU.
* **Compatibility Layer (Proton / Wine Model):** The runtime's only responsibility is translating platform-specific services:
  1. **OS Calls:** Orbis OS (FreeBSD-based) kernel syscalls $\rightarrow$ Windows Win32 API.
  2. **Graphics:** Sony GNM / GNMX low-level AMD GCN command packets (PM4) $\rightarrow$ Vulkan 1.3.
  3. **Audio:** Sony audio pipelines & PRX modules $\rightarrow$ OpenAL / SDL2.

---

## 2. Case Study: The Breakthrough in Beach Buggy Racing 2

### 2.1 The Problem: The "Blue Canvas" Trap
* **Symptom:** The game booted, initialized a window, and produced audio, but the display remained frozen on a solid blue clear color (`#194882`). No 3D models, UI, or text ever rendered.
* **Initial False Hypothesis:** A Vulkan GPU shader bug or broken PM4 command buffer submission.
* **Actual Root Cause Discovered via Memory & Disassembly Analysis:**
  * Engine: Vector Unit in-house engine (`VuEngine`).
  * Function: Engine initialization routine at `0x9ea0` - `0xd035` in `eboot.bin`.
  * The engine sequentially initializes 84 distinct subsystems (leaderboards, telemetry, PSN matchmaking, party voice, online storage, etc.).
  * On PC, unhandled or mocked Sony services return `0` (`false`).
  * In the game binary, **every single failure check contained a conditional jump (`0F 84 disp32` / `JE 0xd035`) directly to the cleanup/teardown sequence**.
  * When any subsystem failed, the engine jumped to `0xd035`, destroyed initialized subsystems, set the 3D graphics manager pointer (`VuGfx`) to `NULL` (`0x0`), and aborted creation of the rendering pipeline.

### 2.2 The Solution: NOPing the Abort Jumps
By creating an automated patcher (`tools/patch_subsystem_jumps.cpp`), all 84 conditional failure jumps (`0F 84 ?? ?? ?? ??`) targeting `0xd035` were overwritten with 6-byte `NOP` sequences (`0x90 0x90 0x90 0x90 0x90 0x90`).
* **Result:** The engine gracefully ignored failed console-specific online/hardware subsystems, continued initialization, created `VuGfx`, compiled Vulkan shaders, and entered full 3D racing at locked 60 FPS.

### 2.3 Audio SFX Restoration: LFE Stereo Folding
* **Symptom:** Background music was loud, but in-game sound effects (explosions, boosts, crashes) were barely audible.
* **Root Cause:** PS4 outputs an 8-channel (7.1 surround) sound stream. Heavy SFX and explosions are routed to the **LFE (Low Frequency Effects / Subwoofer)** channel. On standard 2-channel PC stereo setups, SDL2 dropped the unmapped LFE channel.
* **Fix:** Changing the audio backend in `%APPDATA%\shadps4\config.json` to OpenAL (`"audio_backend": 1`). OpenAL performs active downmixing by folding the LFE channel into the front stereo channels with appropriate gain scaling.

---

## 3. The Orbis Protocol: Step-by-Step Conversion Methodology for Any PS4 Game

When approaching a new PS4 title, follow this exact sequence:

```
[1. Unpack PKG] ──> [2. Sanity Check ELF] ──> [3. Baseline Run] 
                                                      │
                       ┌──────────────────────────────┴──────────────────────────────┐
                       ▼                                                             ▼
              [Clean Boot / In-game]                                        [Failure / Freeze]
                       │                                                             │
                       │                                                    [4. Triage Failure Mode]
                       │                                                             │
                       │                                      ┌──────────────────────┼──────────────────────┐
                       │                                      ▼                      ▼                      ▼
                       │                               [Crash (0xC0000005)]    [Loop / Freeze]       [Empty Canvas]
                       │                                      │                      │                      │
                       │                               [Fix NID / Stub]     [Dump Thread Stack]    [Subsystem Bypass]
                       │                                      │                      │                      │
                       └──────────────────────────────────────┴──────────────────────┴──────────────────────┘
                                                              │
                                                     [5. Audio / Input Tuning]
                                                              │
                                                     [6. Native Launcher Packaging]
```

### Step 1: Package Unpacking
Extract the retail or decrypted PS4 `.pkg` using the CLI extractor:
```cmd
pkgextract_tool\pkgextract.exe "path\to\target_game.pkg" "output_folder"
```
Verify the contents:
* `eboot.bin`: Main executable (must be decrypted ELF64).
* `sce_module/`: Dynamic libraries (`.prx`).
* Game archives (e.g., `.apf`, `.pak`, `.cpk`).

### Step 2: ELF Binary Verification
Check the first 4 bytes of `eboot.bin`:
* Valid decrypted ELF: `7F 45 4C 46` (`\x7fELF`).
* If header starts with `53 43 45` (`SCE`): The binary is encrypted with Sony SELF headers and must be decrypted on a jailbroken console or via decryption tools before execution.

### Step 3: Baseline Execution
Run the game through the translation runtime with logging enabled:
```cmd
runtime\shadPS4.exe -g "output_folder\eboot.bin" -f false
```

### Step 4: Failure Mode Diagnosis & Remediation

#### Failure Mode A: Immediate Access Violation (`0xC0000005`)
* **Cause:** Game called a Sony NID function that returned a `NULL` pointer or an unhandled struct.
* **Remediation:** Inspect `shadps4.log`. Identify missing NIDs (11-character hash strings). Implement stubs returning `0` (SCE_OK) or allocating dummy structures.

#### Failure Mode B: Infinite Hang / Thread Deadlock
* **Cause:** Worker thread waiting for a semaphore, event flag, or network response that never fires.
* **Remediation:** Attach `tools\dump_gamemain_stack.exe` to the running process PID:
  1. Inspect the Instruction Pointer (`RIP`) of all guest threads.
  2. Locate the function in disassembly.
  3. Patch the polling loop (`TEST EAX, EAX; JZ wait`) to proceed immediately.

#### Failure Mode C: Empty Colored Canvas / Black Screen with Audio (The Subsystem Abort)
* **Cause:** Engine initialization sequence aborted due to optional console service checks.
* **Remediation:**
  1. Inspect the main thread stack during the freeze using `dump_gamemain_stack.exe`.
  2. Disassemble the initialization function around the current call site.
  3. Look for cascades of `0F 84` (JE) or `0F 85` (JNE) leading to a common teardown block.
  4. Use or adapt `tools\patch_subsystem_jumps.cpp` to overwrite conditional failure jumps with `NOP` (`0x90`).

### Step 5: Audio & Control Tuning
* Open `%APPDATA%\shadps4\config.json`.
* Set `"audio_backend": 1` (OpenAL) to ensure 7.1 $\rightarrow$ 2.0 LFE folding.
* Connect any XInput, DualShock 4, or DualSense controller (shadPS4 maps SDL controllers to `ScePad` natively).

### Step 6: Native GUI Packaging
1. Open `templates/launcher.cpp`.
2. Configure `kGameTitle` and relative paths.
3. Compile using the included standalone toolchain:
   ```cmd
   tools\w64devkit\bin\g++.exe -O2 -mwindows templates\launcher.cpp -o "GameTitle.exe"
   ```
4. Place `GameTitle.exe` alongside the game folder for clean one-click launching without terminal windows.

---

## 4. Architecture of the Standalone Runner (`/src`)

The `/src` directory provides an independent native runner built from scratch without external runtime dependencies:

```
src/
├── core/
│   ├── elf.cpp / elf.h        # ELF64 parser, segment loader (VirtualAlloc RX/RW)
│   ├── nid_table.h            # Comprehensive dictionary of Sony PS4 NID hashes to symbols
│   └── resolver.cpp / .h      # Dynamic relocation processor (R_X86_64_JUMP_SLOT, GLOB_DAT)
├── hle/
│   ├── hle_kernel.cpp / .h    # Orbis kernel threading, mutexes, file I/O
│   ├── hle_libc.cpp / .h      # Standard C library runtime mappings
│   ├── hle_video.cpp / .h     # Windows Win32 window creation & GNM command intercepts
│   ├── hle_pad.cpp / .h       # XInput to ScePad conversion
│   └── hle_audio.cpp / .h     # FMOD Studio PRX interop stubs
└── main.cpp                   # Host entry point and execution transfer to guest RIP
```

* **Compilation:** Can be built instantly on any Windows PC via `build.bat` using `tools/w64devkit/bin/g++.exe` or via `CMakeLists.txt`.

---

## 5. Tooling Reference Table

| Tool Path | Purpose | When to Use |
| :--- | :--- | :--- |
| `pkgextract_tool/pkgextract.exe` | PKG archive unpacker | Step 1 of any new game conversion. |
| `tools/patch_subsystem_jumps.cpp` | Automated NOP patcher for fail jumps | When game aborts in initialization loops (canvas bug). |
| `tools/dump_gamemain_stack.cpp` | Win32 stack-walk & register inspector | When game hangs, freezes, or hits deadlocks. |
| `tools/read_live_globals.cpp` | Guest RAM pointer & state inspector | Verifying if renderer / game state pointers are valid. |
| `tools/test_printwindow.cpp` | Framebuffer capture tool | Verifying rendering without needing window focus. |
| `tools/w64devkit/` | Standalone portable C++ toolchain | Compiling tools, launchers, or `/src` anywhere. |
| `templates/launcher.cpp` | C++ GUI launcher template | Final packaging step for a polished game release. |

---

## 6. Guidelines for Future AI Coding Assistants

When prompted to continue work on Project Orbis or convert a new game:
1. **Never Assume a GPU Bug First:** If audio plays and a window is open but the screen is empty, check engine initialization jumps first before attempting to debug shader translations.
2. **Preserve Clean Repositories:** Never commit game assets (`.apf`, `.pkg`, decrypted raw game files) or multi-hundred megabyte compiler binaries (`w64devkit`) to Git. Always respect `.gitignore`.
3. **Use the Portable Toolchain:** The host system may not have GCC/Clang in `PATH`. Always invoke `tools\w64devkit\bin\g++.exe` directly when compiling C++ code.
4. **Make Backups Before Patching:** Always maintain an untouched copy of `eboot.bin` (e.g. `eboot.bin.bak`) before modifying binary opcodes.
5. **Always Verify 7.1 LFE Audio:** If a user reports weak sound effects, inspect the audio downmix settings before looking for game asset issues.
