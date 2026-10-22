#!/usr/bin/env python3
"""Kart GP art: renders the kart from 16 sides, the items and the sprites
by the track, the HUD icons, the trophy and the logo, and writes them to
main/kart_art.h - 4 bits a pixel (0 = see-through), the palettes as rgb().

The kart, the item box, the coin and the rocket are little models of boxes,
cylinders and spheres, ray traced from the camera's height (28 degrees
down) and shaded from the upper left, with a dark outline round them. The
rest is drawn pixel by pixel here. Run it again after a change:

    python3 tools/kart_art.py            (needs numpy and Pillow)
    python3 tools/kart_art.py --preview  (also kart_art.png to look at)
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'main', 'kart_art.h')
PITCH = math.radians(28)

# ---------------------------------------------------------------- ray tracing
class Box:
    def __init__(s, lo, hi, slot): s.lo, s.hi, s.slot = np.array(lo, float), np.array(hi, float), slot
    def hit(s, O, D):
        with np.errstate(divide='ignore', invalid='ignore'):
            t1 = (s.lo - O) / D; t2 = (s.hi - O) / D
        tmin = np.minimum(t1, t2); tmax = np.maximum(t1, t2)
        tn = np.max(tmin, axis=-1); tf = np.min(tmax, axis=-1)
        ok = (tn <= tf) & (tf > 0)
        t = np.where(ok, tn, np.inf)
        ax = np.argmax(tmin, axis=-1)
        n = np.zeros(O.shape)
        sign = -np.sign(np.take_along_axis(D, ax[..., None], -1)[..., 0])
        np.put_along_axis(n, ax[..., None], sign[..., None], -1)
        return t, n, None

class Sphere:
    def __init__(s, c, r, slot): s.c, s.r, s.slot = np.array(c, float), r, slot
    def hit(s, O, D):
        oc = O - s.c
        b = np.sum(oc * D, -1); c = np.sum(oc * oc, -1) - s.r * s.r
        disc = b * b - c
        t = np.where(disc >= 0, -b - np.sqrt(np.maximum(disc, 0)), np.inf)
        t = np.where(t > 0, t, np.inf)
        P = O + D * np.where(np.isfinite(t), t, 0)[..., None]
        n = (P - s.c) / s.r
        return t, n, P

class Cyl:  # axis 0 (x) or 1 (y); c = centre, r = radius, h = half length
    def __init__(s, c, r, h, axis, slot, hub=None): s.c, s.r, s.h, s.ax, s.slot, s.hub = np.array(c, float), r, h, axis, slot, hub
    def hit(s, O, D):
        a = s.ax; o = [k for k in range(3) if k != a]
        oc = O - s.c
        dx, dy = D[..., o[0]], D[..., o[1]]; ox, oy = oc[..., o[0]], oc[..., o[1]]
        A = dx * dx + dy * dy; B = ox * dx + oy * dy; C = ox * ox + oy * oy - s.r * s.r
        disc = B * B - A * C
        with np.errstate(divide='ignore', invalid='ignore'):
            ts = np.where((disc >= 0) & (A > 1e-9), (-B - np.sqrt(np.maximum(disc, 0))) / A, np.inf)
        za = oc[..., a] + D[..., a] * np.where(np.isfinite(ts), ts, 0)
        ts = np.where((np.abs(za) <= s.h) & (ts > 0), ts, np.inf)
        # caps
        with np.errstate(divide='ignore', invalid='ignore'):
            tc1 = (s.h - oc[..., a]) / D[..., a]; tc2 = (-s.h - oc[..., a]) / D[..., a]
        tcs = []
        for tc in (tc1, tc2):
            px = ox + dx * np.where(np.isfinite(tc), tc, 0); py = oy + dy * np.where(np.isfinite(tc), tc, 0)
            tcs.append(np.where((px * px + py * py <= s.r * s.r) & (tc > 0), tc, np.inf))
        tc = np.minimum(tcs[0], tcs[1])
        t = np.minimum(ts, tc)
        P = O + D * np.where(np.isfinite(t), t, 0)[..., None]
        n = np.zeros(O.shape)
        side = ts <= tc
        rel = P - s.c
        n[..., o[0]] = np.where(side, rel[..., o[0]] / s.r, 0)
        n[..., o[1]] = np.where(side, rel[..., o[1]] / s.r, 0)
        n[..., a] = np.where(side, 0, np.sign(rel[..., a]))
        return t, n, P

def cam_basis(yaw, pitch=PITCH):
    """yaw: the kart's heading minus the direction from the camera to it (0 = we see its back)."""
    V = np.array([math.cos(-yaw) * math.cos(pitch), math.sin(-yaw) * math.cos(pitch), -math.sin(pitch)])
    R = np.array([math.sin(yaw), math.cos(yaw), 0.0])
    U = np.array([math.sin(pitch) * math.cos(-yaw), math.sin(pitch) * math.sin(-yaw), math.cos(pitch)])
    return V, R, U

