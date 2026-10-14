
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 21
#define OLED_SCL 22

#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1
);

// Animation variables
unsigned long lastBlink = 0;
unsigned long lastLook = 0;
unsigned long lastFrame = 0;

bool isBlinking = false;
int lookDirection = 0;

const unsigned long BLINK_INTERVAL = 3000;
const unsigned long BLINK_DURATION = 180;
const unsigned long LOOK_INTERVAL = 1800;
const unsigned long FRAME_INTERVAL = 50;

// Draw robot eyes
void drawEyes() {
    display.clearDisplay();

    // Header
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(27, 2);
    display.println("Sidi Smith Miaw");

    if (isBlinking) {
        // Closed eyes
        display.drawFastHLine(25, 39, 35, SSD1306_WHITE);
        display.drawFastHLine(68, 39, 35, SSD1306_WHITE);
    } else {
        // Eye positions
        int leftX = 43;
        int rightX = 85;
        int eyeY = 39;

        // Eye whites
        display.fillCircle(leftX, eyeY, 13, SSD1306_WHITE);
        display.fillCircle(rightX, eyeY, 13, SSD1306_WHITE);

        // Pupils move left and right
        int pupilOffset = lookDirection * 4;

        display.fillCircle(
            leftX + pupilOffset,
            eyeY,
            5,
            SSD1306_BLACK
        );

        display.fillCircle(
            rightX + pupilOffset,
            eyeY,
            5,
            SSD1306_BLACK
        );
    }

    display.display();
}

void setup() {
    Serial.begin(115200);

    // Initialize I2C
    Wire.begin(OLED_SDA, OLED_SCL);

    // Initialize OLED
    if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDRESS
    )) {
        Serial.println("OLED initialization failed!");

        while (true) {
            delay(100);
        }
    }

    Serial.println("OLED initialized!");
    Serial.println("Desk Buddy is online.");

    display.clearDisplay();
    display.display();

    lastBlink = millis();
    lastLook = millis();

    drawEyes();
}

void loop() {
    unsigned long now = millis();

    // Blink animation
    if (!isBlinking && now - lastBlink >= BLINK_INTERVAL) {
        isBlinking = true;
        lastBlink = now;
    }

    if (isBlinking && now - lastBlink >= BLINK_DURATION) {
        isBlinking = false;
    }

    // Look left and right
    if (!isBlinking && now - lastLook >= LOOK_INTERVAL) {
        lookDirection++;

        if (lookDirection > 1) {
            lookDirection = -1;
        }

        lastLook = now;
    }

    // Refresh animation
    if (now - lastFrame >= FRAME_INTERVAL) {
        drawEyes();
        lastFrame = now;
    }
}
