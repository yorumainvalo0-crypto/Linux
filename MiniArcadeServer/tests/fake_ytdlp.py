"""Stands in for yt-dlp in the tests: notes its arguments, answers with fixed data."""
import json
import os
import sys

args = sys.argv[1:]
log = os.environ.get('FAKE_YTDLP_LOG')
if log:
    with open(log, 'a') as f:
        f.write(json.dumps(args) + '\n')
target = args[-1]
if 'botcheck' in target:
    sys.stderr.write("ERROR: [youtube] abc: Sign in to confirm you're not a bot. Use --cookies-from-browser\n")
    sys.exit(1)
if target.startswith('ytsearch') or 'results?search_query=' in target:
    print(json.dumps({'_type': 'playlist', 'entries': [
        {'id': 'dQw4w9WgXcQ', 'ie_key': 'Youtube', 'title': 'Grüße – ein sehr, sehr langer Titel mit Ümlauten und mehr Text',
         'channel': 'Kanal', 'duration': 212, 'view_count': 10},
        {'id': 'abcdefghijk', 'ie_key': 'Youtube', 'title': 'Zweites', 'channel': 'K2', 'duration': 61, 'view_count': 99},
        {'id': 'PLxyz', 'ie_key': 'YoutubeTab', '_type': 'playlist', 'title': 'a playlist'}]}))
elif target.startswith('https://www.youtube.com/watch?v='):
    print(json.dumps({'id': target[-11:], 'extractor_key': 'Youtube', 'title': 'Video ' + target[-11:], 'channel': 'Kanal',
                      'duration': 100, 'url': 'https://example.invalid/v.mp4', 'http_headers': {'User-Agent': 'x'}}))
else:
    sys.stderr.write('ERROR: Unsupported URL: %s\n' % target)
    sys.exit(1)
