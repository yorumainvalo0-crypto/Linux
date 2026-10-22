// MiniArcade Color: Kart GP for two consoles - what goes over the radio
// or the internet, and the rules of a race of two. No screen in here:
// kart_online.h draws it, the host tests run the other console with it.
// Part of console.cpp (through game_kart.h).
/* A race is not lockstep: each console drives its own kart in its own
   world and sends its state 20 times a second (LinkCore::kartSend, 24
   bytes: position, speed, heading, what it is doing, how far it got).
   The other kart is shown where that state says it is now - moved on by
   its speed for the time since, and eased there so that it does not
   jump; a lost packet is just covered by the next one. Every packet has
   the sender's clock (10 ms steps): the quickest packet so far sets how
   far the two clocks lie apart, so a late one is moved on further. Things that must
   arrive go along as events: every packet repeats the oldest event the
   other side has not acknowledged yet (its "ack" byte), so they come in
   order and once each, whatever is lost or late. The host (the console
   that started the challenge's coin toss) gives the start and says who
   won; each console decides what hits its own kart.                  */

enum { KV_START = 1, KV_BOX, KV_PEEL, KV_ROCKET, KV_PEELHIT, KV_ROCKETHIT, KV_FINISH, KV_RESULT };
enum { KP_READY, KP_RACE, KP_DONE };
#define KGN_Q   12                     // events waiting for their ack
#define KGN_MS  50                     // a state packet this often

struct __attribute__((packed)) KgPkt {
  uint8_t  hdr[6];                     // 'M' 'A' 3 LK_KART and the session (LinkCore fills them)
  uint8_t  stamp;                      // the sender's clock / 10 ms: an older packet is dropped
  uint8_t  phase;                      // KP_*, the driver in the high half
  uint16_t x, y;                       // 1/16 px
  uint8_t  a;                          // heading / 256
  int8_t   vx, vy;                     // 1/32 px a step
  uint8_t  st;                         // what the kart does (KQ_*)
  int16_t  prog;                       // centre line points driven since the start
  uint8_t  ev[5];                      // an event: number, kind, three bytes
  uint8_t  ack;                        // the other side's newest event we have
};
static_assert(sizeof(KgPkt) == LK_KART_LEN, "a KART packet is 24 bytes");
enum { KQ_DRIFTL = 1, KQ_DRIFTR = 2, KQ_BOOST = 4, KQ_SHIELD = 8, KQ_SPIN = 16, KQ_TUMBLE = 32, KQ_FALL = 64, KQ_CHARGE = 128 };

struct KgDuel {
  bool     host;
  uint8_t  phase;                      // KP_READY until the start, then the race
  uint32_t countAt;                    // ms: the countdown began (0 = not yet)
  // what we send
  uint8_t  q[KGN_Q][5], qn, evNext;
  uint32_t sendAt;
  // what we got
  KgPkt    in;
  bool     seen;                       // a packet of theirs came
  uint32_t inAt;                       // ms it came
  uint16_t inAge;                      // ms it was older than the quickest packet so far
  uint16_t base;                       // ms the quickest packet takes (half the internet's round trip)
  uint8_t  dMin, dMinNew;              // (our clock - its clock) / 10 ms: the least so far, the least lately
  uint32_t dMinAt;
  uint8_t  lastEv;                     // their newest event we took
  uint8_t  theirDrv;
  // the race of two
  uint32_t myTime, theirTime;          // race ms at the line, 0 = not yet
  bool     theirDone;
  int8_t   result;                     // -1 not yet, 0 we won, 1 they won
  uint32_t doneAt;                     // ms we crossed the line
  uint32_t pktIn, evIn, evOut;         // counted (the host tests)
};

static void kgDuelInit(KgDuel &D, bool host) {
  memset(&D, 0, sizeof(D));
  D.host = host;
  D.result = -1;
  D.evNext = 1;
  D.theirDrv = 255;
}

static void kgDuelPush(KgDuel &D, uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
  if (D.qn >= KGN_Q) return;                                    // never so many in flight
  uint8_t *e = D.q[D.qn++];
  e[0] = D.evNext; e[1] = type; e[2] = a; e[3] = b; e[4] = c;
  D.evNext = D.evNext == 255 ? 1 : (uint8_t)(D.evNext + 1);
  D.evOut++;
}

static uint8_t kgKartState(const KgKart &k) {
  return (uint8_t)((k.drift < 0 ? KQ_DRIFTL : k.drift > 0 ? KQ_DRIFTR : 0) | (k.boost ? KQ_BOOST : 0) | (k.shield ? KQ_SHIELD : 0) |
                   (k.spin ? KQ_SPIN : 0) | (k.tumble ? KQ_TUMBLE : 0) | (k.fall ? KQ_FALL : 0) |
                   (k.charge >= KG_DRIFT2 ? KQ_CHARGE : 0));
}

