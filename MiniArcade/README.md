# MiniArcade - ESP-IDF version

14 games: Tetris, Snake, Pong, Doom, Mine, Tunnel 3D, Flappy, Invaders,
Dino, Breakout, Rocks, Racer, Frogger and Connect Four.

## Optional hardware

    buzzer   passive piezo: one leg to any free GPIO, the other to GND.
             On the first start the sound wizard beeps on each free pin and
             you confirm the one you hear ("Setup sound" repeats it later).

    battery  the 5 V booster output cannot be measured, so tap the cell:
                 BAT+ ---[100k]---+---[100k]--- GND
                                  |
                              GPIO 0..4
             That pin then sees half the cell voltage. It is found
             automatically and the menu shows the percentage top right;
             without it the menu simply shows "USB". The "Setup battery"
             entry lists all five ADC pins with their live voltage and lets
             you pick one by hand - useful when a pin is already taken by a
             key or by the buzzer.

Same games as the Arduino sketch, but without the Arduino core and without
U8g2. `main/MiniArcade.ino` is byte identical to the Arduino version - the
Arduino API is provided by the thin platform layer in `main/arcade.cpp`.

    main/MiniArcade.ino   the games (unchanged)
    main/arcade.cpp       SSD1306 driver, GPIO, timing, NVS
    main/U8g2lib.h        1 bit framebuffer with the U8g2 method names
    main/Arduino.h        millis / delay / pinMode / digitalRead / random
    main/Preferences.h    high scores + Mine world, backed by NVS
    main/fonts.h          generated 5x7 and 7x10 bitmap fonts

## Build

    . $IDF_PATH/export.sh          # Windows: start "ESP-IDF PowerShell"
    idf.py set-target esp32c3
    idf.py -p COM5 erase-flash     # only needed once, clears old data
    idf.py -p COM5 flash monitor   # Linux/macOS: /dev/ttyACM0 or /dev/cu.usbmodem*

## Size

    ESP-IDF 6.1-beta1   157 kB firmware
    ESP-IDF 5.3.2       168 kB firmware
    Arduino             358 kB firmware

Both IDF versions build unchanged. 6.1 is smaller because it uses picolibc.

The savings come from dropping the Arduino core plus U8g2 and from the
size options in sdkconfig.defaults (no logging, nano printf, no C++
exceptions or RTTI).

## Tests

`test/build.sh` runs all ten scenarios against the real driver on a PC.
