// Protocol test for main/linkcore.h: consoles on a simulated radio that
// loses packets on purpose.
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "../main/linkcore.h"
#include "../main/rtgames.h"

struct Air;
struct Node { LinkCore core; uint8_t mac[6]; Air *air; bool alive = true; };
struct Msg  { int from, to; std::vector<uint8_t> data; uint32_t at; };   // to -1 = broadcast

struct Air {
  std::vector<Node *> nodes;
  std::vector<Msg> queue;
  int loss = 0;                       // percent
  uint32_t now = 0, rs = 12345;
  uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
  void step(uint32_t ms) {            // advance time, deliver what is due
    for (uint32_t t = 0; t < ms; t += 10) {
      now += 10;
      std::vector<Msg> due, keep;
      for (auto &m : queue) (m.at <= now ? due : keep).push_back(m);
      queue = keep;
      for (auto &m : due)
        for (size_t i = 0; i < nodes.size(); i++) {
          if ((int)i == m.from || (m.to >= 0 && m.to != (int)i) || !nodes[i]->alive) continue;
          if ((int)(rnd() % 100) < loss) continue;
          nodes[i]->core.onPacket(nodes[m.from]->mac, m.data.data(), (int)m.data.size(), -50, now);
        }
      for (auto *n : nodes) if (n->alive) n->core.tick(now);
    }
  }
};

static void sendFn(void *ctx, const uint8_t *mac, const uint8_t *d, uint8_t len) {
  Node *n = (Node *)ctx;
  if (!n->alive) return;
  int from = -1, to = -1;
  for (size_t i = 0; i < n->air->nodes.size(); i++) {
    if (n->air->nodes[i] == n) from = (int)i;
    if (mac && !memcmp(n->air->nodes[i]->mac, mac, 6)) to = (int)i;
  }
  n->air->queue.push_back({ from, mac ? to : -1, std::vector<uint8_t>(d, d + len), n->air->now + 5 + n->air->rnd() % 20 });
}

static void addNode(Air &a, Node &n, uint8_t id, const char *name) {
  uint8_t mac[6] = { 0x24, 0x6F, 0x28, 0x10, 0x20, id };
  memcpy(n.mac, mac, 6);
  n.air = &a;
  a.nodes.push_back(&n);
  n.core.begin(mac, name, sendFn, &n, a.now, 1000 + id);
}

static int idxOf(LinkCore &c, const Node &n) {
  for (uint8_t i = 0; i < c.peerCount(); i++) if (!memcmp(c.peer(i).mac, n.mac, 6)) return i;
  return -1;
}

static int fails = 0;
static void check(const char *w, bool ok) { printf("  [%s] %s\n", ok ? "OK" : "FAIL", w); if (!ok) fails++; }

// plays `moves` moves in turn; true if both saw exactly the same sequence
static bool playGame(Air &a, Node &x, Node &y, int moves) {
  Node *turn = x.core.iStart ? &x : &y, *other = turn == &x ? &y : &x;
  for (int m = 0; m < moves; m++) {
    uint8_t mv = (uint8_t)(m * 7 + 3), got = 255;
    if (!turn->core.sendMove(mv, a.now)) return false;
    for (int w = 0; w < 200 && !other->core.moveIn(&got); w++) a.step(50);
    if (got != mv) { printf("      move %d: sent %u got %u\n", m, mv, got); return false; }
    Node *t = turn; turn = other; other = t;
  }
  x.core.finish(); y.core.finish();
  for (int w = 0; w < 200 && !(x.core.delivered() && y.core.delivered()); w++) a.step(50);
  return x.core.delivered() && y.core.delivered();
}

/* A real-time game in lockstep: both consoles run their own copy of the
   game and feed it from syncStep(). Returns true when both copies ended
   with exactly the same state; ticks/s tells how smooth it ran.        */
