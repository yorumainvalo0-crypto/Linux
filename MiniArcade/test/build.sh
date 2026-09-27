#!/bin/sh
set -e
cd "$(dirname "$0")"
mkdir -p frames
g++ -std=c++17 -O1 -DARCADE_TRACE -I. -I../main -x c++ run.cpp ../main/arcade.cpp -o idfsim
fail=0
for s in menu tetris snake pong doom mine tunnel flappy invaders dino breakout rocks racer frogger frogroll racerfair c4 c4bench ttt tttbench mp4 mpno mpwait mpleft mppong mpsnake mpold mpin mpname settings wlan versions versions2 versions3 upload g2048 g2logic mines mslogic pacman pmlogic pause stats sleep wizard wizard2; do
  ./idfsim "$s" || fail=1
done
g++ -std=c++17 -O1 linktest.cpp -o linktest       # multiplayer protocol on a lossy radio
./linktest || fail=1
[ $fail -eq 0 ] && echo "ALL SCENARIOS PASSED"
exit $fail
