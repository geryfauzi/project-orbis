# Project Orbis: PS4 to PC Native Conversion Knowledge Base

> **Target Audience:** AI Coding Assistants, Reverse Engineers, and Systems Programmers.  
> **Scope:** Architecture, methodology, debugging protocols, binary patching strategies, and runtime packaging for running Sony PlayStation 4 (Orbis OS) software on Windows x86-64.

---

## 1. Executive Summary & Core Philosophy

### 1.1 The x86-64 Native Execution Paradigm
Traditional console emulation (e.g., PS3 Cell, PS2 Emotion Engine) requires heavy CPU interpreters or dynamic binary recompilation (JIT). 
The PlayStation 4 runs on an **AMD Jaguar x86-64** processor. Host PC hardware (Intel Core / AMD Ryzen) also utilizes the **x86-64** architecture.

Therefore:
* **Zero CPU Emulation:** Game machine code (`eboot.bin`) is executed directly by the host CPU without an interpreter or JIT recompiler.
* **Compatibility / Translation Layer (Proton / Wine Model):** Just as Valve's Proton runs Windows games on Linux by translating Win32 calls and DirectX to Vulkan, the PS4 runtime's responsibility is translating platform-specific services:
  1. **OS Calls:** Orbis OS (FreeBSD-based) kernel syscalls $\rightarrow$ Windows Win32 API.
  2. **Graphics:** Sony GNM / GNMX low-level AMD GCN command packets (PM4) $\rightarrow$ Vulkan 1.3.
  3. **Audio:** Sony audio pipelines & PRX modules $\rightarrow$ OpenAL / SDL2.

### 1.2 Architecture Division of Project Orbis
* **Runtime Layer (`runtime/shadPS4.exe`):** Production-grade translation layer handling Vulkan GPU command processing, swapchain presentation, memory paging, and audio routing.
* **Toolkit & RE Layer (`tools/`, `pkgextract_tool/`):** Custom diagnostics (`dump_gamemain_stack`, `read_live_globals`), automated binary patchers (`patch_subsystem_jumps`), PKG extractors, and build tools.
* **Launcher Layer (`templates/launcher.cpp`):** Standalone Win32 GUI wrappers compiled with portable GCC to distribute games cleanly without terminal windows.
* **Experimental Lab (`/src`):** An educational, low-level prototype exploring custom ELF64 segment loaders, NID symbol resolution, and SYSV ABI invocation directly on Win32. *(Note: Not used for full 3D graphics playback, as full GNM PM4 $\rightarrow$ Vulkan translation requires the complete runtime).*

---

## 2. Real-World Case Studies

### 2.1 Case Study 1: Beach Buggy Racing 2 (Vector Unit Engine)
* **Engine:** In-house `VuEngine`.
* **The "Blue Canvas" Trap:**
  * Game booted, created a window, and played audio, but rendered only a solid blue clear color (`#194882`).
  * Disassembly of engine initialization (`0x9ea0` - `0xd035`) revealed sequential checks for 84 console-specific subsystems (telemetry, PSN matchmaking, party voice, online storage).
  * Every failed check branched via conditional jump (`0F 84 disp32` / `JE 0xd035`) straight to the engine teardown sequence, setting `VuGfx` to `NULL` and aborting 3D pipeline creation.
* **The Fix:**
  * Created `tools/patch_subsystem_jumps.cpp` to overwrite all 84 failure jumps with 6-byte NOP sequences (`0x90 0x90 0x90 0x90 0x90 0x90`).
  * Result: Engine ignored unmocked online services and successfully entered 60 FPS 3D racing.
* **Audio SFX Restoration:**
  * PS4 8-channel (7.1) audio routes explosions and heavy SFX to the **LFE (Subwoofer)** channel.
  * Standard stereo dropped the LFE channel under SDL2.
  * Setting `"audio_backend": 1` (OpenAL) in `%APPDATA%\shadps4\config.json` enabled active stereo folding (downmixing LFE into front left/right channels).

