// Online races (docs/RACES.md): rooms of 2 to 8 players on one seed, kept live in Redis, results kept in data/races.json.
// Championship only for now; Draft needs its start points first.
//   POST /api/race/create {install, player, country, version, format, size, public}  -> {token, id, code}
//   POST /api/race/join   {install, player, country, version, code}                   -> {token, id, code}
//   GET  /api/race/open                                         -> {rooms: [{code, format, size, players, creator}]}
//   POST /api/race/close  {token}                 creator: 2 or more players go on to skins and the ready room
//   POST /api/race/kick   {token, pid}            creator: removes a player before the start
//   POST /api/race/pick   {token, link, samus}    the two skins
//   POST /api/race/ready  {token}                 the last one sets startAt
//   POST /api/race/poll   {token, since, progress, events, chat}   -> {now, room, log, you}   heartbeat and messages
//   POST /api/race/finish {token, proof}          the time is the server's arrival time minus startAt
//   POST /api/race/leave  {token}
//   POST /api/race/ladder {token}                 the winner keeps their time
//   GET  /api/race/ladder?format=full|sprint|half
// Room states: open (players join), closed (skins, ready room; the seed is out), running (from startAt), finished, cancelled.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const FORMATS = {   // the Championship lengths: the run's rules, and the countdown after the first finish in a multi race
  full: { rules: {}, countdownMin: 60 },
  sprint: { rules: { ganonCrystals: 2, ganonsTowerCrystals: 2, tourianBosses: 1 }, countdownMin: 15 },
  half: { rules: { ganonCrystals: 4, ganonsTowerCrystals: 4, tourianBosses: 2 }, countdownMin: 30 },
};
const ROOM_MS = 3600 * 1000;        // a room that never gets going
const LIVE_S = 6 * 3600;            // a running race's key, renewed on every change and poll
const DONE_S = 3600;                // a finished race stays readable for its results
const RETURN_MS = 5 * 60 * 1000;    // a dropped player can come back for this long
const OUTAGE_MS = 5 * 60 * 1000;    // a server back after longer cancels the races it was running
const LEAD_MS = 20 * 1000;          // the last confirm to the start: every device loads its game and holds the first frame
const MIN_MS = 3 * 60 * 1000;
const LOG_KEEP = 200;
const CHAT_LEN = 80, CHAT_PER_MIN = 12, EVENT_LEN = 40;
const CODE_CHARS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

