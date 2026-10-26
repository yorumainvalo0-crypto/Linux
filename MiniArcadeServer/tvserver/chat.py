"""Chat between the consoles: every console may take one chat name (unique,
upper and lower case count as the same), then write to everybody else who
took one. The history stays on the server (SQLite on the volume) for
KEEP_DAYS, at most PAIR_MAX messages per two people; a console can block a
name, then nothing of that name reaches it.

One name per device: the console also sends a device id (a hash of its
chip's MAC). A console that comes anew with the same device id (flashed
with "erase", so a new token) replaces the old account; the old name and
its history go. The old account is deleted, never handed over, so a copied
device id can't read anybody's messages.

The consoles show plain ASCII and the German letters, nothing else: the
server keeps only those."""
import re
import sqlite3
import time

NAME_RE = re.compile(r'^[A-Za-z0-9_-]{3,12}$')
TEXT_BYTES = 120                # one message, UTF-8
KEEP_DAYS = 30                  # history this long ...
PAIR_MAX = 500                  # ... and at most so many messages per two people
PAGE = 20                       # messages in one answer
LIST_MAX = 20                   # people in one answer
ANSWER_BYTES = 3600             # an answer fits the console's buffer (4 kB)
ONLINE_S = 60                   # a console counts as online this long after a chat request
GERMAN = set('äöüÄÖÜß')
DEV_RE = re.compile(r'^[0-9a-f]{8,64}$')


def clean_text(s):
    """Printable ASCII and the German letters; runs of white space become one space."""
    out = []
    for ch in str(s):
        if ch in '\r\n\t':
            ch = ' '
        if (' ' <= ch <= '~') or ch in GERMAN:
            if ch == ' ' and out and out[-1] == ' ':
                continue
            out.append(ch)
    t = ''.join(out).strip()
    while len(t.encode()) > TEXT_BYTES:
        t = t[:-1]
    return t.rstrip()