### 2.2 Case Study 2: Crash Team Racing Nitro-Fueled (Beenox IG Engine)
* **Title ID:** `CUSA13795` (v1.21).
* **Engine:** Beenox Internal Graphics Engine (`chunks.igx`, `.pak`, `.bk2`, `.igz`).
* **Trap A: Multi-Part Update Overwrite Loss:**
  * `pkgextract.exe -f` wipes the target directory when extracting.
  * Extracting an 8.5 GB Update PKG over an existing 13.5 GB Base Game directory with `-f` deleted base assets, causing immediate `0xC0000005` crash due to missing `loosefiles.pak` and `localizationData.igz`.
  * **Solution:** Extract Base Game into `game_base`, extract Update into `game_update`, then perform a non-destructive file overlay merge into the final game folder.
* **Trap B: Missing System PRX Libraries:**
  * The game engine dynamically loads `/app0/sce_module/libc.prx` and `/app0/sce_module/libSceFios2.prx`.
  * Without these PRX modules present in `sce_module/`, the game aborts at startup with `Assertion Failed! libc.prx cannot be loaded`.
  * **Solution:** Ensure `sce_module/` is populated with decrypted system PRXs (`libc.prx`, `libSceFios2.prx`, `libSceNpToolkit2.prx`).
* **Trap C: Shader Pipeline Stuttering on New Actions:**
  * **Symptom:** Smooth framerate interrupted by sudden micro-stutters when triggering new actions for the first time (e.g., pressing R2 to look back, emitting nitro flames, entering new track zones).
  * **Root Cause:** Real-time Vulkan Shader Compilation. The GPU driver compiles console SPIR-V/GCN shaders into host GPU machine code on-the-fly.
  * **Solution:**
    * In `%APPDATA%\shadps4\config.json`, enable:
      ```json
      "Vulkan": {
          "pipeline_cache_enabled": true,
          "pipeline_cache_archived": true,
          "vkvalidation_core_enabled": false
      }
      ```
    * Pipelines are cached persistently in `%APPDATA%\shadps4\cache\CUSA13795*.bin`. Once cached, all subsequent races run completely stutter-free.

---

## 3. The Orbis Protocol: Standard Conversion Workflow

```
[1. Unpack PKG (Base + Update Merge)] ──> [2. Sanity Check ELF / FSELF] ──> [3. PRX Module Audit]
                                                                                     │
                                                                            [4. Baseline Run]
                                                                                     │
                        ┌────────────────────────────────────────────────────────────┴──────────────────────────┐
                        ▼                                                                                       ▼
              [Clean Boot / In-game]                                                                   [Failure / Freeze]
                        │                                                                                       │
                        │                                                                             [5. Triage Failure Mode]
                        │                                                                                       │
                        │                                                      ┌────────────────────────────────┼────────────────────────────────┐
                        │                                                      ▼                                ▼                                ▼
                        │                                             [Crash (0xC0000005)]              [Loop / Freeze]                 [Empty Canvas]
                        │                                                      │                                │                                │
                        │                                            [Check PRX & Archives]          [Thread Stack Walk]             [Engine Subsystem Bypass]
                        │                                                      │                                │                                │
                        └──────────────────────────────────────────────────────┴────────────────────────────────┴────────────────────────────────┘
                                                                                       │
                                                                       [6. Audio & Pipeline Cache Tuning]
                                                                                       │
                                                                       [7. Native GUI Packaging]
```

### Step 1: Package Unpacking & Multi-Part Merging
1. Unpack Base Game:
   ```cmd
   pkgextract_tool\pkgextract.exe -o "target_folder" "base_game.pkg"
   ```
2. For Updates/Patches: Unpack to a temporary folder, then copy/overlay all files over `target_folder` to preserve base archives.

### Step 2: ELF Binary Verification
Read the first 4 bytes of `eboot.bin`:
* `7F 45 4C 46` (`\x7fELF`): Standard decrypted ELF64.
* `4F 15 3D 1D` (`0x1D3D154F`): PS4 Signed ELF / Fake SELF (FSELF). Supported natively by the runtime.
* `53 43 45 00` (`SCE\0`): Encrypted retail SELF container; must be decrypted before execution.

