// MiniArcade Color: WLAN page, updates and firmware versions. Part of
// console.cpp, which includes it - not compiled on its own. The network
// itself is net.cpp, shared with the OLED console; the updates come from
// the colour console's own release channel (CONFIG_ARCADE_UPDATE_PATH).

/* The display chip is stored once ("tftc": 1 = ILI9341, 2 = ST7789). Each
   chip has its own firmware file in the release, and the GitHub update
   fetches the one for the stored chip. Flashing the other build over USB
   means the display was changed, so the stored chip follows the build.   */
#if CONFIG_IDF_TARGET_ESP32S3
#define FW_TARGET "s3"
#else
#define FW_TARGET "c3"
#endif
#if CONFIG_ARCADE_TFT_ST7789
#define TFT_CHIP_BUILT 2
#else
#define TFT_CHIP_BUILT 1
#endif
static void tftChipRemember() {
  prefs.begin("arcade", true);
  uint16_t chip = prefs.getUShort("tftc", 0);
  prefs.end();
  if (chip != TFT_CHIP_BUILT) {
    chip = TFT_CHIP_BUILT;
    prefs.begin("arcade", false);
    prefs.putUShort("tftc", chip);
    prefs.end();
  }
  netSetUpdateBin(chip == 2 ? "miniarcade-color-" FW_TARGET "-st7789.bin"
                            : "miniarcade-color-" FW_TARGET ".bin");
}

static bool wlanStay = false;        // keep the WLAN on after leaving this page
static bool phoneLink = false;       // WLAN on outside this page right now

static bool uploadShown = false;     // the upload question is on the screen right now
static void uploadPrompt();          // below, with the WLAN page

/* While the WLAN stays on outside the WLAN page, a firmware sent from the
   web page must still be confirmed on the console - on whatever screen it
   is (the web page waits 30 s for the answer).                         */
static void phoneHook() {
  netTick();
  lastInput = millis();
  if (!uploadShown && netJob() == JOB_ASKING) uploadPrompt();
}

void phoneLinkOff() {
  if (!phoneLink) return;
  phoneLink = false;
  pollHook = NULL;
  netStop();
}

static void wlanTitle(const char *t, col accent = C::BLUE) {
  gfx.gradient(0, 0, SCR_W, SCR_H, colDark(accent, 3), C::BLACK);
  gfx.gradient(0, 0, SCR_W, 32, accent, colDark(accent, 2));
  gfx.textShadow(10, 4, t, C::WHITE, C::BLACK, F_BOLD);
}

static void wlanNeedsClock() {
  btnClear();
  while (poll()) {
    wlanTitle("WLAN");
    gfx.center(80, TR("WLAN needs 80 MHz or more", "WLAN braucht 80 MHz oder mehr"), C::WHITE);
    gfx.center(120, TR("OK = switch to 160 MHz", "OK = auf 160 MHz stellen"), C::LIME);
    gfx.center(146, TR("and restart", "und neu starten"), C::LIME);
    gfx.send();
    if (btn(B_OK)) { cfgClock = 160; saveCfg(); netRestart(); }
  }
}

static void bar(int16_t x, int16_t y, int16_t w, uint8_t pct, col c) {
  gfx.rframe(x, y, w, 16, 4, C::WHITE);
  gfx.rbox(x + 3, y + 3, (int16_t)((w - 6) * pct / 100), 10, 3, c);
}

static void askBox(const char *title, const char *l1, const char *l2, const char *l3) {
  wlanTitle(title, C::ORANGE);
  if (l1) gfx.center(70, l1, C::WHITE);
  if (l2) gfx.center(100, l2, C::YELLOW, F_BOLD);
  if (l3) gfx.center(140, l3, C::LTGREY);
  gfx.center(196, TR("OK = yes      LEFT = no", "OK = ja      LEFT = nein"), C::LIME);
  gfx.send();
}

