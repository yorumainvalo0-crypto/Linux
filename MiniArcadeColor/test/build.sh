#!/bin/sh
# Builds the host simulator and runs every scenario; frames land in frames/.
set -e
cd "$(dirname "$0")"
mkdir -p frames
rm -f frames/*.ppm
g++ -std=c++17 -O1 -DARCADE_TRACE -I. -I../main -I../../MiniArcade/test -I../../MiniArcade/main \
    -x c++ sim.cpp ../main/platform.cpp ../main/gfx.cpp -o colorsim
fail=0
for s in menu wizard ladder ladder4 ladder5 ladderpair ladderbat batglitch uploadask updgithub updupload updfail updnowlan updnonet updask plain2 tilt settings sdcard sdlogic sdlevels sdnone sdpull sdbad maglogic magplay magpull magnone magerr fps snake snakeover tetris pong flappy breakout invaders rocks shooter lander palace palacecont palacelogic palaceshots pumpkin pumpkinkeys pmeditor pmlogic cave doom mine mine3d mine3dfree mine3dlogic tunnel pacman bomber g2048 g2logic mines mslogic sokoban sklevels sudoku lightsout match3 pzlogic blocks blocksadv blockspz blockslogic fruit fruitlogic dino dinofair racer racerfair frogger frogroll jump jrfair stack boardlogic c4 ttt bship bshipown checkers maumau skat skatlogic minigolf chess chesscpu chesssave chesslogic fxlogic pause tvdecode tvui \
         online mpchess mpc4 mpttt mppong mpsnake mppac mpship mpdame mpmau mpbomb mpchessgame \
         inetc4 inetpong inetponglock inetfriend inetlost inetall inetfirst inetpin inetlobby \
         kart kartmenu karttilt kartghost kartlogic mpkart mpkartgone inetkart; do
  ./colorsim "$s" || fail=1
done
# the menus and setup screens again in German: nothing may run off the screen; then
# "sprache" switches the language in the settings and checks every translation
rm -f frames/lang_de.txt
for s in menu settings pause wizard ladder ladder5 ladderpair plain2 tilt sdcard sdlevels sdpull sdbad magplay magpull magnone magerr fps frogger tvui pumpkin pumpkinkeys pmeditor \
         updgithub updupload updfail updnowlan updnonet \
         inetc4 inetfriend inetlost inetfirst inetpin inetlobby kartmenu; do
  SPRACHE=de ./colorsim "$s" || fail=1
done
./colorsim sprache || fail=1
[ $fail -eq 0 ] && echo "ALL SCENARIOS PASSED"
exit $fail
