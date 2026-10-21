"""Playing: one player per console (what, where, paused or not) and, while it
plays, one run: ffmpeg decoding from the position, frames paced to the wall
clock, encoded for the console and handed out in short segments.

Real time, never slow motion: a frame is encoded only when it is due within
LEAD_MS; a frame that is already late is dropped before it is encoded (the
console never gets a broken chain of changes). The console reports what it
showed and dropped with every segment request; the run lowers or raises its
frame rate and its quality level from that."""
import asyncio
import struct
import time

import numpy as np

from .codec import Encoder, MODES, ST_IDLE, ST_PLAYING, ST_PAUSED, ST_ENDED, ST_ERROR
from .egress import Blocked

FPS_MAX = {0: 25, 1: 15}         # frames per second ffmpeg delivers
FPS_MIN = {0: 6, 1: 3}
LEAD_MS = 700                    # frames go out this far ahead of the console's clock
LATE_MS = 120                    # ... and are dropped this far behind it
SEG_S = 0.5                      # one segment (one HTTP answer) lasts this long
IDLE_KILL_S = 15                 # no segment asked for this long: ffmpeg is stopped
QUEUE_MAX = 24


def now():
    return time.monotonic()


class FfmpegSource:
    """Raw RGB frames from ffmpeg, scaled and padded to the mode's size. src['url']
    is the server's own relay (app.py, /_media): ffmpeg never talks to YouTube."""

    def __init__(self, src, pos_ms, mode, fps, ffmpeg='ffmpeg'):
        self.w, self.h, _ = MODES[mode]
        self.src, self.pos, self.fps, self.ffmpeg = src, pos_ms, fps, ffmpeg
        self.proc = None
        self.err = ''

    async def start(self):
        hdr = ''.join('%s: %s\r\n' % (k, v) for k, v in (self.src.get('headers') or {}).items()
                      if k.lower() != 'user-agent')
        ua = (self.src.get('headers') or {}).get('User-Agent', 'Mozilla/5.0')
        vf = ('fps=%d,scale=%d:%d:force_original_aspect_ratio=decrease:flags=area,'
              'pad=%d:%d:(ow-iw)/2:(oh-ih)/2:black' % (self.fps, self.w, self.h, self.w, self.h))
        args = [self.ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'error']
        if self.src['url'].startswith(('http:', 'https:')):          # options of the http protocol
            args += ['-reconnect', '1', '-reconnect_streamed', '1', '-reconnect_delay_max', '5', '-user_agent', ua]
            if hdr:
                args += ['-headers', hdr]
        if self.pos and not self.src.get('live'):
            args += ['-ss', '%.3f' % (self.pos / 1000)]
        args += ['-i', self.src['url'], '-an', '-sn', '-dn', '-vf', vf, '-pix_fmt', 'rgb24', '-f', 'rawvideo', 'pipe:1']
        self.proc = await asyncio.create_subprocess_exec(*args, stdout=asyncio.subprocess.PIPE,
                                                         stderr=asyncio.subprocess.PIPE, stdin=asyncio.subprocess.DEVNULL)
        asyncio.get_running_loop().create_task(self._errors())

    async def _errors(self):
        while self.proc and self.proc.stderr:
            line = await self.proc.stderr.readline()
            if not line:
                break
            self.err = line.decode(errors='replace').strip()[:200]

    async def read(self):
        try:
            data = await self.proc.stdout.readexactly(self.w * self.h * 3)
        except (asyncio.IncompleteReadError, AttributeError):
            return None
        return np.frombuffer(data, np.uint8).reshape(self.h, self.w, 3)

    def close(self):
        if self.proc and self.proc.returncode is None:
            try:
                self.proc.kill()
            except ProcessLookupError:
                pass
        p, self.proc = self.proc, None
        if p:
            asyncio.get_running_loop().create_task(p.wait())


