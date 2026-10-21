"""Writes golden.bin (a stream: an info record and frames, fluent then sharp) and
golden.txt (per frame: seq, ts, flags, skipped tiles and the FNV-1a hash of the
whole 320 x 240 screen after decoding). The console's simulator decodes
golden.bin with the C decoder and compares (MiniArcadeColor/test, "tvdecode").
Run from MiniArcadeServer: python tests/make_golden.py"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from tvserver import codec  # noqa: E402
from tvserver.palette import PALETTE  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def picture(w, h, t):
    """Bars, a gradient, noise and a box that moves with t (frames)."""
    rng = np.random.default_rng(7)
    img = np.zeros((h, w, 3), np.uint8)
    bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0)]
    for k, c in enumerate(bars):
        img[:h // 3, k * w // 8:(k + 1) * w // 8] = c
    for k in range(w // 8):                                    # a row of tiles in exact colours, all different
        img[h // 3 - 8:h // 3, k * 8:k * 8 + 8] = PALETTE[(k * 37) % 252]
    x = np.linspace(0, 255, w, dtype=np.float32)
    img[h // 3:h // 2, :, 0] = x
    img[h // 3:h // 2, :, 1] = 90
    img[h // 3:h // 2, :, 2] = x[::-1]
    img[h // 2:h * 3 // 4] = rng.integers(0, 256, (h * 3 // 4 - h // 2, w, 3), dtype=np.uint8)
    img[h * 3 // 4:] = (20, 40, 90)
    s = h // 6
    bx = (t * 13) % (w - s)
    img[h * 3 // 4 + 4:h * 3 // 4 + 4 + s, bx:bx + s] = (255, 140, 0)
    img[h * 3 // 4 + 8:h * 3 // 4 + 12, bx + 3:bx + s - 3] = (255, 255, 255)
    return img


def build():
    data = bytearray()
    lines = []
    screen = np.zeros((240, 320), np.uint8)
    seq = 0
    for mode, gen, frames, q in ((0, 1, 4, 0), (1, 2, 3, 2)):
        enc = codec.Encoder(mode)
        w, h, _ = codec.MODES[mode]
        data += codec.info(codec.ST_PLAYING, mode, gen, 0, 60000, 25, 'Golden %d' % mode)
        for f in range(frames):
            img = picture(w, h, f)
            if f == frames - 1:
                img[h * 3 // 4:] = picture(w, h, 0)[h * 3 // 4:]          # the box goes back: old tiles again
            ts = f * 40
            rec = enc.encode(img, seq, ts, q if f else 0)
            p = rec[4:]
            r = codec.decode_frame(p, screen)
            assert r is not None
            ops = count_ops(p)
            lines.append('%d %d %d %d %08x' % (seq, ts, p[1], ops[codec.SKIP], codec.fnv1a(screen.tobytes())))
            data += rec
            seq += 1
    return bytes(data), lines


def count_ops(p):
    """Tiles per kind in a frame record."""
    out = [0] * 6
    sharp = p[1] & 2
    n = 1200 if sharp else 300
    pos, t = 8, 0
    size = {codec.SKIP: 0, codec.FLAT: 1, codec.TWO: 10, codec.FOUR: 20, codec.RAW: 64}
    while t < n:
        op = p[pos]
        pos += 1
        k, c = op >> 5, (op & 31) + 1
        out[k] += c
        pos += 1 if k == codec.FILL else size[k] * c
        t += c
    return out


if __name__ == '__main__':
    data, lines = build()
    with open(os.path.join(HERE, 'golden.bin'), 'wb') as f:
        f.write(data)
    with open(os.path.join(HERE, 'golden.txt'), 'w') as f:
        f.write('# seq ts flags skipped-tiles fnv1a-of-the-screen (see make_golden.py)\n')
        f.write('\n'.join(lines) + '\n')
    print(len(data), 'bytes,', len(lines), 'frames')
