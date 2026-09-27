# MiniArcade - ESP-IDF version

22 games: Tetris, Snake, Pong, Doom, Mine, Tunnel 3D, Flappy, Invaders,
Dino, Breakout, Rocks, Racer, Frogger, Connect Four, Tic Tac Toe, 2048,
Minesweeper, Pac-Man, Shooter, Jump & Run, Sokoban and Battleship.
Against a second console nearby (Multiplayer): Connect Four, Tic Tac Toe,
Pong, Snake, Pac-Man and Battleship.

Ready-made firmware: `MiniArcade-Firmware/` in the repository and every
release have `MiniArcade-<version>.zip` with a German how-to.

## Playing

    UP/DOWN/LEFT/RIGHT + OK
    hold OK in a game    pause: continue / restart / quit to menu
                         (hold OK once more = quit)
    UP in the library    jumps from the top to Settings, Stats, Multiplayer
    Minesweeper          OK opens, a quick double tap on OK sets a flag
    Sokoban              OK takes the last move back
    Shooter              the ship fires by itself, OK = bomb
    Battleship           UP shuffles your fleet before the start
    Stats                how often and how long each game was played, and
                         22 awards (LEFT/RIGHT switches the two pages).
                         A new award pops up with a short tune.

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
U8g2. The Arduino API is provided by the thin platform layer in
`main/arcade.cpp`. `main/MiniArcade.ino` still builds in the Arduino IDE;
the WLAN page only exists in this build, because it needs `main/net.h`.

    main/MiniArcade.ino   buttons, sound, storage, pause menu, the library;
                          includes the parts below in this order
    main/game_*.h         one file per game
    main/stats.h          stats and awards
    main/setup_wizard.h   key, sound and battery setup
    main/settings.h       settings page
    main/wlan.h           WLAN page and firmware versions
    main/multiplayer.h    multiplayer page and the online games
    main/rtgames.h        rules of Pong, Snake and Pac-Man for two consoles (no hardware,
                          tested on the PC); Pac-Man alone uses it too
    main/sokoban_levels.h the Sokoban levels (generated, every one solved by the test)
    main/arcade.cpp       SSD1306 driver, GPIO, timing, NVS
    main/net.cpp          WLAN, setup hotspot, update page, GitHub updates
    main/linkcore.h       multiplayer protocol (no hardware, tested on the PC)
    main/link.cpp         multiplayer radio: ESP-NOW, name and friends in flash
    main/U8g2lib.h        1 bit framebuffer with the U8g2 method names
    main/Arduino.h        millis / delay / pinMode / digitalRead / random
    main/Preferences.h    high scores + Mine world, backed by NVS
    main/fonts.h          generated 5x7 and 7x10 bitmap fonts

## Build

    . $IDF_PATH/export.sh          # Windows: start "ESP-IDF PowerShell"
    idf.py set-target esp32c3
    idf.py -p COM5 erase-flash     # only needed once, clears old data
    idf.py -p COM5 flash monitor   # Linux/macOS: /dev/ttyACM0 or /dev/cu.usbmodem*

## WLAN and updates

Settings -> "wlan and update...". The radio is only on while this page is
open, and it needs a cpu clock of at least 80 MHz (the page offers to switch).

    connect            joins the saved network; the display shows the
                       address of the update page, e.g. http://192.168.1.23
    set up with phone  opens the hotspot "MiniArcade-XXXX". Join it with the
                       phone, the setup page opens by itself (otherwise go
                       to 192.168.4.1), pick the network, enter the password.
    check for update   asks GitHub for the latest release, OK again installs it
    forget network     deletes the stored network

The update page in the browser can do the same: check GitHub, or upload a
`miniarcade.bin` from a release or from `build/` and press "flash". No extra
software needed. An uploaded file is only written after **OK is pressed on
the console itself** (LEFT = no), so nobody else on the network can flash
it. Uploads are refused over the open setup hotspot.

WLAN range: many C3 SuperMini boards cannot join at full transmit power, so
it is limited to 8.5 dBm (menuconfig -> MiniArcade -> transmit power). Raise
it if the board is far from the router.

### Two versions, never stuck

The board keeps two firmwares: the running one and the one before. The next
update overwrites the older one.

* **At every start** a "VERSION" page shows both for 3 s (only when two are
  stored). UP/DOWN and OK start the other one, otherwise the current one
  starts by itself.
* **Settings -> "firmware version..."** switches later.
* A **new version is only kept after the first key press** in the menu. If
  it shows nothing or the keys do not work, switch the board off and on:
  the previous version comes back by itself. Until then the sleep timer is
  paused, because waking up would count as such a restart.

