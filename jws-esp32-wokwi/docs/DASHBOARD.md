# Dashboard JWS Kotamobagu — simulasi

Lokasi awal: Mongkonai Barat, Kota Kotamobagu, Sulawesi Utara. Zona waktu WITA
(UTC+08:00). Sumber jadwal: **Kementerian Agama Republik Indonesia**, melalui
tabel publik Bimas Islam. Jadwal mengacu pada titik rujukan Kota Kotamobagu,
bukan GPS rumah atau pengukuran otomatis dari browser.

## Membuka kontrol perangkat

1. Buka folder `jws-esp32-wokwi` di VS Code.
2. Jalankan `pio run -e sim` melalui terminal PlatformIO; tunggu SUCCESS.
3. Jalankan **Wokwi: Start Simulator**.
4. Buka **http://localhost:8180** di browser komputer yang sama.

`wokwi.toml` meneruskan localhost:8180 ke port 80 ESP32 virtual. Jika koneksi
ditolak, pastikan simulator masih berjalan, gateway jaringan Wokwi aktif, dan
tidak ada aplikasi lain menggunakan port 8180. Tidak perlu upload ke ESP32 asli.

Mode `sim_mock` juga menyediakan dashboard melalui Wi-Fi virtual. Jam mock
tetap tanggal pengujian, tidak menunjukkan waktu sekarang. Sinkronisasi HTTPS
otomatis menunggu jam NTP yang valid agar verifikasi sertifikat tidak memakai
tanggal palsu. Tabel tersimpan tetap bisa dipakai pada tanggal mock tahun 2026.

## Pengaturan

- **Nama masjid:** tampil sebagai judul display ILI9341. Nama yang sangat panjang
  dipendekkan di display; nilai lengkap tetap ada di dashboard.
- **Running text:** isi pesan, aktif/nonaktif, dan interval gerak 40–250 ms.
  Interval lebih kecil membuat gerakan lebih cepat.
- **Koreksi waktu:** masing-masing Imsak, Subuh, Syuruq, Dzuhur, Ashar, Maghrib,
  dan Isya memiliki koreksi -30 sampai +30 menit. Positif berarti lebih lambat;
  negatif berarti lebih awal. Syuruq adalah penanda terbit matahari.
- **Simpan perubahan:** baru diterapkan ke perangkat jika permintaan berhasil
  divalidasi dan tersimpan di NVS. Data tidak valid atau kegagalan penyimpanan
  harus menghasilkan error, bukan konfirmasi sukses.
- **Sinkronkan Kemenag:** meminta pengambilan ulang tabel bulan saat ini dari
  endpoint publik resmi. Permintaan dijalankan oleh task jaringan terpisah agar
  jam/display tetap dilayani selama HTTP berlangsung.

Default nama masih `Nama Masjid`; isi nama masjid sebenarnya melalui dashboard.
Teks display menggunakan font Latin dasar, sehingga input dibatasi ke karakter
ASCII yang bisa dicetak. Dashboard sendiri memakai antarmuka berbahasa Indonesia.

Koreksi awal semuanya **0**. Jadwal resmi tidak ditambah ihtiyat global lagi.
Dashboard memperlihatkan nilai Kemenag, koreksi, dan hasil akhir secara terpisah.
Koreksi yang membuat urutan jadwal tidak masuk akal atau keluar dari hari lokal
ditolak. Perubahan lain pada dashboard tidak mengulang seed clock mock.

## Sumber dan cache

Tabel resmi tahun 2026 disertakan sebagai data awal agar bisa dipakai tanpa
internet. Pengambilan online memakai nama provinsi `SULAWESI UTARA` dan kota
`KOTA KOTAMOBAGU` pada endpoint publik situs Bimas Islam, dengan TLS terverifikasi.
Tidak menggunakan mirror pihak ketiga, token API bocor, atau `setInsecure`.

Hasil pengambilan bulan baru harus lengkap dan valid sebelum mengganti cache.
Jika koneksi gagal, data valid sebelumnya dipertahankan. Jika tanggal tidak
tersedia di cache maupun tabel bawaan, jadwal ditandai tidak tersedia; waktu
hasil kalkulasi tidak dilabeli sebagai data Kemenag.

Script `scripts/fetch_kemenag.py` dan metadata dalam `data/` memungkinkan tabel
awal diambil ulang. Lihat [KEMENAG-SOURCE.md](KEMENAG-SOURCE.md) untuk provenance.
Konfigurasi dan cache bulan disimpan di NVS ESP32. Kelangsungan NVS saat Stop/Start
simulator bergantung pada cara Wokwi memulai ulang flash; bedakan reset ESP32
virtual dengan membuat instance simulasi baru.

## API lokal

| Metode | Endpoint | Kegunaan |
|---|---|---|
| GET | `/api/v1/config` | Konfigurasi, revisi, dan token CSRF sesi perangkat |
| PUT | `/api/v1/config` | Simpan konfigurasi valid dengan revisi yang masih sesuai |
| GET | `/api/v1/status` | Clock, jadwal dasar/akhir, sumber, dan hasil sinkronisasi |
| POST | `/api/v1/sync` | Antrekan pengambilan ulang tabel Kemenag |

PUT dan POST memerlukan header `X-JWS-Token` dari GET konfigurasi. Revisi yang
berubah pada perangkat lain menghasilkan 409 agar perubahan tidak tertimpa.
Body JSON dan tipe/rentang dibatasi; permintaan tulis dibatasi frekuensinya.
Tidak ada CORS terbuka. Perlindungan CSRF bukan sistem akun/autentikasi jarak
jauh: endpoint ini ditujukan untuk jaringan lokal simulasi yang dipercaya.
Publikasi ke internet dan pengamanan production bukan bagian tahap ini.

## Preview antarmuka

File `web/` adalah source dashboard yang dibundel gzip ke flash oleh
`scripts/embed_dashboard.py` saat build. Mode `?demo=1` hanya preview antarmuka
dengan data contoh berlabel demo; tidak mengubah ESP32 dan tidak dianggap bukti
bahwa API perangkat atau simulator berjalan.

Pengujian native memeriksa tabel resmi, koreksi, penggantian konfigurasi, dan
kegagalan penyimpanan. Build firmware, browser preview, dan eksekusi Wokwi tetap
memiliki status verifikasi masing-masing di [STATUS.md](STATUS.md).
