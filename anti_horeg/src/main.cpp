#include <RF24.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_bt_main.h>

// ============================================================
// KONFIGURASI
// ============================================================
#define CE_A  16
#define CSN_A 15
#define CE_B  22
#define CSN_B 21

// SPI speed. 19909090 = ~19.9MHz (di atas spec, tapi di modul
// tertentu tetap jalan). Spec datasheet = max 10MHz.
// Kalau modul gagal init, coba turunkan ke 8000000 atau 4000000.
#define SPI_SPEED 19909090

// Interval report ke serial (ms)
#define REPORT_INTERVAL_MS 1000

// ============================================================
// OBJECTS
// ============================================================
SPIClass *hspi = nullptr;
SPIClass *vspi = nullptr;
RF24 radioA(CE_A, CSN_A, SPI_SPEED);
RF24 radioB(CE_B, CSN_B, SPI_SPEED);

// ============================================================
// STATE
// ============================================================
int hspiChannel = 45;
int vspiChannel = 45;
unsigned int hspiDir = 0;   // 0=naik, 1=turun
unsigned int vspiDir = 0;

// ============================================================
// STATISTIK
// ============================================================
unsigned long loopsThisSec     = 0;
unsigned long sweepsThisSec    = 0;
unsigned long randThisSec      = 0;
unsigned long lastReport       = 0;

unsigned long totalLoops       = 0;
unsigned long hspiFlips        = 0;
unsigned long vspiFlips        = 0;

// ============================================================
// LOGGER HELPERS
// ============================================================
void banner(const char* title) {
  Serial.println();
  Serial.println(F("========================================"));
  Serial.print(F(">>> "));
  Serial.println(title);
  Serial.println(F("========================================"));
}

void sub(const char* msg) {
  Serial.print(F("    - "));
  Serial.println(msg);
}

void radioConfig(const char* name, RF24 &r) {
  Serial.print(F("    "));
  Serial.print(name);
  Serial.print(F(" | connected="));
  Serial.print(r.isChipConnected() ? "YES" : "NO");
  Serial.print(F(" | ch="));
  Serial.print(r.getChannel());
  Serial.print(F(" | PA="));
  Serial.print(r.getPALevel());
  Serial.print(F(" | DR="));
  Serial.print(r.getDataRate());
  Serial.print(F(" | CRC="));
  Serial.println(r.getCRCLength());
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  banner("BOOT");
  Serial.print(F("Chip     : ")); Serial.println(ESP.getChipModel());
  Serial.print(F("CPU      : ")); Serial.print(ESP.getCpuFreqMHz()); Serial.println(F(" MHz"));
  Serial.print(F("Heap     : ")); Serial.println(ESP.getFreeHeap());

  // ---------------------------------------------------------
  // STEP 1: matikan WiFi & BT internal
  // ---------------------------------------------------------
  banner("STEP 1: Matikan WiFi/BT internal");
  sub("esp_bt_controller_deinit()");
  esp_bt_controller_deinit();
  sub("esp_wifi_stop()");
  esp_wifi_stop();
  sub("esp_wifi_deinit()");
  esp_wifi_deinit();
  sub("esp_wifi_disconnect()");
  esp_wifi_disconnect();
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
    sub("bluedroid ENABLED -> deinit+disable");
    esp_bluedroid_deinit();
    esp_bluedroid_disable();
  } else {
    sub("bluedroid already disabled");
  }
  Serial.print(F("Heap setelah matikan WiFi/BT: "));
  Serial.println(ESP.getFreeHeap());

  // ---------------------------------------------------------
  // STEP 2: init HSPI (radio A)
  // ---------------------------------------------------------
  banner("STEP 2: Init HSPI (radio A)");
  hspi = new SPIClass(HSPI);
  hspi->begin();
  sub("HSPI bus started");

  bool okA = radioA.begin(hspi);
  Serial.print(F("    radioA.begin() = "));
  Serial.println(okA ? "TRUE" : "FALSE");

  if (okA) {
    sub("setAutoAck(false)");
    radioA.setAutoAck(false);
    sub("stopListening()");
    radioA.stopListening();
    sub("setRetries(0,0)");
    radioA.setRetries(0, 0);
    sub("setPayloadSize(32)");
    radioA.setPayloadSize(32);
    sub("setAddressWidth(5)");
    radioA.setAddressWidth(5);
    sub("setPALevel(MAX)");
    radioA.setPALevel(RF24_PA_MAX, true);
    sub("setDataRate(2MBPS)");
    radioA.setDataRate(RF24_2MBPS);
    sub("setCRCLength(DISABLED)");
    radioA.setCRCLength(RF24_CRC_DISABLED);
    sub("startConstCarrier(MAX, ch=45)");
    radioA.startConstCarrier(RF24_PA_MAX, hspiChannel);
    radioConfig("radioA", radioA);
  } else {
    Serial.println(F("    [!] FAIL. Cek: CE=16 CSN=15 SCK=14 MISO=12 MOSI=13"));
    Serial.println(F("    [!] Cek 3.3V stabil + kapasitor 10uF di VCC radio"));
  }

  // ---------------------------------------------------------
  // STEP 3: init VSPI (radio B)
  // ---------------------------------------------------------
  banner("STEP 3: Init VSPI (radio B)");
  vspi = new SPIClass(VSPI);
  vspi->begin();
  sub("VSPI bus started");

  bool okB = radioB.begin(vspi);
  Serial.print(F("    radioB.begin() = "));
  Serial.println(okB ? "TRUE" : "FALSE");

  if (okB) {
    sub("setAutoAck(false)");
    radioB.setAutoAck(false);
    sub("stopListening()");
    radioB.stopListening();
    sub("setRetries(0,0)");
    radioB.setRetries(0, 0);
    sub("setPayloadSize(32)");
    radioB.setPayloadSize(32);
    sub("setAddressWidth(5)");
    radioB.setAddressWidth(5);
    sub("setPALevel(MAX)");
    radioB.setPALevel(RF24_PA_MAX, true);
    sub("setDataRate(2MBPS)");
    radioB.setDataRate(RF24_2MBPS);
    sub("setCRCLength(DISABLED)");
    radioB.setCRCLength(RF24_CRC_DISABLED);
    sub("startConstCarrier(MAX, ch=45)");
    radioB.startConstCarrier(RF24_PA_MAX, vspiChannel);
    radioConfig("radioB", radioB);
  } else {
    Serial.println(F("    [!] FAIL. Cek: CE=22 CSN=21 SCK=18 MISO=19 MOSI=23"));
    Serial.println(F("    [!] Cek 3.3V stabil + kapasitor 10uF di VCC radio"));
  }

  // ---------------------------------------------------------
  // STEP 4: ringkasan
  // ---------------------------------------------------------
  banner("STEP 4: Siap loop");
  if (okA && okB)      Serial.println(F("    KEDUA radio AKTIF"));
  else if (okA)        Serial.println(F("    Hanya radio A aktif"));
  else if (okB)        Serial.println(F("    Hanya radio B aktif"));
  else                 Serial.println(F("    TIDAK ADA radio aktif - loop diam"));

  Serial.println(F("    Sweep terstruktur : A +-2, B +-4 (ch 2..79)"));
  Serial.println(F("    Random overwrite  : setelah sweeper, langsung ditimpa"));
  Serial.print(F("    Report interval   : "));
  Serial.print(REPORT_INTERVAL_MS);
  Serial.println(F(" ms"));
  Serial.println();

  lastReport = millis();
}

