// GET /api/maprando/seed?settings=<preset names json>&seed=<n> -> BPS patch from the vanilla SM ROM, info in X-Seed-Info, for
// consoles that cannot build Map Rando (docs/STANDALONE.md); one build at a time, cached. Off without MAPRANDO_BIN/DATA/ROM.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFile } = require('child_process');

const KEYS = ['preset', 'skill', 'items', 'qol', 'objectives'];
const NAME = /^[A-Za-z0-9 +.-]{1,40}$/;
const PER_HOUR = 20;
const MAX_QUEUE = 4;
const KEEP = 300;   // patches kept on disk, the oldest go first

module.exports = function ({ DIR, send, limited }) {
  const { MAPRANDO_BIN: BIN, MAPRANDO_DATA: DATA, MAPRANDO_ROM: ROM } = process.env;
  const CACHE = path.join(DIR, 'maprando');
  const queue = [];
  let busy = false;

  function next() {
    if (busy || !queue.length) return;
    busy = true;
    const job = queue.shift();
    const tmp = job.file + '.part';
    execFile(BIN, [DATA, ROM, job.settings, job.seed, tmp, '--bps'], { timeout: 180000, maxBuffer: 1 << 16 }, (err, out) => {
      busy = false;
      if (err) { fs.rm(tmp, { force: true }, () => {}); job.done(err); return next(); }
      fs.renameSync(tmp, job.file);
      fs.writeFileSync(job.file + '.json', out.trim().split('\n').pop());   // upstream also prints progress
      prune();
      job.done(null);
      next();
    });
  }

  function prune() {
    const files = fs.readdirSync(CACHE).filter(f => f.endsWith('.bps')).map(f => ({ f, t: fs.statSync(path.join(CACHE, f)).mtimeMs }));
    files.sort((a, b) => b.t - a.t).slice(KEEP).forEach(({ f }) => {
      fs.rmSync(path.join(CACHE, f), { force: true });
      fs.rmSync(path.join(CACHE, f + '.json'), { force: true });
    });
  }

  function reply(res, file) {
    const info = fs.readFileSync(file + '.json', 'utf8');
    res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'X-Seed-Info': info, 'Access-Control-Allow-Origin': '*',
      'Access-Control-Expose-Headers': 'X-Seed-Info', 'Cache-Control': 'public, max-age=86400', 'X-Content-Type-Options': 'nosniff' });
    fs.createReadStream(file).pipe(res);
  }

  return function routes(req, res, url, ip) {
    if (req.method !== 'GET' || url.pathname !== '/api/maprando/seed') return false;
    if (!BIN || !DATA || !ROM) return send(res, 404, { error: 'not available' }), true;
    let s;
    try { s = JSON.parse(url.searchParams.get('settings') || '{}'); } catch (e) { return send(res, 400, { error: 'bad settings' }), true; }
    const seed = url.searchParams.get('seed') || '';
    if (!s || typeof s !== 'object' || Array.isArray(s) || !Object.keys(s).every(k => KEYS.includes(k) && typeof s[k] === 'string' && NAME.test(s[k]))
        || !/^\d{1,15}$/.test(seed)) return send(res, 400, { error: 'bad settings' }), true;
    const settings = JSON.stringify(Object.fromEntries(KEYS.filter(k => s[k]).map(k => [k, s[k]])));
    fs.mkdirSync(CACHE, { recursive: true });
    const file = path.join(CACHE, crypto.createHash('sha1').update(settings + '#' + seed).digest('hex') + '.bps');
    if (fs.existsSync(file)) { fs.utimes(file, new Date(), new Date(), () => {}); return reply(res, file), true; }
    if (limited('m', ip, PER_HOUR)) return send(res, 429, { error: 'slow down' }), true;
    const waiting = queue.find(j => j.file === file);
    if (!waiting && queue.length >= MAX_QUEUE) return send(res, 503, { error: 'busy, try again in a minute' }), true;
    const done = err => err ? send(res, 422, { error: 'no seed' }) : reply(res, file);
    if (waiting) waiting.others.push(done);
    else {
      const job = { file, settings, seed, others: [] };
      job.done = err => { done(err); job.others.forEach(f => f(err)); };
      queue.push(job);
      next();
    }
    return true;
  };
};