// the question, then the progress until the board restarts (or the answer was no)
static void uploadPrompt() {
  uploadShown = true;
  btnClear();
  for (;;) {
    bool alive = poll();                 // runs phoneHook too, which only ticks the network now
    NetJob jb = netJob();
    if (jb == JOB_ASKING) {
      if (!alive || btn(B_LEFT)) { netAnswer(false); sfx(300, 120); continue; }
      if (btn(B_OK)) { netAnswer(true); sfx(1200, 50); continue; }
      char b[40];
      snprintf(b, sizeof(b), "v%.31s", netAskVersion());
      askBox(TR("UPLOAD", "HOCHLADEN"), TR("install the firmware", "die Firmware installieren"), b, TR("from the web page?", "von der Webseite?"));
    } else if (jb == JOB_UPDATING || jb == JOB_DONE) {
      wlanTitle("UPDATE", C::GREEN);
      gfx.center(80, jb == JOB_DONE ? TR("done - restarting", "fertig - Neustart") : TR("installing...", "installiere..."), C::WHITE, F_BOLD);
      bar(30, 130, SCR_W - 60, netProgress(), C::LIME);
      gfx.center(180, TR("do not switch off", "nicht ausschalten"), C::YELLOW);
      gfx.send();
    } else break;
  }
  btnClear();
  uploadShown = false;
}

