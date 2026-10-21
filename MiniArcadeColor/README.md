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
the CPU or two players on one console (and online, see below). Plus
Mine 3D, a block world in first person, Palace, a platformer, Skat against
two CPU players (all below), and Chess with all rules (castling, en passant,
promotion, check, mate, stalemate, 50 moves), against the CPU, for two
players on one console or online (the board can turn round for black):

- **vs CPU easy / normal / hard**: you pick white or black, the board faces
  you. The CPU searches with alpha-beta (material, piece-square tables, takes
  searched on to the end) for up to 0.5 / 1 / 2.5 s: easy looks one move
  ahead and now and then plays any move, normal 3 plies, hard up to 5.
  Holding OK while it thinks opens the pause menu as usual.
- **saved game**: a game in progress is kept in flash after every move and
  when you leave it (also through sleep or switching off); the next start of
  Chess offers "continue saved game" first. A finished game, "restart" in
  the pause menu or a new game clear it.

## Mine 3D

![Mine 3D](docs/mine3d.png)

A block world in first person: 32 x 32 x 16 blocks of grass hills, beaches,
lakes, trees, caves and stone with ore. It wraps round at the edges. Two ways
to play:

- **free play**: no time, no score - dig and build what you like with every
  kind of block (grass, dirt, stone, sand, wood, leaves, coal, gold, crystal)
  without end. The world is kept in flash (its seed and up to 1000 changed
  blocks, every 30 s and when you leave); "continue my world" brings it back.
- **ore hunt**: a new world, three minutes to dig ore: coal 1, gold 5,
  crystal 20 points; blocks to build with only from what you dug.

    LEFT/RIGHT           turn
    UP/DOWN held         walk forward / back (a step of one block is jumped)
    UP/DOWN tapped       look up / down (four steps, marked on the left)
    OK                   pick: dig the block in the middle of the view
                         block: put it on the face you look at - under your
                         own feet it lifts you up a block
    OK twice             the bag: LEFT/RIGHT choose the pick or a block, OK

Ore glints in the dark; whatever else you dig goes into the bag (grass gives
dirt). Every pixel is a ray through the block grid, all in integers, drawn at
160 x 92 and doubled.

## Palace

![Palace](docs/palace.png)

A nameless runner escapes from a palace dungeon: eight levels, twenty
minutes for all of them. A level is a grid of rooms of 10 x 3 tiles; the
screen flips at a room's edge. Loose tiles crumble (whole rows of them, run
over them), spikes spring out, plates open gates (others shut them again),
one plate opens the exit door. One floor down is safe, two cost a flask of
health, three kill - climbing down first saves a floor. Potions heal or add
a flask. The sword lies in the first level; guards get better level by
level, the captain waits in the sky tower, the forges and below the throne,
the Warden on his throne at the very end (nine flasks, he rages when half
are gone). Dying starts the level again, the clock runs on. Score: 100 per
level, plus the seconds left at the end.

| Level | Rooms | What waits there |
|-------|-------|------------------|
| 1 The Cellars | 3 x 1 | the sword, the first guard, a loose tile |
| 2 The Old Wells | 4 x 2 | a deep drop to climb down, loose rows, spikes |
| 3 The Guard Halls | 5 x 2 | four guards, gates and the plates that shut them |
| 4 The Sky Tower | 4 x 3 | a climb up the tower, the captain |
| 5 The Sunken Baths | 5 x 1 | water in the pits (the room mirrored in it), a timed gate behind a running jump over a pool |
| 6 The Great Library | 4 x 3 | bookshelves, a loose row to cross, hang drops, a gate shut by a plate on the way, a climb of six ledges |
| 7 The Forges | 5 x 2 | furnaces, scythes, two timed gates, molten iron below, the captain again |
| 8 The Dawn Throne | 4 x 3 | windows at dawn, all of it together, the Warden |

New in the later levels:

- **Scythes** sweep out of a slot in the floor in a steady beat (their tip
  shows just before). Wait on the tile before one until it has gone in,
  then run past - or step past carefully right after it.
- **Timed gates** (their plates are blue) fall shut again a few seconds
  after you leave the plate; a bar on the gate shows the time left, and it
  ticks. A loose tile falling onto the plate keeps such a gate open.
- **Pools**: the deepest pits of the baths hold water, those of the forges
  molten iron - a fall in is the end.

The level reached is kept: the next start offers "continue: level N" (with
the flasks of the big potions before it and 2:30 for each level left), a
new game, or any level before it. The score counts the levels played.

    LEFT/RIGHT           run
    DOWN + LEFT/RIGHT    careful step (safe on spikes, stops at an edge)
    UP + LEFT/RIGHT      jump: standing 1-2 tiles, running 3 tiles of gap
    UP                   jump up to a ledge above; hold UP to climb on
    DOWN at an edge      climb down and hang; DOWN again lets go
    OK                   draw the sword (it comes out by itself near a guard)
    with the sword       LEFT/RIGHT step, OK strike, UP parry, DOWN put away

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

Speed: only the parts of the picture that changed go to the display (blocks
of 32 x 1 pixels), so most games run at the 60 fps limit. Games that redraw
the whole screen (Doom, Tunnel 3D, Racer) depend on the SPI clock: about
15 fps at 20 MHz, 28 fps at 40 MHz. Settings -> "screen speed" tries a faster
clock and keeps it only if OK is pressed within 10 s; a key held while
switching on goes back to the safe default. "show fps" puts the frame rate in
the corner.

MPU6050 gyroscope (GY-521): SDA and SCL on any two free pins (S3: GPIO8 /
GPIO18 are kept for it), VCC 3V3, GND. Settings -> "tilt control" switches it
on: the console finds the sensor, then asks to tilt it once to the right and
once away from you. In a game, tilting works like the arrow keys; in Pong,
Breakout, Racer, Rocks, Tunnel 3D and Doom it steers without steps - the
further you tilt, the faster it goes. "tilt level"
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

Two keys at once on one ladder (e.g. UP + RIGHT to jump in Palace): with a
10k pull-up to 3V3 and the keys via **15k, 10k, 6.8k, 5.1k and 2.7k** to GND
every key and every pair of keys has its own level, at least 36 mV apart
(four keys: 15k, 11k, 7.5k, 5.6k, at least 62 mV). The wizard then asks for
two arrows together once - that measures the supply - and works out the
level of every pair from the single keys. Pairs that would lie too close to
another level (e.g. keys on the pin's own pull-up) are simply not used.
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

## Online

"Online" in the game list plays with a console nearby over ESP-NOW (no WLAN
needed), on the same protocol as the OLED console's "Multiplayer" page - a
colour and an OLED console play together. Everybody who has the page open
shows up in the list of the others with a name and a 4 letter code from the
chip; the radio is only on while the page is open.

    own row, OK          change the name (UP/DOWN letter, LEFT/RIGHT place)
    a console, RIGHT     friend (*) or not - friends come first in the list
    a console, OK        challenge it: choose a game, it has 30 s to answer
    challenged           OK = play, LEFT = no
    hold OK              in a game: give up; in the list: back to the games

The games: 4 wins, Tic Tac Toe, Pong, Snake, Pac-Man, Battleship, Checkers,
Mau-Mau and Bomberman with either console, Chess only between two colour
consoles (an OLED console is marked "OLED" in the list). Real-time games run
in lockstep on both consoles; only the keys go over the radio.

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
