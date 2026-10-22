"""The web server: the consoles' API (token in the header X-Token), the phone
page (password, then a signed cookie), the stream and the relay for internet
multiplayer (net.py)."""
import asyncio
import hashlib
import hmac
import ipaddress
import json
import logging
import os
import secrets
import time
from pathlib import Path

import aiohttp
import numpy as np
from aiohttp import web

from . import codec, palette, youtube
from .egress import Blocked, Egress
from .net import Relay
from .store import Store
from .stream import Hub, now

log = logging.getLogger('tvserver')
WEB = Path(__file__).parent / 'web'
THUMB_W, THUMB_H = 80, 45
PIN_TRIES = 5                      # wrong PINs in a row (per address and per console) ...
PIN_LOCK_S = 600                   # ... lock pairing for this long
DEFAULT_CATS = 'Beliebt=;Musik=musik;Gaming=gaming;News=nachrichten;Sport=sport highlights'


class Config:
    def __init__(self, env=None):
        e = os.environ if env is None else env
        self.port = int(e.get('PORT', '8080'))
        bp = e.get('BASE_PATH', '').strip().rstrip('/')
        self.base = ('/' + bp.lstrip('/')) if bp else ''
        self.password = e.get('ADMIN_PASSWORD', '')
        self.pin = ''.join(ch for ch in e.get('CONSOLE_PIN', '') if ch.isdigit())
        self.data = e.get('DATA_DIR', '/data')
        self.cache_s = int(float(e.get('CACHE_MINUTES', '10')) * 60)
        self.thumb_mb = float(e.get('THUMB_CACHE_MB', '50'))
        self.update_h = float(e.get('YTDLP_UPDATE_HOURS', '24'))
        # the reverse proxies whose X-Forwarded-For / X-Real-IP count (IPs or networks)
        self.trusted = []
        for part in e.get('TRUSTED_PROXIES', '').replace(';', ',').split(','):
            if part.strip():
                self.trusted.append(ipaddress.ip_network(part.strip(), strict=False))
        self.ffmpeg = e.get('FFMPEG', 'ffmpeg')
        self.yt_proxy = e.get('YT_PROXY', '')
        self.yt_source = e.get('YT_SOURCE_ADDRESS', '')
        self.yt_cooldown_h = float(e.get('YT_COOLDOWN_HOURS', '6'))
        self.yt_per_min = int(e.get('YT_REQUESTS_PER_MIN', '20'))
        self.yt_thumbs_per_min = int(e.get('YT_THUMBS_PER_MIN', '60'))
        self.yt_sleep = float(e.get('YT_SLEEP_REQUESTS', '1'))
        self.cats = []
        for part in e.get('CATEGORIES', DEFAULT_CATS).split(';'):
            if '=' in part:
                name, q = part.split('=', 1)
                if name.strip():
                    self.cats.append((youtube.ascii_safe(name.strip(), 12), q.strip()))


class Bucket:
    """Token bucket: rate per second, burst."""

    def __init__(self, rate, burst):
        self.rate, self.burst = rate, burst
        self.level, self.t = burst, now()

    def take(self, n=1):
        t = now()
        self.level = min(self.burst, self.level + (t - self.t) * self.rate)
        self.t = t
        if self.level >= n:
            self.level -= n
            return True
        return False


class Limits:
    def __init__(self):
        self.b = {}

    def ok(self, key, rate, burst, n=1):
        b = self.b.get(key)
        if not b:
            if len(self.b) > 5000:
                self.b.clear()
            b = self.b[key] = Bucket(rate, burst)
        return b.take(n)


