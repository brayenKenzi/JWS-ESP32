# Arsitektur MVP

```text
src/main.cpp (composition root, log)
    |
    +-- JwsApp (core C++, tanpa Arduino)
          +-- PrayerCalculator (persamaan astronomi dan validasi)
          +-- ClockProvider -> NtpClockProvider ATAU MockClockProvider
          +-- DisplayDriver -> OledDisplayDriver (SSD1306 virtual)
          +-- ConfigurationStore -> MemoryConfigurationStore (RAM)

AudioDriver -> SerialMockAudio (scaffold saja, belum dipicu scheduler)
```

`lib/JwsCore/src` dapat dikompilasi langsung di host. Driver Arduino berada di
`src/SimulationDrivers.*`. `JwsApp` menghitung jadwal hanya saat tanggal lokal
berubah, lalu mengirim frame paling cepat setiap 250 ms. Tampilan merotasi tiga
halaman jadwal setiap 4 detik. Tidak ada loop menunggu Wi-Fi/NTP atau `delay()`
dalam kode aplikasi; transaksi framebuffer I2C tetap sinkron dan berdurasi terbatas.

Clock NTP menyimpan waktu sistem sebagai UTC; offset menit diterapkan satu kali
saat membaca waktu lokal. Clock belum valid sampai callback SNTP diterima.
Setelah sinkronisasi, kehilangan Wi-Fi atau sinkronisasi berumur >=2 jam memberi
status HOLD (holdover clock internal). Ini bukan RTC baterai dan tidak bertahan
setelah power-off. Mode mock adalah build terpisah, bukan fallback diam-diam.

Konfigurasi MVP di `include/AppConfig.h`: lokasi, UTC offset menit, sudut Subuh/
Isya, faktor bayangan Ashar, ihtiyat, interval imsak. Ubah lalu rebuild. Belum ada
runtime dashboard, DST otomatis, maupun penyimpanan permanen. Memory store
menyediakan titik injeksi untuk pengujian dan menolak nilai invalid.

Kalkulasi menggunakan satu iterasi posisi matahari, elevasi nol, sunrise/sunset
pada -0.833°. Pembulatan ke menit terdekat; ihtiyat ditambah pada lima salat;
Syuruq tanpa tambahan; Imsak = Subuh yang sudah dikoreksi dikurangi interval.
Rentang tanggal MVP 2000–2099. Waktu di luar hari lokal, urutan tak masuk akal,
atau kejadian astronomi yang tidak tersedia menghasilkan schedule invalid.
Ini belum dikalibrasi dengan jadwal resmi Kemenag/lokasi pengguna.

Adapter production yang direncanakan (belum dibuat):

| Interface | Simulation MVP | Production setelah gate |
|---|---|---|
| DisplayDriver | OLED SSD1306 virtual | Panel P10 HUB12, refresh ESP32 yang sesuai |
| AudioDriver | Log Serial mock | DFPlayer Mini, UART tersendiri dan feedback |
| ClockProvider | SNTP / mock deterministic | NTP + DS3231, validasi RTC dan rekonsiliasi |
| ConfigurationStore | RAM | NVS Preferences atau filesystem berversi |

Tidak tersedia environment upload production di `platformio.ini`.
