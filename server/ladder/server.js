// SMZ3 online ladder: a tiny HTTP service with no dependencies.
//   POST /api/start   {install, kind}              -> {token, seed?}   (kind: original | monthly | weekly)
//   POST /api/finish  {token, timeMs, player, country, proof, seed?} -> {ok, rank}   (proof: "loc ms" lines, see below)
//   GET  /api/ladder  ?kind=original | ?kind=monthly&month=YYYY-MM | ?kind=weekly&week=YYYY-Www
//   GET  /api/monthly                              -> {month, seed, months:[...]}
//   GET  /api/weekly                               -> {week, seed, weeks:[...], endsAt}
//   /api/daily..., /api/tokens: the daily timed challenge, see daily.js
//   /api/goals...: the world goals of the month, see goals.js
//   /api/race/...: online races, see race.js
// A finished run is only accepted with a token the server signed when the run began, and its time
// cannot be longer than the wall clock time since then or shorter than MIN_MS. It must also carry a proof of run, the
// checks it cleared with their play times; smz3check replays them against the known seed. A proof that cannot have
// happened is refused, one that leans on far too many checks out of logic is held (hold) until admin.js approves it.
const http = require('http');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const PORT = Number(process.env.PORT || 3360);
const DIR = process.env.LADDER_DIR || path.join(__dirname, 'data');
const DATA = path.join(DIR, 'ladder.json');
const SECRET_FILE = path.join(DIR, 'secret');
const MIN_MS = 10 * 60 * 1000;
const MAX_MS = 3 * 24 * 3600 * 1000;      // a token is good for this long after it was issued
const MAX_BODY = 16384;
const { execFile } = require('child_process');
const CHECKER = process.env.SMZ3CHECK || path.join(__dirname, 'smz3check');
const START_PER_HOUR = 20;
const FINISH_PER_HOUR = 20;

fs.mkdirSync(DIR, { recursive: true });
if (!fs.existsSync(SECRET_FILE)) fs.writeFileSync(SECRET_FILE, crypto.randomBytes(32).toString('hex'), { mode: 0o600 });
const SECRET = fs.readFileSync(SECRET_FILE, 'utf8').trim();

// runs: {kind, month?, player, timeMs, finishedAt, install}; used: finished token ids
let db = { runs: [], used: [] };
try { db = JSON.parse(fs.readFileSync(DATA, 'utf8')); } catch (e) {}
const used = new Set(db.used);
function save() {
  db.used = [...used].slice(-5000);
  fs.writeFileSync(DATA + '.tmp', JSON.stringify(db));
  fs.renameSync(DATA + '.tmp', DATA);
}

