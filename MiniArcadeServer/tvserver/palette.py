"""The colour console's palette (MiniArcadeColor/main/gfx.h, gfx.cpp): a 6 x 7 x 6
RGB cube (index r*42 + g*6 + b) plus four extra greys (252..255), and the ways
to map pictures onto it: ordered dithering in the cube for video (fast, stable
from frame to frame, so unchanged areas stay unchanged) and error diffusion
over all 256 colours for the thumbnails."""
import numpy as np

EXTRA_GREY = (32, 96, 160, 224)


def col_to_rgb(c):
    """colToRgb() of gfx.cpp."""
    if c >= 252:
        g = EXTRA_GREY[c - 252]
        return g, g, g
    return (c // 42) * 51, ((c // 6) % 7) * 255 // 6, (c % 6) * 51


def rgb(r, g, b):
    """rgb() of gfx.h: the nearest cube colour, channel by channel."""
    return ((r * 5 + 127) // 255) * 42 + ((g * 6 + 127) // 255) * 6 + (b * 5 + 127) // 255


PALETTE = np.array([col_to_rgb(c) for c in range(256)], dtype=np.uint8)
_PAL_I = PALETTE.astype(np.int32)

# nearest of all 256 colours for every 5-bit RGB (32 KB)
_g = (np.arange(32) * 255 + 15) // 31
_grid = np.stack(np.meshgrid(_g, _g, _g, indexing='ij'), -1).reshape(-1, 1, 3)
NEAREST5 = np.empty(32768, np.uint8)
for _k in range(0, 32768, 4096):
    _d = ((_grid[_k:_k + 4096] - _PAL_I[None]) ** 2).sum(-1)
    NEAREST5[_k:_k + 4096] = _d.argmin(1)
del _g, _grid, _k, _d


def nearest(rgb_arr):
    """Nearest palette index (all 256) for an (..., 3) array, through the 5-bit table."""
    a = np.clip(np.asarray(rgb_arr, np.int32), 0, 255)
    k = ((a[..., 0] * 31 + 127) // 255) * 1024 + ((a[..., 1] * 31 + 127) // 255) * 32 + (a[..., 2] * 31 + 127) // 255
    return NEAREST5[k]


# 8 x 8 Bayer matrix, thresholds in (0, 1)
_B2 = np.array([[0, 2], [3, 1]])
_B = _B2
for _ in range(2):
    _B = np.block([[4 * _B, 4 * _B + 2], [4 * _B + 3, 4 * _B + 1]])
BAYER8 = ((_B + 0.5) / 64).astype(np.float32)


def dither_ordered(img):
    """(H, W, 3) uint8 -> (H, W) palette indices of the cube, ordered dithering.
    A colour that is in the cube maps to itself on every pixel."""
    h, w, _ = img.shape
    t = np.tile(BAYER8, (h // 8 + 1, w // 8 + 1))[:h, :w]
    f = img.astype(np.float32)
    r = np.minimum(np.floor(f[..., 0] * (5 / 255) + t), 5)
    g = np.minimum(np.floor(f[..., 1] * (6 / 255) + t), 6)
    b = np.minimum(np.floor(f[..., 2] * (5 / 255) + t), 5)
    return (r * 42 + g * 6 + b).astype(np.uint8)


def dither_diffuse(img):
    """(H, W, 3) uint8 -> (H, W) indices of all 256 colours, Floyd-Steinberg.
    For small pictures (thumbnails)."""
    h, w, _ = img.shape
    work = img.astype(np.float32)
    out = np.empty((h, w), np.uint8)
    for y in range(h):
        row = work[y]
        for x in range(w):
            c = int(nearest(row[x]))
            out[y, x] = c
            e = row[x] - _PAL_I[c]
            if x + 1 < w:
                row[x + 1] += e * (7 / 16)
            if y + 1 < h:
                nxt = work[y + 1]
                if x > 0:
                    nxt[x - 1] += e * (3 / 16)
                nxt[x] += e * (5 / 16)
                if x + 1 < w:
                    nxt[x + 1] += e * (1 / 16)
    return out


def to_rgb(idx):
    """Palette indices -> (..., 3) uint8 RGB, as the console shows them."""
    return PALETTE[np.asarray(idx)]
