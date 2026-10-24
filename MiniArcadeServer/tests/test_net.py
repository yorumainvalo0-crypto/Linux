"""Internet multiplayer (tvserver/net.py): lobby, friend codes, challenges,
the relay with its numbering, timeouts and limits. The timeouts run on a
pretend clock (Relay.clock), the waiting polls on the real one."""
import asyncio
import json
import os
import sys
import time

import numpy as np
import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from tvserver import app as tvapp, net, store as tvstore, youtube  # noqa: E402
from tvserver.store import FC_ALPHA, Store  # noqa: E402

FAKE = [sys.executable, os.path.join(HERE, 'fake_ytdlp.py')]


async def no_thumb(vid):
    return np.full((45, 80, 3), 200, np.uint8)


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


async def make_cli(aiohttp_client, tmp_path, **env):
    e = {'ADMIN_PASSWORD': 'geheim', 'DATA_DIR': str(tmp_path), 'YTDLP_UPDATE_HOURS': '0',
         'BASE_PATH': '/tv', 'CONSOLE_PIN': '13572468'}
    e.update(env)
    app = tvapp.make_app(tvapp.Config(e), yt=youtube.YouTube(60, FAKE), thumb_fetch=no_thumb)
    c = await aiohttp_client(app)
    c.srv = app['srv']
    c.clock = Clock()
    c.srv.net.clock = c.clock
    c.dir = tmp_path
    return c


@pytest.fixture
async def cli(aiohttp_client, tmp_path):
    return await make_cli(aiohttp_client, tmp_path)


class Con:
    """One console: its token and the numbering of its messages."""

    def __init__(self, cli, tok):
        self.cli, self.h = cli, {'X-Token': tok}
        self.got = 0
        self.seq = 1
        self.sid = 0
        self.code = ''

    async def poll(self, status=200, **d):
        d.setdefault('sid', self.sid)
        d.setdefault('got', self.got)
        r = await self.cli.post('/tv/api/net/poll', json=d, headers=self.h)
        assert r.status == status, await r.text()
        if status != 200:
            return None
        j = await r.json()
        self.code = j['me']
        return j

    async def post(self, path, status=200, **params):
        r = await self.cli.post('/tv/api/net/' + path, params=params, headers=self.h)
        assert r.status == status, (path, r.status, await r.text())
        return await r.json() if r.content_type == 'application/json' else await r.text()

    def msgs(self, j):
        """The messages of an answer, taken: [(seq, hex)]."""
        out = []
        for part in filter(None, j.get('msg', '').split(',')):
            seq, hx = part.split('.')
            out.append((int(seq), hx))
            self.got = max(self.got, int(seq))
        return out


async def pair(cli, code, name):
    r = await cli.post('/tv/api/pair', params={'code': code, 'name': name, 'pin': '13572468'})
    assert r.status == 200
    return Con(cli, (await r.json())['token'])


async def hello(cli, code, name='', status=200, headers=None):
    """A console without the PIN (NET_OPEN): its token for internet play."""
    r = await cli.post('/tv/api/net/hello', params={'code': code, 'name': name}, headers=headers or {})
    assert r.status == status, await r.text()
    if status != 200:
        return None
    j = await r.json()
    c = Con(cli, j['token'])
    c.code = j['me']
    return c


async def two(cli):
    a = await pair(cli, 'AA11', 'ANNA')
    b = await pair(cli, 'BB22', 'BEN')
    await a.poll(n='ANNA', g=1)
    await b.poll(n='BEN')
    return a, b


async def session(cli, game=1, rtt_a=None, rtt_b=None, rb_a=0, rb_b=0):
    """Two consoles in a game: a challenged b, b said yes."""
    a, b = await two(cli)
    if rtt_a or rb_a:
        await a.poll(rtt=rtt_a or 100, rb=rb_a)
    if rtt_b or rb_b:
        await b.poll(rtt=rtt_b or 100, rb=rb_b)
    oid = (await a.post('invite', to=b.code, g=game))['oid']
    iid = (await b.poll())['inv']['iid']
    j = await b.post('answer', iid=iid, yes=1)
    b.sid = j['ses']['sid']
    ja = await a.poll()
    assert ja['out'] == {'oid': oid, 'st': 'ok'}
    a.sid = ja['ses']['sid']
    return a, b, ja['ses'], j['ses']


