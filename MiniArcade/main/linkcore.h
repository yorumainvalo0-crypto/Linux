// Protocol for two player games between consoles nearby. No hardware in
// here: link.cpp feeds it with ESP-NOW packets, the PC tests with a
// simulated radio that loses packets on purpose.
//
//   BEACON  broadcast once a second: name, busy flag, session id
//   INVITE  "play <game> with me" - repeated until answered, gives up after 30 s
//   ANSWER  yes / no / busy
//   MOVE    one move, repeated until the ACK with the same number arrives
//   ACK     confirms a move
//   BYE     left the game or took the challenge back
//   SYNC    real-time games (Pong, Snake): the last inputs, see below
//
// A game counts as abandoned when the other console stops sending beacons
// with our session id for LK_GONE_MS.
#pragma once
#include <stdint.h>
#include <string.h>

#define LK_INVITE_MS  30000   // a challenge waits this long for an answer
#define LK_GONE_MS    15000   // opponent silent this long = gone
#define LK_PEER_MS     5000   // a console drops off the list after this
#define LK_BEACON_MS   1000
#define LK_RESEND_MS    300   // moves and answers
#define LK_INVITE_RS    600   // invites
#define LK_PEERS         12
#define LK_NAME           6   // characters of a player name
#define LK_LEAD           3   // real-time: an input counts this many ticks later
#define LK_SYNC_N        12   // real-time: inputs repeated in every packet
#define LK_SYNC_MS       20   // real-time: send this often
#define LK_RING          64   // real-time: inputs kept (power of two)

enum LkType  : uint8_t { LK_BEACON = 1, LK_INVITE, LK_ANSWER, LK_MOVE, LK_ACK, LK_BYE, LK_SYNC };
enum LkState : uint8_t { LK_LOBBY, LK_INVITING, LK_INVITED, LK_PLAYING, LK_OVER };
enum LkEnd   : uint8_t { LKE_NONE, LKE_DECLINED, LKE_BUSY, LKE_TIMEOUT, LKE_GONE, LKE_LEFT, LKE_ERROR };
enum LkGame  : uint8_t { LKG_C4 = 1, LKG_TTT = 2, LKG_PONG = 3, LKG_SNAKE = 4, LKG_PAC = 5, LKG_SHIP = 6 };
#define LK_CAPS 14            // beacon flags: 2 = Pong and Snake, 4 = Pac-Man, 8 = Battleship

struct __attribute__((packed)) LkPacket {
  char     m0, m1;                    // 'M' 'A'
  uint8_t  ver, type;
  uint16_t sid;                       // session id, chosen by the challenger
  uint8_t  seq;                       // move number
  uint8_t  arg;                       // move, game or answer
  uint8_t  flags;                     // beacon: 1 = in a game, 2 / 4 = knows more games (LK_CAPS)
                                      // invite: 1 = challenger starts
  char     name[LK_NAME];
};

/* Real-time games run in lockstep: both consoles compute the same game
   one tick at a time, and a tick is only taken when the inputs of both
   players for it are there. Every SYNC packet repeats the last inputs,
   so a lost packet is covered by the next one. Version 2, so consoles
   with an older firmware drop it before they look at it.               */
struct __attribute__((packed)) LkSync {
  char     m0, m1;                    // 'M' 'A'
  uint8_t  ver, type;                 // 2, LK_SYNC
  uint16_t sid;
  uint16_t tick;                      // newest input in here (low 16 bits)
  uint16_t ack;                       // newest input we have from the receiver
  uint8_t  n;                         // inputs for tick-n+1 .. tick
  uint8_t  in[LK_SYNC_N];
};

struct LkPeer {
  uint8_t  mac[6];
  char     name[LK_NAME + 1];
  char     code[5];                   // short id derived from the chip, e.g. "K7F2"
  bool     busy;
  uint8_t  caps;                      // games its firmware knows beyond the first two (LK_CAPS bits)
  uint16_t sid;                       // session the peer is in (from its beacon)
  int8_t   rssi;
  uint32_t seen;                      // ms of the last packet
};

