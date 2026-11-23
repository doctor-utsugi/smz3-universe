// Tests for daily.js with a moving clock and a stub proof checker: node test_daily.js (needs smz3daily, see build-daily.sh).
const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');

const DIR = fs.mkdtempSync(path.join(os.tmpdir(), 'daily-'));
let now = Date.parse('2026-11-02T05:00:00Z');
const at = s => { now = Date.parse(s); };
const hmac = s => crypto.createHmac('sha256', 'test').update(s).digest('hex');
const handle = require('./daily')({
  DIR, hmac, now: () => now,
  send: (res, code, body) => res.done(code, body),
  readJson: (req, res, then) => then(req.body),
  onlyKeys: (b, keys) => b && typeof b === 'object' && Object.keys(b).every(k => keys.includes(k)),
  limited: () => false,
  clean: (v, n) => String(v || '').slice(0, n),
  verifyProof: (kind, period, b, t, done) => setImmediate(() => done(null, { suspect: b.proof === 'SUSPECT', reason: 'stub' })),
});

function call(method, url, body) {
  return new Promise(resolve => {
    const res = { done: (code, b) => resolve({ code, body: b }) };
    if (!handle({ method, body }, res, new URL(url, 'http://x'), '127.0.0.1')) resolve({ code: 404 });
  });
}
const A = 'aaaaaaaaaaaaaaaa', B = 'bbbbbbbbbbbbbbbb', C = 'cccccccccccccccc';
const min = m => m * 60000;