# ---------------- authentication ----------------
async def test_auth(cli):
    for path in ('poll', 'invite', 'cancel', 'answer', 'leave', 'friend'):
        r = await cli.post('/tv/api/net/' + path, json={})
        assert r.status == 401, path
        r = await cli.post('/tv/api/net/' + path, json={}, headers={'X-Token': 'wrong'})
        assert r.status == 401, path
    r = await cli.get('/tv/api/net/me')
    assert r.status == 401
    a = await pair(cli, 'AA11', 'ANNA')
    r = await cli.get('/tv/api/net/me', headers=a.h)
    d = await r.json()
    assert r.status == 200 and d['n'] == 'ANNA' and len(d['me']) == 6
    cli.srv.store.remove(cli.srv.store.by_token(a.h['X-Token']))      # removed on the phone page: not paired
    r = await cli.post('/tv/api/net/poll', json={}, headers=a.h)
    assert r.status == 401


# ---------------- lobby and friend codes ----------------
async def test_lobby(cli):
    a = await pair(cli, 'AA11', 'ANNA')
    b = await pair(cli, 'BB22', 'BEN')
    await pair(cli, 'CC33', 'CARL')                                    # paired, never online
    j = await a.poll(n='anna!', g=3)                                   # the name as the consoles show it
    assert set(j['me']) <= set(FC_ALPHA) and len(j['me']) == 6
    assert 'lob' not in j                                              # the lists only when asked for
    j = await b.poll(n='BEN', l=1)
    assert j['lob'] == [{'c': a.code, 'n': 'ANNA', 'g': 3, 'b': 0, 'f': 0}]
    assert j['fr'] == []
    assert (await b.poll())['me'] == j['me']                           # the code stays
    cli.clock.t += 20
    await b.poll()
    assert [x['n'] for x in (await b.poll(l=1))['lob']] == ['ANNA']
    cli.clock.t += 11                                                  # 31 s without a poll: off the list
    assert (await b.poll(l=1))['lob'] == []
    j = await a.poll(l=1)
    assert [x['n'] for x in j['lob']] == ['BEN']


async def test_friend_codes_kept(cli, tmp_path):
    a = await pair(cli, 'AA11', 'ANNA')
    b = await pair(cli, 'BB22', 'BEN')
    await a.poll()
    await b.poll()
    assert a.code != b.code
    st = Store(str(tmp_path / 'state.json'))                           # on the volume
    codes = {c['fc'] for c in st.consoles().values()}
    assert codes == {a.code, b.code}


async def test_friends(cli, tmp_path):
    a = await pair(cli, 'AA11', 'ANNA')
    b = await pair(cli, 'BB22', 'BEN')
    c = await pair(cli, 'CC33', 'CARL')
    for x, n in ((a, 'ANNA'), (b, 'BEN'), (c, 'CARL')):
        await x.poll(n=n)
    await b.post('friend', 404, c='ZZZZZZ', on=1)                      # nobody has it
    assert (await b.post('friend', 409, c=b.code, on=1))['why'] == 'self'
    j = await b.post('friend', c=a.code.lower(), on=1)                 # typed in small letters: fine
    assert j == {'ok': 1, 'c': a.code, 'n': 'ANNA', 'o': 1}
    j = await a.poll(l=1)                                              # both ways
    assert j['fr'] == [{'c': b.code, 'n': 'BEN', 'o': 1, 'b': 0}]
    assert [x['f'] for x in j['lob'] if x['n'] == 'BEN'] == [1]
    assert j['lob'][0]['n'] == 'BEN'                                   # friends first
    cli.clock.t += 31                                                  # BEN goes away: offline friend
    j = await a.poll(l=1)
    assert j['fr'] == [{'c': b.code, 'n': 'BEN', 'o': 0, 'b': 0}] and j['lob'] == []
    st = json.loads((tmp_path / 'state.json').read_text())             # kept on the volume
    ida = cli.srv.store.by_token(a.h['X-Token'])
    idb = cli.srv.store.by_token(b.h['X-Token'])
    assert st['consoles'][ida]['friends'] == [idb] and st['consoles'][idb]['friends'] == [ida]
    await a.post('friend', c=b.code, on=0)                             # removed: on both
    assert (await b.poll(l=1))['fr'] == []
    cli.srv.store.set_friend(ida, idb, True)
    cli.srv.store.remove(idb)                                          # a removed console is nobody's friend
    assert cli.srv.store.friends(ida) == []


