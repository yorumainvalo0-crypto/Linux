// Host stand-in for main/net.h: a pretend network that always joins and
// always finds version 9.3 online, so the WLAN page can be scripted.
#pragma once
#include "../main/net.h"
#include <stdio.h>

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
