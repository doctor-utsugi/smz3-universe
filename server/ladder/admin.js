// Moderation for the ladder file. Run on the server, then restart the service so it reloads the file:
//   node admin.js list
//   node admin.js approve <number from list>            (publish a held entry)
//   node admin.js remove <number from list>
//   node admin.js remove-install <number from list>     (every entry of that installation)
//   pm2 restart smz3-ladder
const fs = require('fs');
const path = require('path');
const file = path.join(process.env.LADDER_DIR || path.join(__dirname, 'data'), 'ladder.json');
const db = JSON.parse(fs.readFileSync(file, 'utf8'));
const [cmd, arg] = process.argv.slice(2);
const fmt = ms => `${Math.floor(ms / 3600000)}:${String(Math.floor(ms / 60000) % 60).padStart(2, '0')}:${String(Math.floor(ms / 1000) % 60).padStart(2, '0')}.${String(Math.floor(ms % 1000 / 10)).padStart(2, '0')}`;
if (cmd === 'list') {
  db.runs.forEach((r, i) => console.log(i, r.kind, r.month || '-', fmt(r.timeMs), r.country || '--', r.player, r.install.slice(0, 8), r.hold ? 'HELD: ' + r.hold : ''));
} else if (cmd === 'approve') {
  const r = db.runs[Number(arg)];
  if (!r) { console.error('no such entry'); process.exit(1); }
  delete r.hold;
  fs.writeFileSync(file, JSON.stringify(db));
  console.log('done, restart the service');
} else if (cmd === 'remove' || cmd === 'remove-install') {
  const r = db.runs[Number(arg)];
  if (!r) { console.error('no such entry'); process.exit(1); }
  db.runs = cmd === 'remove' ? db.runs.filter(x => x !== r) : db.runs.filter(x => x.install !== r.install);
  fs.writeFileSync(file, JSON.stringify(db));
  console.log('done, restart the service');
} else {
  console.error('usage: node admin.js list | approve N | remove N | remove-install N');
  process.exit(1);
}
