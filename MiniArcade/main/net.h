// WLAN for the IDF build: joining a network, the "MiniArcade-XXXX" setup
// hotspot, the local update page and updates from GitHub releases.
// Plain functions, so the sketch can drive everything from its menu loop.
#pragma once
#include <stdint.h>

enum NetState : uint8_t {
  NET_OFF,          // radio off
  NET_CONNECTING,   // joining the saved network
  NET_ONLINE,       // connected, update page at http://<netAddress()>/
  NET_FAILED,       // saved network not reachable or wrong password
  NET_SETUP,        // own hotspot netApName(), setup page at 192.168.4.1
  NET_PLAY          // phone hotspot netApName() (password netApPass()), phone pages at 192.168.4.1
};

enum NetJob : uint8_t {
  JOB_IDLE,
  JOB_CHECKING,     // asking GitHub for the latest version
  JOB_UPTODATE,     // the latest release is what is running
  JOB_NEWER,        // another version is online: netRemoteVersion()
  JOB_UPDATING,     // writing a new image (GitHub or upload): netProgress()
  JOB_DONE,         // image written, the board restarts in a moment
  JOB_ERROR,        // netError() says why
  JOB_ASKING        // a file was uploaded: install it? netAnswer() decides
};

bool        netHasConfig();       // WLAN name and password stored?
const char *netSsid();
void        netConnect();         // join the saved network, start the web page
void        netSetup();           // open the setup hotspot
void        netStop();            // web page and radio off
void        netPlay();            // phone hotspot: the phone pages without a WLAN around
const char *netApPass();          // its password, "" = open to everybody
void        netApNewPass();       // a new random password (restarts a running hotspot)
void        netApToggleOpen();    // password on / off (restarts a running hotspot)
bool        netApSetPass(const char *p);   // from the phone: 8..63 characters, "" = open
void        netForget();          // drop the stored WLAN
void        netTick();            // call from the UI loop: deferred work
bool        netRadioLink(uint8_t channel);   // radio only, for ESP-NOW (multiplayer)
void        netRestart();
NetState    netState();
const char *netAddress();         // IP of the web page, "" while unknown
const char *netApName();
const char *netVersion();         // version of the running firmware

void        netCheckUpdate();     // runs in the background, see netJob()
void        netInstallUpdate();
NetJob      netJob();
const char *netRemoteVersion();
uint8_t     netProgress();        // 0..100 while JOB_UPDATING
const char *netError();
const char *netAskVersion();      // version of the uploaded file while JOB_ASKING
void        netAnswer(bool yes);  // the player's decision on the device

void        netRestartLater();    // restart in a moment (after a page went out)

// ---------------- pages for a phone (phone.cpp) ----------------
/* With the console in a WLAN, a phone in the same network gets: stats and
   awards, settings, a live picture of the screen, a controller, a Sokoban
   level editor and backups. The sketch answers the app...() calls; all but
   appKey run in the UI loop (from netTick), so they may use the game state. */
int         appStats(char *out, int max);                    // JSON
int         appSettings(char *out, int max);                 // JSON
const char *appSet(const char *key, const char *val);        // NULL = done, else why not
void        appScreen(uint8_t *out);                         // 1024 bytes, SSD1306 page order
const char *appLevel(uint8_t slot, const char *text);        // NULL = saved, else why not
int         appLevelGet(uint8_t slot, char *out, int max);   // "" when the slot is free
void        appKey(uint8_t key, bool down);                  // runs in the web task

bool        phoneAsking();        // a backup came in: restore it? (answered on the WLAN page)
void        phoneAnswer(bool yes);
void        phoneTick();          // from netTick: answers the waiting page
void        phoneRegister(void *server);   // the phone pages, from net.cpp
extern const char PHONE_LINKS[];  // part of the main page

// ---------------- firmware slots ----------------
/* Two app slots: the running firmware and the one before (or the one just
   downloaded). A new firmware counts as "pending" until fwConfirm(); if the
   board restarts before that, the bootloader goes back to the other slot. */
struct FwSlot {
  char version[32];
  bool present;          // holds a firmware
  bool bootable;         // complete, checksum ok and not marked as failed
  bool pending;          // started, but not confirmed yet
};
void fwSlots(FwSlot s[2]);    // [0] = running, [1] = the other slot
bool fwStartOther();          // restarts into the other slot, false if it cannot
void fwConfirm();             // the running firmware works - keep it
bool fwPending();
