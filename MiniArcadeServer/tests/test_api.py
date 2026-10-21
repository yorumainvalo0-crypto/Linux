import asyncio
import os
import sys

import numpy as np
import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from tvserver import app as tvapp, codec, palette, stream, youtube  # noqa: E402

FAKE = [sys.executable, os.path.join(HERE, 'fake_ytdlp.py')]


async def no_thumb(vid):
    return np.full((45, 80, 3), 200, np.uint8)


@pytest.fixture
async def cli(aiohttp_client, tmp_path):
    cfg = tvapp.Config({'ADMIN_PASSWORD': 'geheim', 'DATA_DIR': str(tmp_path), 'YTDLP_UPDATE_HOURS': '0',
                        'BASE_PATH': '/tv', 'CONSOLE_PIN': '13572468'})
    app = tvapp.make_app(cfg, yt=youtube.YouTube(60, FAKE), thumb_fetch=no_thumb)
    c = await aiohttp_client(app)
    c.srv = app['srv']
    return c


async def login(cli):
    r = await cli.post('/tv/api/web/login', json={'password': 'geheim'})
    assert r.status == 200


async def pair(cli, code='AB12', name='MAX', pin='13572468'):
    r = await cli.post('/tv/api/pair', params={'code': code, 'name': name, 'pin': pin})
    assert r.status == 200
    d = await r.json()
    assert len(d['token']) > 30
    return d['token']


async def test_auth_required(cli):
    for path in ('/tv/api/cats', '/tv/api/feed?c=0', '/tv/api/search?q=x', '/tv/api/list?k=fav', '/tv/api/tv/now',
                 '/tv/api/tv/stream', '/tv/api/thumb/dQw4w9WgXcQ', '/tv/api/web/me', '/tv/api/web/consoles',
                 '/tv/api/web/feed?c=0', '/tv/api/web/search?q=x', '/tv/api/web/state?console=x'):
        r = await cli.get(path)
        assert r.status == 401, path
    for path in ('/tv/api/web/play', '/tv/api/web/ctl', '/tv/api/web/remove', '/tv/api/tv/play'):
        r = await cli.post(path, json={})
        assert r.status == 401, path
    r = await cli.get('/tv/api/cats', headers={'X-Token': 'wrong'})
    assert r.status == 401
    r = await cli.post('/tv/api/web/login', json={'password': 'falsch'})
    assert r.status == 401
    r = await cli.get('/tv/api/web/me', cookies={'mas': '9999999999.deadbeef'})
    assert r.status == 401
    r = await cli.get('/tv/')
    assert r.status == 200 and 'MiniArcade TV' in await r.text()


async def test_pairing_and_lists(cli):
    r = await cli.get('/tv/api/ping')
    assert (await r.json())['ok'] == 1                                 # "test connection": no token needed
    r = await cli.post('/tv/api/pair', params={'code': 'AB12', 'name': 'MAX', 'pin': '1357246'})
    assert r.status == 403
    await login(cli)
    tok = await pair(cli)
    tok2 = await pair(cli, 'CD34', 'EVA')                             # every console its own token
    assert tok2 != tok
    r = await cli.get('/tv/api/web/me')
    assert (await r.json())['pin'] == '13572468'                       # the owner sees the PIN on the phone page
    h = {'X-Token': tok}
    r = await cli.get('/tv/api/cats', headers=h)
    assert (await r.json())['cats'][0] == 'Beliebt'
    r = await cli.get('/tv/api/search', params={'q': 'katzen'}, headers=h)
    items = (await r.json())['items']
    assert items[0]['id'] == 'dQw4w9WgXcQ' and set(items[0]) == {'id', 't', 'c', 'd', 'f'}
    r = await cli.get('/tv/api/feed', params={'c': '0'}, headers=h)          # popular: the others merged
    assert [i['id'] for i in (await r.json())['items']] == ['abcdefghijk', 'dQw4w9WgXcQ']
    r = await cli.post('/tv/api/fav', params={'id': 'dQw4w9WgXcQ', 'on': '1'}, headers=h)
    assert r.status == 200
    r = await cli.get('/tv/api/list', params={'k': 'fav'}, headers=h)
    assert [i['f'] for i in (await r.json())['items']] == [1]
    r = await cli.post('/tv/api/fav', params={'id': 'https://vimeo.com/1', 'on': '1'}, headers=h)
    assert r.status == 400
    r = await cli.get('/tv/api/thumb/dQw4w9WgXcQ', headers=h)
    body = await r.read()
    assert len(body) == 80 * 45 and abs(palette.to_rgb(np.frombuffer(body, np.uint8)).mean() - 200) < 4
    r = await cli.get('/tv/api/thumb/..%2Fsecret', headers=h)
    assert r.status in (400, 404)
    r = await cli.get('/tv/api/web/consoles')
    cons = (await r.json())['consoles']
    assert [c['code'] for c in cons] == ['AB12', 'CD34']
    r = await cli.post('/tv/api/web/remove', json={'console': cons[0]['id']})
    assert r.status == 200
    r = await cli.get('/tv/api/cats', headers=h)
    assert r.status == 401                                             # removed: the token is dead
    r = await cli.get('/tv/api/cats', headers={'X-Token': tok2})
    assert r.status == 200                                             # the other one still works