def trace(prims, yaw, scale, shade, size=64, pitch=PITCH):
    """-> palette index image (size x size), anchor (the origin) in the middle"""
    V, R, U = cam_basis(yaw, pitch)
    L = -0.45 * V - 0.55 * R + 0.75 * U
    L /= np.linalg.norm(L)
    j, i = np.mgrid[0:size, 0:size]
    sx = (i + 0.5 - size / 2) / scale; sy = (size * 0.7 - j - 0.5) / scale
    O = R * sx[..., None] + U * sy[..., None] - V * 60
    D = np.broadcast_to(V, O.shape)
    best = np.full(sx.shape, np.inf); out = np.zeros(sx.shape, np.uint8)
    for p in prims:
        t, n, P = p.hit(O, D)
        if P is None: P = O + D * np.where(np.isfinite(t), t, 0)[..., None]
        lit = np.clip(np.sum(n * L, -1), 0, 1)
        idx = shade(p, lit, P, n)
        m = t < best
        best = np.where(m, t, best); out = np.where(m, idx, out)
    return out, (size // 2, int(size * 0.7))

def outline(img, idx):
    o = img.copy()
    h, w = img.shape
    for y in range(h):
        for x in range(w):
            if img[y, x]: continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and 0 <= yy < h and img[yy, xx] and img[yy, xx] != idx: o[y, x] = idx; break
    return o

def crop(img, anchor):
    ys, xs = np.nonzero(img)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    if anchor is None: anchor = (x0, y0)                      # no foot: the top left
    return img[y0:y1, x0:x1], (anchor[0] - x0, anchor[1] - y0)

# ---------------------------------------------------------------- the kart
# kart space in world px: x forward, y right, z up, the origin on the ground under its middle
BODY, ACC, SUIT, HELM, VISOR, TYRE, METAL, DARK = range(8)
def kart_model():
    P = []
    P.append(Box([-3.5, -1.9, 0.55], [3.3, 1.9, 1.5], BODY))          # tub
    P.append(Box([3.2, -1.3, 0.5], [4.4, 1.3, 1.25], BODY))           # nose
    P.append(Box([3.9, -2.9, 0.25], [4.7, 2.9, 0.65], ACC))           # front wing
    P.append(Box([-1.9, -2.75, 0.55], [1.7, -1.9, 1.35], ACC))        # side pods
    P.append(Box([-1.9, 1.9, 0.55], [1.7, 2.75, 1.35], ACC))
    P.append(Box([-2.5, -1.35, 1.4], [-1.3, 1.35, 2.7], DARK))        # seat back
    P.append(Box([-1.7, -1.05, 1.4], [-0.3, 1.05, 2.9], SUIT))        # driver
    P.append(Box([0.15, -0.85, 1.9], [0.55, 0.85, 2.45], DARK))       # wheel
    P.append(Box([-0.6, -1.25, 1.75], [0.3, -0.75, 2.25], SUIT))      # arms
    P.append(Box([-0.6, 0.75, 1.75], [0.3, 1.25, 2.25], SUIT))
    P.append(Sphere([-0.95, 0, 3.55], 1.15, HELM))                     # helmet
    P.append(Box([-4.2, -1.2, 1.35], [-2.5, 1.2, 2.1], METAL))        # engine
    P.append(Cyl([-4.3, -0.7, 1.8], 0.32, 0.55, 0, METAL))            # exhausts
    P.append(Cyl([-4.3, 0.7, 1.8], 0.32, 0.55, 0, METAL))
    P.append(Box([-4.75, -2.7, 2.6], [-3.95, 2.7, 3.0], ACC))         # rear wing
    P.append(Box([-4.5, -1.6, 1.6], [-4.2, -1.3, 2.6], METAL))
    P.append(Box([-4.5, 1.3, 1.6], [-4.2, 1.6, 2.6], METAL))
    for x, y, r, h in ((2.6, 2.55, 0.9, 0.6), (2.6, -2.55, 0.9, 0.6), (-2.6, 2.5, 1.05, 0.75), (-2.6, -2.5, 1.05, 0.75)):
        P.append(Cyl([x, y, r], r, h, 1, TYRE, hub=0.42))
    return P

def kart_shade(p, lit, P, n):
    s = p.slot
    if s == BODY: return np.where(lit > 0.62, 1, np.where(lit > 0.28, 2, 3))
    if s == ACC: return np.where(lit > 0.45, 4, 5)
    if s == SUIT: return np.where(lit > 0.45, 6, 7)
    if s == HELM:
        rel = (P - p.c) / p.r
        visor = (rel[..., 0] > 0.35) & (np.abs(rel[..., 2] - 0.05) < 0.42)
        hel = np.where(lit > 0.5, 8, 9)
        return np.where(visor, np.where(lit > 0.8, 11, 10), hel)
    if s == TYRE:
        rel = P - p.c
        cap = np.abs(n[..., 1]) > 0.5
        hub = cap & (rel[..., 0] ** 2 + rel[..., 2] ** 2 < p.hub ** 2)
        return np.where(hub, 14, np.where(lit > 0.6, 13, 12))
    if s == METAL: return np.where(lit > 0.4, 14, 12)
    return np.full(lit.shape, 15)

# ---------------------------------------------------------------- items
def box_model(rot):
    # a cube turned round z by rot, a little tilted: drawn by turning the camera instead
    return [Box([-2.4, -2.4, 0.6], [2.4, 2.4, 5.4], 0)]

def box_shade_for(rot):
    def sh(p, lit, P, n):
        # n is in the cube's own space: which face, and where on it (the "?" and the frame)
        rel = P - np.array([0, 0, 3.0])
        face = np.where(np.abs(n[..., 2]) > 0.5, 0, np.where(np.abs(n[..., 0]) > 0.5, np.where(n[..., 0] > 0, 1, 2), np.where(n[..., 1] > 0, 3, 4)))
        # coordinates on the face
        u = np.where(np.abs(n[..., 0]) > 0.5, rel[..., 1], rel[..., 0])
        v = np.where(np.abs(n[..., 2]) > 0.5, rel[..., 1], rel[..., 2])
        edge = (np.maximum(np.abs(u), np.abs(v)) > 1.95)
        q = np.zeros(lit.shape, bool)
        # a "?" from a few strokes (u right, v up) on the side faces
        side = np.abs(n[..., 2]) < 0.5
        uu, vv = u, v
        arc = (np.abs(np.hypot(uu, vv - 0.55) - 0.75) < 0.28) & ((vv > 0.35) | (uu > 0.1))
        stem = (np.abs(uu - 0.0) < 0.28) & (vv > -0.55) & (vv < 0.0)
        dot = np.hypot(uu, vv + 1.15) < 0.3
        q = side & (arc | stem | dot)
        base = 1 + ((face + (lit > 0.6)) % 4)
        return np.where(edge, np.where(lit > 0.5, 6, 7), np.where(q, 5, base))
    return sh

def coin_model():
    return [Cyl([0, 0, 2.6], 1.9, 0.35, 0, 0)]

def coin_shade(p, lit, P, n):
    rel = P - p.c
    cap = np.abs(n[..., 0]) > 0.5
    r = np.hypot(rel[..., 1], rel[..., 2])
    inner = cap & (r < 1.25)
    star = cap & (np.abs(rel[..., 1]) < 0.22) & (np.abs(rel[..., 2]) < 0.85)
    return np.where(~cap, 4, np.where(star, 5, np.where(inner, np.where(lit > 0.5, 2, 3), np.where(lit > 0.55, 1, 2))))

def rocket_model():
    P = [Cyl([0, 0, 1.6], 0.6, 1.5, 0, 0), Sphere([1.45, 0, 1.6], 0.6, 1), Cyl([-1.5, 0, 1.6], 0.45, 0.2, 0, 2)]
    P.append(Box([-1.5, -1.25, 1.5], [-0.6, 1.25, 1.7], 3))
    P.append(Box([-1.5, -0.1, 0.4], [-0.6, 0.1, 2.8], 3))
    return P

def rocket_shade(p, lit, P, n):
    s = p.slot
    if s == 0: return np.where(lit > 0.55, 1, np.where(lit > 0.25, 2, 3))
    if s == 1: return np.where(lit > 0.5, 4, 5)
    if s == 2: return np.full(lit.shape, 6)
    return np.where(lit > 0.45, 4, 5)

# ---------------------------------------------------------------- 2D sprites
def blank(w, h): return np.zeros((h, w), np.uint8)

def disc_shade(img, cx, cy, r, cols, light=(-0.6, -0.8), only_empty=False):
    h, w = img.shape
    for y in range(max(0, int(cy - r - 1)), min(h, int(cy + r + 2))):
        for x in range(max(0, int(cx - r - 1)), min(w, int(cx + r + 2))):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy)
            if d > r: continue
            if only_empty and img[y, x]: continue
            nz = math.sqrt(max(0, 1 - (d / r) ** 2))
            l = (dx / r) * light[0] + (dy / r) * light[1] + nz * 0.5
            k = 0 if l > 0.75 else 1 if l > 0.35 else 2 if l > -0.1 else 3
            img[y, x] = cols[min(k, len(cols) - 1)]

