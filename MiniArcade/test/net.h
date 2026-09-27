// Host stand-in for main/net.h: a pretend network that always joins and
// always finds version 9.3 online, so the WLAN page can be scripted.
#pragma once
#include "../main/net.h"
#include <stdio.h>
#include <string.h>

static NetState simNet = NET_OFF;
static NetJob   simJob = JOB_IDLE;

bool        netHasConfig()      { return true; }
const char *netSsid()           { return "HomeNet"; }
void        netConnect()        { simNet = NET_ONLINE; }
void        netSetup()          { simNet = NET_SETUP; }
void        netStop()           { simNet = NET_OFF; if (simJob != JOB_ERROR) simJob = JOB_IDLE; }
void        netForget()         { netStop(); }
void        netTick()           {}
void        netRestart()        { printf("      board restarts\n"); }
NetState    netState()          { return simNet; }
const char *netAddress()        { return "192.168.1.50"; }
const char *netApName()         { return "MiniArcade-AB12"; }
const char *netVersion()        { return "9.2"; }
void        netCheckUpdate()    { simJob = JOB_NEWER; }
void        netInstallUpdate()  { simJob = JOB_DONE; }
NetJob      netJob()            { return simJob; }
const char *netRemoteVersion()  { return "9.3"; }
uint8_t     netProgress()       { return simJob == JOB_DONE ? 100 : 0; }
const char *netError()          { return ""; }

// two stored firmwares only in the "versions" scenarios
static bool simTwoSlots = false, simConfirmed = false, simSwitched = false;

void fwSlots(FwSlot s[2]) {
  memset(s, 0, 2 * sizeof(FwSlot));
  strcpy(s[0].version, "9.3");
  s[0].present = s[0].bootable = true;
  s[0].pending = simTwoSlots;
  if (simTwoSlots) { strcpy(s[1].version, "9.2"); s[1].present = s[1].bootable = true; }
}
bool fwStartOther() { simSwitched = true; printf("      restarts into the other slot\n"); throw SimEnd{}; }
void fwConfirm()    { simConfirmed = true; }
bool fwPending()    { return simTwoSlots && !simConfirmed; }
