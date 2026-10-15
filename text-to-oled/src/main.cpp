#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDRESS 0x3C

// ---------- WiFi + waktu ----------
const char* WIFI_SSID = "[masukin nama wifi kalian]";
const char* WIFI_PASS = "[masukin password wifi kalian]";
const long GMT_OFFSET_SEC = 7 * 3600;
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";

// ---------- MAC override untuk chip eFuse kosong ----------
// Ganti byte terakhir (0x01) kalau punya >1 ESP32 cacat di rumah
uint8_t CUSTOM_MAC[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};

// ---------- Timing ----------
const unsigned long FACE_DURATION = 10000;
const unsigned long CLOCK_DURATION = 6000;
const unsigned long FRAME_INTERVAL = 50;
const unsigned long OLED_CHECK_INTERVAL = 1000;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

enum Mode { MODE_FACE, MODE_CLOCK };
Mode mode = MODE_FACE;
unsigned long modeStart = 0;
unsigned long lastFrame = 0;
unsigned long lastOledCheck = 0;
bool oledOk = false;
bool ntpStarted = false;

float pupilX = 0, pupilY = 0;
int targetX = 0, targetY = 0;
unsigned long nextLook = 0;
unsigned long nextBlink = 0;
unsigned long blinkStart = 0;
bool blinking = false;

const char* DAY_NAMES[] = {"Min", "Sen", "Sel", "Rab", "Kam", "Jum", "Sab"};
const char* MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "Mei", "Jun",
                             "Jul", "Agu", "Sep", "Okt", "Nov", "Des"};

// ---------- Helpers ----------
bool oledPresent() {
    Wire.beginTransmission(OLED_ADDRESS);
    return Wire.endTransmission() == 0;
}

bool initOled() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false)) return false;
    display.clearDisplay();
    display.display();
    return true;
}

bool getTime(struct tm &t) {
    time_t now = time(nullptr);
    if (now < 1700000000) return false;
    localtime_r(&now, &t);
    return true;
}

void printCentered(const char* text, int y) {
    int x = (SCREEN_WIDTH - (int)strlen(text) * 6) / 2;
    if (x < 0) x = 0;
    display.setCursor(x, y);
    display.print(text);
}

// ---------- Face ----------
int updateEyes(unsigned long now) {
    const int fullHeight = 26;
    if (now >= nextLook) {
        targetX = random(-1, 2) * 5;
        targetY = random(-1, 2) * 2;
        nextLook = now + random(1200, 3000);
    }
    pupilX += (targetX - pupilX) * 0.35f;
    pupilY += (targetY - pupilY) * 0.35f;

    if (!blinking && now >= nextBlink) {
        blinking = true;
        blinkStart = now;
    }
    if (!blinking) return fullHeight;

    unsigned long t = now - blinkStart;
    if (t >= 240) {
        blinking = false;
        nextBlink = now + random(2500, 5500);
        return fullHeight;
    }
    float closed = (t < 120) ? t / 120.0f : (240 - t) / 120.0f;
    return fullHeight - (int)(closed * (fullHeight - 2));
}

void drawFace(unsigned long now) {
    int h = updateEyes(now);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    printCentered("Sidi Smith Miaw", 2);
    display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);

    const int cy = 36, leftX = 40, rightX = 88, w = 28;
    int r = h / 2 < 8 ? h / 2 : 8;
    display.fillRoundRect(leftX - w / 2, cy - h / 2, w, h, r, SSD1306_WHITE);
    display.fillRoundRect(rightX - w / 2, cy - h / 2, w, h, r, SSD1306_WHITE);

    if (h > 14) {
        int px = (int)roundf(pupilX);
        int py = (int)roundf(pupilY);
        display.fillCircle(leftX + px, cy + py, 5, SSD1306_BLACK);
        display.fillCircle(rightX + px, cy + py, 5, SSD1306_BLACK);
    }

    struct tm t;
    if (getTime(t)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        printCentered(buf, 56);
    }
    display.display();
}

