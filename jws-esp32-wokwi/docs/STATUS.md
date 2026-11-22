# Status tahap pertama — 3 November 2026

**MVP source dan build tersedia; eksekusi Wokwi belum selesai.**
Tidak ada klaim validasi simulator, panel P10, DFPlayer atau RTC fisik.

## Hasil yang benar-benar diverifikasi

| Pemeriksaan | Hasil | Bukti |
|---|---|---|
| Clone/audit referensi | Commit `4d2f7649feab2694f0615918034c2f08d8e8db08`; working tree bersih | `AUDIT.md`, `evidence/reference-audit.txt` |
| Pengujian core C++ pada Windows host | **26 lulus, 0 gagal**, proses exit 0 | `evidence/native-tests.log` |
| Build ESP32 `sim` (NTP) | **SUCCESS**, RAM 45.736 B / 14,0%, flash 793.233 B / 60,5% | `evidence/build-sim.log` |
| Build ESP32 `sim_mock` | **SUCCESS**, RAM 22.244 B / 6,8%, flash 318.377 B / 24,3% | `evidence/build-sim-mock.log` |
| Wokwi diagram lint v0.29.1 | Exit 0, **0 error**, 1 info undocumented DevKit V1 | `evidence/diagram-lint.log` |
| Percobaan Wokwi CLI | Exit 1 sebelum simulasi: `Missing WOKWI_CLI_TOKEN` | `evidence/wokwi-attempt.log` |

Path bukti di tabel relatif terhadap root proyek. `verification.json` mencatat
SHA-256 binary, versi alat dan batas verifikasi. File firmware ada di `.pio/build`,
diabaikan Git; bisa dibuat ulang dari source.

Build menggunakan PlatformIO Core 6.1.18, platform espressif32 6.10.0,
Arduino-ESP32 2.0.17 (paket 3.20017.241212+sha.dcc1105b), Xtensa GCC 8.4.0.
Native test memakai Zig 0.13.0 pada Windows x64. Instalasi awal runtime Zig
menampilkan warning dllexport dari libunwind bawaannya; source proyek berhasil
dikompilasi dengan `-Wall -Wextra -Werror` dan semua test dieksekusi.

## Sudah diimplementasikan

- Struktur proyek PlatformIO terpisah dan repository Git lokal baru.
- Empat interface: DisplayDriver, AudioDriver, ClockProvider, ConfigurationStore.
- Core kalkulasi Imsak, Subuh, Syuruq, Dzuhur, Ashar, Maghrib, Isya bebas Arduino.
- Validasi konfigurasi/tanggal; jadwal invalid eksplisit pada kejadian yang tidak tersedia.
- Adapter NTP dengan fixed UTC offset menit, WAIT dan HOLD; tidak menunggu Wi-Fi dalam loop.
- Mock clock deterministic dan pergantian tanggal yang lulus host test.
- Driver OLED virtual, rotasi halaman, diagram wiring I2C yang lulus lint.
- MemoryConfigurationStore dan SerialMockAudio sebagai adapter awal.
- Dokumentasi audit, arsitektur, setup, pengujian dan atribusi GPL-3.0.

Keberadaan source adapter bukan bukti perilaku runtime berhasil. Perubahan
konfigurasi MVP memerlukan rebuild; RAM store belum persisten.

## Belum diuji di Wokwi / hardware

- Boot firmware, koneksi Wi-Fi, sinkronisasi NTP dan ketepatan jam lokal.
- Hasil visual OLED dan kecepatan rotasi halaman pada simulator.
- Pergantian hari mock pada ESP32 virtual (baru lulus host test).
- Kehilangan jaringan, holdover, restart offline, dan stabilitas jangka panjang.
- Seluruh hardware production.

CLI terpasang untuk percobaan lokal, tetapi token tidak tersedia. Halaman Wokwi
juga dibuka melalui browser; akses tersebut belum menghasilkan bukti simulator
berjalan. Tidak ada log serial simulasi atau screenshot hasil OLED.
Selesaikan langkah dan checklist [SIMULATION.md](SIMULATION.md) menggunakan
Wokwi for VS Code yang aktif atau CLI dengan token melalui environment.

## Fitur tahap lanjutan yang belum dikerjakan

- Countdown iqomah tiap salat, state adzan, dan running text.
- Scheduler murottal serta simulasi protokol/feedback DFPlayer Mini.
- Dashboard web responsif untuk lokasi, teks, jadwal dan audio.
- Penyimpanan permanen dan migrasi/validasi versi konfigurasi.
- RTC offline yang bertahan saat power-off.
- Driver P10 HUB12, DFPlayer hardware, DS3231 dan integrasi production.
- Validasi jadwal resmi setempat, elevasi, DST dan kebijakan high latitude.

Mock audio hanya mencetak log jika metodenya dipanggil; belum ada playback,
penjadwalan, atau emulasi modul. Belum ada remote GitHub yang dibuat/push.
Gate production tetap tertutup sampai simulasi dan pengujian terkait selesai.