template <class G>
static uint8_t botInput(const G &g, uint8_t me, uint32_t &r);
template <>
uint8_t botInput<RtPong>(const RtPong &g, uint8_t me, uint32_t &r) {
  int c = g.pad[me] + RP_PH / 2, b = g.by >> 4;
  if (rtRand(r) % 5 < 2 || (me ? g.vx < 0 : g.vx > 0)) return 0;   // slow, only when the ball comes
  return b < c - 2 ? 1 : (b > c + 2 ? 2 : 0);
}
template <>
uint8_t botInput<RtSnake>(const RtSnake &g, uint8_t me, uint32_t &r) {
  static const int8_t DX[5] = { 0, 0, 0, -1, 1 }, DY[5] = { 0, -1, 1, 0, 0 };
  uint8_t best = g.dir[me];
  for (int k = 0; k < 5; k++) {                              // keep going, else any free way
    uint8_t d = k == 0 ? g.dir[me] : (uint8_t)(1 + rtRand(r) % 4);
    int nx = g.x[me][0] + DX[d], ny = g.y[me][0] + DY[d];
    if (nx >= 0 && nx < RS_W && ny >= 0 && ny < RS_H && !g.body(nx, ny)) { best = d; if (k || rtRand(r) % 6) break; }
  }
  return best;
}

template <>
uint8_t botInput<RtPac>(const RtPac &g, uint8_t me, uint32_t &r) {
  const RmEnt &e = g.pac[me];
  uint8_t w = g.want[me];
  if (!w || rtRand(r) % 30 == 0 || RtPac::wallAt(e.x + RtPac::dx(w), e.y + RtPac::dy(w)))
    for (int t = 0; t < 8; t++) {
      uint8_t d = 1 + rtRand(r) % 4;
      if (!RtPac::wallAt(e.x + RtPac::dx(d), e.y + RtPac::dy(d))) return d;
    }
  return w;
}

template <class G>
static bool playRealtime(int loss, int seed, uint8_t game, double *tps, uint8_t *win) {
  Air a; a.loss = loss; a.rs = 4242 + seed * 17;
  Node A, B; addNode(a, A, 1, "ANNA"); addNode(a, B, 2, "TOM");
  for (int w = 0; w < 100 && idxOf(A.core, B) < 0; w++) a.step(100);
  int b = idxOf(A.core, B);
  if (b < 0 || !(A.core.peer(b).caps & LinkCore::capOf(game))) return false;
  A.core.invite(b, game, a.now);
  for (int w = 0; w < 100 && B.core.state != LK_INVITED; w++) a.step(50);
  B.core.answer(true, a.now);
  for (int w = 0; w < 200 && A.core.state != LK_PLAYING; w++) a.step(50);
  if (A.core.state != LK_PLAYING || A.core.session() != B.core.session()) return false;

  Node *n[2] = { &A, &B };
  G g[2];
  uint32_t r[2] = { 11u + seed, 99u + seed }, next[2] = { a.now, a.now };
  for (int i = 0; i < 2; i++) g[i].begin(A.core.session() * 2654435761u);
  uint32_t t0 = a.now;
  while (a.now - t0 < 600000 && (!g[0].winner || !g[1].winner)) {
    a.step(10);
    for (int i = 0; i < 2; i++) {
      LinkCore &c = n[i]->core;
      uint8_t me = c.iStart ? 0 : 1;
      for (int k = 0; k < 3 && !g[i].winner && lkDue(a.now, next[i]); k++) {
        uint8_t mine, theirs;
        if (!c.syncStep(&mine, &theirs)) break;
        uint8_t in[2]; in[me] = mine; in[1 - me] = theirs;
        g[i].step(in);
        c.syncPut(botInput(g[i], me, r[i]), a.now);
        next[i] += RT_TICK_MS;
        if ((int32_t)(a.now - next[i]) > 100) next[i] = a.now;
      }
      if (g[i].winner && c.state == LK_PLAYING) c.finish();
    }
  }
  for (int w = 0; w < 200 && !(A.core.delivered() && B.core.delivered()); w++) a.step(50);
  *tps = A.core.syncTick() * 1000.0 / (a.now - t0);
  *win = g[0].winner;
  return g[0].winner && !memcmp(&g[0], &g[1], sizeof(G)) && A.core.delivered() && B.core.delivered();
}

