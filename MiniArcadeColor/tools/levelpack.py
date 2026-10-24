#!/usr/bin/env python3
"""MiniArcade Color: level packs for the SD card.

The console reads extra levels for Sokoban, Pumpkin Maze and the Block
Puzzle adventure from /MINIARC/levels/<game>.txt on the card (see
sdcard/MINIARC/levels/README.txt for the formats). This tool makes such
packs and checks them, with the console's own rules (main/sdlevels.h,
game_sokoban.h, game_pumpkin.h, game_blocks.h) plus more:

  sokoban  every level solved by a solver (fewest pushes), easiest first
  pumpkin  the goal reached from the start under the game's own rules
           (pmPath: no walls, pits or bumpers, arrows one way, doors with
           every key)
  blocks   the goal not impossible: the console's greedy test bot (ported)
           reaches it within the moves in at least 3 of 12 games

  levelpack.py check FILE [--game G]      check a pack (G from the file name)
  levelpack.py gen GAME [-n N] [--seed S] [-o FILE]   make a pack
  levelpack.py samples DIR                the three sample packs into DIR

check exits with 1 when a line would be left out by the console or a level
fails the extra checks.
"""
import argparse
import os
import random
import sys
from collections import deque

LINE_MAX = 318            # the console reads lines of up to 318 characters (sdlevels.h, SDL_LINE)
PACK_MAX = 999            # levels of a pack at most


def is_comment(line):
    if line.startswith('﻿'):
        line = line[1:]
    return not line or (line[0] == '#' and '|' not in line)


def pack_lines(text):
    """(line number, line) of every line the console treats as a level."""
    out = []
    for i, raw in enumerate(text.split('\n'), 1):
        line = raw.rstrip('\r\n \t')
        if is_comment(line):
            continue
        out.append((i, line))
    return out


# ---------------------------------------------------------------- Sokoban
SK_W, SK_H = 21, 8
DIRS = [(0, -1), (0, 1), (-1, 0), (1, 0)]


def sk_valid(t):
    """skValid: None = a level, else why not."""
    if len(t.encode()) > LINE_MAX:
        return 'line too long'
    rows = t.split('|')
    if len(rows) > SK_H:
        return '8 rows at most'
    if len(rows) < 3:
        return '3 rows at least'
    w = max(len(r) for r in rows)
    if w > SK_W:
        return '21 columns at most'
    if w < 3:
        return '3 columns at least'
    boxes = goals = 0
    player = []
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c not in '#.$*@+ -_':
                return 'unknown character %r' % c
            boxes += c in '$*'
            goals += c in '.*+'
            if c in '@+':
                player.append((x, y))
    if len(player) != 1:
        return 'exactly one player'
    if boxes != goals:
        return 'as many boxes as goals'
    if not boxes:
        return 'one box at least'
    seen = {player[0]}
    st = [player[0]]
    while st:
        x, y = st.pop()
        for dx, dy in DIRS:
            nx, ny = x + dx, y + dy
            if nx < 0 or ny < 0 or ny >= len(rows) or nx >= len(rows[ny]):
                return 'not closed by walls'
            if rows[ny][nx] != '#' and (nx, ny) not in seen:
                seen.add((nx, ny))
                st.append((nx, ny))
    return None


def sk_parse(t):
    rows = t.split('|')
    w, h = max(len(r) for r in rows), len(rows)
    wall, goal, boxes, p = set(), set(), [], None
    for y in range(h):
        for x in range(w):
            c = rows[y][x] if x < len(rows[y]) else '#'
            if c == '#':
                wall.add((x, y))
            if c in '.*+':
                goal.add((x, y))
            if c in '$*':
                boxes.append((x, y))
            if c in '@+':
                p = (x, y)
    return w, h, wall, goal, boxes, p


