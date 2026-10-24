// Host stand-in for the server's relay (MiniArcadeServer/tvserver/net.py):
// the same answers by the same rules, in simulated time, with a delay on
// the way there and back. Two clients poll it: our console (through the
// pretend tvnet.h) and the bot console ANNA - a second NetLink
// (main/inetcore.h) whose LinkCore is link.h's bot (simPeer), so the bot
// plays every game over the relay as it does on the radio. BOB is paired
// but never online, CARL is online and always busy. fsExtra(): DAVE and EVE
// online and free, FRED seen 4 minutes ago. /api/net/hello gives our
// console a token without the PIN while simNetOpen (the server's NET_OPEN).
#pragma once
#include <map>
#include <deque>
#include <string>
#include <vector>
#include <algorithm>
#include <math.h>
#include "../main/inetcore.h"

static bool     simNetNoRb = false;                     // the server of before: no rollback
static uint32_t simNetMs = 25, simNetJit = 10;          // one way: this much plus up to jit
static bool     simNetDown = false;                     // nothing gets through (requests fail after 1.5 s)
static bool     simNetOpen = true;                      // NET_OPEN: /api/net/hello gives a token without the PIN
static uint32_t simNetHellos = 0;
static std::string simNetHelloPath;                     // the last one asked
static uint32_t simNetMsgs = 0, simNetPolls[2] = { 0, 0 }, simNetHeld = 0, simNetLead = 0, simNetSessions = 0;
static uint32_t simNetMaxQ = 0;                         // the longest queue for one side
static std::vector<std::string> simNetLog;              // every request but the polls

struct FsCon {
  std::string tok, code, name;
  uint32_t seen = 0;
  bool     on = false, fakeBusy = false, fakeOn = false;   // fakeOn: online and free, never answers
  uint8_t  fakeAgo = 0;                                 // seen this many minutes ago (offline now)
  uint8_t  game = 0;
  uint16_t rtt = 100;
  bool     rb = false;                                  // it can play with rollback
  uint32_t ver = 0, sentVer = 0xFFFFFFFF;
  std::vector<std::string> fr;                          // friends' codes
};
struct FsInv { std::string id, from, to; uint8_t g; uint32_t t, done = 0; std::string st = "wait"; int n; };
struct FsSes {
  uint16_t sid; std::string p[2]; uint8_t g, lead; int first; bool rb = false;
  std::deque<std::pair<uint32_t, std::string>> q[2];
  uint32_t nextIn[2] = { 1, 1 }, nextOut[2] = { 1, 1 }, seen[2];
  std::string end[2];
  bool left[2] = { false, false };
  uint32_t overT = 0;
  int idx(const std::string &c) const { return p[0] == c ? 0 : (p[1] == c ? 1 : -1); }
};
static std::map<std::string, FsCon> fsCon;              // by code
static std::map<std::string, FsInv> fsInv;
static std::map<uint16_t, FsSes> fsSes;
static std::map<std::string, uint16_t> fsOf;            // code -> session
static int fsCount = 0;
static uint32_t fsRnd = 4321;
static uint32_t fsRand() { fsRnd ^= fsRnd << 13; fsRnd ^= fsRnd >> 17; fsRnd ^= fsRnd << 5; return fsRnd; }