int main() {
  // ---- real-time games: both copies stay the same, even with heavy loss ----
  for (uint8_t game : { (uint8_t)LKG_PONG, (uint8_t)LKG_SNAKE, (uint8_t)LKG_PAC }) {
    const char *gn = game == LKG_PONG ? "pong" : (game == LKG_SNAKE ? "snake" : "pac-man");
    int same = 0, runs = 0, wins[4] = { 0 };
    double slow = 1e9;
    for (int loss : { 0, 30, 50 })
      for (int seed = 0; seed < 8; seed++, runs++) {
        double tps = 0; uint8_t w = 0;
        bool ok = game == LKG_PONG  ? playRealtime<RtPong>(loss, seed, game, &tps, &w)
                : game == LKG_SNAKE ? playRealtime<RtSnake>(loss, seed, game, &tps, &w)
                                    : playRealtime<RtPac>(loss, seed, game, &tps, &w);
        if (ok) { same++; wins[w]++; } else printf("      %s loss %d seed %d: out of step\n", gn, loss, seed);
        if (loss == 30 && tps < slow) slow = tps;
      }
    printf("      %s: %d of %d games identical on both consoles, winners p0 %d / p1 %d / draw %d, "
           "slowest at 30%% loss %.0f ticks/s (50 = full speed)\n",
           gn, same, runs, wins[1], wins[2], wins[3], slow);
    char w1[64], w2[64];
    snprintf(w1, sizeof(w1), "%s: same game on both consoles despite loss", gn);
    snprintf(w2, sizeof(w2), "%s: smooth enough at 30%% loss", gn);
    check(w1, same == runs);
    check(w2, slow > 35);
  }

  // ---- list, invite, accept, a whole game with heavy loss ----
  int ok = 0, runs = 0;
  for (int loss : { 0, 30, 50 })
    for (int seed = 0; seed < 20; seed++, runs++) {
      Air a; a.loss = loss; a.rs = 777 + seed * 31;
      Node A, B; addNode(a, A, 1, "ANNA"); addNode(a, B, 2, "TOM");
      for (int w = 0; w < 100 && idxOf(A.core, B) < 0; w++) a.step(100);   // first beacon
      int b = idxOf(A.core, B);
      if (b < 0) { printf("      loss %d seed %d: not listed\n", loss, seed); continue; }
      A.core.invite(b, LKG_C4, a.now);
      for (int w = 0; w < 100 && B.core.state != LK_INVITED; w++) a.step(50);
      if (B.core.state != LK_INVITED || B.core.game != LKG_C4) { printf("      loss %d seed %d: no invite (%d)\n", loss, seed, B.core.state); continue; }
      if (B.core.iStart == A.core.iStart) { printf("      same starter\n"); continue; }
      B.core.answer(true, a.now);
      for (int w = 0; w < 200 && A.core.state != LK_PLAYING; w++) a.step(50);
      if (A.core.state != LK_PLAYING || B.core.state != LK_PLAYING) { printf("      loss %d seed %d: not playing %d %d why %d %d\n", loss, seed, A.core.state, B.core.state, A.core.why, B.core.why); continue; }
      if (!playGame(a, A, B, 30)) { printf("      loss %d seed %d: game broke %d %d why %d %d\n", loss, seed, A.core.state, B.core.state, A.core.why, B.core.why); continue; }
      ok++;
    }
  printf("      %d of %d games complete (0 / 30 / 50 %% packet loss)\n", ok, runs);
  check("challenge, answer and 30 moves arrive in order despite loss", ok == runs);

  {  // names, codes and the list
    Air a; Node A, B; addNode(a, A, 1, "ANNA"); addNode(a, B, 2, "TOM");
    a.step(2500);
    int b = idxOf(A.core, B);
    char code[5]; LinkCore::codeOf(B.mac, code);
    check("other console listed with name and code",
          b >= 0 && !strcmp(A.core.peer(b).name, "TOM") && !strcmp(A.core.peer(b).code, code));
    B.alive = false;
    a.step(LK_PEER_MS + 1500);
    check("silent console drops off the list", idxOf(A.core, B) < 0);
  }

  {  // decline
    Air a; Node A, B; addNode(a, A, 1, "ANNA"); addNode(a, B, 2, "TOM");
    a.step(2500);
    A.core.invite(idxOf(A.core, B), LKG_TTT, a.now);
    a.step(1000);
    B.core.answer(false, a.now);
    a.step(2000);
    check("declined challenge reported", A.core.state == LK_OVER && A.core.why == LKE_DECLINED);
    check("declining player back in the lobby", B.core.state == LK_LOBBY);
  }

  {  // nobody answers: both sides give up after 30 s
    Air a; Node A, B; addNode(a, A, 1, "ANNA"); addNode(a, B, 2, "TOM");
    a.step(2500);
    uint32_t t0 = a.now;
    A.core.invite(idxOf(A.core, B), LKG_TTT, a.now);
    while (A.core.state == LK_INVITING && a.now - t0 < 40000) a.step(100);
    uint32_t took = a.now - t0;
    printf("      challenge expired after %u ms\n", (unsigned)took);
    check("challenge expires after 30 s", A.core.why == LKE_TIMEOUT && took >= 30000 && took <= 30500);
    a.step(1000);
    check("challenged side gives up too", B.core.state == LK_OVER && (B.core.why == LKE_TIMEOUT || B.core.why == LKE_LEFT));
  }

  {  // busy: a third console challenges somebody in a game
    Air a; Node A, B, C; addNode(a, A, 1, "ANNA"); addNode(a, B, 2, "TOM"); addNode(a, C, 3, "EVA");
    a.step(2500);
    A.core.invite(idxOf(A.core, B), LKG_C4, a.now);
    a.step(1000); B.core.answer(true, a.now); a.step(1500);
    int bi = idxOf(C.core, B);
    check("console in a game shows as busy", bi >= 0 && C.core.peer(bi).busy);
    check("busy console cannot be challenged", !C.core.invite(bi, LKG_C4, a.now));
  }

  {  // opponent leaves / disappears
    Air a; Node A, B; addNode(a, A, 1, "ANNA"); addNode(a, B, 2, "TOM");
    a.step(2500);
    A.core.invite(idxOf(A.core, B), LKG_C4, a.now);
    a.step(1000); B.core.answer(true, a.now); a.step(1500);
    B.core.cancel();
    a.step(1000);
    check("leaving the game is reported to the other side", A.core.state == LK_OVER && A.core.why == LKE_LEFT);

    Air a2; Node C, D; addNode(a2, C, 1, "ANNA"); addNode(a2, D, 2, "TOM");
    a2.step(2500);
    C.core.invite(idxOf(C.core, D), LKG_C4, a2.now);
    a2.step(1000); D.core.answer(true, a2.now); a2.step(1500);
    uint32_t t0 = a2.now;
    D.alive = false;                                        // switched off
    while (C.core.state == LK_PLAYING && a2.now - t0 < 30000) a2.step(100);
    printf("      switched off console noticed after %u ms\n", (unsigned)(a2.now - t0));
    check("switched off opponent noticed after about 15 s",
          C.core.why == LKE_GONE && a2.now - t0 >= 14000 && a2.now - t0 <= 16500);
  }

  {  // lost "yes": the challenger learns it from the resent answer
    Air a; a.loss = 0; Node A, B; addNode(a, A, 1, "ANNA"); addNode(a, B, 2, "TOM");
    a.step(2500);
    A.core.invite(idxOf(A.core, B), LKG_TTT, a.now);
    a.step(1000);
    A.alive = false; B.core.answer(true, a.now); a.step(200); A.alive = true;   // answer lost
    for (int w = 0; w < 100 && A.core.state != LK_PLAYING; w++) a.step(50);
    check("lost answer is repeated", A.core.state == LK_PLAYING && B.core.state == LK_PLAYING);
  }

  printf("%s\n", fails ? "### FAILURES ###" : "all checks passed");
  return fails ? 1 : 0;
}
