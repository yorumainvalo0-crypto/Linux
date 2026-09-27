# MiniArcade versions

Everything needed to flash a console yourself, one folder per version -
the highest number is the newest. A new version is added here by itself
when it is released.

| file | for |
|---|---|
| `miniarcade-full.bin` | **first flash** of a new ESP32-C3 over USB, at address `0x0` (clears high scores and settings) |
| `miniarcade.bin` | **updates**: upload it on the console's update page (or let the console fetch it over WLAN) |
| `MiniArcade-<version>.zip` | both files plus `ANLEITUNG.txt`, the German step by step |

Flash in the browser (Chrome / Edge): <https://espressif.github.io/esptool-js/>
→ Connect → address `0x0`, file `miniarcade-full.bin` → Program.

To download a single file: open it here, then "Download raw file".
The same files are also on the [releases page](https://github.com/yorumainvalo0-crypto/Linux/releases).