typedef void (*LkSend)(void *ctx, const uint8_t *mac, const uint8_t *data, uint8_t len);  // mac NULL = everybody

static inline bool lkDue(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

class LinkCore {
public:
  LkState  state = LK_LOBBY;
  LkEnd    why = LKE_NONE;
  uint8_t  game = 0;
  bool     iStart = false;           // who makes the first move
  LkPeer   opp;                      // the other player of the current session
  uint32_t deadline = 0;             // end of the 30 s for a challenge

  /* short id from the chip's MAC: 4 characters, no 0/O or 1/I mix-ups */
  static void codeOf(const uint8_t *mac, char *out) {
    static const char A[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    uint32_t h = 2166136261u;
    for (uint8_t i = 0; i < 6; i++) { h ^= mac[i]; h *= 16777619u; }
    for (uint8_t i = 0; i < 4; i++) { out[i] = A[h & 31]; h >>= 5; }
    out[4] = 0;
  }

  void begin(const uint8_t *mac, const char *name, LkSend send, void *ctx, uint32_t now, uint32_t rnd) {
    memcpy(me, mac, 6);
    codeOf(mac, myCode);
    setName(name);
    tx = send; txCtx = ctx;
    seed = rnd | 1;
    peers = 0;
    state = LK_LOBBY; why = LKE_NONE; sid = 0;
    nextBeacon = now;
  }

  void setName(const char *n) {
    memset(myName, 0, sizeof(myName));
    strncpy(myName, n, LK_NAME);
  }
  const char *code() const { return myCode; }
  const char *name() const { return myName; }

  uint8_t peerCount() const { return peers; }
  const LkPeer &peer(uint8_t i) const { return peer_[i]; }

  // ---------------- challenges ----------------
  bool invite(uint8_t idx, uint8_t g, uint32_t now) {
    if (state != LK_LOBBY || idx >= peers || peer_[idx].busy) return false;
    opp = peer_[idx];
    game = g;
    iStart = rnd() & 1;                       // a coin decides who begins
    do sid = (uint16_t)rnd(); while (!sid);
    startSession();
    state = LK_INVITING;
    deadline = now + LK_INVITE_MS;
    nextSend = now;
    return true;
  }

  // the player's decision on an incoming challenge
  void answer(bool yes, uint32_t now) {
    if (state != LK_INVITED) return;
    sendCtl(LK_ANSWER, yes ? 1 : 0);
    if (yes) { state = LK_PLAYING; lastOpp = now; answered = sid; }
    else     { declined = sid; state = LK_LOBBY; }
  }

  // take a challenge back, or leave a running game
  void cancel() {
    if (state == LK_INVITING || state == LK_PLAYING || state == LK_INVITED) {
      for (uint8_t i = 0; i < 3; i++) sendCtl(LK_BYE, 0);
      if (state == LK_INVITED) declined = sid;
    }
    if (state == LK_PLAYING) { state = LK_OVER; why = LKE_LEFT; }
    else if (state != LK_OVER) state = LK_LOBBY;
  }

  // the game is decided; keep sending until the last move is confirmed
  void finish() { if (state == LK_PLAYING) { state = LK_OVER; why = LKE_NONE; } }
  bool delivered() const { return !pending && (!sync || acked >= myTick); }
  void toLobby() { state = LK_LOBBY; why = LKE_NONE; sid = 0; pending = false; sync = false; }
  uint16_t session() const { return sid; }       // same on both sides: seeds the games

  // ---------------- moves ----------------
  bool sendMove(uint8_t m, uint32_t now) {
    if (state != LK_PLAYING || pending) return false;
    mySeq++;
    outMove = m;
    pending = true;
    nextSend = now;
    return true;
  }
  bool moveIn(uint8_t *m) {
    if (!haveIn) return false;
    *m = inMove; haveIn = false;
    return true;
  }

  // ---------------- real-time games ----------------
  static bool realtime(uint8_t g) { return g == LKG_PONG || g == LKG_SNAKE || g == LKG_PAC; }
  static uint8_t capOf(uint8_t g) { return g == LKG_SHIP ? 8 : (g == LKG_PAC ? 4 : (realtime(g) ? 2 : 0)); }

  /* The next tick, when both inputs for it are there. After each tick the
     game hands in its input for the tick LK_LEAD ahead with syncPut().   */
  bool syncStep(uint8_t *mine, uint8_t *theirs) {
    if (!sync || state != LK_PLAYING) return false;
    if (simTick >= myTick || simTick >= theirTick) return false;
    simTick++;
    *mine = myIn[simTick & (LK_RING - 1)];
    *theirs = theirIn[simTick & (LK_RING - 1)];
    return true;
  }
  void syncPut(uint8_t in, uint32_t now) {
    if (!sync || myTick != simTick + LK_LEAD - 1) return;   // one input per tick
    myTick++;
    myIn[myTick & (LK_RING - 1)] = in;
    nextSync = now;                                        // out with the next tick()
  }
  uint32_t syncTick() const { return simTick; }

  // ---------------- radio ----------------
  void onPacket(const uint8_t *mac, const uint8_t *data, int len, int8_t rssi, uint32_t now) {
    if (len >= (int)sizeof(LkSync) && data[2] == 2) { onSync(mac, data, now); return; }
    if (len < (int)sizeof(LkPacket)) return;
    LkPacket p;
    memcpy(&p, data, sizeof(p));
    if (p.m0 != 'M' || p.m1 != 'A' || p.ver != 1 || !memcmp(mac, me, 6)) return;
    LkPeer *pe = touch(mac, p, rssi, now);
    bool fromOpp = !memcmp(mac, opp.mac, 6) && state != LK_LOBBY;

    switch (p.type) {
    case LK_BEACON:
      if (fromOpp && p.sid == sid && sid) lastOpp = now;
      break;

    case LK_INVITE:
      if (state == LK_LOBBY && pe && p.sid != declined) {
        opp = *pe;
        sid = p.sid; game = p.arg; iStart = !(p.flags & 1);
        startSession();
        state = LK_INVITED;
        deadline = now + LK_INVITE_MS;
      } else if (p.sid == answered && state == LK_PLAYING && fromOpp) {
        lastOpp = now;
        sendCtlTo(mac, LK_ANSWER, 1, p.sid);           // our "yes" got lost
      } else if (p.sid == declined) {
        sendCtlTo(mac, LK_ANSWER, 0, p.sid);           // our "no" got lost
      } else if (!(state == LK_INVITED && p.sid == sid)) {
        sendCtlTo(mac, LK_ANSWER, 2, p.sid);           // busy with somebody else
      }
      break;

    case LK_ANSWER:
      if (state == LK_INVITING && fromOpp && p.sid == sid) {
        if (p.arg == 1) { state = LK_PLAYING; lastOpp = now; }
        else            { state = LK_OVER; why = (p.arg == 2) ? LKE_BUSY : LKE_DECLINED; }
      }
      break;

    case LK_MOVE:
      if (!fromOpp || p.sid != sid) break;
      if (state == LK_INVITING) { state = LK_PLAYING; }  // the "yes" was lost, the move says it
      if (state != LK_PLAYING && state != LK_OVER) break;
      lastOpp = now;
      if (p.seq == (uint8_t)(theirSeq + 1) && !haveIn && state == LK_PLAYING) {
        theirSeq++;
        inMove = p.arg; haveIn = true;
        pending = false;          // taking turns: their move means ours arrived
      }
      if ((int8_t)(p.seq - theirSeq) <= 0) sendCtlTo(mac, LK_ACK, 0, sid, p.seq);   // also repeats
      break;

    case LK_ACK:
      if (fromOpp && p.sid == sid && pending && p.seq == mySeq) pending = false;
      if (fromOpp && p.sid == sid) lastOpp = now;
      break;

    case LK_BYE:
      if (!fromOpp || p.sid != sid) break;
      if (state == LK_PLAYING || state == LK_INVITING) { state = LK_OVER; why = LKE_LEFT; }
      else if (state == LK_INVITED) { state = LK_OVER; why = LKE_LEFT; }
      break;
    }
  }

  void tick(uint32_t now) {
    for (uint8_t i = 0; i < peers; ) {                 // forget consoles that went away
      if (lkDue(now, peer_[i].seen + LK_PEER_MS)) { peer_[i] = peer_[--peers]; continue; }
      i++;
    }
    if (lkDue(now, nextBeacon)) {
      nextBeacon = now + LK_BEACON_MS;
      LkPacket p = pkt(LK_BEACON);
      bool inGame = state == LK_PLAYING || (state == LK_OVER && !delivered());
      p.flags = ((inGame || state == LK_INVITING || state == LK_INVITED) ? 1 : 0) | LK_CAPS;
      p.sid = inGame ? sid : 0;
      tx(txCtx, NULL, (const uint8_t *)&p, sizeof(p));
    }
    switch (state) {
    case LK_INVITING:
      if (lkDue(now, deadline)) { sendCtl(LK_BYE, 0); state = LK_OVER; why = LKE_TIMEOUT; break; }
      if (lkDue(now, nextSend)) {
        nextSend = now + LK_INVITE_RS;
        LkPacket p = pkt(LK_INVITE);
        p.arg = game; p.flags = iStart ? 1 : 0;
        tx(txCtx, opp.mac, (const uint8_t *)&p, sizeof(p));
      }
      break;
    case LK_INVITED:
      if (lkDue(now, deadline)) { declined = sid; sendCtl(LK_ANSWER, 0); state = LK_OVER; why = LKE_TIMEOUT; }
      break;
    case LK_PLAYING:
      if (lkDue(now, lastOpp + LK_GONE_MS)) { state = LK_OVER; why = LKE_GONE; break; }
      /* fall through */
    case LK_OVER:
      if (pending && lkDue(now, nextSend)) {
        nextSend = now + LK_RESEND_MS;
        LkPacket p = pkt(LK_MOVE);
        p.seq = mySeq; p.arg = outMove;
        tx(txCtx, opp.mac, (const uint8_t *)&p, sizeof(p));
      }
      break;
    default:
      break;
    }
    if (sync && (state == LK_PLAYING || state == LK_OVER) && lkDue(now, nextSync)) {
      nextSync = now + LK_SYNC_MS;                   // the other side needs our acks too
      LkSync p;
      memset(&p, 0, sizeof(p));
      p.m0 = 'M'; p.m1 = 'A'; p.ver = 2; p.type = LK_SYNC; p.sid = sid;
      p.tick = (uint16_t)myTick;
      p.ack = (uint16_t)theirTick;
      p.n = myTick < LK_SYNC_N ? (uint8_t)myTick : LK_SYNC_N;
      for (uint8_t i = 0; i < p.n; i++) p.in[i] = myIn[(myTick - p.n + 1 + i) & (LK_RING - 1)];
      tx(txCtx, opp.mac, (const uint8_t *)&p, sizeof(p));
    }
  }

private:
  uint8_t  me[6];
  char     myCode[5];
  char     myName[LK_NAME + 1];
  LkSend   tx = 0;
  void    *txCtx = 0;
  uint32_t seed = 1;
  LkPeer   peer_[LK_PEERS];
  uint8_t  peers = 0;
  uint16_t sid = 0, answered = 0, declined = 0;
  uint8_t  mySeq = 0, theirSeq = 0, outMove = 0, inMove = 0;
  bool     pending = false, haveIn = false;
  uint32_t nextBeacon = 0, nextSend = 0, lastOpp = 0;
  bool     sync = false;                                  // a real-time game
  uint32_t myTick = 0, theirTick = 0, simTick = 0, acked = 0, nextSync = 0;
  uint8_t  myIn[LK_RING], theirIn[LK_RING];

  static uint32_t unwrap(uint32_t near, uint16_t v) { return near + (int16_t)(v - (uint16_t)near); }

  void onSync(const uint8_t *mac, const uint8_t *data, uint32_t now) {
    LkSync p;
    memcpy(&p, data, sizeof(p));
    if (p.m0 != 'M' || p.m1 != 'A' || p.type != LK_SYNC || !sync) return;
    if (memcmp(mac, opp.mac, 6) || p.sid != sid || state == LK_LOBBY) return;
    if (state == LK_INVITING) state = LK_PLAYING;           // the "yes" was lost, this says it
    if (state != LK_PLAYING && state != LK_OVER) return;
    lastOpp = now;
    uint32_t a = unwrap(acked, p.ack);
    if (a > acked && a <= myTick) acked = a;
    if (p.n > LK_SYNC_N) return;
    uint32_t last = unwrap(theirTick, p.tick);
    for (uint8_t i = 0; i < p.n; i++) {
      uint32_t t = last - p.n + 1 + i;
      if (t == theirTick + 1) { theirIn[t & (LK_RING - 1)] = p.in[i]; theirTick = t; }
    }
  }

  uint32_t rnd() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

  void startSession() {
    mySeq = theirSeq = 0;
    pending = haveIn = false;
    why = LKE_NONE;
    sync = realtime(game);                   // the first LK_LEAD ticks have no input
    myTick = theirTick = acked = LK_LEAD;
    simTick = 0;
    memset(myIn, 0, sizeof(myIn));
    memset(theirIn, 0, sizeof(theirIn));
  }

  LkPacket pkt(uint8_t type) const {
    LkPacket p;
    memset(&p, 0, sizeof(p));
    p.m0 = 'M'; p.m1 = 'A'; p.ver = 1; p.type = type; p.sid = sid;
    memcpy(p.name, myName, LK_NAME);
    return p;
  }
  void sendCtlTo(const uint8_t *mac, uint8_t type, uint8_t arg, uint16_t s, uint8_t seq = 0) {
    LkPacket p = pkt(type);
    p.arg = arg; p.sid = s; p.seq = seq;
    tx(txCtx, mac, (const uint8_t *)&p, sizeof(p));
  }
  void sendCtl(uint8_t type, uint8_t arg) { sendCtlTo(opp.mac, type, arg, sid); }

  // updates the list of consoles around; NULL when the list is full
  LkPeer *touch(const uint8_t *mac, const LkPacket &p, int8_t rssi, uint32_t now) {
    LkPeer *pe = NULL;
    for (uint8_t i = 0; i < peers; i++) if (!memcmp(peer_[i].mac, mac, 6)) pe = &peer_[i];
    if (!pe) {
      if (peers >= LK_PEERS) return NULL;
      pe = &peer_[peers++];
      memset(pe, 0, sizeof(*pe));
      memcpy(pe->mac, mac, 6);
      codeOf(mac, pe->code);
    }
    memcpy(pe->name, p.name, LK_NAME);
    pe->name[LK_NAME] = 0;
    for (uint8_t i = 0; i < LK_NAME; i++)            // only plain characters on screen
      if (pe->name[i] && (pe->name[i] < ' ' || pe->name[i] > '~')) pe->name[i] = '?';
    if (p.type == LK_BEACON) { pe->busy = p.flags & 1; pe->caps = p.flags & LK_CAPS; pe->sid = p.sid; }
    pe->rssi = rssi;
    pe->seen = now;
    return pe;
  }
};
