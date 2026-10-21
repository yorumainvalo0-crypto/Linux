"""The one way out to YouTube. Everything that talks to YouTube - yt-dlp
(search, lists, video addresses), the thumbnails and the video data itself -
goes through YT_PROXY (http:// or socks5://) or from YT_SOURCE_ADDRESS (an
extra address of the server), so the server's main address stays clean.
Without either (fine as well) the server's own address is used.

Never a login: no cookies, no account, no OAuth - the options that could
carry one are refused before yt-dlp starts (FORBIDDEN).

YouTube answers a flagged address with "Sign in to confirm you're not a bot",
403 or 429. Then all YouTube requests stop for YT_COOLDOWN_HOURS instead of
trying again (Blocked)."""
import logging
import re
import time

import aiohttp

log = logging.getLogger('tvserver')

FORBIDDEN = ('--cookies', '--cookies-from-browser', '--username', '--password', '--twofactor', '--netrc',
             '--netrc-location', '--netrc-cmd', '--video-password', '--ap-mso', '--ap-username', '--ap-password',
             '--client-certificate', '--client-certificate-key', '--client-certificate-password', '--add-headers',
             '--load-info-json', '-u', '-p', '-2', '-n')
BOT_RE = re.compile(r"sign in to confirm|not a bot|HTTP Error 403|HTTP Error 429|Too Many Requests|"
                    r"confirm you.re not|unusual traffic", re.I)


class Blocked(RuntimeError):
    """YouTube refuses this connection: nothing is asked until the pause is over."""


def check_args(args):
    """No login, ever: refuses every yt-dlp option that could carry one."""
    for a in args:
        name = a.split('=', 1)[0]
        assert name not in FORBIDDEN, 'refused yt-dlp option ' + name
        assert not (name.startswith('--cookies') or name.startswith('--username') or name.startswith('--password')), name


class Egress:
    def __init__(self, proxy='', source='', cooldown_h=6.0):
        self.proxy = proxy.strip()
        self.source = source.strip()
        self.cooldown_s = cooldown_h * 3600
        self.blocked_until = 0.0              # wall clock
        self.why = ''
        self._session = None
        if self.proxy and not re.match(r'^(http|socks4|socks5)://', self.proxy):
            raise SystemExit('YT_PROXY: http://, socks5:// or socks4:// expected')

    def kind(self):
        return 'proxy' if self.proxy else ('source' if self.source else 'main')

    def warn(self):
        if self.kind() == 'main':
            log.info('YouTube: direct from this server (YT_PROXY / YT_SOURCE_ADDRESS not set)')

    def ytdlp_args(self):
        a = []
        if self.proxy:
            a += ['--proxy', self.proxy]
        if self.source:
            a += ['--source-address', self.source]
        return a

    # ---------------- the pause after a bot check ----------------
    def blocked(self):
        return time.time() < self.blocked_until

    def until_text(self):
        return time.strftime('%H:%M', time.localtime(self.blocked_until))

    def check(self):
        """Raises Blocked while the pause lasts."""
        if self.blocked():
            raise Blocked('YouTube blockiert - Pause bis ' + self.until_text())

    def block(self, why):
        if not self.blocked():
            self.blocked_until = time.time() + self.cooldown_s
            self.why = why[:200]
            log.error('YouTube refused this connection (%s) - no YouTube requests until %s', self.why, self.until_text())

    def note_error(self, text):
        """An error from yt-dlp or a fetch: a bot check starts the pause."""
        if BOT_RE.search(text or ''):
            self.block(text)
            self.check()

    def message(self):
        """For the phone page (German)."""
        if self.blocked():
            return 'YouTube blockiert gerade diese Verbindung - Pause bis %s Uhr' % self.until_text()
        return ''

    # ---------------- HTTP for thumbnails and video data ----------------
    def connector(self):
        local = (self.source, 0) if self.source else None
        if self.proxy:
            from aiohttp_socks import ProxyConnector
            return ProxyConnector.from_url(self.proxy, local_addr=local, rdns=True)
        return aiohttp.TCPConnector(local_addr=local)

    def session(self):
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession(
                connector=self.connector(), cookie_jar=aiohttp.DummyCookieJar(),        # never a cookie
                timeout=aiohttp.ClientTimeout(total=None, sock_connect=20, sock_read=60),
                headers={'User-Agent': 'Mozilla/5.0 (X11; Linux x86_64; rv:128.0) Gecko/20100101 Firefox/128.0'})
        return self._session

    async def close(self):
        if self._session:
            await self._session.close()