def sk_solve(t, limit=300000):
    """Fewest pushes (breadth first over box positions, the player as the area
    it reaches), or -1 when there is none within the limit."""
    w, h, wall, goal, boxes, p = sk_parse(t)
    N = w * h
    iw = [False] * N
    for (x, y) in wall:
        iw[y * w + x] = True
    ig = [False] * N
    for (x, y) in goal:
        ig[y * w + x] = True
    d4 = [-w, w, -1, 1]
    dead = [False] * N                    # corners that are not goals
    for i in range(N):
        if iw[i] or ig[i]:
            continue
        x, y = i % w, i // w
        v = (y == 0 or iw[i - w]) or (y == h - 1 or iw[i + w])
        hz = (x == 0 or iw[i - 1]) or (x == w - 1 or iw[i + 1])
        dead[i] = v and hz

    def reach(frm, bx):
        seen = {frm}
        st = [frm]
        while st:
            c = st.pop()
            for d in d4:
                n = c + d
                if 0 <= n < N and not iw[n] and n not in bx and n not in seen:
                    seen.add(n)
                    st.append(n)
        return seen

    b0 = frozenset(y * w + x for (x, y) in boxes)
    p0 = p[1] * w + p[0]
    r0 = reach(p0, b0)
    seen = {(min(r0), b0)}
    q = deque([(p0, b0, 0)])
    while q:
        pp, bx, dist = q.popleft()
        if all(ig[b] for b in bx):
            return dist
        if len(seen) > limit:
            return -1
        area = reach(pp, bx)
        for b in bx:
            for d in d4:
                frm, to = b - d, b + d
                if frm not in area or not (0 <= to < N) or iw[to] or to in bx or dead[to]:
                    continue
                if (d == 1 and to % w == 0) or (d == -1 and b % w == 0):
                    continue
                nb = (bx - {b}) | {to}
                lo = min(reach(b, nb))
                k = (lo, nb)
                if k in seen:
                    continue
                seen.add(k)
                q.append((b, nb, dist + 1))
    return -1


