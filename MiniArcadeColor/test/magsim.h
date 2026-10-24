// MiniArcade Color host tests: the SD games (MAG script, main/mag.h and
// main/game_sdgames.h). Part of sim.cpp, which includes it after the
// SD level tests - not compiled on its own.
//   maglogic  the compiler and the VM without a screen: every documented
//             error at its line, the semantics, the limits, the budget,
//             runtime errors, the page cache, a changed file, the examples
//   magplay   the launcher; each example played through the keys by a bot:
//             the puzzle's levels and its progress on the card, the
//             platformer scrolling while its map streams from the card
//   magpull   the card pulled in the platformer: the game waits, goes on
//   magnone   no card: a harmless entry, everything else as before
//   magerr    broken games on the card: the error screens (and German)
#include <chrono>

struct MagRes { uint8_t r; uint16_t line; std::string msg; };
// src as <card>/MINIARC/games/<name>, loaded; mag stays when it loads (r == 0)
static MagRes magSrc(const std::string &src, const char *name = "t.mag") {
  std::string p = simSdDir + "/MINIARC/games/" + name;
  sdWrite(p, src);
  char msg[120] = "";
  uint16_t line = 0;
  uint8_t r = magLoad(p.c_str(), msg, sizeof(msg), &line);
  return { r, line, msg };
}
// a game's variable by its name (the compiler told magSyms where it is)
static int32_t magG(const char *n, int i = 0) {
  auto it = magSyms.find(n);
  return mag && it != magSyms.end() ? mag->g[it->second + i] : INT32_MIN;
}

// the level blocks of a .mag file as the host reads it: rows of each, in order
static std::vector<std::vector<std::string>> magLevels(const std::string &path) {
  std::vector<std::vector<std::string>> out;
  std::ifstream in(path);
  std::string l;
  int need = 0;
  while (std::getline(in, l)) {
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
    size_t s = l.find_first_not_of(" \t");
    if (s == std::string::npos) continue;
    l = l.substr(s);
    int w, h;
    if (!need && sscanf(l.c_str(), "level %d %d", &w, &h) == 2) { out.push_back({}); need = h; continue; }
    if (need && l[0] == '"') { out.back().push_back(l.substr(1, l.size() - 2)); need--; }
  }
  return out;
}

// ---------------- Ice Slide: its rules, solved breadth first ----------------
static const int ICE_DX[4] = { 0, 0, -1, 1 }, ICE_DY[4] = { -1, 1, 0, 0 };   // UP DOWN LEFT RIGHT
// the shortest list of slides (0..3) from x, y with the gems still on map g; empty = none
static std::vector<int> iceSolve(const std::vector<std::string> &g, int sx, int sy) {
  std::vector<std::pair<int, int>> gems;
  for (int y = 0; y < (int)g.size(); y++) for (int x = 0; x < (int)g[y].size(); x++) if (g[y][x] == '*') gems.push_back({ x, y });
  auto at = [&](int x, int y) { return y < 0 || y >= (int)g.size() || x < 0 || x >= (int)g[y].size() ? '#' : g[y][x]; };
  auto gi = [&](int x, int y) { for (size_t i = 0; i < gems.size(); i++) if (gems[i].first == x && gems[i].second == y) return (int)i; return -1; };
  typedef std::tuple<int, int, int> S;
  std::map<S, std::pair<S, int>> prev;
  std::deque<S> q;
  S s0 { sx, sy, (1 << gems.size()) - 1 };
  prev[s0] = { s0, -1 };
  q.push_back(s0);
  while (!q.empty()) {
    S s = q.front(); q.pop_front();
    for (int d = 0; d < 4; d++) {
      int x = std::get<0>(s), y = std::get<1>(s), m = std::get<2>(s);
      bool win = false;
      for (;;) {
        int nx = x + ICE_DX[d], ny = y + ICE_DY[d];
        char c = at(nx, ny);
        if (c == '#' || (c == 'E' && m)) break;
        x = nx; y = ny;
        if (c == '*') { int k = gi(x, y); if (k >= 0) m &= ~(1 << k); }
        if (c == 'E') { win = true; break; }
        if (c == ':') break;
      }
      if (x == std::get<0>(s) && y == std::get<1>(s)) continue;
      if (win) {
        std::vector<int> path { d };
        for (S t = s; prev[t].second >= 0; t = prev[t].first) path.insert(path.begin(), prev[t].second);
        return path;
      }
      S n { x, y, m };
      if (!prev.count(n)) { prev[n] = { s, d }; q.push_back(n); }
    }
  }
  return {};
}

