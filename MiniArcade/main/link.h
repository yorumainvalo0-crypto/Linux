// Nearby multiplayer for the IDF build: ESP-NOW on a fixed channel, the
// protocol itself is in linkcore.h. The radio is only on while the
// multiplayer page is open - challenges reach nobody who is not there.
#pragma once
#include <stdint.h>
#include "linkcore.h"

bool        linkOpen();                          // false if the radio could not start
void        linkClose();
void        linkTick();                          // call from the UI loop
LinkCore   &linkCore();
uint32_t    linkNow();                           // ms, the clock the protocol runs on

uint8_t     linkList(uint8_t *idx, uint8_t max); // peer indices: friends first, then signal
bool        linkIsFriend(const char *code);
void        linkToggleFriend(const char *code);

const char *linkName();                          // own player name, kept in flash
void        linkSetName(const char *name);
