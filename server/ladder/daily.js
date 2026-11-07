// Daily timed challenge (docs/DAILY.md): three 4 hour windows a day in UTC, one challenge per window made by smz3daily,
// one attempt per install, hints released on the player's own clock, tokens for finishing, rank and streaks.
//   GET  /api/daily?install=          -> {now, current?, next, tokens}   (no seed or hints before the start)
//   POST /api/daily/start  {install, window}            -> {now, token, seed, settings, goal, startedAt, closesAt, hints, nextHintAt}
//   GET  /api/daily/hints?token=                         -> {now, hints, nextHintAt, closesAt, players}
// players = {now, started, done}: attempts heard from in the last 3 minutes and not finished, attempts started, finishes.
// Devices ask for the hints about once a minute while playing, which is what "now" counts (kept in memory only).
//   POST /api/daily/finish {token, timeMs, player, country, proof} -> {ok, late, rank, tokens}
//   GET  /api/daily/chat?token=&since=                   -> {now, log, players}   the window's chat and finishes, newer than since
//   POST /api/daily/chat   {token, player, country, text} -> {ok}
// log = [{seq, at, kind: 'chat' | 'finish', pid, name, country, text}]: the last lines of the window, kept in memory only.
//   GET  /api/daily/table?window=                        -> {window, runs, late, started, finished, medianMs}
//   GET  /api/tokens?install=                            -> {balance, streak, history}
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFile } = require('child_process');

const HOURS = [6, 14, 22];
const LENGTH = 4 * 3600 * 1000;
const MIN_MS = 3 * 60 * 1000;
const PRIZE = { finish: 10, rank: [15, 10, 5], streak: { 3: 5, 7: 15, 30: 50 } };

