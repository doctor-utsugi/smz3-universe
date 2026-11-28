// Runs before the program (emcc --pre-js). The app keeps its store under XDG_DATA_HOME/smz3 as on Linux; here that is
// /data/smz3, kept in the browser's IndexedDB. ROMs the player adds go to /incoming, where the menu takes them.
// Nothing leaves the browser: ROMs, saves and states stay on this device unless the player exports them.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
  FS.mkdir('/incoming');
  FS.mkdir('/data');
  FS.mount(IDBFS, {}, '/data');
  ENV.HOME = '/data/home';
  ENV.XDG_DATA_HOME = '/data';
  addRunDependency('smz3-store');
  FS.syncfs(true, function (err) {
    if (err) console.error('reading the saved data failed', err);
    try { FS.mkdir('/data/home'); } catch (e) {}
    removeRunDependency('smz3-store');
  });
  if (navigator.storage && navigator.storage.persist) navigator.storage.persist();   // the browser won't evict it to free space
  var busy = false, again = false;
  Module.smz3Save = function () {
    if (busy) { again = true; return; }
    busy = true;
    FS.syncfs(false, function (err) {
      if (err) console.error('saving failed', err);
      busy = false;
      if (again) { again = false; Module.smz3Save(); }
    });
  };
  setInterval(Module.smz3Save, 10000);
  document.addEventListener('visibilitychange', function () { if (document.hidden) Module.smz3Save(); });
  window.addEventListener('pagehide', Module.smz3Save);
});

// The page hands over File objects (picked or dropped): each is copied into /incoming
Module.smz3AddFiles = function (files) {
  Array.prototype.forEach.call(files, function (f) {
    f.arrayBuffer().then(function (buf) {
      if (/\.tar$/i.test(f.name)) return Module.smz3Import(new Uint8Array(buf));
      FS.writeFile('/incoming/' + f.name.replace(/[\/\\]/g, '_'), new Uint8Array(buf));
    });
  });
};

// Export and import of the whole store as a tar file: the way to move runs to another browser or keep a copy.
// The base ROMs and the sprites the page ships are left out of the export; the other browser has its own.
var SMZ3_TOP = '/data/smz3/';
function smz3Tar(files) {
  var enc = new TextEncoder(), parts = [];
  files.forEach(function (f) {
    var h = new Uint8Array(512), name = enc.encode(f.path), prefix = new Uint8Array(0);
    if (name.length > 100) {
      var cut = f.path.lastIndexOf('/', 154);
      prefix = enc.encode(f.path.slice(0, cut));
      name = enc.encode(f.path.slice(cut + 1));
    }
    var put = function (at, s) { h.set(typeof s === 'string' ? enc.encode(s) : s, at); };
    put(0, name); put(100, '0000644\0'); put(108, '0000000\0'); put(116, '0000000\0');
    put(124, f.data.length.toString(8).padStart(11, '0') + '\0');
    put(136, Math.floor(f.mtime / 1000).toString(8).padStart(11, '0') + '\0');
    put(148, '        '); put(156, '0'); put(257, 'ustar\0'); put(263, '00'); put(345, prefix);
    var sum = 0;
    for (var i = 0; i < 512; i++) sum += h[i];
    put(148, sum.toString(8).padStart(6, '0') + '\0 ');
    parts.push(h, f.data, new Uint8Array((512 - f.data.length % 512) % 512));
  });
  parts.push(new Uint8Array(1024));
  return new Blob(parts, {type: 'application/x-tar'});
}
Module.smz3Export = function () {
  var files = [];
  (function walk(dir, rel) {
    FS.readdir(dir).forEach(function (n) {
      if (n === '.' || n === '..') return;
      var full = dir + n, r = rel + n, st = FS.stat(full);
      if (FS.isDir(st.mode)) { if (!/^(tmp|derived|base)$/.test(r)) walk(full + '/', r + '/'); }
      else if (/^sprites\//.test(r) && FS.analyzePath('/' + r).exists) return; // shipped in sprites.data
      else if (FS.isFile(st.mode)) files.push({path: r, data: FS.readFile(full), mtime: +st.mtime});
    });
  })(SMZ3_TOP, '');
  var a = document.createElement('a');
  a.href = URL.createObjectURL(smz3Tar(files));
  a.download = 'smz3-saves-' + new Date().toISOString().slice(0, 10) + '.tar';
  a.click();
  setTimeout(function () { URL.revokeObjectURL(a.href); }, 10000);
};
// Takes a tar from Export: its files replace the same files here. The page reloads so the app reads them from the start.
Module.smz3Import = function (tar) {
  var dec = new TextDecoder(), n = 0;
  var str = function (at, len) { var s = dec.decode(tar.subarray(at, at + len)); var z = s.indexOf('\0'); return z < 0 ? s : s.slice(0, z); };
  for (var at = 0; at + 512 <= tar.length; ) {
    var name = str(at, 100);
    if (!name) break;
    var prefix = str(at + 345, 155), size = parseInt(str(at + 124, 12), 8) || 0, type = str(at + 156, 1);
    var path = (prefix ? prefix + '/' : '') + name;
    if ((type === '0' || type === '') && !/(^|\/)\.\.(\/|$)/.test(path) && path[0] !== '/') {
      var full = SMZ3_TOP + path;
      FS.mkdirTree(full.slice(0, full.lastIndexOf('/')));
      FS.writeFile(full, tar.slice(at + 512, at + 512 + size));
      n++;
    }
    at += 512 + Math.ceil(size / 512) * 512;
  }
  FS.syncfs(false, function () { if (n) location.reload(); });
};