static const char SIM_ME_TOK[] = "TOKEN-1234567890-abcdefghijklmnop";   // the pretend tvnet.h's pairing
static const char SIM_NET_TOK[] = "NTOK-0987654321-internet-only";        // /api/net/hello's
static void fsSetup() {
  fsCon.clear(); fsInv.clear(); fsSes.clear(); fsOf.clear();
  FsCon me;   me.tok = SIM_ME_TOK;   me.code = "MY3CDE"; me.name = "PLAYER";
  FsCon anna; anna.tok = "TOK-ANNA"; anna.code = "AN4K7Q"; anna.name = "ANNA";
  FsCon bob;  bob.tok = "TOK-BOB";   bob.code = "BQB234"; bob.name = "BOB";
  FsCon carl; carl.tok = "TOK-CARL"; carl.code = "CRL567"; carl.name = "CARL"; carl.fakeBusy = true; carl.game = 10;
  for (FsCon *c : { &me, &anna, &bob, &carl }) fsCon[c->code] = *c;
}
static void fsExtra() {                                 // more consoles: two free ones, one seen lately
  FsCon dave; dave.tok = "TOK-DAVE"; dave.code = "DV5W8X"; dave.name = "DAVE"; dave.fakeOn = true; dave.game = 3;
  FsCon eve;  eve.tok = "TOK-EVE";   eve.code = "EV6Y9Z";  eve.name = "EVE";   eve.fakeOn = true;
  FsCon fred; fred.tok = "TOK-FRED"; fred.code = "FR7D2M"; fred.name = "FRED"; fred.fakeAgo = 4;
  for (FsCon *c : { &dave, &eve, &fred }) fsCon[c->code] = *c;
}
static FsCon *fsByTok(const std::string &t) { for (auto &kv : fsCon) if (kv.second.tok == t) return &kv.second; return NULL; }
static FsCon *fsByCode(const std::string &c) { auto it = fsCon.find(c); return it == fsCon.end() ? NULL : &it->second; }
static bool fsOnline(const FsCon &c, uint32_t now) { return c.fakeBusy || c.fakeOn || (c.on && now - c.seen < 30000); }
// offline, seen in the last 10 minutes: minutes ago, -1 = no
static int fsAgo(const FsCon &c, uint32_t now) {
  if (fsOnline(c, now)) return -1;
  if (c.fakeAgo) return c.fakeAgo;
  return c.on && now - c.seen < 600000 ? (int)((now - c.seen) / 60000) : -1;
}
static FsSes *fsSessionOf(const std::string &c) {
  auto it = fsOf.find(c);
  if (it == fsOf.end()) return NULL;
  auto s = fsSes.find(it->second);
  if (s == fsSes.end() || s->second.idx(c) < 0 || s->second.left[s->second.idx(c)]) return NULL;
  return &s->second;
}
static bool fsBusy(const FsCon &c) {
  if (c.fakeBusy) return true;
  FsSes *s = fsSessionOf(c.code);
  return s && s->end[s->idx(c.code)].empty();
}
static void fsNotify(const std::string &c) { if (FsCon *k = fsByCode(c)) k->ver++; }
static void fsFinish(FsInv &iv, const char *st, uint32_t now) { iv.st = st; iv.done = now; fsNotify(iv.from); fsNotify(iv.to); }
static void fsDrop(uint16_t sid) {
  auto it = fsSes.find(sid);
  if (it == fsSes.end()) return;
  for (auto &c : it->second.p) if (fsOf.count(c) && fsOf[c] == sid) fsOf.erase(c);
  fsSes.erase(it);
}

static void fsSweep(uint32_t now) {
  for (auto it = fsInv.begin(); it != fsInv.end(); ) {
    FsInv &iv = it->second;
    FsCon *f = fsByCode(iv.from);
    if (iv.st == "wait") {
      if (now - iv.t > 30000) fsFinish(iv, "timeout", now);
      else if (!f || !fsOnline(*f, now) || now - f->seen > 12000) fsFinish(iv, "gone", now);
    } else if (now - iv.done > 10000) { it = fsInv.erase(it); continue; }
    ++it;
  }
  std::vector<uint16_t> drop;
  for (auto &kv : fsSes) {
    FsSes &s = kv.second;
    for (int i = 0; i < 2; i++)
      if (s.end[i].empty() && !s.left[i] && now - s.seen[i] > 12000) {
        s.end[i] = "lost";
        if (s.end[1 - i].empty()) { s.end[1 - i] = "gone"; fsNotify(s.p[1 - i]); }
        if (!s.overT) s.overT = now;
      }
    bool gone = (s.left[0] || s.end[0] == "lost") && (s.left[1] || s.end[1] == "lost");
    if ((s.left[0] && s.left[1]) || (s.overT && now - s.overT > 30000) || gone) drop.push_back(kv.first);
  }
  for (uint16_t d : drop) fsDrop(d);
}

