"""The way out to YouTube: never a login, the proxy / source address used by
yt-dlp and for the video data, and the pause after a bot check."""
import asyncio
import json
import os
import sys

import pytest
from aiohttp import web

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from tvserver import app as tvapp, codec, egress, youtube  # noqa: E402

FAKE = [sys.executable, os.path.join(HERE, 'fake_ytdlp.py')]


def calls(log):
    return [json.loads(x) for x in log.read_text().splitlines()] if log.exists() else []


async def test_never_a_login(tmp_path, monkeypatch):
    log = tmp_path / 'args'
    monkeypatch.setenv('FAKE_YTDLP_LOG', str(log))
    yt = youtube.YouTube(60, FAKE)
    await yt.search('katzen')
    await yt.resolve('dQw4w9WgXcQ')
    for c in calls(log):
        assert '--no-cookies' in c and '--no-cookies-from-browser' in c
        bad = [a for a in c if a.startswith(('--cookies', '--username', '--password', '--netrc')) or a in ('-u', '-p')]
        assert not bad
    for opt in ('--cookies', '--cookies=x.txt', '--cookies-from-browser', '--username', '--password', '--netrc', '-u'):
        with pytest.raises(AssertionError):
            egress.check_args(['--ignore-config', opt, 'x'])
        with pytest.raises(AssertionError):
            yt.args([opt, 'x'])
    s = egress.Egress().session()
    assert isinstance(s.cookie_jar, egress.aiohttp.DummyCookieJar)       # the HTTP side keeps no cookies either
    await s.close()


async def test_proxy_and_source_for_ytdlp(tmp_path, monkeypatch):
    log = tmp_path / 'args'
    monkeypatch.setenv('FAKE_YTDLP_LOG', str(log))
    yt = youtube.YouTube(60, FAKE, egress=egress.Egress('socks5://home.example:1080', '2001:db8::7'), sleep_s=1)
    await yt.search('katzen')
    c = calls(log)[0]
    assert c[c.index('--proxy') + 1] == 'socks5://home.example:1080'
    assert c[c.index('--source-address') + 1] == '2001:db8::7'
    assert c[c.index('--sleep-requests') + 1] == '1'
    assert egress.Egress().kind() == 'main' and egress.Egress('', '10.0.0.2').kind() == 'source'
    with pytest.raises(SystemExit):
        egress.Egress('ftp://x:1')


async def test_rate_limit(tmp_path, monkeypatch):
    monkeypatch.setenv('FAKE_YTDLP_LOG', str(tmp_path / 'args'))
    yt = youtube.YouTube(0, FAKE, per_min=2, sleep_s=0)
    await yt.search('a')
    await yt.search('b')
    monkeypatch.setattr(asyncio, 'sleep', _no_sleep)
    with pytest.raises(RuntimeError, match='zu viele'):
        await yt.search('c')


async def _no_sleep(_):
    pass


async def test_pause_after_bot_check(tmp_path, monkeypatch, aiohttp_client):
    log = tmp_path / 'args'
    monkeypatch.setenv('FAKE_YTDLP_LOG', str(log))
    eg = egress.Egress(cooldown_h=6)
    yt = youtube.YouTube(60, FAKE, egress=eg)
    with pytest.raises(egress.Blocked, match='Pause bis'):
        await yt.search('botcheck')
    n = len(calls(log))
    with pytest.raises(egress.Blocked):
        await yt.search('katzen')                                    # not asked at all during the pause
    with pytest.raises(egress.Blocked):
        await yt.resolve('dQw4w9WgXcQ')
    assert len(calls(log)) == n
    assert 5.9 * 3600 < eg.blocked_until - __import__('time').time() <= 6 * 3600
    # the console and the phone page are told
    cfg = tvapp.Config({'ADMIN_PASSWORD': 'pw', 'DATA_DIR': str(tmp_path), 'YTDLP_UPDATE_HOURS': '0', 'CONSOLE_PIN': '1234'})
    app = tvapp.make_app(cfg, yt=yt)
    cli = await aiohttp_client(app)
    tok = (await (await cli.post('/api/pair', params={'code': 'AB12', 'pin': '1234'})).json())['token']
    r = await cli.get('/api/search', params={'q': 'x'}, headers={'X-Token': tok})
    assert r.status == 503 and 'YouTube blockiert - Pause bis' in await r.text()
    await cli.post('/api/web/login', json={'password': 'pw'})
    me = await (await cli.get('/api/web/me')).json()
    assert me['blocked'].startswith('YouTube blockiert gerade diese Verbindung - Pause bis') and me['egress'] == 'main'
    r = await cli.get('/api/tv/play', params={'id': 'dQw4w9WgXcQ'}, headers={'X-Token': tok})
    assert r.status == 503
    r = await cli.get('/api/thumb/dQw4w9WgXcQ', headers={'X-Token': tok})
    assert r.status == 503


