# Restore Rendering Pipeline & VuGfx Synchronization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Menampilkan visual rendering 3D / UI di layar host PC dengan mengembalikan submit rendering di `0x178ab6` dan sinkronisasi worker di `VuGfx::render`, serta memvalidasi piksel framebuffer non-zero.

**Architecture:** Mengembalikan kode biner mesin game di `extracted_game/eboot.bin` ke kondisi asli (dari `eboot.bin.bak`) pada bagian submit grafis dan sinkronisasi thread agar frame yang digambar oleh GPU Vulkan RTX 5070 diteruskan dan disinkronkan ke swapchain presenter.

**Tech Stack:** C++ (MinGW GCC 16.2 w64devkit), Windows x64 ABI / SysV ABI, Vulkan (shadPS4 core), PowerShell.

**Spec:** Analisis runtime mendapati bahwa shader GPU (`0xfad97caf`, `0x9b2da5cf`, `0x21182b66`) sudah aktif, tetapi frame visual di-skip akibat bypass jump di `0x178ab6` dan `VuGfx::render` (`0x3e33b2`, `0x3e33e5`, `0x3e34f8`, `0x3e3818`).

## Global Constraints
- Target binary: `extracted_game/eboot.bin`
- Backup reference: `extracted_game/eboot.bin.bak`
- Host OS: Windows 11 x64, GPU: NVIDIA GeForce RTX 5070
- Aturan ponytail: Hanya ubah byte yang terbukti menjadi penyebab bug, verifikasi sebelum dan sesudah patch.

## Review Focus
- Memastikan tidak ada regresi crash atau deadlock di `pthread_cond_wait`.
- Memastikan log runtime shadPS4 tetap bersih dari fatal exception.
- Memastikan `analyze_bmp.exe` mendeteksi `non-zero pixels > 0`.

---

### Task 1: Revert Render Submission Bypass di `0x178ab6`

**Files:**
- Modify: `extracted_game/eboot.bin` di offset `0x178ab6` (12 bytes)
- Helper: `tools/apply_patch_render.cpp` -> `tools/apply_patch_render.exe`

- [ ] **Step 1: Buat tool C++ untuk mengembalikan 12 byte di `0x178ab6`**
  Mengembalikan byte `e9 b9 0a 00 90 90 90 90 90 90 90 90` ke nilai aslinya `80 b8 70 01 00 00 0f 84 b1 0a 00 00` (`cmpb $0x0, 0x170(%rax); je 0x179574`).
- [ ] **Step 2: Kompilasi dan eksekusi patch tool**
  Jalankan `apply_patch_render.exe` pada `extracted_game/eboot.bin`.
- [ ] **Step 3: Verifikasi byte via `print_bytes.exe`**
  Pastikan offset `0x178ab6` bernilai `80 b8 70 01 00 00 0f 84 b1 0a 00 00`.

---

### Task 2: Revert Sinkronisasi Thread di `VuGfx::render`

**Files:**
- Modify: `extracted_game/eboot.bin` di offset `0x3e33b2`, `0x3e33e5`, `0x3e34f8`, `0x3e3818`

- [ ] **Step 1: Buat tool C++ untuk mengembalikan 4 titik sinkronisasi `VuGfx`**
  - `0x3e33b2`: `90 90` -> `74 7e` (`je 0x3e3432`)
  - `0x3e33e5`: `90 90` -> `74 62` (`je 0x3e3449`)
  - `0x3e34f8`: `eb` -> `75` (`jne 0x3e3512`)
  - `0x3e3818`: `eb` -> `75` (`jne 0x3e3832`)
- [ ] **Step 2: Eksekusi pengembalian byte**
- [ ] **Step 3: Verifikasi diff menggunakan `diff_eboot.exe`**
  Pastikan keempat offset tersebut tidak lagi muncul di diff terhadap `eboot.bin.bak`.

---

### Task 3: Verifikasi Visual Framebuffer & Stabilitas Runtime

**Files:**
- Execute: `tools/test_monitor_load.ps1`
- Verify: `tools/screen_capture.bmp` melalui `tools/analyze_bmp.exe`

- [ ] **Step 1: Jalankan runner selama 15-20 detik dan pantau stdout**
  Pastikan `shadPS4.exe` tetap mengompilasi shader GPU dan tidak mengalami deadlock.
- [ ] **Step 2: Ambil screenshot window host dengan `capture_window.exe`**
- [ ] **Step 3: Analisis bitmap hasil capture dengan `analyze_bmp.exe`**
  Periksa apakah `non-zero pixels > 0` (muncul warna render grafis 3D / logo).