// a query value of a path ("" = none)
static std::string fsQ(const std::string &path, const char *k) {
  size_t q = path.find('?');
  if (q == std::string::npos) return "";
  std::string a = "&" + path.substr(q + 1) + "&", key = std::string("&") + k + "=";
  size_t i = a.find(key);
  if (i == std::string::npos) return "";
  i += key.size();
  return a.substr(i, a.find('&', i) - i);
}

static void fsSesJson(std::string &o, const FsSes &s, int i) {
  const FsCon *op = fsByCode(s.p[1 - i]);
  char b[200];
  snprintf(b, sizeof(b), "{\"sid\":%u,\"g\":%u,\"first\":%d,\"lead\":%u,\"oc\":\"%s\",\"on\":\"%s\"", s.sid, s.g,
           s.first == i ? 1 : 0, s.lead, s.p[1 - i].c_str(), op ? op->name.c_str() : "?");
  o += b;
  if (s.rb) o += ",\"rb\":1";
}

// the answer to a poll (net.py's Relay.answer)
static std::string fsAnswer(FsCon &c, uint16_t sid, uint32_t got, int lists, uint32_t held, uint32_t now) {
  c.sentVer = c.ver;
  char b[256];
  snprintf(b, sizeof(b), "{\"me\":\"%s\",\"h\":%u", c.code.c_str(), held);
  std::string o = b;
  const FsInv *in = NULL, *out = NULL;
  for (auto &kv : fsInv) {
    if (kv.second.to == c.code && kv.second.st == "wait" && (!in || kv.second.n < in->n)) in = &kv.second;
    if (kv.second.from == c.code && (!out || kv.second.n > out->n)) out = &kv.second;
  }
  if (in) {
    int left = 30 - (int)((now - in->t) / 1000);
    snprintf(b, sizeof(b), ",\"inv\":{\"iid\":\"%s\",\"ic\":\"%s\",\"in\":\"%s\",\"ig\":%u,\"is\":%d}", in->id.c_str(),
             in->from.c_str(), fsByCode(in->from)->name.c_str(), in->g, left < 0 ? 0 : left);
    o += b;
  }
  if (out) { snprintf(b, sizeof(b), ",\"out\":{\"oid\":\"%s\",\"st\":\"%s\"}", out->id.c_str(), out->st.c_str()); o += b; }
  if (FsSes *s = fsSessionOf(c.code)) {
    int i = s->idx(c.code);
    o += ",\"ses\":";
    fsSesJson(o, *s, i);
    std::string msg;
    bool pending = !s->q[i].empty() && s->q[i].back().first > got;
    if (!s->end[i].empty() && !pending) o += ",\"se\":\"" + s->end[i] + "\"";
    o += "}";
    if (s->sid == sid) {
      snprintf(b, sizeof(b), ",\"ack\":%u", s->nextIn[i] - 1);
      o += b;
      int k = 0;
      for (auto &m : s->q[i])
        if (m.first > got && k < 16) { snprintf(b, sizeof(b), "%s%u.", k ? "," : "", m.first); msg += b; msg += m.second; k++; }
      if (k) o += ",\"msg\":\"" + msg + "\"";
    }
  }
  if (lists) {
    std::string lob, fr, rc;
    for (int pass = 0; pass < 4; pass++)                // free ones first, friends first among them
      for (auto &kv : fsCon) {
        const FsCon &x = kv.second;
        bool f = std::find(c.fr.begin(), c.fr.end(), x.code) != c.fr.end();
        if (x.code == c.code || !fsOnline(x, now) || fsBusy(x) != (pass >= 2) || f != (pass % 2 == 0)) continue;
        snprintf(b, sizeof(b), "%s{\"c\":\"%s\",\"n\":\"%s\",\"g\":%u,\"b\":%d,\"f\":%d}", lob.empty() ? "" : ",",
                 x.code.c_str(), x.name.c_str(), x.game, fsBusy(x) ? 1 : 0, f ? 1 : 0);
        lob += b;
      }
    for (const std::string &fc : c.fr) {
      const FsCon *x = fsByCode(fc);
      if (!x) continue;
      snprintf(b, sizeof(b), "%s{\"c\":\"%s\",\"n\":\"%s\",\"o\":%d,\"b\":%d}", fr.empty() ? "" : ",", x->code.c_str(),
               x->name.c_str(), fsOnline(*x, now) ? 1 : 0, fsBusy(*x) ? 1 : 0);
      fr += b;
    }
    o += ",\"lob\":[" + lob + "],\"fr\":[" + fr + "]";
    if (lists >= 2) {                                   // newer consoles: the ones seen lately
      for (auto &kv : fsCon) {
        const FsCon &x = kv.second;
        int ago = fsAgo(x, now);
        if (x.code == c.code || ago < 0) continue;
        bool f = std::find(c.fr.begin(), c.fr.end(), x.code) != c.fr.end();
        snprintf(b, sizeof(b), "%s{\"c\":\"%s\",\"n\":\"%s\",\"f\":%d,\"m\":%d}", rc.empty() ? "" : ",",
                 x.code.c_str(), x.name.c_str(), f ? 1 : 0, ago);
        rc += b;
      }
      o += ",\"rc\":[" + rc + "]";
    }
  }
  return o + "}";
}

