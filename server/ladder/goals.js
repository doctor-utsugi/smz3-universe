// World goals: on the first of every month (UTC) the server picks a few goals out of goals-catalog.json that every player
// works on together until the month ends. A goal's target grows with what was played the month before. Whoever reached a
// goal's minimum counts in it; when the month ends, counting in n goals pays n(n+1)/2 gems, scaled by the share of those
// goals met (a goal missed pays half).
//   GET  /api/goals?install=                         -> {month, endsAt, onlineOnly, goals, counted, gemsIfMet, last?, prev?}
//   POST /api/goals/report {install, run, online, counts, player?, country?} -> {ok}
// prev is the month before: its goals, each with the install's share and rank and its top 10 ("name|CC|count;..." in top).
// A report carries a run's totals so far (not what changed): the server keeps the latest one per run, so a save state
// loaded on the device takes back what it undid. Each counter is capped per run, and an install reports so many runs a month.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const REPORTS_PER_HOUR = 40;

module.exports = function goals(ctx) {
  const { DIR, send, readJson, onlyKeys, limited, clean, daily } = ctx;
  const clock = ctx.now || Date.now;
  const CATALOG = path.join(__dirname, 'goals-catalog.json');
  const OVERRIDE = path.join(DIR, 'goals-override.json');   // {"2026-11": [{key, target}]}: a month set by hand
  const FILE = path.join(DIR, 'goals.json');

  // months[m] = {goals: [{key, target}], settled, paid: {install: gems}}; runs[m][install][run] = {counts, online, at}
  let db = { months: {}, runs: {}, players: {} };   // players[install] = {player, country}: the name the top 10 shows
  try { db = Object.assign(db, JSON.parse(fs.readFileSync(FILE, 'utf8'))); } catch (e) {}
  function save() {
    fs.writeFileSync(FILE + '.tmp', JSON.stringify(db));
    fs.renameSync(FILE + '.tmp', FILE);
  }
  const readCatalog = () => JSON.parse(fs.readFileSync(CATALOG, 'utf8'));   // read every time: it can be edited live

  const monthOf = t => new Date(t).toISOString().slice(0, 7);
  const monthEnd = m => Date.UTC(Number(m.slice(0, 4)), Number(m.slice(5, 7)), 1);
  const prevMonth = m => monthOf(Date.UTC(Number(m.slice(0, 4)), Number(m.slice(5, 7)) - 2, 1));
  const round2 = v => { const p = Math.pow(10, Math.max(0, Math.floor(Math.log10(v)) - 1)); return Math.ceil(v / p) * p; };

  // Totals per key over the month's runs (online ones only when the catalog says so), and the daily challenges finished.
  function totals(m, cat, install) {
    const all = {}, mine = {};
    for (const [inst, runs] of Object.entries(db.runs[m] || {}))
      for (const r of Object.values(runs)) {
        if (cat.onlineOnly && !r.online) continue;
        for (const [k, v] of Object.entries(r.counts)) { all[k] = (all[k] || 0) + v; if (inst === install) mine[k] = (mine[k] || 0) + v; }
      }
    const d = daily.finishesIn(m);
    all.daily = Object.values(d).reduce((a, b) => a + b, 0);
    if (install) mine.daily = d[install] || 0;
    return { all, mine, dailyBy: d };
  }

  function pick(m, cat) {
    let over = {};
    try { over = JSON.parse(fs.readFileSync(OVERRIDE, 'utf8')); } catch (e) {}
    if (Array.isArray(over[m])) return over[m].filter(g => cat.goals.some(c => c.key === g.key)).map(g => ({ key: g.key, target: Number(g.target) }));
    let seed = parseInt(crypto.createHash('sha256').update('goals:' + m).digest('hex').slice(0, 8), 16);
    const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296; };
    const pool = cat.goals.slice(), chosen = [];
    const take = list => { if (!list.length) return; const g = list[Math.floor(rnd() * list.length)]; chosen.push(g); pool.splice(pool.indexOf(g), 1); };
    for (const group of ['zelda', 'metroid', 'shared']) take(pool.filter(g => g.group === group));
    while (chosen.length < (cat.perMonth || 4) && pool.length) take(pool);
    const before = totals(prevMonth(m), cat).all;
    return chosen.map(g => ({ key: g.key, target: Math.max(g.target, before[g.key] ? round2(before[g.key] * (cat.growth || 1.1)) : 0) }));
  }

  function month(m, cat) {
    if (!db.months[m]) { db.months[m] = { goals: pick(m, cat), settled: false, paid: {} }; save(); }
    return db.months[m];
  }

  // What each install gave to key in month m, most first.
  function contributors(m, cat, key) {
    const by = {};
    if (cat.goals.find(c => c.key === key && c.server)) Object.assign(by, daily.finishesIn(m));
    else for (const [inst, runs] of Object.entries(db.runs[m] || {}))
      for (const r of Object.values(runs)) if ((!cat.onlineOnly || r.online) && r.counts[key]) by[inst] = (by[inst] || 0) + r.counts[key];
    return Object.entries(by).filter(([, n]) => n > 0).sort((a, b) => b[1] - a[1]);
  }
  const who = inst => db.players[inst] || daily.nameOf(inst) || { player: 'Player', country: '' };

  // The month before, as the World tab shows it once it is over.
  function previous(m, cat, install) {
    const pm = prevMonth(m), mo = db.months[pm];
    if (!mo) return undefined;
    const t = totals(pm, cat, install), p = payout(mo.goals, cat, t.mine, t.all);
    return {
      month: pm, counted: p.counted, gems: mo.paid[install] || 0,
      goals: mo.goals.map(g => {
        const c = cat.goals.find(x => x.key === g.key) || {}, list = contributors(pm, cat, g.key), rank = list.findIndex(([i]) => i === install);
        return { key: g.key, title: c.title, sprite: c.sprite, target: g.target, total: t.all[g.key] || 0, mine: t.mine[g.key] || 0,
          rank: rank + 1, players: list.length,
          top: list.slice(0, 10).map(([i, n]) => { const w = who(i); return `${w.player}|${w.country}|${n}`; }).join(';') };
      }),
    };
  }

  const triangle = n => n * (n + 1) / 2;
  function payout(goalsOf, cat, mine, all) {
    let counted = 0, met = 0;
    for (const g of goalsOf) {
      const c = cat.goals.find(x => x.key === g.key) || { min: 1 };
      if ((mine[g.key] || 0) >= c.min) { counted++; if ((all[g.key] || 0) >= g.target) met++; }
    }
    return { counted, gems: counted ? Math.round(triangle(counted) * (met + (counted - met) / 2) / counted) : 0, gemsIfMet: triangle(counted) };
  }

  // Months that have ended pay once.
  function settle(now, cat) {
    for (const [m, mo] of Object.entries(db.months)) {
      if (mo.settled || now < monthEnd(m)) continue;
      const t = totals(m, cat), installs = new Set([...Object.keys(db.runs[m] || {}), ...Object.keys(t.dailyBy)]);
      for (const inst of installs) {
        const mine = totals(m, cat, inst).mine, p = payout(mo.goals, cat, mine, t.all);
        if (p.gems) { daily.grant(inst, 'goals-' + m, p.gems, 'world goals ' + m); mo.paid[inst] = p.gems; }
      }
      mo.settled = true;
      save();
    }
  }

  return function handle(req, res, url, ip) {
    const now = clock(), cat = readCatalog();
    settle(now, cat);
    if (req.method === 'GET' && url.pathname === '/api/goals') {
      const install = url.searchParams.get('install');
      const m = monthOf(now), mo = month(m, cat), t = totals(m, cat, daily.validInstall(install) ? install : null);
      const goals = mo.goals.map(g => {
        const c = cat.goals.find(x => x.key === g.key) || {};
        return { key: g.key, title: c.title, sprite: c.sprite, target: g.target, total: t.all[g.key] || 0, min: c.min, mine: t.mine[g.key] || 0 };
      });
      const p = payout(mo.goals, cat, t.mine, t.all), pm = db.months[prevMonth(m)];
      send(res, 200, { now, month: m, endsAt: monthEnd(m), onlineOnly: !!cat.onlineOnly, goals, counted: p.counted, gemsIfMet: p.gemsIfMet,
        last: pm && pm.settled && install && pm.paid[install] ? { month: prevMonth(m), gems: pm.paid[install] } : undefined,
        prev: previous(m, cat, daily.validInstall(install) ? install : null) });
      return true;
    }
    if (req.method === 'POST' && url.pathname === '/api/goals/report') {
      if (limited('goals', ip, REPORTS_PER_HOUR)) return send(res, 429, { error: 'slow down' }), true;
      readJson(req, res, b => {
        if (!onlyKeys(b, ['install', 'run', 'online', 'counts', 'player', 'country']) || !daily.validInstall(b.install) || !/^[A-Za-z0-9_-]{1,64}$/.test(String(b.run || '')) ||
            !b.counts || typeof b.counts !== 'object' || Array.isArray(b.counts)) return send(res, 400, { error: 'invalid' });
        const m = monthOf(now), byInst = (db.runs[m] = db.runs[m] || {}), runs = (byInst[b.install] = byInst[b.install] || {});
        if (!runs[b.run] && Object.keys(runs).length >= (cat.runsPerInstallPerMonth || 60)) return send(res, 429, { error: 'too many runs this month' });
        const counts = {}, before = ((db.runs[prevMonth(m)] || {})[b.install] || {})[b.run];   // a run begun last month: only what came since
        for (const [k, v] of Object.entries(b.counts)) {
          const c = cat.goals.find(x => x.key === k);
          if (!c || c.server) continue;
          const n = Math.floor(Number(v)) - (before ? before.counts[k] || 0 : 0);
          if (!(n > 0)) continue;
          counts[k] = Math.min(n, c.perRun || n);   // more than one run can do is cut to what one can
        }
        runs[b.run] = { counts, online: !!b.online, at: now };
        if (b.player !== undefined) db.players[b.install] = { player: clean(b.player, 24) || 'Player', country: /^[A-Z]{2}$/.test(b.country) ? b.country : '' };
        save();
        send(res, 200, { ok: true });
      });
      return true;
    }
    return false;
  };
};