void wlanRun() {
  if (runClock < 80) { wlanNeedsClock(); return; }
  uint8_t sel = 0;
  bool leaving = false;
  if (phoneLink) { phoneLink = false; pollHook = NULL; }
  if (netHasConfig() && netState() != NET_ONLINE && netState() != NET_PLAY) netConnect();
  btnClear();
  for (;;) {
    netTick();
    bool alive = poll();
    lastInput = millis();
    NetState st = netState();
    NetJob jb = netJob();
    bool locked = jb == JOB_UPDATING || jb == JOB_DONE || jb == JOB_ASKING;
    if (!alive) { leaving = true; gfx.clear(); }
    if (leaving && !locked && jb != JOB_CHECKING) break;
    if (leaving && !locked) btnClear();
    if (jb == JOB_ASKING) {                                  // a file came in over the web page
      if (btn(B_OK))   { netAnswer(true);  sfx(1200, 50); }
      if (btn(B_LEFT)) { netAnswer(false); sfx(300, 120); }
      char b[40];
      snprintf(b, sizeof(b), "v%.31s", netAskVersion());
      askBox(TR("UPLOAD", "HOCHLADEN"), TR("install the firmware", "die Firmware installieren"), b, TR("from the web page?", "von der Webseite?"));
      continue;
    }
    if (phoneAsking()) {
      if (btn(B_OK))   { phoneAnswer(true);  sfx(1200, 50); }
      if (btn(B_LEFT)) { phoneAnswer(false); sfx(300, 120); }
      askBox(TR("BACKUP", "SICHERUNG"), TR("restore the backup", "Sicherung zurückholen"), TR("from the phone?", "vom Handy?"),
             TR("all saves are replaced", "alle Spielstände ersetzt"));
      continue;
    }
    if (locked) {
      wlanTitle("UPDATE", C::GREEN);
      gfx.center(80, jb == JOB_DONE ? TR("done - restarting", "fertig - Neustart") : TR("installing...", "installiere..."), C::WHITE, F_BOLD);
      bar(30, 130, SCR_W - 60, netProgress(), C::LIME);
      gfx.center(180, TR("do not switch off", "nicht ausschalten"), C::YELLOW);
      gfx.send();
      continue;
    }
    if (st == NET_SETUP) {
      wlanTitle(TR("WLAN SETUP", "WLAN EINRICHTEN"), C::PURPLE);
      gfx.center(52, TR("on the phone join", "am Handy verbinden mit"), C::WHITE);
      gfx.center(80, netApName(), C::YELLOW, F_BOLD);
      gfx.center(120, TR("the setup page opens,", "die Seite öffnet sich,"), C::WHITE);
      gfx.center(144, TR("else go to 192.168.4.1", "sonst 192.168.4.1 öffnen"), C::WHITE);
      gfx.center(200, TR("hold OK = leave", "OK halten = verlassen"), C::GREY, F_SMALL);
      gfx.send();
      continue;
    }
    if (st == NET_PLAY) {
      if (btn(B_LEFT))  { netApNewPass();    sfx(900, 40); }
      if (btn(B_RIGHT)) { netApToggleOpen(); sfx(700, 40); }
      if (btn(B_OK))    { netStop(); sfx(1200, 50); btnClear(); continue; }
      wlanTitle(TR("PHONE HOTSPOT", "HANDY-HOTSPOT"), C::PURPLE);
      char b[48];
      snprintf(b, sizeof(b), TR("join  %s", "Netz  %s"), netApName());
      gfx.text(16, 50, b, C::WHITE);
      const char *pw = netApPass();
      if (pw[0]) snprintf(b, sizeof(b), TR("pass  %.24s", "PW    %.24s"), pw); else snprintf(b, sizeof(b), TR("no password - open!", "kein Passwort - offen!"));
      gfx.text(16, 80, b, pw[0] ? C::YELLOW : C::ORANGE);
      gfx.text(16, 110, TR("open  http://192.168.4.1", "öffne http://192.168.4.1"), C::CYAN);
      gfx.text(16, 160, TR("LEFT new password  RIGHT on/off", "LEFT neues Passwort  RIGHT an/aus"), C::LTGREY, F_SMALL);
      gfx.text(16, 176, TR("OK = stop the hotspot", "OK = Hotspot beenden"), C::LTGREY, F_SMALL);
      gfx.send();
      continue;
    }

    bool on = st == NET_CONNECTING || st == NET_ONLINE;
    const uint8_t ITEMS = 7;
    if (btn(B_UP))   { sel = sel ? sel - 1 : ITEMS - 1; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % ITEMS; sfx(700, 15); }
    if (btn(B_OK)) {
      sfx(1200, 50);
      if (sel == 0) { if (on) netStop(); else if (netHasConfig()) netConnect(); else netSetup(); }
      else if (sel == 1) netSetup();
      else if (sel == 2) {
        if (st != NET_ONLINE) sfx(300, 120);
        else if (jb == JOB_NEWER) netInstallUpdate();
        else if (jb != JOB_CHECKING) netCheckUpdate();
      } else if (sel == 3) netPlay();
      else if (sel == 4) netForget();
      else if (sel == 5) wlanStay = !wlanStay;
      else leaving = true;
      btnClear();
      continue;
    }
    char info[48], item[7][32];
    col infoC = C::LTGREY;
    if (jb == JOB_ERROR)           { snprintf(info, sizeof(info), "%s", netError()); infoC = C::ORANGE; }
    else if (st == NET_ONLINE)     { snprintf(info, sizeof(info), "http://%s", netAddress()); infoC = C::CYAN; }
    else if (st == NET_CONNECTING)   snprintf(info, sizeof(info), TR("joining %s", "verbinde %s"), netSsid());
    else if (st == NET_FAILED)     { snprintf(info, sizeof(info), TR("can't join %s", "%s geht nicht"), netSsid()); infoC = C::ORANGE; }
    else if (netHasConfig())         snprintf(info, sizeof(info), TR("network %s", "Netz %s"), netSsid());
    else                             snprintf(info, sizeof(info), TR("no network saved", "kein Netz"));
    snprintf(item[0], 32, st == NET_ONLINE ? TR("disconnect", "trennen")
                        : (st == NET_CONNECTING ? TR("stop joining", "Verbinden abbrechen") : TR("connect", "verbinden")));
    snprintf(item[1], 32, TR("set up with phone", "mit Handy einrichten"));
    if (jb == JOB_CHECKING)        snprintf(item[2], 32, TR("asking GitHub...", "frage GitHub..."));
    else if (jb == JOB_NEWER)      snprintf(item[2], 32, TR("install %s", "%s installieren"), netRemoteVersion());
    else if (jb == JOB_UPTODATE)   snprintf(item[2], 32, TR("up to date", "aktuell"));
    else                           snprintf(item[2], 32, TR("check for update", "nach Update suchen"));
    snprintf(item[3], 32, TR("phone hotspot", "Handy-Hotspot"));
    snprintf(item[4], 32, TR("forget network", "Netz vergessen"));
    snprintf(item[5], 32, TR("stay online: %s", "online bleiben: %s"), wlanStay ? TR("yes", "ja") : TR("no", "nein"));
    snprintf(item[6], 32, TR("back", "zurück"));

    wlanTitle("WLAN");
    {
      char v[20];
      snprintf(v, sizeof(v), "v%s", netVersion());
      gfx.right(SCR_W - 10, 12, v, C::LTGREY, F_SMALL);
      const char *s = st == NET_ONLINE ? "online" : (st == NET_CONNECTING ? TR("joining", "verbinde")
                    : (st == NET_FAILED ? TR("failed", "Fehler") : "offline"));
      col dc = st == NET_ONLINE ? C::LIME : (st == NET_FAILED ? C::RED : C::GREY);
      if (st != NET_CONNECTING || (millis() / 300) % 2) gfx.disc(120, 16, 5, dc);
      gfx.text(132, 9, s, C::WHITE);
    }
    gfx.text(12, 40, info, infoC);
    {                                    // free memory: TLS for the GitHub update needs a 16 kB block
      char m[24];
      snprintf(m, sizeof(m), "RAM %luk/%luk", (unsigned long)(tvFreeHeap(NULL) / 1024), (unsigned long)(tvLargestBlock() / 1024));
      gfx.right(SCR_W - 10, 42, m, tvLargestBlock() < 17 * 1024 ? C::ORANGE : C::GREY, F_SMALL);
    }
    for (uint8_t i = 0; i < ITEMS; i++) {
      int16_t y = 62 + i * 25;
      if (i == sel) gfx.rbox(6, y - 3, SCR_W - 12, 23, 5, C::BLUE);
      gfx.text(16, y + 1, item[i], i == sel ? C::WHITE : C::LTGREY);
    }
    gfx.send();
  }
  if (wlanStay && (netState() == NET_ONLINE || netState() == NET_PLAY)) { phoneLink = true; pollHook = phoneHook; }
  else netStop();
}

