# Project Orbis: PS4 Native Execution & Conversion Toolkit

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x86--64-lightgrey.svg)](https://microsoft.com/windows)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**Project Orbis** adalah toolkit rekayasa balik (*reverse engineering*) dan runtime eksekusi native untuk menjalankan game PlayStation 4 (Orbis OS) di PC Windows secara langsung tanpa emulasi CPU (konsep serupa Steam Proton di Linux).

---

## 🎯 Mengapa Native Execution?

* **Arsitektur CPU Identik:** PS4 dan PC modern sama-sama menggunakan prosesor **x86-64**. Instruksi mesin dieksekusi secara langsung oleh CPU host tanpa overhead *interpreter* atau *JIT recompiler*.
* **Vulkan Translation Layer:** Panggilan grafis PlayStation (GNM / GNMX) diterjemahkan secara langsung ke Vulkan modern, menghasilkan performa rendering 60 FPS terkunci dengan latensi minimal.
* **OpenAL Audio Downmix:** Penanganan surround 7.1 dengan pelipatan channel LFE (*Low-Frequency Effects*) ke stereo agar efek suara ledakan/SFX tetap menggelegar di speaker PC.

---

## 📁 Struktur Repositori

```text
project-orbis/
├── CMakeLists.txt              # Konfigurasi build lintas compiler (CMake 3.20+)
├── build.bat                   # Skrip build cepat menggunakan toolchain bawaan
├── .gitignore                  # Filter aset game & biner untuk menjaga repo tetap bersih
│
├── src/                        # Source Code Custom ELF Loader & HLE
│   ├── core/                   # ELF parser, segment loader, relocation & symbol resolver
│   ├── hle/                    # Emulasi syscall kernel Sony, video GNM, audio, dan pad
│   └── main.cpp                # Titik masuk native runner
│
├── tools/                      # Toolchain Rekayasa Balik & Patching
│   ├── patch_subsystem_jumps.cpp # Tool pemindai & pem-bypass 84 abort jumps engine game
│   ├── dump_gamemain_stack.cpp   # Tool stack walk & inspect thread CPU saat crash/freeze
│   ├── read_live_globals.cpp     # Tool inspect variabel global memori RAM secara live
│   ├── test_printwindow.cpp      # Tool penangkap frame visual buffer aktif
│   └── w64devkit/                # Compiler C++ portable (GCC/G++ untuk Windows)
│
├── pkgextract_tool/            # Tool Ekstraksi Paket Game
│   └── pkgextract.exe          # CLI untuk mengekstrak file game .pkg
│
├── templates/                  # Template Launcher Game
│   └── launcher.cpp            # Template launcher GUI C++ tanpa jendela command prompt
│
└── docs/                       # Dokumentasi Teknis
    └── conversion_guide.md     # Panduan langkah demi langkah konversi game PS4 baru
```

---

## 🚀 Memulai (Quick Start)

### 1. Prasyarat
* Windows 10/11 x86-64
* GPU dengan dukungan Vulkan 1.3 (NVIDIA, AMD, atau Intel)
* Kompiler C++20 (bisa menggunakan `tools/w64devkit/bin/g++.exe` bawaan atau Visual Studio 2022)

### 2. Kompilasi Runner
Untuk mengompilasi custom native runner:
```cmd
build.bat
```
Atau menggunakan CMake:
```cmd
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

---

## 🛠️ Alur Konversi Game PS4 Baru

Untuk mengonversi dan menjalankan game PS4 baru:

1. **Ekstraksi Paket Game:**
   ```cmd
   pkgextract_tool\pkgextract.exe "path\ke\game.pkg" "extracted_game"
   ```

2. **Jalankan via Runtime:**
   ```cmd
   runtime\shadPS4.exe -g "extracted_game\eboot.bin" -f false
   ```

3. **Bypass Pengecekan Konsol (Jika Muncul Canvas Kosong / Freeze):**
   * Gunakan `tools\dump_gamemain_stack.exe` untuk melihat thread yang macet.
   * Gunakan `tools\patch_subsystem_jumps.cpp` untuk men-NOP conditional jumps yang membatalkan inisialisasi renderer grafis 3D.
   * Baca panduan selengkapnya di [docs/conversion_guide.md](docs/conversion_guide.md).

4. **Buat GUI Launcher:**
   Sesuaikan `templates/launcher.cpp` dan kompilasi:
   ```cmd
   tools\w64devkit\bin\g++.exe -O2 -mwindows templates\launcher.cpp -o "NamaGame.exe"
   ```

---

## 📜 Kebijakan Legalitas
Repositori ini **TIDAK** memuat aset game berhak cipta, file biner game (*eboot.bin* asli), maupun kunci enkripsi Sony. Repositori ini murni berisi perkakas open-source untuk rekayasa perangkat lunak dan riset interoperabilitas platform x86-64.
