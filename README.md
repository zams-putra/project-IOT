# Disclaimer

mostly disini code arduino nya i mean code .cpp nya itu AI generated, aku juga belum terlalu jago ngoding IOT ini, jadi sekalian belajar lah
also aku bakal ngasih juga skema wire nya nanti 

# ESP32 Text to Oled | Desk Buddy

jadi ini sejenis kayak ..... ya begini lah liat coba
This project is built from scratch using C++ and the Arduino framework with PlatformIO.


![IOT 1](/img/iot1.jpg)
![IOT 1](/img/iot1.2.jpg)


nah begitu, fitur nya masih ini
- OLED text rendering
- animasi mata kedip
- mata nengok kiri kanan
- ada tulisan di atasnya
- Non-blocking animation using `millis()`
- fitur jam, tanggal dan hari 
- animasi lebih di permak
- bisa connect WIFI
- bisa nampilin musik yg lagi di setel di spotify 
```bash
- makesure ada akun spotify dan lastfm
- dan makesure udah ada API KEY nya, kalo gada buat dulu di : https://www.last.fm/api/account/create
- ke /text-to-oled
- buat file secret.h
- isinya begini 
```
```cpp
#pragma once

static const char* LASTFM_USER = "[last-fm-username]";
static const char* LASTFM_API_KEY = "[last-fm-API]";
```
```bash
- done tinggal lanjut aja build upload firmware nya
```
- bisa nampilin cuaca
- bisa nampilin cuplikan video di oled display


## Hardware

- ESP32 DevKit V1 (ESP-WROOM-32)
- 0.96-inch OLED I2C (4-pin)
- Breadboard
- Jumper wires

## Wiring

| OLED | ESP32 |
|---|---|
| GND | GND |
| VDD | 3V3 |
| SDA | GPIO 21 |
| SCK / SCL | GPIO 22 |

## Software

- C++
- Arduino Framework
- PlatformIO
- Adafruit SSD1306
- Adafruit GFX Library

## Getting Started

1. Clone this repository.
2. Open the project using VS Code with PlatformIO.
3. Modif wifi setting nya di /text-to-oled/src/main.cpp, ctrl + f -> "const char* WIFI_SSID" -> modif aja 
4. Connect the ESP32.
5. Build and upload the firmware.



## Diagram wire
- newest diagram: https://wokwi.com/projects/477842258090558465

![IOT 1](/img/diag1.png)
- jadi disini aku pakai jumper wire F to M
- F ku colok ke pin oled dan pin esp32 
- M ku colok ke Breadboard
- nah posisi colok breadboard juga harus 1 kolom lah
- misal GND esp32 di 30 A ya GND oled display di 30 (B - E)

**pembaruan**
ini karna kemarin breadboard nya masih sempit bgt lubangnya, jadi hari ini aku putar putar colok colok pakai male jumper cable
nah setelah itu esp32 dan olednya bisa masuk ke lubang yg di longgarkan tadi, akhirnya cuman pakai 4 cable jumper skrg jadi hemat

![IOT 1](/img/diag1.2.png)


---

# ESP32 Servo Movement

jadi ini sejenis kayak palang pintu ajah sih, buat nyoba servo di esp32

This project is built from scratch using C++ and the Arduino framework with PlatformIO.


![IOT 1](/img/iot2.jpg)


nah begitu, fitur nya masih ini
- gerakin servo 180deg, disini sih aku 90deg aja kira kira
- connect ke web, jadi palang pintu geraknya dari tombol di webnya


## Hardware

- ESP32 DevKit V1 (ESP-WROOM-32)
- servo sg90
- Jumper wires
- sumpit kayu (bukan hardware)
- solasi (bukan hardware)

## Wiring

| Servo | ESP32 |
|---|---|
| Coklat / hitam (GND) | GND |
| Merah (5V) | 5V / VN samping GND satunya 3v3 |
| Oranye / kuning (signal) | GPIO 13 |

## Software

- C++
- Arduino Framework
- PlatformIO
- ESP32Servo 

## Getting Started

1. Clone this repository.
2. Open the project using VS Code with PlatformIO.
3. Modif wifi setting nya di /servo-movement/src/main.cpp, ctrl + f -> "const char* WIFI_SSID" -> modif aja bisa modif deg juga speed juga dan lain lain lah
4. Connect the ESP32.
5. Build and upload the firmware.



## Diagram wire
- newest diagram: https://wokwi.com/projects/478020244861931521

![IOT 1](/img/diag2.png)