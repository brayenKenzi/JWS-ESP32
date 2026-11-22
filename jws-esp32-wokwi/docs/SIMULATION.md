# Setup dan menjalankan Wokwi

## Persiapan

1. Buka folder **jws-esp32-wokwi** sebagai workspace VS Code, bukan folder clone referensi.
2. Pasang extension PlatformIO IDE dan Wokwi for VS Code dari penerbit resminya.
3. Ikuti aktivasi/lisensi Wokwi yang diminta extension. Jangan simpan token ke Git.
4. Build `sim` melalui PlatformIO atau langkah CLI di README.
5. Pastikan `.pio/build/sim/firmware.bin` dan `firmware.elf` tersedia.
6. Tekan F1 → **Wokwi: Start Simulator**.

Dokumentasi resmi:
[konfigurasi proyek](https://docs.wokwi.com/vscode/project-config),
[PlatformIO](https://docs.wokwi.com/vscode/platformio),
[Wi-Fi ESP32](https://docs.wokwi.com/guides/esp32-wifi),
[SSD1306](https://docs.wokwi.com/parts/board-ssd1306).

## Jika muncul "firmware binary ... not found in workspace"

Wokwi mencari hasil kompilasi, bukan source C++. File di `.pio/build` dapat hilang
setelah Clean, inisialisasi ulang build atau perpindahan komputer karena folder
ini diabaikan Git. Pastikan folder workspace yang dibuka adalah `jws-esp32-wokwi`,
yaitu folder yang berisi `platformio.ini`, `wokwi.toml` dan `diagram.json`.

Di sidebar PlatformIO, pilih **Project Tasks → sim → General → Build**, atau
**sim_mock → General → Build** sesuai path di `wokwi.toml`. Tunggu sampai
`SUCCESS`, lalu jalankan **Wokwi: Start Simulator** kembali. Gunakan **Build**;
perintah **Clean** menghapus hasil kompilasi.

Alternatif di terminal **PlatformIO: New Terminal**:

```powershell
pio run -e sim -e sim_mock
```

Gunakan PlatformIO dari extension yang sama untuk build sehari-hari agar tidak
berganti-ganti Core. Build yang berhasil harus menghasilkan `firmware.bin` dan
`firmware.elf` di folder environment yang dipilih.

## Rangkaian virtual

| ESP32 DevKit V1 | OLED SSD1306 |
|---|---|
| 3V3 | VCC |
| GND | GND |
| GPIO21 | SDA |
| GPIO22 | SCL |

OLED I2C address `0x3c`, resolusi 128×64. Diagram memakai part `board-ssd1306`.
Pin ESP32 pada diagram DevKit V1 bernama `D21` dan `D22` (GPIO21/GPIO22 di kode).
CLI lint v0.29.1 memberi info `unsupported-part` karena DevKit V1 terdaftar sebagai
tipe undocumented; ini bukan bukti bahwa simulator tidak mendukungnya. Tetap perlu
verifikasi runtime; lihat log lint di `evidence/diagram-lint.log`.
Display ini pengganti virtual untuk memeriksa business logic; tidak memvalidasi
refresh, brightness, pinout, atau timing panel P10 HUB12.

## Skenario NTP — harus dijalankan dan dicatat

- Boot menampilkan `JWS SIM WAIT`, `Menunggu NTP...`, tanpa jadwal palsu.
- Wi-Fi otomatis menggunakan `Wokwi-GUEST`, password kosong, channel 6.
- Setelah log `[CLOCK] NTP synchronized`, OLED menunjukkan label `NTP`, tanggal
  lokal dan detik berjalan. Periksa terhadap jam UTC+8 yang diketahui.
- Ketiga halaman menampilkan Imsak, Subuh, Syuruq, Dzuhur, Ashar, Maghrib, Isya;
  perpindahan setiap 4 detik, satu siklus penuh 12 detik.
- Catat tanggal, lokasi, parameter kalkulasi dan seluruh waktu yang dicetak Serial.
- Putuskan akses gateway setelah sinkronisasi: jam harus tetap berjalan dan
  menampilkan `HOLD` saat Wi-Fi terputus atau sinkronisasi berumur >=2 jam.
  Kehilangan internet saat Wi-Fi tetap tersambung tidak langsung dapat dideteksi.
- Restart tanpa akses internet: kembali `WAIT`. Persistent RTC belum tersedia.

Tidak ada timeout yang diam-diam mengubah mode NTP menjadi mock. Jika NTP gagal,
periksa gateway, DNS, UDP/123 dan status jaringan. Angka pada display tidak boleh
dianggap waktu aktual sampai sinkronisasi terbukti.

## Skenario mock offline — build terpisah

1. Build environment `sim_mock`.
2. Di `wokwi.toml`, ganti kedua segmen path `/sim/` menjadi `/sim_mock/` untuk tes ini.
   Simpan nilai sebelumnya dan kembalikan setelah tes. Contoh file sudah ada di
   `wokwi.mock.toml`; jangan mengubah `diagram.json`.
3. Mulai simulator. Label harus `MOCK`, bukan `NTP`.
4. Clock lokal mulai `2026-11-03 23:59:50`, berjalan mengikuti `millis()` simulasi.
5. Setelah sekitar 10 detik simulasi, tanggal harus `2026-11-04`, waktu `00:00:00`,
   dan log `[SCHEDULE] 2026-11-04` muncul tepat sekali untuk pergantian tanggal.
6. Periksa perubahan halaman dan jam tanpa internet. Restart harus mengulang seed.

Seed mock adalah waktu lokal tetap untuk pengujian; bukan pembacaan RTC dan
bukan waktu saat ini. Tidak ada komponen DFPlayer/DS3231 dalam diagram tahap ini.

## CLI opsional

Install [Wokwi CLI](https://docs.wokwi.com/wokwi-ci/cli-installation).
Sediakan `WOKWI_CLI_TOKEN` melalui environment lokal/secret manager, lalu dari
folder proyek:

```powershell
wokwi-cli --timeout 30000 --serial-log-file evidence/wokwi-ntp.log .
```

Jalankan mock setelah memilih firmware mock seperti langkah sebelumnya:

```powershell
wokwi-cli --timeout 20000 --serial-log-file evidence/wokwi-mock.log .
```

Untuk pemeriksaan statis diagram tanpa token: `wokwi-cli lint diagram.json`.

Periksa log, exit code dan screenshot secara langsung; jangan menyimpulkan
kelulusan hanya karena proses berakhir. CLI membutuhkan token Wokwi yang valid.
Log CLI tidak menggantikan pemeriksaan visual OLED.

## Gate sebelum tahap berikutnya

- [ ] Firmware `sim` dan `sim_mock` berhasil dieksekusi di Wokwi.
- [ ] Ada bukti Serial dan screenshot OLED untuk kedua mode.
- [ ] Sinkronisasi NTP dan offset zona waktu cocok dengan waktu pembanding.
- [ ] Tujuh jadwal terbaca pada semua halaman dan detik terus bertambah.
- [ ] Pergantian tanggal mock menghitung jadwal hari berikutnya sekali.
- [ ] Perilaku WAIT / HOLD / restart offline sesuai batasan yang didokumentasikan.
- [ ] Perbandingan jadwal setempat ditinjau sebelum penggunaan ibadah sebenarnya.

Checklist ini belum boleh dicentang dari hasil unit test atau build saja.
Driver production tidak dikerjakan sampai gate simulasi diselesaikan.
