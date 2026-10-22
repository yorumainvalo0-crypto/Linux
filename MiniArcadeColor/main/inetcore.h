// MiniArcade Color: internet play through the own MiniArcade server - the
// transport that carries linkcore.h's packets over the server's relay
// (MiniArcadeServer/tvserver/net.py). No hardware and no HTTP in here:
// inet.h sends the requests next() asks for over the TV's connection
// (tvnet.h) and hands the answers to onAnswer(); the PC tests do the same
// with a pretend server and a second console.
/* One request at a time. The poll goes again and again: it reports us
   (name, the game we want, our round trip), carries our packets and
   brings what is new - a challenge, the answer to ours, the session, the
   other console's packets. The server holds a poll while nothing is new
   (at most the time we allow), so packets for us arrive at once; ours
   wait for the next poll.
   In a session our packets are numbered and repeated until the server
   confirms them ("ack"); "got" tells it up to which number we have the
   other side's. The relay loses nothing, so linkcore's repeated ACK and
   BYE are left out, and of the real-time SYNC packets only the newest
   unsent one is kept - as long as it still covers every input not sent.
   The matching (lobby, friends, challenges) is the server's; when a
   session starts, LinkCore::startNet() begins the game at once.      */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "linkcore.h"
#include "jsonlite.h"

#define IN_CODE        6     // friend codes: 6 characters of IN_ALPHA
#define IN_LIST       16     // consoles in the lobby list, friends
#define IN_OUT        24     // our packets waiting for the server's ack
#define IN_PER_POLL   16     // packets in one poll (the server's limit)
#define IN_PKT        24     // the longest packet (LkSync)
#define IN_WAIT_LOBBY 1000   // ms a poll may wait at the server: in the lobby
#define IN_WAIT_TURN   300   // ... in a game taking turns (our next move waits at most this long)
#define IN_WAIT_RT     100   // ... in a real-time game
#define IN_RETRY_MS   1000   // after a failed request
#define IN_LOST_MS   10000   // no answer for this long in a game: the connection is lost
#define IN_LEFT_MS    2500   // the other one left: its last moves get this long to be played
#define IN_FLUSH_MS   3000   // after a game: at most this long to get our last packets out
#define IN_REPEAT_MS  5000   // a packet the same as one sent this recently is a repeat