class Server:
    def __init__(self, cfg, yt=None, thumb_fetch=None):
        self.cfg = cfg
        Path(cfg.data).mkdir(parents=True, exist_ok=True)
        self.thumbs = Path(cfg.data) / 'thumbs'
        self.thumbs.mkdir(exist_ok=True)
        self.store = Store(str(Path(cfg.data) / 'state.json'))
        sec = Path(cfg.data) / 'secret'
        if not sec.exists():
            sec.write_text(secrets.token_hex(32))
            os.chmod(sec, 0o600)
        self.secret = bytes.fromhex(sec.read_text().strip())
        self.egress = yt.egress if yt else Egress(cfg.yt_proxy, cfg.yt_source, cfg.yt_cooldown_h)
        self.egress.warn()
        self.yt = yt or youtube.YouTube(cfg.cache_s, egress=self.egress, per_min=cfg.yt_per_min, sleep_s=cfg.yt_sleep)
        self.hub = Hub(self.yt, cfg.ffmpeg, self.relay)
        self.media = {}                    # relay key -> (address, headers, made)
        self.thumb_fetch = thumb_fetch or self._thumb_ffmpeg
        self.limits = Limits()
        self.net = Relay(self.store)
        self.net.msg_ok = lambda cid, n: self.limits.ok(('nm', cid), 40, 80, n)
        self.fails = {}                    # ip or console code -> [failures, locked until]
        self.thumb_writes = 0
        self.pin = cfg.pin
        if not self.pin:                   # none given: one is made once and kept on the volume
            f = Path(cfg.data) / 'console_pin'
            if not f.exists():
                f.write_text('%08d' % secrets.randbelow(10 ** 8))
                os.chmod(f, 0o600)
            self.pin = f.read_text().strip()
            log.warning('CONSOLE_PIN not set - the consoles pair with this PIN: %s', self.pin)

    # ---------------- helpers ----------------
    def trusted(self, addr):
        try:
            a = ipaddress.ip_address(addr or '')
        except ValueError:
            return False
        return any(a in n for n in self.cfg.trusted)

    def ip(self, req):
        """The client's address: the socket's peer, or - only when that peer is one of
        TRUSTED_PROXIES - the first address in X-Forwarded-For (read from the right)
        that is not a trusted proxy itself, else X-Real-IP."""
        peer = req.remote or '?'
        if not self.trusted(peer):
            return peer
        xf = [x.strip() for x in req.headers.get('X-Forwarded-For', '').split(',') if x.strip()]
        for x in reversed(xf):
            if not self.trusted(x):
                return x
        xr = req.headers.get('X-Real-IP', '').strip()
        return xr or peer

    def console(self, req, net=False):
        tok = req.headers.get('X-Token') or ''
        if not tok and req.headers.get('Authorization', '').startswith('Bearer '):
            tok = req.headers['Authorization'][7:]
        cid = self.store.by_token(tok.strip())
        if not cid:
            raise web.HTTPUnauthorized(text='not paired')
        # internet play polls often while a real-time game runs: its own, wider limit
        if not (self.limits.ok(('n', cid), 40, 80) if net else self.limits.ok(('c', cid), 10, 40)):
            raise web.HTTPTooManyRequests(text='slow down')
        return cid

    def _sign(self, until):
        return hmac.new(self.secret, b'web:%d' % until, hashlib.sha256).hexdigest()

    def cookie(self):
        until = int(time.time()) + 30 * 86400
        return '%d.%s' % (until, self._sign(until))

    def admin(self, req):
        c = req.cookies.get('mas', '')
        try:
            until, sig = c.split('.', 1)
            ok = int(until) > time.time() and hmac.compare_digest(sig, self._sign(int(until)))
        except ValueError:
            ok = False
        if not ok:
            raise web.HTTPUnauthorized(text='Bitte anmelden')
        if not self.limits.ok(('w', self.ip(req)), 10, 40):
            raise web.HTTPTooManyRequests(text='zu viele Anfragen')

    @staticmethod
    def j(data, status=200):
        return web.json_response(data, status=status, dumps=lambda d: json.dumps(d, separators=(',', ':')))

    async def body(self, req):
        if req.content_type == 'application/json':
            try:
                d = await req.json()
                return d if isinstance(d, dict) else {}
            except (ValueError, UnicodeDecodeError):
                raise web.HTTPBadRequest(text='bad json')
        d = dict(req.query)
        if req.can_read_body:
            d.update(await req.post())
        return d

    # ---------------- lists ----------------
    async def feed(self, c):
        if not (0 <= c < len(self.cfg.cats)):
            raise web.HTTPNotFound(text='no such category')
        name, q = self.cfg.cats[c]
        try:
            if q:
                return await self.yt.search(q, 20, 'week')
            parts = await asyncio.gather(*[self.yt.search(q2, 12, 'week') for _, q2 in self.cfg.cats if q2],
                                         return_exceptions=True)
            seen, out = set(), []
            for it in sorted((i for p in parts if isinstance(p, list) for i in p), key=lambda i: -i.get('v', 0)):
                if it['id'] not in seen:
                    seen.add(it['id'])
                    out.append(it)
            return out[:20]
        except Blocked as e:
            raise web.HTTPServiceUnavailable(text=str(e))
        except RuntimeError as e:
            raise web.HTTPBadGateway(text=str(e))

    def small(self, cid, items):
        """Items for the console: id, title, channel, duration, favourite."""
        favs = {x['id'] for x in (self.store.console(cid) or {}).get('favs', [])}
        return {'items': [{'id': i['id'], 't': i['t'], 'c': i['c'], 'd': i['d'], 'f': 1 if i['id'] in favs else 0}
                          for i in items]}

    async def find_item(self, vid):
        """Title etc. of a video for history and favourites."""
        try:
            r = await self.yt.resolve(vid)
            return {'id': vid, 't': r['title'], 'c': r['c'], 'd': r['d']}
        except Blocked:
            raise
        except RuntimeError:
            return None

    # ---------------- console API ----------------
    async def ping(self, req):
        """For the console's "test connection": no token needed."""
        return self.j({'ok': 1, 'name': 'MiniArcade'})

    def locked(self, key):
        f = self.fails.get(key)
        return bool(f and f[1] > now())

    def failed(self, key):
        f = self.fails.setdefault(key, [0, 0.0])
        f[0] += 1
        if f[0] >= PIN_TRIES:                       # too many wrong PINs: locked for a while
            f[0], f[1] = 0, now() + PIN_LOCK_S

    async def pair(self, req):
        """A console pairs with the server's PIN and gets its own token."""
        d = await self.body(req)
        code = ''.join(ch for ch in str(d.get('code', '')).upper() if ch.isalnum())[:4] or '????'
        name = youtube.ascii_safe(str(d.get('name', '')), 12) or code
        keys = (('ip', self.ip(req)), ('code', code))
        if any(self.locked(k) for k in keys):
            raise web.HTTPTooManyRequests(text='too many wrong PINs - wait a few minutes')
        if len(self.fails) > 5000:
            self.fails = {k: v for k, v in self.fails.items() if v[1] > now()}
        pin = str(d.get('pin', '')).strip()
        if not hmac.compare_digest(pin.encode(), self.pin.encode()):
            for k in keys:
                self.failed(k)
            await asyncio.sleep(0.5)
            raise web.HTTPForbidden(text='wrong PIN')
        for k in keys:
            self.fails.pop(k, None)
        cid, tok = self.store.add_console(code, name)
        log.info('console %s (%s) paired', code, name)
        return self.j({'token': tok, 'id': cid})

    async def cats(self, req):
        self.console(req)
        return self.j({'cats': [n for n, _ in self.cfg.cats]})

    async def c_feed(self, req):
        cid = self.console(req)
        try:
            c = int(req.query.get('c', '0'))
        except ValueError:
            raise web.HTTPBadRequest(text='c')
        return self.j(self.small(cid, await self.feed(c)))

    async def c_search(self, req):
        cid = self.console(req)
        if not self.limits.ok(('s', cid), 0.5, 5):
            raise web.HTTPTooManyRequests(text='slow down')
        try:
            items = await self.yt.search(req.query.get('q', ''), 20)
        except Blocked as e:
            raise web.HTTPServiceUnavailable(text=str(e))
        except RuntimeError as e:
            raise web.HTTPBadGateway(text=str(e))
        return self.j(self.small(cid, items))

    async def c_list(self, req):
        cid = self.console(req)
        c = self.store.console(cid)
        k = 'favs' if req.query.get('k') == 'fav' else 'history'
        return self.j(self.small(cid, c[k]))

    async def c_fav(self, req):
        cid = self.console(req)
        d = await self.body(req)
        vid = youtube.video_id(str(d.get('id', '')))
        if not vid:
            raise web.HTTPBadRequest(text='not a YouTube video')
        on = str(d.get('on', '1')) == '1'
        it = None
        if on:
            it = await self.find_item(vid)
            if not it:
                raise web.HTTPBadGateway(text='video not found')
        self.store.set_fav(cid, it or {'id': vid}, on)
        return self.j({'ok': 1, 'on': 1 if on else 0})

    async def thumb(self, req):
        self.console(req)
        vid = req.match_info['vid']
        if not youtube.ID_RE.match(vid):
            raise web.HTTPBadRequest(text='bad id')
        f = self.thumbs / (vid + '.bin')
        if not f.exists():
            try:
                img = await self.thumb_fetch(vid)
            except Blocked as e:
                raise web.HTTPServiceUnavailable(text=str(e))
            if img is None:
                raise web.HTTPNotFound(text='no thumbnail')
            data = await asyncio.get_running_loop().run_in_executor(None, lambda: palette.dither_diffuse(img).tobytes())
            tmp = f.with_suffix('.tmp')
            tmp.write_bytes(data)
            tmp.replace(f)
            self.thumb_writes += 1
            if self.thumb_writes % 50 == 1:
                self.trim_thumbs()
        else:
            os.utime(f)
        return web.Response(body=f.read_bytes(), content_type='application/octet-stream',
                            headers={'Cache-Control': 'max-age=86400'})

    def trim_thumbs(self):
        files = sorted(self.thumbs.glob('*.bin'), key=lambda p: p.stat().st_mtime)
        total = sum(p.stat().st_size for p in files)
        cap = self.cfg.thumb_mb * 1e6
        for p in files:
            if total <= cap:
                break
            total -= p.stat().st_size
            p.unlink(missing_ok=True)

    async def _thumb_ffmpeg(self, vid):
        """The picture through the egress, decoded by ffmpeg from a pipe."""
        self.egress.check()
        t = now()
        self.thumb_times = [x for x in getattr(self, 'thumb_times', []) if t - x < 60]
        if len(self.thumb_times) >= self.cfg.yt_thumbs_per_min:
            return None
        self.thumb_times.append(t)
        try:
            async with self.egress.session().get('https://i.ytimg.com/vi/%s/mqdefault.jpg' % vid) as r:
                if r.status in (403, 429):
                    self.egress.note_error('HTTP Error %d' % r.status)
                if r.status != 200:
                    return None
                jpg = await r.read()
        except (aiohttp.ClientError, asyncio.TimeoutError):
            return None
        vf = ('scale=%d:%d:force_original_aspect_ratio=decrease:flags=area,pad=%d:%d:(ow-iw)/2:(oh-ih)/2:black'
              % (THUMB_W, THUMB_H, THUMB_W, THUMB_H))
        p = await asyncio.create_subprocess_exec(
            self.cfg.ffmpeg, '-nostdin', '-loglevel', 'error', '-i', 'pipe:0', '-frames:v', '1', '-vf', vf,
            '-pix_fmt', 'rgb24', '-f', 'rawvideo', 'pipe:1',
            stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
        try:
            out, _ = await asyncio.wait_for(p.communicate(jpg), 20)
        except asyncio.TimeoutError:
            p.kill()
            return None
        if len(out) != THUMB_W * THUMB_H * 3:
            return None
        return np.frombuffer(out, np.uint8).reshape(THUMB_H, THUMB_W, 3)

    # ---------------- the relay: ffmpeg reads the video through the egress ----------------
    def relay(self, url, headers):
        t = now()
        for k in [k for k, v in self.media.items() if t - v[2] > 6 * 3600]:
            del self.media[k]
        key = secrets.token_urlsafe(16)
        self.media[key] = (url, dict(headers), t)
        return 'http://127.0.0.1:%d%s/_media/%s' % (self.cfg.port, self.cfg.base, key)

    async def media_get(self, req):
        if req.remote not in ('127.0.0.1', '::1'):
            raise web.HTTPForbidden(text='local only')
        m = self.media.get(req.match_info['key'])
        if not m:
            raise web.HTTPNotFound(text='gone')
        try:
            self.egress.check()
        except Blocked as e:
            raise web.HTTPServiceUnavailable(text=str(e))
        h = {k: v for k, v in m[1].items() if k.lower() not in ('cookie', 'authorization', 'accept-encoding')}
        if 'Range' in req.headers:
            h['Range'] = req.headers['Range']
        try:
            async with self.egress.session().get(m[0], headers=h) as up:
                if up.status in (403, 429):
                    self.egress.block('HTTP Error %d (video data)' % up.status)
                resp = web.StreamResponse(status=up.status)
                for k in ('Content-Type', 'Content-Length', 'Content-Range', 'Accept-Ranges'):
                    if k in up.headers:
                        resp.headers[k] = up.headers[k]
                await resp.prepare(req)
                async for chunk in up.content.iter_chunked(65536):
                    await resp.write(chunk)
                await resp.write_eof()
                return resp
        except (aiohttp.ClientError, asyncio.TimeoutError):
            raise web.HTTPBadGateway(text='upstream')

    async def start_video(self, cid, vid, test=False):
        p = self.hub.player(cid)
        if test:
            p.play({'id': '', 't': 'Testbild', 'c': 'MiniArcade', 'd': 60, 'test': True})
            return p
        it = await self.find_item(vid) or {'id': vid, 't': vid, 'c': '', 'd': 0}
        self.store.add_history(cid, it)
        p.play(dict(it))
        return p

    async def tv_play(self, req):
        cid = self.console(req)
        d = await self.body(req)
        vid = youtube.video_id(str(d.get('id', '')))
        if not vid:
            raise web.HTTPBadRequest(text='not a YouTube video')
        p = await self.start_video(cid, vid)
        return self.j({'ok': 1, 'g': p.gen})

    async def tv_now(self, req):
        cid = self.console(req)
        return self.j(self.hub.player(cid).state_json())

    async def tv_ctl(self, req):
        cid = self.console(req)
        p = self.hub.player(cid)
        if not p.action(req.query.get('a', ''), req.query.get('d', ''), req.query.get('m', '')):
            raise web.HTTPBadRequest(text='unknown action')
        return self.j(p.state_json())

    async def tv_stream(self, req):
        cid = self.console(req)
        p = self.hub.player(cid)
        p.last_pull = now()
        q = req.query

        def num(k, d=0):
            try:
                return int(q.get(k, d))
            except ValueError:
                return d
        g, have = num('g'), num('have', -1)
        a = q.get('a', '')
        if a:
            p.action(a, q.get('d', ''), q.get('m', ''))
        elif p.state != codec.ST_PLAYING and g == p.gen:          # nothing new: wait a little for a change
            try:
                await asyncio.wait_for(p.event.wait(), 1.5)
            except asyncio.TimeoutError:
                pass
        if p.state == codec.ST_PLAYING:
            await self.hub.ensure_run(p)
        run = p.run if p.state == codec.ST_PLAYING else None
        resp = web.StreamResponse(headers={'Content-Type': 'application/octet-stream', 'Cache-Control': 'no-store',
                                           'X-Accel-Buffering': 'no'})
        await resp.prepare(req)
        title = p.msg if p.state == codec.ST_ERROR else (p.video or {}).get('t', '')
        await resp.write(codec.info(p.state, p.mode, p.gen, p.position(), p.dur(), round(run.fps) if run else 0, title))
        if run:
            if g == p.gen:
                run.adapt(num('shown'), num('late'), num('buf'))
                if have != run.last_sent:
                    run.resync()
            elif run.last_sent >= 0:
                run.resync()
            if run.origin is None:
                run.origin = now()
            end, gen0 = now() + 0.5, p.gen
            while p.gen == gen0 and p.run is run:
                left = end - now()
                if left <= 0:
                    break
                try:
                    item = await asyncio.wait_for(run.queue.get(), left)
                except asyncio.TimeoutError:
                    break
                if item is None:
                    p.ended(run.src.err if run.last_sent < 0 and run.src.err else '')
                    break
                ep, seq, rec = item
                if ep != run.epoch:
                    continue
                await resp.write(rec)
                run.last_sent = seq
                run.bytes += len(rec)
        p.last_pull = now()
        await resp.write_eof()
        return resp

    # ---------------- internet play (net.py) ----------------
    async def n_poll(self, req):
        cid = self.console(req, net=True)
        d = await self.body(req)
        try:
            return self.j(await self.net.poll(cid, d))
        except ValueError as e:
            raise web.HTTPBadRequest(text=str(e))
        except OverflowError as e:
            raise web.HTTPTooManyRequests(text=str(e))

    async def n_me(self, req):
        cid = self.console(req, net=True)
        return self.j({'me': self.store.friend_code(cid), 'n': self.net.name_of(cid)})

    async def n_invite(self, req):
        cid = self.console(req, net=True)
        d = await self.body(req)
        if not self.limits.ok(('ni', cid), 1, 8):
            raise web.HTTPTooManyRequests(text='slow down')
        try:
            g = int(d.get('g', 0))
        except (TypeError, ValueError):
            raise web.HTTPBadRequest(text='g')
        try:
            iv = self.net.invite(cid, str(d.get('to', '')), g)
        except LookupError as e:
            raise web.HTTPNotFound(text=str(e))
        except ValueError as e:
            return self.j({'why': str(e)}, status=409)
        return self.j({'oid': iv.id})

    async def n_cancel(self, req):
        cid = self.console(req, net=True)
        d = await self.body(req)
        try:
            self.net.cancel(cid, str(d.get('oid', '')))
        except LookupError as e:
            raise web.HTTPNotFound(text=str(e))
        return self.j({'ok': 1})

    async def n_answer(self, req):
        cid = self.console(req, net=True)
        d = await self.body(req)
        try:
            s = self.net.reply(cid, str(d.get('iid', '')), str(d.get('yes', '0')) == '1')
        except LookupError as e:
            raise web.HTTPNotFound(text=str(e))
        except ValueError as e:
            return self.j({'why': str(e)}, status=409)
        return self.j(self.net.answer(cid, {'sid': s.sid}, 0) if s else {'ok': 1})

    async def n_leave(self, req):
        cid = self.console(req, net=True)
        d = await self.body(req)
        try:
            self.net.leave(cid, int(d.get('sid', 0) or 0))
        except (TypeError, ValueError):
            raise web.HTTPBadRequest(text='sid')
        return self.j({'ok': 1})

    async def n_friend(self, req):
        """Adds (on=1) or removes (on=0) a friend by the code; friends go both ways."""
        cid = self.console(req, net=True)
        d = await self.body(req)
        if not self.limits.ok(('nf', cid), 0.2, 5):           # no guessing of codes
            raise web.HTTPTooManyRequests(text='slow down')
        other = self.store.by_friend_code(str(d.get('c', '')))
        if not other:
            raise web.HTTPNotFound(text='no console with this code')
        if other == cid:
            return self.j({'why': 'self'}, status=409)
        if not self.store.set_friend(cid, other, str(d.get('on', '1')) == '1'):
            return self.j({'why': 'full'}, status=409)
        self.net.notify(other)
        return self.j({'ok': 1, 'c': self.store.friend_code(other), 'n': self.net.name_of(other),
                       'o': 1 if self.net.online(other) else 0})

    # ---------------- phone page ----------------
    async def index(self, req):
        html = (WEB / 'index.html').read_text(encoding='utf-8').replace('{{BASE}}', self.cfg.base)
        return web.Response(text=html, content_type='text/html', headers={'Cache-Control': 'no-store'})

    async def login(self, req):
        ip = self.ip(req)
        if not self.limits.ok(('l', ip), 10 / 600, 10):
            raise web.HTTPTooManyRequests(text='zu viele Versuche - bitte warten')
        d = await self.body(req)
        pw = str(d.get('password', ''))
        if not self.cfg.password or not hmac.compare_digest(pw.encode(), self.cfg.password.encode()):
            await asyncio.sleep(1)
            raise web.HTTPUnauthorized(text='falsches Passwort')
        r = self.j({'ok': 1})
        r.set_cookie('mas', self.cookie(), max_age=30 * 86400, httponly=True, samesite='Strict',
                     secure=req.secure or (self.trusted(req.remote) and req.headers.get('X-Forwarded-Proto') == 'https'),
                     path=self.cfg.base or '/')
        return r

    async def logout(self, req):
        r = self.j({'ok': 1})
        r.del_cookie('mas', path=self.cfg.base or '/')
        return r

    async def w_me(self, req):
        self.admin(req)
        return self.j({'ok': 1, 'cats': [n for n, _ in self.cfg.cats], 'pin': self.pin,
                       'egress': self.egress.kind(), 'blocked': self.egress.message()})

    async def w_feed(self, req):
        self.admin(req)
        try:
            c = int(req.query.get('c', '0'))
        except ValueError:
            raise web.HTTPBadRequest(text='c')
        return self.j({'items': await self.feed(c)})

    async def w_search(self, req):
        self.admin(req)
        q = req.query.get('q', '')
        vid = youtube.video_id(q)
        try:
            if vid:
                it = await self.find_item(vid)
                return self.j({'items': [it] if it else []})
            return self.j({'items': await self.yt.search(q, 20)})
        except Blocked:
            raise web.HTTPServiceUnavailable(text=self.egress.message())
        except RuntimeError as e:
            raise web.HTTPBadGateway(text=str(e))

    async def w_consoles(self, req):
        self.admin(req)
        out = []
        for cid, c in self.store.consoles().items():
            p = self.hub.players.get(cid)
            out.append({'id': cid, 'name': c['name'], 'code': c['code'], 'since': c['since'],
                        'state': p.state_json() if p else None,
                        'online': bool(p and now() - p.last_pull < 20)})
        return self.j({'consoles': out})

    def w_cid(self, d):
        cid = str(d.get('console', ''))
        if not self.store.console(cid):
            raise web.HTTPNotFound(text='Konsole unbekannt')
        return cid

    async def w_remove(self, req):
        self.admin(req)
        cid = self.w_cid(await self.body(req))
        p = self.hub.players.pop(cid, None)
        if p:
            p._kill()
        self.store.remove(cid)
        return self.j({'ok': 1})

    async def w_play(self, req):
        self.admin(req)
        d = await self.body(req)
        cid = self.w_cid(d)
        if str(d.get('test', '')) == '1':
            p = await self.start_video(cid, None, test=True)
        else:
            vid = youtube.video_id(str(d.get('url', '') or d.get('id', '')))
            if not vid:
                raise web.HTTPBadRequest(text='Nur YouTube-Links oder Video-IDs')
            p = await self.start_video(cid, vid)
        return self.j(p.state_json())

    async def w_ctl(self, req):
        self.admin(req)
        d = await self.body(req)
        p = self.hub.player(self.w_cid(d))
        if not p.action(str(d.get('a', '')), str(d.get('d', '')), str(d.get('m', ''))):
            raise web.HTTPBadRequest(text='unbekannte Aktion')
        return self.j(p.state_json())

    async def w_state(self, req):
        self.admin(req)
        return self.j(self.hub.player(self.w_cid(req.query)).state_json())

    async def health(self, req):
        return web.Response(text='ok')

    # ---------------- app ----------------
    def routes(self, app):
        r = app.router
        r.add_get('/', self.index)
        r.add_get('/health', self.health)
        r.add_get('/_media/{key}', self.media_get)
        r.add_post('/api/pair', self.pair)
        r.add_get('/api/ping', self.ping)
        r.add_get('/api/cats', self.cats)
        r.add_get('/api/feed', self.c_feed)
        r.add_get('/api/search', self.c_search)
        r.add_get('/api/list', self.c_list)
        r.add_route('*', '/api/fav', self.c_fav)
        r.add_get('/api/thumb/{vid}', self.thumb)
        r.add_route('*', '/api/tv/play', self.tv_play)
        r.add_get('/api/tv/now', self.tv_now)
        r.add_route('*', '/api/tv/ctl', self.tv_ctl)
        r.add_get('/api/tv/stream', self.tv_stream)
        r.add_post('/api/net/poll', self.n_poll)
        r.add_get('/api/net/me', self.n_me)
        r.add_post('/api/net/invite', self.n_invite)
        r.add_post('/api/net/cancel', self.n_cancel)
        r.add_post('/api/net/answer', self.n_answer)
        r.add_post('/api/net/leave', self.n_leave)
        r.add_post('/api/net/friend', self.n_friend)
        r.add_post('/api/web/login', self.login)
        r.add_post('/api/web/logout', self.logout)
        r.add_get('/api/web/me', self.w_me)
        r.add_get('/api/web/feed', self.w_feed)
        r.add_get('/api/web/search', self.w_search)
        r.add_get('/api/web/consoles', self.w_consoles)
        r.add_post('/api/web/remove', self.w_remove)
        r.add_post('/api/web/play', self.w_play)
        r.add_post('/api/web/ctl', self.w_ctl)
        r.add_get('/api/web/state', self.w_state)


def make_app(cfg, **kw):
    srv = Server(cfg, **kw)

    @web.middleware
    async def blocked(req, handler):            # YouTube refuses us: a clear answer, no retries
        try:
            return await handler(req)
        except Blocked as e:
            raise web.HTTPServiceUnavailable(text=srv.egress.message() if '/api/web/' in req.path else str(e))
    app = web.Application(client_max_size=64 * 1024, middlewares=[blocked])
    srv.routes(app)
    root = app
    if cfg.base:
        root = web.Application()
        root.add_subapp(cfg.base, app)
    root['srv'] = srv

    async def bg(_):
        tasks = [asyncio.get_running_loop().create_task(srv.hub.janitor()),
                 asyncio.get_running_loop().create_task(net_janitor(srv.net))]
        if cfg.update_h > 0:
            tasks.append(asyncio.get_running_loop().create_task(update_ytdlp(cfg.update_h)))
        yield
        for t in tasks:
            t.cancel()
        srv.hub.close()
        await srv.egress.close()
    root.cleanup_ctx.append(bg)
    return root


async def net_janitor(net):
    """Timeouts of internet play: challenges, games, consoles that went away."""
    while True:
        await asyncio.sleep(2)
        net.sweep()


async def update_ytdlp(hours):
    """Keeps yt-dlp current (YouTube changes often); the next call uses it."""
    import sys
    while True:
        await asyncio.sleep(hours * 3600)
        p = await asyncio.create_subprocess_exec(sys.executable, '-m', 'pip', 'install', '-q', '-U', '--no-cache-dir',
                                                 'yt-dlp[default]', stdout=asyncio.subprocess.DEVNULL,
                                                 stderr=asyncio.subprocess.PIPE)
        _, err = await p.communicate()
        log.info('yt-dlp update: %s', 'ok' if p.returncode == 0 else err.decode(errors='replace')[-200:])


def main():
    logging.basicConfig(level=os.environ.get('LOG_LEVEL', 'INFO'), format='%(asctime)s %(levelname)s %(message)s')
    cfg = Config()
    if not cfg.password:
        raise SystemExit('ADMIN_PASSWORD fehlt (Umgebungsvariable)')
    web.run_app(make_app(cfg), port=cfg.port, access_log=None)