module.exports = function race(ctx) {
  const { DIR, hmac, send, readJson, onlyKeys, limited, clean, verifyProof } = ctx;
  const clock = ctx.now || Date.now;
  const r = require('./redis')({ host: process.env.SMZ3_REDIS_HOST || '127.0.0.1', port: Number(process.env.SMZ3_REDIS_PORT || 6379), db: Number(process.env.SMZ3_REDIS_DB || 5) });
  const K = s => 'smz3:' + s;
  const FILE = path.join(DIR, 'races.json');
  const VERSION = process.env.SMZ3_APP_VERSION || '';   // when set, rooms only take this app version

  // results: finished races; ladder[format]: one best time per install
  let db = { results: [], ladder: { full: [], sprint: [], half: [] } };
  try { db = Object.assign(db, JSON.parse(fs.readFileSync(FILE, 'utf8'))); } catch (e) {}
  function save() {
    db.results = db.results.slice(-2000);
    fs.writeFileSync(FILE + '.tmp', JSON.stringify(db));
    fs.renameSync(FILE + '.tmp', FILE);
  }

  // One change at a time per room.
  const locks = new Map();
  function locked(id, fn) {
    const prev = locks.get(id) || Promise.resolve();
    const next = prev.then(fn, fn);
    locks.set(id, next.catch(() => {}));
    next.finally(() => { if (locks.get(id) === next) locks.delete(id); }).catch(() => {});
    return next;
  }

  async function load(id) { const s = await r.cmd('GET', K('race:' + id)); return s ? JSON.parse(s) : null; }
  async function store(room) {
    const live = room.state === 'running';
    const done = room.state === 'finished' || room.state === 'cancelled';
    const key = K('race:' + room.id), body = JSON.stringify(room);
    if (live) await r.cmd('SET', key, body, 'EX', LIVE_S);
    else if (done) await r.cmd('SET', key, body, 'EX', DONE_S);
    else await r.cmd('SET', key, body, 'PXAT', room.createdAt + ROOM_MS);
    if (room.state === 'open' && room.public) await r.cmd('ZADD', K('open'), room.createdAt, room.id);
    else await r.cmd('ZREM', K('open'), room.id);
    if (done) await r.cmd('SREM', K('active'), room.id); else await r.cmd('SADD', K('active'), room.id);
  }

  const tokenOf = (id, pid) => id + '.' + pid + '.' + hmac('race:' + id + '.' + pid);
  function readToken(t) {
    const p = String(t || '').split('.');
    if (p.length !== 3) return null;
    const want = Buffer.from(hmac('race:' + p[0] + '.' + p[1])), sig = Buffer.from(p[2]);
    return sig.length === want.length && crypto.timingSafeEqual(sig, want) ? { id: p[0], pid: p[1] } : null;
  }

  function note(room, kind, pid, text) {
    room.seq = (room.seq || 0) + 1;
    room.log.push({ seq: room.seq, at: clock(), kind, pid, text });
    if (room.log.length > LOG_KEEP) room.log.splice(0, room.log.length - LOG_KEEP);
  }
  const named = (room, pid) => (room.players.find(p => p.pid === pid) || {}).name || 'Player';
  const inRace = room => room.players.filter(p => !p.left);

  function newPlayer(b) {
    return { pid: crypto.randomBytes(4).toString('hex'), install: b.install, name: clean(b.player, 24) || 'Player',
      country: /^[A-Z]{2}$/.test(b.country) ? b.country : '', link: '', samus: '', ready: false, joinedAt: clock(),
      lastSeen: clock(), progress: 0, chat: [] };
  }

  // Ends the race when its rules say so; returns true when something changed.
  function settle(room) {
    if (room.state !== 'running') return false;
    const now = clock(), left = inRace(room), done = room.players.filter(p => p.timeMs != null);
    let changed = false;
    for (const p of left) if (p.timeMs == null && now - p.lastSeen > RETURN_MS) { p.left = true; note(room, 'system', p.pid, p.name + ' left the race'); changed = true; }
    const still = inRace(room).filter(p => p.timeMs == null);
    const end = (why) => { room.state = 'finished'; room.endedAt = now; room.endWhy = why; record(room); return true; };
    if (room.size === 2 && done.length) return end('finish');
    if (!inRace(room).length) { room.state = 'cancelled'; room.endedAt = now; room.endWhy = 'everyone left'; record(room); return true; }
    if (inRace(room).length === 1 && !done.length && room.players.length > 1) { inRace(room)[0].forfeitWin = true; return end('last one left'); }
    if (done.length && (!still.length || (room.endAt && now >= room.endAt))) return end(still.length ? 'countdown' : 'finish');
    return changed;
  }

  // Positions: finishers by time, then the rest.
  function standings(room) {
    const fin = room.players.filter(p => p.timeMs != null).sort((a, b) => a.timeMs - b.timeMs);
    return room.players.map(p => {
      const i = fin.indexOf(p);
      return { pid: p.pid, name: p.name, country: p.country, link: p.link, samus: p.samus, ready: p.ready, left: !!p.left,
        progress: p.progress, timeMs: p.timeMs, place: i >= 0 ? i + 1 : p.forfeitWin ? 1 : 0, held: !!p.held,
        away: room.state === 'running' && !p.left && p.timeMs == null && clock() - p.lastSeen > 15000 };
    });
  }

  function record(room) {
    db.results.push({ id: room.id, format: room.format, size: room.size, startAt: room.startAt, endedAt: room.endedAt, why: room.endWhy,
      players: standings(room).map(({ pid, ...p }) => p) });
    save();
  }

  function view(room, you) {
    const v = { code: room.code, state: room.state, format: room.format, size: room.size, public: room.public, creator: room.creator,
      startAt: room.startAt, endAt: room.endAt, players: standings(room), endWhy: room.endWhy };
    if (room.state !== 'open') { v.seed = room.seed; v.settings = room.settings; }
    return v;
  }

  async function newCode() {
    for (let i = 0; i < 20; i++) {
      let c = '';
      for (const b of crypto.randomBytes(5)) c += CODE_CHARS[b % CODE_CHARS.length];
      if ((await r.cmd('SET', K('code:' + c), 'x', 'NX', 'PX', ROOM_MS)) === 'OK') return c;
    }
    throw new Error('no code');
  }

  const fail = (res, code, error, more) => send(res, code, Object.assign({ error }, more));
  const okVersion = v => !VERSION || v === VERSION;
  const validInstall = i => /^[0-9a-f-]{16,40}$/.test(String(i));

  // A request on a room the token names: loads it, runs fn under the room's lock, stores it if fn says so.
  function onRoom(res, token, fn) {
    const t = readToken(token);
    if (!t) return fail(res, 400, 'invalid');
    locked(t.id, async () => {
      const room = await load(t.id);
      if (!room) return fail(res, 404, 'no such race');
      const me = room.players.find(p => p.pid === t.pid);
      if (!me) return fail(res, 403, 'not in this race');
      settle(room);
      const out = await fn(room, me);
      await store(room);
      if (out) send(res, 200, out);
    }).catch(e => { console.error('race', e.message); fail(res, 503, 'race server unavailable'); });
  }

  // A server that was down: back soon, every player's clock to return starts again; back late, its running races end.
  (async () => {
    try {
      const beat = Number(await r.cmd('GET', K('beat')) || 0), gap = clock() - beat;
      const ids = await r.cmd('SMEMBERS', K('active')) || [];
      for (const id of ids) await locked(id, async () => {
        const room = await load(id);
        if (!room) return r.cmd('SREM', K('active'), id);
        if (room.state === 'running' && beat && gap > OUTAGE_MS) { room.state = 'cancelled'; room.endedAt = clock(); room.endWhy = 'server down'; record(room); }
        else for (const p of room.players) p.lastSeen = clock();
        await store(room);
      });
    } catch (e) { console.error('race startup', e.message); }
  })();
  setInterval(() => { r.cmd('SET', K('beat'), String(clock())).catch(() => {}); }, 5000).unref();
  setInterval(async () => {   // races whose players all went quiet still end
    try {
      for (const id of await r.cmd('SMEMBERS', K('active')) || []) await locked(id, async () => {
        const room = await load(id);
        if (!room) return r.cmd('SREM', K('active'), id);
        if (settle(room)) await store(room);
      });
    } catch (e) {}
  }, 30000).unref();

  return function routes(req, res, url, ip) {
    const p = url.pathname;
    if (req.method === 'GET' && p === '/api/race/open') {
      r.cmd('ZRANGE', K('open'), '0', '49').then(async ids => {
        const rooms = [];
        for (const id of ids || []) {
          const room = await load(id);
          if (!room || room.state !== 'open') { await r.cmd('ZREM', K('open'), id); continue; }
          rooms.push({ code: room.code, format: room.format, size: room.size, players: room.players.length, creator: named(room, room.creator) });
        }
        send(res, 200, { rooms });
      }).catch(() => fail(res, 503, 'race server unavailable'));
      return true;
    }
    if (req.method === 'GET' && p === '/api/race/ladder') {
      const f = FORMATS[url.searchParams.get('format')] ? url.searchParams.get('format') : 'full';
      send(res, 200, { format: f, runs: db.ladder[f].filter(e => !e.hold).sort((a, b) => a.timeMs - b.timeMs).slice(0, 100).map(({ install, hold, ...e }) => e) });
      return true;
    }
    if (req.method !== 'POST' || !p.startsWith('/api/race/')) return false;
    if (limited('r', ip, 3600)) { fail(res, 429, 'slow down'); return true; }
    const what = p.slice('/api/race/'.length);
    readJson(req, res, b => {
      if (what === 'create') {
        if (!onlyKeys(b, ['install', 'player', 'country', 'version', 'format', 'size', 'public']) || !validInstall(b.install) || !FORMATS[b.format]) return fail(res, 400, 'invalid');
        if (!okVersion(b.version)) return fail(res, 409, 'version', { need: VERSION });
        if (limited('rc', ip, 30)) return fail(res, 429, 'slow down');
        const size = b.size === 2 ? 2 : 8;
        newCode().then(code => {
          const id = crypto.randomBytes(8).toString('hex'), me = newPlayer(b);
          const room = { id, code, public: !!b.public, mode: 'championship', format: b.format, size, version: String(b.version || ''), creator: me.pid,
            state: 'open', createdAt: clock(), players: [me], seq: 0, log: [] };
          return r.cmd('SET', K('code:' + code), id, 'PX', ROOM_MS).then(() => store(room)).then(() => send(res, 200, { token: tokenOf(id, me.pid), id, code }));
        }).catch(() => fail(res, 503, 'race server unavailable'));
        return;
      }
      if (what === 'join') {
        if (!onlyKeys(b, ['install', 'player', 'country', 'version', 'code']) || !validInstall(b.install)) return fail(res, 400, 'invalid');
        const code = String(b.code || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
        r.cmd('GET', K('code:' + code)).then(id => {
          if (!id || id === 'x') return fail(res, 404, 'no such room');
          return locked(id, async () => {
            const room = await load(id);
            if (!room) return fail(res, 404, 'no such room');
            if (room.version !== String(b.version || '') || !okVersion(b.version)) return fail(res, 409, 'version', { need: room.version });
            const again = room.players.find(q => q.install === b.install && !q.left);
            if (again) return send(res, 200, { token: tokenOf(id, again.pid), id, code: room.code });   // the same device back in
            if (room.state !== 'open') return fail(res, 409, 'closed');
            if (room.players.length >= room.size) return fail(res, 409, 'full');
            const me = newPlayer(b);
            room.players.push(me);
            note(room, 'system', me.pid, me.name + ' joined');
            await store(room);
            send(res, 200, { token: tokenOf(id, me.pid), id, code: room.code });
          });
        }).catch(() => fail(res, 503, 'race server unavailable'));
        return;
      }
      if (!onlyKeys(b, ['token', 'since', 'progress', 'events', 'chat', 'pid', 'link', 'samus', 'proof'])) return fail(res, 400, 'invalid');
      onRoom(res, b.token, async (room, me) => {
        const now = clock();
        me.lastSeen = now;
        const creator = me.pid === room.creator;
        switch (what) {
        case 'close':
          if (!creator || room.state !== 'open') return fail(res, 409, 'not now');
          if (room.players.length < 2) return fail(res, 409, 'needs 2 players');
          room.state = 'closed';
          room.seed = String(crypto.randomInt(100000000, 1000000000));
          room.settings = JSON.stringify(Object.assign({ seed: room.seed, challenge: true }, FORMATS[room.format].rules));
          note(room, 'system', me.pid, 'Room closed: pick your skins');
          return { room: view(room) };
        case 'kick': {
          const who = room.players.find(q => q.pid === b.pid);
          if (!creator || !who || who === me || (room.state !== 'open' && room.state !== 'closed')) return fail(res, 409, 'not now');
          room.players = room.players.filter(q => q !== who);
          note(room, 'system', who.pid, who.name + ' was removed');
          if (room.state === 'closed' && room.players.length < 2) { room.state = 'open'; for (const q of room.players) q.ready = false; }   // back to waiting for players
          return { room: view(room) };
        }
        case 'pick':
          if (room.state !== 'closed') return fail(res, 409, 'not now');
          me.link = clean(b.link, 31); me.samus = clean(b.samus, 31);
          return { room: view(room) };
        case 'ready':
          if (room.state !== 'closed') return fail(res, 409, 'not now');
          me.ready = true;
          note(room, 'system', me.pid, me.name + ' is ready');
          if (room.players.every(q => q.ready)) { room.state = 'running'; room.startAt = now + LEAD_MS; note(room, 'system', null, 'Everyone is ready'); }
          return { room: view(room) };
        case 'leave':
          if (me.left) return { room: view(room) };
          if (room.state === 'open' || room.state === 'closed') {
            room.players = room.players.filter(q => q !== me);
            if (!room.players.length) { room.state = 'cancelled'; room.endedAt = now; room.endWhy = 'empty'; }
            else if (creator) room.creator = room.players[0].pid;   // the one who has been in longest
            if (room.state === 'closed' && room.players.length < 2) { room.state = 'open'; for (const q of room.players) q.ready = false; }
          } else if (room.state === 'running') { me.left = true; note(room, 'system', me.pid, me.name + ' left the race'); settle(room); }
          return { room: view(room) };
        case 'poll': {
          if (Number.isInteger(b.progress) && room.state === 'running' && me.timeMs == null) me.progress = Math.max(0, Math.min(100, b.progress));
          if (Array.isArray(b.events) && room.state === 'running' && now >= room.startAt)
            for (const e of b.events.slice(0, 8)) if (typeof e === 'string' && e.trim()) note(room, 'event', me.pid, e.trim().slice(0, EVENT_LEN));
          if (Array.isArray(b.chat)) {
            me.chat = (me.chat || []).filter(t => now - t < 60000);
            for (const c of b.chat.slice(0, 3)) {
              if (typeof c !== 'string' || !c.trim() || me.chat.length >= CHAT_PER_MIN) continue;
              me.chat.push(now);
              note(room, 'chat', me.pid, c.trim().slice(0, CHAT_LEN));
            }
          }
          settle(room);
          const since = Number.isInteger(b.since) ? b.since : 0;
          return { now, room: view(room), you: me.pid, log: room.log.filter(e => e.seq > since).map(e => Object.assign({ name: named(room, e.pid) }, e)) };
        }
        case 'finish': {
          if (room.state !== 'running' || me.left || me.timeMs != null) return fail(res, 409, 'not now');
          if (typeof b.proof !== 'string' || b.proof.length > 12000) return fail(res, 400, 'invalid');
          const timeMs = now - room.startAt;
          if (timeMs < MIN_MS) return fail(res, 400, 'invalid time');
          const verdict = await new Promise(done => verifyProof('race', room.settings, { proof: b.proof }, timeMs, (err, v) => done(err ? { err } : v)));
          if (verdict.err) return fail(res, 400, verdict.err);   // a proof that can't have happened: the finish doesn't count
          me.timeMs = timeMs; me.progress = 100;
          if (verdict.suspect) me.held = verdict.reason;   // counts for the race; its ladder entry waits for a review
          note(room, 'finish', me.pid, me.name + ' finished');
          if (room.size > 2 && !room.endAt) room.endAt = now + FORMATS[room.format].countdownMin * 60000;
          settle(room);
          return { room: view(room), timeMs };
        }
        case 'ladder': {
          const mine = standings(room).find(q => q.pid === me.pid);
          if (room.state !== 'finished' || !mine || mine.place !== 1 || me.timeMs == null) return fail(res, 409, 'only the winner who finished');
          const table = db.ladder[room.format], i = table.findIndex(e => e.install === me.install);
          const entry = { player: me.name, country: me.country, timeMs: me.timeMs, at: now, install: me.install, hold: me.held || undefined };
          if (i < 0) table.push(entry); else if (table[i].timeMs > me.timeMs) table[i] = entry;
          save();
          return { ok: true, held: !!me.held };
        }
        default: return fail(res, 404, 'not found');
        }
      });
    });
    return true;
  };
};
