// One player on every device, and what the RomM sync needs from the server (docs/PLAN_ROMM_SYNC.md, step 2).
//   POST /api/player/code {install}                 -> {code, expiresAt}   a short code another device enters to join
//   POST /api/player/link {install, code}           -> {ok, player}        this install joins the code's player
//   GET  /api/player?install=                       -> {player, devices}
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

module.exports = function players(ctx) {
  const { DIR, send, readJson, onlyKeys, limited, validInstall } = ctx;
  const clock = ctx.now || Date.now;
  const FILE = path.join(DIR, 'players.json');
  // of[install] = player; codes[code] = {player, exp}; hashes[player][run][hash] = {at, playMs?, proof?}; holds[player][run] = {install, at}
  let db = { of: {}, codes: {}, hashes: {}, holds: {} };
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