def tree():
    im = blank(30, 44)
    for y in range(26, 44):                                   # trunk
        for x in range(13, 18): im[y, x] = 7 if x < 15 else 6
    for x in range(11, 20): im[43, x] = 6
    for cx, cy, r in ((15, 22, 9), (9, 18, 7), (21, 17, 7), (15, 10, 8), (10, 26, 5.5), (21, 25, 5.5)):
        disc_shade(im, cx, cy, r, [5, 4, 3, 2])
    rng = np.random.default_rng(3)
    for _ in range(26):
        x, y = rng.integers(3, 27), rng.integers(2, 34)
        if im[y, x] in (3, 4) and im[y - 1, x] in (3, 4, 5): im[y, x] = 5 if im[y, x] == 4 else 4
    return outline(im, 1), (15, 43)

def bush():
    im = blank(22, 14)
    for cx, cy, r in ((7, 8, 5.5), (14, 8, 5.5), (11, 5, 5)):
        disc_shade(im, cx, cy, r, [5, 4, 3, 2])
    for x, y in ((5, 5), (9, 3), (14, 4), (16, 8), (8, 9), (12, 9)): im[y, x] = 8
    return outline(im, 1), (11, 13)

def pine():
    im = blank(26, 46)
    for y in range(38, 46):
        for x in range(11, 15): im[y, x] = 7 if x < 13 else 6
    tiers = ((13, 2, 9, 18), (13, 12, 11, 30), (13, 22, 13, 40))
    for cx, top, hw, bot in tiers:
        for y in range(top, bot):
            f = (y - top) / (bot - top)
            half = 1 + hw * f
            for x in range(int(cx - half), int(cx + half) + 1):
                if 0 <= x < 26:
                    dx = (x - cx) / max(1, half)
                    k = 4 if dx < -0.35 else 3 if dx < 0.3 else 2
                    if y - top < 2 + (1 - abs(dx)) * 2.5 or (y - top) % 6 == 0 and abs(dx) > 0.5: k = 9 if dx < 0 else 8   # snow
                    im[y, x] = k
    return outline(im, 1), (13, 45)