// ---------------- maglogic ----------------
struct MagCase { const char *src; uint16_t line; const char *says; };
static const char *const MAG_HEAD_SRC = "game \"T\"\n";                    // line 1
static const char *const MAG_TAIL = "\nfunc draw()\nend\n";
static const MagCase MAG_ERRORS[] = {
  { "var a\n", 1, "a game starts with its name" },
  { "game \"T\"\nfunc draw()\nend\n", 3, "needs a func update()" },
  { "game \"T\"\nfunc update()\n  x = 1\nend", 3, "unknown variable 'x'" },
  { "game \"T\"\nfunc update()\n  local a = foo\nend", 3, "unknown name 'foo'" },
  { "game \"T\"\nfunc update()\n  foo()\nend", 3, "unknown function 'foo'" },
  { "game \"T\"\nfunc update()\n  if 1 then\n    sfx(1, 1)\nfunc g()\nend", 3, "'if' on line 3 has no 'end'" },
  { "game \"T\"\nend\nfunc update()\nend", 2, "'end' without a func" },
  { "game \"T\"\nvar a\nfunc update()\n  a += 1\nend", 4, "'+=' is not supported" },
  { "game \"T\"\nvar a\nfunc update()\n  if a = 1 then a = 2 end\nend", 4, "use == to compare" },
  { "game \"T\"\nvar a\nfunc update()\n  if a && 1 then a = 2 end\nend", 4, "use 'and' instead of '&&'" },
  { "game \"T\"\nvar a\nfunc update()\n  if a then\n  else if a then\n  end\nend", 5, "write 'elif' instead of 'else if'" },
  { "game \"T\"\nvar a = 1.5\n", 2, "only whole numbers" },
  { "game \"T\"\nfunc update()\n  local s = \"hi\"\nend", 3, "a text in quotes can only be the text" },
  { "game \"T\"\nfunc update()\n  text(0, 0, 5, WHITE)\nend", 3, "argument 3 of text() must be a text in double quotes" },
  { "game \"T\"\nfunc update()\n  rect(1, 2, 3)\nend", 3, "rect() needs 5 arguments, got 3" },
  { "game \"T\"\nfunc f(a)\nend\nfunc update()\n  f(1, 2)\nend", 5, "f() takes 1 argument" },
  { "game \"T\"\nfunc update()\n  f(1, 2)\nend\nfunc f(a)\nend", 5, "f() has 1 parameter, but it is called with 2 on line 3" },
  { "game \"T\"\nfunc update()\n  local a = 5 / 0\nend", 3, "division by zero" },
  { "game \"T\"\narray arr[4]\nfunc update()\n  local a = arr\nend", 4, "'arr' is an array: use arr[index]" },
  { "game \"T\"\nconst K = 3\nfunc update()\n  K = 4\nend", 4, "'K' is a const and cannot be assigned" },
  { "game \"T\"\nvar end\n", 2, "'end' is a keyword" },
  { "game \"T\"\nvar rnd\n", 2, "'rnd' is a built-in function" },
  { "game \"T\"\nvar RED\n", 2, "'RED' is a built-in constant" },
  { "game \"T\"\nvar a\nvar a\n", 3, "'a' is already defined on line 2" },
  { "game \"T\"\nsprite s 4 2\n\"kkkk\"\n\"kkk\"\n", 4, "row 2 of sprite s has 3 characters, but the sprite is 4 wide" },
  { "game \"T\"\nsprite s 2 1\n\"kq\"\n", 3, "'q' is not a palette letter" },
  { "game \"T\"\nsprite s 2 3\n\"kk\"\n\"kk\"\nfunc update()\nend", 2, "sprite s has 2 rows, but 3 are needed" },
  { "game \"T\"\nmap m 2 1\n\"ab\"\nfunc update()\n  spr(m, 0, 0)\nend", 5, "argument 1 of spr() must be a sprite, but 'm' is a map" },
  { "game \"T\"\nfunc update()\n  spr(m, 0, 0)\nend\nmap m 2 1\n\"ab\"\n", 3, "'m' is a map, but a sprite is needed here" },
  { "game \"T\"\nfunc update()\n  text(0, 0, \"open, 1)\nend", 3, "text not closed" },
  { "game \"T\"\nvar a\nfunc update()\n  if 1 < a < 3 then a = 1 end\nend", 4, "comparisons cannot be chained" },
  { "game \"T\"\nfunc update()\n  break\nend", 3, "'break' outside a while or for loop" },
  { "game \"T\"\nvar a\nfunc update()\n  local a = 1\nend", 4, "local 'a' would hide a variable" },
  { "game \"T\"\nfunction update()\nend", 2, "use 'func' to define a function" },
  { "game \"T\"\nx = 1\n", 2, "only declarations at the top" },
  { "game \"T\"\nfunc update()\n  y = 1\nend\nvar y\n", 3, "unknown variable 'y'" },
  { "game \"T\"\nfunc update()\n  local a = y\nend\nvar y\n", 5, "'y' is used on line 3 before it is declared here" },
  { "game \"T\"\nfps 45\n", 2, "fps is 30 or 60" },
  { "game \"T\"\ntiles t 16\n  \"#\" = s\nend\nsprite s 3 3\n\"kkk\"\n\"kkk\"\n\"kkk\"\nfunc update()\nend", 2, "tile '#': its sprite is 3x3" },
  { "game \"T\"\narray big[3000]\n", 2, "an array has 1..2048 elements" },
  { "game \"T\"\narray a[2000]\narray b[100]\n", 3, "too many variables" },
  { "game \"T\"\nlocal a = 1\n", 2, "'local' only inside a function" },
  { "game \"T\"\nvar a\nauthor \"me\"\n", 3, "'author' belongs to the header" },
  { "game \"T\"\nfunc update()\n  local a = 3 @ 4\nend", 3, "unexpected character '@'" },
  { "game \"T\"\nvar p\nfunc update()\n  p.x = 1\nend", 4, "'.' is not supported" },
  { "game \"T\"\nfunc update()\n  local a = 'ab'\nend", 3, "one character in single quotes" },
  { "game \"T\"\nfunc update()\n  for i = 1, 5, 0 do end\nend", 3, "the step of a for loop cannot be 0" },
  { "game \"T\"\nfunc f(a, b, c, d, e, f2, g, h, i)\nend", 2, "too many parameters" },
  { "game \"T\"\nfunc init(a)\nend\nfunc update()\nend\nfunc draw()\nend", 2, "init() has no parameters" },
  { "game \"T\"\nfunc update()\n  print(\"x\")\nend", 3, "unknown function 'print' - draw text with text" },
  { "game \"T\"\nmap m 3 2\n\"abc\"\n\"ab\"\n", 4, "row 2 of map m has 2 characters, but the map is 3 wide" },
  { "game \"T\"\nfunc update()\n  rect(1, 2, 3, 4, 5\nend", 4, "expected ',' or ')'" },
  { "game \"T\"\nfunc update()\n  local a = 1\n  a\nend", 4, "'a' alone does nothing" },
  { "game \"T\"\nfunc update()\n  return 1 +\nend", 4, "expected a value, found the keyword 'end'" },
  { "game \"T\"\nvar a = b\n", 2, "a var's first value must be a constant" },
  { "game \"T\"\nfunc update()\n  local a = 99999999999\nend", 3, "number too big" },
  { "game \"T\"\nfunc update()\n  local t = {}\nend", 3, "{ } are not supported" },
  { "game \"T\"\nfunc update()\n  while true\n    sfx(1, 1)\nend", 2, "'func' on line 2 has no 'end' (a new func starts on line 6)" },
  { "game \"T\"\nfunc update()\n  mget(1, 2)\nend", 3, "mget() needs 3 arguments, got 2" },
  { "game \"T\"\nfunc update()\n  btn(UP, 1)\nend", 3, "btn() takes 1 argument" },
  { "game \"T\"\nsprite a 99 1\n", 2, "a sprite is 1..64 wide and 1..64 high" },
  { "game \"T\"\nfunc update()\nend\nfunc update()\nend", 4, "'update' is already defined on line 2" },
  { "game \"T\"\nfunc update()\n  local a = 1\n  local a = 2\nend", 4, "'a' is already a local or parameter here" },
  { "game \"T\"\nfunc update()\n  local a = levels\nend", 3, "'levels' is a built-in function: call it as levels(...)" },
};

static void magProbeStart() { volatile char here; magProbeTop = magProbeLow = (long)&here; }

