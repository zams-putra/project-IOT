#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <secret.h>

#include "video_frame.h"

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDRESS 0x3C

// ---------- WiFi + waktu ----------
const char* WIFI_SSID = "ini wifi";
const char* WIFI_PASS = "kokpanggah";
const long GMT_OFFSET_SEC = 7 * 3600;
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";

// ---------- Cuaca ----------
const char* WEATHER_CITY = "Tulungagung";
const float WEATHER_LAT = -8.0654f;
const float WEATHER_LON = 111.9025f;
const unsigned long WEATHER_INTERVAL = 15UL * 60UL * 1000UL;
const unsigned long WEATHER_RETRY    = 60UL * 1000UL;

// ---------- Last.fm ----------
const unsigned long LASTFM_INTERVAL = 20UL * 1000UL;
const unsigned long LASTFM_RETRY    = 60UL * 1000UL;

// ---------- MAC override ----------
uint8_t CUSTOM_MAC[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0x01};

// ---------- Timing ----------
const unsigned long FACE_DURATION     = 10000;
const unsigned long CLOCK_DURATION    = 6000;
const unsigned long WEATHER_DURATION  = 6000;
const unsigned long MUSIC_DURATION    = 8000;
const unsigned long VIDEO_DURATION    = 12000;
const unsigned long FRAME_INTERVAL    = 50;
const unsigned long OLED_CHECK_INTERVAL = 1000;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

enum Mode { MODE_FACE, MODE_CLOCK, MODE_WEATHER, MODE_MUSIC, MODE_VIDEO };
Mode mode = MODE_FACE;
unsigned long modeStart = 0;
unsigned long lastFrame = 0;
unsigned long lastOledCheck = 0;
unsigned long lastHeartbeat = 0;
bool oledOk = false;
bool ntpStarted = false;

// ---------- Mood ----------
enum Mood { MOOD_NORMAL, MOOD_HAPPY, MOOD_SLEEPY, MOOD_HOT };
Mood mood = MOOD_NORMAL;
unsigned long moodUntil = 0;
unsigned long nextHappy = 0;
bool dimmed = false;

float pupilX = 0, pupilY = 0;
int targetX = 0, targetY = 0;
unsigned long nextLook = 0;
unsigned long nextBlink = 0;
unsigned long blinkStart = 0;
bool blinking = false;

// ---------- Cuaca ----------
enum WeatherKind { W_CLEAR, W_PARTLY, W_CLOUD, W_FOG, W_RAIN, W_STORM, W_SNOW };

struct WeatherData {
    bool valid = false;
    float temp = 0;
    int humidity = 0;
    int code = 0;
    bool isDay = true;
};
WeatherData weather;
unsigned long nextWeatherFetch = 0;

// ---------- Lagu ----------
struct TrackData {
    bool playing = false;
    char title[64] = "";
    char artist[48] = "";
};
TrackData track;
unsigned long nextLastfmFetch = 0;

const char* DAY_NAMES[]   = {"Min","Sen","Sel","Rab","Kam","Jum","Sab"};
const char* MONTH_NAMES[] = {"Jan","Feb","Mar","Apr","Mei","Jun",
                             "Jul","Agu","Sep","Okt","Nov","Des"};

// ---------- Helpers ----------
bool oledPresent() {
    Wire.beginTransmission(OLED_ADDRESS);
    return Wire.endTransmission() == 0;
}

// Satu-satunya tempat yang boleh mengubah contrast OLED.
// Kalau statusnya sama, tidak dikirim ulang (hemat I2C + cegah glitch).
void oledContrast(uint8_t c) {
    Wire.beginTransmission(OLED_ADDRESS);
    Wire.write(0x00);   // control byte: command stream
    Wire.write(0x81);   // SETCONTRAST
    Wire.write(c);
    Wire.endTransmission();
}

// Ganti definisi applyDim yang lama dengan ini:
void applyDim(bool want) {
    if (want == dimmed) return;
    dimmed = want;
    uint8_t c = want ? 0x30 : 0xFF;   // 0x30 = redup tapi jelas (dulu 0x10 = terlalu gelap)
    oledContrast(c);
    Serial.printf("[Dim] set -> %d (contrast=0x%02X)\n", want, c);
}

bool initOled() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false)) return false;
    display.dim(false);        // paksa contrast balik ke max
    display.clearDisplay();
    display.display();
    dimmed = false;
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

