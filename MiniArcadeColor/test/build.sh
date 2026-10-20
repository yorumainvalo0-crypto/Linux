#!/bin/sh
# Builds the host simulator and runs every scenario; frames land in frames/.
set -e
cd "$(dirname "$0")"
mkdir -p frames
rm -f frames/*.ppm
g++ -std=c++17 -O1 -DARCADE_TRACE -I. -I../main -I../../MiniArcade/test -I../../MiniArcade/main \
    -x c++ sim.cpp ../main/platform.cpp ../main/gfx.cpp -o colorsim
fail=0
for s in menu wizard ladder ladder4 plain2 tilt settings snake snakeover tetris pong flappy breakout pause; do
  ./colorsim "$s" || fail=1
done
[ $fail -eq 0 ] && echo "ALL SCENARIOS PASSED"
exit $fail
