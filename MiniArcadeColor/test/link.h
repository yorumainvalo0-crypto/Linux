// Host stand-in for main/link.h: the OLED tests' radio (MiniArcade/test/link.h)
// - the real protocol (linkcore.h) with a second console "ANNA" next to us,
// played by a little bot that knows the OLED console's nine games. Chess,
// the colour console's own, is played by a bot in sim.cpp (simBotOther).
// Its LinkCore is called simPeer here: sim.cpp has a simBot of its own.
#pragma once
#define simBot simPeer
#include "../../MiniArcade/test/link.h"
#undef simBot
