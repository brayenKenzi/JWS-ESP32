# Atribusi dan lisensi

Proyek MVP: JWS Murottal ESP32 — Wokwi First, 2026. Source baru menggunakan
GPL-3.0-only; teks lisensi lengkap ada di `LICENSE`.

Referensi fungsional dan audit: **busel7/JWSMurottal**, NodeMCU ESP8266 / P10 HUB12,
https://github.com/busel7/JWSMurottal,
commit `4d2f7649feab2694f0615918034c2f08d8e8db08`, lisensi GPL-3.0.
Repository itu di-clone ke `../reference/JWSMurottal` untuk dibaca saja; tidak diubah.
Implementasi MVP ditulis terpisah, tanpa menyalin sketch, font, atau dashboard aslinya.
Teks `LICENSE` disalin utuh dari referensi.

Persamaan astronomi dirujuk dari **PrayTimes.org**, Hamid Zarrabi-Zadeh,
https://praytimes.org/docs/calculation. Algoritme C++ ditulis independen dari
dokumentasi persamaan tersebut. Fixture numerik dihasilkan dengan PrayTimes.js v2.5
(Copyright 2007–2017 PrayTimes.org, GNU LGPL v3.0),
https://praytimes.org/code/v2/js/PrayTimes.js.
Implementasi JS tidak ditautkan ke firmware dan tidak disertakan di proyek ini.
Sumber pembanding lokal tetap utuh di `../reference/PrayTimes.js`, beserta copyright
block-nya. SHA-256 sumber tercatat dalam `test/fixtures.json`.

Audit juga memeriksa `asmaklad/Arduino-Prayer-Times`, commit
`73c7cb7dcecc7b81c1529391c02250d7b32806b1`.
Header itu bergantung pada Arduino `String` dan tidak memiliki deklarasi lisensi
yang jelas dalam file yang diperiksa. Tidak ada kodenya yang di-vendor ke MVP.

Dependency build diunduh PlatformIO dan tetap memiliki lisensi masing-masing:

- Arduino-ESP32 / ESP-IDF: lihat paket framework untuk LGPL dan lisensi komponennya.
- Adafruit SSD1306: BSD, copyright Adafruit Industries.
- Adafruit GFX: BSD, copyright Adafruit Industries.
- Adafruit BusIO: MIT, copyright Adafruit Industries.

Untuk distribusi firmware/source, pertahankan `LICENSE`, `NOTICE.md`, copyright
dependency, dan sediakan source sesuai lisensi yang berlaku. Belum ada publikasi
atau distribusi binary ke layanan eksternal pada tahap ini.
