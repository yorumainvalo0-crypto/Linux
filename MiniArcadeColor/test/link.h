// Host stand-in for main/link.h: only the player name (no radio yet).
#pragma once
#include "../../MiniArcade/main/link.h"
#include <string.h>
static char simName[LK_NAME + 1] = "PLAYER";
const char *linkName() { return simName; }
void linkSetName(const char *n) { memset(simName, 0, sizeof(simName)); strncpy(simName, n, LK_NAME); }
