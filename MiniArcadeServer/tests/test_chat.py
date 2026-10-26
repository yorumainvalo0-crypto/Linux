import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from tvserver import app as tvapp, youtube  # noqa: E402
from tvserver.chat import Chat, clean_text, PAGE, PAIR_MAX  # noqa: E402

FAKE = [sys.executable, os.path.join(HERE, 'fake_ytdlp.py')]


@pytest.fixture
async def cli(aiohttp_client, tmp_path):
    cfg = tvapp.Config({'ADMIN_PASSWORD': 'geheim', 'DATA_DIR': str(tmp_path), 'YTDLP_UPDATE_HOURS': '0',
                        'BASE_PATH': '/tv', 'CONSOLE_PIN': '13572468'})
    c = await aiohttp_client(tvapp.make_app(cfg, yt=youtube.YouTube(60, FAKE)))
    c.srv = c.server.app['srv']
    return c


async def hello(cli, code):
    r = await cli.post('/tv/api/net/hello', params={'code': code, 'name': 'X'})
    assert r.status == 200
    return {'X-Token': (await r.json())['token']}


async def post(cli, h, path, **body):
    r = await cli.post('/tv/api/chat/' + path, json=body, headers=h)
    return r.status, (await r.json() if r.content_type == 'application/json' else await r.text())


async def test_names(cli):
    a, b = await hello(cli, 'AA11'), await hello(cli, 'BB22')
    assert await post(cli, a, 'me') == (200, {'n': '', 'u': 0})
    assert (await post(cli, a, 'list'))[0] == 409                     # nothing before a name
    assert await post(cli, a, 'name', n='ab') == (409, {'why': 'bad'})
    assert await post(cli, a, 'name', n='Ana Lena') == (409, {'why': 'bad'})
    assert await post(cli, a, 'name', n='Anna_1') == (200, {'n': 'Anna_1'})
    assert await post(cli, b, 'name', n='ANNA_1') == (409, {'why': 'taken'})   # case does not count
    assert await post(cli, b, 'name', n='Ben') == (200, {'n': 'Ben'})
    assert await post(cli, a, 'name', n='anna_1') == (200, {'n': 'anna_1'})   # the own name: another case
    st, d = await post(cli, b, 'people')
    assert st == 200 and [p['n'] for p in d['p']] == ['anna_1'] and d['p'][0]['o'] == 1
    r = await cli.post('/tv/api/chat/me', headers={'X-Token': 'wrong'})
    assert r.status == 401


async def test_messages_read_block(cli):
    a, b = await hello(cli, 'AA11'), await hello(cli, 'BB22')
    await post(cli, a, 'name', n='Anna')
    await post(cli, b, 'name', n='Ben')
    assert (await post(cli, a, 'send', n='Nobody', x='hi'))[0] == 404
    assert await post(cli, a, 'send', n='ben', x='  ') == (409, {'why': 'empty'})
    st, d = await post(cli, a, 'send', n='ben', x='Hallo Ben, wie geht\'s? Grüße\n"x":1')
    assert st == 200
    r = await cli.post('/tv/api/chat/msgs', json={'n': 'Anna'}, headers=b)
    raw = await r.read()
    assert 'Grüße'.encode() in raw                                    # UTF-8, not \u escapes
    assert b'\\"x\\":1' in raw
    assert (await post(cli, b, 'me'))[1]['u'] == 0                     # read now
    st, d = await post(cli, a, 'list')
    assert d['c'][0]['n'] == 'Ben' and d['c'][0]['u'] == 0
    await post(cli, b, 'send', n='Anna', x='gut')
    await post(cli, b, 'send', n='Anna', x='und dir?')
    assert (await post(cli, a, 'me'))[1]['u'] == 2
    st, d = await post(cli, a, 'list')
    assert d == {'c': [{'n': 'Ben', 'u': 2, 'o': 1, 'b': 0, 'm': 0}], 'u': 2}
    st, d = await post(cli, a, 'msgs', n='Ben')
    assert [(m['m'], m['x']) for m in d['m']] == [(1, 'Hallo Ben, wie geht\'s? Grüße "x":1'), (0, 'gut'), (0, 'und dir?')]
    last = d['m'][-1]['i']
    assert (await post(cli, a, 'msgs', n='Ben', after=last))[1]['m'] == []
    # Ben blocks Anna: she can't write, he sees nothing new of her
    assert await post(cli, b, 'block', n='Anna', on=1) == (200, {'ok': 1, 'b': 1})
    assert await post(cli, a, 'send', n='Ben', x='hallo?') == (409, {'why': 'blocked'})
    assert await post(cli, b, 'send', n='Anna', x='x') == (409, {'why': 'blocked'})
    assert (await post(cli, b, 'msgs', n='Anna'))[1]['b'] == 1
    await post(cli, b, 'block', n='Anna', on=0)
    assert (await post(cli, a, 'send', n='Ben', x='wieder da'))[0] == 200


async def test_one_account_per_device_and_removal(cli):
    a = await hello(cli, 'AA11')
    b = await hello(cli, 'BB22')
    await post(cli, a, 'name', n='Anna', dev='00112233aabbccdd')
    await post(cli, b, 'name', n='Ben')
    await post(cli, a, 'send', n='Ben', x='hi')
    a2 = await hello(cli, 'AA11')                                     # the same board, flashed with erase
    assert await post(cli, a2, 'name', n='Anna', dev='00112233aabbccdd') == (200, {'n': 'Anna'})
    assert (await post(cli, a, 'list'))[0] == 409                      # the old account is gone ...
    assert (await post(cli, a2, 'list'))[1]['c'] == []                  # ... its history with it
    assert (await post(cli, b, 'list'))[1]['c'] == []
    # a console removed on the phone page loses its name
    srv = cli.srv
    cid = srv.chat.cid_of('Ben')
    srv.store.remove(cid)
    assert srv.chat.cid_of('Ben') is None


def test_clean_text_and_limits():
    assert clean_text('a\tb\n\nc  d\x00 €ä') == 'a b c d ä'
    assert len(clean_text('ü' * 100).encode()) <= 120
    t = [0.0]
    c = Chat('', clock=lambda: t[0], keep_days=30)
    c.set_name('a', 'Anna')
    c.set_name('b', 'Ben')
    for i in range(PAIR_MAX + 30):
        c.send('a', 'b', 'm%d' % i)
    m, more = c.messages('b', 'a')
    assert more and len(m) == PAGE and m[-1]['x'] == 'm%d' % (PAIR_MAX + 29)
    assert c.db.execute('SELECT COUNT(*) FROM msgs').fetchone()[0] == PAIR_MAX
    older, _ = c.messages('b', 'a', before=m[0]['i'])
    assert older[-1]['i'] < m[0]['i']
    t[0] = 31 * 86400
    c.sweep()
    assert c.db.execute('SELECT COUNT(*) FROM msgs').fetchone()[0] == 0
    # a long page still fits the console's buffer
    for i in range(PAGE):
        c.send('a', 'b', 'x' * 120)
    m, _ = c.messages('b', 'a')
    assert sum(len(x['x']) + 40 for x in m) <= 3600
