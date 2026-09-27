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
  NET_SETUP         // own hotspot netApName(), setup page at 192.168.4.1
};

enum NetJob : uint8_t {
  JOB_IDLE,
  JOB_CHECKING,     // asking GitHub for the latest version
  JOB_UPTODATE,     // the latest release is what is running
  JOB_NEWER,        // another version is online: netRemoteVersion()
  JOB_UPDATING,     // writing a new image (GitHub or upload): netProgress()
  JOB_DONE,         // image written, the board restarts in a moment
  JOB_ERROR         // netError() says why
};

bool        netHasConfig();       // WLAN name and password stored?
const char *netSsid();
void        netConnect();         // join the saved network, start the web page
void        netSetup();           // open the setup hotspot
void        netStop();            // web page and radio off
void        netForget();          // drop the stored WLAN
void        netTick();            // call from the UI loop: deferred work
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
