// MiniArcade Color: finding the SD card. Part of console.cpp, which includes
// it - not compiled on its own. The card shares the display's SCK, SDI and
// SDO; its CS goes on any free pin. That pin is searched once at the first
// start and again from the settings; it is kept as "sdcs" (pin + 1,
// 255 = searched, nothing found).
#include "sdcard.h"

static int8_t sdCs = -1;                         // the card's CS pin, -1 = none

static bool sdPinFree(uint8_t p) { return gyroPinFree(p) && p != gyroSda && p != gyroScl; }

static void sdScreen(const char *l1, const char *l2) {
  gfx.gradient(0, 0, SCR_W, SCR_H, C::DKBLUE, C::BLACK);
  gfx.center(14, TR("SD CARD", "SD-KARTE"), C::WHITE, F_BOLD);
  gfx.center(100, l1, C::YELLOW);
  if (l2) gfx.center(130, l2, C::LTGREY, F_SMALL);
  gfx.send();
}

static void sdSave(uint16_t v) {
  prefs.begin("arcade", false);
  prefs.putUShort("sdcs", v);
  prefs.end();
}

// every free pin as CS, the saved one first; true = a card answered
static bool sdSearch(bool show) {
  sdUnmount();
  sdCs = -1;
  if (tftPins.miso < 0) { if (show) { sdScreen(TR("display SDO not wired", "Display-SDO nicht verbunden"), TR("the card answers on SDO (MISO)", "die Karte antwortet über SDO (MISO)")); delay(2000); } return false; }
  prefs.begin("arcade", true);
  uint16_t saved = prefs.getUShort("sdcs", 0);
  prefs.end();
  int8_t order[sizeof(PINS_ALL) + 1];
  uint8_t n = 0;
  if (saved > 0 && saved < 255 && sdPinFree((uint8_t)(saved - 1))) order[n++] = (int8_t)(saved - 1);
  for (uint8_t i = 0; i < CAND_N; i++)
    if (sdPinFree(CAND[i]) && (n == 0 || CAND[i] != order[0])) order[n++] = (int8_t)CAND[i];
  for (uint8_t i = 0; i < n; i++) {
    char b[32];
    snprintf(b, sizeof(b), TR("CS on GPIO%d ?", "CS an GPIO%d ?"), order[i]);
    if (show) sdScreen(TR("looking for the card...", "suche die Karte..."), b);
    if (sdMount(order[i])) { sdCs = order[i]; sdSave((uint16_t)(sdCs + 1)); return true; }
  }
  sdSave(255);
  return false;
}

// at the start: the saved pin only, or one search on the very first start
void sdBoot() {
  prefs.begin("arcade", true);
  uint16_t saved = prefs.getUShort("sdcs", 0);
  prefs.end();
  if (saved == 0) { sdSearch(true); return; }
  if (saved < 255 && sdPinFree((uint8_t)(saved - 1)) && tftPins.miso >= 0 && sdMount((int8_t)(saved - 1))) sdCs = (int8_t)(saved - 1);
}

// the settings line
static char sdLast[36] = "";                     // the settings line, asked every 2 s
static void sdLine(char *out, size_t n) {
  static uint32_t at = 0;
  char *last = sdLast;
  if (last[0] && millis() - at < 2000) { snprintf(out, n, "%s", last); return; }   // the card is asked every 2 s
  at = millis();
  if (sdCs >= 0 && sdReady()) {
    uint32_t mb = sdSizeMB(), fr = sdFreeMB();
    snprintf(out, n, TR("sd card      %lu.%lu GB, %lu%% free", "SD-Karte     %lu.%lu GB, %lu%% frei"), (unsigned long)(mb / 1024), (unsigned long)(mb % 1024 * 10 / 1024),
             (unsigned long)(mb ? (uint64_t)fr * 100 / mb : 0));
  } else {
    sdCs = -1;
    snprintf(out, n, TR("sd card      none (OK: search)", "SD-Karte     keine (OK sucht)"));
  }
  snprintf(last, sizeof(sdLast), "%s", out);
}

// OK on that line: search, then say what was found
static void sdSetup() {
  bool ok = sdSearch(true);
  sdLast[0] = 0;
  char b[40];
  if (ok) snprintf(b, sizeof(b), TR("found: CS on GPIO%d, %lu MB", "gefunden: CS an GPIO%d, %lu MB"), sdCs, (unsigned long)sdSizeMB());
  sdScreen(ok ? TR("SD CARD READY", "SD-KARTE BEREIT") : TR("NO CARD FOUND", "KEINE KARTE"),
           ok ? b : TR("CS on a free pin, SCK/SDI/SDO shared", "CS an einen freien Pin, SCK/SDI/SDO geteilt"));
  sfx(ok ? 1500 : 300, 120);
  delay(1800);
  btnClear();
}
