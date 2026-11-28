// One player on every device, and what the RomM sync needs from the server (docs/PLAN_ROMM_SYNC.md, step 2).
//   POST /api/player/code {install}                 -> {code, expiresAt}   a short code another device enters to join
//   POST /api/player/link {install, code}           -> {ok, player}        this install joins the code's player
//   GET  /api/player?install=                       -> {player, devices}
//   POST /api/player/recovery {install, renew?}     -> {code}   the player's recovery code, made on first ask; renew replaces it
//   POST /api/player/recover {install, code}        -> {ok, player}   this install joins the code's player (every device lost)
//   POST /api/sync/hash {install, run, hash, online?, playMs?, proof?} -> {ok}   a save or state SMZ3 itself wrote
//   GET  /api/sync/hash?install=&run=&hash=         -> {known, playMs?, proof?}
//   POST /api/sync/hold {install, run, force?}      -> {ok} or 409 {holder}   an online run plays on one device at a time
//   POST /api/sync/release {install, run}           -> {ok}
// A player's id is the install id of its first device, so data keyed by install before linking stays valid. Every other
// route sees the player id in place of the install (server.js maps it), which makes ladders, daily attempts, goals, gems
// and race rejoins follow the player across devices.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const CODE_MS = 10 * 60 * 1000;
const HASHES_PER_RUN = 64;
const RUN_RE = /^[A-Za-z0-9_-]{1,64}$/;
const HASH_RE = /^[0-9a-f]{16,128}$/;
// Recovery codes: 20 Crockford base32 characters (100 bits), shown as 4 groups of 5. They hold no personal data and work
// like the install ids, which already are the players' only credential.
const B32 = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
const newRecovery = () => Array.from({ length: 20 }, (_, i) => (i && i % 5 === 0 ? '-' : '') + B32[crypto.randomInt(32)]).join('');
const readRecovery = c => {
  const t = String(c || '').toUpperCase().replace(/[\s-]/g, '').replace(/O/g, '0').replace(/[IL]/g, '1');
  return /^[0-9A-HJKMNP-TV-Z]{20}$/.test(t) ? t.replace(/(.{5})(?=.)/g, '$1-') : null;
};

