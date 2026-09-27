#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p frames
g++ -std=c++17 -O1 -DARCADE_TRACE -I. -I../main -x c++ run.cpp ../main/arcade.cpp -o idfsim
fail=0
for s in menu tetris snake pong doom mine tunnel flappy invaders dino breakout rocks racer frogger c4 c4bench settings sleep wizard wizard2; do
  ./idfsim "$s" || fail=1
done
[ $fail -eq 0 ] && echo "ALL SCENARIOS PASSED"
exit $fail