static void magLogic() {
  sdCard("frames/maglogic", true);
  simSdDir = "frames/maglogic"; simSd = simSdIn = true;
  simNvsU16["sdcs"] = 21;                            // the card's pin, as the console found it once
  const std::string G = "frames/maglogic/MINIARC/games/";
  // every documented compile error: at its line, with its reason
  int bad = 0;
  for (const MagCase &k : MAG_ERRORS) {
    std::string src = k.src;
    if (src.find("func draw()") == std::string::npos && src.find("game") == 0) src += MAG_TAIL;
    MagRes r = magSrc(src);
    if (!r.r) magEnd();
    if (r.r != 2 || r.line != k.line || r.msg.find(k.says) == std::string::npos) {
      bad++;
      printf("      wanted line %u '%s', got %u: line %u: %s\n", k.line, k.says, r.r, r.line, r.msg.c_str());
    }
  }
  printf("      %zu compile errors checked\n", sizeof(MAG_ERRORS) / sizeof(MAG_ERRORS[0]));
  check("maglogic: every documented compile error is found, at its line, with its reason", !bad);
  // the language: values, operators, loops, functions
  const char *VM =
    "game \"VM\"\n"
    "array r[64]\n"
    "const K = 7 * 6\n"
    "var g = 5, h, neg = -3\n"
    "func fib(n)\n  if n < 2 then return n end\n  return fib(n - 1) + fib(n - 2)\nend\n"
    "func fact(n)\n  local p = 1\n  for i = 2, n do p = p * i end\n  return p\nend\n"
    "func noret()\nend\n"
    "func update()\n"
    "  r[0] = 2 + 3 * 4\n  r[1] = (2 + 3) * 4\n  r[2] = 7 / 2\n  r[3] = -7 / 2\n  r[4] = -7 % 3\n"
    "  r[5] = 1 << 4 | 1\n  r[6] = 6 & 3 ^ 1\n  r[7] = 5 > 3 and 2 < 1\n  r[8] = 0 or 7\n  r[9] = not 0\n"
    "  r[10] = K\n  r[11] = fib(15)\n  r[12] = fact(10)\n  r[13] = g + neg\n"
    "  local s = 0\n  for i = 10, 1, -3 do s = s + i end\n  r[14] = s\n"
    "  s = 0\n  local i = 0\n  while true do\n    i = i + 1\n    if i % 2 == 0 then continue end\n    if i > 9 then break end\n    s = s + i\n  end\n  r[15] = s\n"
    "  r[16] = 'A'\n  r[17] = min(3, -2) + max(4, 9) + abs(-5) + clamp(15, 0, 10)\n"
    "  r[18] = sin(90) + cos(180) + sin(30)\n  r[19] = sqrt(1000000)\n  r[20] = atan2(10, 10)\n  r[21] = col(255, 0, 0)\n"
    "  r[22] = 2147483647 + 1\n  r[23] = noret()\n  r[24] = -2147483647 - 1\n"
    "  local mn = r[24]\n  local m1 = -1\n  r[25] = mn / m1\n  r[26] = 0x1F + 0b101\n  r[27] = 3 == 3\n  r[28] = -(-5)\n"
    "  r[29] = -1 >> 1\n  r[30] = 1 - 2 - 3\n  r[31] = 100 / 10 / 5\n"
    "  if g == 5 then r[32] = 1 elif g == 6 then r[32] = 2 else r[32] = 3 end\n"
    "  r[33] = levels()\n"
    "  local tot = 0\n  for a = 1, 3 do\n    for b = 1, 3 do\n      if b == 2 then break end\n      tot = tot + a * 10 + b\n    end\n  end\n  r[34] = tot\n"
    "  local x = 1\n  if true then\n    local x2 = 5\n    x = x + x2\n  end\n  r[35] = x\n"
    "  h = h + 1\n  r[36] = h\n  r[37] = 1 + 2 * 3 - 4 / 2 % 3\n"
    "  r[38] = (1 < 2) + (2 <= 2) + (3 > 4) + (4 >= 4) + (1 != 1)\n  r[39] = 7 and 0\n"
    "  local cnt = 0\n  for k = 1, 0 do cnt = cnt + 1 end\n  r[40] = cnt\n"
    "  for k = 2147483646, 2147483647 do cnt = cnt + 1 end\n  r[41] = cnt\n"
    "  r[42] = fx(3); r[43] = g * -2\n"
    "  local v = 0\n  for k = 1, 3 do\n    local w = k\n    v = v + w\n  end\n  r[44] = v\n"
    "  r[45] = rnd(1) + rnd(0) + rnd(-5)\n  r[46] = (5 - 3) * -(2 + 1)\n  r[47] = 1 or fx(1000000)\n"
    "end\n"
    "func fx(a) return a * a end\n"
    "func draw()\nend\n";
  const int32_t WANT[48] = { 14, 20, 3, -3, -1, 17, 3, 0, 1, 1, 42, 610, 3628800, 2, 22, 25, 65, 22, 512, 1000, 45, rgb(255, 0, 0),
                             INT32_MIN, 0, INT32_MIN, INT32_MIN, 36, 1, 5, -1, -4, 2, 1, 0, 63, 6, 1, 5, 3, 0, 0, 2, 9, -10, 6, 0, -6, 1 };
  MagRes r = magSrc(VM);
  bool vmOk = !r.r;
  if (vmOk) {
    vmOk = magStart();
    mag->budget = MAG_BUDGET;
    vmOk = vmOk && magRun(mag->fUpdate, "update");
    for (int i = 0; i < 48 && vmOk; i++) if (mag->g[i] != WANT[i]) { printf("      r[%d] = %d, wanted %d\n", i, mag->g[i], WANT[i]); vmOk = false; }
    if (mag->failed) printf("      %s line %u\n", mag->err, magLineAt(mag->errPc));
    magEnd();
  } else printf("      %s line %u\n", r.msg.c_str(), r.line);
  check("maglogic: operators, precedence, folding, loops, break/continue, functions, recursion, locals, built-ins compute right", vmOk);
  // speed: a tight loop, instructions per second on this PC
  r = magSrc("game \"S\"\nvar s\nfunc update()\n  for i = 1, 30000 do s = s + i * 2 end\nend\nfunc draw()\nend\n");
  double nsOp = 0;
  long loopOps = 0;
  if (!r.r) {
    magStart();
    mag->budget = MAG_BUDGET;
    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 20; k++) { mag->budget = MAG_BUDGET; magRun(mag->fUpdate, "update"); loopOps += MAG_BUDGET - mag->budget; }
    auto t1 = std::chrono::steady_clock::now();
    nsOp = std::chrono::duration<double, std::nano>(t1 - t0).count() / loopOps;
    magEnd();
  }
  printf("      speed: %.1f ns per instruction on this PC (%ld instructions); a frame of %d at most\n", nsOp, loopOps, MAG_BUDGET);
  // the budget: an endless loop in update, draw and init stops with its line
  struct { const char *src; uint16_t line; const char *says; } LOOPS[] = {
    { "game \"L\"\nfunc update()\n  while 1 do end\nend\nfunc draw()\nend\n", 3, "endless loop in update()" },
    { "game \"L\"\nvar a\nfunc update()\nend\nfunc draw()\n  while true do\n    a = a + 1\n  end\nend\n", 7, "endless loop in draw()" },
    { "game \"L\"\nfunc init()\n  local i = 0\n  while i >= 0 do i = 1 end\nend\nfunc update()\nend\nfunc draw()\nend\n", 4, "endless loop in init()" },
    { "game \"L\"\nfunc f()\n  return f()\nend\nfunc update()\n  f()\nend\nfunc draw()\nend\n", 3, "too deep" },
    { "game \"L\"\narray a[10]\nfunc update()\n  for i = 0, 10 do\n    a[i] = i\n  end\nend\nfunc draw()\nend\n", 5, "array index 10 is outside 0..9" },
    { "game \"L\"\narray a[10]\nfunc update()\n  local k = -1\n  local v = a[k]\nend\nfunc draw()\nend\n", 5, "array index -1 is outside 0..9" },
    { "game \"L\"\nfunc update()\n  local z = 0\n  local q = 5 / z\nend\nfunc draw()\nend\n", 4, "division by zero" },
    { "game \"L\"\nfunc update()\n  local z = 0\n  local q = 5 % z\nend\nfunc draw()\nend\n", 4, "division by zero" },
    { "game \"L\"\nfunc update()\n  loadmap(99)\nend\nfunc draw()\nend\n", 3, "99 is not a map" },
    { "game \"L\"\nfunc update()\n  mset(m, 0, 0, 'a')\nend\nfunc draw()\nend\nmap m 2 1\n\"ab\"\n", 3, "this map is not loaded" },
    { "game \"L\"\nfunc update()\n  loadmap(m)\n  mset(m, 5, 0, 'a')\nend\nfunc draw()\nend\nmap m 2 1\n\"ab\"\n", 4, "outside the map" },
    { "game \"L\"\nfunc update()\n  local a = level(0)\nend\nfunc draw()\nend\n", 3, "level(0): there are 0 levels" },
    { "game \"L\"\nfunc update()\nend\nfunc draw()\n  cls(300)\nend\n", 5, "colour 300 is not 0..255" },
    { "game \"L\"\nfunc update()\nend\nfunc draw()\n  rect(0, 0, 9, 9, 16711680)\nend\n", 5, "use a colour name or col(r, g, b)" },
    { "game \"L\"\nfunc update()\n  local k = 7\n  local a = btn(k)\nend\nfunc draw()\nend\n", 4, "btn(7): the keys are" },
    { "game \"L\"\nfunc update()\n  local s = 0\n  for i = 1, 5, s do end\nend\nfunc draw()\nend\n", 4, "the step of a for loop is 0" },
    { "game \"L\"\nfunc update()\n  save(16, 1)\nend\nfunc draw()\nend\n", 3, "the slots are 0..15" },
    { "game \"L\"\nfunc update()\nend\nfunc draw()\n  spr(99, 0, 0)\nend\n", 5, "99 is not a sprite" },
    { "game \"L\"\nfunc g(n)\n  local a = 1\n  local b = 2\n  return g(n + a + b)\nend\nfunc update()\n  g(1)\nend\nfunc draw()\nend\n", 5, "too deep" },
  };
  bad = 0;
  for (auto &k : LOOPS) {
    r = magSrc(k.src);
    if (r.r) { bad++; printf("      did not load: %s\n", r.msg.c_str()); continue; }
    bool ok = magStart();
    if (ok) { mag->budget = mag->budgetMax = MAG_BUDGET; ok = magRun(mag->fUpdate, "update") && magRun(mag->fDraw, "draw"); }
    uint16_t l = magLineAt(mag->errPc);
    if (ok || !mag->failed || l != k.line || !strstr(mag->err, k.says)) { bad++; printf("      wanted line %u '%s', got %d: line %u %s\n", k.line, k.says, ok, l, mag->err); }
    magEnd();
  }
  check("maglogic: the instruction budget stops while 1 (update, draw, init) at the loop's line", bad == 0);
  // too big and too much memory
  std::string big = "game \"B\"\nvar a\nfunc update()\n";
  for (int i = 0; i < 3000; i++) big += "  a = a + 12345 * a\n";
  big += "end\nfunc draw()\nend\n";
  MagRes rb = magSrc(big);
  if (!rb.r) magEnd();
  printf("      too big: line %u: %s\n", rb.line, rb.msg.c_str());
  std::string longl = "game \"B\"\nvar a\nfunc update()\n  a = 1" + std::string(600, ' ') + "\nend\nfunc draw()\nend\n";
  MagRes rl = magSrc(longl);
  if (!rl.r) magEnd();
  simMagHeap = 30000;
  MagRes rm = magSrc(VM);
  if (!rm.r) magEnd();
  simMagHeap = 200000;
  printf("      too long: %u line %u: %s; no memory: %u line %u: %s\n", rl.r, rl.line, rl.msg.c_str(), rm.r, rm.line, rm.msg.c_str());
  check("maglogic: too big a game, too long a line, too little memory: refused with a message, nothing crashes",
        rb.r == 2 && rb.msg.find("too big") != std::string::npos && rb.line > 1000 && rl.r == 2 && rl.line == 4 && rl.msg.find("too long") != std::string::npos &&
        rm.r == 1 && rm.msg.find("not enough memory") != std::string::npos && !mag);
  // runtime errors count as errors in their own function, and the stack never grows into the heap
  // data blocks: sprites decoded, maps read through the page cache, rows unevenly indented
  const char *DATA =
    "game \"D\"\narray r[16]\n"
    "func update()\n"
    "  r[0] = mget(u, 0, 0)\n  r[1] = mget(u, 2, 1)\n  r[2] = mget(u, 3, 2)\n  r[3] = mget(u, 4, 0)\n  r[4] = mget(u, -1, 0)\n"
    "  r[5] = mapw(u)\n  r[6] = maph(u)\n  r[7] = sprw(s)\n  r[8] = sprh(s)\n"
    "  loadmap(u)\n  mset(u, 1, 1, 'Z')\n  r[9] = mget(u, 1, 1)\n  r[10] = mapw()\n  r[11] = levels()\n  r[12] = mget(level(2), 1, 0)\n"
    "  loadmap(u)\n  r[13] = mget(u, 1, 1)\n"
    "end\n"
    "func draw()\n  cls(BLACK)\n  spr(s, 10, 10)\n  spr(s, 20, 10, 2, 1)\n  drawmap(u, t, 100, 100)\nend\n"
    "sprite s 3 2\n\"kwr\"\n\".b.\"\n"
    "map u 4 3\n\"abcd\"\n    \"efgh\"\n  \"ijkl\"\n"
    "level 2 1\n\"xy\"\n"
    "level 2 1\n\"pq\"\n"
    "tiles t 4\n  \"a\" = RED\n  \"f\" = s2\nend\n"
    "sprite s2 2 2\n\"gg\"\n\"gg\"\n";
  r = magSrc(DATA);
  bool dOk = !r.r;
  if (dOk) {
    magStart();
    mag->budget = MAG_BUDGET;
    gfx.clear();
    dOk = magRun(mag->fUpdate, "update") && magRun(mag->fDraw, "draw");
    const int32_t W[14] = { 'a', 'g', 'l', 0, 0, 4, 3, 3, 2, 'Z', 4, 2, 'q', 'f' };
    for (int i = 0; i < 14 && dOk; i++) if (mag->g[i] != W[i]) { printf("      data r[%d] = %d, wanted %d\n", i, mag->g[i], W[i]); dOk = false; }
    const uint8_t *fb = gfx.buffer();
    auto px = [&](int x, int y) { return fb[y * SCR_W + x]; };
    bool spr1 = px(10, 10) == C::BLACK && px(11, 10) == C::WHITE && px(12, 10) == C::RED && px(11, 11) == C::BLUE && px(10, 11) == C::BLACK;
    bool spr2 = px(20, 10) == C::RED && px(21, 11) == C::RED && px(24, 10) == C::BLACK && px(25, 11) == C::BLACK && px(22, 12) == C::BLUE;   // mirrored, twice as big
    bool map1 = px(100, 100) == C::RED && px(103, 103) == C::RED && px(104, 100) == C::BLACK && px(104, 104) == C::GREEN && px(107, 107) == C::GREEN;
    printf("      uneven rows: stride %u; sprites %d %d, map %d\n", mag->map[0].stride, spr1, spr2, map1);
    dOk = dOk && mag->map[0].stride == 0 && mag->map[1].stride != 0 && spr1 && spr2 && map1;
    if (mag->failed) printf("      %s line %u\n", mag->err, magLineAt(mag->errPc));
    magEnd();
  } else printf("      %s line %u\n", r.msg.c_str(), r.line);
  check("maglogic: sprites drawn (scaled, mirrored), maps read from the card, uneven rows, mset on the loaded copy, levels", dOk);
  // the page cache: what it reads is the file, byte for byte
  std::string qp = G + "quest.mag";
  r = { 0, 0, "" };
  char msg[120]; uint16_t line;
  bool cOk = !magLoad(qp.c_str(), msg, sizeof(msg), &line);
  if (cOk) {
    std::string file = sdRead(qp);
    srand(7);
    uint8_t b[300];
    for (int k = 0; k < 3000 && cOk; k++) {
      uint32_t off = (uint32_t)rand() % (uint32_t)file.size(), n = 1 + (uint32_t)rand() % 299;
      if (off + n > file.size()) n = (uint32_t)file.size() - off;
      if (!magRead(off, b, (uint16_t)n) || memcmp(b, file.data() + off, n)) { cOk = false; printf("      cache: %u bytes at %u wrong\n", n, off); }
    }
    printf("      cache: %u misses, %u reads of the card for 3000 random reads of a %zu byte file\n", mag->misses, mag->reads, file.size());
    magEnd();
  }
  check("maglogic: the page cache reads the file right (3000 random reads)", cOk);
  // the card out: a page that is not cached waits; the card back with another file: told, no crash
  bool changed = false, back = false;
  if (!magLoad(qp.c_str(), msg, sizeof(msg), &line)) {
    uint8_t b[8];
    magRead(0, b, 8);                                  // the first page cached
    simSd = simSdIn = false;
    uint32_t t = (uint32_t)(clockUs / 1000);
    simAt.push_back({ t + 1500, [qp] { sdWrite(qp, sdRead(qp) + "\n-- changed\n"); simSd = true; } });
    bool cached = magRead(0, b, 8);                    // from the cache: no waiting
    bool got = magRead(9000, b, 8);                    // not cached: waits for the card, finds another file
    changed = cached && !got && mag->failed && strstr(mag->err, "has changed") && mag->quit && mag->waits == 1;
    printf("      changed file: %s\n", mag->err);
    magEnd();
    sdCard("frames/maglogic", true);                   // the card as it was
    simSd = simSdIn = true;
    // the same file back: goes on
    if (!magLoad(qp.c_str(), msg, sizeof(msg), &line)) {
      std::string file = sdRead(qp);
      simSd = simSdIn = false;
      t = (uint32_t)(clockUs / 1000);
      simAt.push_back({ t + 2500, [] { simSd = true; } });
      uint8_t c[16];
      bool got2 = magRead(5000, c, 16);
      back = got2 && !memcmp(c, file.data() + 5000, 16) && !mag->failed && mag->waits == 1 && simSdIn;
      magEnd();
    }
  }
  check("maglogic: card pulled - a cached page is read, a missing one waits; the same file back: goes on; a changed file: told", changed && back);
  // the save file: tmp + rename, read back; nothing else written
  if (system("rm -rf frames/maglogic/MINIARC/save")) {}
  bool sOk = !magLoad((G + "iceslide.mag").c_str(), msg, sizeof(msg), &line);
  if (sOk) {
    magSaveSlot(0, 7); magSaveSlot(3, -12); mag->best = 900;
    sOk = magSaveWrite();
    magEnd();
    std::string sv = sdRead("frames/maglogic/MINIARC/save/iceslide.sav");
    uint16_t best = 0; int32_t slots[MAG_SLOTS] = { 0 };
    magSaveRead("iceslide.mag", &best, slots);
    printf("      iceslide.sav:\n%s", sv.c_str());
    sOk = sOk && best == 900 && slots[0] == 7 && slots[3] == -12 && !sdExists("frames/maglogic/MINIARC/save/iceslide.tmp");
    remove("frames/maglogic/MINIARC/save/iceslide.sav");
    sdWrite("frames/maglogic/MINIARC/save/iceslide.tmp", "best 5\nslot 1 4\n");   // an interrupted save
    best = 0; memset(slots, 0, sizeof(slots));
    magSaveRead("iceslide.mag", &best, slots);
    sOk = sOk && best == 5 && slots[1] == 4;
  }
  check("maglogic: save slots and the best score onto the card (tmp + rename), read back, an interrupted save read", sOk);
  // the examples: each compiles, its first frames run well inside the budget
  bad = 0;
  for (const char *g : { "meteors.mag", "iceslide.mag", "quest.mag" }) {
    if (magLoad((G + g).c_str(), msg, sizeof(msg), &line)) { bad++; printf("      %s: line %u: %s\n", g, line, msg); continue; }
    printf("      %-12s code %5u bytes, %4u vars, %2u sprites, %2u maps (%2u levels), %2u functions; heap %5zu bytes\n", g, mag->codeN, mag->nG, mag->nSpr, mag->nMap,
           mag->nLvl, mag->nFn, mag->heap);
    bool ok = magStart();
    for (int f = 0; f < 30 && ok; f++) { mag->budget = MAG_BUDGET; ok = magRun(mag->fUpdate, "update") && magRun(mag->fDraw, "draw"); }
    if (!ok) { bad++; printf("      %s: %s line %u\n", g, mag->err, magLineAt(mag->errPc)); }
    magEnd();
  }
  printf("      compiler: %zu bytes of heap at most\n", magCompilePeak);
  check("maglogic: the three example games compile and run", !bad);
  // Ice Slide's levels: every one solvable by its rules
  auto lv = magLevels(G + "iceslide.mag");
  bad = 0;
  std::string moves;
  for (auto &g : lv) {
    int sx = -1, sy = -1;
    for (int y = 0; y < (int)g.size(); y++) for (int x = 0; x < (int)g[y].size(); x++) if (g[y][x] == '@') { sx = x; sy = y; }
    std::vector<int> p = sx >= 0 ? iceSolve(g, sx, sy) : std::vector<int>();
    if (p.empty()) bad++;
    moves += " " + std::to_string(p.size());
  }
  printf("      ice slide: %zu levels, fewest moves:%s\n", lv.size(), moves.c_str());
  check("maglogic: Ice Slide has 15+ levels, every one solvable", lv.size() >= 15 && !bad);
  auto qv = magLevels(G + "quest.mag");
  bool qOk = qv.size() == 3;
  for (auto &g : qv) {
    std::string all;
    for (auto &row : g) all += row;
    qOk = qOk && g.size() == 13 && g[0].size() * 16 > 3 * SCR_W && all.find('P') != std::string::npos && all.find('F') != std::string::npos;
  }
  check("maglogic: Pixel Quest has three worlds, each more than three screens wide, a start and a flag", qOk);
  // how deep the compiler's stack goes on a deeply nested game; deeper than that: an error, no crash
  std::string deep = "game \"N\"\nvar a\nfunc update()\n";
  for (int i = 0; i < 10; i++) deep += std::string(i * 2 + 2, ' ') + "if a < 3 then\n";
  deep += "  a = min((((((((a + 1) * 2) - 3) / 4) + 5) * 6) - 7) / 8, abs(max(a, 2) + clamp(a, 1, 9)))\n";
  for (int i = 0; i < 10; i++) deep += "end\n";
  deep += "end\nfunc draw()\nend\n";
  magProbeStart();
  MagRes rd = magSrc(deep);
  if (!rd.r) magEnd();
  long used = magProbeTop - magProbeLow;
  std::string deeper = "game \"N\"\nvar a\nfunc update()\n  a = " + std::string(39, '(') + "a" + std::string(39, ')') + "\nend\nfunc draw()\nend\n";
  MagRes rd2 = magSrc(deeper);
  if (!rd2.r) magEnd();
  printf("      10 nested ifs, 8 brackets, calls in calls: %s, the compiler's stack went %ld bytes deep (on this PC); 39 brackets: line %u: %s\n",
         rd.r ? rd.msg.c_str() : "compiled", used, rd2.line, rd2.msg.c_str());
  check("maglogic: deep nesting compiles within the stack it may take; deeper: an error at its line, no crash",
        !rd.r && used < MAG_CSTACK + 1024 && rd2.r == 2 && rd2.line == 4 && rd2.msg.find("nested too deeply") != std::string::npos);
  check("maglogic: nothing of a game is left on the heap", !mag);
}