// ---------- Cuaca: kode ----------
WeatherKind weatherKind(int code) {
    if (code == 0) return W_CLEAR;
    if (code <= 2) return W_PARTLY;
    if (code == 3) return W_CLOUD;
    if (code == 45 || code == 48) return W_FOG;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return W_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return W_SNOW;
    if (code >= 95) return W_STORM;
    return W_CLOUD;
}

const char* weatherText(WeatherKind k) {
    switch (k) {
        case W_CLEAR:  return "Cerah";
        case W_PARTLY: return "Agak berawan";
        case W_CLOUD:  return "Berawan";
        case W_FOG:    return "Berkabut";
        case W_RAIN:   return "Hujan";
        case W_STORM:  return "Badai petir";
        case W_SNOW:   return "Salju";
    }
    return "-";
}

bool fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) return false;

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
             "&timezone=Asia%%2FJakarta",
             WEATHER_LAT, WEATHER_LON);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(4000);
    if (!http.begin(client, url)) {
        client.stop();
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[Weather] HTTP %d\n", httpCode);
        http.end();
        client.stop();
        return false;
    }
    String body = http.getString();
    http.end();
    client.stop();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[Weather] JSON error: %s\n", err.c_str());
        return false;
    }

    JsonObject cur = doc["current"];
    if (cur.isNull()) return false;

    weather.temp     = cur["temperature_2m"] | 0.0f;
    weather.humidity = cur["relative_humidity_2m"] | 0;
    weather.code     = cur["weather_code"] | 0;
    weather.isDay    = (cur["is_day"] | 1) == 1;
    weather.valid    = true;
    Serial.printf("[Weather] %.1fC, %d%%, code=%d\n",
                  weather.temp, weather.humidity, weather.code);
    return true;
}

// ---------- Cuaca: ikon ----------
void drawSun(int cx, int cy, int r) {
    display.fillCircle(cx, cy, r, SSD1306_WHITE);
    for (int i = 0; i < 8; i++) {
        float a = i * PI / 4.0f;
        int x1 = cx + (int)roundf(cosf(a) * (r + 3));
        int y1 = cy + (int)roundf(sinf(a) * (r + 3));
        int x2 = cx + (int)roundf(cosf(a) * (r + 7));
        int y2 = cy + (int)roundf(sinf(a) * (r + 7));
        display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
    }
}

void drawMoon(int cx, int cy) {
    display.fillCircle(cx, cy, 10, SSD1306_WHITE);
    display.fillCircle(cx + 6, cy - 4, 9, SSD1306_BLACK);
}

void drawCloud(int cx, int cy) {
    display.fillCircle(cx - 9, cy + 3, 6, SSD1306_WHITE);
    display.fillCircle(cx, cy - 2, 8, SSD1306_WHITE);
    display.fillCircle(cx + 9, cy + 3, 6, SSD1306_WHITE);
    display.fillRect(cx - 9, cy + 3, 19, 7, SSD1306_WHITE);
}

void drawWeatherIcon(WeatherKind k, bool isDay, int cx, int cy) {
    switch (k) {
        case W_CLEAR:
            if (isDay) drawSun(cx, cy, 7); else drawMoon(cx, cy);
            break;
        case W_PARTLY:
            if (isDay) drawSun(cx + 6, cy - 7, 5); else drawMoon(cx + 6, cy - 7);
            drawCloud(cx - 2, cy + 4);
            break;
        case W_CLOUD:
            drawCloud(cx, cy);
            break;
        case W_FOG:
            for (int i = 0; i < 4; i++) {
                int y = cy - 9 + i * 6;
                display.drawFastHLine(cx - 14 + (i % 2) * 4, y, 24, SSD1306_WHITE);
            }
            break;
        case W_RAIN:
            drawCloud(cx, cy - 6);
            for (int i = 0; i < 3; i++) {
                int x = cx - 8 + i * 8;
                display.drawLine(x, cy + 8, x - 2, cy + 14, SSD1306_WHITE);
            }
            break;
        case W_STORM:
            drawCloud(cx, cy - 6);
            display.fillTriangle(cx + 3, cy + 5, cx - 4, cy + 14, cx + 1, cy + 14, SSD1306_WHITE);
            display.fillTriangle(cx + 1, cy + 12, cx + 6, cy + 12, cx - 2, cy + 21, SSD1306_WHITE);
            break;
        case W_SNOW:
            drawCloud(cx, cy - 6);
            for (int i = 0; i < 3; i++) {
                display.fillCircle(cx - 8 + i * 8, cy + 11 + (i % 2) * 3, 1, SSD1306_WHITE);
            }
            break;
    }
}