def palm():
    im = blank(36, 48)
    # a bent trunk with rings
    for k in range(0, 34):
        y = 47 - k
        x = 18 + int(4 * math.sin(k / 34 * 1.6))
        for dx in range(-2, 2):
            im[y, x + dx] = 7 if dx < 0 else 6
        if k % 4 == 0: im[y, x - 2] = 6; im[y, x + 1] = 1
    top = (18 + int(4 * math.sin(1.6)), 13)
    for ang, ln in ((-170, 15), (-140, 16), (-100, 12), (-60, 14), (-25, 16), (10, 14), (160, 13)):
        a = math.radians(ang)
        for s in range(ln):
            t = s / ln
            x = top[0] + math.cos(a) * s
            y = top[1] + math.sin(a) * s + 9 * t * t
            for w in range(-1, 2):
                yy, xx = int(y + w * 0.5), int(x)
                if 0 <= xx < 36 and 0 <= yy < 48:
                    im[yy, xx] = 4 if w < 0 else 3 if w == 0 else 2
            if s % 2 == 0 and s > 2:
                for d in (2, 3):
                    yy, xx = int(y + d * 0.8), int(x - math.sin(a) * 0.5)
                    if 0 <= xx < 36 and 0 <= yy < 48 and not im[yy, xx]: im[yy, xx] = 2
    disc_shade(im, top[0] - 1, top[1] + 2, 2.2, [9, 8, 8, 8])
    disc_shade(im, top[0] + 2, top[1] + 2, 2.0, [9, 8, 8, 8])
    return outline(im, 1), (18, 47)

def cactus():
    im = blank(22, 38)
    def col(x0, y0, y1, r):
        for y in range(y0, y1):
            for x in range(x0 - r, x0 + r + 1):
                dx = (x - x0) / (r + 0.5)
                im[y, x] = 4 if dx < -0.45 else 3 if dx < 0.1 else 2 if dx < 0.6 else 5
        for x in range(x0 - r + 1, x0 + r):               # round top
            im[y0 - 1, x] = 3
    col(11, 6, 38, 3)
    col(4, 14, 24, 2); col(18, 10, 22, 2)
    for x in range(4, 11): im[24, x] = 3; im[23, x] = 3
    for x in range(12, 19): im[22, x] = 3; im[21, x] = 3
    for y in range(8, 36, 3): im[y, 11] = 5
    im[4, 11] = 8; im[4, 10] = 9; im[4, 12] = 9; im[3, 11] = 9
    return outline(im, 1), (11, 37)