static bool fsReady(const FsCon &c, uint16_t sid, uint32_t got) {
  if (c.ver != c.sentVer) return true;
  auto it = fsSes.find(sid);
  if (!sid || it == fsSes.end()) return false;
  int i = it->second.idx(c.code);
  return i >= 0 && !it->second.q[i].empty() && it->second.q[i].back().first > got;
}

struct FsReq {
  bool active = false, processed = false, done = false;
  std::string tok, path, body, resp;
  uint32_t arrive = 0, holdUntil = 0, respAt = 0, wait = 0, got = 0;
  uint16_t sid = 0;
  int lists = 0;
  int status = 0;
};
static FsReq fsReq[2];                                  // 0 = our console, 1 = ANNA
static uint32_t fsLat() { return simNetMs + (simNetJit ? fsRand() % simNetJit : 0); }

// a poll arrives: the report, the messages; false = the answer waits (net.py's Relay.poll)
static bool fsPoll(FsReq &r, FsCon &c, uint32_t now) {
  const char *b = r.body.c_str(), *e = b + r.body.size();
  c.seen = now; c.on = true;
  char n[16] = "";
  if (jsStr(b, e, "n", n, sizeof(n)) && n[0]) c.name = n;
  c.game = (uint8_t)jsInt(b, e, "g", c.game);
  c.rtt = (uint16_t)jsInt(b, e, "rtt", c.rtt);
  c.rb = jsInt(b, e, "rb", 0) != 0 && !simNetNoRb;
  r.wait = (uint32_t)jsInt(b, e, "wait", 0);
  r.sid = (uint16_t)jsInt(b, e, "sid", 0);
  r.got = (uint32_t)jsInt(b, e, "got", 0);
  r.lists = (int)jsInt(b, e, "l", 0);
  auto it = fsSes.find(r.sid);
  if (r.sid && it != fsSes.end() && it->second.idx(c.code) >= 0 && !it->second.left[it->second.idx(c.code)]) {
    FsSes &s = it->second;
    int i = s.idx(c.code), o = 1 - i;
    s.seen[i] = now;
    while (!s.q[i].empty() && s.q[i].front().first <= r.got) s.q[i].pop_front();
    const char *m = jsFind(b, e, "m");
    for (const char *q = m; q && q < e; ) {             // [[seq,"hex"],...]
      const char *a = strchr(q, '[');
      if (!a || a >= e) break;
      if (a[1] == '[') { q = a + 1; continue; }
      uint32_t seq = (uint32_t)strtoul(a + 1, NULL, 10);
      const char *h0 = strchr(a, '"'), *h1 = h0 ? strchr(h0 + 1, '"') : NULL;
      if (!h1) break;
      q = h1 + 1;
      if (seq != s.nextIn[i]) continue;                  // a repeat (or a gap: it comes again)
      s.nextIn[i]++;
      if (!s.end[o].empty() || s.left[o]) continue;
      s.q[o].push_back({ s.nextOut[o]++, std::string(h0 + 1, h1) });
      if (s.q[o].size() > simNetMaxQ) simNetMaxQ = (uint32_t)s.q[o].size();
      simNetMsgs++;
      fsNotify(s.p[o]);
    }
  }
  return !r.wait || fsReady(c, r.sid, r.got);
}

