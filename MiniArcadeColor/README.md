# MiniArcade Color

The colour console: a 2.4" SPI display with 320 x 240 pixels (ILI9341,
ST7789 optional), four keys, a buzzer and a battery. Its own firmware, apart
from the OLED MiniArcade: it never installs as an update on the OLED console
and the OLED firmware never on this one.

Games so far: Snake, Tetris, Pong, Flappy, Breakout. More follow.

## Boards

ESP32-C3 SuperMini or ESP32-S3 WROOM-1. The firmware is built for both.

| Display  | ESP32-C3 | ESP32-S3 |
|----------|----------|----------|
| VCC, LED | 3V3      | 3V3      |
| GND      | GND      | GND      |
| SCK      | GPIO6    | GPIO12   |
| SDI/MOSI | GPIO7    | GPIO11   |
| SDO/MISO | GPIO20   | GPIO13   |
| CS       | GPIO10   | GPIO10   |
| DC       | GPIO5    | GPIO9    |
| RESET    | 3V3      | 3V3      |

SDO is not needed for the picture; it is reserved for the SD card and touch
(not used yet, their other pins stay unconnected). GPIO8 (SDA) and GPIO9
(C3) / GPIO18 (S3) (SCL) are kept free for a MPU6050 gyroscope (not used yet).
Everything can be changed in `idf.py menuconfig` -> MiniArcade Color.

Keys, buzzer and battery go on any free pin: the console asks for them at the
first start (C3 free: GPIO0..4 and 21).

## Playing

    UP/DOWN/LEFT/RIGHT
    OK                   UP+DOWN or LEFT+RIGHT pressed together
                         (or a 5th key, the key wizard offers it)
    hold OK in a game    pause: continue / restart / quit to menu
    key held at start    learn the keys again

## Updates

Its own channel: the release `color-latest` in this repository. A new
`version.txt` on main builds a release `color-v<version>` (a prerelease, so
it never becomes the OLED console's "latest") and refreshes `color-latest`.

    miniarcade-color-c3.bin / -s3.bin             update (WLAN page or GitHub)
    miniarcade-color-c3-full.bin / -s3-full.bin   first flash at address 0x0

## Build

    idf.py -B build-c3 set-target esp32c3 && idf.py -B build-c3 build
    idf.py -B build-s3 set-target esp32s3 && idf.py -B build-s3 build

PC simulator with all scenarios: `sh test/build.sh`, contact sheet of the
captured frames: `python3 test/render.py` (needs Pillow).
