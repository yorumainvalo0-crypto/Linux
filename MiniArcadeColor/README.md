# MiniArcade Color

The colour console: a 2.4" SPI display with 320 x 240 pixels (ILI9341,
ST7789 optional), four keys, a buzzer and a battery. Its own firmware, apart
from the OLED MiniArcade: it never installs as an update on the OLED console
and the OLED firmware never on this one.

All 32 games of the OLED console, redrawn for 320 x 240 in colour: Snake,
Tetris, Pong, Doom, Mine, Tunnel 3D, Flappy, Invaders, Dino, Breakout,
Rocks, Racer, Frogger, 4 wins, Tic Tac Toe, 2048, Minesweeper, Pac-Man,
Shooter, Jump & Run, Sokoban, Battleship, Sudoku, Lights Out, Match 3,
Checkers, Mau-Mau, Bomberman, Cave, Stack, Minigolf and Lander - against
the CPU or two players on one console (no online multiplayer yet). Plus
Chess for two players on one console, with all rules (castling, en passant,
promotion, check, mate, stalemate, 50 moves); the board can turn round for
black. And Skat against two CPU players (German Skat Order), with a list of
12, 24 or 36 games.

## Boards

ESP32-C3 SuperMini or ESP32-S3 WROOM-1. The firmware is built for both.

| Display  | ESP32-C3 | ESP32-S3 |
|----------|----------|----------|
| VCC, LED | 3V3      | 3V3      |
| GND      | GND      | GND      |
| SCK      | GPIO8    | GPIO12   |
| SDI/MOSI | GPIO10   | GPIO11   |
| SDO/MISO | GPIO9    | GPIO13   |
| CS       | GPIO21   | GPIO10   |
| DC       | GPIO0    | GPIO9    |
| RESET    | GPIO5    | 3V3      |

With SDO wired the console finds the display's pins by itself at the start
(it writes a register of the panel and reads it back), so other wirings work
too; the result is saved. Without SDO the table above is used.
Touch and SD card stay unconnected for now.

MPU6050 gyroscope (GY-521): SDA and SCL on any two free pins (S3: GPIO8 /
GPIO18 are kept for it), VCC 3V3, GND. Settings -> "tilt control" switches it
on: the console finds the sensor, then asks to tilt it once to the right and
once away from you. In a game, tilting works like the arrow keys. "tilt level"
chooses what counts as level: the way the console is held when a game starts,
or a position saved once (OK, hold it, 3 s); "tilt" sets how far to tilt
(weak / normal / strong). The pause menu can set the level anew.

Keys, buzzer and battery go on any free pin: the console asks for them at the
first start. Several keys (up to all five) may share one ADC pin, each to
GND through its own resistor, with or without a pull-up resistor to 3V3: the
key wizard learns each key by its voltage (it shows it, e.g. "0.65 V").
Without a pull-up resistor the levels lie close together (10k, 11k, 12k give
about 50 mV steps) - that works, but a 10k pull-up to 3V3 with keys via 0,
2.2k, 4.7k, 10k and 22k is sturdier. OK is two opposite keys on different
pins, an own OK key, or - on a ladder - two keys pressed together, learned
in the last step of the wizard.
Free on the C3 with the wiring above: GPIO1..4, 6, 7 and 20; ladder keys and
the battery need one of GPIO1..4 (ADC). The plans use: ladder keys GPIO4,
RIGHT GPIO3, battery GPIO1, buzzer GPIO20, gyroscope GPIO6/7, GPIO2 free.

## Building it on a perfboard

`docs/verdrahtung.png` shows every connection for the ESP32-C3 SuperMini
(display, ladder keys on GPIO4, RIGHT on GPIO3, buzzer GPIO20, battery GPIO1,
gyroscope GPIO6/7, charger and switch); `docs/aufbau.png` where the parts go
on a 7 x 9 cm perfboard.

## Playing

    UP/DOWN/LEFT/RIGHT
    OK                   UP+DOWN or LEFT+RIGHT pressed together
                         (or a 5th key, the key wizard offers it)
    hold OK in a game    pause: continue / restart / quit to menu
    key held at start    learn the keys again

### Skat

You (bottom) against ANNA (left) and BEN (right); the dealer goes round.
32 French cards (Kreuz, Pik, Herz, Karo; B D K A), 10 each, 2 in the Skat.

    Reizen               the panel asks: you say a value (UP/DOWN 18, 20, 22,
                         23 ...) or answer "ja"; LEFT/RIGHT bid / passe, OK
    you won the bidding  LEFT/RIGHT Skat or Hand, OK
    druecken             LEFT/RIGHT, OK marks 2 cards, OK on "push"
    announce             UP/DOWN Kreuz, Pik, Herz, Karo, Grand, Null, Null
                         Ouvert; in a Hand game LEFT/RIGHT adds Schneider,
                         Schwarz or Ouvert. The panel shows mit/ohne and the
                         value; a Null worth less than the bid cannot be chosen
    trick play           LEFT/RIGHT choose, OK play; cards you may not play
                         are dimmed

Full rules: Buben and the trump suit are trumps (Grand: only the Buben),
Null without trumps and with the 10 below the Bube. Game value = base value
(Karo 9, Herz 10, Pik 11, Kreuz 12, Grand 24) x (Spitzen mit/ohne + 1, + Hand,
Schneider, angesagt, Schwarz, angesagt, Ouvert); Null 23 / Hand 35 / Ouvert
46 / Ouvert Hand 59. The declarer needs 61 Augen (Schneider 90, Schwarz all
tricks); a game worth less than the bid is lost "ueberreizt" with the next
multiple of its base value. Everyone passes: a new deal. Scoring as in the
Seeger list: won +value, lost -2 x value; best = your total at the end of
the list (0 when it is negative).

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
