"""Consoles, their tokens (only as a hash), history and favourites in one small
JSON file on the volume. Written whole and renamed into place."""
import hashlib
import json
import os
import secrets
import time

HISTORY_MAX = 50
FAVS_MAX = 100


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

    def save(self):
        if not self.path:
            return
        tmp = self.path + '.tmp'
        with open(tmp, 'w', encoding='utf-8') as f:
            json.dump(self.data, f, ensure_ascii=False, indent=1)
        os.replace(tmp, self.path)

    # ---------------- consoles ----------------
    def add_console(self, code, name):
        """A new console; returns (console id, token). The token is shown once."""
        cid = secrets.token_hex(4)
        tok = secrets.token_urlsafe(32)
        self.data['consoles'][cid] = {'code': code, 'name': name, 'tok': token_hash(tok), 'since': int(time.time()),
                                      'history': [], 'favs': []}
        self.save()
        return cid, tok

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
            self.save()
            return True
        return False

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
