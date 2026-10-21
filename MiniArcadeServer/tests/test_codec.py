import os
import re
import sys

import numpy as np
import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
from tvserver import codec, palette  # noqa: E402
import make_golden  # noqa: E402

GFX = os.path.join(HERE, '..', '..', 'MiniArcadeColor', 'main')


# ---------------- palette ----------------
def test_palette_is_the_consoles():
    assert palette.col_to_rgb(0) == (0, 0, 0)
    assert palette.col_to_rgb(251) == (255, 255, 255)
    assert palette.col_to_rgb(41) == (0, 255, 255)
    assert palette.col_to_rgb(210) == (255, 0, 0)
    assert [palette.col_to_rgb(c) for c in range(252, 256)] == [(g, g, g) for g in (32, 96, 160, 224)]
    for c in range(252):                                    # rgb() finds every cube colour again
        assert palette.rgb(*palette.col_to_rgb(c)) == c
    for c in range(256):                                    # the nearest colour of a palette colour is itself
        assert palette.PALETTE[palette.nearest(palette.PALETTE[c])].tolist() == palette.PALETTE[c].tolist()


@pytest.mark.skipif(not os.path.exists(os.path.join(GFX, 'gfx.cpp')), reason='console sources not here')
def test_palette_matches_console_source():
    src = open(os.path.join(GFX, 'gfx.cpp')).read()
    greys = re.search(r'EXTRA_GREY\[4\]\s*=\s*\{([^}]*)\}', src).group(1)
    assert tuple(int(x) for x in greys.split(',')) == palette.EXTRA_GREY
    assert 'r = (uint8_t)((c / 42) * 51);' in src
    assert 'g = (uint8_t)(((c / 6) % 7) * 255 / 6);' in src
    assert 'b = (uint8_t)((c % 6) * 51);' in src
    hdr = open(os.path.join(GFX, 'gfx.h')).read()
    assert '((r * 5 + 127) / 255) * 42 + ((g * 6 + 127) / 255) * 6 + (b * 5 + 127) / 255' in hdr


def test_ordered_dither():
    img = np.zeros((16, 16, 3), np.uint8)
    img[:] = palette.col_to_rgb(100)
    assert (palette.dither_ordered(img) == 100).all()        # a palette colour stays itself
    img[:] = (128, 128, 128)                                  # between two levels: a mix of both, about half each
    d = palette.dither_ordered(img)
    rgb = palette.to_rgb(d).astype(float)
    assert abs(rgb.mean() - 128) < 8
    assert len(np.unique(d)) <= 4


def test_diffuse_dither_keeps_mean():
    rng = np.random.default_rng(1)
    img = (rng.random((45, 80, 3)) * 255).astype(np.uint8)
    d = palette.dither_diffuse(img)
    assert d.shape == (45, 80)
    assert np.abs(palette.to_rgb(d).astype(float).mean((0, 1)) - img.astype(float).mean((0, 1))).max() < 6


# ---------------- tiles ----------------
def screen_of(enc):
    """What the encoder believes the console shows, as the 320 x 240 screen."""
    _, _, s = codec.MODES[enc.mode]
    t = enc.ref_idx.reshape(enc.th, enc.tw, 8, 8).swapaxes(1, 2).reshape(enc.h, enc.w)
    return np.repeat(np.repeat(t, s, 0), s, 1)


@pytest.mark.parametrize('mode,q', [(0, 0), (0, 3), (1, 0), (1, 2)])
def test_round_trip(mode, q):
    enc = codec.Encoder(mode)
    w, h, _ = codec.MODES[mode]
    screen = np.full((240, 320), 77, np.uint8)
    for f in range(5):
        rec = enc.encode(make_golden.picture(w, h, f), f, f * 40, q)
        p = list(codec.records(rec))
        assert len(p) == 1
        assert codec.decode_frame(p[0], screen) == (f, f * 40, (1 if f == 0 else 0) | (2 if mode else 0))
        assert (screen == screen_of(enc)).all()
    assert enc.stats[codec.SKIP] > 0


def test_unchanged_frame_is_all_skips():
    enc = codec.Encoder(0)
    img = make_golden.picture(160, 120, 3)
    enc.encode(img, 0, 0)
    p = next(codec.records(enc.encode(img, 1, 40)))
    assert p[8:] == bytes([0x1F] * 9 + [0x00 | (300 - 9 * 32 - 1)])     # 9 runs of 32, one of 12


def test_broken_frames_refused():
    enc = codec.Encoder(0)
    p = next(codec.records(enc.encode(make_golden.picture(160, 120, 0), 0, 0)))
    s = np.zeros((240, 320), np.uint8)
    assert codec.decode_frame(p[:-1], s) is None                       # cut short
    assert codec.decode_frame(p + b'\0', s) is None                    # too long
    assert codec.decode_frame(p[:8] + bytes([0xFF]), s) is None        # unknown kind
    assert codec.decode_frame(b'I' + p[1:], s) is None


def test_info_record():
    rec = codec.info(codec.ST_PAUSED, 1, 70000, 12345, 99000, 17, 'Hallo Welt')
    p = next(codec.records(rec))
    assert codec.parse_info(p) == dict(state=2, mode=1, gen=70000 & 0xFFFF, pos=12345, dur=99000, fps=17,
                                       title='Hallo Welt')


def test_golden_file():
    """The committed stream decodes to the committed screens (the console's
    simulator checks the same with the C decoder)."""
    data = open(os.path.join(HERE, 'golden.bin'), 'rb').read()
    want = [ln.split() for ln in open(os.path.join(HERE, 'golden.txt')) if not ln.startswith('#')]
    screen = np.zeros((240, 320), np.uint8)
    got = []
    kinds = set()
    for p in codec.records(data):
        if p[0] == ord('I'):
            continue
        seq, ts, flags = codec.decode_frame(p, screen)
        ops = make_golden.count_ops(p)
        kinds |= {k for k in range(6) if ops[k]}
        got.append(['%d' % seq, '%d' % ts, '%d' % flags, '%d' % ops[0], '%08x' % codec.fnv1a(screen.tobytes())])
    assert got == want
    assert kinds == set(range(6))                                      # every kind of tile is in it
