"""The TV stream format: records the console decodes straight into its frame
buffer (MiniArcadeColor/main/tvcodec.h holds the C decoder; decode_frame()
below mirrors it step by step).

A stream (the body of one /api/tv/stream answer) is a row of records:
  [u32 length, little endian][payload]
Payload byte 0 tells the kind:
  'I' info:  state u8, mode u8, gen u16, pos u32 (ms), dur u32 (ms), fps u8,
             title (ASCII, 0-terminated)
  'F' frame: flags u8 (bit 0 key frame, bit 1 sharp mode), seq u16, ts u32
             (ms of video time), then tile operations
Modes: 0 "fluent" = 160 x 120 shown doubled, 1 "sharp" = 320 x 240. A frame
is cut into 8 x 8 tiles (20 x 15 or 40 x 30), taken row by row. Every
operation is one byte, kind << 5 | (tiles - 1), for 1..32 tiles:
  0 SKIP  -                          the tiles stay as they are
  1 FILL  1 colour                   all tiles one colour
  2 FLAT  1 colour per tile
  3 TWO   per tile c0 c1 + 8 bytes   bit set = c1, bit 7 = left pixel
  4 FOUR  per tile 4 colours + 16 bytes, 2 bits a pixel, first pixel on top
  5 RAW   per tile 64 bytes
Little endian everywhere. The operations of a frame cover all its tiles."""
import struct
import numpy as np

from .palette import PALETTE, dither_ordered, nearest

SKIP, FILL, FLAT, TWO, FOUR, RAW = range(6)
MODES = {0: (160, 120, 2), 1: (320, 240, 1)}        # width, height, scale on the screen
ST_IDLE, ST_PLAYING, ST_PAUSED, ST_ENDED, ST_ERROR = range(5)
SCR_W, SCR_H = 320, 240

# per quality level 0 (best) .. 4 (fewest bytes): a tile counts as unchanged below
# this mean difference (0..255 per channel), and a busy tile may become four colours
# below that error
SKIP_T = (3, 5, 8, 12, 18)
FOUR_T = (0, 10, 16, 24, 48)


def record(payload):
    return struct.pack('<I', len(payload)) + payload


def info(state, mode, gen, pos, dur, fps, title=''):
    t = title.encode('ascii', 'replace')[:60]
    return record(struct.pack('<BBBHIIB', ord('I'), state, mode, gen & 0xFFFF, pos, dur, fps) + t + b'\0')


def _tiles(a, tw, th):
    """(H, W, ...) -> (tiles, 64, ...) in raster order of the tiles."""
    rest = a.shape[2:]
    t = a.reshape(th, 8, tw, 8, *rest).swapaxes(1, 2)
    return t.reshape(th * tw, 64, *rest)


class Encoder:
    """Turns RGB frames into frame records. Keeps what the console has, so
    unchanged tiles are skipped; reset() makes the next frame a key frame."""

    def __init__(self, mode):
        self.mode = mode
        self.w, self.h, _ = MODES[mode]
        self.tw, self.th = self.w // 8, self.h // 8
        self.reset()
        self.stats = [0] * 6

    def reset(self):
        self.ref_idx = None          # (tiles, 64) indices the console shows
        self.ref_src = None          # (tiles, 64, 3) the source they came from

    def encode(self, img, seq, ts, q=0):
        """img: (h, w, 3) uint8. Returns the record (with its length)."""
        assert img.shape == (self.h, self.w, 3)
        n = self.tw * self.th
        src = _tiles(img, self.tw, self.th).astype(np.int16)
        idx = _tiles(dither_ordered(img), self.tw, self.th)
        key = self.ref_idx is None
        if key:
            skip = np.zeros(n, bool)
        else:
            diff = np.abs(src - self.ref_src).mean(axis=(1, 2))
            skip = (idx == self.ref_idx).all(1) | (diff < SKIP_T[q])
        srt = np.sort(idx, 1)
        distinct = 1 + (np.diff(srt.astype(np.int16), axis=1) != 0).sum(1)
        kind = np.full(n, RAW, np.uint8)
        kind[distinct <= 4] = FOUR
        kind[distinct == 2] = TWO
        kind[distinct == 1] = FLAT
        # busy tiles: four colours by brightness, when that is close enough
        if FOUR_T[q]:
            busy = np.nonzero((kind == RAW) & ~skip)[0]
            if len(busy):
                s = src[busy].astype(np.float32)
                y = s @ np.array([0.30, 0.59, 0.11], np.float32)
                qs = np.quantile(y, [0.25, 0.5, 0.75], axis=1).T             # (k, 3)
                grp = (y[:, :, None] > qs[:, None, :]).sum(2)               # 0..3 per pixel
                cols = np.empty((len(busy), 4), np.uint8)
                rec = np.empty_like(s)
                for g in range(4):
                    m = grp == g
                    cnt = m.sum(1)
                    mean = (s * m[:, :, None]).sum(1) / np.maximum(cnt, 1)[:, None]
                    c = nearest(np.rint(mean))
                    cols[:, g] = c
                    rec[m] = PALETTE[np.repeat(c, cnt)].astype(np.float32)
                err = np.abs(rec - s).mean(axis=(1, 2))
                ok = err < FOUR_T[q]
                sel = busy[ok]
                kind[sel] = FOUR
                idx = idx.copy()
                idx[sel] = np.take_along_axis(cols[ok], grp[ok], 1)
        kind[skip] = SKIP
        sent = ~skip
        if key:
            self.ref_idx = idx.copy()
            self.ref_src = src.copy()
        else:
            self.ref_idx[sent] = idx[sent]
            self.ref_src[sent] = src[sent]
        flags = 1 if key else 0
        if self.mode == 1:
            flags |= 2
        out = bytearray(struct.pack('<BBHI', ord('F'), flags, seq & 0xFFFF, ts))
        out += self._ops(kind, idx)
        for k in range(6):
            self.stats[k] += int((kind == k).sum())
        return record(bytes(out))

    @staticmethod
    def _ops(kind, idx):
        n = len(kind)
        out = bytearray()
        i = 0
        while i < n:
            k = int(kind[i])
            j = i + 1
            if k == FLAT:
                c = idx[i, 0]
                while j < n and j - i < 32 and kind[j] == FLAT and idx[j, 0] == c:
                    j += 1
                if j - i >= 2:
                    out.append(FILL << 5 | (j - i - 1))
                    out.append(int(c))
                    i = j
                    continue
                # single colours up to the next run of two equal ones
                while j < n and j - i < 32 and kind[j] == FLAT and not (
                        j + 1 < n and kind[j + 1] == FLAT and idx[j + 1, 0] == idx[j, 0]):
                    j += 1
                out.append(FLAT << 5 | (j - i - 1))
                out += bytes(idx[i:j, 0])
                i = j
                continue
            while j < n and j - i < 32 and kind[j] == k:
                j += 1
            out.append(k << 5 | (j - i - 1))
            for t in range(i, j):
                out += _tile_data(k, idx[t])
            i = j
        return bytes(out)