// ---------------- the bots of magplay / magpull ----------------
static std::vector<std::vector<std::string>> magQuest;        // the platformer's worlds, read by the host
static std::map<std::string, std::array<uint64_t, 3>> magOps; // per game: frames, instructions, most in a frame
static int32_t magCamMax = 0, magIceAgain = 0;
static bool magRestartOk = false;
static uint32_t magMissStart = 0, magMissEnd = 0, magPullT = 0, magBackT = 0, magWaitSeen = 0;
static int32_t magCamAtPull = 0, magCamAfter = 0;
static long magFramesAtBack = 0;
static void magHook() {                                       // every frame of an SD game
  auto &o = magOps[mag->title];
  o[0]++; o[1] += mag->opsFrame;
  if (mag->opsFrame > o[2]) o[2] = mag->opsFrame;
  if (!strcmp(mag->title, "Pixel Quest")) {
    static int32_t lastSt = 0, lx = 0, ly = 0;
    if (getenv("MAGDBG") && magG("state") == 1 && lastSt == 0) printf("      HURT at x %d y %d (before: x %d y %d) walkers:", magG("px") / 16, magG("py") / 16, lx, ly);
    if (getenv("MAGDBG") && magG("state") == 1 && lastSt == 0) { for (int i = 0; i < 16; i++) if (magG("es", i) == 1) printf(" %d,%d", magG("ex", i), magG("ey", i)); printf("\n"); }
    lastSt = magG("state"); lx = magG("px") / 16; ly = magG("py") / 16;
    if (getenv("MAGDBG") && o[0] % 15 == 0) printf("      t %u world %d x %d y %d vy %d ground %d state %d lives %d\n", (unsigned)(clockUs / 1000), magG("world"), magG("px") / 16, magG("py") / 16, magG("vy"), magG("onground"), magG("state"), magG("lives"));
    int32_t cam = magG("camx");
    if (cam > magCamMax) magCamMax = cam;
    if (magBackT && cam > magCamAfter) magCamAfter = cam;
  }
}

