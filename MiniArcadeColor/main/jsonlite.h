// MiniArcade Color: reading the MiniArcade server's JSON answers (TV and
// internet play). They are small and nearly flat: a key is looked up in a
// range of the text, a list is walked object by object.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// the value of "key": in p..e (the first one); NULL = not there
static const char *jsFind(const char *p, const char *e, const char *key) {
  char pat[16];
  int pl = snprintf(pat, sizeof(pat), "\"%s\":", key);
  for (const char *q = p; q + pl <= e; q++)
    if (!memcmp(q, pat, pl)) { q += pl; while (q < e && *q == ' ') q++; return q; }
  return NULL;
}
static bool jsStr(const char *p, const char *e, const char *key, char *out, size_t max) {
  const char *q = jsFind(p, e, key);
  if (!q || q >= e || *q != '"') return false;
  size_t k = 0;
  for (q++; q < e && *q != '"'; ) {
    char c = *q++;
    if (c == '\\' && q < e) { c = *q++; if (c == 'u') { q += 4; c = '?'; } else if (c == 'n' || c == 't') c = ' '; }
    if (k + 1 < max) out[k++] = c;
  }
  out[k] = 0;
  return true;
}
static long jsInt(const char *p, const char *e, const char *key, long def) {
  const char *q = jsFind(p, e, key);
  return q && q < e ? strtol(q, NULL, 10) : def;
}
// the next {...} from p on (strings skipped); NULL = none
static const char *jsObj(const char *p, const char *e, const char **end) {
  while (p < e && *p != '{') p++;
  int depth = 0;
  bool str = false;
  for (const char *q = p; q < e; q++) {
    if (str) { if (*q == '\\') q++; else if (*q == '"') str = false; continue; }
    if (*q == '"') str = true;
    else if (*q == '{') depth++;
    else if (*q == '}' && --depth == 0) { *end = q + 1; return p; }
  }
  return NULL;
}