def rock():
    im = blank(26, 18)
    for y in range(18):
        for x in range(26):
            dx, dy = (x - 13) / 12.5, (y - 11) / 8.5
            d = dx * dx + dy * dy + 0.15 * math.sin(x * 1.3) * (dy < 0)
            if d > 1 or y > 16: continue
            l = -dx * 0.6 - dy * 0.8
            im[y, x] = 2 if l > 0.4 else 3 if l > -0.05 else 4 if l > -0.5 else 5
    for x in range(4, 23):
        if im[16, x]: im[16, x] = 5
    return outline(im, 1), (13, 17)

def lamp():
    im = blank(16, 48)
    for y in range(6, 48):
        im[y, 7] = 3; im[y, 8] = 2
    for x in range(3, 13): im[47, x] = 2
    for x in range(7, 14): im[5, x] = 3; im[6, x] = 2
    for y in range(4, 9):
        for x in range(10, 15): im[y, x] = 4 if y < 6 else 5
    im[8, 11] = 6; im[8, 12] = 6; im[8, 13] = 6
    return outline(im, 1), (8, 47)

def chevron():
    im = blank(22, 20)
    for y in range(1, 11):
        for x in range(1, 21): im[y, x] = 2
    for k in range(3):
        for y in range(2, 10):
            x0 = 4 + k * 6 + abs(y - 6) // 1
            for d in range(3):
                if x0 + d < 21: im[y, x0 + d] = 3
    for y in range(11, 20):
        im[y, 4] = 4; im[y, 17] = 4; im[y, 5] = 5; im[y, 18] = 5
    return outline(im, 1), (11, 19)

def deadtree():
    im = blank(26, 40)
    def br(x, y, a, ln, w):
        for s in range(ln):
            xx = int(x + math.cos(a) * s); yy = int(y - math.sin(a) * s)
            for d in range(w):
                if 0 <= xx + d < 26 and 0 <= yy < 40: im[yy, xx + d] = 3 if d == 0 else 2
        return x + math.cos(a) * ln, y - math.sin(a) * ln
    x, y = br(12, 39, math.radians(90), 20, 3)
    br(x, y, math.radians(130), 10, 2); br(x, y, math.radians(60), 11, 2)
    br(12, 28, math.radians(30), 9, 1); br(13, 24, math.radians(150), 8, 1)
    return outline(im, 1), (13, 39)

def peel():
    im = blank(20, 15)
    # three flaps lying on the ground, the stub standing up in the middle
    for ang, ln in ((200, 9), (340, 9), (265, 6), (235, 8), (305, 8)):
        a = math.radians(ang)
        for s in range(ln):
            x = 10 + math.cos(a) * s * 1.05
            y = 8 - math.sin(a) * s * 0.55 + (s * s) * 0.03
            for w in (-1, 0, 1):
                xx, yy = int(x), int(y + w * 0.7)
                if 0 <= xx < 20 and 0 <= yy < 15:
                    im[yy, xx] = 5 if s >= ln - 1 else (2 if w < 0 else 3 if w == 0 else 4)
    for y in range(2, 9):
        for x in (9, 10, 11):
            im[y, x] = 2 if x == 9 else 3 if x == 10 else 4
    im[1, 10] = 5; im[1, 9] = 5; im[2, 10] = 5
    return outline(im, 1), (10, 14)

def icon_turbo():
    im = blank(22, 22)
    for y in range(4, 21):
        for x in range(6, 16):
            dx = (x - 10.5) / 5
            im[y, x] = 2 if dx < -0.4 else 3 if dx < 0.3 else 4
    for x in range(8, 14): im[2, x] = 5; im[3, x] = 5
    for x in range(7, 15): im[4, x] = 6
    for (x, y) in ((11, 7), (10, 8), (11, 8), (9, 9), (10, 9), (11, 9), (12, 10), (11, 11), (12, 11), (10, 12), (11, 12), (12, 12), (13, 12), (11, 13), (12, 13), (12, 14), (11, 15), (10, 16), (11, 16)):
        im[y, x] = 7
    return outline(im, 1), (11, 21)

