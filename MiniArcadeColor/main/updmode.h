// MiniArcade Color: the update mode. Part of console.cpp, which includes it -
// not compiled on its own.
/* An update needs a lot of memory at once - the TLS connection to GitHub
   takes a 16 kB block on its own - and beside the 77 kB picture the C3
   has too little left. So "install" and "upload" restart the board into
   this mode: no games, no SD card, no gyroscope; the picture keeps only
   its top rows for a status line and a bar, the rest goes to the heap.
   That memory cannot be taken back, so the board always restarts at the
   end - after the update, after an error and after a cancel.          */
#include "updmem.h"

#define UPD_ROWS 80                            // rows kept for the screen; everything below is given away

static void updScreen(const char *line, const char *small, int8_t pct, col c) {
  gfx.fill(0, 0, SCR_W, UPD_ROWS, C::BLACK);   // only ever these rows
  gfx.fill(0, 0, SCR_W, 22, colDark(C::GREEN, 2));
  gfx.text(10, 5, TR("UPDATE MODE", "UPDATE-MODUS"), C::WHITE, F_BOLD);
  gfx.text(10, 28, line, c);
  if (pct >= 0) {
    gfx.rframe(10, 44, SCR_W - 20, 14, 4, C::WHITE);
    gfx.rbox(13, 47, (int16_t)((SCR_W - 26) * pct / 100), 8, 3, C::LIME);
  }
  if (small) gfx.text(10, 64, small, C::LTGREY, F_SMALL);
  gfx.send();
}

// waits ms while the network runs; a key held for 2 s cancels (restart)
static bool updWait(uint32_t ms) {
  static uint32_t heldSince = 0;
  for (uint32_t t0 = millis(); millis() - t0 < ms; ) {
    netTick();
    bool any = false;
    for (uint8_t i = 0; i < B_COUNT; i++) if (BTN_PIN[i] != 255 && rawPressed(i)) any = true;
    if (!any) heldSince = 0;
    else if (!heldSince) heldSince = millis();
    else if (millis() - heldSince > 2000) return false;
    delay(20);
  }
  return true;
}

static void updEnd(const char *line, const char *small, col c) {
  updScreen(line, small, -1, c);
  updWait(5000);
  updRestart();
}

void updModeRun(uint8_t what) {
  setCpuFrequencyMhz(160);                     // the WLAN needs 80 MHz or more
  runClock = 160;
  gfx.overlay = nullptr;                       // no fps corner: it would draw into the rows given away
  gfx.clear();
  gfx.send();                                  // the whole panel black once ...
  updScreen(TR("freeing memory...", "Speicher freigeben..."), NULL, -1, C::WHITE);
  gfx.liveRows = UPD_ROWS;                     // ... then only the top rows are ever sent again
  bool given = updDonate(gfx.buffer() + UPD_ROWS * SCR_W, (size_t)SCR_W * (SCR_H - UPD_ROWS));
  char small[48];
  snprintf(small, sizeof(small), TR("RAM %luk free, %luk block%s", "RAM %luk frei, %luk Block%s"),
           (unsigned long)(tvFreeHeap(NULL) / 1024), (unsigned long)(tvLargestBlock() / 1024), given ? "" : " (!)");

  if (!netHasConfig()) updEnd(TR("no WLAN saved", "kein WLAN gespeichert"), TR("set it up on the WLAN page", "auf der WLAN-Seite einrichten"), C::ORANGE);
  netConnect();
  updScreen(TR("joining the WLAN...", "verbinde mit dem WLAN..."), small, -1, C::WHITE);
  for (uint32_t t0 = millis(); netState() != NET_ONLINE; ) {
    if (netState() == NET_FAILED || millis() - t0 > 30000)
      updEnd(TR("WLAN not reached", "WLAN nicht erreicht"), netSsid(), C::ORANGE);
    if (!updWait(100)) updRestart();
  }

  if (what == UPD_GITHUB) netInstallUpdate();
  uint32_t idle = millis();
  for (;;) {
    NetJob jb = netJob();
    if (jb == JOB_ASKING) netAnswer(true);      // the update mode was chosen on the console itself
    if (jb == JOB_DONE) updEnd(TR("done - restarting", "fertig - Neustart"), NULL, C::LIME);
    if (jb == JOB_ERROR) updEnd(netError(), TR("restarting - try again", "Neustart - nochmal versuchen"), C::ORANGE);
    if (jb == JOB_UPDATING || jb == JOB_CHECKING) {
      idle = millis();
      char p[40];
      snprintf(p, sizeof(p), TR("installing... %u%%", "installiere... %u%%"), netProgress());
      updScreen(p, TR("do not switch off", "nicht ausschalten"), netProgress(), C::WHITE);
    } else if (what == UPD_UPLOAD) {
      char p[48];
      snprintf(p, sizeof(p), TR("phone: http://%s", "Handy: http://%s"), netAddress());
      updScreen(p, TR("\"Update from a file\" - hold a key = cancel", "\"Update from a file\" - Taste halten = Abbruch"), -1, C::CYAN);
      if (millis() - idle > 600000) updRestart();   // nothing came in 10 minutes
    }
    if (!updWait(100)) updRestart();
  }
}