void drawWeather() {
    applyDim(false);   // mode selain FACE selalu terang

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    char head[24];
    snprintf(head, sizeof(head), "Cuaca - %s", WEATHER_CITY);
    display.setCursor(0, 1);
    display.print(head);
    display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

    if (!weather.valid) {
        printCentered(WiFi.status() == WL_CONNECTED ? "Ambil cuaca..." : "Cari WiFi...", 28);
        display.display();
        return;
    }

    WeatherKind kind = weatherKind(weather.code);
    drawWeatherIcon(kind, weather.isDay, 24, 36);

    char buf[24];
    display.setTextSize(3);
    snprintf(buf, sizeof(buf), "%d", (int)roundf(weather.temp));
    display.setCursor(52, 16);
    display.print(buf);
    int tx = 52 + (int)strlen(buf) * 18 + 3;
    display.drawCircle(tx, 19, 2, SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(tx + 6, 16);
    display.print("C");

    display.setTextSize(1);
    display.setCursor(52, 44);
    display.print(weatherText(kind));
    snprintf(buf, sizeof(buf), "Lembab %d%%", weather.humidity);
    display.setCursor(52, 54);
    display.print(buf);
    display.display();
}

// ---------- Last.fm ----------
void sanitizeAscii(const char* in, char* out, size_t outSize) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < outSize - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= 32 && c < 127) out[j++] = (char)c;
    }
    out[j] = '\0';
}

bool fetchLastfm() {
    if (WiFi.status() != WL_CONNECTED) return false;

    char url[320];
    snprintf(url, sizeof(url),
             "https://ws.audioscrobbler.com/2.0/?method=user.getrecenttracks"
             "&user=%s&api_key=%s&format=json&limit=1",
             LASTFM_USER, LASTFM_API_KEY);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(4000);
    if (!http.begin(client, url)) {
        client.stop();
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[Lastfm] HTTP %d\n", httpCode);
        http.end();
        client.stop();
        return false;
    }
    String body = http.getString();
    http.end();
    client.stop();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[Lastfm] JSON error: %s\n", err.c_str());
        return false;
    }

    JsonVariant raw = doc["recenttracks"]["track"];
    JsonObject obj = raw.is<JsonArray>() ? raw[0].as<JsonObject>() : raw.as<JsonObject>();
    if (obj.isNull()) {
        track.playing = false;
        return true;
    }

    const char* flag = obj["@attr"]["nowplaying"] | "false";
    track.playing = (strcmp(flag, "true") == 0);
    if (track.playing) {
        sanitizeAscii(obj["name"] | "?", track.title, sizeof(track.title));
        sanitizeAscii(obj["artist"]["#text"] | "?", track.artist, sizeof(track.artist));
        if (track.title[0] == '\0') strcpy(track.title, "(judul non-ASCII)");
        Serial.printf("[Lastfm] Playing: %s - %s\n", track.artist, track.title);
    } else {
        Serial.println("[Lastfm] Tidak ada lagu yang diputar");
    }
    return true;
}

// ---------- Musik ----------
void drawScrollText(const char* s, int y, int size, unsigned long now) {
    int cw = 6 * size;
    int w = (int)strlen(s) * cw;
    display.setTextSize(size);
    if (w <= SCREEN_WIDTH) {
        display.setCursor((SCREEN_WIDTH - w) / 2, y);
        display.print(s);
        return;
    }
    int total = w + 3 * cw;
    int off = (int)((now / 30) % total);
    display.setCursor(-off, y);
    display.print(s);
    display.setCursor(-off + total, y);
    display.print(s);
}

void drawNote(int x, int y) {
    display.fillCircle(x, y + 6, 2, SSD1306_WHITE);
    display.drawFastVLine(x + 2, y, 7, SSD1306_WHITE);
    display.drawLine(x + 2, y, x + 5, y + 2, SSD1306_WHITE);
}

void drawMusic(unsigned long now) {
    applyDim(false);

    static float bars[16];
    static unsigned long lastBars = 0;

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    display.setTextSize(1);
    drawNote(2, 1);
    display.setCursor(12, 1);
    display.print("Now Playing");
    display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

    if (!track.playing) {
        printCentered("Lagi nggak muter lagu", 28);
        display.setTextWrap(true);
        display.display();
        return;
    }

    drawScrollText(track.title, 16, 2, now);
    drawScrollText(track.artist, 36, 1, now);

    if (now - lastBars >= 90) {
        lastBars = now;
        for (int i = 0; i < 16; i++) {
            bars[i] += (random(2, 13) - bars[i]) * 0.5f;
        }
    }
    for (int i = 0; i < 16; i++) {
        int bh = (int)bars[i];
        if (bh < 1) bh = 1;
        display.fillRect(i * 8, 64 - bh, 6, bh, SSD1306_WHITE);
    }

    display.setTextWrap(true);
    display.display();
}