static int8_t kgClamp8(int32_t v) { return (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v); }

static void kgDuelPacket(KgDuel &D, const KgKart &k, uint8_t drv, KgPkt &p, uint32_t now) {
  memset(&p, 0, sizeof(p));
  p.stamp = (uint8_t)(now / 10);
  p.phase = (uint8_t)(D.phase | drv << 4);
  p.x = (uint16_t)(k.x >> 4); p.y = (uint16_t)(k.y >> 4);
  p.a = (uint8_t)(k.a >> 8);
  p.vx = kgClamp8(k.vx >> 3); p.vy = kgClamp8(k.vy >> 3);
  p.st = kgKartState(k);
  p.prog = (int16_t)k.prog;
  if (D.qn) memcpy(p.ev, D.q[0], 5);
  p.ack = D.lastEv;
}

// a packet came: newer than the last one? Their ack takes our events off the list
static bool kgDuelTake(KgDuel &D, const uint8_t *d, uint32_t now) {
  KgPkt p;
  memcpy(&p, d, sizeof(p));
  if (D.seen && (int8_t)(p.stamp - D.in.stamp) <= 0) return false;
  uint8_t gap = (uint8_t)(now / 10 - p.stamp);
  if (!D.seen || (int8_t)(gap - D.dMin) < 0) D.dMin = gap;
  if (!D.seen || (int8_t)(gap - D.dMinNew) < 0) D.dMinNew = gap;
  if (now - D.dMinAt > 4000) { D.dMin = D.dMinNew; D.dMinNew = gap; D.dMinAt = now; }   // the clocks may drift a little
  D.inAge = (uint16_t)((int8_t)(gap - D.dMin) > 0 ? (int8_t)(gap - D.dMin) * 10 : 0);
  D.in = p;
  D.seen = true;
  D.inAt = now;
  D.theirDrv = (uint8_t)((p.phase >> 4) % KG_KARTS);
  D.pktIn++;
  while (D.qn && (int8_t)(p.ack - D.q[0][0]) >= 0) { memmove(D.q[0], D.q[1], (size_t)(D.qn - 1) * 5); D.qn--; }
  return true;
}

// the remote kart, where its last state says it is by now; eased there
static void kgDuelRemote(KgDuel &D, KgKart &k, uint32_t now, const KgTrack &T) {
  if (!D.seen) return;
  const KgPkt &p = D.in;
  uint32_t age = now - D.inAt + D.inAge + D.base;
  if (age > 400) age = 400;
  int32_t vx = (int32_t)p.vx << 3, vy = (int32_t)p.vy << 3;
  int32_t tx = ((int32_t)p.x << 4) + vx * (int32_t)age / KG_STEP, ty = ((int32_t)p.y << 4) + vy * (int32_t)age / KG_STEP;
  int32_t dx = tx - k.x, dy = ty - k.y;
  if (dx > (48 << 8) || dx < -(48 << 8) || dy > (48 << 8) || dy < -(48 << 8) || (p.st & KQ_FALL)) { k.x = tx; k.y = ty; }
  else { k.x += dx / 2; k.y += dy / 2; }
  uint16_t a = (uint16_t)(p.a << 8);
  k.a = (uint16_t)(k.a + kgAngDiff(a, k.a) / 2);
  k.vx = vx; k.vy = vy;
  k.drift = (int8_t)((p.st & KQ_DRIFTL) ? -1 : (p.st & KQ_DRIFTR) ? 1 : 0);
  k.charge = (p.st & KQ_CHARGE) ? KG_DRIFT2 : (k.drift ? KG_DRIFT1 : 0);
  k.boost = (p.st & KQ_BOOST) ? 10 : 0;
  k.shield = (p.st & KQ_SHIELD) ? 200 : 0;
  k.fall = (p.st & KQ_FALL) ? 30 : 0;
  k.z = (p.st & KQ_TUMBLE) ? 600 : 0;
  k.prog = p.prog;
  k.lap = (uint8_t)(p.prog > 0 ? p.prog / (int32_t)T.n : 0);
  if (k.lap > KG_LAPS) k.lap = KG_LAPS;
  k.surf = kgMat(T, k.x >> 8, k.y >> 8);
}

/* Their events, in order, each once. rem = the remote kart's index;
   returns KV_START when the host said go.                            */
