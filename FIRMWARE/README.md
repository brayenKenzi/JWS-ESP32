# Panduan Flash Firmware JWS ESP32 (Jadwal Waktu Sholat)

Repositori folder ini berisi seluruh file binary (`.bin`) siap flash untuk sistem Jadwal Waktu Sholat (JWS) ESP32. Firmware sudah mengintegrasikan **Web Dashboard responsif**, **6 Waktu Sholat Kota Kotamobagu (Imsak, Subuh, Dzuhur, Ashar, Maghrib, Isya)**, **Kaligrafi Twinkle 32x32 (Muhammad & Allah)**, **Nama Hari**, serta **Hitung Mundur Sholat & Iqomah**.

---

## 📁 Struktur Direktori Firmware

```
FIRMWARE/
├── 1_HD_WF2_ESP32_S3_PANEL_P5/       <-- Modul Huidu HD-WF2 versi ESP32-S3 (Rekomendasi Produksi)
│   ├── bootloader.bin
│   ├── partitions.bin
│   └── firmware.bin
├── 2_HD_WF2_ESP32_CLASSIC_PANEL_P5/  <-- Modul Huidu HD-WF2 versi ESP32 Standar (Classic)
│   ├── bootloader.bin
│   ├── partitions.bin
│   └── firmware.bin
├── 3_ESP32_TFT_ILI9341/              <-- ESP32 DevKit V1 + Layar TFT SPI 2.8" / 3.2" (ILI9341)
│   ├── bootloader.bin
│   ├── partitions.bin
│   └── firmware.bin
├── 4_ESP32_TFT_MOCK_OFFLINE/         <-- ESP32 + TFT Versi Simulasi Offline (Clock Pengujian)
│   ├── bootloader.bin
│   ├── partitions.bin
│   └── firmware.bin
├── 5_ESP32_S3_TFT_ILI9341/           <-- ESP32-S3 N16R8 + Layar TFT 2.8" SPI (Desktop JWS & Smart AI)
│   ├── bootloader.bin
│   ├── partitions.bin
│   └── firmware.bin
└── README.md
```

---

## ⚡ Panduan Flash Menggunakan ESP Flash Download Tool (GUI)

Jika Anda menggunakan software resmi **Espressif Flash Download Tool**:

### 1. Modul HD-WF2 ESP32-S3 (`1_HD_WF2_ESP32_S3_PANEL_P5`)
- **Chip Type:** `ESP32-S3`
- **WorkMode:** `Develop`
- **LoadMode:** `UART`
- **Daftar File & Offset:**
  | File Path | Alamat Offset |
  |---|---|
  | `...\1_HD_WF2_ESP32_S3_PANEL_P5\bootloader.bin` | `0x0000` |
  | `...\1_HD_WF2_ESP32_S3_PANEL_P5\partitions.bin` | `0x8000` |
  | `...\1_HD_WF2_ESP32_S3_PANEL_P5\firmware.bin`   | `0x10000` |
- **SPI Speed:** `40MHz` atau `80MHz`
- **SPI Mode:** `DIO` atau `QIO`
- **Flash Size:** `8MB` (64Mbit) atau `4MB`

### 2. Modul HD-WF2 ESP32 Classic (`2_HD_WF2_ESP32_CLASSIC_PANEL_P5`) & Layar TFT (`3_ESP32_TFT_ILI9341`)
- **Chip Type:** `ESP32`
- **WorkMode:** `Develop`
- **LoadMode:** `UART`
- **Daftar File & Offset:**
  | File Path | Alamat Offset |
  |---|---|
  | `...\bootloader.bin` | `0x1000` |
  | `...\partitions.bin` | `0x8000` |
  | `...\firmware.bin`   | `0x10000` |
- **SPI Speed:** `40MHz`
- **SPI Mode:** `DIO`
- **Flash Size:** `4MB` (32Mbit)

### 3. Modul ESP32-S3 + Layar TFT 2.8" SPI (`5_ESP32_S3_TFT_ILI9341`)
- **Peruntukan:** Board **ESP32-S3 N16R8** + Layar **TFT ILI9341 2.8"**
- **Chip Type:** `ESP32-S3`
- **WorkMode:** `Develop`
- **LoadMode:** `UART`
- **Daftar File & Offset:**
  | File Path | Alamat Offset |
  |---|---|
  | `...\5_ESP32_S3_TFT_ILI9341\bootloader.bin` | `0x0000` |
  | `...\5_ESP32_S3_TFT_ILI9341\partitions.bin` | `0x8000` |
  | `...\5_ESP32_S3_TFT_ILI9341\firmware.bin`   | `0x10000` |
- **SPI Speed:** `40MHz` atau `80MHz`
- **SPI Mode:** `QIO` atau `DIO`
- **Flash Size:** `16MB` (128Mbit) atau `8MB`

---

## 💻 Panduan Flash via Command Line (CLI)

### Cara A: Menggunakan PlatformIO (Langsung dari Proyek)
Buka terminal PowerShell pada direktori root proyek (`C:\Users\Kenzi\Desktop\MASTER WEBSITE\JWS ESP32`), lalu jalankan:

