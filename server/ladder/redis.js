// A small Redis client (RESP over one socket, commands answered in order), so the server keeps having no packages.
//   const r = require('./redis')({ host, port, db }); await r.cmd('SET', 'k', 'v');
const net = require('net');

module.exports = function redis({ host = '127.0.0.1', port = 6379, db = 0 } = {}) {
  let sock = null, buf = Buffer.alloc(0), queue = [], ready = null;

  function connect() {
    if (ready) return ready;
    ready = new Promise((resolve, reject) => {
      sock = net.createConnection({ host, port }, () => {
        if (!db) return resolve();
        send(['SELECT', String(db)]).then(resolve, reject);
      });
      sock.on('data', d => { buf = Buffer.concat([buf, d]); drain(); });
      const fail = e => {
        const err = e || new Error('redis closed');
        for (const q of queue) q.reject(err);
        queue = []; ready = null; sock = null; buf = Buffer.alloc(0);
        reject(err);
      };
      sock.on('error', fail);
      sock.on('close', () => fail());
    });
    return ready;
  }

  // One reply from buf at i: [value, next index], or null when it hasn't all arrived.
  function parse(i) {
    const nl = buf.indexOf('\r\n', i);
    if (nl < 0) return null;
    const type = String.fromCharCode(buf[i]), line = buf.toString('utf8', i + 1, nl), next = nl + 2;
    if (type === '+') return [line, next];
    if (type === '-') return [new Error(line), next];
    if (type === ':') return [Number(line), next];
    if (type === '$') {
      const n = Number(line);
      if (n < 0) return [null, next];
      if (buf.length < next + n + 2) return null;
      return [buf.toString('utf8', next, next + n), next + n + 2];
    }
    if (type === '*') {
      const n = Number(line), out = [];
      let j = next;
      for (let k = 0; k < n; k++) {
        const r = parse(j);
        if (!r) return null;
        out.push(r[0]); j = r[1];
      }
      return [n < 0 ? null : out, j];
    }
    return [new Error('bad reply'), buf.length];
  }

  function drain() {
    for (;;) {
      if (!queue.length) return;
      const r = parse(0);
      if (!r) return;
      buf = buf.subarray(r[1]);
      const q = queue.shift();
      if (r[0] instanceof Error) q.reject(r[0]); else q.resolve(r[0]);
    }
  }

  function send(args) {
    return new Promise((resolve, reject) => {
      let out = '*' + args.length + '\r\n';
      for (const a of args) { const s = String(a); out += '$' + Buffer.byteLength(s) + '\r\n' + s + '\r\n'; }
      queue.push({ resolve, reject });
      sock.write(out);
    });
  }

  return {
    async cmd(...args) { await connect(); return send(args); },
    close() { if (sock) sock.end(); },
  };
};