def icon_rocket():
    im = blank(24, 14)
    for x in range(4, 19):
        for y in range(4, 10):
            dy = (y - 6.5) / 3
            im[y, x] = 2 if dy < -0.3 else 3 if dy < 0.4 else 4
    for x in range(19, 23):
        for y in range(4 + (x - 18), 10 - (x - 18)): im[y, x] = 5 if y < 7 else 6
    for k in range(4):
        im[3 - k // 2, 4 + k] = 6; im[10 + k // 2, 4 + k] = 6; im[3, 4 + k] = 6; im[10, 4 + k] = 6
    for y in range(5, 9): im[y, 2] = 7; im[y, 3] = 8
    im[6, 1] = 7; im[7, 1] = 7
    return outline(im, 1), (12, 13)

def icon_shield():
    im = blank(22, 22)
    for y in range(22):
        for x in range(22):
            dx, dy = x - 10.5, y - 10.5
            r = max(abs(dx) * 0.87 + abs(dy) * 0.5, abs(dy))
            if r <= 10:
                im[y, x] = 2 if r > 8.5 else (3 if dx + dy < -6 else 4 if dx + dy < 4 else 5)
    for x, y in ((6, 6), (7, 5), (8, 5), (6, 7), (5, 8)): im[y, x] = 6
    return outline(im, 1), (11, 21)

def trophy():
    im = blank(30, 36)
    for y in range(2, 20):
        f = (y - 2) / 18
        half = 11 - 5 * f * f
        for x in range(int(15 - half), int(15 + half) + 1):
            dx = (x - 15) / half
            im[y, x] = 2 if dx < -0.55 else 3 if dx < -0.1 else 4 if dx < 0.5 else 5
    for y in range(4, 14):                                      # handles
        for x, side in ((2, -1), (27, 1)):
            if abs(y - 9) in (4, 5) or x in (2, 27): pass
        im[y, 2] = 4; im[y, 27] = 5
    for x in range(2, 6): im[4, x] = 4; im[13, x] = 4
    for x in range(24, 28): im[4, x] = 5; im[13, x] = 5
    for y in range(20, 27):
        for x in range(13, 18): im[y, x] = 3 if x < 15 else 4
    for y in range(27, 35):
        for x in range(7, 24): im[y, x] = 6 if y < 29 else 7
    for x in range(9, 22): im[31, x] = 8; im[32, x] = 8
    for y in range(3, 12): im[y, 10] = 1 + 0 * y
    for y in range(3, 12): im[y, 10] = 2 if y % 3 else 2
    for y in range(4, 10): im[y, 9] = 2
    im[5, 8] = 2
    for y in range(4, 10): im[y, 10] = 9
    return outline(im, 1), (15, 35)

def logo():
    font = ImageFont.truetype('/usr/share/fonts/truetype/freefont/FreeSansBoldOblique.ttf', 50)
    bb = font.getbbox("KART GP")
    W, H = bb[2] - bb[0] + 24, bb[3] - bb[1] + 16
    m = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(m)
    d.text((8 - bb[0], 6 - bb[1]), "KART GP", font=font, fill=255)
    a = np.array(m) > 110
    img = np.zeros((H, W), np.uint8)
    ys = np.nonzero(a.any(1))[0]
    top, bot = ys.min(), ys.max()
    for y in range(H):
        for x in range(W):
            if a[y, x]:
                f = (y - top) / max(1, bot - top)
                img[y, x] = 2 + min(5, int(f * 6))                  # 2..7: yellow to red
    # a light edge on top of each letter
    for y in range(1, H):
        for x in range(W):
            if a[y, x] and not a[y - 1, x]: img[y, x] = 8
    # outline (2 px) and a shadow down right
    o = img.copy()
    for y in range(H):
        for x in range(W):
            if img[y, x]: continue
            near = False
            for dx in (-2, -1, 0, 1, 2):
                for dy in (-2, -1, 0, 1, 2):
                    if abs(dx) + abs(dy) > 3: continue
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < W and 0 <= yy < H and img[yy, xx] and img[yy, xx] != 1: near = True
            if near: o[y, x] = 1
    s = o.copy()
    for y in range(H):
        for x in range(W):
            if not o[y, x]:
                xx, yy = x - 4, y - 4
                if 0 <= xx < W and 0 <= yy < H and o[yy, xx]: s[y, x] = 9
    return s, None

# ---------------------------------------------------------------- palettes (rgb)
PALS = {
 'TREE':    [(10, 40, 10), (30, 90, 30), (50, 140, 40), (90, 190, 60), (150, 230, 90), (90, 60, 30), (140, 100, 50), (230, 80, 120)],
 'TREE_AU': [(40, 20, 0), (140, 60, 10), (200, 100, 20), (240, 160, 40), (255, 210, 90), (80, 50, 30), (130, 90, 50), (255, 255, 200)],
 'BUSH':    [(10, 40, 10), (30, 90, 30), (50, 140, 40), (90, 190, 60), (150, 230, 90), (90, 60, 30), (140, 100, 50), (255, 120, 180)],
 'PINE':    [(10, 30, 30), (20, 80, 60), (30, 110, 70), (50, 150, 90), (0, 0, 0), (90, 60, 40), (130, 90, 60), (200, 220, 240), (255, 255, 255)],
 'PALM':    [(30, 30, 10), (30, 110, 30), (60, 160, 40), (120, 210, 70), (0, 0, 0), (120, 80, 40), (180, 130, 70), (100, 60, 20), (150, 100, 50)],
 'CACTUS':  [(20, 40, 10), (40, 110, 50), (60, 150, 60), (30, 90, 40), (120, 200, 110), (0, 0, 0), (0, 0, 0), (255, 80, 120), (255, 200, 60)],
 'ROCK':    [(40, 25, 15), (190, 150, 110), (150, 110, 80), (110, 80, 55), (75, 50, 35)],
 'ROCK_V':  [(10, 5, 5), (110, 90, 90), (75, 60, 60), (50, 40, 40), (30, 20, 20)],
 'ROCK_I':  [(30, 40, 60), (240, 250, 255), (190, 210, 230), (140, 160, 190), (100, 120, 150)],
 'LAMP':    [(10, 10, 20), (90, 100, 120), (150, 160, 180), (255, 255, 200), (255, 220, 120), (255, 255, 255)],
 'CHEV':    [(20, 20, 20), (240, 240, 240), (230, 30, 30), (120, 120, 130), (80, 80, 90)],
 'DEAD':    [(10, 5, 5), (70, 40, 30), (110, 70, 50)],
 'PEEL':    [(60, 40, 0), (230, 190, 20), (255, 230, 60), (200, 150, 0), (100, 70, 20)],
 'TURBO':   [(10, 10, 40), (20, 60, 200), (40, 120, 255), (120, 200, 255), (200, 200, 210), (120, 120, 130), (255, 230, 60)],
 'ROCKETI': [(40, 0, 0), (255, 120, 120), (230, 30, 30), (150, 0, 0), (240, 240, 240), (180, 180, 190), (90, 90, 100), (255, 200, 0), (255, 120, 0)],
 'SHIELD':  [(0, 30, 60), (0, 140, 200), (60, 210, 255), (130, 235, 255), (200, 250, 255), (255, 255, 255)],
 'GOLD':    [(60, 40, 0), (255, 255, 160), (255, 220, 60), (230, 170, 20), (170, 110, 0), (120, 80, 20), (90, 60, 20), (255, 230, 120), (255, 255, 255)],
 'SILVER':  [(30, 30, 40), (255, 255, 255), (220, 225, 235), (170, 175, 190), (120, 125, 140), (90, 90, 100), (60, 60, 70), (230, 230, 240), (255, 255, 255)],
 'BRONZE':  [(40, 20, 0), (255, 210, 160), (220, 150, 90), (180, 110, 60), (130, 75, 35), (100, 60, 30), (70, 40, 20), (230, 170, 120), (255, 255, 255)],
 'COIN':    [(255, 250, 170), (255, 210, 40), (200, 140, 0), (150, 100, 0), (255, 255, 255)],
 'BOX':     [(255, 0, 0), (255, 0, 0), (255, 0, 0), (255, 0, 0), (255, 255, 255), (255, 255, 255), (200, 200, 220)],
 'ROCKET':  [(255, 120, 120), (230, 30, 30), (140, 0, 0), (250, 250, 250), (180, 180, 190), (255, 200, 0)],
 'LOGO':    [(20, 10, 40), (255, 250, 120), (255, 220, 40), (255, 180, 20), (255, 130, 0), (240, 80, 0), (210, 30, 10), (255, 255, 230), (60, 20, 80)],
}

# ---------------------------------------------------------------- output
def pack(img):
    h, w = img.shape
    if w % 2: img = np.concatenate([img, np.zeros((h, 1), np.uint8)], 1); w += 1
    b = []
    for y in range(h):
        for x in range(0, w, 2): b.append(int(img[y, x]) << 4 | int(img[y, x + 1]))
    return b

def carr(name, data):
    s = 'static const uint8_t %s[%d] = {' % (name, len(data))
    lines = []
    for i in range(0, len(data), 24):
        lines.append('  ' + ','.join(str(v) for v in data[i:i + 24]))
    return s + '\n' + ',\n'.join(lines) + ' };\n'

def main():
    preview = '--preview' in sys.argv
    out = ['// Kart GP art - written by tools/kart_art.py, do not edit by hand.',
           '// 4 bits a pixel, two pixels a byte (the left one high), 0 = see-through;',
           '// (ox, oy) = the point that stands on the ground, from the top left.',
           '#pragma once', '',
           'struct KgSpr { uint8_t w, h, ox, oy; const uint8_t *d; };', '']
    sheet = []
    def emit(name, img, anchor):
        img, a = crop(img, anchor)
        out.append(carr('KA_' + name, pack(img)))
        sheet.append((name, img))
        return '{ %d, %d, %d, %d, KA_%s }' % (img.shape[1], img.shape[0], a[0], a[1], name)
    # the kart from 16 sides, 5 px a world px
    km = kart_model()
    ent = []
    for k in range(16):
        img, anc = trace(km, k * math.pi / 8, 5.0, kart_shade, 80)
        img = outline(img, 15)
        ent.append(emit('KART%d' % k, img, anc))
    out.append('static const KgSpr KG_KART[16] = {\n  ' + ',\n  '.join(ent) + ' };\n')
    # the item box turning (a quarter turn is all of it), the coin, the rocket
    ent = []
    for k in range(6):
        rot = k * (math.pi / 2) / 6
        img, anc = trace(box_model(rot), rot + 0.3, 4.0, box_shade_for(rot), 64, pitch=math.radians(22))
        ent.append(emit('BOX%d' % k, outline(img, 7), anc))
    out.append('static const KgSpr KG_BOX[6] = {\n  ' + ',\n  '.join(ent) + ' };\n')
    ent = []
    for k in range(6):
        img, anc = trace(coin_model(), k * math.pi / 6, 4.0, coin_shade, 40, pitch=math.radians(10))
        ent.append(emit('COIN%d' % k, outline(img, 3), anc))
    out.append('static const KgSpr KG_COIN[6] = {\n  ' + ',\n  '.join(ent) + ' };\n')
    ent = []
    for k in range(8):
        img, anc = trace(rocket_model(), k * math.pi / 4, 4.0, rocket_shade, 40)
        ent.append(emit('RKT%d' % k, outline(img, 3), anc))
    out.append('static const KgSpr KG_ROCKET[8] = {\n  ' + ',\n  '.join(ent) + ' };\n')
    # by the track, 2 px a world px
    names = []
    for nm, fn in (('TREE', tree), ('BUSH', bush), ('PINE', pine), ('PALM', palm), ('CACTUS', cactus), ('ROCK', rock),
                   ('LAMP', lamp), ('CHEV', chevron), ('DEAD', deadtree), ('PEEL', peel),
                   ('ITURBO', icon_turbo), ('IROCKET', icon_rocket), ('ISHIELD', icon_shield), ('TROPHY', trophy), ('LOGO', logo)):
        img, anc = fn()
        out.append('static const KgSpr KG_%s = %s;\n' % (nm, emit(nm, img, anc)))
    for nm, pal in PALS.items():
        out.append('static const col KP_%s[16] = { 0, %s };' % (nm, ', '.join('rgb(%d, %d, %d)' % c for c in pal)))
    out.append('')
    open(OUT, 'w').write('\n'.join(out))
    print('wrote', OUT, sum(1 for _ in sheet), 'sprites')
    if preview:
        prevpal = {}
        kp = [(0, 0, 0), (255, 110, 110), (220, 30, 30), (130, 0, 0), (255, 230, 60), (190, 140, 0), (230, 40, 40), (120, 10, 10),
              (255, 255, 255), (170, 180, 200), (30, 40, 90), (220, 240, 255), (40, 40, 45), (100, 100, 110), (170, 170, 180), (10, 10, 10)]
        W = 1100; x = y = 0; rowh = 0
        im = Image.new('RGB', (W, 520), (60, 120, 60))
        for nm, img in sheet:
            h, w = img.shape
            if x + w * 3 + 4 > W: x = 0; y += rowh + 4; rowh = 0
            key = next((k for k in PALS if nm.startswith(k)), None)
            if nm.startswith('KART'): pal = kp
            elif nm.startswith('BOX'): pal = [(0,0,0),(255,60,60),(255,200,40),(60,200,255),(120,255,120),(255,255,255),(255,255,255),(90,90,120)]
            elif nm.startswith('COIN'): pal = [(0,0,0)] + PALS['COIN']
            elif nm.startswith('RKT'): pal = [(0,0,0)] + PALS['ROCKET']
            elif nm == 'ITURBO': pal = [(0,0,0)] + PALS['TURBO']
            elif nm == 'IROCKET': pal = [(0,0,0)] + PALS['ROCKETI']
            elif nm == 'ISHIELD': pal = [(0,0,0)] + PALS['SHIELD']
            elif nm == 'TROPHY': pal = [(0,0,0)] + PALS['GOLD']
            else: pal = [(0, 0, 0)] + PALS.get(key, PALS['TREE'])
            pal = pal + [(255, 0, 255)] * 16
            for yy in range(h):
                for xx in range(w):
                    v = img[yy, xx]
                    if v: im.paste(pal[v], (x + xx * 3, y + yy * 3, x + xx * 3 + 3, y + yy * 3 + 3))
            x += w * 3 + 4; rowh = max(rowh, h * 3)
        im.save(os.path.join(HERE, 'kart_art.png') if '--here' in sys.argv else '/tmp/claude-0/-home-user-Linux/47db8580-6a27-57ec-a3ba-3c3e683b74ae/scratchpad/kart_art.png')

main()