// an action (invite, answer, ...): its status and answer
static int fsAction(FsReq &r, FsCon &c, uint32_t now, std::string &resp) {
  const std::string &p = r.path;
  simNetLog.push_back(c.name + " " + p);
  c.seen = now; c.on = true;
  if (p.compare(0, 16, "/api/net/invite?") == 0) {
    FsCon *to = fsByCode(fsQ(p, "to"));
    if (!to) { resp = "no console with this code"; return 404; }
    const char *why = to == &c ? "self" : !fsOnline(*to, now) ? "offline" : fsBusy(*to) ? "busy" : fsBusy(c) ? "playing" : NULL;
    if (why) { resp = std::string("{\"why\":\"") + why + "\"}"; return 409; }
    for (auto &kv : fsInv) if (kv.second.from == c.code && kv.second.st == "wait") fsFinish(kv.second, "back", now);
    FsInv iv;
    char id[12];
    snprintf(id, sizeof(id), "i%04x", fsRand() & 0xFFFF);
    iv.id = id; iv.from = c.code; iv.to = to->code; iv.g = (uint8_t)atoi(fsQ(p, "g").c_str()); iv.t = now; iv.n = ++fsCount;
    fsInv[iv.id] = iv;
    fsNotify(to->code);
    resp = "{\"oid\":\"" + iv.id + "\"}";
    return 200;
  }
  if (p.compare(0, 16, "/api/net/cancel?") == 0) {
    auto it = fsInv.find(fsQ(p, "oid"));
    if (it == fsInv.end() || it->second.from != c.code) { resp = "no such challenge"; return 404; }
    if (it->second.st == "wait") fsFinish(it->second, "back", now);
    resp = "{\"ok\":1}";
    return 200;
  }
  if (p.compare(0, 16, "/api/net/answer?") == 0) {
    auto it = fsInv.find(fsQ(p, "iid"));
    if (it == fsInv.end() || it->second.to != c.code || it->second.st != "wait") { resp = "no such challenge"; return 404; }
    FsInv &iv = it->second;
    if (fsQ(p, "yes") != "1") { fsFinish(iv, "no", now); resp = "{\"ok\":1}"; return 200; }
    FsCon *f = fsByCode(iv.from);
    if (!f || fsBusy(*f) || !fsOnline(*f, now)) { fsFinish(iv, "gone", now); resp = "gone"; return 404; }
    for (const std::string *x : { &c.code, &iv.from })
      if (FsSes *old = fsSessionOf(*x)) { old->left[old->idx(*x)] = true; if (old->left[0] && old->left[1]) fsDrop(old->sid); }
    FsSes s;
    do s.sid = (uint16_t)(fsRand() % 65535 + 1); while (fsSes.count(s.sid));
    s.p[0] = iv.from; s.p[1] = c.code; s.g = iv.g; s.first = (int)(fsRand() & 1);
    s.rb = f->rb && c.rb;
    if (s.rb) {                                                 // net.py's lead_rb
      int lead = (int)ceil((0.25 * (f->rtt + c.rtt) + 20) / 20);
      s.lead = (uint8_t)(lead < 3 ? 3 : (lead > 6 ? 6 : lead));
    } else {
      double ms = 1.25 * (f->rtt + c.rtt) + 40;                 // net.py's lead_for
      int lead = (int)ceil(ms / 20);
      s.lead = (uint8_t)(lead < 6 ? 6 : (lead > 24 ? 24 : lead));
    }
    if (getenv("SIM_LEAD")) s.lead = (uint8_t)atoi(getenv("SIM_LEAD"));   // experiments
    s.seen[0] = s.seen[1] = now;
    simNetLead = s.lead;
    simNetSessions++;
    fsSes[s.sid] = s;
    fsOf[iv.from] = fsOf[c.code] = s.sid;
    iv.st = "ok"; iv.done = now;
    for (auto &kv : fsInv)
      if (&kv.second != &iv && kv.second.st == "wait" && (kv.second.to == c.code || kv.second.to == iv.from || kv.second.from == c.code || kv.second.from == iv.from))
        fsFinish(kv.second, (kv.second.to == c.code || kv.second.to == iv.from) ? "busy" : "back", now);
    fsNotify(iv.from); fsNotify(c.code);
    resp = fsAnswer(c, s.sid, 0, false, 0, now);
    return 200;
  }
  if (p.compare(0, 15, "/api/net/leave?") == 0) {
    uint16_t sid = (uint16_t)atoi(fsQ(p, "sid").c_str());
    auto it = fsSes.find(sid);
    if (it != fsSes.end() && it->second.idx(c.code) >= 0) {
      FsSes &s = it->second;
      int i = s.idx(c.code), o = 1 - i;
      s.left[i] = true; s.q[i].clear();
      if (s.end[o].empty()) s.end[o] = "left";
      if (s.end[i].empty()) s.end[i] = "left";
      if (!s.overT) s.overT = now;
      if (fsOf.count(c.code) && fsOf[c.code] == sid) fsOf.erase(c.code);
      fsNotify(s.p[o]);
      if (s.left[0] && s.left[1]) fsDrop(sid);
    }
    resp = "{\"ok\":1}";
    return 200;
  }
  if (p.compare(0, 16, "/api/net/friend?") == 0) {
    FsCon *o = fsByCode(fsQ(p, "c"));
    if (!o) { resp = "no console with this code"; return 404; }
    if (o == &c) { resp = "{\"why\":\"self\"}"; return 409; }
    bool on = fsQ(p, "on") == "1";
    for (FsCon *x : { &c, o }) {
      const std::string &y = x == &c ? o->code : c.code;
      auto f = std::find(x->fr.begin(), x->fr.end(), y);
      if (on && f == x->fr.end()) x->fr.push_back(y);
      if (!on && f != x->fr.end()) x->fr.erase(f);
    }
    fsNotify(o->code);
    char b[96];
    snprintf(b, sizeof(b), "{\"ok\":1,\"c\":\"%s\",\"n\":\"%s\",\"o\":%d}", o->code.c_str(), o->name.c_str(), fsOnline(*o, now) ? 1 : 0);
    resp = b;
    return 200;
  }
  resp = "?";
  return 404;
}