async def test_friend_limits(cli):
    a = await pair(cli, 'AA11', 'ANNA')
    await a.poll()
    for k in range(5):
        await a.post('friend', 404, c='ZZZZZ%d' % (k + 2), on=1)       # guessing ...
    await a.post('friend', 429, c='ZZZZZZ', on=1)                      # ... is slowed down


# ---------------- challenges ----------------
async def test_invite_flow(cli):
    a, b = await two(cli)
    c = await pair(cli, 'CC33', 'CARL')
    assert (await a.post('invite', 409, to=a.code, g=1))['why'] == 'self'
    await a.post('invite', 404, to='ZZZZZZ', g=1)
    j = await c.poll()                                                 # CARL online for a moment
    cli.clock.t += 31
    await a.poll()
    await b.poll()
    assert (await a.post('invite', 409, to=c.code, g=1))['why'] == 'offline'
    oid = (await a.post('invite', to=b.code, g=4))['oid']
    j = await b.poll()
    assert j['inv']['ic'] == a.code and j['inv']['in'] == 'ANNA' and j['inv']['ig'] == 4 and 25 <= j['inv']['is'] <= 30
    assert (await a.poll())['out'] == {'oid': oid, 'st': 'wait'}
    await b.post('answer', iid=j['inv']['iid'], yes=0)                 # no
    assert (await a.poll())['out'] == {'oid': oid, 'st': 'no'}
    assert 'inv' not in await b.poll()
    await b.post('answer', 404, iid=j['inv']['iid'], yes=1)            # answered already
    oid = (await a.post('invite', to=b.code, g=2))['oid']              # taken back
    await a.post('cancel', oid=oid)
    assert (await a.poll())['out']['st'] == 'back' and 'inv' not in await b.poll()
    await a.post('cancel', 404, oid='nothing')
    oid = (await a.post('invite', to=b.code, g=2))['oid']              # no answer in 30 s
    cli.clock.t += 20
    await b.poll()
    await a.poll()
    cli.clock.t += 11
    cli.srv.net.sweep()
    assert (await a.poll())['out']['st'] == 'timeout' and 'inv' not in await b.poll()
    cli.clock.t += 11
    cli.srv.net.sweep()
    assert 'out' not in await a.poll()                                 # the result is not kept for ever


async def test_invite_inviter_gone(cli):
    a, b = await two(cli)
    await a.post('invite', to=b.code, g=1)
    iid = (await b.poll())['inv']['iid']
    cli.clock.t += 13                                                  # ANNA stopped polling
    await b.poll()
    cli.srv.net.sweep()
    assert 'inv' not in await b.poll()
    await b.post('answer', 404, iid=iid, yes=1)


async def test_session_and_busy(cli):
    a, b, sa, sb = await session(cli, game=7)
    assert sa['sid'] == sb['sid'] and 1 <= sa['sid'] <= 65535 and sa['g'] == sb['g'] == 7
    assert sa['first'] + sb['first'] == 1                              # one of them begins
    assert sa['oc'] == b.code and sa['on'] == 'BEN' and sb['oc'] == a.code and sb['on'] == 'ANNA'
    c = await pair(cli, 'CC33', 'CARL')
    j = await c.poll(l=1)
    assert sorted((x['n'], x['b']) for x in j['lob']) == [('ANNA', 1), ('BEN', 1)]
    assert (await c.post('invite', 409, to=a.code, g=1))['why'] == 'busy'
    assert (await a.post('invite', 409, to=c.code, g=1))['why'] == 'playing'


async def test_lead(cli):
    assert net.lead_for(20, 20) == 6                                   # near: the least delay
    assert net.lead_for(60, 60) == 10
    assert net.lead_for(100, 100) == 15
    assert net.lead_for(400, 400) == 24                                # far: capped
    a, b, sa, sb = await session(cli, game=3, rtt_a=80, rtt_b=60)
    assert sa['lead'] == sb['lead'] == net.lead_for(80, 60)
    assert 'rb' not in sa and 'rb' not in sb                           # consoles of before: lockstep


async def test_lead_rollback(cli):
    assert net.lead_rb(20, 20) == 3                                    # near: the least delay
    assert net.lead_rb(100, 100) == 4
    assert net.lead_rb(400, 400) == 6                                  # far: capped, the consoles guess the rest
    a, b, sa, sb = await session(cli, game=3, rtt_a=80, rtt_b=60, rb_a=1, rb_b=1)
    assert sa['rb'] == sb['rb'] == 1 and sa['lead'] == sb['lead'] == net.lead_rb(80, 60)