class PatternSource:
    """A test picture without YouTube: colour bars, a gradient, a moving box
    and a running bar - for setting the console up and for the tests."""

    def __init__(self, pos_ms, mode, fps, length_ms=60000):
        self.w, self.h, _ = MODES[mode]
        self.pos, self.fps, self.length = pos_ms, fps, length_ms
        self.i = 0
        self.err = ''

    async def start(self):
        pass

    def frame(self, t_ms):
        w, h = self.w, self.h
        img = np.zeros((h, w, 3), np.uint8)
        bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0), (255, 0, 255), (255, 0, 0), (0, 0, 255),
                (0, 0, 0)]
        for k, c in enumerate(bars):
            img[:h // 2, k * w // 8:(k + 1) * w // 8] = c
        x = np.linspace(0, 255, w, dtype=np.float32)
        img[h // 2:h * 3 // 4, :, 0] = x
        img[h // 2:h * 3 // 4, :, 1] = x[::-1]
        img[h // 2:h * 3 // 4, :, 2] = 128
        bx = int((t_ms / 20) % (w - h // 4))
        img[h // 8:h // 8 + h // 4, bx:bx + h // 4] = (255, 128, 0)
        prog = int(w * (t_ms % 10000) / 10000)
        img[h * 7 // 8:, :prog] = (40, 200, 40)
        return img

    async def read(self):
        t = self.pos + int(self.i * 1000 / self.fps)
        if t >= self.length:
            return None
        self.i += 1
        await asyncio.sleep(0)
        return self.frame(t)

    def close(self):
        pass


class Run:
    def __init__(self, player, source, pos_ms, mode, gen):
        self.player, self.src, self.pos, self.mode, self.gen = player, source, pos_ms, mode, gen
        self.enc = Encoder(mode)
        self.fps = FPS_MAX[mode]
        self.q = 1
        self.origin = None               # wall clock of the first segment
        self.queue = asyncio.Queue(QUEUE_MAX)
        self.seq = 0
        self.last_sent = -1              # seq of the last frame written to the console
        self.epoch = 0                   # raised when the console lost frames
        self.want_reset = False
        self.dropped = 0
        self.ended = False
        self.task = None
        self.bytes = 0

    async def start(self):
        await self.src.start()
        self.task = asyncio.get_running_loop().create_task(self._produce())

    def video_now(self):
        return self.pos if self.origin is None else self.pos + (now() - self.origin) * 1000

    def resync(self):
        """The console missed frames: next one is a key frame."""
        self.epoch += 1
        self.want_reset = True
        while not self.queue.empty():
            self.queue.get_nowait()

    def adapt(self, shown, late, buf):
        """From the console's report (frames shown and dropped since the last
        one, kB waiting in its buffer): frame rate and quality. Frames waiting
        here mean the connection is the bottleneck: fewer bytes."""
        total = shown + late
        full = self.queue.qsize() / QUEUE_MAX
        if total >= 3 and late > total * 0.2:
            self.fps = max(FPS_MIN[self.mode], self.fps * 0.8)
            self.q = min(4, self.q + 1)
        elif full > 0.7:
            self.q = min(4, self.q + 1)
        elif total >= 3 and late <= total * 0.05 and full < 0.5:
            self.fps = min(FPS_MAX[self.mode], self.fps + 1)
            if self.fps >= FPS_MAX[self.mode] * 0.9:
                self.q = max(0, self.q - 1)

    async def _produce(self):
        loop = asyncio.get_running_loop()
        src_fps = FPS_MAX[self.mode]
        i = 0
        next_ts = self.pos
        try:
            while True:
                img = await self.src.read()
                if img is None:
                    break
                ts = self.pos + int(i * 1000 / src_fps)
                i += 1
                while True:
                    ahead = ts - self.video_now() - LEAD_MS
                    if ahead <= 0:
                        break
                    await asyncio.sleep(min(ahead, 200) / 1000)
                if ts + 0.5 < next_ts:
                    continue                                  # fewer frames than the source has
                if self.origin is not None and ts < self.video_now() - LATE_MS:
                    self.dropped += 1
                    continue
                next_ts = ts + 1000 / self.fps
                if self.want_reset:
                    self.enc.reset()
                    self.want_reset = False
                ep, seq = self.epoch, self.seq
                rec = await loop.run_in_executor(None, self.enc.encode, img, seq, int(ts), self.q)
                self.seq = (self.seq + 1) & 0xFFFF
                await self.queue.put((ep, seq, rec))
        finally:
            self.ended = True
            if self.queue.full():
                self.queue.get_nowait()
            self.queue.put_nowait(None)

    def stop(self):
        if self.task:
            self.task.cancel()
        self.src.close()


class Player:
    def __init__(self, cid):
        self.cid = cid
        self.gen = 1
        self.state = ST_IDLE
        self.video = None                # {'id', 't', 'c', 'd'} or the test picture
        self.pos = 0
        self.mode = 0
        self.msg = ''
        self.run = None
        self.event = asyncio.Event()
        self.last_pull = 0.0

    def _changed(self):
        self.gen = self.gen % 65535 + 1
        ev, self.event = self.event, asyncio.Event()
        ev.set()

    def _kill(self):
        if self.run:
            self.run.stop()
            self.run = None

    def position(self):
        if self.state == ST_PLAYING and self.run and self.run.origin is not None:
            return int(self.run.video_now())
        return self.pos

    def dur(self):
        return int((self.video or {}).get('d', 0)) * 1000

    def play(self, video, pos=0):
        self._kill()
        self.video, self.pos, self.state, self.msg = video, pos, ST_PLAYING, ''
        self._changed()

    def pause(self):
        if self.state == ST_PLAYING:
            self.pos = self.position()
            self._kill()
            self.state = ST_PAUSED
            self._changed()

    def resume(self):
        if self.state in (ST_PAUSED, ST_ENDED, ST_ERROR) and self.video:
            if self.state != ST_PAUSED:
                self.pos = 0
            self.state, self.msg = ST_PLAYING, ''
            self._changed()

    def seek(self, d_s):
        if not self.video or self.state == ST_IDLE:
            return
        p = self.position() + int(d_s * 1000)
        d = self.dur()
        self.pos = max(0, min(p, d - 1000 if d > 1000 else p))
        self._kill()
        if self.state == ST_ENDED:
            self.state = ST_PAUSED
        self._changed()

    def set_mode(self, m):
        if m in MODES and m != self.mode:
            self.pos = self.position()
            self._kill()
            self.mode = m
            self._changed()

    def stop(self):
        self._kill()
        self.state, self.video, self.pos = ST_IDLE, None, 0
        self._changed()

    def ended(self, err=''):
        self._kill()
        self.state, self.pos, self.msg = (ST_ERROR, self.pos, err) if err else (ST_ENDED, 0, '')
        self._changed()

    def action(self, a, d='', m=''):
        """The console's or the phone's keys."""
        if a == 'pause':
            self.pause()
        elif a == 'play':
            self.resume()
        elif a == 'toggle':
            self.pause() if self.state == ST_PLAYING else self.resume()
        elif a == 'seek':
            try:
                self.seek(max(-3600, min(3600, int(d))))
            except ValueError:
                return False
        elif a == 'mode':
            self.set_mode(1 if str(m) in ('1', 'sharp') else 0)
        elif a == 'stop':
            self.stop()
        else:
            return False
        return True

    def state_json(self):
        v = self.video or {}
        return {'g': self.gen, 's': self.state, 'id': v.get('id', ''), 't': v.get('t', ''), 'c': v.get('c', ''),
                'd': v.get('d', 0), 'pos': self.position(), 'mode': self.mode, 'msg': self.msg,
                'fps': round(self.run.fps) if self.run else 0}


class Hub:
    """All players; starts and stops the runs."""

    def __init__(self, yt, ffmpeg='ffmpeg', relay=None):
        self.yt, self.ffmpeg = yt, ffmpeg
        self.players = {}
        self.relay = relay              # (video address, headers) -> the local address ffmpeg reads

    def player(self, cid):
        p = self.players.get(cid)
        if not p:
            p = self.players[cid] = Player(cid)
        return p

    async def ensure_run(self, p):
        """Starts the run for p's current state; False when it cannot play."""
        if p.state != ST_PLAYING:
            return False
        if p.run and p.run.gen == p.gen:
            return True
        p._kill()
        gen, v, pos, mode = p.gen, p.video, p.pos, p.mode
        try:
            if v.get('test'):
                src = PatternSource(pos, mode, FPS_MAX[mode])
            else:
                info = await self.yt.resolve(v['id'])
                if not v.get('d'):
                    v['d'] = info['d']
                local = dict(info, url=self.relay(info['url'], info.get('headers') or {}), headers={})
                src = FfmpegSource(local, pos, mode, FPS_MAX[mode], self.ffmpeg)
            if p.gen != gen:                      # changed while asking YouTube
                return False
            run = Run(p, src, pos, mode, gen)
            await run.start()
        except Blocked as e:
            if p.gen == gen:
                p.ended(str(e)[:40])
            return False
        except Exception as e:                    # noqa: BLE001 - shown to the user
            if p.gen == gen:
                p.ended(str(e)[:40] or 'Fehler')
            return False
        if p.gen != gen:
            run.stop()
            return False
        p.run = run
        return True

    async def janitor(self):
        while True:
            await asyncio.sleep(5)
            self.sweep()

    def sweep(self):
        t = now()
        for p in self.players.values():
            if p.run and t - p.last_pull > IDLE_KILL_S:      # the console is gone: stop ffmpeg
                p.pos = p.position()
                p._kill()
                p._changed()

    def close(self):
        for p in self.players.values():
            p._kill()


def frame_seq(rec):
    return struct.unpack_from('<H', rec, 6)[0]
