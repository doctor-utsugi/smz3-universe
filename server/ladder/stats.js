// GET /api/stats -> {online, weekPlayers}, for the landing page: counts only, never ids.
//   online: players heard from in the last 10 minutes (any call that names an install)
//   weekPlayers: players heard from, who started a daily or who finished a ladder run in the last 7 days
// Last-heard times live in data/seen.json, written every few minutes and pruned to a week.
const fs = require('fs');
const path = require('path');

const ONLINE_MS = 10 * 60 * 1000;
const WEEK_MS = 7 * 24 * 3600 * 1000;

module.exports = function stats({ DIR, send, daily, runs }) {
  const FILE = path.join(DIR, 'seen.json');
  let seen = {};   // seen[player] = last heard, ms
  try { seen = JSON.parse(fs.readFileSync(FILE, 'utf8')); } catch (e) {}
  let dirty = false, cached = null;
  setInterval(() => {
    if (!dirty) return;
    dirty = false;
    const cut = Date.now() - WEEK_MS;
    for (const p of Object.keys(seen)) if (seen[p] < cut) delete seen[p];
    fs.writeFileSync(FILE + '.tmp', JSON.stringify(seen));
    fs.renameSync(FILE + '.tmp', FILE);
  }, 5 * 60 * 1000).unref();

  function heard(player) {
    if (!daily.validInstall(player)) return;
    seen[player] = Date.now();
    dirty = true;
  }

  function routes(req, res, url) {
    if (req.method !== 'GET' || url.pathname !== '/api/stats') return false;
    const now = Date.now();
    if (!cached || now - cached.at > 30000) {
      let online = 0;
      const week = new Set();
      for (const [p, at] of Object.entries(seen)) {
        if (now - at < ONLINE_MS) online++;
        if (now - at < WEEK_MS) week.add(p);
      }
      for (const p of daily.startedSince(now - WEEK_MS)) week.add(p);
      for (const r of runs()) if (r.finishedAt > now - WEEK_MS) week.add(r.install);
      cached = { at: now, body: { online, weekPlayers: week.size } };
    }
    send(res, 200, cached.body);
    return true;
  }

  return { heard, routes };
};