async def test_lead_rollback_one_side(cli):
    a, b, sa, sb = await session(cli, game=3, rtt_a=80, rtt_b=60, rb_a=1, rb_b=0)
    assert 'rb' not in sa and 'rb' not in sb and sa['lead'] == net.lead_for(80, 60)   # one of before: lockstep


# ---------------- the relay ----------------
async def test_relay_order(cli):
    a, b, _, _ = await session(cli)
    j = await a.poll(m=[[1, '4d4101'], [2, '4d4102'], [3, '4d4103']])
    assert j['ack'] == 3
    j = await a.poll(m=[[3, '4d4103'], [4, '4d4104'], [5, '4d4105']])  # 3 again (its answer was lost)
    assert j['ack'] == 5
    j = await b.poll()
    assert b.msgs(j) == [(1, '4d4101'), (2, '4d4102'), (3, '4d4103'), (4, '4d4104'), (5, '4d4105')]
    assert 'msg' not in await b.poll()                                 # taken: not again
    j = await a.poll(m=[[7, '4d4107']])                                # 6 is missing: 7 waits
    assert j['ack'] == 5 and 'msg' not in await b.poll()
    j = await a.poll(m=[[6, '4d4106'], [7, '4d4107']])
    assert j['ack'] == 7
    j = await b.poll(got=5)
    assert b.msgs(j) == [(6, '4d4106'), (7, '4d4107')]
    j = await b.poll(got=5)                                            # our answer got lost: once more
    assert [m[0] for m in b.msgs(j)] == [6, 7]
    j = await b.poll(m=[[1, 'ff00ff']])                                # the other way
    assert j['ack'] == 1
    assert a.msgs(await a.poll()) == [(1, 'ff00ff')]
    j = await b.poll(m=[[1, 'ff00ff']])                                # the same again: not twice
    assert 'msg' not in await a.poll()


async def test_relay_wakes_waiting_poll(cli):
    a, b, _, _ = await session(cli)
    await b.poll()                                                     # all seen

    async def later():
        await asyncio.sleep(0.15)
        await a.poll(m=[[1, '4d41']])
    t0 = time.monotonic()
    task = asyncio.ensure_future(later())
    j = await b.poll(wait=5000)
    took = time.monotonic() - t0
    await task
    assert b.msgs(j) == [(1, '4d41')] and took < 2 and j['h'] >= 100
    t0 = time.monotonic()
    j = await b.poll(wait=300)                                         # nothing: held for the time asked
    assert 'msg' not in j and time.monotonic() - t0 >= 0.25 and j['h'] >= 250
    net.WAIT_MAX_S = 0.4                                               # never longer than WAIT_MAX_S (25 s)
    try:
        t0 = time.monotonic()
        j = await b.poll(wait=999999)
        assert 0.35 <= time.monotonic() - t0 < 2
    finally:
        net.WAIT_MAX_S = 25


async def test_challenge_wakes_waiting_poll(cli):
    a, b = await two(cli)
    await b.poll()

    async def later():
        await asyncio.sleep(0.1)
        await a.post('invite', to=b.code, g=2)
    task = asyncio.ensure_future(later())
    t0 = time.monotonic()
    j = await b.poll(wait=5000)
    await task
    assert j['inv']['ig'] == 2 and time.monotonic() - t0 < 2


async def test_leave(cli):
    a, b, _, _ = await session(cli)
    await a.poll(m=[[1, '4d4101'], [2, '4d4106']])                     # the last move, then "bye"
    await a.post('leave', sid=a.sid)
    assert 'ses' not in await a.poll(sid=0)
    j = await b.poll()
    assert b.msgs(j) == [(1, '4d4101'), (2, '4d4106')]
    assert 'se' not in j['ses']                                        # the end only after the last messages
    j = await b.poll()
    assert j['ses']['se'] == 'left'
    await b.post('leave', sid=b.sid)
    assert cli.srv.net.sessions == {} and cli.srv.net.of == {}
    j = await a.poll(m=[[3, '4d41']], sid=b.sid)                       # an old session: ignored
    assert 'ack' not in j