// ---------- Video Frame ----------
void drawVideo(unsigned long now) {
    applyDim(false);   // video selalu terang

    static int frameIdx = 0;
    static unsigned long lastFrameChange = 0;
    static bool reported = false;

    if (!reported) {
        reported = true;
        Serial.printf("[Video] enter, TOTAL=%d DELAY=%d\n", TOTAL_FRAMES, FRAME_DELAY);
    }

    if (now - lastFrameChange >= FRAME_DELAY) {
        lastFrameChange = now;
        frameIdx++;
        if (frameIdx >= TOTAL_FRAMES) frameIdx = 0;
    }

    display.clearDisplay();
    display.drawBitmap(0, 0,
                       &video_frames[frameIdx][0],
                       128, 64,
                       SSD1306_WHITE);
    display.display();
}

// ---------- Face ----------
void updateMood(unsigned long now) {
    struct tm t;
    bool night = getTime(t) && (t.tm_hour >= 22 || t.tm_hour < 5);

    if (night) {
        mood = MOOD_SLEEPY;
    } else if (weather.valid && weather.temp >= 33.0f) {
        mood = MOOD_HOT;
    } else if (mood == MOOD_HAPPY && now < moodUntil) {
        // tetap happy
    } else if (now >= nextHappy) {
        mood = MOOD_HAPPY;
        moodUntil = now + random(2500, 4000);
        nextHappy = moodUntil + (track.playing ? random(5000, 10000) : random(12000, 25000));
    } else {
        mood = MOOD_NORMAL;
    }

    // HANYA di FACE, dim tergantung mood. Mode lain terang.
    applyDim(mood == MOOD_SLEEPY);
}

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

// Helper: gambar satu mata mengantuk (slit horizontal dengan ujung luar turun)
void drawSleepyEye(int cx, int cy) {
    // Slit mata (garis tebal membulat)
    display.fillRoundRect(cx - 13, cy - 2, 26, 5, 2, SSD1306_WHITE);
    // Kelopak atas: garis pendek di atas slit (kesan berat)
    for (int i = -2; i <= 2; i++) {
        int x = cx + i * 5;
        display.drawFastHLine(x, cy - 6, 3, SSD1306_WHITE);
    }
    // Kantung mata: garis lembut di bawah
    display.drawFastHLine(cx - 11, cy + 6, 22, SSD1306_WHITE);
}

// Helper: gambar "Zz" melayang naik-turun
void drawSleepingZz(unsigned long now) {
    int tick = (now / 350) % 4;         // 0..3
    int dy   = tick * 2;                 // 0,2,4,6 px naik
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(112, 18 + (6 - dy));      display.print("z");
    display.setCursor(106, 14 + (6 - dy));      display.print("Z");
    display.setCursor(100, 10 + (6 - dy));      display.print("Z");
}

void drawFace(unsigned long now) {
    updateMood(now);
    int h = updateEyes(now);
    // MOOD_HOT tetap pakai mekanisme lama
    if (mood == MOOD_HOT && h > 16) h = 16;
    // MOOD_SLEEPY tidak lagi pakai h, punya gambar sendiri

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    printCentered("Sidi Smith Miaw", 2);
    display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);

    const int cy = 36, leftX = 40, rightX = 88, w = 28;

    if (mood == MOOD_SLEEPY) {
        // ============ WAJAH MENGANTUK ============
        drawSleepyEye(leftX,  cy);
        drawSleepyEye(rightX, cy);

        // Mulut menguap kecil (oval)
        int mx = SCREEN_WIDTH / 2;
        display.fillRoundRect(mx - 4, cy + 12, 8, 6, 3, SSD1306_WHITE);
        display.fillRoundRect(mx - 2, cy + 13, 4, 4, 2, SSD1306_BLACK);

        // Zz melayang
        drawSleepingZz(now);

    } else {
        // ============ WAJAH NORMAL / HAPPY / HOT ============
        int r = h / 2 < 8 ? h / 2 : 8;
        display.fillRoundRect(leftX  - w / 2, cy - h / 2, w, h, r, SSD1306_WHITE);
        display.fillRoundRect(rightX - w / 2, cy - h / 2, w, h, r, SSD1306_WHITE);

        if (mood == MOOD_HAPPY && h > 14) {
            display.fillCircle(leftX,  cy + 14, 12, SSD1306_BLACK);
            display.fillCircle(rightX, cy + 14, 12, SSD1306_BLACK);
        } else if (h > 14) {
            int px = (int)roundf(pupilX);
            int py = (int)roundf(pupilY);
            display.fillCircle(leftX  + px, cy + py, 5, SSD1306_BLACK);
            display.fillCircle(rightX + px, cy + py, 5, SSD1306_BLACK);
        }
    }

    // Jam di bawah (selalu ada)
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
    applyDim(false);

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

