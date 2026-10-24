"""Internet multiplayer for the paired consoles: who is online, friends by a
short code, challenges and a relay that carries the games' messages.

Everything is in memory except the friend codes and the friends lists (they
are in the store on the volume). Game messages are never written anywhere.

The consoles only make short HTTPS requests (one TLS connection, no
WebSocket): POST /api/net/poll is sent again and again. It reports the
console (name, the game it wants, its round trip), carries its outgoing
messages and returns what is new: a challenge, the answer to its own, the
session, the other side's messages. When there is nothing new the server
holds the request a little (the console says how long, at most 25 s), so a
message reaches a waiting console at once.

Messages in a session are numbered per direction. The console repeats its
unconfirmed ones (the answer says up to which number they arrived: "ack")
and says up to which number it has the other side's ("got"); so a lost
request or answer loses nothing and nothing is taken twice."""
import asyncio
import math
import re
import secrets
import time

ONLINE_S = 30           # a console counts as online this long after its last poll
GONE_S = 12             # a player in a game silent this long: the game is over
INVITE_S = 30           # a challenge waits this long for an answer
INVITE_KEEP_S = 10      # ... and its result stays this long for the challenger
LEFT_KEEP_S = 30        # a session the other one left stays this long for the last one
WAIT_MAX_S = 25         # the longest a poll is held
QUEUE_MAX = 64          # messages waiting for one side
MSG_MAX = 16            # messages in one poll
PAYLOAD_MAX = 32        # bytes in one message
LOBBY_MAX = 16          # consoles in the lobby list
RECENT_S = 600          # a console that went offline is listed this long as "seen lately"
RECENT_MAX = 8          # ... at most so many
LEAD_MIN, LEAD_MAX = 6, 24   # real-time games: input delay in ticks of 20 ms
LEAD_RB_MIN, LEAD_RB_MAX = 3, 6   # ... when both consoles play with rollback ("rb" in their polls)
HEX_RE = re.compile(r'^(?:[0-9a-fA-F]{2}){1,%d}$' % PAYLOAD_MAX)


def clean_name(s):
    """A player name as the consoles show it: 6 plain characters."""
    out = ''.join(ch for ch in str(s).upper() if ch.isascii() and (ch.isalnum() or ch == ' '))
    return out.strip()[:6]


def lead_for(rtt_a, rtt_b):
    """Input delay for a real-time game in lockstep. One poll at a time per console:
    an input waits for the next poll and then needs half of both round trips; in
    the console tests (MiniArcadeColor/test, inetpong) the game keeps full speed
    from about 1.1 times both round trips on - a little more for jitter."""
    ms = 1.25 * (rtt_a + rtt_b) + 40
    return max(LEAD_MIN, min(LEAD_MAX, math.ceil(ms / 20)))


def lead_rb(rtt_a, rtt_b):
    """Input delay when both consoles play with rollback (1.19 on): the keys count
    almost at once and the consoles guess the other's for the ticks still missing;
    a little delay keeps those guesses short. Both consoles get the same."""
    ms = 0.25 * (rtt_a + rtt_b) + 20
    return max(LEAD_RB_MIN, min(LEAD_RB_MAX, math.ceil(ms / 20)))


class Presence:
    def __init__(self, t):
        self.seen = t
        self.name = ''
        self.game = 0
        self.rtt = 100
        self.rb = False                # it plays real-time games with rollback
        self.ver = 0                   # bumped by everything a waiting poll should see
        self.sent_ver = -1             # ... the version the last answer showed
        self.ev = asyncio.Event()


class Invite:
    def __init__(self, iid, frm, to, game, t):
        self.id, self.frm, self.to, self.game, self.t = iid, frm, to, game, t
        self.st = 'wait'               # wait, ok, no, busy, gone, timeout, back (taken back)
        self.done = 0.0                # when it stopped waiting
        self.sid = 0
        self.n = 0                     # the how-manyth challenge (the newest counts)


class Session:
    def __init__(self, sid, a, b, game, first, lead, t, rb=False):
        self.sid, self.game, self.lead, self.rb = sid, game, lead, rb
        self.p = [a, b]
        self.first = first             # index of the player who begins
        self.q = [[], []]              # messages for player i: (seq, hex)
        self.next_in = [1, 1]          # the next number expected from player i
        self.next_out = [1, 1]         # the number the next message for player i gets
        self.seen = [t, t]
        self.end = ['', '']            # why it is over for player i: left, gone, lost
        self.left = [False, False]
        self.over_t = 0.0

    def idx(self, cid):
        return self.p.index(cid) if cid in self.p else -1