static void fsSubmit(int who, const char *tok, const std::string &path, const std::string &body, uint32_t now) {
  FsReq &r = fsReq[who];
  r = FsReq();
  r.active = true; r.tok = tok; r.path = path; r.body = body;
  if (simNetDown) { r.processed = true; r.status = 0; r.respAt = now + 1500; return; }   // TLS never gets through
  r.arrive = now + fsLat();
}

static void fsPump(uint32_t now) {
  static uint32_t nextSweep = 0;
  if (now >= nextSweep) { nextSweep = now + 500; fsSweep(now); }
  for (int w = 0; w < 2; w++) {
    FsReq &r = fsReq[w];
    if (!r.active || r.done) continue;
    if (simNetDown && !r.respAt) { r.processed = true; r.status = 0; r.respAt = now + 1500; }
    if (!r.processed && now >= r.arrive) {
      r.processed = true;
      FsCon *c = fsByTok(r.tok);
      if (r.path.compare(0, 15, "/api/net/hello?") == 0) {   // no token needed: our console gets one (NET_OPEN)
        simNetHellos++;
        simNetHelloPath = r.path;
        simNetLog.push_back("hello " + r.path);
        if (!simNetOpen) { r.status = 403; r.resp = "closed - pair with the PIN"; }
        else {
          FsCon &me = fsCon["MY3CDE"];
          me.tok = SIM_NET_TOK;
          std::string n = fsQ(r.path, "name");
          if (!n.empty()) me.name = n;
          r.status = 200; r.resp = std::string("{\"token\":\"") + SIM_NET_TOK + "\",\"id\":\"c9\",\"me\":\"MY3CDE\"}";
        }
        r.respAt = now + fsLat();
      } else if (!c) { r.status = 401; r.resp = "not paired"; r.respAt = now + fsLat(); }
      else if (r.path == "/api/net/poll") {
        simNetPolls[w]++;
        if (fsPoll(r, *c, now)) { r.status = 200; r.resp = fsAnswer(*c, r.sid, r.got, r.lists, 0, now); r.respAt = now + fsLat(); }
        else r.holdUntil = now + (r.wait > 25000 ? 25000 : r.wait);
      } else { r.status = fsAction(r, *c, now, r.resp); r.respAt = now + fsLat(); }
    }
    if (r.processed && !r.respAt) {                      // a held poll: something new, or its time is up
      FsCon *c = fsByTok(r.tok);
      if (c && (fsReady(*c, r.sid, r.got) || now >= r.holdUntil)) {
        uint32_t held = now - r.arrive;
        simNetHeld += held;
        c->seen = now;
        if (FsSes *s = fsSessionOf(c->code)) s->seen[s->idx(c->code)] = now;
        r.status = 200; r.resp = fsAnswer(*c, r.sid, r.got, r.lists, held, now); r.respAt = now + fsLat();
      }
    }
    if (r.respAt && now >= r.respAt) r.done = true;
  }
}

