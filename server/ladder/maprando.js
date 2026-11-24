// GET /api/maprando/seed?settings=<preset names json>&seed=<n> -> BPS patch from the vanilla SM ROM, info in X-Seed-Info, for
// consoles that cannot build Map Rando (docs/STANDALONE.md); one build at a time, cached. Off without MAPRANDO_BIN/DATA/ROM.
// check(): Map Rando's logic for a finished run's proof, through the same one-at-a-time queue (server.js verifyProof).
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFile } = require('child_process');

const KEYS = ['preset', 'skill', 'items', 'qol', 'objectives'];
const NAME = /^[A-Za-z0-9 +.-]{1,40}$/;
const PER_HOUR = 20;
const MAX_QUEUE = 4;
const KEEP = 300;   // patches and check caches kept on disk, the oldest go first

module.exports = function ({ DIR, send, limited }) {
  const { MAPRANDO_BIN: BIN, MAPRANDO_DATA: DATA, MAPRANDO_ROM: ROM } = process.env;
  const CACHE = path.join(DIR, 'maprando');
  const queue = [];
  let busy = false;

  // A job runs BIN with args (and input on stdin); done(err, last line of its output), as upstream also prints progress.
  function next() {
    if (busy || !queue.length) return;
    busy = true;
    const job = queue.shift();
    const child = execFile(BIN, job.args, { timeout: 180000, maxBuffer: 1 << 20 }, (err, out) => {
      busy = false;
      job.done(err, err ? '' : out.trim().split('\n').pop());
      next();
    });
    child.stdin.on('error', () => {});
    child.stdin.end(job.input || '');
  }

  function prune() {
    const files = fs.readdirSync(CACHE).filter(f => f.endsWith('.bps') || f.endsWith('.check.json'))
      .map(f => ({ f, t: fs.statSync(path.join(CACHE, f)).mtimeMs }));
    files.sort((a, b) => b.t - a.t).slice(KEEP).forEach(({ f }) => {
      fs.rmSync(path.join(CACHE, f), { force: true });
      fs.rmSync(path.join(CACHE, f + '.json'), { force: true });
    });
  }

  const cacheName = (settings, seed) => path.join(CACHE, crypto.createHash('sha1').update(settings + '#' + seed).digest('hex'));
  const crateSettings = sm => JSON.stringify(Object.fromEntries(KEYS.filter(k => typeof sm[k] === 'string').map(k => [k, sm[k]])));

  function reply(res, file) {
    const info = fs.readFileSync(file + '.json', 'utf8');
    res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'X-Seed-Info': info, 'Access-Control-Allow-Origin': '*',
      'Access-Control-Expose-Headers': 'X-Seed-Info', 'Cache-Control': 'public, max-age=86400', 'X-Content-Type-Options': 'nosniff' });
    fs.createReadStream(file).pipe(res);
  }

  function routes(req, res, url, ip) {
    if (req.method !== 'GET' || url.pathname !== '/api/maprando/seed') return false;
    if (!BIN || !DATA || !ROM) return send(res, 404, { error: 'not available' }), true;
    let s;
    try { s = JSON.parse(url.searchParams.get('settings') || '{}'); } catch (e) { return send(res, 400, { error: 'bad settings' }), true; }
    const seed = url.searchParams.get('seed') || '';
    if (!s || typeof s !== 'object' || Array.isArray(s) || !Object.keys(s).every(k => KEYS.includes(k) && typeof s[k] === 'string' && NAME.test(s[k]))
        || !/^\d{1,15}$/.test(seed)) return send(res, 400, { error: 'bad settings' }), true;
    const settings = crateSettings(s);
    fs.mkdirSync(CACHE, { recursive: true });
    const file = cacheName(settings, seed) + '.bps';
    if (fs.existsSync(file)) { fs.utimes(file, new Date(), new Date(), () => {}); return reply(res, file), true; }
    if (limited('m', ip, PER_HOUR)) return send(res, 429, { error: 'slow down' }), true;
    const waiting = queue.find(j => j.file === file);
    if (!waiting && queue.length >= MAX_QUEUE) return send(res, 503, { error: 'busy, try again in a minute' }), true;
    const done = err => err ? send(res, 422, { error: 'no seed' }) : reply(res, file);
    if (waiting) waiting.others.push(done);
    else {
      const tmp = file + '.part';
      const job = { file, args: [DATA, ROM, settings, seed, tmp, '--bps'], others: [] };
      job.done = (err, last) => {
        if (err) fs.rm(tmp, { force: true }, () => {});
        else { fs.renameSync(tmp, file); fs.writeFileSync(file + '.json', last); prune(); }
        done(err);
        job.others.forEach(f => f(err));
      };
      queue.push(job);
      next();
    }
    return true;
  }

  // A run's settings JSON (its "sm" object and seed) and the item bits of its cleared checks in order (smz3check's smBits)
  // -> done(err, [in logic or not, per check]). The seed's randomization is kept for the next finish of the same seed.
  routes.check = function (settingsJson, bits, done) {
    if (!BIN || !DATA || !ROM) return done(new Error('Map Rando is not available'));
    let s;
    try { s = JSON.parse(settingsJson); } catch (e) { return done(e); }
    const seed = String(s && s.seed || '');
    if (!s || !s.sm || typeof s.sm !== 'object' || !/^\d{1,15}$/.test(seed) || !/^[\d ]*$/.test(bits)) return done(new Error('bad settings'));
    const settings = crateSettings(s.sm);
    fs.mkdirSync(CACHE, { recursive: true });
    const file = cacheName(settings, seed) + '.check.json';
    queue.push({ args: ['--check', DATA, ROM, settings, seed, file], input: bits, done: (err, last) => {
      if (err) return done(err);
      let v;
      try { v = JSON.parse(last); } catch (e) { return done(e); }
      fs.utimes(file, new Date(), new Date(), () => {});
      prune();
      done(null, String(v.inLogic || '').split(' ').filter(Boolean).map(x => x === '1'));
    } });
    next();
  };

  return routes;
};