class Relay:
    def __init__(self, store, clock=None):
        self.store = store
        self.clock = clock or time.monotonic
        self.pres = {}                 # console id -> Presence
        self.invites = {}              # invite id -> Invite
        self.sessions = {}             # sid -> Session
        self.of = {}                   # console id -> sid it plays in
        self.count = 0                 # challenges so far
        self.msg_ok = lambda cid, n: True   # rate limit for new messages (set by the app)

    # ---------------- helpers ----------------
    def now(self):
        return self.clock()

    def presence(self, cid):
        p = self.pres.get(cid)
        if not p:
            p = self.pres[cid] = Presence(self.now())
        return p

    def online(self, cid):
        p = self.pres.get(cid)
        return bool(p and self.now() - p.seen < ONLINE_S)

    def notify(self, cid):
        p = self.pres.get(cid)
        if p:
            p.ver += 1
            p.ev.set()

    def name_of(self, cid):
        p = self.pres.get(cid)
        if p and p.name:
            return p.name
        c = self.store.console(cid) or {}
        return clean_name(c.get('name', '')) or c.get('code', '?')

    def session_of(self, cid):
        s = self.sessions.get(self.of.get(cid, 0))
        if s and s.idx(cid) >= 0 and not s.left[s.idx(cid)]:
            return s
        return None

    def busy(self, cid):
        s = self.session_of(cid)
        return bool(s and not s.end[s.idx(cid)])

    # ---------------- cleanup ----------------
    def sweep(self):
        t = self.now()
        for iid, iv in list(self.invites.items()):
            if iv.st == 'wait':
                if t - iv.t > INVITE_S:
                    self.finish_invite(iv, 'timeout')
                elif not self.online(iv.frm) or t - self.pres[iv.frm].seen > GONE_S:
                    self.finish_invite(iv, 'gone')
            elif t - iv.done > INVITE_KEEP_S:
                del self.invites[iid]
        for sid, s in list(self.sessions.items()):
            for i in (0, 1):
                if not s.end[i] and not s.left[i] and t - s.seen[i] > GONE_S:
                    s.end[i] = 'lost'                      # this one is away too long ...
                    o = 1 - i
                    if not s.end[o]:
                        s.end[o] = 'gone'                  # ... for the other one it is gone
                        self.notify(s.p[o])
                    s.over_t = s.over_t or t
            if all(s.left) or (s.over_t and t - s.over_t > LEFT_KEEP_S) or \
                    all(s.end[i] == 'lost' or s.left[i] for i in (0, 1)):
                self.drop_session(s)
        for cid in [c for c, p in self.pres.items() if t - p.seen > RECENT_S]:
            if cid not in self.of:
                del self.pres[cid]

    def drop_session(self, s):
        self.sessions.pop(s.sid, None)
        for c in s.p:
            if self.of.get(c) == s.sid:
                del self.of[c]

    def finish_invite(self, iv, st):
        iv.st, iv.done = st, self.now()
        self.notify(iv.frm)
        self.notify(iv.to)

    # ---------------- poll ----------------
    def take_messages(self, cid, d):
        """Checks a poll's session part; returns (session, index, new messages) or raises ValueError."""
        try:
            sid = int(d.get('sid', 0) or 0)
            got = int(d.get('got', 0) or 0)
        except (TypeError, ValueError):
            raise ValueError('sid/got')
        msgs = d.get('m') or []
        if not isinstance(msgs, list) or len(msgs) > MSG_MAX:
            raise ValueError('m')
        clean = []
        for m in msgs:
            if not (isinstance(m, list) and len(m) == 2 and isinstance(m[0], int) and isinstance(m[1], str)
                    and HEX_RE.match(m[1])):
                raise ValueError('message')
            clean.append((m[0], m[1].lower()))
        s = self.sessions.get(sid) if sid else None
        i = s.idx(cid) if s else -1
        if i < 0 or s.left[i]:
            return None, -1, []
        q = s.q[i]                                     # what the console has now: no longer kept
        while q and q[0][0] <= got:
            q.pop(0)
        new = [m for m in clean if m[0] >= s.next_in[i]]
        new.sort()
        return s, i, new

    def apply_messages(self, s, i, new):
        """Appends the messages in order; a gap ends it (the console repeats from there)."""
        o = 1 - i
        added = 0
        for seq, hx in new:
            if seq != s.next_in[i]:
                break
            s.next_in[i] += 1
            if s.end[o] or s.left[o]:
                continue                               # nobody listens any more: confirmed, dropped
            s.q[o].append((s.next_out[o], hx))
            s.next_out[o] += 1
            added += 1
        if added:
            self.notify(s.p[o])

    def ready(self, cid, got, sid):
        """Something for this console it has not seen yet?"""
        p = self.pres[cid]
        if p.ver != p.sent_ver:
            return True
        s = self.sessions.get(sid) if sid else None
        i = s.idx(cid) if s else -1
        return i >= 0 and bool(s.q[i]) and s.q[i][-1][0] > got

    def answer(self, cid, d, held_ms):
        p = self.pres[cid]
        p.sent_ver = p.ver
        out = {'me': self.store.friend_code(cid), 'h': held_ms}
        try:
            got = int(d.get('got', 0) or 0)
            sid = int(d.get('sid', 0) or 0)
        except (TypeError, ValueError):
            got = sid = 0
        # a challenge for us: the oldest one still waiting
        inv = [iv for iv in self.invites.values() if iv.to == cid and iv.st == 'wait']
        if inv:
            iv = min(inv, key=lambda x: x.n)
            out['inv'] = {'iid': iv.id, 'ic': self.store.friend_code(iv.frm), 'in': self.name_of(iv.frm),
                          'ig': iv.game, 'is': max(0, int(INVITE_S - (self.now() - iv.t)))}
        # our own challenge
        mine = [iv for iv in self.invites.values() if iv.frm == cid]
        if mine:
            iv = max(mine, key=lambda x: x.n)
            out['out'] = {'oid': iv.id, 'st': iv.st}
        # the session
        s = self.session_of(cid)
        if s:
            i = s.idx(cid)
            o = s.p[1 - i]
            out['ses'] = {'sid': s.sid, 'g': s.game, 'first': 1 if s.first == i else 0, 'lead': s.lead,
                          'oc': self.store.friend_code(o), 'on': self.name_of(o)}
            if s.rb:
                out['ses']['rb'] = 1
            if s.sid == sid:
                out['ack'] = s.next_in[i] - 1
                msgs = [m for m in s.q[i] if m[0] > got][:MSG_MAX]
                if msgs:
                    out['msg'] = ','.join('%d.%s' % m for m in msgs)
            if s.end[i] and not (s.q[i] and s.q[i][-1][0] > got):
                out['ses']['se'] = s.end[i]            # over - after the last messages
        try:
            lists = int(d.get('l', 0) or 0)
        except (TypeError, ValueError):
            lists = 0
        if lists:
            out['lob'], out['fr'], rc = self.lists(cid)
            if lists >= 2:                             # newer consoles: the ones seen lately too
                out['rc'] = rc
        return out

    def lists(self, cid):
        """Everybody online (free ones first, friends first among them), the friends,
        and the consoles that went offline in the last RECENT_S."""
        friends = self.store.friends(cid)
        t = self.now()
        lob, rc = [], []
        for c, p in sorted(self.pres.items(), key=lambda kv: (self.busy(kv[0]), kv[0] not in friends,
                                                              self.name_of(kv[0]))):
            if c == cid or not self.online(c) or not self.store.console(c):
                continue
            lob.append({'c': self.store.friend_code(c), 'n': self.name_of(c), 'g': p.game,
                        'b': 1 if self.busy(c) else 0, 'f': 1 if c in friends else 0})
            if len(lob) >= LOBBY_MAX:
                break
        for c, p in sorted(self.pres.items(), key=lambda kv: -kv[1].seen):
            if c == cid or self.online(c) or t - p.seen > RECENT_S or not self.store.console(c):
                continue
            rc.append({'c': self.store.friend_code(c), 'n': self.name_of(c), 'f': 1 if c in friends else 0,
                       'm': int((t - p.seen) // 60)})
            if len(rc) >= RECENT_MAX:
                break
        fr = [{'c': self.store.friend_code(c), 'n': self.name_of(c), 'o': 1 if self.online(c) else 0,
               'b': 1 if self.busy(c) else 0} for c in friends if self.store.console(c)]
        return lob, fr, rc

    async def poll(self, cid, d):
        """One poll: report, messages in, then what is new (now or after waiting)."""
        t = self.now()
        p = self.presence(cid)
        p.seen = t
        if 'n' in d:
            p.name = clean_name(d.get('n', '')) or p.name
        try:
            p.game = max(0, min(255, int(d.get('g', p.game) or 0)))
            if d.get('rtt') is not None:
                p.rtt = max(5, min(5000, int(d.get('rtt'))))
            if 'rb' in d:
                p.rb = bool(int(d.get('rb') or 0))
            wait = max(0, min(WAIT_MAX_S * 1000, int(d.get('wait', 0) or 0))) / 1000
        except (TypeError, ValueError):
            raise ValueError('number')
        s, i, new = self.take_messages(cid, d)
        if s:
            s.seen[i] = t
            if len(s.q[1 - i]) + len(new) > QUEUE_MAX:
                raise OverflowError('the other console does not take its messages')
            if new and not self.msg_ok(cid, len(new)):
                raise OverflowError('slow down')
            self.apply_messages(s, i, new)
        try:
            got = int(d.get('got', 0) or 0)
            sid = int(d.get('sid', 0) or 0)
        except (TypeError, ValueError):
            got = sid = 0
        held = 0
        if wait and not self.ready(cid, got, sid):
            p.ev.clear()
            t0 = time.monotonic()
            try:
                await asyncio.wait_for(p.ev.wait(), wait)
            except asyncio.TimeoutError:
                pass
            held = int((time.monotonic() - t0) * 1000)
            self.presence(cid).seen = self.now()
            s2 = self.session_of(cid)
            if s2:
                s2.seen[s2.idx(cid)] = self.now()
        return self.answer(cid, d, held)

    # ---------------- challenges ----------------
    def invite(self, cid, to_code, game):
        """Returns the invite or raises LookupError (unknown code) / ValueError(reason)."""
        to = self.store.by_friend_code(to_code)
        if not to:
            raise LookupError('no console with this code')
        if to == cid:
            raise ValueError('self')
        if not (1 <= game <= 255):
            raise ValueError('game')
        if not self.online(to):
            raise ValueError('offline')
        if self.busy(to):
            raise ValueError('busy')
        if self.busy(cid):
            raise ValueError('playing')
        for iv in self.invites.values():               # only one challenge of ours at a time
            if iv.frm == cid and iv.st == 'wait':
                self.finish_invite(iv, 'back')
        self.presence(cid).seen = self.now()
        iv = Invite(secrets.token_hex(4), cid, to, game, self.now())
        self.count += 1
        iv.n = self.count
        self.invites[iv.id] = iv
        self.notify(to)
        return iv

    def cancel(self, cid, iid):
        iv = self.invites.get(iid)
        if not iv or iv.frm != cid:
            raise LookupError('no such challenge')
        if iv.st == 'wait':
            self.finish_invite(iv, 'back')

    def reply(self, cid, iid, yes):
        """The answer to a challenge; returns the session for a yes."""
        iv = self.invites.get(iid)
        if not iv or iv.to != cid or iv.st != 'wait':
            raise LookupError('no such challenge')
        if not yes:
            self.finish_invite(iv, 'no')
            return None
        if self.busy(iv.frm) or not self.online(iv.frm):
            self.finish_invite(iv, 'gone')
            raise LookupError('the other console is gone')
        if self.busy(cid):
            self.finish_invite(iv, 'busy')
            raise ValueError('playing')
        for c in (cid, iv.frm):                        # an ended game of theirs is put away
            old = self.session_of(c)
            if old:
                old.left[old.idx(c)] = True
                if all(old.left):
                    self.drop_session(old)
        sid = 0
        while not sid or sid in self.sessions:
            sid = secrets.randbelow(65535) + 1
        pa, pb = self.presence(iv.frm), self.presence(cid)
        rb = pa.rb and pb.rb
        lead = (lead_rb if rb else lead_for)(pa.rtt, pb.rtt)
        s = Session(sid, iv.frm, cid, iv.game, secrets.randbelow(2), lead, self.now(), rb)
        self.sessions[sid] = s
        self.of[iv.frm] = self.of[cid] = sid
        iv.st, iv.done, iv.sid = 'ok', self.now(), sid
        # other challenges for either of them are over
        for o in self.invites.values():
            if o is not iv and o.st == 'wait' and (o.to in (cid, iv.frm) or o.frm in (cid, iv.frm)):
                self.finish_invite(o, 'busy' if o.to in (cid, iv.frm) else 'back')
        self.notify(iv.frm)
        self.notify(cid)
        return s

    def leave(self, cid, sid):
        s = self.sessions.get(sid)
        i = s.idx(cid) if s else -1
        if i < 0:
            return
        s.left[i] = True
        s.q[i].clear()
        o = 1 - i
        if not s.end[o]:
            s.end[o] = 'left'
        s.end[i] = s.end[i] or 'left'
        s.over_t = s.over_t or self.now()
        if self.of.get(cid) == sid:
            del self.of[cid]
        self.notify(s.p[o])
        if all(s.left):
            self.drop_session(s)