// ---------------- ANNA, the bot console on the internet ----------------
static NetLink  annaNet;
static bool     annaOn = false, annaReq = false;
static uint32_t annaAnswerAt = 0, annaInviteAt = 0;
static std::vector<int> annaResults;                    // how ANNA's games ended (simBotResult), in order
static uint8_t  annaInviteGame = 0;
static void annaStart(uint32_t now) {
  static const uint8_t MAC[6] = { 0x02, 'A', 'N', 'N', 'A', 1 };
  annaNet.begin(&simPeer, MAC, "ANNA", now, 1234);
  annaNet.lists = true;
  annaOn = true; annaReq = false;
}
static void annaStop() { annaOn = false; fsReq[1].active = false; }   // switched off: not a word more

static void annaTick(uint32_t now) {
  NetLink &N = annaNet;
  if (annaReq && fsReq[1].done) {
    N.onAnswer(fsReq[1].status, fsReq[1].resp.c_str(), fsReq[1].resp.size(), now);
    fsReq[1].active = false;
    annaReq = false;
  }
  if (!annaReq) {
    static char path[96], body[1400];
    if (N.next(path, sizeof(path), body, sizeof(body), now)) { fsSubmit(1, "TOK-ANNA", path, body, now); annaReq = true; }
  }
  N.tick(now);
  // a challenge for ANNA: an answer after a moment
  if (N.inv && !N.busy()) {
    if (!annaAnswerAt) annaAnswerAt = now + 700;
    if (now >= annaAnswerAt && simBotAnswer >= 0) { N.answer(simBotAnswer == 1); annaAnswerAt = 0; }
  } else if (!N.inv) annaAnswerAt = 0;
  // ANNA challenges us
  if (annaInviteAt && now >= annaInviteAt && !N.sid && !N.busy() && N.out == INO_NONE) {
    int16_t i = N.find(N.lob, N.nLob, "MY3CDE");
    if (i >= 0 && !N.lob[i].busy) { N.invite(N.lob[i], annaInviteGame, now); annaInviteAt = 0; }
  }
  if (N.out >= INO_OK) N.out = INO_NONE;
  if (simPeer.state == LK_OVER && simPeer.why != LKE_NONE) simPeer.toLobby();   // ended by the other one or the relay
  int before = simBotGames;
  simBotBrain(now);                                     // the games: link.h's bot on simPeer
  if (simBotGames != before) annaResults.push_back(simBotResult);
}

static void simInetTick(uint32_t t) {
  fsPump(t);
  if (annaOn) annaTick(t);
}
