// Protocol test for main/linkcore.h: consoles on a simulated radio that
// loses packets on purpose.
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "../main/linkcore.h"

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

int main() {
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
