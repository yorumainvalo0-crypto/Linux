"""YouTube only: video ids and youtube.com / youtu.be links are accepted, every
other address is refused before yt-dlp sees it, and yt-dlp itself runs with
its YouTube extractors only (no generic extractor, no account, no cookies),
through the egress (egress.py), gently: results cached, a limit of requests
per minute for the whole server, pauses between yt-dlp's own requests."""
import asyncio
import json
import re
import sys
import time
import unicodedata
import urllib.parse

from .egress import Egress, check_args

ID_RE = re.compile(r'^[A-Za-z0-9_-]{11}$')
HOSTS = {'youtube.com', 'www.youtube.com', 'm.youtube.com', 'music.youtube.com', 'youtube-nocookie.com',
         'www.youtube-nocookie.com'}
EXTRACTORS = 'youtube,youtube:search,youtube:search_url'
# best video up to 480 lines, H.264 first (decodes fastest); else whatever there is
FORMAT = 'bv*[height<=480][vcodec^=avc1]/bv*[height<=480]/b[height<=480]/wv*/w'


def video_id(text):
    """An 11 character id from an id or a YouTube link, else None."""
    if not isinstance(text, str):
        return None
    s = text.strip()
    if ID_RE.match(s):
        return s
    if '://' not in s:
        s = 'https://' + s
    try:
        u = urllib.parse.urlsplit(s)
        if u.scheme not in ('http', 'https') or u.username or u.password or u.port not in (None, 80, 443):
            return None
    except ValueError:
        return None
    host = (u.hostname or '').lower()
    parts = [p for p in u.path.split('/') if p]
    cand = None
    if host in ('youtu.be', 'www.youtu.be'):
        cand = parts[0] if parts else None
    elif host in HOSTS:
        if u.path == '/watch':
            cand = (urllib.parse.parse_qs(u.query).get('v') or [None])[0]
        elif len(parts) >= 2 and parts[0] in ('shorts', 'live', 'embed', 'v'):
            cand = parts[1]
    return cand if cand and ID_RE.match(cand) else None


def watch_url(vid):
    assert ID_RE.match(vid)
    return 'https://www.youtube.com/watch?v=' + vid


def clean_query(q):
    """A search text: printable, at most 80 characters, '' if nothing is left."""
    if not isinstance(q, str):
        return ''
    q = ''.join(ch for ch in q if ch.isprintable()).strip()
    return q[:80]


def ascii_safe(s, n):
    """Plain ASCII for the console's font, cut to n characters."""
    tr = {'ä': 'ae', 'ö': 'oe', 'ü': 'ue', 'Ä': 'Ae', 'Ö': 'Oe', 'Ü': 'Ue', 'ß': 'ss', '–': '-', '—': '-',
          '’': "'", '‘': "'", '“': '"', '”': '"', '…': '...', '|': '-'}
    out = []
    for ch in s or '':
        ch = tr.get(ch, ch)
        for c in ch:
            if 32 <= ord(c) < 127:
                out.append(c)
            elif c.isspace():
                out.append(' ')
            else:
                d = unicodedata.normalize('NFKD', c).encode('ascii', 'ignore').decode()
                out.append(d)
    t = re.sub(r'\s+', ' ', ''.join(out)).strip()
    return t if len(t) <= n else t[:n - 2].rstrip() + '..'


def item(e):
    """A search / list entry from yt-dlp -> the console's small item."""
    vid = e.get('id')
    if not vid or not ID_RE.match(vid):
        return None
    if e.get('ie_key') not in (None, 'Youtube') or e.get('_type') == 'playlist':
        return None
    return {'id': vid, 't': ascii_safe(e.get('title') or '', 40),
            'c': ascii_safe(e.get('channel') or e.get('uploader') or '', 24),
            'd': int(e.get('duration') or 0), 'v': int(e.get('view_count') or 0),
            'live': 1 if e.get('live_status') == 'is_live' else 0}


