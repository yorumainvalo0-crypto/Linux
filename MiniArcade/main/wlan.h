// MiniArcade: WLAN page and firmware versions (ESP-IDF build only). Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  WLAN   (ESP-IDF build only - the Arduino IDE has no net.h)
// =========================================================
/* Joins the saved network and serves the update page, opens the
   "MiniArcade-XXXX" hotspot to set the network up from a phone and
   installs new firmware from the GitHub releases. The radio is only on
   while this page is open.                                              */
#if __has_include("net.h")
#include "net.h"
#define HAVE_NET 1

static void wlanTitle(const char *t) {
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, t);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  oled.setFont(FONT);
}

static void wlanNeedsClock() {           // the radio does not run below 80 MHz
  btnClear();
  while (poll()) {
    wlanTitle("WLAN");
    centerStr(30, "WLAN needs 80 MHz+");
    centerStr(44, "OK = switch to 80 MHz");
    centerStr(54, "and restart");
    oled.sendBuffer();
    if (btn(B_OK)) { cfgClock = 80; saveCfg(); netRestart(); }
  }
}

static void wlanProgress(NetJob jb) {
  wlanTitle("UPDATE");
  centerStr(30, jb == JOB_DONE ? "done - restarting" : "installing...");
  oled.drawFrame(10, 36, 108, 8);
  oled.drawBox(12, 38, (int16_t)(104 * netProgress() / 100), 4);
  centerStr(60, "do not switch off");
}

static void wlanSetupHelp() {
  wlanTitle("WLAN SETUP");
  centerStr(25, "on the phone join");
  oled.setFont(FONT_B);
  centerStr(37, netApName());
  oled.setFont(FONT);
  centerStr(47, "the setup page opens,");
  centerStr(56, "else go to 192.168.4.1");
  centerStr(64, "hold OK = leave");
}

