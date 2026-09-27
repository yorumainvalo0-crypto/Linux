// Sokoban solver for the PC tests (and the level generator): breadth first
// over the box positions, the player only counts by the area it can reach.
// Returns the least number of pushes, -1 if there is no solution within
// the state limit.
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <deque>
#include <algorithm>

struct SkLevel {
  int w = 0, h = 0, px = 0, py = 0;
  std::vector<uint8_t> wall, goal;          // w * h
  std::vector<int> boxes;                   // cell indices
};

static SkLevel skParse(const char *s) {
  SkLevel L;
  std::vector<std::string> rows(1);
  for (; *s; s++) { if (*s == '|') rows.emplace_back(); else rows.back() += *s; }
  L.h = (int)rows.size();
  for (auto &r : rows) L.w = std::max(L.w, (int)r.size());
  L.wall.assign(L.w * L.h, 1); L.goal.assign(L.w * L.h, 0);
  for (int y = 0; y < L.h; y++)
    for (int x = 0; x < (int)rows[y].size(); x++) {
      char c = rows[y][x]; int i = y * L.w + x;
      L.wall[i] = c == '#';
      if (c == '.' || c == '*' || c == '+') L.goal[i] = 1;
      if (c == '$' || c == '*') L.boxes.push_back(i);
      if (c == '@' || c == '+') { L.px = x; L.py = y; }
    }
  return L;
}

// area the player can reach; returns its smallest cell (the state key)
static int skReach(const SkLevel &L, int from, const std::vector<uint8_t> &box, std::vector<uint8_t> &seen) {
  seen.assign(L.w * L.h, 0);
  std::vector<int> st{ from }; seen[from] = 1; int lo = from;
  const int d[4] = { -L.w, L.w, -1, 1 };
  while (!st.empty()) {
    int c = st.back(); st.pop_back();
    lo = std::min(lo, c);
    for (int k = 0; k < 4; k++) {
      int n = c + d[k];
      if (n < 0 || n >= L.w * L.h || L.wall[n] || box[n] || seen[n]) continue;
      seen[n] = 1; st.push_back(n);
    }
  }
  return lo;
}

static int skSolve(const SkLevel &L, size_t limit = 2000000) {
  int N = L.w * L.h;
  const int d[4] = { -L.w, L.w, -1, 1 };
  std::vector<uint8_t> dead(N, 0);                 // corners that are not goals
  for (int i = 0; i < N; i++) {
    if (L.wall[i] || L.goal[i]) continue;
    bool v = (i - L.w < 0 || L.wall[i - L.w]) || (i + L.w >= N || L.wall[i + L.w]);
    bool h = L.wall[i - 1] || L.wall[i + 1];
    dead[i] = v && h;
  }
  auto key = [&](int p, std::vector<int> b) {
    std::sort(b.begin(), b.end());
    std::string k((char *)&p, sizeof(p));
    for (int x : b) k.append((char *)&x, sizeof(x));
    return k;
  };
  struct S { int p; std::vector<int> b; int d; };
  std::unordered_set<std::string> seen;
  std::deque<S> q;
  std::vector<uint8_t> box(N, 0), area;
  for (int b : L.boxes) box[b] = 1;
  int p0 = skReach(L, L.py * L.w + L.px, box, area);
  seen.insert(key(p0, L.boxes));
  q.push_back({ L.py * L.w + L.px, L.boxes, 0 });
  while (!q.empty()) {
    S s = q.front(); q.pop_front();
    bool done = true;
    for (int b : s.b) if (!L.goal[b]) done = false;
    if (done) return s.d;
    if (seen.size() > limit) return -1;
    std::fill(box.begin(), box.end(), 0);
    for (int b : s.b) box[b] = 1;
    skReach(L, s.p, box, area);
    std::vector<uint8_t> reach = area;
    for (size_t j = 0; j < s.b.size(); j++)
      for (int k = 0; k < 4; k++) {
        int b = s.b[j], from = b - d[k], to = b + d[k];
        if (from < 0 || to < 0 || from >= N || to >= N || !reach[from]) continue;
        if (L.wall[to] || box[to] || dead[to]) continue;
        std::vector<int> nb = s.b; nb[j] = to;
        box[b] = 0; box[to] = 1;
        int lo = skReach(L, b, box, area);
        box[to] = 0; box[b] = 1;
        std::string kk = key(lo, nb);
        if (!seen.insert(kk).second) continue;
        q.push_back({ b, nb, s.d + 1 });
      }
  }
  return -1;
}