async def test_wrong_pins_lock_out(cli, monkeypatch):
    codes = []
    for k in range(6):
        r = await cli.post('/tv/api/pair', params={'code': 'ZZ99', 'name': 'X', 'pin': '1111%04d' % k})
        codes.append(r.status)
    assert codes == [403] * 5 + [429]
    r = await cli.post('/tv/api/pair', params={'code': 'ZZ99', 'name': 'X', 'pin': '13572468'})
    assert r.status == 429                                             # locked: even the right PIN waits
    t = tvapp.now() + tvapp.PIN_LOCK_S + 1
    monkeypatch.setattr(tvapp, 'now', lambda: t)
    await pair(cli, 'ZZ99')                                            # after the lock it works again


async def test_pin_made_when_missing(tmp_path):
    cfg = tvapp.Config({'ADMIN_PASSWORD': 'x', 'DATA_DIR': str(tmp_path), 'YTDLP_UPDATE_HOURS': '0'})
    pin = tvapp.Server(cfg, yt=youtube.YouTube(60, FAKE)).pin
    assert len(pin) == 8 and pin.isdigit()
    assert tvapp.Server(cfg, yt=youtube.YouTube(60, FAKE)).pin == pin  # kept on the volume


async def test_play_only_youtube(cli):
    await login(cli)
    tok = await pair(cli)
    cid = (await (await cli.get('/tv/api/web/consoles')).json())['consoles'][0]['id']
    for bad in ('https://vimeo.com/123', 'https://evil.com/watch?v=dQw4w9WgXcQ', 'hallo'):
        r = await cli.post('/tv/api/web/play', json={'console': cid, 'url': bad})
        assert r.status == 400
    r = await cli.post('/tv/api/tv/play', params={'id': 'file:///etc/passwd'}, headers={'X-Token': tok})
    assert r.status == 400
    r = await cli.post('/tv/api/web/play', json={'console': cid, 'url': 'https://youtu.be/dQw4w9WgXcQ'})
    s = await r.json()
    assert s['s'] == codec.ST_PLAYING and s['id'] == 'dQw4w9WgXcQ'
    r = await cli.get('/tv/api/list', params={'k': 'hist'}, headers={'X-Token': tok})
    assert (await r.json())['items'][0]['id'] == 'dQw4w9WgXcQ'


async def read_segment(cli, tok, **q):
    r = await cli.get('/tv/api/tv/stream', params={k: str(v) for k, v in q.items()}, headers={'X-Token': tok})
    assert r.status == 200
    return list(codec.records(await r.read()))


