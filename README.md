# Stopkontak Pintar PZEM-004T — ESP32 + Relay + LCD I2C

Stopkontak yang mengukur dayanya sendiri dan memutus beban secara
otomatis begitu daya melewati batas aman. Begitu dispenser dan setrika
menyala bareng dan daya total melewati 1000 watt, relay langsung lepas
sebelum kabel sempat panas atau MCB rumah jeglek. Tegangan, arus, daya,
dan energi tampil di LCD 16x2, dan tombol reset menolkan penghitung
energi sekaligus membuka kunci cutoff.

Video penjelasan (60 detik): link menyusul setelah upload

> **Peringatan keselamatan:** PZEM-004T terhubung LANGSUNG ke listrik
> PLN 220 V AC. Pasang hanya bila paham instalasi listrik; jangan sentuh
> terminal PZEM saat stopkontak tercolok. Relay yang dipakai WAJIB
> 10 A 250 V AC dan kabel stopkontak minimal 1,5 mm2.

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| Modul PZEM-004T v3.0 | Pengukur tegangan, arus, daya, energi, frekuensi, faktor daya |
| Relay 1 channel | 10 A 250 V AC, aktif LOW — memutus beban |
| LCD 16x2 I2C (0x27) | Tampil daya (W), arus (A), tegangan (V), energi (kWh) |
| Buzzer aktif | Bunyi 3 detik saat cutoff |
| Tombol push button | Reset energi + buka kunci cutoff |
| LED hijau + resistor 220 Ω | Menyala = relay ON, beban tersambung |

## Wiring

| Dari | Ke |
|---|---|
| PZEM-004T TX | ESP32 GPIO16 (RX2) |
| PZEM-004T RX | ESP32 GPIO17 (TX2) |
| PZEM-004T VCC / GND | 5V (VIN) / GND |
| PLN L / N | PZEM-004T L IN / N IN |
| PZEM-004T L OUT | Relay COM |
| PZEM-004T N OUT | Stopkontak N |
| Relay NO | Stopkontak L |
| Relay IN | ESP32 GPIO26 (aktif LOW) |
| Relay VCC / GND | 5V (VIN) / GND |
| LCD I2C SDA | ESP32 GPIO21 |
| LCD I2C SCL | ESP32 GPIO22 |
| Buzzer + | ESP32 GPIO14 (aktif HIGH) |
| Buzzer − | GND |
| Tombol reset | ESP32 GPIO13 (INPUT_PULLUP, aktif LOW), kaki lain ke GND |
| LED hijau | ESP32 GPIO27 via resistor 220 Ω, katoda ke GND |

## Cara Kerja

1. Setiap 1 detik ESP32 membaca tegangan, arus, daya, dan energi dari
   PZEM-004T lewat Serial2 (9600 baud) dan menampilkannya di LCD —
   baris 1 daya + arus, baris 2 tegangan + energi — sekaligus mengirim
   semuanya ke Serial Monitor (115200 baud).
2. Begitu daya terukur mencapai `DAYA_MAKS_WATT` (1000 W), ESP32
   mengaktifkan cutoff: relay dilepas (beban putus), buzzer berbunyi
   selama 3 detik, LCD menampilkan `CUTOFF! Daya lebih`, dan status
   cutoff TERKUNCI.
3. Kunci cutoff sengaja tidak membuka sendiri meski daya sudah turun —
   pengguna harus memeriksa dulu bebannya, baru menekan tombol reset.
4. Tombol reset memanggil `resetEnergy()` pada PZEM (penghitung kWh
   kembali ke nol), membuka kunci cutoff, dan menyambung lagi beban.
5. LED hijau menyala selama relay ON (beban tersambung) dan padam saat
   cutoff.
6. Kalau PZEM tidak merespons (kabel TX/RX lepas atau catu 5V putus),
   LCD menampilkan `PZEM tdk respon` dan ESP32 tidak mengubah relay.
7. Opsional: bila `USE_TELEGRAM` 1 dan WiFi tersambung, setiap cutoff
   juga dikirim sebagai pesan Telegram.

## Konfigurasi

Ubah di bagian atas sketch
(`StopkontakPintarPZEM004T/StopkontakPintarPZEM004T.ino`):

| Konstanta | Nilai | Keterangan |
|---|---|---|
| `DAYA_MAKS_WATT` | `1000.0` | Batas auto-cutoff (watt) |
| `INTERVAL_BACA_MS` | `1000` | Jeda baca PZEM + update LCD |
| `DURASI_BUZZER_CUTOFF_MS` | `3000` | Lama buzzer bunyi saat cutoff |
| `ALAMAT_LCD_I2C` | `0x27` | Alamat I2C modul LCD |
| `PIN_PZEM_RX` / `PIN_PZEM_TX` | `16` / `17` | Serial2 ESP32 ke PZEM-004T |
| `USE_TELEGRAM` | `0` | 1 = kirim peringatan cutoff ke Telegram, 0 = tanpa Telegram. Isi `WIFI_SSID`, `WIFI_PASS`, `BOT_TOKEN`, dan `CHAT_ID` bila Telegram aktif. |

## Compile

| Item | Nilai |
|---|---|
| Board | `esp32:esp32:esp32` |
| Core ESP32 | 4.0.0 |
| Program (`USE_TELEGRAM` 0) | 318.857 byte (24% dari flash) |
| RAM global (`USE_TELEGRAM` 0) | 24.632 byte (7%) |
| Program (`USE_TELEGRAM` 1) | 1.081.553 byte (82% dari flash) |
| RAM global (`USE_TELEGRAM` 1) | 51.632 byte (15%) |
| Library PZEM | PZEM004Tv30 1.2.1 (Jakub Mandula) |
| Library LCD | LiquidCrystal_I2C 1.1.2 |

Status: angka di atas berasal dari compile uji memakai replika
antarmuka library PZEM004Tv30 (stub) yang dibuat dari header aslinya —
sintaks sketch dan pemakaian API-nya sudah lolos compile di ESP32 core
4.0.0, termasuk jalur `USE_TELEGRAM` 1. Compile penuh memakai library
PZEM004Tv30 asli masih menunggu persetujuan (pemeriksaan risiko
eksekusi); angkanya bisa bergeser sedikit dari tabel di atas.

## Library

Folder `libraries/` berisi salinan library yang dipakai, dalam bentuk
ZIP (folder library di top-level zip). Instal lewat Arduino IDE:
**Sketch → Include Library → Add .ZIP Library…**

| File | Library |
|---|---|
| `libraries/LiquidCrystal_I2C.zip` | LiquidCrystal_I2C 1.1.2 |
| `libraries/UniversalTelegramBot.zip` | UniversalTelegramBot 1.3.0 (hanya bila `USE_TELEGRAM` 1) |
| `libraries/ArduinoJson.zip` | ArduinoJson 6.21.5 (hanya bila `USE_TELEGRAM` 1) |

**PZEM004Tv30 tidak disertakan sebagai ZIP** — instal langsung dari
Library Manager: `arduino-cli lib install "PZEM004Tv30@1.2.1"` atau
lewat Arduino IDE (**Sketch → Include Library → Manage Libraries…**,
cari `PZEM004Tv30`, pasang versi 1.2.1).

## Skema

Lihat `schematic.png` (skema Fritzing: PLN 220V, PZEM-004T, ESP32,
relay, LCD I2C, buzzer, tombol reset, LED).

## Lisensi

MIT — lihat `LICENSE`.