void wlanRun() {
  if (runClock < 80) { wlanNeedsClock(); return; }
  uint8_t sel = 0;
  bool leaving = false;
  if (netHasConfig()) netConnect();
  btnClear();
  for (;;) {
    bool alive = poll();
    lastInput = millis();                  // no deep sleep while the radio is on
    netTick();
    NetState st = netState();
    NetJob   jb = netJob();
    bool locked = (jb == JOB_UPDATING || jb == JOB_DONE || jb == JOB_ASKING);   // update running
    if (!alive) {                          // hold OK = back
      leaving = true;
      oled.clearBuffer();
      oled.setFont(FONT);
    }
    if (leaving && !locked && jb != JOB_CHECKING) break;    // let a GitHub check finish
    if (leaving && !locked) btnClear();                     // ... and ignore the keys meanwhile
    if (jb == JOB_ASKING) {                // a file came in over the web page
      if (btn(B_OK))                        { netAnswer(true);  sfx(1200, 50); }
      if (btn(B_LEFT) || btn(B_RIGHT))      { netAnswer(false); sfx(300, 120); }
      char b[40];
      wlanTitle("UPLOAD");
      centerStr(28, "install firmware");
      snprintf(b, sizeof(b), "v%.31s", netAskVersion());
      oled.setFont(FONT_B);
      centerStr(42, b);
      oled.setFont(FONT);
      centerStr(54, "from the web page?");
      centerStr(63, "OK = yes  LEFT = no");
      oled.sendBuffer();
      continue;
    }
    if (locked) { wlanProgress(jb); oled.sendBuffer(); continue; }
    if (st == NET_SETUP) { wlanSetupHelp(); oled.sendBuffer(); continue; }

    bool on = (st == NET_CONNECTING || st == NET_ONLINE);
    if (btn(B_UP)   && sel)     { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < 4) { sel++; sfx(700, 15); }
    if (btn(B_OK)) {
      sfx(1200, 50);
      if (sel == 0) {
        if (on) netStop();
        else if (netHasConfig()) netConnect();
        else netSetup();
      } else if (sel == 1) {
        netSetup();
      } else if (sel == 2) {
        if (st != NET_ONLINE)       sfx(300, 120);
        else if (jb == JOB_NEWER)   netInstallUpdate();
        else if (jb != JOB_CHECKING) netCheckUpdate();
      } else if (sel == 3) {
        netForget();
      } else leaving = true;
      btnClear();
      continue;
    }

    char info[26], item[5][26];
    if (jb == JOB_ERROR)            snprintf(info, sizeof(info), "%s", netError());
    else if (st == NET_ONLINE)      snprintf(info, sizeof(info), "http://%s", netAddress());
    else if (st == NET_CONNECTING)  snprintf(info, sizeof(info), "joining %s", netSsid());
    else if (st == NET_FAILED)      snprintf(info, sizeof(info), "can't join %s", netSsid());
    else if (netHasConfig())        snprintf(info, sizeof(info), "network %s", netSsid());
    else                            snprintf(info, sizeof(info), "no network saved");
    snprintf(item[0], 26, on ? "disconnect" : "connect");
    snprintf(item[1], 26, "set up with phone");
    if (jb == JOB_CHECKING)         snprintf(item[2], 26, "asking GitHub...");
    else if (jb == JOB_NEWER)       snprintf(item[2], 26, "install %s", netRemoteVersion());
    else if (jb == JOB_UPTODATE)    snprintf(item[2], 26, "up to date");
    else                            snprintf(item[2], 26, "check for update");
    snprintf(item[3], 26, "forget network");
    snprintf(item[4], 26, "back");

    wlanTitle("WLAN");
    {
      char v[16];
      snprintf(v, sizeof(v), "v%s", netVersion());
      rightStr(12, v);
    }
    oled.drawStr(2, 23, info);
    for (uint8_t i = 0; i < 5; i++) {
      uint8_t y = 24 + i * 8;                // last baseline: row 63
      if (i == sel) { oled.drawBox(0, y, SCR_W, 8); oled.setDrawColor(0); }
      oled.drawStr(3, y + 7, item[i]);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
  netStop();
}

// ---------------- firmware version ----------------
/* Both update slots: the running firmware and the one before. At start this
   page shows up for 3 s whenever both can be started, so a new version that
   turns out broken never locks the player in. A fresh update is only kept
   for good after a key press (here or in the menu) - switching the board
   off and on before that also brings the previous version back.         */
void versionRun(bool atBoot) {
  FwSlot s[2];
  fwSlots(s);
  if (atBoot && !(s[1].present && s[1].bootable)) return;
  uint8_t  sel = 0;
  bool     waiting = atBoot;                 // count down until a key is pressed
  uint32_t t0 = millis();
  btnClear();
  for (;;) {
    bool alive = poll();
    if (!alive) {
      if (!atBoot) return;                     // hold OK = back to the settings
      oled.clearBuffer();
      oled.setFont(FONT);
    }
    if (btn(B_UP))   { sel = 0; waiting = false; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = 1; waiting = false; sfx(700, 15); }
    bool pressed = btn(B_OK);
    uint32_t left = 3000 - (millis() - t0 < 3000 ? millis() - t0 : 3000);
    if (pressed || (waiting && !left)) {
      if (sel == 0) {
        if (pressed) { fwConfirm(); fwUnconfirmed = false; }   // display and keys work
        sfx(1200, 50);
        return;
      }
      if (s[1].present && s[1].bootable) {
        oled.clearBuffer();
        oled.setFont(FONT_B);
        centerStr(36, "STARTING");
        oled.setFont(FONT);
        {
          char b[40];
          snprintf(b, sizeof(b), "v%.31s", s[1].version);
          centerStr(50, b);
        }
        oled.sendBuffer();
        sfx(1200, 50);
        delay(400);
        fwStartOther();                        // only returns when it cannot
        s[1].bootable = false;
      }
      sfx(300, 120);
      continue;
    }

    oled.setFont(FONT_B);
    oled.drawStr(2, 12, "VERSION");
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    oled.setFont(FONT);
    if (waiting) {
      char b[8];
      snprintf(b, sizeof(b), "%us", (unsigned)((left + 999) / 1000 % 10));
      rightStr(12, b);
    }
    for (uint8_t i = 0; i < 2; i++) {
      uint8_t y = 18 + i * 14;
      const char *tag;
      if (i == 0)                 tag = s[0].pending ? "new" : "running";
      else if (!s[1].present)     tag = "";
      else if (!s[1].bootable)    tag = "broken";
      else                        tag = "previous";
      char b[40];
      if (s[i].present) snprintf(b, sizeof(b), "v%.31s", s[i].version);
      else              snprintf(b, sizeof(b), "no other version");
      if (i == sel) { oled.drawBox(0, y, SCR_W, 13); oled.setDrawColor(0); }
      oled.setFont(s[i].present ? FONT_B : FONT);
      oled.drawStr(3, y + 11, b);
      oled.setFont(FONT);
      rightStr(y + 10, tag);
      oled.setDrawColor(1);
    }
    centerStr(55, "UP/DOWN, OK = start");
    if (!atBoot) centerStr(63, "hold OK = back");
    oled.sendBuffer();
  }
}
#endif
