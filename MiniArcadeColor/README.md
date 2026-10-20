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
| SCK      | GPIO8    | GPIO12   |
| SDI/MOSI | GPIO9    | GPIO11   |
| SDO/MISO | any free | GPIO13   |
| CS       | GPIO21   | GPIO10   |
| DC       | GPIO4    | GPIO9    |
| RESET    | GPIO5    | 3V3      |

With SDO wired the console finds the display's pins by itself at the start
(it writes a register of the panel and reads it back), so other wirings work
too; the result is saved. Without SDO the table above is used.
Touch and SD card stay unconnected for now. The MPU6050 gyroscope (not used
yet) gets SDA GPIO8 / SCL GPIO18 on the S3; the C3 has no pins left for it.

Keys, buzzer and battery go on any free pin: the console asks for them at the
first start (C3: GPIO0..3, 6, 7 and 10 or 20 - the battery needs one of
GPIO0..3).

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