module.exports = function daily(ctx) {
  const { DIR, hmac, send, readJson, onlyKeys, limited, clean, verifyProof } = ctx;
  const clock = ctx.now || Date.now;   // tests move it
  const BIN = process.env.SMZ3DAILY || path.join(__dirname, 'smz3daily');
  const FILE = path.join(DIR, 'daily.json');
  const CDIR = path.join(DIR, 'daily');
  fs.mkdirSync(CDIR, { recursive: true });

  // starts[window][install] = {id, startedAt}; results[window] = [{install, player, country, timeMs, finishedAt, late, hold}]
  // settled[window] = true once its rank prizes are paid; tokens[install] = {balance, history: [{at, window, amount, why}]}
  let db = { starts: {}, results: {}, settled: {}, tokens: {} };
  try { db = Object.assign(db, JSON.parse(fs.readFileSync(FILE, 'utf8'))); } catch (e) {}
  function save() {
    fs.writeFileSync(FILE + '.tmp', JSON.stringify(db));
    fs.renameSync(FILE + '.tmp', FILE);
  }

  const dayOf = t => new Date(t).toISOString().slice(0, 10);
  function windowAt(dayMs, i) {
    const opensAt = dayMs + HOURS[i] * 3600000;
    return { id: dayOf(dayMs) + '-' + i, opensAt, closesAt: opensAt + LENGTH };
  }
  function windowsAround(t) {
    const d = new Date(t), day = Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate()), out = [];
    for (const off of [-1, 0, 1]) for (let i = 0; i < HOURS.length; i++) out.push(windowAt(day + off * 86400000, i));
    return out;
  }
  function windowById(id) {
    const m = /^(\d{4}-\d{2}-\d{2})-([0-2])$/.exec(String(id || ''));
    if (!m) return null;
    const day = Date.parse(m[1] + 'T00:00:00Z');
    return Number.isNaN(day) ? null : windowAt(day, Number(m[2]));
  }
  const currentWindow = t => windowsAround(t).find(w => w.opensAt <= t && t < w.closesAt) || null;
  const nextWindow = t => windowsAround(t).find(w => w.opensAt > t);

  // The challenge of a window, generated once and kept in data/daily/<id>.json.
  const cache = new Map(), pending = new Map();
  function challenge(id, done) {
    if (cache.has(id)) return done(null, cache.get(id));
    const file = path.join(CDIR, id + '.json');
    try { const c = JSON.parse(fs.readFileSync(file, 'utf8')); cache.set(id, c); return done(null, c); } catch (e) {}
    if (pending.has(id)) return pending.get(id).push(done);
    pending.set(id, [done]);
    execFile(BIN, [id], { timeout: 60000, maxBuffer: 1 << 20 }, (err, out) => {
      let c = null;
      try { c = JSON.parse(out); } catch (e) {}
      if (c) { fs.writeFileSync(file, JSON.stringify(c)); cache.set(id, c); }
      for (const cb of pending.get(id)) cb(c ? null : 'challenge not available', c);
      pending.delete(id);
    });
  }
  const seen = new Map();   // window -> Map(install -> last time it asked)
  const CHAT_LEN = 80, CHAT_PER_MIN = 12, CHAT_KEEP = 60;
  const chats = new Map();  // window -> {seq, lines}; sent[install] = times of the last minute's messages
  const sent = new Map();
  const pidOf = install => hmac('chat.' + install).slice(0, 10);   // a stable id per player, for muting, that is not the install
  function say(w, kind, install, name, country, text) {
    let c = chats.get(w);
    if (!c) { chats.set(w, c = { seq: 0, lines: [] }); for (const k of chats.keys()) if (k < w && chats.size > 3) chats.delete(k); }
    c.lines.push({ seq: ++c.seq, at: clock(), kind, pid: pidOf(install), name, country, text });
    if (c.lines.length > CHAT_KEEP) c.lines.shift();
  }
  const hms = ms => { const s = Math.floor(ms / 1000); return Math.floor(s / 3600) + ':' + String(Math.floor(s / 60) % 60).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0'); };
  const LIVE_MS = 3 * 60 * 1000;
  function heard(w, install) {
    let m = seen.get(w);
    if (!m) { seen.set(w, m = new Map()); for (const k of seen.keys()) if (k < w && seen.size > 3) seen.delete(k); }
    m.set(install, clock());
  }
  function players(w) {
    const done = new Set((db.results[w] || []).map(r => r.install)), now = clock();
    let live = 0;
    for (const [inst, at] of seen.get(w) || []) if (now - at < LIVE_MS && !done.has(inst)) live++;
    return { now: live, started: Object.keys(db.starts[w] || {}).length, done: done.size };
  }
  const difficulty = c => c.estimate.searchMin < 45 ? 'Easy' : c.estimate.searchMin < 70 ? 'Normal' : 'Hard';
  const publicInfo = (w, c) => ({ id: w.id, opensAt: w.opensAt, closesAt: w.closesAt, goal: c.goal.text, start: c.start, difficulty: difficulty(c), estimateMin: c.estimate.searchMin });
  function released(c, startedAt, now) {
    const at = m => startedAt + m * 60000;
    const hints = c.hints.filter(h => at(h.atMin) <= now).map(h => ({ at: at(h.atMin), text: h.text }));
    const next = c.hints.find(h => at(h.atMin) > now);
    return { hints, nextHintAt: next ? at(next.atMin) : null };
  }

  // ---------- tokens ----------
  function grant(install, window, amount, why) {
    const t = db.tokens[install] || (db.tokens[install] = { balance: 0, history: [] });
    t.balance += amount;
    t.history.push({ at: clock(), window, amount, why });
    if (t.history.length > 200) t.history.splice(0, t.history.length - 200);
  }
  const finishedOk = r => !r.late && !r.hold;
  // Days in a row (UTC, by window date) with at least one window finished in time, ending on `day`.
  function streak(install, day) {
    const days = new Set();
    for (const [w, list] of Object.entries(db.results)) if (list.some(r => r.install === install && finishedOk(r))) days.add(w.slice(0, 10));
    let n = 0;
    for (let d = Date.parse(day + 'T00:00:00Z'); days.has(dayOf(d)); d -= 86400000) n++;
    return n;
  }
  const streakNow = (install, now) => streak(install, dayOf(now)) || streak(install, dayOf(now - 86400000));   // today not played yet keeps it
  function table(id) {
    return (db.results[id] || []).filter(finishedOk).sort((a, b) => a.timeMs - b.timeMs);
  }
  // Rank prizes are paid once a window has closed.
  function settle(now) {
    let changed = false;
    for (const id of Object.keys(db.results)) {
      const w = windowById(id);
      if (db.settled[id] || !w || now < w.closesAt) continue;
      table(id).slice(0, PRIZE.rank.length).forEach((r, i) => grant(r.install, id, PRIZE.rank[i], 'rank ' + (i + 1)));
      db.settled[id] = true;
      changed = true;
    }
    if (changed) save();
  }
  setInterval(() => {
    const now = clock();
    settle(now);
    for (const w of [currentWindow(now), nextWindow(now)]) if (w) challenge(w.id, () => {});   // made ahead of time
  }, 60000).unref();

  function tokenParts(token) {
    const parts = String(token || '').split('.');
    if (parts.length !== 6 || parts[2] !== 'daily') return null;
    const body = parts.slice(0, 5).join('.');
    const sig = Buffer.from(parts[5]), want = Buffer.from(hmac(body));
    if (sig.length !== want.length || !crypto.timingSafeEqual(sig, want)) return null;
    return { id: parts[0], install: parts[1], window: parts[3], startedAt: Number(parts[4]) };
  }
  const validInstall = s => /^[0-9a-f-]{16,40}$/.test(String(s || ''));

  // ---------- routes ----------
  function handle(req, res, url, ip) {
    const now = clock();
    settle(now);   /* cheap; the timer does it too */
    if (req.method === 'GET' && url.pathname === '/api/daily') {
      const install = url.searchParams.get('install'), cur = currentWindow(now), next = nextWindow(now);
      const state = w => {
        if (!validInstall(install)) return undefined;
        const s = (db.starts[w.id] || {})[install], r = (db.results[w.id] || []).find(x => x.install === install);
        if (r) return { state: r.late ? 'late' : 'done', timeMs: r.timeMs, startedAt: s && s.startedAt };
        return s ? { state: now >= w.closesAt ? 'failed' : 'started', startedAt: s.startedAt } : { state: 'open' };
      };
      const tokens = validInstall(install) ? { balance: (db.tokens[install] || {}).balance || 0, streak: streakNow(install, now) } : undefined;
      if (!cur) return send(res, 200, { now, current: null, next: { id: next.id, opensAt: next.opensAt, closesAt: next.closesAt }, tokens }), true;
      challenge(cur.id, (err, c) => {
        if (err) return send(res, 503, { error: err });
        send(res, 200, { now, current: Object.assign(publicInfo(cur, c), state(cur), { players: players(cur.id) }), next: { id: next.id, opensAt: next.opensAt, closesAt: next.closesAt }, tokens });
      });
      return true;
    }
    if (req.method === 'POST' && url.pathname === '/api/daily/start') {
      if (limited('ds', ip, 20)) return send(res, 429, { error: 'slow down' }), true;
      readJson(req, res, b => {
        if (!onlyKeys(b, ['install', 'window']) || !validInstall(b.install)) return send(res, 400, { error: 'invalid' });
        const w = currentWindow(clock());
        if (!w || w.id !== b.window) return send(res, 409, { error: 'window not open' });
        challenge(w.id, (err, c) => {
          if (err) return send(res, 503, { error: err });
          const starts = db.starts[w.id] || (db.starts[w.id] = {});
          if ((db.results[w.id] || []).some(r => r.install === b.install)) return send(res, 409, { error: 'already finished' });
          let s = starts[b.install];
          if (!s) { s = starts[b.install] = { id: crypto.randomBytes(8).toString('hex'), startedAt: clock() }; save(); }   // one attempt: a second start resumes it
          heard(w.id, b.install);
          const body = [s.id, b.install, 'daily', w.id, s.startedAt].join('.');
          send(res, 200, Object.assign({ now: clock(), token: body + '.' + hmac(body), window: w.id, seed: c.seed, settings: c.settings, start: c.start, goal: c.goal,
            startedAt: s.startedAt, closesAt: w.closesAt, players: players(w.id) }, released(c, s.startedAt, clock())));
        });
      });
      return true;
    }
    if (req.method === 'GET' && url.pathname === '/api/daily/hints') {
      if (limited('dh', ip, 240)) return send(res, 429, { error: 'slow down' }), true;
      const t = tokenParts(url.searchParams.get('token')), w = t && windowById(t.window);
      if (!w) return send(res, 400, { error: 'invalid' }), true;
      challenge(w.id, (err, c) => {
        if (err) return send(res, 503, { error: err });
        if (clock() < w.closesAt) heard(w.id, t.install);
        send(res, 200, Object.assign({ now: clock(), closesAt: w.closesAt, players: players(w.id) }, released(c, t.startedAt, clock())));
      });
      return true;
    }
    if (req.method === 'GET' && url.pathname === '/api/daily/chat') {
      if (limited('dc', ip, 600)) return send(res, 429, { error: 'slow down' }), true;
      const t = tokenParts(url.searchParams.get('token')), w = t && windowById(t.window);
      if (!w) return send(res, 400, { error: 'invalid' }), true;
      if (now < w.closesAt) heard(w.id, t.install);
      const since = Number(url.searchParams.get('since')) || 0;
      send(res, 200, { now, log: ((chats.get(w.id) || {}).lines || []).filter(l => l.seq > since), players: players(w.id) });
      return true;
    }
    if (req.method === 'POST' && url.pathname === '/api/daily/chat') {
      if (limited('dw', ip, 60)) return send(res, 429, { error: 'slow down' }), true;
      readJson(req, res, b => {
        if (!onlyKeys(b, ['token', 'player', 'country', 'text']) || typeof b.text !== 'string' || !b.text.replace(/[\u0000-\u001f\u007f]/g, '').trim()) return send(res, 400, { error: 'invalid' });
        const t = tokenParts(b.token), w = t && windowById(t.window);
        if (!w || clock() >= w.closesAt) return send(res, 409, { error: 'window closed' });
        const mine = (sent.get(t.install) || []).filter(at => clock() - at < 60000);
        if (mine.length >= CHAT_PER_MIN) return send(res, 429, { error: 'slow down' });
        mine.push(clock()); sent.set(t.install, mine);
        if (sent.size > 5000) sent.clear();
        say(w.id, 'chat', t.install, clean(b.player, 24) || 'Player', /^[A-Z]{2}$/.test(b.country) ? b.country : '', b.text.replace(/[\u0000-\u001f\u007f]/g, '').trim().slice(0, CHAT_LEN));
        send(res, 200, { ok: true });
      });
      return true;
    }
    if (req.method === 'POST' && url.pathname === '/api/daily/finish') {
      if (limited('df', ip, 20)) return send(res, 429, { error: 'slow down' }), true;
      readJson(req, res, b => {
        if (!onlyKeys(b, ['token', 'timeMs', 'player', 'country', 'proof']) || typeof b.proof !== 'string' || b.proof.length > 12000) return send(res, 400, { error: 'invalid' });
        const t = tokenParts(b.token), w = t && windowById(t.window);
        if (!w) return send(res, 400, { error: 'invalid' });
        const elapsed = clock() - t.startedAt;
        if (!Number.isInteger(b.timeMs) || b.timeMs < MIN_MS || b.timeMs > elapsed) return send(res, 400, { error: 'invalid time' });
        const mine = () => (db.results[w.id] || []).find(r => r.install === t.install);
        if (mine()) return send(res, 200, { ok: true, duplicate: true });
        verifyProof('daily', w.id, b, b.timeMs, (err, verdict) => {
          if (err) return send(res, 400, { error: err });
          if (mine()) return send(res, 200, { ok: true, duplicate: true });
          const late = t.startedAt + b.timeMs > w.closesAt;
          const entry = { install: t.install, player: clean(b.player, 24) || 'Player', country: /^[A-Z]{2}$/.test(b.country) ? b.country : '', timeMs: b.timeMs,
            finishedAt: clock(), late: late || undefined, hold: verdict.suspect ? verdict.reason : undefined };
          (db.results[w.id] || (db.results[w.id] = [])).push(entry);
          let tokens = 0;
          if (finishedOk(entry)) {
            grant(t.install, w.id, PRIZE.finish, 'finished'); tokens += PRIZE.finish;
            const day = w.id.slice(0, 10), sameDay = Object.keys(db.results).filter(id => id !== w.id && id.startsWith(day) && db.results[id].some(r => r.install === t.install && finishedOk(r)));
            const n = streak(t.install, day), bonus = PRIZE.streak[n] || (n > 30 && n % 30 === 0 ? PRIZE.streak[30] : 0);
            if (!sameDay.length && bonus) { grant(t.install, w.id, bonus, 'streak ' + n); tokens += bonus; }
          }
          save();
          const rank = finishedOk(entry) ? table(w.id).indexOf(entry) + 1 : 0;
          if (rank) say(w.id, 'finish', t.install, entry.player, entry.country, 'finished in ' + hms(entry.timeMs) + ', place ' + rank);
          send(res, 200, { ok: true, late: late || undefined, held: verdict.suspect || undefined, rank, tokens, balance: (db.tokens[t.install] || {}).balance || 0 });
        });
      });
      return true;
    }
    if (req.method === 'GET' && url.pathname === '/api/daily/table') {
      const w = windowById(url.searchParams.get('window')) || currentWindow(now) || windowsAround(now).filter(x => x.closesAt <= now).pop();
      const list = db.results[w.id] || [], ok = table(w.id), times = ok.map(r => r.timeMs);
      const pub = ({ install, hold, ...r }) => r;
      send(res, 200, { window: w.id, opensAt: w.opensAt, closesAt: w.closesAt, runs: ok.slice(0, 100).map(pub), late: list.filter(r => r.late && !r.hold).map(pub),
        started: Object.keys(db.starts[w.id] || {}).length, finished: ok.length, medianMs: times.length ? times[times.length >> 1] : null });
      return true;
    }
    if (req.method === 'GET' && url.pathname === '/api/tokens') {
      const install = url.searchParams.get('install');
      if (!validInstall(install)) return send(res, 400, { error: 'invalid' }), true;
      const t = db.tokens[install] || { balance: 0, history: [] };
      send(res, 200, { balance: t.balance, streak: streakNow(install, now), history: t.history.slice(-50).reverse() });
      return true;
    }
    return false;
  }
  // For goals.js: gems from elsewhere go through the same balance, and the windows finished in time per install in a month.
  handle.grant = (install, window, amount, why) => { grant(install, window, amount, why); save(); };
  handle.finishesIn = month => {
    const n = {};
    for (const [id, list] of Object.entries(db.results)) if (id.startsWith(month)) for (const r of list) if (finishedOk(r)) n[r.install] = (n[r.install] || 0) + 1;
    return n;
  };
  handle.nameOf = install => {   // the latest name and country an install finished a daily challenge with
    let best = null;
    for (const list of Object.values(db.results)) for (const r of list) if (r.install === install && (!best || r.finishedAt > best.finishedAt)) best = r;
    return best ? { player: best.player, country: best.country } : null;
  };
  handle.validInstall = validInstall;
  // The settings smz3check rebuilds a window's seed from.
  handle.settingsOf = id => {
    if (!cache.has(id)) try { cache.set(id, JSON.parse(fs.readFileSync(path.join(CDIR, id + '.json'), 'utf8'))); } catch (e) { return '{"nologic":1}'; }
    return cache.get(id).settings;
  };
  return handle;
};

module.exports.HOURS = HOURS;