static const char IN_ALPHA[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";   // what friend codes are made of

struct InPeer {
  char    code[IN_CODE + 1];
  char    name[LK_NAME + 1];
  uint8_t game;                      // the game it would like to play (0 = any)
  bool    busy, friendly, online;
};

enum InReq : uint8_t { INR_NONE, INR_POLL, INR_INVITE, INR_CANCEL, INR_ANSWER, INR_LEAVE, INR_FRIEND };
// our challenge: being sent, waiting for the answer, and how it ended
enum InOut : uint8_t { INO_NONE, INO_SENDING, INO_WAIT, INO_OK, INO_NO, INO_BUSY, INO_GONE, INO_TIMEOUT,
                       INO_OFFLINE, INO_UNKNOWN, INO_ERROR };
// adding or removing a friend
enum InFr : uint8_t { INF_NONE, INF_SENDING, INF_ADDED, INF_REMOVED, INF_UNKNOWN, INF_SELF, INF_FULL, INF_ERROR };

class NetLink {
public:
  LinkCore *L = NULL;
  char     me[IN_CODE + 1];          // own friend code ("" until the server said it)
  uint8_t  want = 0;                 // the game we would like to play, shown to the others
  bool     lists = false;            // the lobby is on the screen: the polls ask for the lists
  InPeer   lob[IN_LIST];             // online now (friends first)
  uint8_t  nLob = 0;
  InPeer   fr[IN_LIST];              // friends, online or not
  uint8_t  nFr = 0;
  bool     haveLists = false;

  bool     inv = false;              // a challenge for us
  char     invId[12];
  InPeer   invFrom;
  uint8_t  invGame = 0;
  uint32_t invUntil = 0;
  bool     invGone = false;          // we said yes, but it was taken back meanwhile

  InOut    out = INO_NONE;           // our challenge
  InPeer   outTo;
  uint8_t  outGame = 0;
  uint32_t outUntil = 0;

  InFr     frRes = INF_NONE;         // the last friend request
  char     frName[LK_NAME + 1];

  int      status = -1;              // HTTP status of the last poll; 0 = no connection, -1 = none yet
  bool     unpaired = false;         // the server does not know our token (401)
  bool     everOk = false;
  uint32_t lastOk = 0;
  uint16_t rtt = 0;                  // ms, smoothed, without the time the server held the poll
  uint16_t sid = 0;                  // the session we play in (0 = none)
  char     oppCode[IN_CODE + 1];
  uint32_t polls = 0, sentPkts = 0, gotPkts = 0, skipped = 0;   // for the tests and the screen

  static void macOf(const char *code, uint8_t *mac) {   // a made-up MAC for linkcore, from the code
    uint32_t h = 2166136261u;
    for (const char *c = code; *c; c++) { h ^= (uint8_t)*c; h *= 16777619u; }
    mac[0] = 0x02; mac[1] = 'N';                          // locally administered: no real chip has it
    for (uint8_t i = 2; i < 6; i++) { mac[i] = (uint8_t)h; h >>= 8; }
  }

  void begin(LinkCore *core, const uint8_t *mac, const char *name, uint32_t now, uint32_t rnd) {
    L = core;
    clock = now;
    L->begin(mac, name, send, this, now, rnd);
    L->caps = LK_CAPS | LK_CAP_CHESS;
    L->caps2 = LK_CAP2_KART;
    me[0] = 0;
    nLob = nFr = 0; haveLists = false;
    inv = invGone = false; out = INO_NONE; frRes = INF_NONE;
    status = -1; unpaired = false; everOk = false; rtt = 0;
    act = INR_NONE; inFlight = INR_NONE;
    nextPoll = now;
    endSession();
    polls = sentPkts = gotPkts = skipped = 0;
  }

  // ---------------- what the screen asks for ----------------
  bool busy() const { return act != INR_NONE; }          // an action waits for its answer

  bool invite(const InPeer &p, uint8_t game, uint32_t now) {
    if (act != INR_NONE || sid || out == INO_SENDING || out == INO_WAIT) return false;
    outTo = p; outGame = game;
    out = INO_SENDING;
    outUntil = now + LK_INVITE_MS;
    act = INR_INVITE;
    return true;
  }
  void takeBack() {                                       // our challenge: no longer
    if (out == INO_WAIT && act == INR_NONE) act = INR_CANCEL;
    else if (out == INO_SENDING && act == INR_INVITE) act = INR_NONE;
    out = INO_NONE;
  }
  void answer(bool yes) {
    if (!inv || act != INR_NONE) return;
    ansYes = yes;
    act = INR_ANSWER;
    invGone = false;
  }
  void friendSet(const char *code, bool on) {
    if (act != INR_NONE) return;
    snprintf(frCode, sizeof(frCode), "%s", code);
    frOn = on;
    frRes = INF_SENDING;
    act = INR_FRIEND;
  }
  void leave(uint32_t now) {                              // done with the server: the game ends now
    if (sid && L->state == LK_PLAYING) L->cancel();
    if (out == INO_WAIT) takeBack();
    if (sid && !leaving) { leaving = true; flushUntil = now + 1000; }
  }
  bool idle() const { return !sid && act == INR_NONE && inFlight == INR_NONE; }

  int16_t find(const InPeer *list, uint8_t n, const char *code) const {
    for (uint8_t i = 0; i < n; i++) if (!strcmp(list[i].code, code)) return i;
    return -1;
  }

  // ---------------- the requests ----------------
  /* The next request, if one is due: its path and body ("" = none).
     Actions come first, then the poll.                                  */
  bool next(char *path, size_t pmax, char *body, size_t bmax, uint32_t now) {
    clock = now;
    body[0] = 0;
    if (inFlight != INR_NONE) return false;
    if (unpaired) return false;
    if (leaving && sid && (!nOut || lkDue(now, flushUntil))) {
      snprintf(path, pmax, "/api/net/leave?sid=%u", sid);
      return start(INR_LEAVE, now);
    }
    if (act != INR_NONE && lkDue(now, actAt)) {
      switch (act) {
      case INR_INVITE: snprintf(path, pmax, "/api/net/invite?to=%s&g=%u", outTo.code, outGame); break;
      case INR_CANCEL: snprintf(path, pmax, "/api/net/cancel?oid=%s", outId); break;
      case INR_ANSWER: snprintf(path, pmax, "/api/net/answer?iid=%s&yes=%u", invId, ansYes ? 1 : 0); break;
      case INR_FRIEND: snprintf(path, pmax, "/api/net/friend?c=%s&on=%u", frCode, frOn ? 1 : 0); break;
      default: break;
      }
      return start(act, now);
    }
    if (!lkDue(now, nextPoll)) return false;
    snprintf(path, pmax, "/api/net/poll");
    pollBody(body, bmax);
    return start(INR_POLL, now);
  }

  // the answer to the request next() gave: HTTP status (0 = no connection) and body
  void onAnswer(int st, const char *b, size_t n, uint32_t now) {
    clock = now;
    InReq r = inFlight;
    inFlight = INR_NONE;
    if (st == 401) { unpaired = true; status = 401; return; }
    const char *e = b + n;
    switch (r) {
    case INR_POLL:
      status = st;
      if (st != 200) { nextPoll = now + IN_RETRY_MS; return; }
      polled(b, e, now);
      break;
    case INR_INVITE:
      act = INR_NONE;
      if (out != INO_SENDING) {                          // taken back meanwhile
        if (st == 200 && jsStr(b, e, "oid", outId, sizeof(outId))) act = INR_CANCEL;
        break;
      }
      if (st == 200 && jsStr(b, e, "oid", outId, sizeof(outId))) { out = INO_WAIT; outUntil = now + LK_INVITE_MS; }
      else if (st == 404) out = INO_UNKNOWN;
      else if (st == 409) {
        char why[12] = "";
        jsStr(b, e, "why", why, sizeof(why));
        out = !strcmp(why, "offline") ? INO_OFFLINE : (!strcmp(why, "self") ? INO_UNKNOWN : INO_BUSY);
      } else if (st == 429) { act = INR_INVITE; actAt = now + IN_RETRY_MS; }   // a moment later
      else out = INO_ERROR;
      break;
    case INR_CANCEL:
      act = INR_NONE;
      break;
    case INR_ANSWER:
      act = INR_NONE;
      if (st == 0 || st == 429) { act = INR_ANSWER; actAt = now + IN_RETRY_MS; break; }   // once more
      inv = false;
      if (st == 200 && ansYes) {
        const char *s = section(b, e, "ses", &e);
        if (s) session(s, e, now);
      } else if (st != 200 && ansYes) invGone = true;
      break;
    case INR_LEAVE:
      leftSid = sid;                                     // the server may still tell of it for a moment
      endSession();
      break;
    case INR_FRIEND:
      act = INR_NONE;
      if (st == 200) {
        frRes = frOn ? INF_ADDED : INF_REMOVED;
        jsStr(b, e, "n", frName, sizeof(frName));
        nextPoll = now;                                  // the lists anew
      } else if (st == 404) frRes = INF_UNKNOWN;
      else if (st == 409) {
        char why[12] = "";
        jsStr(b, e, "why", why, sizeof(why));
        frRes = !strcmp(why, "self") ? INF_SELF : INF_FULL;
      } else frRes = INF_ERROR;
      break;
    default:
      break;
    }
  }

  // the clock: linkcore's resends and timeouts, the end of a game, a lost connection
  void tick(uint32_t now) {
    clock = now;
    L->tick(now);
    if (sid && L->state == LK_PLAYING) {
      if (leftAt && lkDue(now, leftAt + IN_LEFT_MS)) L->lost(LKE_LEFT);
      else if (everOk && lkDue(now, lastOk + IN_LOST_MS)) L->lost(LKE_ERROR);
    }
    if (sid && !leaving && L->state == LK_LOBBY) {        // the game is over and done with
      leaving = true;
      flushUntil = now + IN_FLUSH_MS;
    }
    if (out == INO_WAIT && lkDue(now, outUntil + 5000)) out = INO_TIMEOUT;   // the server never said
    if (inv && lkDue(now, invUntil)) inv = false;
  }

  uint32_t lastPollMs() const { return lastDur; }

private:
  uint32_t clock = 0, nextPoll = 0, sentAt = 0, lastDur = 0;
  InReq    act = INR_NONE, inFlight = INR_NONE;
  uint32_t actAt = 0;
  bool     ansYes = false;
  char     outId[12];
  char     frCode[IN_CODE + 1];
  bool     frOn = true;
  // the session
  uint8_t  oppMac[6];
  uint16_t reqSid = 0;                 // the session the poll in flight was about
  uint32_t got = 0;                    // the other side's packets we have, up to this number
  uint32_t outSeq = 1, sentUpTo = 0;   // the next number of ours; the highest one sent so far
  uint16_t syncSent = 0;               // the newest real-time input sent so far (tick)
  uint32_t leftAt = 0;                 // the other one left at this time
  uint16_t leftSid = 0;                // the session we left last
  bool     leaving = false;
  uint32_t flushUntil = 0;
  struct Pkt { uint32_t seq; uint8_t len; uint8_t d[IN_PKT]; };
  Pkt      outQ[IN_OUT];
  uint8_t  nOut = 0;
  struct Rec { uint32_t h, t; };
  Rec      recent[8];                  // the packets sent lately (linkcore's repeats are left out)
  uint8_t  nRec = 0;

  bool start(InReq r, uint32_t now) { inFlight = r; sentAt = now; return true; }

  void endSession() {
    sid = 0; reqSid = 0; got = 0; outSeq = 1; sentUpTo = 0; syncSent = 0;
    nOut = 0; nRec = 0; leftAt = 0; leaving = false;
    oppCode[0] = 0;
  }

  static uint32_t hashOf(const uint8_t *d, uint8_t n) {
    uint32_t h = 2166136261u;
    for (uint8_t i = 0; i < n; i++) { h ^= d[i]; h *= 16777619u; }
    return h ^ n;
  }

  // linkcore sends: in a session it goes into the queue for the next poll
  static void send(void *ctx, const uint8_t *, const uint8_t *d, uint8_t n) { ((NetLink *)ctx)->queue(d, n); }
  void queue(const uint8_t *d, uint8_t n) {
    if (!sid || n > IN_PKT || (L->state != LK_PLAYING && L->state != LK_OVER)) return;
    bool sync = n >= sizeof(LkSync) && d[2] == 2;
    bool kart = n >= LK_KART_LEN && d[2] == 3;
    if (kart && nOut && outQ[nOut - 1].seq > sentUpTo && outQ[nOut - 1].len >= LK_KART_LEN && outQ[nOut - 1].d[2] == 3) {
      memcpy(outQ[nOut - 1].d, d, n);                    // Kart GP: only the newest state waits for the poll
      skipped++;
      return;
    }
    /* ACK and BYE again: the relay loses nothing, so a repeat is left out.
       A MOVE is repeated until its ACK comes back: the other console only
       takes it once its game has the one before (linkcore's haveIn).    */
    if (!sync && !kart && n >= sizeof(LkPacket) && (d[3] == LK_ACK || d[3] == LK_BYE)) {
      uint32_t h = hashOf(d, n);
      for (uint8_t i = 0; i < nRec; i++)
        if (recent[i].h == h && !lkDue(clock, recent[i].t + IN_REPEAT_MS)) { skipped++; return; }
      if (nRec == 8) { memmove(recent, recent + 1, sizeof(recent[0]) * 7); nRec--; }
      recent[nRec++] = { h, clock };
    }
    if (sync && nOut && outQ[nOut - 1].seq > sentUpTo && outQ[nOut - 1].len >= sizeof(LkSync)) {
      LkSync p;
      memcpy(&p, d, sizeof(p));
      if ((int16_t)(p.tick - p.n + 1 - (uint16_t)(syncSent + 1)) <= 0) {   // covers all not sent: replaces it
        memcpy(outQ[nOut - 1].d, d, n);
        outQ[nOut - 1].len = n;
        skipped++;
        return;
      }
    }
    if (nOut == IN_OUT) { skipped++; return; }           // the server takes nothing: linkcore repeats
    Pkt &k = outQ[nOut++];
    k.seq = outSeq++;
    k.len = n;
    memcpy(k.d, d, n);
  }

  void pollBody(char *b, size_t max) {
    char nm[LK_NAME + 1];
    const char *src = L->name();
    uint8_t k = 0;
    for (; src[k] && k < LK_NAME; k++) nm[k] = (src[k] >= ' ' && src[k] <= '~' && src[k] != '"' && src[k] != '\\') ? src[k] : '?';
    nm[k] = 0;
    uint16_t wait = leaving ? 0 : (sid ? (LinkCore::live(L->game) ? IN_WAIT_RT : IN_WAIT_TURN) : IN_WAIT_LOBBY);
    if (!sid && (out == INO_WAIT || inv)) wait = IN_WAIT_TURN;       // a challenge: quicker
    int w = snprintf(b, max, "{\"n\":\"%s\",\"g\":%u,\"l\":%u,\"rtt\":%u,\"sid\":%u,\"got\":%lu,\"wait\":%u,\"m\":[",
                     nm, want, lists ? 1 : 0, rtt ? rtt : 100, sid, (unsigned long)got, wait);
    reqSid = sid;
    for (uint8_t i = 0; i < nOut && i < IN_PER_POLL && w + 80 < (int)max; i++) {
      const Pkt &k2 = outQ[i];
      w += snprintf(b + w, max - w, "%s[%lu,\"", i ? "," : "", (unsigned long)k2.seq);
      for (uint8_t j = 0; j < k2.len; j++) w += snprintf(b + w, max - w, "%02x", k2.d[j]);
      w += snprintf(b + w, max - w, "\"]");
      if (k2.seq > sentUpTo) sentUpTo = k2.seq;
      if (k2.len >= sizeof(LkSync) && k2.d[2] == 2) {
        LkSync p;
        memcpy(&p, k2.d, sizeof(p));
        if ((int16_t)(p.tick - syncSent) > 0) syncSent = p.tick;
      }
      sentPkts++;
    }
    snprintf(b + w, max - w, "]}");
  }

  // the part "key":{...} of an answer; NULL = not there
  static const char *section(const char *b, const char *e, const char *key, const char **end) {
    const char *q = jsFind(b, e, key);
    if (!q || q >= e || *q != '{') return NULL;
    return jsObj(q, e, end);
  }
  static const char *listOf(const char *b, const char *e, const char *key, const char **end) {
    const char *q = jsFind(b, e, key);
    if (!q || q >= e || *q != '[') return NULL;
    int depth = 0;
    for (const char *p = q; p < e; p++) {
      if (*p == '[') depth++;
      else if (*p == ']' && --depth == 0) { *end = p + 1; return q; }
    }
    return NULL;
  }

  static void peerOf(const char *s, const char *e, const char *kc, const char *kn, InPeer &p) {
    memset(&p, 0, sizeof(p));
    jsStr(s, e, kc, p.code, sizeof(p.code));
    jsStr(s, e, kn, p.name, sizeof(p.name));
    for (char *c = p.name; *c; c++) if (*c < ' ' || *c > '~') *c = '?';
  }

  static uint8_t readList(const char *b, const char *e, const char *key, InPeer *list) {
    const char *end, *s = listOf(b, e, key, &end);
    uint8_t n = 0;
    if (!s) return 0;
    for (const char *oe; n < IN_LIST && (s = jsObj(s, end, &oe)); s = oe) {
      InPeer &p = list[n++];
      peerOf(s, oe, "c", "n", p);
      p.game = (uint8_t)jsInt(s, oe, "g", 0);
      p.busy = jsInt(s, oe, "b", 0) != 0;
      p.friendly = jsInt(s, oe, "f", 1) != 0;
      p.online = jsInt(s, oe, "o", 1) != 0;
    }
    return n;
  }

  // a session from an answer: a new game begins, or ours is over
  void session(const char *s, const char *e, uint32_t now) {
    uint16_t id = (uint16_t)jsInt(s, e, "sid", 0);
    char se[8] = "";
    jsStr(s, e, "se", se, sizeof(se));
    if (!id) return;
    if (!sid) {
      if (se[0] || id == leftSid) {                       // over before we came, or left already: goodbye
        endSession();
        sid = id; leaving = true; flushUntil = now;
        return;
      }
      endSession();
      LkPeer o;
      memset(&o, 0, sizeof(o));
      jsStr(s, e, "oc", oppCode, sizeof(oppCode));
      jsStr(s, e, "on", o.name, sizeof(o.name));
      for (char *c = o.name; *c; c++) if (*c < ' ' || *c > '~') *c = '?';
      memcpy(o.code, oppCode, 4);                         // linkcore keeps 4 characters
      macOf(oppCode, oppMac);
      memcpy(o.mac, oppMac, 6);
      o.caps = LK_CAPS | LK_CAP_CHESS;
      o.seen = now;
      sid = id;
      L->startNet(o, id, (uint8_t)jsInt(s, e, "g", 0), jsInt(s, e, "first", 0) != 0, (uint8_t)jsInt(s, e, "lead", LK_LEAD), now);
      syncSent = L->lead;                                 // the inputs up to the delay are 0 on both sides
      out = INO_NONE;
      inv = false;
      return;
    }
    if (id != sid || !se[0]) return;
    if (!strcmp(se, "gone")) L->lost(LKE_GONE);
    else if (!strcmp(se, "left")) { if (!leftAt) leftAt = now; }
    else L->lost(LKE_ERROR);                              // "lost": we were away too long
  }

  void polled(const char *b, const char *e, uint32_t now) {
    polls++;
    uint32_t dur = now - sentAt, held = (uint32_t)jsInt(b, e, "h", 0);
    lastDur = dur;
    uint32_t r = dur > held ? dur - held : 0;
    rtt = (uint16_t)(rtt ? (rtt * 3 + r) / 4 : (r ? r : 1));
    lastOk = now;
    everOk = true;
    unpaired = false;
    nextPoll = now;                                       // at once: the server held it if nothing was new
    char c[IN_CODE + 1];
    if (jsStr(b, e, "me", c, sizeof(c))) strcpy(me, c);
    // the lists first (their keys are short: only looked for inside them)
    const char *le;
    const char *lst = listOf(b, e, "lob", &le);
    if (lst) {
      nLob = readList(b, e, "lob", lob);
      nFr = readList(b, e, "fr", fr);
      haveLists = true;
      e = lst;                                            // the rest is before the lists
    }
    // our packets that arrived
    if (sid && reqSid == sid) {
      long ack = jsInt(b, e, "ack", -1);
      uint8_t k = 0;
      while (k < nOut && ack >= 0 && outQ[k].seq <= (uint32_t)ack) k++;
      if (k) { memmove(outQ, outQ + k, sizeof(outQ[0]) * (nOut - k)); nOut -= k; }
    }
    // a challenge for us, ours
    const char *se2;
    const char *s = section(b, e, "inv", &se2);
    if (s && !sid) {
      char id[12] = "";
      jsStr(s, se2, "iid", id, sizeof(id));
      if (!inv || strcmp(id, invId)) {
        strcpy(invId, id);
        peerOf(s, se2, "ic", "in", invFrom);
        invGame = (uint8_t)jsInt(s, se2, "ig", 0);
        invUntil = now + (uint32_t)jsInt(s, se2, "is", 30) * 1000;
        inv = true;
      }
    } else if (act != INR_ANSWER) inv = false;
    s = section(b, e, "out", &se2);
    if (s && (out == INO_WAIT)) {
      char id[12] = "", st[10] = "";
      jsStr(s, se2, "oid", id, sizeof(id));
      jsStr(s, se2, "st", st, sizeof(st));
      if (!strcmp(id, outId)) {
        if (!strcmp(st, "no")) out = INO_NO;
        else if (!strcmp(st, "busy")) out = INO_BUSY;
        else if (!strcmp(st, "gone")) out = INO_GONE;
        else if (!strcmp(st, "timeout")) out = INO_TIMEOUT;
      }
    }
    // the session, and the other side's packets
    s = section(b, e, "ses", &se2);
    bool wasIn = sid && reqSid == sid;
    if (s) {
      if (wasIn) packetsIn(b, e, now);
      session(s, se2, now);
    } else if (wasIn && !leaving) L->lost(LKE_ERROR);     // the server knows no game of ours any more
  }

  // "msg":"12.4d41...,13.4d41..." - in order, each once
  void packetsIn(const char *b, const char *e, uint32_t now) {
    const char *q = jsFind(b, e, "msg");
    if (!q || *q != '"') return;
    q++;
    while (q < e && *q != '"') {
      uint32_t seq = (uint32_t)strtoul(q, (char **)&q, 10);
      if (q >= e || *q != '.') return;
      q++;
      uint8_t d[IN_PKT];
      uint8_t n = 0;
      while (q + 1 < e && isxdigit((unsigned char)q[0]) && isxdigit((unsigned char)q[1])) {
        char h[3] = { q[0], q[1], 0 };
        if (n < sizeof(d)) d[n++] = (uint8_t)strtoul(h, NULL, 16);
        q += 2;
      }
      if (q < e && *q == ',') q++;
      if (seq != got + 1) continue;                       // seen already
      got = seq;
      gotPkts++;
      if (L->state == LK_PLAYING || L->state == LK_OVER) L->onPacket(oppMac, d, n, -50, now);
    }
  }
};