### Step 3: Module Audit
Ensure `target_folder\sce_module\` contains required dynamic libraries:
* `libc.prx`
* `libSceFios2.prx`

### Step 4: Baseline Execution & Log Analysis
Run game via runtime:
```cmd
runtime\shadPS4.exe -g "target_folder\eboot.bin" -f false
```
Inspect `%APPDATA%\shadps4\log\shadps4.log`.

### Step 5: Triage Failure Modes
* **Failure Mode A: 0xC0000005 (Access Violation):** Missing loose archives or unhandled NID. Check log for missing `.pak` / `.prx`.
* **Failure Mode B: Thread Deadlock:** Use `tools\dump_gamemain_stack.exe <PID>` to inspect RIP across all guest threads.
* **Failure Mode C: Empty Canvas (Subsystem Abort):** Game boots to solid color with audio. Scan initialization disassembly for `0F 84` (JE) abort loops and apply NOP patching.

### Step 6: Audio & Shader Cache Tuning
In `%APPDATA%\shadps4\config.json`:
* `"audio_backend": 1` $\rightarrow$ OpenAL active 8ch $\rightarrow$ 2ch LFE folding.
* `"pipeline_cache_enabled": true` $\rightarrow$ Persistent disk caching in `%APPDATA%\shadps4\cache\`.
* `"pipeline_cache_archived": true` $\rightarrow$ Compressed pipeline archives.
* `"vkvalidation_core_enabled": false` $\rightarrow$ Removes debug validation overhead.

### Step 7: Native GUI Packaging
1. Configure `kGameTitle`, relative runtime path, and relative eboot path in `templates/launcher.cpp`.
2. Compile standalone `.exe` using portable toolchain:
   ```cmd
   tools\w64devkit\bin\g++.exe -O2 -mwindows templates\launcher.cpp -o "GameTitle.exe"
   ```
3. Place `GameTitle.exe` in the root folder alongside `game\` and `runtime\`.

---

## 4. Tooling Reference Table

| Tool Path | Scope | Purpose |
| :--- | :--- | :--- |
| `pkgextract_tool/pkgextract.exe` | Generic | Unpacks PS4 PKG archives to directories. |
| `tools/dump_gamemain_stack.cpp` | Generic | Inspects guest thread RIP, RSP, and call return addresses live in RAM. |
| `tools/test_printwindow.cpp` | Generic | Captures window framebuffer to `.bmp` without window focus. |
| `tools/w64devkit/` | Generic | Portable standalone GCC/G++ C++20 compiler toolchain. |
| `templates/launcher.cpp` | Generic | Clean Win32 GUI wrapper template for one-click launching. |
| `tools/patch_subsystem_jumps.cpp` | Game-Specific (BBR2) | Overwrites 84 abort jumps in Vector Unit engine with NOP. |
| `tools/read_live_globals.cpp` | Game-Specific (BBR2) | Inspects VuEngine global pointers in RAM. |

---

## 5. Guidelines for Future AI Coding Assistants

1. **Be Honest About the Architecture:** Never claim game binaries are converted into standalone native Windows binaries without a translation layer. The runtime layer (`shadPS4`) handles GPU translation (GNM $\rightarrow$ Vulkan) and OS syscalls.
2. **Game-Specific Patches are Not Universal:** `patch_subsystem_jumps.cpp` only applies to BBR2. Different engines (e.g. Beenox) have completely different memory layouts and failure modes.
3. **Always Back Up `eboot.bin`:** Create `eboot.bin.bak` before making opcode modifications.
4. **Never Commit Large Binaries or Game Data:** Respect `.gitignore`. Do not commit PKG files, game dumps, or extracted archives.
5. **Always Enable Pipeline Caching:** To prevent user complaints about stuttering during initial gameplay, ensure `pipeline_cache_enabled` is set to `true`.
