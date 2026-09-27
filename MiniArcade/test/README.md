# Host test harness

Compiles the real game code together with the real platform layer
(`../main/arcade.cpp`) and replaces only the hardware calls. The screen is
decoded from the bytes the SSD1306 driver would put on the I2C bus, so the
tests look at exactly what the panel would show.

    g++ -std=c++17 -O1 -DARCADE_TRACE -I. -I../main -x c++ run.cpp ../main/arcade.cpp -o idfsim
    mkdir -p frames
    for s in menu tetris snake pong doom mine tunnel flappy invaders wizard; do ./idfsim $s; done

Frames listed in `captureAt` are written to `frames/` and can be turned into
a PNG contact sheet with `python3 render.py` (needs Pillow).
Set `GRAB=SAVED` to capture the frame after a given string was drawn.