async def test_video_data_through_the_proxy(tmp_path, aiohttp_client, aiohttp_server):
    """ffmpeg reads the server's relay; the relay fetches through YT_PROXY (here a
    small CONNECT proxy) and passes the byte range on."""
    data = bytes(range(256)) * 64

    async def video(req):
        rng = req.headers.get('Range', '')
        assert 'Cookie' not in req.headers
        a = int(rng[6:rng.index('-')]) if rng.startswith('bytes=') else 0
        return web.Response(status=206 if rng else 200, body=data[a:],
                            headers={'Content-Range': 'bytes %d-%d/%d' % (a, len(data) - 1, len(data))})
    up = web.Application()
    up.router.add_get('/v.mp4', video)
    upstream = await aiohttp_server(up)

    seen = []

    async def proxy(r, w):
        line = (await r.readline()).decode()
        seen.append(line.split()[:2])
        while (await r.readline()) not in (b'\r\n', b''):
            pass
        host, port = line.split()[1].rsplit(':', 1)
        ur, uw = await asyncio.open_connection(host, int(port))
        w.write(b'HTTP/1.1 200 Connection established\r\n\r\n')

        async def pipe(a, b):
            try:
                while (d := await a.read(65536)):
                    b.write(d)
                    await b.drain()
            except ConnectionError:
                pass
            finally:
                b.close()
        await asyncio.gather(pipe(r, uw), pipe(ur, w))
    ps = await asyncio.start_server(proxy, '127.0.0.1', 0)
    pport = ps.sockets[0].getsockname()[1]

    eg = egress.Egress('http://127.0.0.1:%d' % pport)
    cfg = tvapp.Config({'ADMIN_PASSWORD': 'pw', 'DATA_DIR': str(tmp_path), 'YTDLP_UPDATE_HOURS': '0',
                        'CONSOLE_PIN': '1234', 'FFMPEG': 'true'})
    app = tvapp.make_app(cfg, yt=youtube.YouTube(60, FAKE, egress=eg))
    cli = await aiohttp_client(app)
    srv = app['srv']
    local = srv.relay('http://127.0.0.1:%d/v.mp4' % upstream.port, {'User-Agent': 'x', 'Cookie': 'no'})
    assert local.startswith('http://127.0.0.1:') and '/_media/' in local
    r = await cli.get(local[local.index('/_media/'):], headers={'Range': 'bytes=1000-'})
    assert r.status == 206 and await r.read() == data[1000:]
    assert seen and seen[0] == ['CONNECT', '127.0.0.1:%d' % upstream.port]     # through the proxy
    r = await cli.get('/_media/unknown')
    assert r.status == 404
    # a video played: ffmpeg gets the relay, never the YouTube address
    tok = (await (await cli.post('/api/pair', params={'code': 'AB12', 'pin': '1234'})).json())['token']
    await cli.get('/api/tv/play', params={'id': 'dQw4w9WgXcQ'}, headers={'X-Token': tok})
    p = next(iter(srv.hub.players.values()))
    assert await srv.hub.ensure_run(p)
    assert p.run.src.src['url'].startswith('http://127.0.0.1:') and 'example.invalid' not in p.run.src.src['url']
    p.stop()
    ps.close()
    assert codec.ST_IDLE == p.state
