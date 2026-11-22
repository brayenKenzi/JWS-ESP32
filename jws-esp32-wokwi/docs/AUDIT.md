# Audit referensi — 3 November 2026

Sumber: https://github.com/busel7/JWSMurottal pada commit
`4d2f7649feab2694f0615918034c2f08d8e8db08`.
Clone lokal: `../../reference/JWSMurottal` relatif terhadap dokumen ini.
Audit statis; sketch ESP8266 asli tidak dibuild atau diuji hardware.

## Temuan kompatibilitas

| Bagian | Bukti source asli | Keputusan ESP32 / MVP |
|---|---|---|
| Wi-Fi, HTTP, web server | `JWSMurottal.ino:1–6`, ESP8266WiFi, ESP8266WebServer, ESP8266HTTPClient | Tidak bisa dipakai langsung; ESP32 memakai WiFi.h; dashboard/HTTP ditunda |
| Refresh P10 HJS589 | `Display.h:17–30`, ICACHE_RAM_ATTR, timer0_*, ESP.getCycleCount | Integrasi timer khusus ESP8266 tidak kompatibel; OLED virtual sebagai display MVP; driver HUB12 ditunda |
| Arduino Prayer Times | `FSConfig.h:549–556`, Karachi, Shafii, Fajr 20°, Isha 18° | Persamaan ditulis di core C++ bebas Arduino; kebijakan high latitude belum diterapkan |
| Dependency PrayerTimes | Header referensi mengimpor Arduino.h, WString.h, String.h | Ketergantungan Arduino perlu dihapus untuk host test; tidak digunakan karena metadata lisensi tidak jelas |
| DFPlayer Mini | `DFPlayer.h:8–12`, HardwareSerial memakai Serial | Library bukan otomatis tidak kompatibel; integrasi UART bentrok dengan log; production kelak memakai UART terpisah |
| DFPlayer service | `JWSMurottal.ino:94`, mp3.loop() dikomentari | Perlu service non-blocking dan status/error saat tahap audio |
| DS3231 | `RTC.h`, RtcDS3231<TwoWire> | Tidak terbukti inkompatibel; perlu explicit SDA/SCL dan penanganan RTC invalid; ditunda |
| FS / SPIFFS | `FSConfig.h`, SPIFFS global dan timer0_* pada save | ESP32 memerlukan include/inisialisasi sesuai core; interrupt display tidak boleh dikaitkan dengan store |
| ArduinoJson | DynamicJsonDocument/deserializeJson/serializeJson | Pola API v6; jangan upgrade mayor ke v7 tanpa audit; tidak dipakai MVP |
| WebSockets | WebSocketsServer.h | Dependency berpotensi mendukung ESP32, tetapi wiring event dan API harus diuji; ditunda |
| SoftwareSerial | include di sketch, komentar opsional di DFPlayer | Gunakan HardwareSerial ESP32; hapus kebutuhan emulasi UART |
| Pin NodeMCU | `JWS.h:2`, buzzer di GPIO3 (RX); `RTC.h:93`, Wire.begin() default | Jangan pakai ulang pemetaan lama tanpa audit UART, pin ESP32 dan kebutuhan boot-strapping |

## Temuan perilaku

- `JWS.h` berisi banyak delay 1–10 detik dan delay durasi adzan; web server,
  sinkronisasi, dan audio dapat terhambat selama menunggu.
- `WebServer.h` menunggu Wi-Fi dalam loop; MVP memakai koneksi asinkron dan retry berkala.
- `LoadDataAwal()` memperlakukan angka nol sebagai belum diisi: merusak koordinat
  ekuator/meridian nol, UTC+0, dan konfigurasi nol yang sah. MVP memvalidasi rentang.
- File header mendefinisikan state global dan driver sekaligus. MVP memisahkan
  interface, kalkulasi, aplikasi, dan adapter Arduino.
- Jadwal asli mengaktifkan high-latitude AngleBased. MVP hanya menerima hasil
  astronomi yang tersedia; hasil polar ditandai invalid, tanpa waktu buatan.
- Referensi tidak mengunci versi dependency. MVP mengunci platform dan library langsung.

## Lisensi dan keputusan scope

Lisensi referensi GPL-3.0 dipertahankan melalui atribusi dan lisensi proyek baru.
Lihat `../NOTICE.md`. Tidak menyalin dashboard, font, atau sketch ke MVP.

Production belum diimplementasikan: P10 HUB12, DFPlayer Mini dan DS3231 baru boleh
dikerjakan setelah checklist simulasi di `SIMULATION.md` lulus. Ini menunda bagian
production dari arsitektur akhir secara sengaja sesuai batas tahap pertama.