static char magQuestAt(int w, int x, int y) {
  if (w < 1 || w > (int)magQuest.size() || y < 0 || y >= 13 || x < 0 || x >= (int)magQuest[w - 1][0].size()) return 0;
  return magQuest[w - 1][y][x];
}
static bool magQuestSolid(int w, int x, int y) { char c = magQuestAt(w, x, y); return c == '#' || c == '=' || c == 'B'; }
// runs right; jumps over walls, pits, spikes and walkers
static uint32_t magJumpUntil = 0;
static void magQuestBot(uint32_t t) {
  if (!mag || strcmp(mag->title, "Pixel Quest") || magG("state") != 0) { applyMask(0); return; }
  int w = magG("world"), x = magG("px") / 16, y = magG("py") / 16;
  bool ground = magG("onground") != 0;
  int fy = (y + 13) / 16, front = (x + 9) / 16;
  bool jump = false;
  for (int d = 4; d <= 24; d += 4) {
    int cx = (x + 9 + d) / 16;
    if (d <= 12 && (magQuestSolid(w, cx, fy) || magQuestSolid(w, cx, y / 16))) jump = true;   // a wall
    if (magQuestAt(w, cx, fy) == '^') jump = true;                                            // spikes
  }
  if (!magQuestSolid(w, front + 1, fy + 1) && magQuestSolid(w, front, fy + 1)) jump = true;   // the edge of a pit
  for (int i = 0; i < 16; i++)
    if (magG("es", i) == 1) { int d = magG("ex", i) - x; if (d > 4 && d < 60 && abs(magG("ey", i) - y) < 20) jump = true; }
  if (jump && ground && t > magJumpUntil + 150) magJumpUntil = t + 520;
  applyMask((uint8_t)(RT | (t < magJumpUntil ? UP : 0)));
}
// slides the penguin along the shortest solution, one move each time it stands
static void magIceBot(uint32_t t) {
  if (botBusy(t) || !mag || strcmp(mag->title, "Ice Slide")) return;
  if (magG("mode") != 1 || magG("dx") || magG("dy") || mag->mid < 0) return;
  const MagBlk &b = mag->map[mag->mid];
  std::vector<std::string> g(b.h);
  for (int y = 0; y < b.h; y++) g[y] = std::string((const char *)mag->mbuf + y * b.w, b.w);
  std::vector<int> p = iceSolve(g, magG("px"), magG("py"));
  if (p.empty()) return;
  static const uint8_t K[4] = { UP, DN, LT, RT };
  botPress(t, K[p[0]], 50);
}

