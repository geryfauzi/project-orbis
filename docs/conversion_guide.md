# Panduan Konversi & Porting Game PS4 ke PC (Project Orbis)

Dokumen ini memuat metodologi rekayasa balik (*reverse engineering*), penanganan aset, dan alur kerja teknis untuk menjalankan game PlayStation 4 di PC secara native menggunakan Project Orbis.

---

## 1. Persiapan Awal & Ekstraksi Game

Setiap game PS4 didistribusikan dalam bentuk paket biner `.pkg`.

### A. Ekstraksi Base Game & Update
1. Ekstrak Base Game terlebih dahulu:
   ```cmd
   pkgextract_tool\pkgextract.exe -o "game_base" "path\ke\base_game.pkg"
   ```
2. **Peringatan Penting untuk Update:**
   Flag `-f` / `--overwrite` pada `pkgextract.exe` akan **menghapus seluruh folder tujuan** sebelum mengekstrak. Oleh karena itu, jangan mengekstrak Update langsung ke folder base game dengan flag `-f`.
   * Ekstrak Update ke folder terpisah:
     ```cmd
     pkgextract_tool\pkgextract.exe -o "game_update" "path\ke\update.pkg"
     ```
   * Gabungkan (*overlay merge*) file `game_update` ke atas `game_base` agar file arsip dasar tidak hilang.

### B. Audit Modul Sistem (`sce_module/`)
Banyak game PS4 membutuhkan dynamic library sistem konsol:
* `libc.prx`
* `libSceFios2.prx`
Pastikan kedua file PRX ini berada di dalam folder `game\sce_module\`. Tanpa modul ini, game akan gagal memuat dengan error assertion `libc.prx cannot be loaded`.

---

## 2. Uji Coba Awal & Triage Masalah

Jalankan game menggunakan translation layer Vulkan:
```cmd
runtime\shadPS4.exe -g "game\eboot.bin" -f false
```

Amati gejala awal pada layar dan log (`%APPDATA%\shadps4\log\shadps4.log`):
1. **Layar Hitam / Freeze Seketika:** Sering disebabkan oleh kegagalan inisialisasi thread utama atau deadlock saat memanggil API sistem Sony yang belum di-stub.
2. **Canvas Berwarna Tanpa Objek 3D (Kasus Canvas Biru):** Engine game berhasil menginisialisasi framebuffer dasar, namun subsistem rendering 3D dibatalkan karena kegagalan pengecekan hardware/layanan konsol (lihat Bab 4).
3. **Crash Access Violation (`0xC0000005`):** Dereferensi pointer NULL karena file arsip `.pak` hilang saat penggabungan update, atau modul PRX belum terpasang.

---

## 3. Investigasi Call Stack & Memori Live

Ketika game freeze atau tidak merender frame:

1. **Dump Thread Call Stack:**
   Gunakan tool `dump_gamemain_stack`:
   ```cmd
   tools\dump_gamemain_stack.exe <PID_GAME>
   ```
   Tool ini menempel (*attach*) ke proses game yang sedang berjalan, membaca konteks register CPU (`RIP`, `RSP`, `RBP`), dan menelusuri rantai pemanggilan fungsi (*stack walk*) untuk menemukan offset instruksi yang sedang macet.

2. **Inspeksi Variabel Global di RAM:**
   Gunakan `read_live_globals`:
   ```cmd
   tools\read_live_globals.exe <PID_GAME>
   ```
   Membaca alamat memori guest untuk memastikan apakah instance renderer grafis dan game state bernilai valid atau `0x0` (NULL).

---

## 4. Teknik Bypass Abort Jump (Binary Patching)

Pada game konsol tertentu (contoh: *Beach Buggy Racing 2* dengan engine Vector Unit), engine memiliki puluhan pengecekan subsistem beruntun pada fungsi inisialisasi awal:
* Contoh: Jika layanan PSN, telemetry, atau voice chat mengembalikan `false`, engine mengeksekusi conditional jump (`je` / `0F 84`) ke blok pembersihan/destruktor akhir.
* Akibatnya, inisialisasi renderer grafis 3D dilewati dan dibatalkan.

### Solusi Otomatis:
Gunakan atau adaptasi tool `patch_subsystem_jumps.cpp`:
1. Tool ini memindai rentang fungsi inisialisasi biner `eboot.bin`.
2. Mencari pola instruksi conditional jump kegagalan (`0F 84 disp32`).
3. Mengganti (*NOP*) lompatan kegagalan tersebut menjadi `90 90 90 90 90 90`, sehingga kegagalan satu subsistem opsional tidak menghentikan inisialisasi subsistem grafis dan gameplay.
*(Catatan: Langkah ini hanya diperlukan jika game mengalami Subsystem Abort / Empty Canvas. Game modern seperti CTR Nitro-Fueled tidak memerlukan patch ini).*

---

## 5. Konfigurasi Audio, Shader Cache & Anti-Stuttering

Buka file konfigurasi runtime di `%APPDATA%\shadps4\config.json`:

### A. Audio LFE Folding (7.1 $\rightarrow$ 2.0 Stereo)
* Ubah backend audio ke OpenAL (`"audio_backend": 1`).
* OpenAL secara otomatis melipat channel LFE (*Low-Frequency Effects / bass ledakan*) ke channel stereo depan (*stereo folding*), sehingga suara efek ledakan terdengar seimbang dan bertenaga.

### B. Vulkan Pipeline Cache (Mencegah Stuttering Awal)
Saat aksi baru (seperti toleh ke belakang atau efek partikel baru) pertama kali muncul, driver kartu grafis harus mengompilasi shader secara *on-the-fly*.
Agar tidak patah-patah di sesi berikutnya:
* Ubah `"pipeline_cache_enabled": true`
* Ubah `"pipeline_cache_archived": true`
* Ubah `"vkvalidation_core_enabled": false`
Cache shader akan otomatis disimpan di `%APPDATA%\shadps4\cache\`.

---

## 6. Pembuatan Launcher PC Mandiri

Setelah game berjalan mulus:
1. Buka `templates/launcher.cpp`.
2. Sesuaikan `kGameTitle` dan path relatif `eboot.bin`.
3. Kompilasi menggunakan toolchain portable bawaan:
   ```cmd
   tools\w64devkit\bin\g++.exe -O2 -mwindows templates\launcher.cpp -o "NamaGame.exe"
   ```
4. Letakkan file `.exe` ini di root folder game. Pemain cukup klik ganda file ini untuk langsung bermain tanpa jendela terminal.
