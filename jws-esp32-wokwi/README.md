# JWS Murottal ESP32 — Wokwi First

MVP tahap pertama: ESP32 DevKit V1, Arduino C++, PlatformIO, OLED virtual Wokwi,
clock NTP dan kalkulasi jadwal salat. Proyek terpisah dari
[busel7/JWSMurottal](https://github.com/busel7/JWSMurottal).
Lisensi GPL-3.0; baca [atribusi](NOTICE.md) dan [audit](docs/AUDIT.md).

**Build dan host test berbeda dari eksekusi simulator.** Status bukti aktual
tercantum di [STATUS](docs/STATUS.md). Hardware production belum digunakan.

## Mulai di Windows / PowerShell

Buka terminal di folder proyek ini. Python 3.11+, Git dan internet diperlukan;
unduhan awal PlatformIO/toolchain bisa memerlukan beberapa menit.

```powershell
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r requirements-dev.txt
.venv/Scripts/python.exe scripts/test_native.py --zig
.venv/Scripts/python.exe -m platformio run -e sim
.venv/Scripts/python.exe -m platformio run -e sim_mock
```

Alternatif verifikasi lengkap dengan penyimpanan log:

```powershell
./scripts/verify.ps1 -Python ./.venv/Scripts/python.exe
```

Jika memakai PlatformIO IDE, pilih environment **sim** lalu Build. Perintah setara
di terminal PlatformIO adalah `pio run -e sim`. Build tidak meng-upload firmware.
Untuk Linux/macOS, gunakan `.venv/bin/python`; native test juga bisa memakai g++
atau clang++ yang ada di PATH. Zig hanya diperlukan bila compiler host belum ada.

Output firmware:

- `.pio/build/sim/firmware.bin` dan `firmware.elf`: clock NTP.
- `.pio/build/sim_mock/firmware.bin` dan `firmware.elf`: clock mock offline.

Lanjutkan dengan [panduan Wokwi dan checklist](docs/SIMULATION.md).
`diagram.json` dan `wokwi.toml` sudah menunjuk OLED serta firmware mode NTP.

## Konfigurasi MVP

Ubah `include/AppConfig.h` lalu rebuild. Default: Makassar, latitude -5.1477,
longitude 119.4327, UTC offset 480 menit (WITA), Subuh 20°, Isya 18°, Ashar standar,
ihtiyat +2 menit pada lima waktu, Imsak 10 menit sebelum Subuh. Syuruq tanpa
ihtiyat. Ini parameter awal simulasi, belum validasi jadwal resmi setempat.
Zona waktu berupa fixed offset menit; DST otomatis belum diimplementasikan.

Saat belum tersinkron NTP, display menunggu tanpa jadwal. Mock mulai pada tanggal
uji tetap dan selalu diberi label `MOCK`. Penyimpanan masih RAM dan reset saat
restart. Tidak ada dashboard konfigurasi pada tahap ini.

## Struktur

| Path | Isi |
|---|---|
| `lib/JwsCore/src` | Perhitungan, interface, aplikasi, memory store, mock clock |
| `src` | Adapter OLED/NTP, mock audio, composition root |
| `include/AppConfig.h` | Konfigurasi lokasi/metode untuk MVP |
| `test` | Fixture pembanding dan pengujian core C++ |
| `scripts` | Runner test, generator fixture, verifikasi PowerShell |
| `docs` | Audit, arsitektur, setup simulasi, status dan batasan |
| `evidence` | Log verifikasi; simulator hanya bila benar-benar dijalankan |

Lihat [arsitektur](docs/ARCHITECTURE.md). Interface `DisplayDriver`, `AudioDriver`,
`ClockProvider`, `ConfigurationStore` memisahkan logika dari adapter hardware.

## Pengujian dan fixture

`scripts/test_native.py` mengompilasi dan **menjalankan core C++ yang sama dengan
firmware**, bukan implementasi Python pengganti. Test memeriksa lima fixture
PrayTimes, interval imsak, koreksi menit, offset zona waktu, metode Ashar,
kalender leap year, input invalid/NaN, kondisi polar, format waktu, pergantian
tengah malam, clock belum sinkron dan rollover `millis()`.

Fixture memakai PrayTimes.js v2.5 sebagai regresi numerik, toleransi maksimal
1 menit per event, parameter eksplisit dan SHA-256 di `test/fixtures.json`.
Algoritme pembanding berasal dari keluarga persamaan yang sama, sehingga hasil
ini tidak merupakan validasi astronomi independen atau persetujuan Kemenag.
Regenerasi opsional dengan Node.js dan file pembanding yang diunduh terpisah:

```powershell
node scripts/generate-fixtures.cjs ../reference/PrayTimes.js
```

File JS pembanding dan clone referensi tidak diperlukan untuk build normal.

## Tahap lanjutan yang ditunda

Iqomah, running text, jadwal murottal, emulasi protokol DFPlayer, dashboard
responsif, konfigurasi persisten dan RTC offline belum diimplementasikan.
Mock audio saat ini hanya adapter log, tanpa scheduler atau suara.
P10 HUB12/DFPlayer/DS3231 production menunggu gate simulasi, kemudian dirancang
dan diuji dalam tahap terpisah. Tidak ada environment production dalam proyek.
