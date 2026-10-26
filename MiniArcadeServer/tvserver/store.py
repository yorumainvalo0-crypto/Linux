"""Consoles, their tokens (only as a hash), history and favourites, the friend
codes and friends of internet play in one small JSON file on the volume.
Written whole and renamed into place."""
import hashlib
import json
import os
import secrets
import time

HISTORY_MAX = 50
FAVS_MAX = 100
FRIENDS_MAX = 16
FC_ALPHA = '23456789ABCDEFGHJKMNPQRSTUVWXYZ'   # friend codes: no 0/O, 1/I/L mix-ups
FC_LEN = 6
NET_CONSOLES_MAX = 500                       # consoles that only came for internet play (NET_OPEN)


def token_hash(tok):
    return hashlib.sha256(tok.encode()).hexdigest()


class Store:
    def __init__(self, path):
        self.path = path
        self.data = {'consoles': {}}
        if path and os.path.exists(path):
            with open(path, encoding='utf-8') as f:
                self.data = json.load(f)
        self.data.setdefault('consoles', {})
        self.on_remove = lambda cid: None          # the chat forgets the console too (set by the app)

    def save(self):
        if not self.path:
            return
        tmp = self.path + '.tmp'
        with open(tmp, 'w', encoding='utf-8') as f:
            json.dump(self.data, f, ensure_ascii=False, indent=1)
        os.replace(tmp, self.path)

    # ---------------- consoles ----------------
    def add_console(self, code, name, scope=''):
        """A new console; returns (console id, token). The token is shown once.
        scope 'net': the token is good for internet play only (no PIN asked, NET_OPEN)."""
        if scope == 'net':                          # never more than NET_CONSOLES_MAX of those: the oldest goes
            net = [cid for cid, c in self.data['consoles'].items() if c.get('scope') == 'net']   # oldest first
            for cid in net[:max(0, len(net) - NET_CONSOLES_MAX + 1)]:
                self.remove(cid)
        cid = secrets.token_hex(4)
        tok = secrets.token_urlsafe(32)
        self.data['consoles'][cid] = {'code': code, 'name': name, 'tok': token_hash(tok), 'since': int(time.time()),
                                      'history': [], 'favs': []}
        if scope:
            self.data['consoles'][cid]['scope'] = scope
        self.save()
        return cid, tok

    def scope(self, cid):
        """'' = paired with the PIN (everything), 'net' = internet play only."""
        return (self.console(cid) or {}).get('scope', '')

    def upgrade(self, cid, name):
        """A console of internet play paired with the PIN: the same console (its friend
        code and friends stay) with a new token that is good for everything."""
        c = self.console(cid)
        tok = secrets.token_urlsafe(32)
        c['tok'] = token_hash(tok)
        c.pop('scope', None)
        if name:
            c['name'] = name
        self.save()
        return tok

    def by_token(self, tok):
        if not tok or len(tok) > 100:
            return None
        h = token_hash(tok)
        for cid, c in self.data['consoles'].items():
            if secrets.compare_digest(c['tok'], h):
                return cid
        return None

    def consoles(self):
        return self.data['consoles']

    def console(self, cid):
        return self.data['consoles'].get(cid)

    def remove(self, cid):
        if self.data['consoles'].pop(cid, None) is not None:
            for c in self.data['consoles'].values():      # nobody keeps it as a friend
                if cid in c.get('friends', []):
                    c['friends'].remove(cid)
            self.save()
            self.on_remove(cid)
            return True
        return False

    # ---------------- internet play: friend codes and friends ----------------
    def friend_code(self, cid):
        """The console's short code for "add a friend"; made once, then kept."""
        c = self.console(cid)
        if not c:
            return ''
        if not c.get('fc'):
            used = {x.get('fc') for x in self.data['consoles'].values()}
            code = ''
            while not code or code in used:
                code = ''.join(secrets.choice(FC_ALPHA) for _ in range(FC_LEN))
            c['fc'] = code
            self.save()
        return c['fc']

    def by_friend_code(self, code):
        code = str(code).upper().strip()
        if len(code) != FC_LEN:
            return None
        for cid, c in self.data['consoles'].items():
            if c.get('fc') == code:
                return cid
        return None

    def friends(self, cid):
        c = self.console(cid)
        return [f for f in (c or {}).get('friends', []) if f in self.data['consoles']]

    def set_friend(self, cid, other, on):
        """Friends go both ways: added or removed on both consoles. False = a list is full."""
        a, b = self.console(cid), self.console(other)
        if not a or not b or cid == other:
            return False
        fa, fb = a.setdefault('friends', []), b.setdefault('friends', [])
        if on:
            if (other not in fa and len(fa) >= FRIENDS_MAX) or (cid not in fb and len(fb) >= FRIENDS_MAX):
                return False
            if other not in fa:
                fa.append(other)
            if cid not in fb:
                fb.append(cid)
        else:
            if other in fa:
                fa.remove(other)
            if cid in fb:
                fb.remove(cid)
        self.save()
        return True

    # ---------------- lists ----------------
    def add_history(self, cid, it):
        c = self.console(cid)
        if not c:
            return
        h = [x for x in c['history'] if x['id'] != it['id']]
        c['history'] = [it] + h[:HISTORY_MAX - 1]
        self.save()

    def set_fav(self, cid, it, on):
        c = self.console(cid)
        if not c:
            return
        f = [x for x in c['favs'] if x['id'] != it['id']]
        if on:
            f = [it] + f[:FAVS_MAX - 1]
        c['favs'] = f
        self.save()

    def is_fav(self, cid, vid):
        c = self.console(cid)
        return bool(c) and any(x['id'] == vid for x in c['favs'])
