#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESP32Servo.h>

// Kalau WiFi sudah ada di secrets.h, hapus dua baris ini dan #include "secrets.h"
const char* WIFI_SSID = "ini wifi";
const char* WIFI_PASS = "kokpanggah";

const int SERVO_PIN = 13;

// Posisi servo: sesuaikan dengan mekanisme pintu/gerbang kamu
const int CLOSED_ANGLE = 0;
const int OPEN_ANGLE = 90;
const unsigned long OPEN_HOLD_MS = 2000;  // berapa lama tetap terbuka sebelum menutup lagi

// MAC override untuk chip dengan eFuse MAC kosong (sama seperti di Desk Buddy)
uint8_t CUSTOM_MAC[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};

Servo servo;
WebServer server(80);

// ---------- Gerakan: buka sekali, tahan, tutup sekali (non-blocking) ----------
const unsigned long SETTLE_MS = 500;  // waktu servo kembali ke posisi tutup sebelum request baru diterima

bool busy = false;
bool isOpen = false;
unsigned long closeAt = 0;
unsigned long freeAt = 0;

bool startMove() {
    if (busy) return false;  // masih proses, abaikan request baru
    busy = true;
    isOpen = true;
    servo.write(OPEN_ANGLE);
    closeAt = millis() + OPEN_HOLD_MS;
    return true;
}

void updateMove() {
    if (!busy) return;
    unsigned long now = millis();
    if (isOpen && (long)(now - closeAt) >= 0) {
        servo.write(CLOSED_ANGLE);
        isOpen = false;
        freeAt = now + SETTLE_MS;
    } else if (!isOpen && (long)(now - freeAt) >= 0) {
        busy = false;
    }
}

// ---------- Halaman web ----------
const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Desk Buddy</title>
  <style>
    body { font-family: sans-serif; text-align: center; margin-top: 20vh; }
    button { font-size: 2rem; padding: 1rem 2.5rem; border-radius: 12px; }
  </style>
</head>
<body>
  <button onclick="hit()">Buka</button>
  <p><small>atau tekan Spasi</small></p>
  <p id="s"></p>
  <script>
    async function hit() {
      try {
        const r = await fetch('/hit');
        const j = await r.json();
        document.getElementById('s').textContent = j.ok ? 'terbuka' : 'masih proses, tunggu';
      } catch (e) {
        document.getElementById('s').textContent = 'gagal konek';
      }
    }
    // Tombol Spasi = buka. Abaikan tahan-tekan (repeat) dan cegah scroll halaman
    document.addEventListener('keydown', (e) => {
      if (e.code === 'Space') {
        e.preventDefault();
        if (!e.repeat) hit();
      }
    });
    // Cegah tombol yang sedang fokus ikut ter-klik oleh Spasi (supaya tidak dobel)
    document.addEventListener('keyup', (e) => {
      if (e.code === 'Space') e.preventDefault();
    });
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send_P(200, "text/html", PAGE);
}

void handleHit() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    bool ok = startMove();
    server.send(ok ? 200 : 429, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"busy\":true}");
}

// ---------- Debug WiFi: cetak alasan putus ----------
// 201 = SSID tidak ketemu | 202/15/204 = password salah atau handshake gagal
// 205 = gagal konek | 200 = sinyal lemah (beacon timeout) | 2/3/8 = diputus oleh router
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        Serial.printf("\n[WiFi] putus, reason=%d\n", info.wifi_sta_disconnected.reason);
    }
}

// ---------- Setup / loop ----------
void setup() {
    Serial.begin(115200);
    delay(1000);

    servo.attach(SERVO_PIN, 500, 2400);
    servo.write(CLOSED_ANGLE);

    uint8_t base[6];
    esp_err_t macErr = esp_base_mac_addr_get(base);

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    delay(300);

    if (macErr == ESP_ERR_INVALID_MAC) {
        esp_err_t setErr = esp_wifi_set_mac(WIFI_IF_STA, CUSTOM_MAC);
        Serial.printf("[MAC] eFuse kosong, override %s\n", setErr == ESP_OK ? "OK" : "GAGAL");
    }

    WiFi.onEvent(onWifiEvent);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        if (++tries >= 60) {  // 30 detik
            Serial.printf("\n[WiFi] gagal, status=%d, restart...\n", (int)WiFi.status());
            ESP.restart();
        }
    }
    Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

    if (MDNS.begin("deskbuddy")) {
        Serial.println("Buka: http://deskbuddy.local");
    }

    server.on("/", handleRoot);
    server.on("/hit", handleHit);
    server.begin();
}

void loop() {
    server.handleClient();
    updateMove();
}