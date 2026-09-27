# MiniArcade firmware

One zip per version: `MiniArcade-<version>.zip`. Each holds

| file | for |
|---|---|
| `miniarcade-full.bin` | first flash over USB at address `0x0` (clears high scores and settings) |
| `miniarcade.bin` | updates: upload it on the console's update page |
| `ANLEITUNG.txt` | step by step, in German |

Flash in the browser (Chrome / Edge): <https://espressif.github.io/esptool-js/>
→ Connect → address `0x0`, file `miniarcade-full.bin` → Program.

The newest version is also on the [releases page](https://github.com/yorumainvalo0-crypto/Linux/releases/latest).
