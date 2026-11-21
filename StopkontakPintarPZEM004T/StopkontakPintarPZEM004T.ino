/*
  Stopkontak Pintar PZEM-004T — ESP32 + PZEM-004T v3 + Relay + LCD I2C
  ========================================================================
  Stopkontak yang mengukur dayanya sendiri dan memutus beban secara
  otomatis begitu daya melewati batas aman. Cocok untuk melindungi
  stopkontak dari beban berlebih (mis. dispenser + setrika nyala bareng)
  sebelum kabel panas atau MCB rumah jeglek.

  Yang diukur PZEM-004T: tegangan (V), arus (A), daya (W), energi (kWh),
  frekuensi (Hz), dan faktor daya. Semua tampil di LCD 16x2.

  Perilaku penting:
  - Daya >= DAYA_MAKS_WATT -> relay LEPAS (beban putus), buzzer bunyi,
    LCD menampilkan "CUTOFF! Daya lebih", dan status TERKUNCI sampai
    tombol reset ditekan. Relay TIDAK nyambung sendiri meski daya sudah
    turun — sengaja, supaya pengguna memeriksa dulu bebannya.
  - Tombol reset: menolkan penghitung energi PZEM dan membuka kunci
    cutoff (relay nyambung lagi).
  - LED hijau menyala = relay ON, beban tersambung ke listrik.

  Peringatan keselamatan:
  - PZEM-004T terhubung LANGSUNG ke listrik PLN 220 V AC. Pasang hanya
    bila paham instalasi listrik; jangan sentuh terminal PZEM saat
    stopkontak tercolok. Modul ini memakai trafo arus (CT) internal,
    jadi sisi DC (ESP32) terisolasi dari sisi AC.
  - Relay yang dipakai WAJIB 10 A 250 V AC dan kabel stopkontak minimal
    1,5 mm2. Batas 1000 W di sketch ini di bawah kemampuan relay,
    sengaja sebagai pengaman tambahan.

  Library yang dipakai:
  - PZEM004Tv30 1.2.1 (Jakub Mandula) — komunikasi Serial ke PZEM-004T v3
  - LiquidCrystal_I2C 1.1.2 — LCD 16x2 via backpack PCF8574
  - UniversalTelegramBot 1.3.0 + ArduinoJson 6.21.5 (hanya bila
    USE_TELEGRAM 1) — kirim peringatan cutoff ke Telegram

  Board: ESP32 DevKit (esp32:esp32:esp32), core ESP32 4.0.0
*/

// ---------------- Konfigurasi: pin ----------------
const uint8_t PIN_PZEM_RX = 16;   // ESP32 RX2 <- TX modul PZEM-004T
const uint8_t PIN_PZEM_TX = 17;   // ESP32 TX2 -> RX modul PZEM-004T
const uint8_t PIN_RELAY = 26;     // IN relay, aktif LOW
const uint8_t PIN_BUZZER = 14;    // buzzer aktif HIGH
const uint8_t PIN_TOMBOL_RESET = 13;  // tombol reset energi, aktif LOW
const uint8_t PIN_LED_STATUS = 27;    // LED hijau: ON = beban tersambung
const uint8_t PIN_LCD_SDA = 21;   // SDA LCD I2C
const uint8_t PIN_LCD_SCL = 22;   // SCL LCD I2C

// ---------------- Konfigurasi: batas & waktu ----------------
const float DAYA_MAKS_WATT = 1000.0;   // batas auto-cutoff (watt)
const unsigned long INTERVAL_BACA_MS = 1000;  // jeda baca PZEM + update LCD
const unsigned long DURASI_BUZZER_CUTOFF_MS = 3000;  // buzzer saat cutoff
const unsigned long DURASI_BIP_TOMBOL_MS = 150;      // bip pendek saat reset
const unsigned long DEBOUNCE_TOMBOL_MS = 50;         // debounce tombol reset
const uint8_t ALAMAT_LCD_I2C = 0x27;   // alamat backpack LCD I2C
const unsigned long BAUD_SERIAL = 115200;

// ---------------- Konfigurasi: Telegram (opsional) ----------------
// 1 = kirim peringatan cutoff ke Telegram, 0 = tanpa Telegram.
// Bila 1, isi WIFI_SSID, WIFI_PASS, BOT_TOKEN, dan CHAT_ID di bawah,
// lalu sambungkan ESP32 ke WiFi. Tanpa Telegram alat tetap jalan penuh.
#define USE_TELEGRAM 0
#if USE_TELEGRAM
const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const char *BOT_TOKEN = "TOKEN_BOT_TELEGRAM";
const char *CHAT_ID = "CHAT_ID_TELEGRAM";
#endif

