#!/usr/bin/env node
// Step 11 of docs/RACES.md: real race times against the lengths they were made for, from data/races.json.
//   node balance.js [races.json]
// Draft: per goal kind, how long the winners took against the generator's estimate (the ratio scales draft_estimate in
// smz3daily.c). Championship: the winners' times per length, to check Sprint and Half sit where the menu says.
const fs = require('fs');
const path = require('path');
const file = process.argv[2] || path.join(__dirname, 'data', 'races.json');
const results = (JSON.parse(fs.readFileSync(file, 'utf8')).results || []).filter(r => r.why !== 'everyone left');
const median = a => { const s = [...a].sort((x, y) => x - y); return s.length ? (s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2) : NaN; };
const min = ms => (ms / 60000).toFixed(1);
const best = r => Math.min(...r.players.filter(p => p.timeMs != null).map(p => p.timeMs));

const kinds = {};
for (const r of results.filter(r => r.format === 'draft' && r.draft)) {
  const kind = String(r.draft.goal).split(':')[0] || '?', t = best(r);
  const k = kinds[kind] = kinds[kind] || { n: 0, done: [], ratio: [], draws: 0 };
  k.n++;
  if (!Number.isFinite(t)) { k.draws++; continue; }
  k.done.push(t);
  if (r.draft.estimateMin) k.ratio.push(t / 60000 / r.draft.estimateMin);
}
console.log('Draft (winner time against the estimate)');
console.log('  kind        races  no winner  median min  median real/estimate');
for (const [kind, k] of Object.entries(kinds).sort())
  console.log(`  ${kind.padEnd(10)} ${String(k.n).padStart(6)} ${String(k.draws).padStart(10)} ${min(median(k.done)).padStart(11)} ${median(k.ratio).toFixed(2).padStart(21)}`);

console.log('\nChampionship (winner time)');
for (const f of ['sprint', 'half', 'full']) {
  const t = results.filter(r => r.format === f && r.why !== 'last one left').map(best).filter(Number.isFinite);
  if (!t.length) { console.log(`  ${f.padEnd(7)}    0 races`); continue; }
  console.log(`  ${f.padEnd(7)} ${String(t.length).padStart(4)} races  median ${min(median(t))} min  fastest ${min(Math.min(...t))} min`);
}