const hmac = s => crypto.createHmac('sha256', SECRET).update(s).digest('hex');
const monthOf = t => new Date(t).toISOString().slice(0, 7);
// ISO week of a time, e.g. 2026-W44 (weeks start on Monday, UTC)
function weekOf(t) {
  const d = new Date(t); d.setUTCHours(0, 0, 0, 0);
  d.setUTCDate(d.getUTCDate() + 4 - (d.getUTCDay() || 7));
  const week = Math.ceil(((d - Date.UTC(d.getUTCFullYear(), 0, 1)) / 86400000 + 1) / 7);
  return d.getUTCFullYear() + '-W' + String(week).padStart(2, '0');
}
function weekEnd(t) {
  const d = new Date(t); d.setUTCHours(0, 0, 0, 0);
  return d.setUTCDate(d.getUTCDate() + 8 - (d.getUTCDay() || 7));
}
const periodNow = (kind, t) => kind === 'weekly' ? weekOf(t) : monthOf(t);
const seedOf = month => String(parseInt(hmac('seed:' + month).slice(0, 8), 16) % 900000000 + 100000000);
const clean = (v, n) => String(v == null ? '' : v).replace(/[^\p{L}\p{N} _.'!-]/gu, '').trim().slice(0, n);

function onlyKeys(b, keys) {
  return b && typeof b === 'object' && !Array.isArray(b) && Object.keys(b).every(k => keys.includes(k));
}

const hits = new Map();
function limited(bucket, ip, max) {
  const now = Date.now(), key = bucket + ip;
  const list = (hits.get(key) || []).filter(t => now - t < 3600000);
  list.push(now);
  hits.set(key, list);
  return list.length > max;
}

setInterval(() => {
  const now = Date.now();
  for (const [k, list] of hits) if (!list.some(t => now - t < 3600000)) hits.delete(k);
}, 600000).unref();

const periodOfRun = r => r.period || r.month;
function ladder(kind, month) {
  const best = new Map();   // best run per install
  for (const r of db.runs) {
    if (r.hold || r.kind !== kind || (kind !== 'original' && periodOfRun(r) !== month)) continue;
    if (!best.has(r.install) || r.timeMs < best.get(r.install).timeMs) best.set(r.install, r);
  }
  return [...best.values()].sort((a, b) => a.timeMs - b.timeMs).slice(0, 100)
    .map(({ install, ...r }) => r);
}

// The rules a ladder fixes, as the settings the checker rebuilds the seed from.
function proofSettings(kind, period, seed) {
  if (kind === 'daily') return dailyRoutes.settingsOf(period);
  if (kind === 'race') return period;   // a race hands in its own settings
  if (kind === 'original' && !/^\d{1,9}$/.test(String(seed || ''))) return '{"nologic":1}';
  const s = { seed: kind === 'original' ? String(seed) : seedOf(period) };
  if (kind === 'weekly') Object.assign(s, { ganonCrystals: 2, ganonsTowerCrystals: 2, tourianBosses: 1 });
  return JSON.stringify(s);
}

function verifyProof(kind, period, b, timeMs, done) {
  const child = execFile(CHECKER, [String(timeMs)], { timeout: 60000, maxBuffer: 1 << 16 }, (err, out) => {
    let v;
    try { v = JSON.parse(out); } catch (e) { return done('proof check failed'); }
    if (!v.ok) return done('proof refused: ' + v.reason);
    done(null, { suspect: !!v.suspect, reason: v.reason });
  });
  child.stdin.on('error', () => {});
  child.stdin.end(proofSettings(kind, period, b.seed) + '\n' + b.proof);
}

function send(res, code, body, type = 'application/json') {
  res.writeHead(code, { 'Content-Type': type + '; charset=utf-8', 'Access-Control-Allow-Origin': '*', 'Cache-Control': 'no-store',
    'X-Content-Type-Options': 'nosniff', 'Referrer-Policy': 'no-referrer',
    'Content-Security-Policy': "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'" });
  res.end(typeof body === 'string' ? body : JSON.stringify(body));
}

function readJson(req, res, then) {
  let size = 0; const chunks = [];
  req.on('data', c => { size += c.length; if (size > MAX_BODY) req.destroy(); else chunks.push(c); });
  req.on('end', () => {
    let b; try { b = JSON.parse(Buffer.concat(chunks).toString('utf8')); } catch (e) { return send(res, 400, { error: 'bad json' }); }
    then(b);
  });
}

const CLOCK_SHIFT = Number(process.env.DAILY_CLOCK_SHIFT_MS || 0);   // local tests only: moves the daily challenge's clock
const dailyRoutes = require('./daily')({ DIR, hmac, send, readJson, onlyKeys, limited, clean, verifyProof, now: CLOCK_SHIFT ? () => Date.now() + CLOCK_SHIFT : undefined });

const goalRoutes = require('./goals')({ DIR, send, readJson, onlyKeys, limited, clean, daily: dailyRoutes });

const raceRoutes = require('./race')({ DIR, hmac, send, readJson, onlyKeys, limited, clean, verifyProof });

const page = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
const periods = kind => [...new Set(db.runs.filter(r => r.kind === kind).map(periodOfRun).concat(periodNow(kind, Date.now())))].sort().reverse();
const months = () => periods('monthly');

http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  const ip = (req.headers['x-forwarded-for'] || req.socket.remoteAddress || '').split(',').pop().trim();   // the entry the proxy appended
  if (url.pathname.startsWith('/api/')) console.log(new Date().toISOString(), req.method, url.pathname);
  if (req.method === 'OPTIONS') return send(res, 204, '');
  if (req.method === 'GET' && url.pathname === '/') return send(res, 200, page, 'text/html');
  if (url.pathname.startsWith('/api/daily') || url.pathname === '/api/tokens') { if (dailyRoutes(req, res, url, ip)) return; }
  if (url.pathname.startsWith('/api/goals')) { if (goalRoutes(req, res, url, ip)) return; }
  if (url.pathname.startsWith('/api/race/')) { if (raceRoutes(req, res, url, ip)) return; }
  if (req.method === 'GET' && url.pathname === '/api/monthly') {
    const month = monthOf(Date.now());
    return send(res, 200, { month, seed: seedOf(month), months: months() });
  }
  if (req.method === 'GET' && url.pathname === '/api/weekly') {
    const week = weekOf(Date.now());
    return send(res, 200, { week, seed: seedOf(week), weeks: periods('weekly'), endsAt: weekEnd(Date.now()) });
  }
  if (req.method === 'GET' && url.pathname === '/api/ladder') {
    const k = url.searchParams.get('kind');
    const kind = k === 'monthly' || k === 'weekly' ? k : 'original';
    if (kind === 'weekly') {
      const week = /^\d{4}-W\d{2}$/.test(url.searchParams.get('week') || '') ? url.searchParams.get('week') : weekOf(Date.now());
      return send(res, 200, { kind, week, runs: ladder(kind, week) });
    }
    const month = /^\d{4}-\d{2}$/.test(url.searchParams.get('month') || '') ? url.searchParams.get('month') : monthOf(Date.now());
    return send(res, 200, { kind, month, runs: ladder(kind, month) });
  }
  if (req.method === 'POST' && url.pathname === '/api/start') {
    if (limited('s', ip, START_PER_HOUR)) return send(res, 429, { error: 'slow down' });
    return readJson(req, res, b => {
      if (!onlyKeys(b, ['install', 'kind']) || !['original', 'monthly', 'weekly'].includes(b.kind) || !/^[0-9a-f-]{16,40}$/.test(String(b.install))) return send(res, 400, { error: 'invalid' });
      const now = Date.now(), month = periodNow(b.kind, now);
      const id = crypto.randomBytes(8).toString('hex');
      const body = [id, b.install, b.kind, month, now].join('.');
      const seed = b.kind === 'original' ? undefined : seedOf(month);
      send(res, 200, { token: body + '.' + hmac(body), seed });
    });
  }
  if (req.method === 'POST' && url.pathname === '/api/finish') {
    if (limited('f', ip, FINISH_PER_HOUR)) return send(res, 429, { error: 'slow down' });
    return readJson(req, res, b => {
      if (!onlyKeys(b, ['token', 'timeMs', 'player', 'country', 'proof', 'seed']) || typeof b.token !== 'string' || typeof b.proof !== 'string' || b.proof.length > 12000) return send(res, 400, { error: 'invalid' });
      const parts = b.token.split('.');
      if (parts.length !== 6) return send(res, 400, { error: 'invalid' });
      const body = parts.slice(0, 5).join('.');
      const sig = Buffer.from(parts[5]), want = Buffer.from(hmac(body));
      if (sig.length !== want.length || !crypto.timingSafeEqual(sig, want)) return send(res, 400, { error: 'invalid' });
      const [id, install, kind, month, startedAt] = parts;
      const timeMs = b.timeMs, elapsed = Date.now() - Number(startedAt);
      if (!Number.isInteger(timeMs) || timeMs < MIN_MS || timeMs > MAX_MS || timeMs > elapsed || elapsed > MAX_MS) return send(res, 400, { error: 'invalid time' });
      if (used.has(id)) return send(res, 200, { ok: true, duplicate: true });
      verifyProof(kind, month, b, timeMs, (err, verdict) => {
        if (err) return send(res, 400, { error: err });
        if (used.has(id)) return send(res, 200, { ok: true, duplicate: true });
        used.add(id);
        const mine = db.runs.findIndex(r => r.install === install && r.kind === kind && (kind !== 'original' ? periodOfRun(r) === month : true) && !r.hold === !verdict.suspect);
        const entry = { kind, month: kind === 'monthly' ? month : undefined, period: kind === 'weekly' ? month : undefined, player: clean(b.player, 24) || 'Player', country: /^[A-Z]{2}$/.test(b.country) ? b.country : '', timeMs, finishedAt: Date.now(), install, hold: verdict.suspect ? verdict.reason : undefined };
        if (mine < 0) db.runs.push(entry);
        else if (db.runs[mine].timeMs > timeMs) db.runs[mine] = entry;      // one entry per install and ladder: its best
        else { db.runs[mine].player = entry.player; db.runs[mine].country = entry.country; }
        save();
        const rank = verdict.suspect ? 0 : ladder(kind, month).findIndex(r => r.timeMs === timeMs) + 1;
        send(res, 200, { ok: true, rank, held: verdict.suspect ? true : undefined });
      });
    });
  }
  send(res, 404, { error: 'not found' });
}).listen(PORT, '127.0.0.1', () => console.log('smz3 ladder on 127.0.0.1:' + PORT));