async def test_gone_and_lost(cli):
    a, b, _, _ = await session(cli)
    cli.clock.t += 8
    await a.poll()
    cli.clock.t += 5                                                   # BEN silent for 13 s
    cli.srv.net.sweep()
    assert (await a.poll())['ses']['se'] == 'gone'
    j = await b.poll()                                                 # BEN comes back: too late
    assert j['ses']['se'] == 'lost'
    await a.post('leave', sid=a.sid)
    await b.post('leave', sid=b.sid)
    assert cli.srv.net.sessions == {}
    oid = (await a.post('invite', to=b.code, g=1))['oid']              # and they can play again
    iid = (await b.poll())['inv']['iid']
    j = await b.post('answer', iid=iid, yes=1)
    assert 'se' not in j['ses'] and (await a.poll())['out'] == {'oid': oid, 'st': 'ok'}


async def test_cleanup(cli):
    a, b, _, _ = await session(cli)
    await a.post('leave', sid=a.sid)
    cli.clock.t += 31                                                  # BEN never comes back
    cli.srv.net.sweep()
    assert cli.srv.net.sessions == {}
    cli.clock.t += 200
    cli.srv.net.sweep()
    assert len(cli.srv.net.pres) == 2 and cli.srv.net.invites == {}   # seen lately: still known
    cli.clock.t += 400
    cli.srv.net.sweep()
    assert cli.srv.net.pres == {}


async def test_new_game_after_one(cli):
    a, b, s1, _ = await session(cli)
    await a.poll(m=[[1, '4d41']])
    await b.poll()
    await b.post('leave', sid=b.sid)                                  # BEN is back in the lobby ...
    oid = (await b.post('invite', to=a.code, g=2))['oid']             # ... and challenges ANNA, who has not left yet
    iid = (await a.poll())['inv']['iid']
    j = await a.post('answer', iid=iid, yes=1)
    assert j['ses']['sid'] != s1['sid'] and j['ses']['g'] == 2
    a.sid = j['ses']['sid']
    jb = await b.poll()
    assert jb['out'] == {'oid': oid, 'st': 'ok'} and jb['ses']['sid'] == a.sid
    assert len(cli.srv.net.sessions) == 1


# ---------------- limits and bad input ----------------
async def test_bad_input(cli):
    a, b, _, _ = await session(cli)
    await a.poll(400, m=[[1, 'xyz']])
    await a.poll(400, m=[[1, '4d4']])                                  # half a byte
    await a.poll(400, m=[[1, 'aa' * 33]])                              # longer than 32 bytes
    await a.poll(400, m=[[k + 1, 'aa'] for k in range(17)])            # more than 16 at once
    await a.poll(400, m='4d41')
    await a.poll(400, wait='soon')
    r = await cli.post('/tv/api/net/poll', data='{nope', headers={**a.h, 'Content-Type': 'application/json'})
    assert r.status == 400
    await a.post('invite', 400, to=b.code, g='x')


async def test_queue_full(cli):
    a, b, _, _ = await session(cli)
    seq = 1
    for _ in range(4):                                                 # 64 waiting for BEN
        await a.poll(m=[[seq + k, '4d41'] for k in range(16)])
        seq += 16
    await a.poll(429, m=[[seq, '4d41']])                               # BEN does not take them
    j = await b.poll()
    assert len(b.msgs(j)) == 16
    await b.poll()                                                     # got 16: room again
    assert (await a.poll(m=[[seq, '4d41']]))['ack'] == 65


async def test_message_rate(cli):
    a, b, _, _ = await session(cli)
    seq, refused = 1, 0
    for _ in range(12):                                                # 192 new messages at once
        r = await cli.post('/tv/api/net/poll', json={'sid': a.sid, 'm': [[seq + k, 'aa'] for k in range(16)]},
                           headers=a.h)
        if r.status == 429:
            refused += 1
        else:
            seq += 16
        b.msgs(await b.poll())                                         # BEN takes them: no queue limit
    assert refused > 0 and seq <= 81                                   # 40/s, 80 at once


async def test_poll_rate(cli):
    a = await pair(cli, 'AA11', 'ANNA')
    codes = []
    for _ in range(90):
        r = await cli.post('/tv/api/net/poll', json={}, headers=a.h)
        codes.append(r.status)
    assert codes[:80] == [200] * 80 and 429 in codes


