# MiniArcade - ESP-IDF version

22 games: Tetris, Snake, Pong, Doom, Mine, Tunnel 3D, Flappy, Invaders,
Dino, Breakout, Rocks, Racer, Frogger, Connect Four, Tic Tac Toe, 2048,
Minesweeper, Pac-Man, Shooter, Jump & Run, Sokoban and Battleship.
Against a second console nearby (Multiplayer): Connect Four, Tic Tac Toe,
Pong, Snake, Pac-Man and Battleship.

Ready-made firmware: `Versions/<version>/` in the repository and every
release have `MiniArcade-<version>.zip` with a German how-to.

## Playing

    UP/DOWN/LEFT/RIGHT + OK
    hold OK in a game    pause: continue / restart / quit to menu
                         (hold OK once more = quit)
    UP in the library    jumps from the top to Settings, Stats, Multiplayer
    Minesweeper          OK opens, a quick double tap on OK sets a flag
    Sokoban              OK takes the last move back
    Shooter              the ship fires by itself, OK = bomb
    Battleship           before the start: UP mixes a fleet, DOWN places the
                         ships by hand (OK sets one down, double OK turns it)
                         marks: square = missed shot, X = hit, solid = sunk,
                         dot = water next to a sunk ship
    Stats                how often and how long each game was played, and
                         22 awards (LEFT/RIGHT switches the two pages).
                         A new award pops up with a short tune.

## Optional hardware

    buzzer   passive piezo: one leg to any free GPIO, the other to GND.
             On the first start the sound wizard beeps on each free pin and
             you confirm the one you hear ("Setup sound" repeats it later).

    battery  the 5 V booster output cannot be measured, so tap the cell
             with two equal resistors (e.g. 2x 56k or 2x 100k):
                 BAT+ ---[R]---+---[R]--- GND
                               |
                           GPIO 0..4
             That pin then sees half the cell voltage. The console finds
             it by itself at every start until one is found. On the
             "set up battery" page RIGHT searches again (e.g. after
             rewiring), "bat" marks the pin that looks like the battery
             and "high" a pin with too much voltage: the upper resistor
             sits on the 5 V booster output instead of BAT+, or the board
             pulls that pin up (GPIO2 is a strapping pin - GPIO3 or 4 is
             the safer choice).
             UP/DOWN + OK picks a pin by hand, "no battery" stops the
             search. A floating pin is told apart with the internal
             pull-down: it falls to 0 V, the divider keeps the pin up.
             The percentage follows the Li-ion discharge curve and the
             low points under load (sound, WLAN): an older cell switches
             the board off in such a dip long before its resting voltage
             looks empty. A dip pulls the number down within a second, it
             climbs back over about half a minute; without a battery the
             menu shows "USB".

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
    main/phone.cpp        the phone pages (HTTP, backup to and from the flash)
    main/phone.h          what the console tells them (stats, settings, keys, levels)
    main/backupfmt.h      the backup file format (tested on the PC)
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
    phone hotspot      the console opens its own WLAN for the phone pages
                       (see "On the phone")
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

## On the phone

With the console in your WLAN (Settings -> "wlan and update..." -> connect),
open the address it shows in the phone's browser. Without a WLAN around,
pick "phone hotspot" on the same page: the console opens its own network
"MiniArcade-XXXX" and shows its password. Join it with the phone and open
http://192.168.4.1 (no internet meanwhile).

    LEFT               a new random password (phones have to join again)
    RIGHT              password off / on - off means anybody nearby can
                       join, so firmware uploads are refused then
    OK                 closes the hotspot
    phone: Settings    set an own password (8..63 characters, empty = none),
                       used from the next start of the hotspot

The first password is 8 random digits. Below the update part the page has:

    Stats and awards   best score, times played and play time of every game,
                       all awards with what they need
    Settings           player name, brightness, sleep time, sound on/off,
                       cpu clock (from the next start on)
    Screen             live picture of the display, "save a picture" = PNG
    Controller         the screen plus big keys - play with the phone
                       (a computer's arrow keys and Enter work too)
    Sokoban editor     draw up to 3 own levels; on the console they come
                       after the last built-in level ("OWN 1".."OWN 3")
    Backup             download all saves as a text file (scores, stats,
                       awards, Mine world, own levels, name, friends,
                       settings - not the WLAN password) and restore it:
                       the WLAN page on the console asks first, then the
                       console restarts

Screen and controller while playing: set "stay online: yes" on the WLAN page
and leave it - the WLAN stays on (the menu shows "WLAN" top right) and the
console answers the phone between frames (this works on the phone hotspot
too). The multiplayer page switches it off again, as it needs the radio
itself. The pages only exist in your own WLAN and on the phone hotspot,
never on the open setup hotspot.

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
    ESP-IDF 5.3.2       818 kB firmware  9.7: + phone pages (stats, settings, screen,
                                         controller, Sokoban editor, backup)
    ESP-IDF 5.3.2       820 kB firmware  9.8: + phone hotspot
    ESP-IDF 5.3.2       820 kB firmware  9.9: battery found by itself, smoothed percentage
    ESP-IDF 5.3.2       820 kB firmware  9.10: fix: the pull-down test of the battery search
    ESP-IDF 5.3.2       820 kB firmware  9.11: battery percentage from the low points under load

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
