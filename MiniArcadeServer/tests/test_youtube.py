import asyncio
import json
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from tvserver import youtube  # noqa: E402

FAKE = [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fake_ytdlp.py')]


@pytest.mark.parametrize('text,vid', [
    ('dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
    ('https://www.youtube.com/watch?v=dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
    ('https://youtube.com/watch?v=dQw4w9WgXcQ&t=42s&list=PL123', 'dQw4w9WgXcQ'),
    ('youtube.com/watch?v=dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
    ('https://m.youtube.com/watch?v=dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
    ('https://music.youtube.com/watch?v=dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
    ('https://youtu.be/dQw4w9WgXcQ?si=abc', 'dQw4w9WgXcQ'),
    ('https://www.youtube.com/shorts/dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
    ('https://www.youtube.com/live/dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
    ('https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ', 'dQw4w9WgXcQ'),
    ('  dQw4w9WgXcQ  ', 'dQw4w9WgXcQ'),
])
def test_youtube_accepted(text, vid):
    assert youtube.video_id(text) == vid


@pytest.mark.parametrize('text', [
    'https://vimeo.com/123456', 'https://www.dailymotion.com/video/x7xxxx', 'https://example.com/watch?v=dQw4w9WgXcQ',
    'https://youtube.com.evil.com/watch?v=dQw4w9WgXcQ', 'https://evil.com/youtu.be/dQw4w9WgXcQ',
    'https://user:pw@youtube.com/watch?v=dQw4w9WgXcQ', 'https://youtube.com:8443/watch?v=dQw4w9WgXcQ',
    'ftp://youtube.com/watch?v=dQw4w9WgXcQ', 'file:///etc/passwd', 'https://www.youtube.com/playlist?list=PL123',
    'https://www.youtube.com/watch?v=short', 'dQw4w9WgXc', 'dQw4w9WgXcQ1', 'dQw4w9WgX/Q', '', None, 42,
    'https://www.youtube.com/@channel', 'ytsearch:cats', 'https://youtube.com/redirect?q=https://evil.com',
])
def test_others_refused(text):
    assert youtube.video_id(text) is None


def test_ascii_safe():
    assert youtube.ascii_safe('Grüße – Straße', 40) == 'Gruesse - Strasse'
    assert youtube.ascii_safe('日本 abc​  x', 40) == 'abc x'
    t = youtube.ascii_safe('x' * 100, 40)
    assert len(t) == 40 and t.endswith('..')
    assert all(32 <= ord(c) < 127 for c in youtube.ascii_safe('🎵 Musik 🎶 ♥ é', 40))


def test_clean_query():
    assert youtube.clean_query(' katzen\n\x00 ') == 'katzen'
    assert len(youtube.clean_query('a' * 500)) == 80


async def test_ytdlp_youtube_only(tmp_path, monkeypatch):
    log = tmp_path / 'args'
    monkeypatch.setenv('FAKE_YTDLP_LOG', str(log))
    yt = youtube.YouTube(60, FAKE)
    items = await yt.search('katzen', 20)
    assert [i['id'] for i in items] == ['dQw4w9WgXcQ', 'abcdefghijk']       # the playlist is left out
    assert len(items[0]['t']) <= 40 and items[0]['t'].isascii()
    await yt.search('katzen', 20)                                            # cached: yt-dlp not asked again
    r = await yt.resolve('dQw4w9WgXcQ')
    assert r['url'] and r['d'] == 100
    await yt.search('sport', 10, 'week')
    calls = [json.loads(line) for line in log.read_text().splitlines()]
    assert len(calls) == 3
    for c in calls:
        i = c.index('--use-extractors')
        assert c[i + 1] == 'youtube,youtube:search,youtube:search_url'
        assert '--ignore-config' in c
    assert calls[0][-1] == 'ytsearch20:katzen'
    assert calls[1][-1] == 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
    assert calls[2][-1].startswith('https://www.youtube.com/results?search_query=sport')
    with pytest.raises(AssertionError):
        await yt.resolve('../../etc/x')
    assert await yt.search('   ') == []
    asyncio.get_running_loop()