class YouTube:
    """yt-dlp as a program (so an update needs no restart), results cached."""

    def __init__(self, cache_s=600, ytdlp=None, egress=None, per_min=20, sleep_s=1.0):
        self.cache_s = cache_s
        self.ytdlp = ytdlp or [sys.executable, '-m', 'yt_dlp']
        self.egress = egress or Egress()
        self.cache = {}
        self.lock = asyncio.Semaphore(2)                # yt-dlp runs at once
        self.per_min, self.sleep_s = per_min, sleep_s
        self.recent = []                                 # start times of the last minute

    async def _slot(self):
        """At most per_min yt-dlp runs a minute for the whole server: waits a little, else refuses."""
        for _ in range(20):
            t = time.monotonic()
            self.recent = [x for x in self.recent if t - x < 60]
            if len(self.recent) < self.per_min:
                self.recent.append(t)
                return
            await asyncio.sleep(0.5)
        raise RuntimeError('zu viele YouTube-Anfragen - gleich nochmal')

    def args(self, args):
        a = ['--ignore-config', '--no-cookies', '--no-cookies-from-browser', '--no-warnings',
             '--use-extractors', EXTRACTORS, *self.egress.ytdlp_args()]
        if self.sleep_s:
            a += ['--sleep-requests', '%g' % self.sleep_s]
        a += args
        check_args(a)                                    # no login, ever
        return a

    async def _run(self, args, timeout=90):
        self.egress.check()
        full = self.args(args)
        async with self.lock:
            await self._slot()
            self.egress.check()
            p = await asyncio.create_subprocess_exec(*self.ytdlp, *full, stdout=asyncio.subprocess.PIPE,
                                                     stderr=asyncio.subprocess.PIPE)
            try:
                out, err = await asyncio.wait_for(p.communicate(), timeout)
            except asyncio.TimeoutError:
                p.kill()
                await p.wait()
                raise RuntimeError('yt-dlp: zu langsam')
        if p.returncode:
            text = err.decode(errors='replace').strip()
            self.egress.note_error(text)                 # a bot check: Blocked, and the pause starts
            msg = text.splitlines()
            raise RuntimeError((msg[-1] if msg else 'yt-dlp Fehler')[:200])
        return out

    async def _cached(self, key, make):
        hit = self.cache.get(key)
        now = time.monotonic()
        if hit and now - hit[0] < self.cache_s:
            return hit[1]
        val = await make()
        self.cache[key] = (now, val)
        if len(self.cache) > 500:                       # drop the oldest
            for k, _ in sorted(self.cache.items(), key=lambda kv: kv[1][0])[:100]:
                self.cache.pop(k, None)
        return val

    async def search(self, query, n=20, sort=None):
        """sort None = relevance, 'week' = this week by views."""
        q = clean_query(query)
        if not q:
            return []
        if sort == 'week':
            target = 'https://www.youtube.com/results?search_query=' + urllib.parse.quote(q) + '&sp=CAMSAggD'
            args = ['--playlist-end', str(n), target]
        else:
            args = ['ytsearch%d:%s' % (n, q)]

        async def make():
            out = await self._run(['--flat-playlist', '-J', *args])
            data = json.loads(out)
            items = [item(e) for e in data.get('entries') or []]
            return [i for i in items if i][:n]
        return await self._cached(('s', q, n, sort), make)

    async def resolve(self, vid):
        """Stream address, headers, title, length of a video (cached shortly:
        the addresses expire after some hours)."""
        assert ID_RE.match(vid)

        async def make():
            out = await self._run(['-J', '--no-playlist', '-f', FORMAT, watch_url(vid)])
            d = json.loads(out)
            f = d
            if d.get('requested_formats'):
                f = d['requested_formats'][0]
            if d.get('extractor_key') != 'Youtube' or not f.get('url'):
                raise RuntimeError('kein Video gefunden')
            return {'url': f['url'], 'headers': f.get('http_headers') or d.get('http_headers') or {},
                    'title': ascii_safe(d.get('title') or '', 40),
                    'c': ascii_safe(d.get('channel') or d.get('uploader') or '', 24),
                    'd': int(d.get('duration') or 0), 'live': d.get('is_live') or False}
        hit = self.cache.get(('r', vid))
        if hit and time.monotonic() - hit[0] > min(self.cache_s, 1800):
            self.cache.pop(('r', vid), None)
        return await self._cached(('r', vid), make)