```powershell
# Untuk Modul Desktop ESP32-S3 N16R8 + Layar TFT 2.8":
& ".\.tools\venv\Scripts\platformio.exe" run -d jws-esp32-wokwi -e desktop_s3_tft -t upload

# Untuk Modul HD-WF2 ESP32-S3:
& ".\.tools\venv\Scripts\platformio.exe" run -d jws-esp32-wokwi -e hd_wf2_s3 -t upload

# Untuk Modul HD-WF2 ESP32 Classic:
& ".\.tools\venv\Scripts\platformio.exe" run -d jws-esp32-wokwi -e hd_wf2 -t upload

# Untuk ESP32 DevKit V1 + Layar TFT:
& ".\.tools\venv\Scripts\platformio.exe" run -d jws-esp32-wokwi -e sim -t upload
```

### Cara B: Menggunakan `esptool.py` Manual
Ganti `COMx` dengan nomor port USB Anda (contoh `COM3`):

```powershell
# Contoh flash HD-WF2 ESP32-S3:
python -m esptool --chip esp32s3 -p COMx -b 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_size 8MB 0x0000 "1_HD_WF2_ESP32_S3_PANEL_P5\bootloader.bin" 0x8000 "1_HD_WF2_ESP32_S3_PANEL_P5\partitions.bin" 0x10000 "1_HD_WF2_ESP32_S3_PANEL_P5\firmware.bin"

# Contoh flash HD-WF2 ESP32 Classic:
python -m esptool --chip esp32 -p COMx -b 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_size 4MB 0x1000 "2_HD_WF2_ESP32_CLASSIC_PANEL_P5\bootloader.bin" 0x8000 "2_HD_WF2_ESP32_CLASSIC_PANEL_P5\partitions.bin" 0x10000 "2_HD_WF2_ESP32_CLASSIC_PANEL_P5\firmware.bin"
```

---

## 🔌 Pinout Resmi Modul HD-WF2 ke Panel P5 (HUB75)

Modul **Huidu HD-WF2** memiliki port output HUB75 bawaan (**Port X1**). Sambungkan kabel pita 16-pin dari port X1 ke **HUB75 INPUT** pada Panel P5 pertama:

| Pin HUB75 | ESP32-S3 (HD-WF2 V2) | ESP32 Classic (HD-WF2 V1) | Keterangan |
|---|---|---|---|
| **R1** | GPIO 2  | GPIO 19 | Red Data Baris Atas |
| **G1** | GPIO 6  | GPIO 13 | Green Data Baris Atas |
| **B1** | GPIO 10 | GPIO 18 | Blue Data Baris Atas |
| **R2** | GPIO 3  | GPIO 5  | Red Data Baris Bawah |
| **G2** | GPIO 7  | GPIO 12 | Green Data Baris Bawah |
| **B2** | GPIO 11 | GPIO 17 | Blue Data Baris Bawah |
| **A**  | GPIO 39 | GPIO 16 | Row Address A |
| **B**  | GPIO 38 | GPIO 14 | Row Address B |
| **C**  | GPIO 37 | GPIO 4  | Row Address C |
| **D**  | GPIO 36 | GPIO 27 | Row Address D (1/16 scan) |
| **E**  | GPIO 21 | GPIO 25 | Row Address E (1/32 scan P5 64x64) |
| **CLK**| GPIO 34 | GPIO 2  | Shift Clock |
| **LAT**| GPIO 33 | GPIO 23 | Data Latch |
| **OE** | GPIO 35 | GPIO 15 | Output Enable (Brightness PWM) |
| **Buzzer** | GPIO 4 | GPIO 32 | Alarm Adzan & Iqomah |
| **DFPlayer TX/RX** | GPIO 8 / 5 | - | Modul Suara Tartil / Tarhim |
| **RTC DS3231** | SDA: 41, SCL: 42 | SDA: 21, SCL: 22 | I2C RTC Clock Eksternal |

> ⚠️ **Catatan Penting Daya (Power Supply):**
> Panel P5 membutuhkan arus puncak hingga **3A - 5A per panel** pada kecerahan tinggi. Gunakan Power Supply **5V DC 10A - 20A berkualitas** dan sambungkan kabel power langsung ke terminal DC 5V panel dan board. Jangan menyalakan panel hanya dari port USB laptop!

---

## 🌐 Akses Web Dashboard Setelah Flash

1. Setelah modul menyala, ESP32 akan menyiarkan Wi-Fi Access Point atau terhubung ke jaringan lokal.
2. Buka browser melalui HP atau laptop, lalu buka alamat IP modul (misalnya `http://192.168.4.1` atau IP lokal yang didapat dari router).
3. Anda dapat langsung:
   - Mengubah **Nama Masjid** & Pesan **Running Text**.
   - Memilih jumlah panel: **1, 2, 3, atau 4 Panel** serta opsi **1 Baris (32px)** atau **2 Baris (64px)**.
   - Menyesuaikan **Revisi Menit Sholat (-30 s/d +30 menit)** & Jeda Iqomah tiap sholat.
   - Mengaktifkan **Kaligrafi Twinkle** & Scene **Petugas Jumat / QRIS**.