#include <PZEM004Tv30.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#if USE_TELEGRAM
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#endif

// ---------------- Objek global ----------------
PZEM004Tv30 pzem(Serial2, PIN_PZEM_RX, PIN_PZEM_TX);
LiquidCrystal_I2C lcd(ALAMAT_LCD_I2C, 16, 2);

#if USE_TELEGRAM
WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);
#endif

// ---------------- State ----------------
struct Pengukuran {
  float tegangan;   // volt
  float arus;       // ampere
  float daya;       // watt
  float energi;     // kWh
  float frekuensi;  // Hz
  float faktorDaya; // 0..1
  bool valid;       // false bila PZEM tidak merespons (NaN)
};

bool cutoffAktif = false;        // true = beban diputus karena daya lebih
bool tombolSebelumnya = HIGH;    // state tombol sebelumnya (debounce)
unsigned long waktuTombolTerakhirMs = 0;
unsigned long waktuBacaTerakhirMs = 0;

// ---------------- Fungsi: baca PZEM ----------------
Pengukuran bacaDaya() {
  Pengukuran ukur;
  ukur.tegangan = pzem.voltage();
  ukur.arus = pzem.current();
  ukur.daya = pzem.power();
  ukur.energi = pzem.energy();
  ukur.frekuensi = pzem.frequency();
  ukur.faktorDaya = pzem.pf();
  // Library mengembalikan NaN bila modul tidak merespons.
  ukur.valid = !isnan(ukur.tegangan) && !isnan(ukur.daya);
  return ukur;
}

// ---------------- Fungsi: relay & LED ----------------
void sambungBeban() {
  digitalWrite(PIN_RELAY, LOW);   // relay aktif LOW -> ON
  digitalWrite(PIN_LED_STATUS, HIGH);
}

void putusBeban() {
  digitalWrite(PIN_RELAY, HIGH);  // relay OFF
  digitalWrite(PIN_LED_STATUS, LOW);
}

// ---------------- Fungsi: buzzer ----------------
void bunyiBuzzer(unsigned long durasiMs) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(durasiMs);
  digitalWrite(PIN_BUZZER, LOW);
}

// ---------------- Fungsi: LCD ----------------
// Tambah spasi di ujung sampai 16 karakter, supaya sisa tulisan lama
// di LCD tertutup saat baris baru lebih pendek dari baris sebelumnya.
String padKe16(const char *teks) {
  String hasil = teks;
  while (hasil.length() < 16) {
    hasil += ' ';
  }
  return hasil;
}

void tampilkanNormal(const Pengukuran &ukur) {
  char barisPertama[17];
  char barisKedua[17];
  // Baris 1: daya + arus, mis. "187W 0.85A"
  snprintf(barisPertama, sizeof(barisPertama), "%.0fW %.2fA",
           ukur.daya, ukur.arus);
  // Baris 2: tegangan + energi, mis. "220V 0.123kWh"
  snprintf(barisKedua, sizeof(barisKedua), "%.0fV %.3fkWh",
           ukur.tegangan, ukur.energi);
  lcd.setCursor(0, 0);
  lcd.print(padKe16(barisPertama));
  lcd.setCursor(0, 1);
  lcd.print(padKe16(barisKedua));
}

void tampilkanCutoff(float dayaTerukur) {
  lcd.setCursor(0, 0);
  lcd.print("CUTOFF! Daya lbh");
  lcd.setCursor(0, 1);
  // Mis. "1234W > 1000W" (13 karakter, muat di LCD 16x2).
  String barisKedua = String((int)dayaTerukur);
  barisKedua += "W > ";
  barisKedua += String((int)DAYA_MAKS_WATT);
  barisKedua += "W";
  lcd.print(padKe16(barisKedua.c_str()));
}

void tampilkanPzemTidakRespon() {
  lcd.setCursor(0, 0);
  lcd.print("PZEM tdk respon ");
  lcd.setCursor(0, 1);
  lcd.print("Cek kabel TX/RX ");
}

// ---------------- Fungsi: Serial Monitor ----------------
void kirimSerial(const Pengukuran &ukur) {
  Serial.print("V=");
  Serial.print(ukur.tegangan, 1);
  Serial.print("V I=");
  Serial.print(ukur.arus, 2);
  Serial.print("A P=");
  Serial.print(ukur.daya, 0);
  Serial.print("W E=");
  Serial.print(ukur.energi, 3);
  Serial.print("kWh f=");
  Serial.print(ukur.frekuensi, 1);
  Serial.print("Hz pf=");
  Serial.print(ukur.faktorDaya, 2);
  Serial.print(" cutoff=");
  Serial.println(cutoffAktif ? "YA" : "TIDAK");
}