class Chat:
    def __init__(self, path, clock=None, keep_days=KEEP_DAYS):
        self.db = sqlite3.connect(path or ':memory:')
        self.clock = clock or time.time
        self.keep_s = keep_days * 86400
        self.seen = {}                 # console id -> time of its last chat request
        self.also_online = lambda cid: False   # online for internet play counts too (set by the app)
        self.db.executescript('''
            CREATE TABLE IF NOT EXISTS users (cid TEXT PRIMARY KEY, name TEXT NOT NULL, key TEXT NOT NULL UNIQUE,
                                              dev TEXT, since INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS users_dev ON users(dev);
            CREATE TABLE IF NOT EXISTS msgs (id INTEGER PRIMARY KEY AUTOINCREMENT, frm TEXT NOT NULL, rcp TEXT NOT NULL,
                                             t INTEGER NOT NULL, x TEXT NOT NULL);
            CREATE INDEX IF NOT EXISTS msgs_pair ON msgs(frm, rcp, id);
            CREATE INDEX IF NOT EXISTS msgs_rcp ON msgs(rcp, id);
            CREATE TABLE IF NOT EXISTS reads (cid TEXT NOT NULL, other TEXT NOT NULL, upto INTEGER NOT NULL,
                                              PRIMARY KEY (cid, other));
            CREATE TABLE IF NOT EXISTS blocks (cid TEXT NOT NULL, other TEXT NOT NULL, PRIMARY KEY (cid, other));
        ''')
        self.db.commit()
        self.sweep()

    # ---------------- people ----------------
    def touch(self, cid):
        self.seen[cid] = self.clock()

    def online(self, cid):
        return self.clock() - self.seen.get(cid, -1e9) < ONLINE_S or self.also_online(cid)

    def name_of(self, cid):
        r = self.db.execute('SELECT name FROM users WHERE cid=?', (cid,)).fetchone()
        return r[0] if r else ''

    def cid_of(self, name):
        r = self.db.execute('SELECT cid FROM users WHERE key=?', (str(name).lower(),)).fetchone()
        return r[0] if r else None

    def dev_of(self, cid):
        r = self.db.execute('SELECT dev FROM users WHERE cid=?', (cid,)).fetchone()
        return (r[0] or '') if r else ''

    def set_name(self, cid, name, dev=''):
        """Takes or changes the name. Raises ValueError('bad') or ValueError('taken')."""
        name = str(name).strip()
        if not NAME_RE.match(name):
            raise ValueError('bad')
        dev = str(dev).lower() if DEV_RE.match(str(dev).lower()) else ''
        owner = self.cid_of(name)
        if owner and owner != cid and not (dev and self.dev_of(owner) == dev):   # the own device's old name is free
            raise ValueError('taken')
        if dev:                                         # one account per device: an older one goes
            for (old,) in self.db.execute('SELECT cid FROM users WHERE dev=? AND cid<>?', (dev, cid)).fetchall():
                self.remove(old, commit=False)
        if self.name_of(cid):
            self.db.execute('UPDATE users SET name=?, key=?, dev=COALESCE(NULLIF(?, \'\'), dev) WHERE cid=?',
                            (name, name.lower(), dev, cid))
        else:
            self.db.execute('INSERT INTO users (cid, name, key, dev, since) VALUES (?,?,?,?,?)',
                            (cid, name, name.lower(), dev or None, int(self.clock())))
        self.db.commit()
        return name

    def remove(self, cid, commit=True):
        """The account and everything of it (a console removed, or replaced on its device)."""
        self.db.execute('DELETE FROM users WHERE cid=?', (cid,))
        self.db.execute('DELETE FROM msgs WHERE frm=? OR rcp=?', (cid, cid))
        self.db.execute('DELETE FROM reads WHERE cid=? OR other=?', (cid, cid))
        self.db.execute('DELETE FROM blocks WHERE cid=? OR other=?', (cid, cid))
        if commit:
            self.db.commit()

    def blocked(self, cid, other):
        return self.db.execute('SELECT 1 FROM blocks WHERE cid=? AND other=?', (cid, other)).fetchone() is not None

    def block(self, cid, other, on):
        if on:
            self.db.execute('INSERT OR IGNORE INTO blocks (cid, other) VALUES (?,?)', (cid, other))
        else:
            self.db.execute('DELETE FROM blocks WHERE cid=? AND other=?', (cid, other))
        self.db.commit()

    def unread(self, cid, other=None):
        """Unread messages for cid (from one person, or from all it did not block)."""
        q = ('SELECT COUNT(*) FROM msgs m LEFT JOIN reads r ON r.cid=m.rcp AND r.other=m.frm '
             'WHERE m.rcp=? AND m.id>COALESCE(r.upto, 0) '
             'AND NOT EXISTS (SELECT 1 FROM blocks b WHERE b.cid=m.rcp AND b.other=m.frm)')
        args = [cid]
        if other:
            q += ' AND m.frm=?'
            args.append(other)
        return self.db.execute(q, args).fetchone()[0]

    def people(self, cid, q='', off=0):
        """Everybody with a name, alphabetical, from q on (the start of a name)."""
        q = str(q).strip().lower()
        rows = self.db.execute('SELECT cid, name FROM users WHERE cid<>? AND key>=? ORDER BY key LIMIT ? OFFSET ?',
                               (cid, q, LIST_MAX + 1, max(0, int(off)))).fetchall()
        out = [{'n': n, 'o': 1 if self.online(c) else 0, 'b': 1 if self.blocked(cid, c) else 0}
               for c, n in rows[:LIST_MAX]]
        return out, len(rows) > LIST_MAX

    def chats(self, cid):
        """The people cid wrote with, the newest conversation first: unread, minutes since."""
        rows = self.db.execute('''
            SELECT other, MAX(id), MAX(t) FROM (
                SELECT rcp AS other, id, t FROM msgs WHERE frm=?
                UNION ALL SELECT frm AS other, id, t FROM msgs WHERE rcp=?)
            GROUP BY other ORDER BY MAX(id) DESC LIMIT ?''', (cid, cid, LIST_MAX)).fetchall()
        now = self.clock()
        out = []
        for other, _, t in rows:
            n = self.name_of(other)
            if not n:
                continue
            out.append({'n': n, 'u': self.unread(cid, other), 'o': 1 if self.online(other) else 0,
                        'b': 1 if self.blocked(cid, other) else 0, 'm': int(max(0, now - t) // 60)})
        return out

    # ---------------- messages ----------------
    def send(self, cid, other, text):
        """Stores a message; returns its id. Raises ValueError('empty' / 'blocked')."""
        x = clean_text(text)
        if not x:
            raise ValueError('empty')
        if self.blocked(other, cid) or self.blocked(cid, other):
            raise ValueError('blocked')
        cur = self.db.execute('INSERT INTO msgs (frm, rcp, t, x) VALUES (?,?,?,?)', (cid, other, int(self.clock()), x))
        mid = cur.lastrowid
        # the pair keeps its newest PAIR_MAX
        self.db.execute('''DELETE FROM msgs WHERE id IN (
            SELECT id FROM msgs WHERE (frm=? AND rcp=?) OR (frm=? AND rcp=?) ORDER BY id DESC LIMIT -1 OFFSET ?)''',
                        (cid, other, other, cid, PAIR_MAX))
        self.db.commit()
        return mid

    def messages(self, cid, other, before=0, after=0):
        """Up to PAGE messages of the two, oldest first: the newest ones, or the ones before /
        after an id. Marks what cid got from the other as read. Returns (list, more older)."""
        now = self.clock()
        q = 'SELECT id, frm, t, x FROM msgs WHERE ((frm=? AND rcp=?) OR (frm=? AND rcp=?))'
        args = [cid, other, other, cid]
        if after:
            q += ' AND id>? ORDER BY id ASC LIMIT ?'
            args += [int(after), PAGE + 1]
        else:
            if before:
                q += ' AND id<?'
                args.append(int(before))
            q += ' ORDER BY id DESC LIMIT ?'
            args.append(PAGE + 1)
        rows = self.db.execute(q, args).fetchall()
        more = len(rows) > PAGE
        rows = rows[:PAGE]
        if not after:
            rows.reverse()
        out, size = [], 0
        for mid, frm, t, x in rows:
            it = {'i': mid, 'm': 1 if frm == cid else 0, 'a': int(max(0, now - t) // 60), 'x': x}
            size += len(x.encode()) + 40
            out.append(it)
        while size > ANSWER_BYTES and out:              # the console's buffer: the far end goes
            drop = out.pop(0 if not after else -1)
            size -= len(drop['x'].encode()) + 40
            more = True
        got = [it['i'] for it in out if not it['m']]
        if got:
            self.db.execute('INSERT INTO reads (cid, other, upto) VALUES (?,?,?) '
                            'ON CONFLICT(cid, other) DO UPDATE SET upto=MAX(upto, excluded.upto)',
                            (cid, other, max(got)))
            self.db.commit()
        return out, more

    def sweep(self):
        """Old messages go."""
        self.db.execute('DELETE FROM msgs WHERE t<?', (int(self.clock() - self.keep_s),))
        self.db.commit()