// ---------- Mode rotation ----------
unsigned long modeDuration(Mode m) {
    switch (m) {
        case MODE_FACE:    return FACE_DURATION;
        case MODE_CLOCK:   return CLOCK_DURATION;
        case MODE_WEATHER: return WEATHER_DURATION;
        case MODE_MUSIC:   return MUSIC_DURATION;
        case MODE_VIDEO:   return VIDEO_DURATION;
    }
    return FACE_DURATION;
}

Mode nextMode(Mode m) {
    struct tm t;
    bool timeOk = getTime(t);
    switch (m) {
        case MODE_FACE:
            if (timeOk) return MODE_CLOCK;
            if (weather.valid) return MODE_WEATHER;
            if (track.playing) return MODE_MUSIC;
            return MODE_VIDEO;
        case MODE_CLOCK:
            if (weather.valid) return MODE_WEATHER;
            if (track.playing) return MODE_MUSIC;
            return MODE_VIDEO;
        case MODE_WEATHER:
            if (track.playing) return MODE_MUSIC;
            return MODE_VIDEO;
        case MODE_MUSIC:
            return MODE_VIDEO;
        case MODE_VIDEO:
            return MODE_FACE;
    }
    return MODE_FACE;
}

// ---------- Setup / loop ----------
void setup() {
    Serial.begin(115200);
    delay(1500);
    randomSeed(esp_random());

    // Reset reason — buat tahu kenapa boot: power-on, crash, brownout, dll.
    esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("[BOOT] reset_reason=%d heap=%u\n", (int)rr, ESP.getFreeHeap());
    // 1=POWERON  3=SW  4=PANIC  5/6=WDT  8=BROWNOUT

    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setTimeOut(50);   // cegah hang di bus I2C

    // ---- MAC ----
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
            Serial.printf("[MAC] Override FAIL err=%d\n", (int)setErr);
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
    nextHappy = now + 8000;
}

void loop() {
    unsigned long now = millis();

    // ---- Heartbeat tiap 5 detik: tahu ESP32 masih hidup atau tidak ----
    if (now - lastHeartbeat >= 5000) {
        lastHeartbeat = now;
        Serial.printf("[HB] t=%lus heap=%u mode=%d oledOk=%d dim=%d\n",
                      now / 1000, ESP.getFreeHeap(),
                      (int)mode, oledOk, dimmed);
    }

    // ---- NTP ----
    if (!ntpStarted && WiFi.status() == WL_CONNECTED) {
        configTime(GMT_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
        ntpStarted = true;
        Serial.printf("[NTP] started, IP=%s\n", WiFi.localIP().toString().c_str());
    }

    // ---- Fetch cuaca (skip saat layar cuaca) ----
    if (WiFi.status() == WL_CONNECTED && now >= nextWeatherFetch && mode != MODE_WEATHER) {
        bool ok = fetchWeather();
        nextWeatherFetch = millis() + (ok ? WEATHER_INTERVAL : WEATHER_RETRY);
    }

    // ---- Fetch lagu (skip saat layar musik) ----
    if (WiFi.status() == WL_CONNECTED && now >= nextLastfmFetch && mode != MODE_MUSIC) {
        bool ok = fetchLastfm();
        nextLastfmFetch = millis() + (ok ? LASTFM_INTERVAL : LASTFM_RETRY);
    }

    // ---- Cek OLED ----
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

    // ---- Ganti mode ----
    if (now - modeStart >= modeDuration(mode)) {
        mode = nextMode(mode);
        modeStart = now;
        Serial.printf("[Mode] -> %d\n", (int)mode);
        // Reset dim ke terang; FACE akan set ulang sesuai mood.
        applyDim(false);
    }

    // ---- Render ----
    if (now - lastFrame >= FRAME_INTERVAL) {
        lastFrame = now;
        switch (mode) {
            case MODE_FACE:    drawFace(now); break;
            case MODE_CLOCK:   drawClock();   break;
            case MODE_WEATHER: drawWeather(); break;
            case MODE_MUSIC:   drawMusic(now); break;
            case MODE_VIDEO:   drawVideo(now); break;
        }
    }
}