async def test_stream_test_picture(cli, monkeypatch):
    monkeypatch.setattr(stream, 'LEAD_MS', 300)
    await login(cli)
    tok = await pair(cli)
    cid = (await (await cli.get('/tv/api/web/consoles')).json())['consoles'][0]['id']
    r = await cli.post('/tv/api/web/play', json={'console': cid, 'test': 1})
    assert r.status == 200
    gen = (await r.json())['g']
    recs = await read_segment(cli, tok)
    inf = codec.parse_info(recs[0])
    assert inf['state'] == codec.ST_PLAYING and inf['gen'] == gen and inf['title'] == 'Testbild'
    frames = recs[1:]
    assert len(frames) >= 5
    screen = np.zeros((240, 320), np.uint8)
    last = None
    for p in frames:
        seq, ts, flags = codec.decode_frame(p, screen)
        assert (flags & 1) == (seq == 0)                               # only the first one is a key frame
        assert last is None or (seq == last[0] + 1 and ts > last[1])
        last = (seq, ts)
    # the next segment goes on where this one ended, with the report
    recs = await read_segment(cli, tok, g=gen, have=last[0], shown=len(frames), late=0, buf=300)
    p = recs[1]
    seq, ts, flags = codec.decode_frame(p, screen)
    assert seq == last[0] + 1 and not flags & 1
    # frames got lost: a key frame comes
    recs = await read_segment(cli, tok, g=gen, have=0)
    assert any(codec.decode_frame(p, screen)[2] & 1 for p in recs[1:])
    # pause from the console: the info says so, no frames
    recs = await read_segment(cli, tok, g=gen, have=-1, a='pause')
    inf = codec.parse_info(recs[0])
    assert inf['state'] == codec.ST_PAUSED and inf['gen'] != gen and len(recs) == 1
    assert cli.srv.hub.player(cid).run is None                        # ffmpeg (here: the pattern) stopped
    # sharp mode, playing again from the phone
    r = await cli.post('/tv/api/web/ctl', json={'console': cid, 'a': 'mode', 'm': 1})
    r = await cli.post('/tv/api/web/ctl', json={'console': cid, 'a': 'play'})
    g2 = (await r.json())['g']
    recs = await read_segment(cli, tok, g=inf['gen'])
    inf = codec.parse_info(recs[0])
    assert inf['gen'] == g2 and inf['mode'] == 1 and inf['pos'] > 0
    assert codec.decode_frame(recs[1], screen)[2] == 3                 # key frame, sharp
    # the console goes away: the run is stopped after a while
    p = cli.srv.hub.player(cid)
    p.last_pull -= stream.IDLE_KILL_S + 1
    cli.srv.hub.sweep()
    assert p.run is None
    r = await cli.get('/tv/api/tv/ctl', params={'a': 'stop'}, headers={'X-Token': tok})
    assert (await r.json())['s'] == codec.ST_IDLE
    r = await cli.get('/tv/api/tv/ctl', params={'a': 'reboot'}, headers={'X-Token': tok})
    assert r.status == 400
    await asyncio.sleep(0)


async def test_stream_real_time(cli, monkeypatch):
    """Segment after segment, as the console asks: about 25 frames a second,
    never ahead of the wall clock by more than the lead, one key frame only."""
    monkeypatch.setattr(stream, 'LEAD_MS', 300)
    await login(cli)
    tok = await pair(cli)
    cid = (await (await cli.get('/tv/api/web/consoles')).json())['consoles'][0]['id']
    gen = (await (await cli.post('/tv/api/web/play', json={'console': cid, 'test': 1})).json())['g']
    screen = np.zeros((240, 320), np.uint8)
    loop = asyncio.get_running_loop()
    t0, have, frames, keys, first_ts = loop.time(), -1, 0, 0, None
    while loop.time() - t0 < 2.5:
        recs = await read_segment(cli, tok, g=gen, have=have, shown=10, late=0, buf=4)
        for p in recs[1:]:
            seq, ts, flags = codec.decode_frame(p, screen)
            if first_ts is None:
                first_ts = ts
            assert ts - first_ts <= (loop.time() - t0) * 1000 + 300 + 100     # not ahead of real time
            have, frames, keys = seq, frames + 1, keys + (flags & 1)
    assert 45 <= frames <= 2.5 * 25 + 10 and keys == 1


class _Req:
    def __init__(self, remote, headers):
        self.remote, self.headers = remote, headers


@pytest.mark.parametrize('trusted', ['', '127.0.0.1', '10.0.0.0/8,127.0.0.0/8'])
async def test_client_address_behind_proxy(aiohttp_client, tmp_path, trusted):
    """X-Forwarded-For counts only from TRUSTED_PROXIES: then a wrong-PIN lock hits
    the one client, otherwise everybody behind the same connection address."""
    cfg = tvapp.Config({'ADMIN_PASSWORD': 'pw', 'DATA_DIR': str(tmp_path), 'YTDLP_UPDATE_HOURS': '0',
                        'CONSOLE_PIN': '13572468', 'TRUSTED_PROXIES': trusted})
    app = tvapp.make_app(cfg, yt=youtube.YouTube(60, FAKE))
    cli = await aiohttp_client(app)
    a = {'X-Forwarded-For': '203.0.113.7'}
    for k in range(5):
        r = await cli.post('/api/pair', params={'code': 'AAA%d' % k, 'pin': '0'}, headers=a)
        assert r.status == 403
    b = {'X-Forwarded-For': '198.51.100.9, 10.1.2.3'}               # a client, then a hop (trusted if 10/8 is)
    r = await cli.post('/api/pair', params={'code': 'BBBB', 'pin': '13572468'}, headers=b)
    assert r.status == (429 if not trusted else 200)
    srv = app['srv']
    if trusted:
        assert srv.ip(_Req('127.0.0.1', b)) == ('198.51.100.9' if '10.0.0.0/8' in trusted else '10.1.2.3')
    assert srv.ip(_Req('192.0.2.1', a)) == '192.0.2.1'                # not a proxy: its header is ignored