// an error as the error screen wraps it: every line of it on the screen
static void magSeeError(const std::string &msg) {
  sdAdd([=](uint32_t) {
    Font f = Gfx::width(msg.c_str(), F_TEXT) <= 4 * 300 ? F_TEXT : F_SMALL;
    const char *p = msg.c_str();
    while (*p) {
      size_t cut = magWrapCut(p, 300, f);
      if (!recent(std::string(p, cut).c_str())) return false;
      p += cut;
      while (*p == ' ') p++;
    }
    return true;
  });
}
static void magLeave() {                                       // hold OK: the pause menu (then quit) or back from game over
  sdKey(OK, 1, 900); sdWait(500);
  sdAdd([](uint32_t t) { if (recent("PAUSE")) botPress(t, OK, 900); return true; });
  sdWait(700);
}
static void magPlayQuest(uint32_t ms) {
  sdAdd([=](uint32_t t) { if (t - sdStepT >= ms) { applyMask(0); return true; } magQuestBot(t); return false; });
}

/* The scenarios with a screen. true = it was one of them. */
static bool magScenario(const uint32_t M) {
  if (scenario != "magplay" && scenario != "magpull" && scenario != "magnone" && scenario != "magerr") return false;
  magFrameHook = magHook;
  simBot = sdDriver; botFrom = M;
  sdDo([]() { sdNvsU16 = simNvsU16; sdNvsBlob = simNvsBlob; });
  if (scenario == "magnone") {
    // no card (searched at the first start, none found): the entry says so, Snake as before
    simNvsU16["sdcs"] = 255;
    sdGame("SD games");
    sdSee(DTR("no SD card", "keine SD-Karte")); sdWait(300); sdSnap("nocard");
    sdKey(OK, 1, 80); sdWait(600);
    sdGame("Snake"); sdWait(3000); sdSnap("snake");
    sdKey(OK, 1, 900); sdWait(400); sdKey(OK, 1, 900); sdWait(600);
  } else if (scenario == "magerr") {
    // a card without games, then broken ones: the launcher and every error screen
    sdCard("frames/magerr", false);
    simSdDir = "frames/magerr"; simSd = true; simNvsU16["sdcs"] = 21;
    sdGame("SD games");
    sdSee(DTR("no games on the card", "keine Spiele auf der Karte")); sdWait(300); sdSnap("empty");
    sdKey(OK, 1, 80); sdWait(500);
    sdDo([]() {
      const std::string G = "frames/magerr/MINIARC/games/";
      if (system(("mkdir -p " + G).c_str())) {}
      sdWrite(G + "broken.mag", "game \"Broken\"\nauthor \"test\"\ninfo \"a compile error in line 5\"\nfunc update()\n  x += 1\nend\nfunc draw()\nend\n");
      sdWrite(G + "loop.mag", "game \"Endless\"\ninfo \"loops forever in update()\"\nvar a\nfunc update()\n  while true do\n    a = a + 1\n  end\nend\nfunc draw()\nend\n");
      sdWrite(G + "notagame.mag", "hello, I am a text file\n");
      sdWrite(G + "sokoban.mag", "game \"Taken name\"\nfunc update()\nend\nfunc draw()\nend\n");
      sdWrite(G + "arrays.mag", "game \"Out of range\"\narray a[4]\nvar i\nfunc update()\n  i = i + 1\n  a[i] = 1\nend\nfunc draw()\n  cls(BLUE)\nend\n");
    });
    sdGame("SD games");
    sdSee("Broken"); sdWait(300); sdSnap("list");
    // sorted: Broken, Endless, notagame.mag, Out of range, Taken name
    sdKey(OK, 1, 80);
    magSeeError("line 5: '+=' is not supported: write x = x + ..."); sdSee("x += 1"); sdWait(300); sdSnap("compile");
    sdKey(OK, 1, 80); sdWait(400);
    sdKey(DN); sdKey(OK, 1, 80);
    magSeeError("line 6: script too slow or endless loop in update() (150000 instructions in one frame)"); sdWait(300); sdSnap("loop");
    sdKey(OK, 1, 80); sdWait(400);
    sdKey(DN); sdWait(300);
    sdSee(DTR("no 'game' line at the start: not a MAG game?", "keine 'game'-Zeile am Anfang: kein MAG-Spiel?")); sdSnap("notagame");
    sdKey(OK, 1, 80);
    magSeeError("line 1: a game starts with its name: game \"My Game\""); sdWait(300);
    sdKey(OK, 1, 80); sdWait(400);
    sdKey(DN); sdKey(OK, 1, 80);
    magSeeError("line 6: array index 4 is outside 0..3"); sdWait(300); sdSnap("array");
    sdKey(OK, 1, 80); sdWait(400);
    sdKey(DN); sdKey(OK, 1, 80);
    sdSee(DTR("CANNOT START", "START GEHT NICHT")); sdWait(300); sdSnap("taken");
    sdKey(OK, 1, 80); sdWait(400);
    sdDo([]() { simMagHeap = 20000; });                // no room: told
    sdKey(UP); sdKey(OK, 1, 80);
    magSeeError("not enough memory to compile a game"); sdWait(300); sdSnap("memory");
    sdDo([]() { simMagHeap = 200000; });
    sdKey(OK, 1, 80); sdWait(400);
    sdKey(OK, 1, 900); sdWait(600);
  } else {
    sdCard("frames/magcard", true);
    simSdDir = "frames/magcard"; simSd = true; simNvsU16["sdcs"] = 21;
    magQuest = magLevels("../sdcard/MINIARC/games/quest.mag");
    sdGame("SD games");
    sdSee("Ice Slide"); sdSee("Meteor Dodge"); sdSee("Pixel Quest"); sdWait(400); sdSnap("list");
    if (scenario == "magplay") {
      // Meteor Dodge: random steering for 20 s
      sdKey(DN); sdKey(OK, 1, 80);
      sdSee("DODGE THE METEORS!"); sdWait(1500); sdSnap("meteors");
      sdAdd([](uint32_t t) {
        if (t - sdStepT >= 20000) return true;
        if (!botBusy(t)) botPress(t, rand() % 2 ? LT : RT, 150 + rand() % 400);
        return false;
      });
      sdSnap("meteors_late");
      magLeave();
      // again, then "restart" from the pause menu: a new round from init()
      sdKey(OK, 1, 80);
      sdSee("DODGE THE METEORS!"); sdWait(3000);
      sdKey(OK, 1, 900); sdWait(500); sdSee("PAUSE");
      sdKey(DN); sdKey(OK, 1, 80);
      sdMark();
      sdAdd([](uint32_t) { return sdSeenSince("DODGE THE METEORS!"); });
      sdDo([]() { magRestartOk = magG("timer") < 60 && magG("lives") == 3; });
      magLeave();
      // Ice Slide: the title, level 1, the bot solves three levels
      sdSee(DTR("SD GAMES", "SD-SPIELE"));
      sdKey(UP); sdKey(OK, 1, 80);
      sdSee("ICE SLIDE"); sdWait(500); sdSnap("ice_title");
      sdKey(OK, 1, 80);
      sdAdd([](uint32_t t) { if (magG("lv") >= 4 && magG("mode") == 1) return true; magIceBot(t); return false; });
      sdWait(800); sdSnap("ice_level4");
      sdDo([]() { printf("      ice slide: on level %d, save file:\n%s", magG("lv"), sdRead("frames/magcard/MINIARC/save/iceslide.sav").c_str()); });
      magLeave();
      // again: the title starts at the level reached (read from the card)
      sdKey(OK, 1, 80);
      sdSee("ICE SLIDE"); sdWait(500);
      sdDo([]() { printf("      ice slide again: the title offers level %d\n", magG("lv")); magIceAgain = magG("lv"); });
      sdSnap("ice_again");
      magLeave();
      // Pixel Quest: the bot runs and jumps for 45 s
      sdKey(DN); sdKey(DN); sdKey(OK, 1, 80);
      sdSee("WORLD"); sdWait(1500);
      sdDo([]() { magMissStart = mag ? mag->misses : 0; });
      magPlayQuest(45000);
      sdDo([]() { magMissEnd = mag ? mag->misses : 0; });
      sdSnap("quest");
      magLeave();
    } else {
      // magpull: the platformer; the card pulled while running, put back after the wait shows
      sdKey(DN); sdKey(DN); sdKey(OK, 1, 80);
      sdSee("WORLD"); sdWait(1000);
      sdAdd([](uint32_t t) { magQuestBot(t); return magG("camx") > 200; });
      sdDo([]() { simSd = simSdIn = false; magPullT = (uint32_t)(clockUs / 1000); magCamAtPull = magG("camx"); printf("      card pulled at camx %d\n", magCamAtPull); });
      sdAdd([](uint32_t t) { if (recent(DTR("insert the SD card", "SD-Karte wieder einstecken"))) { applyMask(0); magWaitSeen = t; return true; } magQuestBot(t); return t - sdStepT > 30000; });
      sdWait(300); sdSnap("insert");
      sdWait(2500);
      sdDo([]() { simSd = true; magBackT = (uint32_t)(clockUs / 1000); magFramesAtBack = frames; printf("      card back in\n"); });
      sdAdd([](uint32_t t) { return !recent(DTR("insert the SD card", "SD-Karte wieder einstecken")) && t - sdStepT > 300; });
      magPlayQuest(8000);
      sdSnap("after");
      magLeave();
    }
  }
  sdKey(OK, 1, 900); sdWait(800);                              // out of the launcher
  sdAdd([](uint32_t t) { simEnd = t + 300; return true; });
  simEnd = M + 400000;
  return true;
}

