#!/bin/sh
# Builds the host simulator and runs every scenario; frames land in frames/.
set -e
cd "$(dirname "$0")"
mkdir -p frames
rm -f frames/*.ppm
g++ -std=c++17 -O1 -DARCADE_TRACE -I. -I../main -I../../MiniArcade/test -I../../MiniArcade/main \
    -x c++ sim.cpp ../main/platform.cpp ../main/gfx.cpp -o colorsim
fail=0
for s in menu wizard ladder ladder4 ladder5 plain2 tilt settings fps snake snakeover tetris pong flappy breakout invaders rocks shooter lander palace palacelogic cave doom mine mine3d mine3dfree mine3dlogic tunnel pacman bomber g2048 g2logic mines mslogic sokoban sklevels sudoku lightsout match3 pzlogic dino dinofair racer racerfair frogger frogroll jump jrfair stack boardlogic c4 ttt bship bshipown checkers maumau skat skatlogic minigolf chess chesscpu chesssave chesslogic pause \
         online mpchess mpc4 mpttt mppong mpsnake mppac mpship mpdame mpmau mpbomb mpchessgame; do
  ./colorsim "$s" || fail=1
done
[ $fail -eq 0 ] && echo "ALL SCENARIOS PASSED"
exit $fail