### First flash (once, by cable)

The updates need a new partition table with two app slots, so the first
flash after v9.1 has to go over USB:

    idf.py fullclean               # an old sdkconfig still has the old partition table
    idf.py -p COM5 flash           # keeps high scores and settings

or, on a fresh board, `miniarcade-full.bin` from a release at address 0x0
(this one also clears high scores and settings):

    esptool.py --chip esp32c3 write_flash 0x0 miniarcade-full.bin

## Multiplayer

Library -> "Multiplayer". Every console with this page open appears in the
list of the others, with its name and a 4 letter code taken from the chip
(e.g. `ANNA  K7F2  free`), sorted by signal strength. No router is needed:
the consoles talk directly over ESP-NOW (channel 1), typically 50-200 m.

    first line   your own name and code - OK = change the name
    UP / DOWN    choose a player
    OK           challenge: pick 4 wins, Tic Tac Toe, Pong, Snake, Pac-Man
                 or Battleship
    RIGHT        mark as friend (*) - friends are listed first
    hold OK      back to the games

A challenge waits 30 s: the other console shows "wants to play ..." with
OK = yes, LEFT = no. Challenges only reach consoles that have the
multiplayer page open - nobody is disturbed in the middle of another game,
and the radio is off everywhere else. A coin decides who begins; in
Tic Tac Toe that player is X. Holding OK during a game gives up, and a
console that is switched off is noticed after 15 s.

Every move is repeated until the other console confirms it, so lost radio
packets do not matter; `test/linktest.cpp` plays whole games with 50 %
packet loss.

**Pong and Snake** run in real time. Both consoles compute the same game
(`main/rtgames.h`); only the keys go over the radio, 50 times a second, and
every packet repeats the last inputs. A step is only taken when the keys
of both players for it are there, so both screens always show the same
game - with a bad signal it waits a moment ("waiting...") instead of
drifting apart. Pong: first to 5 points, your paddle is always on the
left. Snake: your snake is filled, the other one hollow; whoever hits a
wall, a body or the other head loses. **Pac-Man** online: both Pac-Men
share the maze and the dots, the ghosts hunt whoever is nearer, and when
the dots are gone the higher score wins. **Battleship** takes turns like
4 wins. Consoles with an older firmware do not know the newer games - the
challenge then says "needs an update".

### Version numbers and publishing an update

Every update has a number in `MiniArcade/version.txt` (9.3, 9.4, ...).
Raise it in the pull request; when the pull request is merged into main,
the workflow `.github/workflows/miniarcade.yml` publishes the release
"MiniArcade 9.4" with the tag `v9.4` by itself, and the consoles offer it
under "check for update". Pushes that keep the number only build.

Other ways to publish:

* GitHub -> Actions -> "MiniArcade firmware" -> "Run workflow", enter e.g. `9.4`
* push a tag: `git tag v9.4 && git push origin v9.4`

"Run workflow" publishes from `main` only. The release then holds
`miniarcade.bin` (for updates), `miniarcade-full.bin` (for the first flash)
and `version.txt`. Versions like `9.3-rc1` become prereleases and are never
offered to the boards; a board only offers versions newer than its own. The boards look at the repository
set in `CONFIG_ARCADE_GITHUB_REPO` (menuconfig -> MiniArcade); the GitHub
build fills in its own repository. The repository must be public.

## Size

    ESP-IDF 6.1-beta1   157 kB firmware  (v9.1, without WLAN)
    ESP-IDF 5.3.2       168 kB firmware  (v9.1, without WLAN)
    Arduino             358 kB firmware
    ESP-IDF 5.3.2       785 kB firmware  9.3: WLAN, TLS, updates and multiplayer -
                                         each update slot holds 1.9 MB
    ESP-IDF 5.3.2       792 kB firmware  9.4: + pause, stats, awards, online Pong / Snake
    ESP-IDF 5.3.2       797 kB firmware  9.5: + 7 games, online Pac-Man and Battleship

Both IDF versions build unchanged. 6.1 is smaller because it uses picolibc.

The savings come from dropping the Arduino core plus U8g2 and from the
size options in sdkconfig.defaults (no logging, nano printf, no C++
exceptions or RTTI).

## Tests

`test/build.sh` runs every scenario against the real driver on a PC - each
game, the menus, pause and stats, WLAN and updates, multiplayer against a
bot console - and `test/linktest.cpp`, the radio protocol with lost packets
(including whole online Pong and Snake games that must end the same on both
consoles).
