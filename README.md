# Project Orbis: PS4 Native Execution & Conversion Toolkit

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x86--64-lightgrey.svg)](https://microsoft.com/windows)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**Project Orbis** adalah toolkit rekayasa balik (*reverse engineering*), diagnostik biner, dan pipeline packaging untuk menjalankan game PlayStation 4 (Orbis OS) di PC Windows secara native tanpa emulasi CPU (paradigma serupa Steam Proton di Linux).

---

## 🎯 Mengapa Native Execution?

* **Arsitektur CPU Identik:** PS4 dan PC sama-sama menggunakan prosesor **x86-64** (AMD Jaguar di konsol dan Intel Core / AMD Ryzen di PC). Instruksi mesin dieksekusi secara langsung oleh CPU host tanpa overhead *interpreter* atau *JIT recompiler*.
* **Vulkan Translation Layer:** Panggilan grafis PlayStation (GNM / GNMX PM4) diterjemahkan secara langsung ke Vulkan 1.3 modern, menghasilkan latensi minimal dan framerate terkunci.
* **Persistent Pipeline Caching:** Cache shader Vulkan disimpan secara persisten di disk untuk mencegah *shader stuttering* saat aksi atau efek baru dipicu pertama kali.
* **OpenAL Audio Downmix:** Penanganan surround 7.1 dengan pelipatan channel LFE (*Low-Frequency Effects / Subwoofer*) ke stereo agar efek suara ledakan/SFX tetap seimbang di speaker PC.

---

## 📁 Struktur Repositori

```text
project-orbis/
├── CMakeLists.txt              # Konfigurasi build lintas compiler (CMake 3.20+)
├── build.bat                   # Skrip build cepat menggunakan toolchain bawaan
├── .gitignore                  # Filter aset game & biner untuk menjaga repo tetap bersih
│
├── src/                        # [Eksperimental] Lab Riset Custom ELF Loader & HLE
│   ├── core/                   # ELF parser, segment loader, relocation & symbol resolver
│   ├── hle/                    # Riset syscall kernel Sony, video GNM, audio, dan pad
│   └── main.cpp                # Prototipe titik masuk native runner
│
├── runtime/                    # Production Runtime Compatibility Layer
│   └── shadPS4.exe             # Vulkan GNM translation layer & Orbis runtime
│
├── tools/                      # Toolchain Rekayasa Balik & Patching
│   ├── patch_subsystem_jumps.cpp # Tool bypass abort jumps (studi kasus Vector Unit Engine)
│   ├── dump_gamemain_stack.cpp   # Tool stack walk & inspect register thread CPU saat crash/freeze
│   ├── read_live_globals.cpp     # Tool inspect variabel global memori RAM secara live
│   ├── test_printwindow.cpp      # Tool penangkap frame visual buffer aktif
│   └── w64devkit/                # Compiler C++ portable (GCC/G++ untuk Windows)
│
├── pkgextract_tool/            # Tool Ekstraksi Paket Game
│   └── pkgextract.exe          # CLI untuk mengekstrak file game .pkg
│
├── templates/                  # Template Launcher Game
│   ├── launcher.cpp            # Template launcher GUI C++ tanpa jendela command prompt
│   └── launcher_ctr.cpp        # Launcher siap pakai untuk CTR Nitro-Fueled
│
└── docs/                       # Dokumentasi Teknis
    ├── knowledge.md            # Dokumentasi arsitektur, rekayasa balik & studi kasus
    └── conversion_guide.md     # Panduan langkah demi langkah konversi game PS4 baru
```

---

## 🎮 Studi Kasus Game yang Telah Berhasil Dikonversi

| Game | Engine | Status | Catatan Teknis Utama |
| :--- | :--- | :--- | :--- |
| **Beach Buggy Racing 2** | Vector Unit (`VuEngine`) | 100% Playable | Bypass 84 conditional abort jumps (`patch_subsystem_jumps.cpp`) & OpenAL LFE downmixing. |
| **Crash Team Racing Nitro-Fueled** | Beenox (`IG Engine`) | 100% Playable | Overlay merge Base + Update v1.21, injeksi PRX `libc`/`fios2`, dan Vulkan pipeline caching. |

---

## 🚀 Alur Konversi Game PS4 Baru

Untuk mengonversi dan menjalankan game PS4 baru:

1. **Ekstraksi Paket Game & Merge Update:**
   ```cmd
   pkgextract_tool\pkgextract.exe -o "game_base" "path\ke\base_game.pkg"
   pkgextract_tool\pkgextract.exe -o "game_update" "path\ke\update.pkg"
   ```
   *Salin seluruh isi `game_update` menimpa ke dalam `game_base` (jangan ekstrak update langsung dengan flag `-f` karena akan menghapus file dasar).*

2. **Audit Modul Sistem:**
   Pastikan folder `game\sce_module\` memiliki modul `libc.prx` dan `libSceFios2.prx`.

3. **Uji Coba via Runtime & Logging:**
   ```cmd
   runtime\shadPS4.exe -g "game\eboot.bin" -f false
   ```

4. **Triage & Patching (Jika Terjadi Freeze / Layar Kosong):**
   * Gunakan `tools\dump_gamemain_stack.exe <PID>` untuk melihat thread yang macet.
   * Baca panduan selengkapnya di [docs/conversion_guide.md](docs/conversion_guide.md).

5. **Optimasi Shader Cache (Anti-Stuttering):**
   Di `%APPDATA%\shadps4\config.json`, pastikan:
   * `"pipeline_cache_enabled": true`
   * `"pipeline_cache_archived": true`
   * `"vkvalidation_core_enabled": false`

6. **Buat GUI Launcher Mandiri:**
   Sesuaikan `templates/launcher.cpp` dan kompilasi:
   ```cmd
   tools\w64devkit\bin\g++.exe -O2 -mwindows templates\launcher.cpp -o "NamaGame.exe"
   ```

---

## 📜 Kebijakan Legalitas
Repositori ini **TIDAK** memuat aset game berhak cipta, file biner game (*eboot.bin* asli), maupun kunci enkripsi Sony. Repositori ini murni berisi perkakas open-source untuk rekayasa perangkat lunak, riset interoperabilitas platform x86-64, dan preservasi game.