static void magChecks() {
  if (scenario != "magplay" && scenario != "magpull" && scenario != "magnone" && scenario != "magerr") return;
  printf("      steps done %zu of %zu\n", sdAt, sdSteps.size());
  check("sd games: every step of the scenario reached", sdAt == sdSteps.size());
  for (auto &o : magOps) {
    double avg = o.second[0] ? (double)o.second[1] / o.second[0] : 0;
    printf("      %-13s %5llu frames, %6.0f instructions a frame on average, %6llu at most; C3 at 160 MHz (40 cycles each): %.1f ms / %.1f ms\n", o.first.c_str(),
           (unsigned long long)o.second[0], avg, (unsigned long long)o.second[2], avg * 40 / 160000.0, o.second[2] * 40 / 160000.0);
  }
  printf("      heap: %zu bytes for a running game at most, %zu while compiling\n", magHeapPeak, magCompilePeak);
  if (scenario == "magnone") {
    check("sd games none: the entry says there is no card, Snake as before", seen(TR("no SD card", "keine SD-Karte")) && seen("SNAKE") && !sdExists("frames/no-sd-card") && magOps.empty());
    check("sd games none: nothing written to the console's flash", sdNvsSame());
  }
  if (scenario == "magerr") {
    check("sd games errors: compile error, endless loop, array index, not a game, a taken name, no memory - each told, back to the list",
          seen("x += 1") && seen(TR("CANNOT START", "START GEHT NICHT")) && sdAt == sdSteps.size() && !mag);
    check("sd games errors: nothing written to the card or the flash", !sdExists("frames/magerr/MINIARC/save") && sdNvsSame());
  }
  if (scenario == "magplay") {
    auto &me = magOps["Meteor Dodge"], &ic = magOps["Ice Slide"], &qu = magOps["Pixel Quest"];
    std::string sv = sdRead("frames/magcard/MINIARC/save/iceslide.sav");
    printf("      pixel quest: camera up to %d px, %u pages read while it ran (%u in the first second)\n", magCamMax, magMissEnd - magMissStart, magMissStart);
    check("sd games: the launcher lists the three games with title, author and info", seen("Ice Slide") && seen("Meteor Dodge") && seen("Pixel Quest") &&
          seen(TR("by MiniArcade", "von MiniArcade")));
    std::string ms = sdRead("frames/magcard/MINIARC/save/meteors.sav");
    check("sd games: Meteor Dodge ran with keys, sound and the status bar; its record went onto the card", me[0] > 300 && seen("Meteor Dodge") && toneCount > 0 &&
          ms.find("best ") != std::string::npos && ms.find("best 0") == std::string::npos);
    check("sd games: restart from the pause menu starts a new round (vars reset, init() again)", magRestartOk);
    check("sd games: Ice Slide solved levels 1-3 from the card, the level reached saved on the card and offered again", ic[0] > 100 &&
          sv.find("slot 0 4") != std::string::npos && magIceAgain == 4 && !sdExists("frames/magcard/MINIARC/save/iceslide.tmp"));
    check("sd games: Pixel Quest scrolled (camera past 2 screens) and streamed its map from the card while running", qu[0] > 600 && magCamMax > 2 * SCR_W &&
          magMissEnd > magMissStart + 10);
    bool inside = true;
    for (auto &o : magOps) if (o.second[2] > MAG_BUDGET / 5) inside = false;
    check("sd games: every example stays well inside the budget (a fifth of it at most)", inside);
    check("sd games: no file but the save file written, nothing to the console's flash", sdNvsSame() && !sdExists("frames/magcard/MINIARC/save/meteors.tmp"));
  }
  if (scenario == "magpull") {
    printf("      pulled at %u ms (camera %d), the wait showed at %u, back at %u; camera after %d, %ld frames after\n", magPullT, magCamAtPull, magWaitSeen, magBackT,
           magCamAfter, frames - magFramesAtBack);
    check("sd games pull: the game waited with the message while the card was out", magWaitSeen > magPullT && seen(TR("insert the SD card", "SD-Karte wieder einstecken")));
    check("sd games pull: the card back, the same game went on (scrolled further, no error)", magBackT && magCamAfter > magCamAtPull + 100 && !seen("THE GAME STOPPED") &&
          frames - magFramesAtBack > 150);
  }
}