def sk_room(rng, w, h):
    """A room: a rectangle with random wall bits inside, all its floor connected."""
    g = [['#'] * w for _ in range(h)]
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            g[y][x] = ' '
    for _ in range(rng.randint(2, (w * h) // 7)):
        x, y = rng.randint(1, w - 2), rng.randint(1, h - 2)
        g[y][x] = '#'
        if rng.random() < 0.4:
            dx, dy = rng.choice(DIRS)
            if 1 <= x + dx < w - 1 and 1 <= y + dy < h - 1:
                g[y + dy][x + dx] = '#'
    floor = [(x, y) for y in range(h) for x in range(w) if g[y][x] == ' ']
    if not floor:
        return None
    start = rng.choice(floor)
    seen = {start}
    st = [start]
    while st:
        x, y = st.pop()
        for dx, dy in DIRS:
            n = (x + dx, y + dy)
            if g[n[1]][n[0]] == ' ' and n not in seen:
                seen.add(n)
                st.append(n)
    for (x, y) in floor:
        if (x, y) not in seen:
            g[y][x] = '#'
    return g, sorted(seen)


def sk_make(rng, nbox):
    """Pulls the boxes backwards from the goals: always solvable in principle."""
    w, h = rng.randint(7, 13), rng.randint(6, SK_H)
    r = sk_room(rng, w, h)
    if not r:
        return None
    g, floor = r
    if len(floor) < nbox * 4 + 8:
        return None
    cells = rng.sample(floor, nbox + 1)
    goals = set(cells[:nbox])
    boxes = set(goals)
    px, py = cells[nbox]
    for _ in range(rng.randint(150, 600)):
        dx, dy = rng.choice(DIRS)
        nx, ny = px + dx, py + dy
        if g[ny][nx] == '#' or (nx, ny) in boxes:
            continue
        bx, by = px - dx, py - dy                     # the box behind the player comes along
        if (bx, by) in boxes and rng.random() < 0.7:
            boxes.remove((bx, by))
            boxes.add((px, py))
        px, py = nx, ny
    if boxes & goals:
        return None
    rows = []
    for y in range(h):
        row = ''
        for x in range(w):
            c = g[y][x]
            if (x, y) in boxes:
                c = '*' if (x, y) in goals else '$'
            elif (x, y) in goals:
                c = '+' if (x, y) == (px, py) else '.'
            elif (x, y) == (px, py):
                c = '@'
            row += c
        rows.append(row.rstrip())
    return '|'.join(rows)


def sk_gen(n, rng, log):
    found = {}
    tries = 0
    while len(found) < n * 4 and tries < n * 400:
        tries += 1
        nbox = rng.choice([2, 2, 3, 3, 3, 4])
        t = sk_make(rng, nbox)
        if not t or sk_valid(t) or t in found:
            continue
        p = sk_solve(t)
        if p < 4 + nbox:
            continue
        found[t] = (p, nbox)
        log('sokoban: %d candidates' % len(found))
    # an even spread of difficulty, easiest first
    order = sorted(found.items(), key=lambda kv: (kv[1][0], kv[1][1], kv[0]))
    if len(order) <= n:
        pick = order
    else:
        pick = [order[round(i * (len(order) - 1) / (n - 1))] for i in range(n)]
    return [(t, 'boxes %d, pushes %d' % (v[1], v[0])) for t, v in pick]


# ---------------------------------------------------------------- Pumpkin Maze
PM_W, PM_H, PM_NAME, PM_KEYS, PM_MOVERS = 20, 13, 12, 3, 6
PM_CH = '.#SGOim*><^vckDCHU'
PM_DX, PM_DY = [1, -1, 0, 0], [0, 0, -1, 1]


def pm_parse(t):
    """pmParse + pmCheck: (name, map, why) - why None = a level."""
    if len(t.encode()) > LINE_MAX:
        return None, None, 'line too long'
    if '|' not in t:
        return None, None, 'NAME|row|row|... expected'
    name, rest = t.split('|', 1)
    name = name.lstrip(' ')
    for c in name:
        if ord(c) < 32 or ord(c) > 126 or c in '"\\':
            return None, None, 'the name: letters, digits, spaces'
    if len(name) > PM_NAME:
        return None, None, 'the name: 12 characters at most'
    name = name.rstrip(' ')
    if not name:
        return None, None, 'the level needs a name'
    m = ['.'] * (PM_W * PM_H)
    x = y = 0
    for c in rest:
        if c in '\r\n':
            continue
        if c == '|':
            y += 1
            if y >= PM_H:
                return None, None, '13 rows at most'
            x = 0
            continue
        if x >= PM_W:
            return None, None, '20 tiles a row at most'
        if c not in PM_CH:
            return None, None, 'unknown tile in the level'
        m[y * PM_W + x] = c
        x += 1
    why = pm_check(m)
    return name, m, why


def pm_arrow(c):
    return '><^v'.index(c) if c and c in '><^v' else -1


def pm_path(m):
    """pmPath: the shortest way in tiles from S to G, -1 = none."""
    key_at, keys, start = {}, 0, -1
    for i, c in enumerate(m):
        if c == 'k' and keys < PM_KEYS:
            key_at[i] = keys
            keys += 1
        if c == 'S':
            start = i
    if start < 0:
        return -1
    full = (1 << keys) - 1
    seen = {(start, 0)}
    q = deque([(start, 0, 0)])
    while q:
        cell, mask, d = q.popleft()
        if m[cell] == 'G':
            return d
        frm = pm_arrow(m[cell])
        for k in range(4):
            x, y = cell % PM_W + PM_DX[k], cell // PM_W + PM_DY[k]
            if x < 0 or y < 0 or x >= PM_W or y >= PM_H:
                continue
            n = y * PM_W + x
            to = pm_arrow(m[n])
            if (frm >= 0 and frm == (k ^ 1)) or (to >= 0 and to == (k ^ 1)):
                continue
            c = m[n]
            if c in '#O*' or (c == 'D' and mask != full):
                continue
            nm = mask | (1 << key_at[n]) if n in key_at else mask
            if (n, nm) in seen:
                continue
            seen.add((n, nm))
            q.append((n, nm, d + 1))
    return -1


def pm_check(m):
    s = m.count('S')
    g = m.count('G')
    k = m.count('k')
    if s != 1:
        return 'exactly one start (S) is needed'
    if g != 1:
        return 'exactly one goal (G) is needed'
    if k > PM_KEYS:
        return '3 keys at most'
    if m.count('D') and not k:
        return 'a door needs a key'
    if m.count('H') + m.count('U') > PM_MOVERS:
        return '6 moving pits at most'
    if pm_path(m) < 0:
        return 'the goal cannot be reached from the start'
    return None


PM_A = ['BAT', 'OWL', 'GHOST', 'WITCH', 'MOON', 'BONE', 'CROW', 'FOG', 'RAVEN', 'SKULL', 'TOAD', 'WOLF',
        'MUMMY', 'SPOOK', 'NIGHT', 'GRAVE', 'CANDY', 'BROOM', 'SLIME', 'ROOT', 'ASH', 'IVY', 'RUNE', 'TOMB']
PM_B = ['HALL', 'LANE', 'PATH', 'WALK', 'CAVE', 'YARD', 'GATE', 'DEN', 'NEST', 'MAZE', 'PIT', 'ROAD',
        'CRYPT', 'TOWER', 'WOODS', 'MARSH', 'RIDGE', 'BRIDGE', 'HOLE', 'LOOP', 'STEPS']


def pm_make(rng, diff):
    """A board of 20 x 13: walls round it, a few wall bands with gaps, then
    pits, ice, mud, bumpers, arrows, candy, keys and a door, more of them the
    harder it is (diff 0..1)."""
    g = [['.'] * PM_W for _ in range(PM_H)]
    for x in range(PM_W):
        g[0][x] = g[PM_H - 1][x] = '#'
    for y in range(PM_H):
        g[y][0] = g[y][PM_W - 1] = '#'
    kind = rng.choice(['bands', 'bands', 'rooms', 'open'])
    if kind == 'bands':                       # horizontal walls with a gap at alternating ends
        ys = list(range(3, PM_H - 2, rng.choice([3, 4])))
        for i, y in enumerate(ys):
            gap = rng.randint(3, 5)
            left = (i % 2 == 0) != (rng.random() < 0.2)
            for x in range(1, PM_W - 1):
                if (left and x > gap) or (not left and x < PM_W - 1 - gap):
                    g[y][x] = '#'
    elif kind == 'rooms':                     # a cross of walls, gaps in each arm
        cx, cy = rng.randint(7, 12), rng.randint(5, 7)
        for x in range(1, PM_W - 1):
            g[cy][x] = '#'
        for y in range(1, PM_H - 1):
            g[y][cx] = '#'
        for (x0, x1, y0, y1, vert) in [(1, cx - 1, cy, cy, False), (cx + 1, PM_W - 2, cy, cy, False),
                                       (cx, cx, 1, cy - 1, True), (cx, cx, cy + 1, PM_H - 2, True)]:
            if rng.random() < 0.8:
                if vert:
                    y = rng.randint(y0, y1)
                    g[y][cx] = '.'
                    if y + 1 <= y1:
                        g[y + 1][cx] = '.'
                else:
                    x = rng.randint(x0, x1)
                    g[cy][x] = '.'
                    if x + 1 <= x1:
                        g[cy][x + 1] = '.'
    free = [(x, y) for y in range(1, PM_H - 1) for x in range(1, PM_W - 1) if g[y][x] == '.']
    corners = sorted(free, key=lambda p: p[0] + p[1])
    s = rng.choice(corners[:6])
    gl = rng.choice(corners[-6:])
    if rng.random() < 0.5:                    # or the other diagonal
        corners = sorted(free, key=lambda p: (PM_W - p[0]) + p[1])
        s, gl = rng.choice(corners[:6]), rng.choice(corners[-6:])
    if rng.random() < 0.5:
        s, gl = gl, s
    g[s[1]][s[0]] = 'S'
    g[gl[1]][gl[0]] = 'G'

    def near(p, q, r):
        return abs(p[0] - q[0]) <= r and abs(p[1] - q[1]) <= r

    def spots():
        return [(x, y) for y in range(1, PM_H - 1) for x in range(1, PM_W - 1)
                if g[y][x] == '.' and not near((x, y), s, 2) and not near((x, y), gl, 1)]

    def open_spots(r):                        # hazards: no wall (nor another hazard) this near, the ways stay wide
        return [(x, y) for (x, y) in spots()
                if all(g[y + dy][x + dx] not in '#DO*HU' for dx in range(-r, r + 1) for dy in range(-r, r + 1)
                       if 0 <= x + dx < PM_W and 0 <= y + dy < PM_H)]

    def patch(ch, n):
        for _ in range(n):
            sp = spots()
            if not sp:
                return
            x, y = rng.choice(sp)
            w, h = rng.randint(2, 5), rng.randint(2, 4)
            for yy in range(y, min(y + h, PM_H - 1)):
                for xx in range(x, min(x + w, PM_W - 1)):
                    if g[yy][xx] == '.' and not near((xx, yy), s, 1) and not near((xx, yy), gl, 1):
                        g[yy][xx] = ch
    if rng.random() < 0.3 + diff * 0.4:
        patch('i', rng.randint(1, 2))
    if rng.random() < 0.3:
        patch('m', rng.randint(1, 2))
    pits = int(2 + diff * 14)
    for _ in range(rng.randint(pits // 2, pits)):
        sp = open_spots(1)
        if sp:
            x, y = rng.choice(sp)
            g[y][x] = 'O'
    if rng.random() < 0.15 + diff * 0.4:
        for _ in range(rng.randint(2, 6)):
            sp = open_spots(1)
            if sp:
                x, y = rng.choice(sp)
                g[y][x] = '*'
    if diff > 0.5 and rng.random() < 0.35:     # a moving pit or two, in the open
        for _ in range(rng.randint(1, 2)):
            sp = open_spots(2)
            if sp:
                x, y = rng.choice(sp)
                g[y][x] = rng.choice('HU')
    for _ in range(rng.randint(0, 4)):
        sp = spots()
        if sp:
            x, y = rng.choice(sp)
            g[y][x] = 'c'
    if diff > 0.3 and kind != 'open' and rng.random() < 0.4:   # a key and a door in a gap
        gaps = [(x, y) for y in range(1, PM_H - 1) for x in range(1, PM_W - 1)
                if g[y][x] == '.' and ((g[y][x - 1] == '#' and g[y][x + 1] == '#') or (g[y - 1][x] == '#' and g[y + 1][x] == '#'))]
        sp = spots()
        if gaps and sp:
            x, y = rng.choice(gaps)
            g[y][x] = 'D'
            x, y = rng.choice(sp)
            g[y][x] = 'k'
    return [c for row in g for c in row]


def pm_gen(n, rng, log):
    found, names = [], set()
    tries = 0
    while len(found) < n and tries < n * 300:
        tries += 1
        diff = len(found) / max(1, n - 1)
        m = pm_make(rng, diff)
        if pm_check(m):
            continue
        d = pm_path(m)
        if d < 14:
            continue
        name = ''
        for _ in range(50):
            name = rng.choice(PM_A) + ' ' + rng.choice(PM_B)
            if len(name) <= PM_NAME and name not in names:
                break
        if len(name) > PM_NAME or name in names:
            continue
        names.add(name)
        t = name + '|' + '|'.join(''.join(m[y * PM_W:(y + 1) * PM_W]) for y in range(PM_H))
        score = d + 6 * m.count('O') + 3 * m.count('*') + 10 * (m.count('H') + m.count('U')) + 8 * m.count('D') + m.count('i')
        found.append((score, t, d))
        log('pumpkin: %d levels' % len(found))
    found.sort()
    return [(t, 'way %d tiles, difficulty %d' % (d, sc)) for sc, t, d in found]


# ---------------------------------------------------------------- Block Puzzle
BK_SHAPES = [  # w, h, weight, rows (bit 0 = left) - game_blocks.h BK_SHAPES
    (1, 1, 2, [1]), (2, 1, 3, [3]), (1, 2, 3, [1, 1]), (3, 1, 3, [7]), (1, 3, 3, [1, 1, 1]),
    (4, 1, 2, [15]), (1, 4, 2, [1, 1, 1, 1]), (5, 1, 1, [31]), (1, 5, 1, [1, 1, 1, 1, 1]),
    (2, 2, 3, [3, 3]), (3, 3, 1, [7, 7, 7]), (3, 2, 2, [7, 7]), (2, 3, 2, [3, 3, 3]),
    (2, 2, 3, [3, 1]), (2, 2, 3, [3, 2]), (2, 2, 3, [1, 3]), (2, 2, 3, [2, 3]),
    (2, 3, 2, [1, 1, 3]), (2, 3, 2, [2, 2, 3]), (2, 3, 2, [3, 1, 1]), (2, 3, 2, [3, 2, 2]),
    (3, 2, 2, [7, 1]), (3, 2, 2, [7, 4]), (3, 2, 2, [1, 7]), (3, 2, 2, [4, 7]),
    (3, 2, 2, [7, 2]), (3, 2, 2, [2, 7]), (2, 3, 2, [1, 3, 1]), (2, 3, 2, [2, 3, 2]),
    (3, 2, 2, [6, 3]), (3, 2, 2, [3, 6]), (2, 3, 2, [1, 3, 2]), (2, 3, 2, [2, 3, 1]),
    (3, 3, 1, [7, 1, 1]), (3, 3, 1, [7, 4, 4]), (3, 3, 1, [1, 1, 7]), (3, 3, 1, [4, 4, 7]),
]
FULL = (1 << 64) - 1
BK_MASK = []
for _w, _h, _wt, _r in BK_SHAPES:
    _m = 0
    for _i, _v in enumerate(_r):
        _m |= _v << (_i * 8)
    BK_MASK.append(_m)
ROWS = [0xFF << (8 * j) for j in range(8)]
COLS = [0x0101010101010101 << j for j in range(8)]
LINES = ROWS + COLS
NOT_COL0 = FULL & ~COLS[0]
NOT_COL7 = FULL & ~COLS[7]
GOALS = ['gems', 'blocks', 'score']


def popc(v):
    return bin(v).count('1')


def bk_parse(t):
    """bkParse: (goal, moves, target, map) or a reason (str)."""
    if len(t.encode()) > LINE_MAX:
        return 'line too long'
    g = next((i for i, w in enumerate(GOALS) if t.startswith(w + ' ')), None)
    if g is None:
        return 'goal: gems, blocks or score'
    rest = t[len(GOALS[g]) + 1:].lstrip(' \t')
    parts = rest.split(' ', 1)
    if len(parts) < 2 or not parts[0].isdigit():
        return 'moves expected'
    mv = int(parts[0])
    rest = parts[1].lstrip(' \t')
    parts = rest.split(' ', 1)
    if len(parts) < 2 or not parts[0].isdigit():
        return 'target expected'
    tg = int(parts[0])
    board = parts[1]
    if not 1 <= mv <= 99:
        return 'moves 1..99'
    if tg > 60000:
        return 'target too big'
    rows = board.split('|')
    if len(rows) != 8 or any(len(r) != 8 for r in rows):
        return 'the board: 8 rows of 8 split by |'
    m = ''.join(rows)
    if any(c not in '.xgsi' for c in m):
        return 'cells: . x g s i'
    if m.count('.') < 8:
        return '8 free cells at least'
    if g == 0 and not m.count('g'):
        return 'goal gems: a gem is needed'
    if g == 1 and not (m.count('s') + m.count('i')):
        return 'goal blocks: a stone or ice is needed'
    if g == 2 and (not tg or tg > mv * 100):
        return 'goal score: target 1 .. 100 a move'
    return (g, mv, tg if g == 2 else 0, m)


class Bk:
    """The board as game_blocks.h keeps it, kinds as bit masks."""
    __slots__ = ('b', 'gem', 'stone', 'ice', 'crack', 'gems', 'blocks', 'score', 'moves', 'streak', 'lines', 'tray')

    def copy(self):
        o = Bk()
        for k in Bk.__slots__:
            setattr(o, k, getattr(self, k))
        o.tray = list(self.tray)
        return o


def bk_fits(b, s, r, c):
    w, h = BK_SHAPES[s][0], BK_SHAPES[s][1]
    if r < 0 or c < 0 or r + h > 8 or c + w > 8:
        return False
    return not (b & (BK_MASK[s] << (r * 8 + c)))


def bk_fits_any(b, s):
    return any(bk_fits(b, s, r, c) for r in range(8) for c in range(8))


def bk_place(k, s, r, c):
    m = BK_MASK[s] << (r * 8 + c)
    k.b |= m
    k.gem &= ~m
    k.stone &= ~m
    k.ice &= ~m
    k.crack &= ~m
    k.moves += 1
    pts = popc(BK_MASK[s])
    full, n = 0, 0
    for L in LINES:
        if k.b & L == L:
            full |= L
            n += 1
    k.lines = n
    if not n:
        k.streak = 0
        k.score += pts
        return
    k.streak += 1
    pts += 10 * n * n * min(k.streak, 5)
    ice = k.ice & full
    k.ice &= ~full
    k.crack |= ice
    gone = full & ~ice
    k.gems = max(0, k.gems - popc(k.gem & gone))
    k.blocks = max(0, k.blocks - popc((k.stone | k.crack) & gone))
    k.gem &= ~gone
    k.stone &= ~gone
    k.crack &= ~gone
    k.b &= ~gone
    k.score += pts


def bk_eval(k, was, adv=True):
    v = (was.gems - k.gems) * 1000 + (was.blocks - k.blocks) * 700 + k.lines * 120 + (k.score - was.score) * (3 if adv else 1)
    v += 300 * popc(was.ice & k.crack)
    e = ~k.b & FULL
    edges = popc((e ^ (e >> 1)) & NOT_COL7 & 0x7F7F7F7F7F7F7F7F) + popc((e ^ (e >> 8)) & ((1 << 56) - 1))
    nb = ((e >> 1) & NOT_COL7) | ((e << 1) & NOT_COL0) | (e >> 8) | ((e << 8) & FULL)
    holes = popc(e & ~nb)
    v -= edges * 6 + holes * 40
    goal = k.b & (k.gem | k.stone | k.ice | k.crack)
    for L in LINES:
        if goal & L:
            f = popc(k.b & L)
            v += f * f * 4
    return v


def bk_pick(rng):
    tot = sum(s[2] for s in BK_SHAPES)
    v = rng.randrange(tot)
    for i, s in enumerate(BK_SHAPES):
        if v < s[2]:
            return i
        v -= s[2]
    return 0


def bk_deal(k, rng):
    for i in range(3):
        s = bk_pick(rng)
        t = 0
        while t < 16 and not bk_fits_any(k.b, s):
            s = bk_pick(rng)
            t += 1
        k.tray[i] = s if bk_fits_any(k.b, s) else 0


def bk_start(lv):
    goal, mv, tg, m = lv
    k = Bk()
    k.b = k.gem = k.stone = k.ice = k.crack = 0
    for i, ch in enumerate(m):
        bit = 1 << i
        if ch != '.':
            k.b |= bit
        if ch == 'g':
            k.gem |= bit
        if ch == 's':
            k.stone |= bit
        if ch == 'i':
            k.ice |= bit
    k.gems = popc(k.gem)
    k.blocks = popc(k.stone | k.ice)
    k.score = k.moves = k.streak = k.lines = 0
    k.tray = [255, 255, 255]
    return k


def bk_won(k, lv):
    goal, mv, tg, m = lv
    return not k.gems if goal == 0 else (not k.blocks if goal == 1 else k.score >= tg)


def bk_play(lv, rng, max_moves=99):
    """The test bot (sim.cpp bkSimRound): the best move each time; returns the
    moves it took to reach the goal (None = never) and the score then."""
    k = bk_start(lv)
    bk_deal(k, rng)
    for _ in range(max_moves):
        if bk_won(k, lv):
            return k.moves, k.score
        best, mv = None, None
        for si, s in enumerate(k.tray):
            if s == 255:
                continue
            for r in range(8):
                for c in range(8):
                    if not bk_fits(k.b, s, r, c):
                        continue
                    t = k.copy()
                    bk_place(t, s, r, c)
                    v = bk_eval(t, k)
                    if best is None or v > best:
                        best, mv = v, (si, s, r, c)
        if mv is None:
            return None, k.score
        si, s, r, c = mv
        bk_place(k, s, r, c)
        k.tray[si] = 255
        if k.tray == [255, 255, 255]:
            bk_deal(k, rng)
    return (k.moves, k.score) if bk_won(k, lv) else (None, k.score)


def bk_wins(lv, seed=1, games=12):
    """Games of 12 the bot wins within the level's moves."""
    w = 0
    for g in range(games):
        n, _ = bk_play(lv, random.Random(seed * 1000 + g), lv[1])
        if n is not None and n <= lv[1]:
            w += 1
    return w


def bk_make(rng, goal):
    cells = ['.'] * 64
    sym = rng.choice(['lr', 'quad', 'none', 'lr'])

    def put(i, ch):
        r, c = divmod(i, 8)
        spots = {(r, c)}
        if sym in ('lr', 'quad'):
            spots.add((r, 7 - c))
        if sym == 'quad':
            spots |= {(7 - r, c), (7 - r, 7 - c)}
        for (rr, cc) in spots:
            cells[rr * 8 + cc] = ch
    for _ in range(rng.randint(2, 9)):
        put(rng.randrange(64), 'x')
    if goal == 0:
        for _ in range(rng.randint(1, 3)):
            put(rng.randrange(64), 'g')
    elif goal == 1:
        for _ in range(rng.randint(1, 3)):
            put(rng.randrange(64), rng.choice('ssi'))
    else:
        for _ in range(rng.randint(0, 4)):
            put(rng.randrange(64), 'x')
    return ''.join(cells)


def bk_gen(n, rng, log):
    found = []
    tries = 0
    seen = set()
    while len(found) < n and tries < n * 40:
        tries += 1
        goal = (len(found) + tries) % 3
        m = bk_make(rng, goal)
        if m in seen or m.count('.') < 24:
            continue
        seen.add(m)
        lvl = (goal, 99, 1 if goal == 2 else 0, m)
        if isinstance(bk_parse(bk_line(lvl)), str) and goal != 2:
            continue
        need, scores = [], []
        for g in range(12):
            took, _ = bk_play(lvl, random.Random(tries * 100 + g), 60)
            need.append(took if took is not None else 999)
        if goal == 2:
            mv = rng.randint(12, 26)
            for g in range(12):
                _, sc = bk_play((2, mv, 60000, m), random.Random(tries * 100 + g), mv)
                scores.append(sc)
            scores.sort()
            tg = max(30, scores[len(scores) // 3] // 10 * 10)
            lvl = (2, mv, tg, m)
        else:
            need.sort()
            if need[5] >= 60:
                continue
            mv = min(99, need[5] + rng.randint(2, 5))       # the bot's median, a little slack
            if mv < 6:
                continue
            lvl = (goal, mv, 0, m)
        t = bk_line(lvl)
        if isinstance(bk_parse(t), str):
            continue
        w = bk_wins(bk_parse(t), seed=7 + tries)
        if w < 6:
            continue
        cnt = m.count('g') + m.count('s') + 2 * m.count('i')
        found.append((lvl[1] + cnt * 2 - w, t, w))
        log('blocks: %d levels' % len(found))
    found.sort()
    return [(t, 'test bot wins %d of 12' % w) for _, t, w in found]


def bk_line(lv):
    goal, mv, tg, m = lv
    return '%s %d %d %s' % (GOALS[goal], mv, tg, '|'.join(m[i:i + 8] for i in range(0, 64, 8)))


# ---------------------------------------------------------------- check / write
def check_level(game, t):
    """(why it is left out by the console or None, a note, extra problem or None)."""
    if game == 'sokoban':
        why = sk_valid(t)
        if why:
            return why, '', None
        p = sk_solve(t)
        _, _, _, _, boxes, _ = sk_parse(t)
        if p < 0:
            return None, '', 'no solution found'
        return None, 'boxes %d, pushes %d' % (len(boxes), p), None
    if game == 'pumpkin':
        name, m, why = pm_parse(t)
        if why:
            return why, '', None
        return None, '%-12s way %d tiles' % (name, pm_path(m)), None
    lv = bk_parse(t)
    if isinstance(lv, str):
        return lv, '', None
    w = bk_wins(lv)
    note = '%s, %d moves, test bot wins %d of 12' % (GOALS[lv[0]], lv[1], w)
    return None, note, None if w >= 3 else 'the test bot reaches the goal in %d of 12 games only' % w


def game_of(path):
    b = os.path.basename(path).lower()
    for g in ('sokoban', 'pumpkin', 'blocks'):
        if b.startswith(g):
            return g
    return None


def cmd_check(path, game, quiet=False):
    game = game or game_of(path)
    if not game:
        print('which game? --game sokoban|pumpkin|blocks')
        return 2
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    lines = pack_lines(text)
    good = bad = extra = 0
    for n, t in lines:
        if good + bad >= PACK_MAX:
            print('line %d: more than %d levels, the rest is not read' % (n, PACK_MAX))
            break
        why, note, prob = check_level(game, t)
        if why:
            bad += 1
            print('line %d: left out by the console: %s' % (n, why))
        else:
            good += 1
            if prob:
                extra += 1
                print('line %d: %s' % (n, prob))
            elif not quiet:
                print('line %d: level %d ok, %s' % (n, good, note))
    print('%s: %d levels, %d left out, %d failing the extra checks' % (path, good, bad, extra))
    return 1 if bad or extra else 0


HEAD = {
    'sokoban': '# MiniArcade Color - Sokoban levels for the SD card (made by tools/levelpack.py)\n'
               '# one level a line, rows split by a bar: # wall, space floor, . goal, $ box, * box on a goal,\n'
               '# @ player, + player on a goal. Every level here is solved by the tool; easiest first.\n',
    'pumpkin': '# MiniArcade Color - Pumpkin Maze levels for the SD card (made by tools/levelpack.py)\n'
               '# NAME (12 at most), then 13 rows of 20 tiles, each after a bar: . floor # wall S start G goal O pit i ice m mud\n'
               '# * bumper > < ^ v arrows c candy k key D door C rune H U moving pits. Goal reached: checked.\n',
    'blocks': '# MiniArcade Color - Block Puzzle adventure levels for the SD card (made by tools/levelpack.py)\n'
              '# goal moves target board: goal gems / blocks / score, board 8 rows of 8 split by bars:\n'
              '# . free, x block, g gem, s stone, i ice. The tool\'s test bot reaches every goal.\n',
}


def write_pack(path, game, levels):
    assert all('|' not in l for l in HEAD[game].split('\n')), 'a comment must not hold a bar'
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(HEAD[game])
        for i, (t, note) in enumerate(levels, 1):
            f.write('# %d: %s\n%s\n' % (i, note, t))


def gen(game, n, seed, log):
    rng = random.Random(seed)
    return {'sokoban': sk_gen, 'pumpkin': pm_gen, 'blocks': bk_gen}[game](n, rng, log)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check', help='check a pack as the console does, and more')
    c.add_argument('file')
    c.add_argument('--game', choices=['sokoban', 'pumpkin', 'blocks'])
    c.add_argument('-q', '--quiet', action='store_true', help='only problems and the sum')
    g = sub.add_parser('gen', help='make a pack')
    g.add_argument('game', choices=['sokoban', 'pumpkin', 'blocks'])
    g.add_argument('-n', type=int, default=20)
    g.add_argument('--seed', type=int, default=1)
    g.add_argument('-o', '--out')
    s = sub.add_parser('samples', help='the three sample packs (40 / 30 / 20 levels) into DIR')
    s.add_argument('dir')
    s.add_argument('--seed', type=int, default=2026)
    a = ap.parse_args()

    def log(msg):
        sys.stderr.write('\r' + msg + '   ')
        sys.stderr.flush()
    if a.cmd == 'check':
        return cmd_check(a.file, a.game, a.quiet)
    if a.cmd == 'gen':
        lv = gen(a.game, a.n, a.seed, log)
        sys.stderr.write('\n')
        out = a.out or a.game + '.txt'
        write_pack(out, a.game, lv)
        return cmd_check(out, a.game, quiet=True)
    os.makedirs(a.dir, exist_ok=True)
    rc = 0
    for game, n in (('sokoban', 40), ('pumpkin', 30), ('blocks', 20)):
        lv = gen(game, n, a.seed, log)
        sys.stderr.write('\n')
        out = os.path.join(a.dir, game + '.txt')
        write_pack(out, game, lv)
        rc |= cmd_check(out, game, quiet=True)
    return rc


if __name__ == '__main__':
    sys.exit(main())