(async () => {
  let r = await call('GET', '/api/daily?install=' + A);
  assert.strictEqual(r.body.current, null);                      // 05:00: between the 22:00 and the 06:00 windows
  assert.strictEqual(r.body.next.id, '2026-11-02-0');

  at('2026-11-02T07:00:00Z');
  r = await call('GET', '/api/daily?install=' + A);
  const cur = r.body.current;
  assert.strictEqual(cur.id, '2026-11-02-0');
  assert.strictEqual(cur.state, 'open');
  assert.ok(cur.goal && !('seed' in cur) && !('hints' in cur));  // nothing that helps before the start
  assert.strictEqual(r.body.next.id, '2026-11-02-1');

  r = await call('POST', '/api/daily/start', { install: A, window: '2026-11-02-1' });
  assert.strictEqual(r.code, 409);                               // not the open window
  r = await call('POST', '/api/daily/start', { install: A, window: cur.id });
  assert.strictEqual(r.code, 200);
  const tokA = r.body.token, startA = r.body.startedAt;
  assert.ok(r.body.settings.includes('"challenge":true') && r.body.seed);
  assert.strictEqual(r.body.hints.length, 0);
  assert.strictEqual(r.body.nextHintAt, startA + min(10));

  at('2026-11-02T07:05:00Z');
  r = await call('POST', '/api/daily/start', { install: A, window: cur.id });
  assert.strictEqual(r.body.startedAt, startA);                  // one attempt: starting again resumes it

  at('2026-11-02T07:09:59Z');
  r = await call('GET', '/api/daily/hints?token=' + tokA);
  assert.strictEqual(r.body.hints.length, 0);
  at('2026-11-02T07:10:00Z');
  r = await call('GET', '/api/daily/hints?token=' + tokA);
  assert.strictEqual(r.body.hints.length, 1);
  r = await call('GET', '/api/daily/hints?token=' + tokA.slice(0, -1) + (tokA.endsWith('0') ? '1' : '0'));
  assert.strictEqual(r.code, 400);                               // forged token

  at('2026-11-02T07:50:00Z');
  r = await call('POST', '/api/daily/finish', { token: tokA, timeMs: min(2), proof: 'x' });
  assert.strictEqual(r.body.error, 'invalid time');              // too short
  r = await call('POST', '/api/daily/finish', { token: tokA, timeMs: min(51), proof: 'x' });
  assert.strictEqual(r.body.error, 'invalid time');              // longer than the real time since the start
  r = await call('POST', '/api/daily/finish', { token: tokA, timeMs: min(50), player: 'Ana', country: 'ES', proof: 'x' });
  assert.deepStrictEqual([r.body.ok, r.body.rank, r.body.tokens, r.body.late], [true, 1, 10, undefined]);
  r = await call('POST', '/api/daily/finish', { token: tokA, timeMs: min(49), proof: 'x' });
  assert.ok(r.body.duplicate);                                   // the first finish stays
  r = await call('POST', '/api/daily/start', { install: A, window: cur.id });
  assert.strictEqual(r.code, 409);                               // finished: no second attempt

  // C starts later and is faster; D's proof is held
  at('2026-11-02T08:00:00Z');
  const tokC = (await call('POST', '/api/daily/start', { install: C, window: cur.id })).body.token;
  const tokD = (await call('POST', '/api/daily/start', { install: 'dddddddddddddddd', window: cur.id })).body.token;
  at('2026-11-02T08:30:00Z');
  r = await call('POST', '/api/daily/finish', { token: tokC, timeMs: min(30), player: 'Cid', proof: 'x' });
  assert.strictEqual(r.body.rank, 1);
  r = await call('POST', '/api/daily/finish', { token: tokD, timeMs: min(20), proof: 'SUSPECT' });
  assert.deepStrictEqual([r.body.held, r.body.rank, r.body.tokens], [true, 0, 0]);

  // B starts with 30 minutes left and ends after the close: late, no tokens
  at('2026-11-02T09:30:00Z');
  const tokB = (await call('POST', '/api/daily/start', { install: B, window: cur.id })).body.token;
  at('2026-11-02T10:20:00Z');
  r = await call('GET', '/api/daily?install=' + B);
  assert.strictEqual(r.body.current, null);
  r = await call('POST', '/api/daily/finish', { token: tokB, timeMs: min(50), player: 'Bea', proof: 'x' });
  assert.deepStrictEqual([r.body.late, r.body.rank, r.body.tokens], [true, 0, 0]);

  r = await call('GET', '/api/daily/table?window=' + cur.id);
  assert.deepStrictEqual(r.body.runs.map(x => x.player), ['Cid', 'Ana']);
  assert.deepStrictEqual(r.body.late.map(x => x.player), ['Bea']);
  assert.deepStrictEqual([r.body.started, r.body.finished, r.body.medianMs], [4, 2, min(50)]);
  assert.ok(!('install' in r.body.runs[0]));

  // the window has closed: rank prizes are paid once
  r = await call('GET', '/api/tokens?install=' + C);
  assert.strictEqual(r.body.balance, 10 + 15);
  r = await call('GET', '/api/tokens?install=' + A);
  assert.strictEqual(r.body.balance, 10 + 10);
  await call('GET', '/api/daily');
  assert.strictEqual((await call('GET', '/api/tokens?install=' + A)).body.balance, 20);

  // streak: A finishes on three days in a row (two windows on the third day pay the streak once)
  async function play(id, startIso, finishIso, inst = A) {
    at(startIso);
    const t = (await call('POST', '/api/daily/start', { install: inst, window: id })).body.token;
    at(finishIso);
    return (await call('POST', '/api/daily/finish', { token: t, timeMs: Date.parse(finishIso) - Date.parse(startIso), proof: 'x' })).body;
  }
  let f = await play('2026-11-03-0', '2026-11-03T06:10:00Z', '2026-11-03T07:00:00Z');
  assert.strictEqual(f.tokens, 10);                              // streak of 2: no bonus yet
  f = await play('2026-11-04-1', '2026-11-04T14:10:00Z', '2026-11-04T15:00:00Z');
  assert.strictEqual(f.tokens, 10 + 5);                          // third day in a row
  f = await play('2026-11-04-2', '2026-11-04T22:10:00Z', '2026-11-04T23:00:00Z');
  assert.strictEqual(f.tokens, 10);                              // same day: no second streak bonus
  r = await call('GET', '/api/tokens?install=' + A);
  assert.strictEqual(r.body.streak, 3);
  at('2026-11-06T12:00:00Z');                                    // a day without a run breaks it
  assert.strictEqual((await call('GET', '/api/tokens?install=' + A)).body.streak, 0);

  // the 22:00 window runs past midnight
  at('2026-11-07T01:30:00Z');
  r = await call('GET', '/api/daily');
  assert.strictEqual(r.body.current.id, '2026-11-06-2');

  // state survives a restart: a second instance on the same folder sees the same tokens and tables
  let out;
  const again = require('./daily')({ DIR, hmac, now: () => now, send: (res, code, body) => { out = body; }, readJson: () => {}, onlyKeys: () => true, limited: () => false, clean: v => v, verifyProof: () => {} });
  again({ method: 'GET' }, {}, new URL('/api/tokens?install=' + A, 'http://x'), '');
  assert.strictEqual(out.balance, 20 + 10 + 15 + 10 + 3 * 15);   // alone in the three later windows: rank 1 each
  again({ method: 'GET' }, {}, new URL('/api/daily/table?window=2026-11-02-0', 'http://x'), '');
  assert.deepStrictEqual(out.runs.map(x => x.player), ['Cid', 'Ana']);
  assert.ok(again.settingsOf('2026-11-02-0').includes('"challenge":true'));
  console.log('daily ok');
  process.exit(0);
})().catch(e => { console.error(e); process.exit(1); });