async def test_invite_rate(cli):
    a, b = await two(cli)
    for k in range(8):
        await a.post('invite', to=b.code, g=1)
    await a.post('invite', 429, to=b.code, g=1)


async def test_messages_not_kept(cli, tmp_path):
    a, b, _, _ = await session(cli)
    await a.poll(m=[[1, 'deadbeef']])
    await b.poll()
    for f in tmp_path.rglob('*'):
        if f.is_file():
            assert b'deadbeef' not in f.read_bytes(), f


# ---------------- NET_OPEN: internet play without the PIN ----------------
async def test_hello_open(cli):
    a = await hello(cli, 'aa11', 'Anna')
    assert len(a.code) == 6 and set(a.code) <= set(FC_ALPHA)
    j = await a.poll(n='ANNA', l=1)
    assert j['me'] == a.code and j['lob'] == []
    r = await cli.get('/tv/api/net/me', headers=a.h)
    assert r.status == 200 and (await r.json())['n'] == 'ANNA'
    for path in ('/tv/api/cats', '/tv/api/feed', '/tv/api/list', '/tv/api/tv/now'):   # the TV: only with the PIN
        r = await cli.get(path, headers=a.h)
        assert r.status == 401, path
    r = await cli.post('/tv/api/tv/play', params={'id': 'dQw4w9WgXcQ'}, headers=a.h)
    assert r.status == 401
    b = await hello(cli, 'BB22')                                       # no name: one is made up from the code
    st = cli.srv.store
    assert st.console(st.by_token(b.h['X-Token']))['name'] == 'MABB22'
    assert (await (await cli.get('/tv/api/net/me', headers=b.h)).json())['n'] == 'MABB22'
    assert st.scope(st.by_token(a.h['X-Token'])) == 'net'
    p = await pair(cli, 'CC33', 'CARL')                                # a paired console: TV and internet
    assert st.scope(st.by_token(p.h['X-Token'])) == ''
    assert (await cli.get('/tv/api/cats', headers=p.h)).status == 200
    await p.poll(n='CARL')
    oid = (await a.post('invite', to=p.code, g=1))['oid']              # both kinds play together
    iid = (await p.poll())['inv']['iid']
    j = await p.post('answer', iid=iid, yes=1)
    assert j['ses']['oc'] == a.code and (await a.poll())['out'] == {'oid': oid, 'st': 'ok'}


async def test_hello_closed(aiohttp_client, tmp_path):
    cli = await make_cli(aiohttp_client, tmp_path, NET_OPEN='0')
    r = await cli.post('/tv/api/net/hello', params={'code': 'AA11', 'name': 'ANNA'})
    assert r.status == 403
    assert cli.srv.store.consoles() == {}
    cid, tok = cli.srv.store.add_console('BB22', 'BEN', scope='net')   # from the time it was open
    r = await cli.post('/tv/api/net/poll', json={}, headers={'X-Token': tok})
    assert r.status == 401                                             # closed: the PIN is needed again
    a = await pair(cli, 'AA11', 'ANNA')                                # the PIN works as before
    assert (await a.poll(n='ANNA'))['me']
    for v in ('0', 'no', 'off', 'false'):
        assert not tvapp.Config({'NET_OPEN': v}).net_open
    assert tvapp.Config({}).net_open and tvapp.Config({'NET_OPEN': '1'}).net_open


async def test_hello_rate(aiohttp_client, tmp_path):
    cli = await make_cli(aiohttp_client, tmp_path, TRUSTED_PROXIES='127.0.0.1')
    x = {'X-Forwarded-For': '203.0.113.7'}
    for k in range(tvapp.HELLO_BURST):
        await hello(cli, 'AA%d' % k, headers=x)
    await hello(cli, 'AA9', status=429, headers=x)                     # the same address: slowed down
    await hello(cli, 'BB1', headers={'X-Forwarded-For': '198.51.100.9'})   # another one behind the proxy: fine
    assert len(cli.srv.store.consoles()) == tvapp.HELLO_BURST + 1


async def test_hello_rate_untrusted(aiohttp_client, tmp_path):
    cli = await make_cli(aiohttp_client, tmp_path)                     # no TRUSTED_PROXIES: the header counts for nothing
    for k in range(tvapp.HELLO_BURST):
        await hello(cli, 'AA%d' % k, headers={'X-Forwarded-For': '203.0.113.%d' % k})
    await hello(cli, 'AA9', status=429, headers={'X-Forwarded-For': '198.51.100.9'})