static uint8_t kgDuelEvents(KgDuel &D, KgRace &R, uint8_t rem) {
  uint8_t got = 0;
  const uint8_t *e = D.in.ev;
  if (!D.seen || !e[0] || e[0] != (D.lastEv == 255 ? 1 : D.lastEv + 1)) return 0;
  D.lastEv = e[0];
  D.evIn++;
  KgTrack &T = *R.T;
  KgKart &k = R.k[rem];
  switch (e[1]) {
  case KV_START: got = KV_START; break;
  case KV_BOX: if (e[2] < T.nbox) T.box[e[2]].off = 160; break;
  case KV_PEEL:
    for (uint8_t p = 0; p < KG_PEELS; p++)
      if (!R.peel[p].on) { R.peel[p] = { (int16_t)(e[2] * 4 + 2), (int16_t)(e[3] * 4 + 2), e[4], rem, true }; break; }
    break;
  case KV_ROCKET:
    for (uint8_t p = 0; p < KG_ROCKETS; p++)
      if (!R.rk[p].on) {
        KgRocket &q = R.rk[p];
        q.x = k.x + (kgCos(k.a) * 8 >> 6); q.y = k.y + (kgSin(k.a) * 8 >> 6);
        q.a = k.a; q.idx = k.idx; q.life = 600; q.on = true; q.owner = rem; q.id = e[2];
        q.target = e[3] ? R.me : 255;
        break;
      }
    break;
  case KV_PEELHIT:                                               // they ran over one: e[3] = 1 when it was theirs
    for (uint8_t p = 0; p < KG_PEELS; p++)
      if (R.peel[p].on && R.peel[p].id == e[2] && (R.peel[p].owner == rem) == (e[3] != 0)) { R.peel[p].on = false; break; }
    break;
  case KV_ROCKETHIT:
    for (uint8_t p = 0; p < KG_ROCKETS; p++)
      if (R.rk[p].on && R.rk[p].id == e[2] && (R.rk[p].owner == rem) == (e[3] != 0)) { R.rk[p].on = false; break; }
    break;
  case KV_FINISH:
    D.theirTime = (uint32_t)(e[2] | e[3] << 8 | e[4] << 16) * 10;
    if (!D.theirTime) D.theirTime = 1;
    D.theirDone = true;
    k.finish = D.theirTime; k.flags |= KK_DONE;
    break;
  case KV_RESULT:
    if (!D.host) D.result = (int8_t)(e[2] == 1 ? 0 : 1);         // e[2]: who won, 0 = the host
    break;
  }
  return got;
}

// the player's networked things (KgRace::net) -> events
static KgDuel *kgDuelNow = NULL;
static void kgDuelNet(uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
  KgDuel *D = kgDuelNow;
  if (!D) return;
  switch (type) {
  case KN_BOX:       kgDuelPush(*D, KV_BOX, a, 0, 0); break;
  case KN_PEEL:      kgDuelPush(*D, KV_PEEL, a, b, c); break;
  case KN_ROCKET:    kgDuelPush(*D, KV_ROCKET, a, b != 255 ? 1 : 0, 0); break;
  case KN_PEELHIT:   kgDuelPush(*D, KV_PEELHIT, a, b, 0); break;      // b = 1: it was our own
  case KN_ROCKETHIT: kgDuelPush(*D, KV_ROCKETHIT, a, b, 0); break;
  case KN_FINISH:    kgDuelPush(*D, KV_FINISH, a, b, c); break;
  }
}

/* The host decides: both at the line - the quicker one; one at the line
   and the other one gone or 20 s behind - the one at the line.       */
static void kgDuelDecide(KgDuel &D, uint32_t now, bool theyGone) {
  if (!D.host || D.result >= 0) return;
  bool decide = (D.myTime && (D.theirDone || theyGone || (int32_t)(now - D.doneAt) > 20000)) || (D.theirDone && theyGone);
  if (!decide) return;
  bool iWon = D.myTime && (!D.theirDone || D.myTime <= D.theirTime);
  D.result = iWon ? 0 : 1;
  kgDuelPush(D, KV_RESULT, iWon ? 0 : 1, 0, 0);
}

// one turn of the protocol: what came in, and our state out every 50 ms
static uint8_t kgDuelTick(KgDuel &D, KgRace &R, uint8_t meK, uint8_t rem, LinkCore &L, uint32_t now) {
  uint8_t buf[LK_KART_LEN], got = 0;
  while (L.kartIn(buf))
    if (kgDuelTake(D, buf, now)) got |= kgDuelEvents(D, R, rem);
  if (lkDue(now, D.sendAt)) {
    D.sendAt = now + KGN_MS;
    KgPkt p;
    kgDuelPacket(D, R.k[meK], R.k[meK].drv, p, now);
    L.kartSend((uint8_t *)&p);
  }
  return got;
}