def _tile_data(k, px):
    if k == SKIP:
        return b''
    if k == RAW:
        return px.tobytes()
    u = np.unique(px)
    if k == TWO:
        c0, c1 = int(u[0]), int(u[-1])
        return bytes((c0, c1)) + np.packbits(px == c1).tobytes()
    # FOUR: up to four colours, the last one repeated to fill
    cols = list(u) + [u[-1]] * (4 - len(u))
    v = np.searchsorted(u, px).astype(np.uint8)
    v = v.reshape(16, 4)
    packed = (v[:, 0] << 6) | (v[:, 1] << 4) | (v[:, 2] << 2) | v[:, 3]
    return bytes(int(c) for c in cols) + packed.astype(np.uint8).tobytes()


# ---------------- reading, as the console does it ----------------
TILE_BYTES = {SKIP: 0, FLAT: 1, TWO: 10, FOUR: 20, RAW: 64}


def records(data):
    """Splits a stream into payloads."""
    i = 0
    while i + 4 <= len(data):
        n = struct.unpack_from('<I', data, i)[0]
        yield data[i + 4:i + 4 + n]
        i += 4 + n


def parse_info(p):
    state, mode, gen, pos, dur, fps = struct.unpack_from('<BBHIIB', p, 1)
    title = p[14:p.index(b'\0', 14)].decode('ascii')
    return dict(state=state, mode=mode, gen=gen, pos=pos, dur=dur, fps=fps, title=title)


def decode_frame(p, screen):
    """Mirror of tvDecode() in tvcodec.h: paints the frame record p into screen
    ((240, 320) uint8, the console's frame buffer). Returns (seq, ts, flags) or
    None for a broken frame (then the screen may be half painted)."""
    if len(p) < 8 or p[0] != ord('F'):
        return None
    flags, seq, ts = p[1], p[2] | p[3] << 8, p[4] | p[5] << 8 | p[6] << 16 | p[7] << 24
    sharp = flags & 2
    tw, th, s = (40, 30, 1) if sharp else (20, 15, 2)
    n = tw * th
    pos = 8
    t = 0
    while t < n:
        if pos >= len(p):
            return None
        op = p[pos]
        pos += 1
        kind, cnt = op >> 5, (op & 31) + 1
        if t + cnt > n or kind > RAW:
            return None
        if kind == FILL:
            if pos + 1 > len(p):
                return None
            c = p[pos]
            pos += 1
        for _ in range(cnt):
            x0, y0 = (t % tw) * 8 * s, (t // tw) * 8 * s
            if kind == SKIP:
                pass
            elif kind == FILL or kind == FLAT:
                if kind == FLAT:
                    if pos + 1 > len(p):
                        return None
                    c = p[pos]
                    pos += 1
                screen[y0:y0 + 8 * s, x0:x0 + 8 * s] = c
            else:
                need = TILE_BYTES[kind]
                if pos + need > len(p):
                    return None
                d = p[pos:pos + need]
                pos += need
                for y in range(8):
                    for x in range(8):
                        k = y * 8 + x
                        if kind == TWO:
                            c = d[1] if d[2 + (k >> 3)] & (0x80 >> (k & 7)) else d[0]
                        elif kind == FOUR:
                            c = d[(d[4 + (k >> 2)] >> (6 - 2 * (k & 3))) & 3]
                        else:
                            c = d[k]
                        screen[y0 + y * s:y0 + y * s + s, x0 + x * s:x0 + x * s + s] = c
            t += 1
    if pos != len(p):
        return None
    return seq, ts, flags


def fnv1a(data):
    """FNV-1a 32 bit, as the console's tests use it."""
    h = 2166136261
    for b in bytes(data):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h
