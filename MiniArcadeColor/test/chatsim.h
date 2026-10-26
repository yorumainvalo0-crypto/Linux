// Host stand-in for the server's chat (MiniArcadeServer/tvserver/chat.py):
// the same rules - unique names (case does not count), one name per device,
// messages with ids, unread counts, blocks - in simulated time. Anna, Ben
// and Carla have names; Anna is online and answers every message after a
// moment, Ben writes once when the scenario says so (csBenAt).
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "../main/jsonlite.h"

struct CsMsg { uint32_t id; std::string frm, to, x; uint32_t t; };
static std::map<std::string, std::string> csName;      // console id -> chat name
static std::map<std::string, std::string> csDev;       // console id -> device id
static std::vector<CsMsg> csMsgs;
static std::map<std::pair<std::string, std::string>, uint32_t> csRead;   // (who, from whom) -> read up to
static std::set<std::pair<std::string, std::string>> csBlock;          // (who, whom)
static std::vector<CsMsg> csLater;                      // answers on their way (t = when)
static uint32_t csNextId = 1, csBenAt = 0;
static bool     csNobody = false, csSlow = false;      // nobody else has a name / sending too fast
static std::vector<std::string> simChatLog;             // every chat request: "path body"

static void csSetup() {
  csName = { { "anna", "Anna" }, { "ben", "Ben" }, { "carla", "Carla" } };
  csDev.clear(); csMsgs.clear(); csRead.clear(); csBlock.clear(); csLater.clear();
  csNextId = 1; csBenAt = 0; simChatLog.clear();
  csNobody = csSlow = false;
}
static std::string csLower(std::string s) { for (char &c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string csCidOf(const std::string &n) {
  for (auto &u : csName) if (csLower(u.second) == csLower(n)) return u.first;
  return "";
}
static std::string csEsc(const std::string &s) {
  std::string o;
  for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
  return o;
}
static bool csBlocked(const std::string &a, const std::string &b) { return csBlock.count({ a, b }) > 0; }
static uint32_t csUnread(const std::string &me, const std::string &from = "") {
  uint32_t n = 0;
  for (auto &m : csMsgs)
    if (m.to == me && (from.empty() || m.frm == from) && !csBlocked(me, m.frm) && m.id > csRead[{ me, m.frm }]) n++;
  return n;
}
static void csSend(const std::string &frm, const std::string &to, const std::string &x, uint32_t now) {
  csMsgs.push_back({ csNextId++, frm, to, x, now });
}
// the answers and Ben's message, when their time has come
static void csTick(uint32_t now) {
  for (size_t i = 0; i < csLater.size(); )
    if (now >= csLater[i].t) { csSend(csLater[i].frm, csLater[i].to, csLater[i].x, now); csLater.erase(csLater.begin() + i); }
    else i++;
  if (csBenAt && now >= csBenAt) { csBenAt = 0; csSend("ben", "me", "Grüße aus Köln!", now); }
}

// one request: the status, the answer in resp
static int simChat(const std::string &path, const std::string &body, std::string &resp, uint32_t now) {
  simChatLog.push_back(path + " " + body);
  csTick(now);
  const char *b = body.c_str(), *e = b + body.size();
  char n[40] = "", x[200] = "", dev[40] = "", q[40] = "";
  jsStr(b, e, "n", n, sizeof(n));
  jsStr(b, e, "x", x, sizeof(x));
  jsStr(b, e, "dev", dev, sizeof(dev));
  jsStr(b, e, "q", q, sizeof(q));
  const std::string me = "me";
  char t[96];
  if (path == "/api/chat/me") {
    std::string nm = csName.count(me) ? csName[me] : "";
    snprintf(t, sizeof(t), "{\"n\":\"%s\",\"u\":%u}", nm.c_str(), nm.empty() ? 0 : csUnread(me));
    resp = t;
    return 200;
  }
  if (path == "/api/chat/name") {
    std::string nn = n;
    bool ok = nn.size() >= 3 && nn.size() <= 12;
    for (char c : nn) ok = ok && (isalnum((unsigned char)c) || c == '-' || c == '_');
    if (!ok) { resp = "{\"why\":\"bad\"}"; return 409; }
    std::string owner = csCidOf(nn);
    if (!owner.empty() && owner != me) { resp = "{\"why\":\"taken\"}"; return 409; }
    csName[me] = nn;
    csDev[me] = dev;
    resp = "{\"n\":\"" + nn + "\"}";
    return 200;
  }
  if (!csName.count(me)) { resp = "noname"; return 409; }
  if (path == "/api/chat/list") {
    // the people we wrote with, the newest conversation first
    std::vector<std::pair<uint32_t, std::string>> last;
    for (auto &m : csMsgs) {
      std::string o = m.frm == me ? m.to : (m.to == me ? m.frm : "");
      if (o.empty()) continue;
      auto it = std::find_if(last.begin(), last.end(), [&](const std::pair<uint32_t, std::string> &p) { return p.second == o; });
      if (it != last.end()) it->first = m.id; else last.push_back({ m.id, o });
    }
    std::sort(last.rbegin(), last.rend());
    snprintf(t, sizeof(t), "{\"u\":%u,\"c\":[", csUnread(me));
    resp = t;
    int k = 0;
    for (size_t i = 0; i < last.size(); i++) {
      const std::string &o = last[i].second;
      if (!csName.count(o)) continue;                   // a name that went
      snprintf(t, sizeof(t), "%s{\"n\":\"%s\",\"u\":%u,\"o\":%d,\"b\":%d,\"m\":0}", k++ ? "," : "", csName[o].c_str(),
               csUnread(me, o), o == "anna" ? 1 : 0, csBlocked(me, o) ? 1 : 0);
      resp += t;
    }
    resp += "]}";
    return 200;
  }
  if (path == "/api/chat/people") {
    resp = "{\"p\":[";
    int k = 0;
    std::vector<std::pair<std::string, std::string>> all;
    for (auto &u : csName) if (u.first != me && csLower(u.second) >= csLower(q) && !csNobody) all.push_back({ csLower(u.second), u.first });
    std::sort(all.begin(), all.end());
    long off = jsInt(b, e, "off", 0);
    bool more = all.size() > (size_t)off + 20;
    all.erase(all.begin(), all.begin() + std::min((size_t)off, all.size()));
    if (all.size() > 20) all.resize(20);
    for (auto &a : all) {
      snprintf(t, sizeof(t), "%s{\"n\":\"%s\",\"o\":%d,\"b\":%d}", k++ ? "," : "", csName[a.second].c_str(),
               a.second == "anna" ? 1 : 0, csBlocked(me, a.second) ? 1 : 0);
      resp += t;
    }
    resp += more ? "],\"more\":1}" : "],\"more\":0}";
    return 200;
  }
  std::string o = csCidOf(n);
  if (o.empty() || o == me) { resp = "no such name"; return 404; }
  if (path == "/api/chat/msgs") {
    long after = jsInt(b, e, "after", 0), before = jsInt(b, e, "before", 0);
    std::vector<const CsMsg *> sel;
    for (auto &m : csMsgs)
      if (((m.frm == me && m.to == o) || (m.frm == o && m.to == me)) && (!after || m.id > (uint32_t)after) &&
          (!before || m.id < (uint32_t)before)) sel.push_back(&m);
    bool more = false;
    if (!after && sel.size() > 20) { sel.erase(sel.begin(), sel.end() - 20); more = true; }
    snprintf(t, sizeof(t), "{\"n\":\"%s\",\"o\":%d,\"b\":%d,\"more\":%d,\"m\":[", csName[o].c_str(), o == "anna" ? 1 : 0,
             csBlocked(me, o) ? 1 : 0, more ? 1 : 0);
    resp = t;
    for (size_t i = 0; i < sel.size(); i++) {
      snprintf(t, sizeof(t), "%s{\"i\":%u,\"m\":%d,\"a\":%u,\"x\":\"", i ? "," : "", sel[i]->id, sel[i]->frm == me ? 1 : 0,
               (now - sel[i]->t) / 60000);
      resp += t + csEsc(sel[i]->x) + "\"}";
      if (sel[i]->to == me) csRead[{ me, o }] = std::max(csRead[{ me, o }], sel[i]->id);
    }
    resp += "]}";
    return 200;
  }
  if (path == "/api/chat/send") {
    if (csSlow) { resp = "slow down"; return 429; }
    if (!x[0]) { resp = "{\"why\":\"empty\"}"; return 409; }
    if (csBlocked(me, o) || csBlocked(o, me)) { resp = "{\"why\":\"blocked\"}"; return 409; }
    csSend(me, o, x, now);
    if (o == "anna") csLater.push_back({ 0, "anna", me, std::string("Hallo ") + csName[me] + "! Wie geht's?", now + 1500 });
    snprintf(t, sizeof(t), "{\"i\":%u}", csNextId - 1);
    resp = t;
    return 200;
  }
  if (path == "/api/chat/block") {
    bool on = jsInt(b, e, "on", 1) != 0;
    if (on) csBlock.insert({ me, o }); else csBlock.erase({ me, o });
    snprintf(t, sizeof(t), "{\"ok\":1,\"b\":%d}", on ? 1 : 0);
    resp = t;
    return 200;
  }
  resp = "?";
  return 404;
}
