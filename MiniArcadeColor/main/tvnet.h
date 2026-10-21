// MiniArcade Color: talking to the MiniArcade server for the TV (tv.h).
// One background task does all of it over one kept-alive HTTPS connection,
// so only one TLS session needs memory: short requests (lists, thumbnails,
// pairing) one at a time, and while a video plays the stream - segments of
// half a second asked for again and again, each request carrying the
// report (frames shown and dropped) and a key action if there is one.
// tvnet.cpp on the board, a pretend server in the host tests.
#pragma once
#include <stdint.h>
#include <stddef.h>

#include "sdkconfig.h"

#define TV_RESP_MAX  4096          // the longest answer to a short request
#if CONFIG_IDF_TARGET_ESP32S3
#define TV_RING      16384         // stream bytes waiting for the decoder (a power of 2)
#else
#define TV_RING       8192         // the C3: TLS (16 kB records) and TCP buffer the rest
#endif

enum TvReqState : uint8_t { TVR_IDLE, TVR_BUSY, TVR_DONE, TVR_ERROR };

bool        tvNetStart(const char *base, const char *token);   // base: https://host[:port][/path]
void        tvNetStop();
void        tvNetToken(const char *token);                     // after pairing

bool        tvReq(const char *path, bool post = false);        // false = one is still running
TvReqState  tvReqState();
int         tvReqStatus();                                     // HTTP status, 0 = no connection
const uint8_t *tvReqBody(size_t *len);                         // the answer, 0-terminated
void        tvReqClear();                                      // back to TVR_IDLE

// the stream; the decoder reads records with tvStreamPeek / tvStreamRead
bool        tvStreamStart();                                   // false = no memory
void        tvStreamStop();
void        tvStreamAct(const char *act);                      // e.g. "a=seek&d=10", sent with the next segment
void        tvStreamCount(uint16_t shown, uint16_t late, uint16_t bufMs);   // for the next report
size_t      tvStreamAvail();
size_t      tvStreamPeek(uint8_t *dst, size_t n);
size_t      tvStreamRead(uint8_t *dst, size_t n);
int         tvStreamStatus();                                  // last HTTP status (401 = not paired any more)
uint16_t    tvStreamBroken();                                  // counts lost connections
void        tvStreamReset();                                   // after a loss: empty the ring, go on
uint32_t    tvStreamBytes();                                   // received so far

const char *tvConsoleCode();                                   // 4 characters, as in the lobby
uint32_t    tvFreeHeap(uint32_t *lowest);                      // bytes free now (and the lowest seen)
