// MiniArcade Color: the TV stream's records, decoded straight into the
// frame buffer, and the clock that decides when a frame is shown. The
// format is made by the MiniArcade server (MiniArcadeServer/tvserver/
// codec.py describes it; its decode_frame() mirrors tvDecode() below).
//   record  = [u32 length][payload], little endian
//   'I' info:  state, mode, gen u16, pos u32, dur u32, fps, title\0
//   'F' frame: flags (1 key, 2 sharp), seq u16, ts u32, tile operations
// Fluent: 160 x 120 shown doubled, 20 x 15 tiles; sharp: 320 x 240, 40 x 30.
// An operation is kind << 5 | (tiles - 1) for 1..32 tiles in a row.
#pragma once
#include <stdint.h>
#include <string.h>
#include "gfx.h"

enum : uint8_t { TVK_SKIP, TVK_FILL, TVK_FLAT, TVK_TWO, TVK_FOUR, TVK_RAW };
enum : uint8_t { TVS_IDLE, TVS_PLAYING, TVS_PAUSED, TVS_ENDED, TVS_ERROR };

// fills dst with n bytes of the record; false = they do not come (broken)
typedef bool (*TvRead)(void *ctx, uint8_t *dst, uint32_t n);

struct TvFrame { uint16_t seq; uint32_t ts; uint8_t flags; uint16_t skipped; };

struct TvInfo { uint8_t state, mode, fps; uint16_t gen; uint32_t pos, dur; char title[44]; };

static inline uint32_t tvU32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// an info payload (the 'I' included); false = too short
static bool tvParseInfo(const uint8_t *p, uint32_t n, TvInfo &in) {
  if (n < 15 || p[0] != 'I') return false;
  in.state = p[1]; in.mode = p[2]; in.gen = (uint16_t)(p[3] | p[4] << 8);
  in.pos = tvU32(p + 5); in.dur = tvU32(p + 9); in.fps = p[13];
  uint32_t k = 0;
  for (uint32_t i = 14; i < n && p[i] && k < sizeof(in.title) - 1; i++) in.title[k++] = (char)p[i];
  in.title[k] = 0;
  return true;
}

/* Paints a frame into fb (320 x 240). len = payload bytes after the 'F'.
   Tiles marked SKIP keep what fb has. False on a broken frame (fb may then
   be half painted - the server sends a key frame after a loss).       */
static bool tvDecode(uint8_t *fb, uint32_t len, TvRead rd, void *ctx, TvFrame &f) {
  static const uint8_t SIZE[6] = { 0, 0, 1, 10, 20, 64 };
  uint8_t d[64];
  if (len < 7 || !rd(ctx, d, 7)) return false;
  len -= 7;
  f.flags = d[0];
  f.seq = (uint16_t)(d[1] | d[2] << 8);
  f.ts = tvU32(d + 3);
  f.skipped = 0;
  const bool sharp = f.flags & 2;
  const int tw = sharp ? 40 : 20, n = sharp ? 1200 : 300, s = sharp ? 1 : 2;
  int t = 0;
  while (t < n) {
    uint8_t op, c = 0;
    if (!len-- || !rd(ctx, &op, 1)) return false;
    uint8_t kind = op >> 5;
    int cnt = (op & 31) + 1;
    if (t + cnt > n || kind > TVK_RAW) return false;
    if (kind == TVK_FILL && (!len-- || !rd(ctx, &c, 1))) return false;
    for (; cnt; cnt--, t++) {
      if (kind == TVK_SKIP) { f.skipped++; continue; }
      uint8_t need = SIZE[kind];
      if (need) {
        if (len < need || !rd(ctx, d, need)) return false;
        len -= need;
      }
      uint8_t *o = fb + (t / tw) * 8 * s * SCR_W + (t % tw) * 8 * s;
      if (kind == TVK_FILL || kind == TVK_FLAT) {
        if (kind == TVK_FLAT) c = d[0];
        for (int y = 0; y < 8 * s; y++) memset(o + y * SCR_W, c, 8 * s);
        continue;
      }
      for (int y = 0; y < 8; y++) {
        uint8_t row[8];
        if (kind == TVK_TWO) {
          uint8_t m = d[2 + y];
          for (int x = 0; x < 8; x++) row[x] = d[(m >> (7 - x)) & 1];
        } else if (kind == TVK_FOUR) {
          uint8_t a = d[4 + y * 2], b = d[5 + y * 2];
          for (int x = 0; x < 4; x++) { row[x] = d[(a >> (6 - 2 * x)) & 3]; row[x + 4] = d[(b >> (6 - 2 * x)) & 3]; }
        } else memcpy(row, d + y * 8, 8);
        uint8_t *r = o + y * s * SCR_W;
        if (s == 1) { memcpy(r, row, 8); continue; }
        for (int x = 0; x < 8; x++) r[2 * x] = r[2 * x + 1] = row[x];
        memcpy(r + SCR_W, r, 16);
      }
    }
  }
  return len == 0;
}

/* When a frame is shown: a local clock starts with the first frame (plus
   a little buffer); a frame waits for its time, and one that is too late
   is not sent to the panel (its tiles are in fb all the same, so the next
   frame is right again). Far behind (the network stalled): start anew.  */
#define TV_PREBUF_MS  250
#define TV_LATE_MS     60
#define TV_RESYNC_MS 1500
enum : uint8_t { TV_WAIT, TV_SHOW, TV_DROP };
struct TvClock { bool run; uint32_t t0, ts0; };

static uint8_t tvDue(TvClock &k, uint32_t now, uint32_t ts) {
  if (!k.run) { k.run = true; k.t0 = now + TV_PREBUF_MS; k.ts0 = ts; }
  int32_t at = (int32_t)(k.t0 + (ts - k.ts0) - now);
  if (at > 0) return TV_WAIT;
  if (-at > TV_RESYNC_MS) { k.t0 = now; k.ts0 = ts; return TV_SHOW; }
  return -at > TV_LATE_MS ? TV_DROP : TV_SHOW;
}