// ============================================================
// LOGIC
// ============================================================
void channelSweeper() {
  unsigned int prevHspiDir = hspiDir;
  unsigned int prevVspiDir = vspiDir;

  // B loncat 4
  if (vspiDir == 0) vspiChannel += 4;
  else              vspiChannel -= 4;

  // A loncat 2
  if (hspiDir == 0) hspiChannel += 2;
  else              hspiChannel -= 2;

  // Bounce batas
  if ((vspiChannel > 79) && (vspiDir == 0))      { vspiDir = 1; vspiFlips++; }
  else if ((vspiChannel < 2) && (vspiDir == 1))  { vspiDir = 0; vspiFlips++; }

  if ((hspiChannel > 79) && (hspiDir == 0))      { hspiDir = 1; hspiFlips++; }
  else if ((hspiChannel < 2) && (hspiDir == 1))  { hspiDir = 0; hspiFlips++; }

  radioA.setChannel(hspiChannel);
  radioB.setChannel(vspiChannel);
  sweepsThisSec++;

  // Log saat arah flip (tidak tiap loop, biar nggak spam)
  if (hspiDir != prevHspiDir) {
    Serial.print(F("[FLIP-A] "));
    Serial.print(hspiDir ? "TURUN" : "NAIK");
    Serial.print(F(" | ch="));
    Serial.println(hspiChannel);
  }
  if (vspiDir != prevVspiDir) {
    Serial.print(F("[FLIP-B] "));
    Serial.print(vspiDir ? "TURUN" : "NAIK");
    Serial.print(F(" | ch="));
    Serial.println(vspiChannel);
  }
}

void randomChannel() {
  radioA.setChannel(random(80));
  radioB.setChannel(random(80));
  delayMicroseconds(random(60));
  randThisSec++;
}

// ============================================================
// REPORT PERIODIK
// ============================================================
void reportStats() { 
  unsigned long now = millis();
  if (now - lastReport < REPORT_INTERVAL_MS) return;

  unsigned long dt = now - lastReport;
  lastReport = now;

  unsigned long lps = (loopsThisSec * 1000UL) / dt;
  unsigned long sps = (sweepsThisSec * 1000UL) / dt;
  unsigned long rps = (randThisSec * 1000UL) / dt;

  Serial.println(F("--- REPORT ---"));
  Serial.print(F("  uptime="));   Serial.print(now / 1000);
  Serial.print(F("s loop="));     Serial.print(loopsThisSec);
  Serial.print(F(" ("));          Serial.print(lps); Serial.print(F("/s)"));
  Serial.print(F(" sweep="));     Serial.print(sweepsThisSec);
  Serial.print(F(" ("));          Serial.print(sps); Serial.print(F("/s)"));
  Serial.print(F(" rand="));      Serial.print(randThisSec);
  Serial.print(F(" ("));          Serial.print(rps); Serial.println(F("/s)"));
  Serial.print(F("  A: ch="));    Serial.print(hspiChannel);
  Serial.print(F(" "));           Serial.print(hspiDir ? "TURUN" : "NAIK");
  Serial.print(F(" flips="));     Serial.print(hspiFlips);
  Serial.print(F("  |  B: ch=")); Serial.print(vspiChannel);
  Serial.print(F(" "));           Serial.print(vspiDir ? "TURUN" : "NAIK");
  Serial.print(F(" flips="));     Serial.print(vspiFlips);
  Serial.println();
  Serial.print(F("  heap="));     Serial.println(ESP.getFreeHeap());

  // reset counter per-detik
  loopsThisSec = 0;
  sweepsThisSec = 0;
  randThisSec = 0;
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  channelSweeper();
  randomChannel();
  loopsThisSec++;
  totalLoops++;
  reportStats();
}