// ---------------- Fungsi: Telegram (opsional) ----------------
#if USE_TELEGRAM
void sambungWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Menghubungkan WiFi");
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi tersambung.");
    telegramClient.setInsecure();  // tanpa verifikasi sertifikat (bot API)
  } else {
    Serial.println("WiFi GAGAL tersambung, peringatan Telegram dimatikan.");
  }
}

void kirimPeringatanTelegram(float dayaTerukur) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  String pesan = "Stopkontak pintar: CUTOFF! Daya terukur ";
  pesan += String((int)dayaTerukur);
  pesan += " W melewati batas ";
  pesan += String((int)DAYA_MAKS_WATT);
  pesan += " W. Beban diputus, tekan tombol reset untuk menyambung lagi.";
  bot.sendMessage(CHAT_ID, pesan, "");
}
#endif

// ---------------- Fungsi: cutoff & reset ----------------
void aktifkanCutoff(float dayaTerukur) {
  cutoffAktif = true;
  putusBeban();
  tampilkanCutoff(dayaTerukur);
  Serial.print("CUTOFF! Daya ");
  Serial.print(dayaTerukur, 0);
  Serial.print(" W melewati batas ");
  Serial.print(DAYA_MAKS_WATT, 0);
  Serial.println(" W. Beban diputus.");
  bunyiBuzzer(DURASI_BUZZER_CUTOFF_MS);
#if USE_TELEGRAM
  kirimPeringatanTelegram(dayaTerukur);
#endif
}

void resetEnergiDanBukaKunci() {
  if (pzem.resetEnergy()) {
    Serial.println("Penghitung energi PZEM direset ke nol.");
  } else {
    Serial.println("Reset energi PZEM GAGAL (modul tidak merespons).");
  }
  cutoffAktif = false;
  sambungBeban();
  bunyiBuzzer(DURASI_BIP_TOMBOL_MS);
  Serial.println("Kunci cutoff dibuka, beban tersambung lagi.");
}

// Cek tombol reset tiap loop dengan debounce sederhana.
void cekTombolReset() {
  bool tombolSekarang = digitalRead(PIN_TOMBOL_RESET);
  if (tombolSekarang != tombolSebelumnya &&
      millis() - waktuTombolTerakhirMs > DEBOUNCE_TOMBOL_MS) {
    waktuTombolTerakhirMs = millis();
    if (tombolSekarang == LOW) {  // tombol ditekan (aktif LOW)
      resetEnergiDanBukaKunci();
    }
  }
  tombolSebelumnya = tombolSekarang;
}

// ---------------- setup & loop ----------------
void setup() {
  Serial.begin(BAUD_SERIAL);
  Serial.println("Stopkontak Pintar PZEM-004T mulai.");

  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_TOMBOL_RESET, INPUT_PULLUP);
  digitalWrite(PIN_BUZZER, LOW);

  // PZEM-004T memakai Serial2 (hardware) 9600 baud; library
  // PZEM004Tv30 mengatur baud sendiri dari konstruktor.
  Serial2.begin(PZEM_BAUD_RATE, SERIAL_8N1, PIN_PZEM_RX, PIN_PZEM_TX);

  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Stopkontak");
  lcd.setCursor(0, 1);
  lcd.print("PZEM-004T siap");

  sambungBeban();  // beban tersambung saat alat dinyalakan
  Serial.print("Batas daya: ");
  Serial.print(DAYA_MAKS_WATT, 0);
  Serial.println(" W. Tekan tombol reset untuk menolkan energi.");

#if USE_TELEGRAM
  sambungWiFi();
#endif
  delay(1500);
  lcd.clear();
}

void loop() {
  cekTombolReset();

  if (millis() - waktuBacaTerakhirMs < INTERVAL_BACA_MS) {
    return;
  }
  waktuBacaTerakhirMs = millis();

  Pengukuran ukur = bacaDaya();

  if (!ukur.valid) {
    tampilkanPzemTidakRespon();
    Serial.println("PZEM tidak merespons, cek kabel TX/RX dan catu 5V.");
    return;
  }

  kirimSerial(ukur);

  if (cutoffAktif) {
    // Tetap tampilkan layar cutoff sampai tombol reset ditekan.
    tampilkanCutoff(ukur.daya);
    return;
  }

  tampilkanNormal(ukur);

  if (ukur.daya >= DAYA_MAKS_WATT) {
    aktifkanCutoff(ukur.daya);
  }
}