// ---------- Clock ----------
void drawClock() {
    struct tm t;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(0, 1);   display.print("Desk Buddy");
    display.setCursor(110, 1); display.print("WIB");
    display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

    if (!getTime(t)) {
        printCentered(WiFi.status() == WL_CONNECTED ? "Sync waktu..." : "Cari WiFi...", 28);
        display.display();
        return;
    }

    char buf[24];
    display.setTextSize(3);
    snprintf(buf, sizeof(buf), "%02d", t.tm_hour);
    display.setCursor(5, 16); display.print(buf);
    if (t.tm_sec % 2 == 0) { display.setCursor(41, 16); display.print(":"); }
    snprintf(buf, sizeof(buf), "%02d", t.tm_min);
    display.setCursor(59, 16); display.print(buf);

    display.setTextSize(2);
    snprintf(buf, sizeof(buf), "%02d", t.tm_sec);
    display.setCursor(99, 24); display.print(buf);

    display.setTextSize(1);
    snprintf(buf, sizeof(buf), "%s, %d %s %d", DAY_NAMES[t.tm_wday], t.tm_mday,
             MONTH_NAMES[t.tm_mon], t.tm_year + 1900);
    printCentered(buf, 50);
    display.display();
}

// ---------- Setup / loop ----------
void setup() {
    Serial.begin(115200);
    delay(1500);
    randomSeed(esp_random());

    Wire.begin(OLED_SDA, OLED_SCL);

    // ---- Cek MAC asli ----
    uint8_t base[6];
    esp_err_t macErr = esp_base_mac_addr_get(base);

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    delay(300);

    if (macErr == ESP_ERR_INVALID_MAC) {
        Serial.println("[MAC] eFuse kosong, applying override...");
        esp_err_t setErr = esp_wifi_set_mac(WIFI_IF_STA, CUSTOM_MAC);
        if (setErr == ESP_OK) {
            Serial.printf("[MAC] Override OK -> %s\n", WiFi.macAddress().c_str());
        } else {
            Serial.printf("[MAC] Override FAIL err=%d, WiFi dimatikan\n", (int)setErr);
            // tetap lanjut tanpa WiFi — jam akan tampil "Cari WiFi..."
        }
    } else {
        Serial.printf("[MAC] Valid: %s\n", WiFi.macAddress().c_str());
    }

    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    oledOk = oledPresent() && initOled();
    Serial.println(oledOk ? "OLED initialized!" : "OLED not found");

    unsigned long now = millis();
    modeStart = now;
    nextLook = now + 1500;
    nextBlink = now + 3000;
}

void loop() {
    unsigned long now = millis();

    if (!ntpStarted && WiFi.status() == WL_CONNECTED) {
        configTime(GMT_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
        ntpStarted = true;
        Serial.printf("[NTP] started, IP=%s\n", WiFi.localIP().toString().c_str());
    }

    if (now - lastOledCheck >= OLED_CHECK_INTERVAL) {
        lastOledCheck = now;
        bool present = oledPresent();
        if (present && !oledOk) {
            oledOk = initOled();
            Serial.println(oledOk ? "OLED reconnected" : "OLED init failed");
        } else if (!present && oledOk) {
            oledOk = false;
            Serial.println("OLED lost");
        }
    }

    if (!oledOk) { delay(10); return; }

    struct tm t;
    unsigned long duration = (mode == MODE_FACE) ? FACE_DURATION : CLOCK_DURATION;
    if (now - modeStart >= duration) {
        if (mode == MODE_FACE && getTime(t)) mode = MODE_CLOCK;
        else mode = MODE_FACE;
        modeStart = now;
    }

    if (now - lastFrame >= FRAME_INTERVAL) {
        lastFrame = now;
        if (mode == MODE_FACE) drawFace(now);
        else drawClock();
    }
}