async def test_hello_then_pin(cli):
    """A console that played online without the PIN pairs for the TV: the same console."""
    a = await hello(cli, 'AA11', 'ANNA')
    b = await pair(cli, 'BB22', 'BEN')
    await a.poll()
    await b.poll()
    await a.post('friend', c=b.code, on=1)
    st = cli.srv.store
    ida = st.by_token(a.h['X-Token'])
    r = await cli.post('/tv/api/pair', params={'code': 'AA11', 'name': 'ANNA', 'pin': '13572468'}, headers=a.h)
    j = await r.json()
    assert r.status == 200 and j['id'] == ida
    new = Con(cli, j['token'])
    assert (await cli.get('/tv/api/cats', headers=new.h)).status == 200      # the TV now
    assert (await new.poll(l=1))['me'] == a.code                            # the same code ...
    assert [f['c'] for f in (await new.poll(l=1))['fr']] == [b.code]       # ... and friends
    assert (await cli.post('/tv/api/net/poll', json={}, headers=a.h)).status == 401   # the old token is gone
    assert st.scope(ida) == '' and len(st.consoles()) == 2
    r = await cli.post('/tv/api/pair', params={'code': 'CC33', 'pin': '13572468'}, headers=b.h)
    assert (await r.json())['id'] != st.by_token(b.h['X-Token'])            # a paired one: a new console as before


async def test_hello_limit_kept(cli, monkeypatch):
    monkeypatch.setattr(tvstore, 'NET_CONSOLES_MAX', 3)
    cons = [await hello(cli, 'AA%d' % k) for k in range(4)]
    await pair(cli, 'PP11', 'PAUL')                                    # paired ones never go
    st = cli.srv.store
    assert len(st.consoles()) == 4
    assert st.by_token(cons[0].h['X-Token']) is None and st.by_token(cons[3].h['X-Token'])


async def test_lobby_everybody(cli):
    """Everybody online is in the lobby without friends: the free ones first."""
    a = await hello(cli, 'AA11')
    b = await hello(cli, 'BB22')
    c = await hello(cli, 'CC33')
    d = await hello(cli, 'DD44')
    for x, n, g in ((a, 'ANNA', 0), (b, 'BEN', 3), (c, 'CARL', 0), (d, 'DORA', 5)):
        await x.poll(n=n, g=g)
    oid = (await a.post('invite', to=c.code, g=1))['oid']              # ANNA and CARL play
    iid = (await c.poll())['inv']['iid']
    await c.post('answer', iid=iid, yes=1)
    assert (await a.poll())['out'] == {'oid': oid, 'st': 'ok'}
    j = await b.poll(l=2)
    assert [(x['n'], x['b'], x['f']) for x in j['lob']] == [('DORA', 0, 0), ('ANNA', 1, 0), ('CARL', 1, 0)]
    assert j['lob'][0]['g'] == 5 and j['fr'] == [] and j['rc'] == []
    await b.post('friend', c=d.code, on=1)
    oid = (await b.post('invite', to=d.code, g=2))['oid']              # a challenge needs no friendship
    assert (await d.poll())['inv']['ic'] == b.code


async def test_lobby_recent(cli):
    a = await hello(cli, 'AA11')
    b = await hello(cli, 'BB22')
    c = await hello(cli, 'CC33')
    await a.poll(n='ANNA')
    await c.poll(n='CARL')
    cli.clock.t += 25
    await b.poll(n='BEN')
    cli.clock.t += 100                                                 # ANNA and CARL went away
    j = await b.poll(l=2)
    assert j['lob'] == []
    assert sorted((x['n'], x['m'], x['f']) for x in j['rc']) == [('ANNA', 2, 0), ('CARL', 2, 0)]
    assert 'rc' not in await b.poll(l=1)                               # older consoles: as before
    cli.clock.t += 60
    await c.poll()                                                     # CARL is back: online, not "seen lately"
    j = await b.poll(l=2)
    assert [x['n'] for x in j['lob']] == ['CARL'] and [(x['n'], x['m']) for x in j['rc']] == [('ANNA', 3)]
    cli.clock.t += 600                                                 # 10 minutes: forgotten
    await b.poll()
    await c.poll()
    cli.srv.net.sweep()
    j = await b.poll(l=2)
    assert j['rc'] == [] and [x['n'] for x in j['lob']] == ['CARL']