// ---------------- firmware version ----------------
/* Both update slots: the running firmware and the one before. At start the
   page shows for 3 s whenever both can be started, so a broken update
   never locks the player in; a fresh update is kept after a key press. */
void versionRun(bool atBoot) {
  FwSlot s[2];
  fwSlots(s);
  if (atBoot && !(s[1].present && s[1].bootable)) return;
  uint8_t sel = 0;
  bool waiting = atBoot;
  uint32_t t0 = millis();
  btnClear();
  for (;;) {
    bool alive = poll();
    if (!alive) { if (!atBoot) return; gfx.clear(); }
    if (btn(B_UP))   { sel = 0; waiting = false; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = 1; waiting = false; sfx(700, 15); }
    bool pressed = btn(B_OK);
    uint32_t left = 3000 - (millis() - t0 < 3000 ? millis() - t0 : 3000);
    if (pressed || (waiting && !left)) {
      if (sel == 0) {
        if (pressed) { fwConfirm(); fwUnconfirmed = false; }
        sfx(1200, 50);
        return;
      }
      if (s[1].present && s[1].bootable) {
        wlanTitle("VERSION");
        gfx.center(100, TR("STARTING", "STARTE"), C::WHITE, F_BOLD);
        char b[40];
        snprintf(b, sizeof(b), "v%.31s", s[1].version);
        gfx.center(140, b, C::YELLOW);
        gfx.send();
        sfx(1200, 50);
        delay(400);
        fwStartOther();
        s[1].bootable = false;
      }
      sfx(300, 120);
      continue;
    }
    wlanTitle("VERSION");
    if (waiting) { char b[8]; snprintf(b, sizeof(b), "%us", (unsigned)((left + 999) / 1000 % 10)); gfx.right(SCR_W - 12, 8, b, C::YELLOW, F_BOLD); }
    for (uint8_t i = 0; i < 2; i++) {
      int16_t y = 56 + i * 52;
      const char *tag = i == 0 ? (s[0].pending ? TR("new", "neu") : TR("running", "läuft"))
                               : (!s[1].present ? "" : (!s[1].bootable ? TR("broken", "defekt") : TR("previous", "vorige")));
      char b[40];
      if (s[i].present) snprintf(b, sizeof(b), "v%.31s", s[i].version); else snprintf(b, sizeof(b), TR("no other version", "keine andere Version"));
      gfx.rbox(10, y, SCR_W - 20, 42, 6, i == sel ? C::BLUE : C::DKGREY);
      gfx.text(22, y + 9, b, C::WHITE, s[i].present ? F_BOLD : F_TEXT);
      gfx.right(SCR_W - 22, y + 14, tag, C::YELLOW);
    }
    gfx.center(182, TR("UP/DOWN, OK = start", "UP/DOWN, OK = starten"), C::LTGREY);
    if (!atBoot) gfx.center(206, TR("hold OK = back", "OK halten = zurück"), C::GREY, F_SMALL);
    gfx.send();
  }
}
