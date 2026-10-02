// Host stand-in for main/net.h: a pretend network that always joins and
// always finds version 9.3 online, so the WLAN page can be scripted.
#pragma once
#include "../main/net.h"
#include <stdio.h>
#include <string.h>

static NetState simNet = NET_OFF;
static NetJob   simJob = JOB_IDLE;
static bool     simUpload = false;  // a file arrives over the web page right after joining
static bool     simNoNet  = false;  // the saved network is not there: joining, then failed
static int      simJoinTicks = 0;

bool        netHasConfig()      { return true; }
const char *netSsid()           { return "HomeNet"; }
void        netConnect()        { simNet = simNoNet ? NET_CONNECTING : NET_ONLINE; simJoinTicks = 0; if (simUpload) simJob = JOB_ASKING; }
void        netSetup()          { simNet = NET_SETUP; }
void        netStop()           { simNet = NET_OFF; if (simJob != JOB_ERROR) simJob = JOB_IDLE; }
void        netForget()         { netStop(); }
void        netTick()           { if (simNet == NET_CONNECTING && ++simJoinTicks > 500) simNet = NET_FAILED; }
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
static uint8_t simAnswer = 0;       // 1 = yes, 2 = no
const char *netAskVersion()     { return "9.4"; }
void        netAnswer(bool yes) { simAnswer = yes ? 1 : 2; simJob = yes ? JOB_DONE : JOB_ERROR; }

// a backup from the phone waiting for OK on the WLAN page
static bool simRestoreAsk = false;
static int  simRestoreAnswer = 0;   // 1 = yes, 2 = no
bool phoneAsking()          { return simRestoreAsk; }
void phoneAnswer(bool yes)  { simRestoreAnswer = yes ? 1 : 2; simRestoreAsk = false; }
void netRestartLater()      {}

// the phone hotspot
static char simApPass[65] = "12345678";
static bool simApOpen = false;
void        netPlay()           { simNet = NET_PLAY; }
const char *netApPass()         { return simApOpen ? "" : simApPass; }
void        netApNewPass()      { strcpy(simApPass, "87654321"); }
void        netApToggleOpen()   { simApOpen = !simApOpen; }
bool        netApSetPass(const char *p) {
  size_t n = strlen(p);
  if (n && (n < 8 || n > 63)) return false;
  simApOpen = !n; if (n) strcpy(simApPass, p);
  return true;
}

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

// the AI chat: a fixed answer some frames after the question
static bool    simAiKey = false;
static char    simAiKeyText[201], simAiQ[AI_QUESTION_MAX + 1];
static AiState simAi = AI_IDLE;
static int     simAiTicks = 0;
static const char *simAiText = "";
static const char SIM_AI_ANSWER[] =
  "**Hallo!** Gr\xC3\xBC\xC3\x9F" "e aus der Wolke \xE2\x80\x93 ich bin Claude. \xC2\xBFQu\xC3\xA9 tal? \xF0\x9F\x98\x80\n\n"
  "# Tipp\nDiese Antwort ist absichtlich lang, damit man sie mit den Pfeiltasten scrollen muss: "
  "eins zwei drei vier f\xC3\xBCnf sechs sieben acht neun zehn elf zw\xC3\xB6lf dreizehn vierzehn "
  "f\xC3\xBCnfzehn sechzehn siebzehn achtzehn neunzehn zwanzig. Ende.";
bool    netAiHasKey()               { return simAiKey; }
bool    netAiSetKey(const char *k)  {
  size_t n = strlen(k);
  if (n && (n < 20 || n > 200)) return false;
  strcpy(simAiKeyText, k); simAiKey = n > 0;
  return true;
}
bool    netAiAsk(const char *q) {
  if (simNet != NET_ONLINE) { simAi = AI_ERROR; simAiText = "Not connected to the WLAN."; return false; }
  strcpy(simAiQ, q); simAi = AI_ASKING; simAiTicks = 0; simAiText = SIM_AI_ANSWER;
  return true;
}
AiState     netAiState()  { if (simAi == AI_ASKING && ++simAiTicks > 400) simAi = AI_DONE; return simAi; }
const char *netAiAnswer() { return simAiText; }
