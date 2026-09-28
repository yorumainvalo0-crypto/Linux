// Backup file of the saves (high scores, stats, awards, Mine world, own
// Sokoban levels, name, friends, settings): plain text, one line per stored
// value, so it can be read and even edited by hand:
//
//   MiniArcade backup 1
//   <namespace> <key> u16 <number>
//   <namespace> <key> str <text to the end of the line>
//   <namespace> <key> blob <base64>
//
// No hardware in here - phone.cpp reads and writes the NVS with it, the PC
// test checks that everything survives the round trip.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BK_HEAD "MiniArcade backup 1"
enum BkType : uint8_t { BK_U16 = 1, BK_STR, BK_BLOB };

static const char BK_B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// base64 of n bytes; returns the length written, -1 when out is too small
static int bkB64Enc(const uint8_t *in, int n, char *out, int max) {
  int need = (n + 2) / 3 * 4;
  if (need + 1 > max) return -1;
  int k = 0;
  for (int i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[k++] = BK_B64[v >> 18 & 63];
    out[k++] = BK_B64[v >> 12 & 63];
    out[k++] = i + 1 < n ? BK_B64[v >> 6 & 63] : '=';
    out[k++] = i + 2 < n ? BK_B64[v & 63] : '=';
  }
  out[k] = 0;
  return k;
}

// decodes base64 up to the end of the string; returns the byte count, -1 = bad
static int bkB64Dec(const char *in, uint8_t *out, int max) {
  uint32_t v = 0;
  int bits = 0, k = 0;
  for (; *in && *in != '='; in++) {
    const char *p = strchr(BK_B64, *in);
    if (!p || !*in) return -1;
    v = v << 6 | (uint32_t)(p - BK_B64);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (k >= max) return -1;
      out[k++] = (uint8_t)(v >> bits);
    }
  }
  return k;
}

static bool bkName(const char *s) {                // NVS names: 1..15 plain characters
  size_t n = strlen(s);
  if (!n || n > 15) return false;
  for (; *s; s++) if (*s <= ' ' || *s > '~') return false;
  return true;
}

// one line (with "\n"); returns its length, -1 when it does not fit
static int bkLine(char *out, int max, const char *ns, const char *key, uint8_t type, const void *val, int len) {
  if (!bkName(ns) || !bkName(key)) return -1;
  int n = 0;
  if (type == BK_U16) {
    n = snprintf(out, max, "%s %s u16 %u\n", ns, key, (unsigned)*(const uint16_t *)val);
  } else if (type == BK_STR) {
    const char *t = (const char *)val;
    for (const char *p = t; *p; p++) if (*p == '\n' || *p == '\r') return -1;   // one line only
    n = snprintf(out, max, "%s %s str %s\n", ns, key, t);
  } else if (type == BK_BLOB) {
    n = snprintf(out, max, "%s %s blob ", ns, key);
    if (n < 0 || n >= max) return -1;
    int k = bkB64Enc((const uint8_t *)val, len, out + n, max - n - 1);
    if (k < 0) return -1;
    n += k;
    out[n++] = '\n';
    out[n] = 0;
    return n;
  } else return -1;
  return n > 0 && n < max ? n : -1;
}

struct BkEntry {
  const char *ns, *key;
  uint8_t     type;
  uint16_t    u16;
  const char *text;                               // BK_STR
  int         len;                                // BK_BLOB: bytes in buf
};

/* Splits one line (changed in place, without the "\n"). A blob is decoded
   into buf. false = not a valid line.                                  */
static bool bkParse(char *line, BkEntry &e, uint8_t *buf, int bufMax) {
  size_t L = strlen(line);
  while (L && (line[L - 1] == '\r' || line[L - 1] == '\n')) line[--L] = 0;
  char *sp1 = strchr(line, ' ');
  if (!sp1) return false;
  *sp1 = 0;
  char *key = sp1 + 1, *sp2 = strchr(key, ' ');
  if (!sp2) return false;
  *sp2 = 0;
  char *type = sp2 + 1, *sp3 = strchr(type, ' ');
  if (!sp3) return false;
  *sp3 = 0;
  char *val = sp3 + 1;
  if (!bkName(line) || !bkName(key)) return false;
  e.ns = line; e.key = key; e.text = NULL; e.len = 0; e.u16 = 0;
  if (!strcmp(type, "u16")) {
    char *end;
    long v = strtol(val, &end, 10);
    if (end == val || *end || v < 0 || v > 65535) return false;
    e.type = BK_U16; e.u16 = (uint16_t)v;
  } else if (!strcmp(type, "str")) {
    e.type = BK_STR; e.text = val;
  } else if (!strcmp(type, "blob")) {
    int k = bkB64Dec(val, buf, bufMax);
    if (k < 0) return false;
    e.type = BK_BLOB; e.len = k;
  } else return false;
  return true;
}