module.exports = function players(ctx) {
  const { DIR, send, readJson, onlyKeys, limited, validInstall } = ctx;
  const clock = ctx.now || Date.now;
  const FILE = path.join(DIR, 'players.json');
  // of[install] = player; codes[code] = {player, exp}; hashes[player][run][hash] = {at, playMs?, proof?}; holds[player][run] = {install, at}
  // recovery[player] = code; recovered[code] = player
  let db = { of: {}, codes: {}, hashes: {}, holds: {}, recovery: {}, recovered: {} };
  try { db = Object.assign(db, JSON.parse(fs.readFileSync(FILE, 'utf8'))); } catch (e) {}
  function save() {
    fs.writeFileSync(FILE + '.tmp', JSON.stringify(db));
    fs.renameSync(FILE + '.tmp', FILE);
  }
  const of = install => db.of[install] || install;

  function routes(req, res, url, ip) {
    const p = url.pathname, now = clock();
    for (const [c, v] of Object.entries(db.codes)) if (v.exp < now) delete db.codes[c];
    if (req.method === 'GET' && p === '/api/player') {
      const install = url.searchParams.get('install');
      if (!validInstall(install)) { send(res, 400, { error: 'bad install' }); return true; }
      const player = of(install);
      send(res, 200, { player, devices: 1 + Object.values(db.of).filter(x => x === player).length });
      return true;
    }
    if (req.method === 'POST' && p === '/api/player/code') {
      if (limited('code', ip, 10)) { send(res, 429, { error: 'slow down' }); return true; }
      readJson(req, res, b => {
        if (!onlyKeys(b, ['install']) || !validInstall(b.install)) return send(res, 400, { error: 'bad request' });
        let code;
        do code = String(crypto.randomInt(0, 1e6)).padStart(6, '0'); while (db.codes[code]);
        db.codes[code] = { player: of(b.install), exp: now + CODE_MS };
        save();
        send(res, 200, { code, expiresAt: now + CODE_MS });
      });
      return true;
    }
    if (req.method === 'POST' && p === '/api/player/link') {
      if (limited('link', ip, 10)) { send(res, 429, { error: 'slow down' }); return true; }
      readJson(req, res, b => {
        if (!onlyKeys(b, ['install', 'code']) || !validInstall(b.install) || !/^\d{6}$/.test(String(b.code || ''))) return send(res, 400, { error: 'bad request' });
        const c = db.codes[b.code];
        if (!c) return send(res, 404, { error: 'unknown or expired code' });
        if (Object.values(db.of).includes(b.install)) return send(res, 409, { error: 'this device already has other devices linked to it' });
        delete db.codes[b.code];
        if (c.player === b.install) delete db.of[b.install]; else db.of[b.install] = c.player;
        save();
        send(res, 200, { ok: true, player: c.player });
      });
      return true;
    }
    if (req.method === 'POST' && p === '/api/player/recovery') {
      if (limited('recovery', ip, 20)) { send(res, 429, { error: 'slow down' }); return true; }
      readJson(req, res, b => {
        if (!onlyKeys(b, ['install', 'renew']) || !validInstall(b.install)) return send(res, 400, { error: 'bad request' });
        const player = of(b.install);
        let code = db.recovery[player];
        if (!code || b.renew) {
          if (code) delete db.recovered[code];
          do code = newRecovery(); while (db.recovered[code]);
          db.recovery[player] = code;
          db.recovered[code] = player;
          save();
        }
        send(res, 200, { code });
      });
      return true;
    }
    if (req.method === 'POST' && p === '/api/player/recover') {
      if (limited('recover', ip, 10)) { send(res, 429, { error: 'slow down' }); return true; }
      readJson(req, res, b => {
        if (!onlyKeys(b, ['install', 'code']) || !validInstall(b.install)) return send(res, 400, { error: 'bad request' });
        const code = readRecovery(b.code), player = code && db.recovered[code];
        if (!code) return send(res, 400, { error: 'a recovery code has 20 letters and digits' });
        if (!player) return send(res, 404, { error: 'unknown recovery code' });
        if (of(b.install) === player) return send(res, 200, { ok: true, player });
        if (Object.values(db.of).includes(b.install)) return send(res, 409, { error: 'this device already has other devices linked to it' });
        if (player === b.install) delete db.of[b.install]; else db.of[b.install] = player;
        save();
        send(res, 200, { ok: true, player });
      });
      return true;
    }
    if (req.method === 'GET' && p === '/api/sync/hash') {
      const install = url.searchParams.get('install'), run = url.searchParams.get('run'), hash = url.searchParams.get('hash');
      if (!validInstall(install) || !RUN_RE.test(run || '') || !HASH_RE.test(hash || '')) { send(res, 400, { error: 'bad request' }); return true; }
      const h = ((db.hashes[of(install)] || {})[run] || {})[hash];
      send(res, 200, h ? { known: true, playMs: h.playMs, proof: h.proof } : { known: false });
      return true;
    }
    if (req.method === 'POST' && p === '/api/sync/hash') {
      if (limited('hash', ip, 120)) { send(res, 429, { error: 'slow down' }); return true; }
      readJson(req, res, b => {
        if (!onlyKeys(b, ['install', 'run', 'hash', 'online', 'playMs', 'proof']) || !validInstall(b.install) || !RUN_RE.test(String(b.run || '')) ||
            !HASH_RE.test(String(b.hash || '')) || (b.playMs !== undefined && !(Number.isInteger(b.playMs) && b.playMs >= 0)) ||
            (b.proof !== undefined && (typeof b.proof !== 'string' || b.proof.length > 16384))) return send(res, 400, { error: 'bad request' });
        const player = of(b.install), hold = (db.holds[player] || {})[b.run];
        if (b.online && hold && hold.install !== b.install) return send(res, 409, { error: 'another device holds this run', holder: hold.install });
        const runs = (db.hashes[player] = db.hashes[player] || {}), byHash = (runs[b.run] = runs[b.run] || {});
        byHash[b.hash] = Object.assign({ at: now }, b.online ? { playMs: b.playMs, proof: b.proof } : {});
        const keys = Object.keys(byHash);
        if (keys.length > HASHES_PER_RUN) keys.sort((x, y) => byHash[x].at - byHash[y].at).slice(0, keys.length - HASHES_PER_RUN).forEach(k => delete byHash[k]);
        save();
        send(res, 200, { ok: true });
      });
      return true;
    }
    if (req.method === 'POST' && (p === '/api/sync/hold' || p === '/api/sync/release')) {
      readJson(req, res, b => {
        if (!onlyKeys(b, ['install', 'run', 'force']) || !validInstall(b.install) || !RUN_RE.test(String(b.run || ''))) return send(res, 400, { error: 'bad request' });
        const player = of(b.install), holds = (db.holds[player] = db.holds[player] || {}), h = holds[b.run];
        if (p === '/api/sync/release') { if (h && h.install === b.install) delete holds[b.run]; }
        else if (h && h.install !== b.install && !b.force) return send(res, 409, { error: 'another device holds this run', holder: h.install, at: h.at });
        else holds[b.run] = { install: b.install, at: now };   // force: the old holder's next upload of this run is refused
        save();
        send(res, 200, { ok: true });
      });
      return true;
    }
    return false;
  }
  routes.of = of;
  return routes;
};
