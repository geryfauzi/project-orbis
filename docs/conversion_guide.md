# Panduan Konversi & Porting Game PS4 ke PC (Project Orbis)

Dokumen ini memuat metodologi rekayasa balik (*reverse engineering*) dan alur kerja teknis untuk menjalankan game PlayStation 4 di PC secara native menggunakan Project Orbis.

---

## 1. Persiapan Awal & Ekstraksi Game

Setiap game PS4 didistribusikan dalam bentuk paket biner `.pkg`.

1. Letakkan file game `.pkg` di direktori kerja.
2. Jalankan tool ekstraksi:
   ```cmd
   pkgextract_tool\pkgextract.exe "path\ke\game.pkg" "extracted_game"
   ```
3. Struktur minimum yang dihasilkan:
   * `eboot.bin`: Biner eksekusi utama (ELF64 x86-64 Orbis OS).
   * `sce_module/`: Modul PRX dinamis sistem atau engine.
   * File arsip aset (misal: `.apf`, `.pak`, `.dat`, dll).

---

## 2. Uji Coba Awal & Triage Masalah

Jalankan game menggunakan translation layer Vulkan:
```cmd
runtime\shadPS4.exe -g "extracted_game\eboot.bin" -f false
```

Amati gejala awal pada layar:
1. **Layar Hitam / Freeze Seketika:** Sering disebabkan oleh kegagalan inisialisasi thread utama atau deadlock saat memanggil API sistem Sony yang belum di-stub.
2. **Canvas Berwarna Tanpa Objek 3D (Kasus Canvas Biru):** Engine game berhasil menginisialisasi framebuffer dasar, namun subsistem rendering 3D dibatalkan karena kegagalan pengecekan hardware/layanan konsol.
3. **Crash Access Violation (`0xC0000005`):** Dereferensi pointer NULL karena salah satu subsistem mengembalikan status gagal (`0`).

---

## 3. Investigasi Call Stack & Memori Live

Ketika game freeze atau tidak merender frame:

1. **Dump Thread Call Stack:**
   Gunakan tool `dump_gamemain_stack`:
   ```cmd
   tools\dump_gamemain_stack.exe
   ```
   Tool ini menempel (*attach*) ke proses game yang sedang berjalan, membaca konteks register CPU (`RIP`, `RSP`, `RBP`), dan menelusuri rantai pemanggilan fungsi (*stack walk*) untuk menemukan offset instruksi yang sedang macet.

2. **Inspeksi Variabel Global di RAM:**
   Gunakan `read_live_globals`:
   ```cmd
   tools\read_live_globals.exe
   ```
   Membaca alamat memori guest untuk memastikan apakah instance renderer grafis dan game state bernilai valid atau `0x0` (NULL).

---

## 4. Teknik Bypass Abort Jump (Binary Patching)

Pada game konsol, engine game sering memiliki lusinan pengecekan subsistem beruntun pada fungsi inisialisasi awal (misalnya fungsi `init()` engine):
* Contoh: Jika layanan PSN, telemetry, atau voice chat mengembalikan `false`, engine mengeksekusi conditional jump (`je` / `0F 84`) ke blok pembersihan/destruktor akhir.
* Akibatnya, inisialisasi renderer grafis 3D dilewati dan dibatalkan.

### Solusi Otomatis:
Gunakan tool `patch_subsystem_jumps.cpp`:
1. Tool ini memindai rentang fungsi inisialisasi biner `eboot.bin`.
2. Mencari pola instruksi conditional jump kegagalan (`0F 84 disp32`).
3. Mengganti (*NOP*) lompatan kegagalan tersebut menjadi `90 90 90 90 90 90`, sehingga kegagalan satu subsistem opsional tidak menghentikan inisialisasi subsistem grafis dan gameplay.

---

## 5. Konfigurasi Audio & Downmixing

Game PS4 umumnya dirancang untuk output audio 7.1 surround (8-channel).
Jika suara efek (ledakan/SFX) terdengar terlalu pelan pada speaker stereo PC:
* Buka konfigurasi runtime di `%APPDATA%\shadps4\config.json`.
* Ubah backend audio ke OpenAL (`"audio_backend": 1`).
* OpenAL secara otomatis melipat channel LFE (*Low-Frequency Effects / bass ledakan*) ke channel stereo depan (*stereo folding*), sehingga suara efek ledakan terdengar seimbang dan bertenaga.

---

## 6. Pembuatan Launcher PC

Setelah game berjalan mulus:
1. Buka `templates/launcher.cpp`.
2. Sesuaikan `kGameTitle` dan path relatif `eboot.bin`.
3. Kompilasi menggunakan toolchain:
   ```cmd
   tools\w64devkit\bin\g++.exe -O2 -mwindows templates\launcher.cpp -o "NamaGame.exe"
   ```
4. File `.exe` ini dapat diklik ganda oleh pemain tanpa memunculkan jendela terminal hitam.
