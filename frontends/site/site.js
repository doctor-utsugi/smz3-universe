'use strict';
// smz3.com landing: tabs, and the live cards from the same-origin API (server/ladder). API text only ever goes in through textContent.
(function () {
  document.documentElement.classList.add('js');

  const $ = id => document.getElementById(id);
  const el = (tag, cls, text) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = String(text);
    return e;
  };
  const UNAVAILABLE = 'Unavailable right now.';

  function getJson(url) {
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 10000);
    return fetch(url, { signal: ctl.signal, credentials: 'omit', headers: { Accept: 'application/json' } })
      .then(r => { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .finally(() => clearTimeout(timer));
  }

  const num = n => Number(n || 0).toLocaleString('en-US');
  const hms = ms => {
    const s = Math.max(0, Math.floor(ms / 1000));
    return Math.floor(s / 3600) + ':' + String(Math.floor(s / 60) % 60).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
  };
  function span(ms) {
    const m = Math.max(0, Math.ceil(ms / 60000));
    if (m >= 48 * 60) return Math.floor(m / 1440) + ' days';
    if (m >= 60) return Math.floor(m / 60) + 'h ' + String(m % 60).padStart(2, '0') + 'm';
    return m + ' min';
  }

  // ---------- sections: the header links show one panel at a time ----------
  const panels = [...document.querySelectorAll('.panels > .panel')];
  const links = [...document.querySelectorAll('a[data-tab]')];
  const onShow = {};

  function select(id) {
    if (!panels.some(p => p.id === id)) return false;
    panels.forEach(p => { p.hidden = p.id !== id; });
    document.querySelectorAll('.sections-nav a').forEach(a => {
      if (a.dataset.tab === id) a.setAttribute('aria-current', 'true');
      else a.removeAttribute('aria-current');
    });
    if (onShow[id]) onShow[id]();
    return true;
  }
  function selectAndShow(id) {
    if (!select(id)) return;
    history.replaceState(null, '', '#' + id);
    $('sections').scrollIntoView({ block: 'start' });
  }

  links.forEach(a => a.addEventListener('click', e => {
    e.preventDefault();
    selectAndShow(a.dataset.tab);
  }));
  window.addEventListener('hashchange', () => {
    const id = location.hash.slice(1);
    if (panels.some(p => p.id === id)) selectAndShow(id);
  });

  // ---------- daily ----------
  let daily = null, clockShift = 0, refetching = false;   // clockShift: server time minus local time, so the countdown follows the server
  function renderDaily() {
    const left = $('daily-left'), goal = $('daily-goal'), meta = $('daily-meta');
    meta.replaceChildren();
    if (!daily) {
      left.textContent = '';
      goal.replaceChildren(el('span', 'quiet', UNAVAILABLE));
      return;
    }
    const now = Date.now() + clockShift, cur = daily.current;
    const edge = cur ? cur.closesAt : daily.next && daily.next.opensAt;
    if (edge && now >= edge && !refetching) { refetching = true; setTimeout(loadDaily, 5000); }
    if (cur && now < cur.closesAt) {
      left.textContent = span(cur.closesAt - now) + ' left';
      goal.textContent = cur.goal || 'Today\'s challenge';
      meta.append(el('span', 'live', '● LIVE'));
      if (cur.difficulty) meta.append(el('span', '', cur.difficulty));
      const p = cur.players || {};
      if (p.now > 0) meta.append(el('span', '', num(p.now) + ' playing'));
      if (p.done > 0) meta.append(el('span', '', num(p.done) + ' finished'));
      if (!(p.now > 0) && !(p.done > 0) && p.started > 0) meta.append(el('span', '', num(p.started) + ' started'));
    } else if (daily.next && daily.next.opensAt) {
      left.textContent = now < daily.next.opensAt ? 'opens in ' + span(daily.next.opensAt - now) : 'opening';
      goal.textContent = 'The next window opens soon';
      meta.append(el('span', '', 'Three 4-hour windows a day: 06:00, 14:00 and 22:00 UTC'));
    } else {
      daily = null;
      renderDaily();
    }
  }
  function loadDaily() {
    refetching = false;
    getJson('/api/daily?game=smz3').then(d => {
      if (!d || typeof d !== 'object') throw new Error('bad data');
      daily = d;
      if (typeof d.now === 'number') clockShift = d.now - Date.now();
    }).catch(() => { daily = null; }).then(renderDaily);
  }

  // ---------- world goals ----------
  function loadGoals() {
    const left = $('goals-left'), box = $('goals-list');
    getJson('/api/goals?game=smz3').then(d => {
      if (!d || !Array.isArray(d.goals) || !d.goals.length) throw new Error('no goals');
      const now = typeof d.now === 'number' ? d.now : Date.now();
      left.textContent = typeof d.endsAt === 'number' ? span(d.endsAt - now) + ' left' : '';
      const ul = el('ul', 'goals-list');
      for (const g of d.goals) {
        const total = Math.max(0, Number(g.total) || 0), target = Math.max(1, Number(g.target) || 1);
        const li = el('li');
        const line = el('div', 'goal-line');
        line.append(el('span', 'goal-name', g.title || g.key || 'Goal'),
          el('span', 'goal-num' + (total >= target ? ' goal-met' : ''), num(total) + ' / ' + num(target)));
        const bar = el('progress', 'goal-bar');
        bar.max = target;
        bar.value = Math.min(total, target);
        bar.setAttribute('aria-label', (g.title || 'Goal') + ': ' + num(total) + ' of ' + num(target));
        li.append(line, bar);
        ul.append(li);
      }
      box.replaceChildren(ul);
    }).catch(() => {
      left.textContent = '';
      box.replaceChildren(el('span', 'quiet', UNAVAILABLE));
    });
  }

  // ---------- downloads ----------
  function loadVersion() {
    getJson('/api/version').then(d => {
      const tag = typeof d.tag === 'string' ? d.tag : '', base = typeof d.base === 'string' ? d.base : '';
      if (!tag) throw new Error('no tag');
      $('dl-version').textContent = tag;
      if (!/^https:\/\/github\.com\/doctor-utsugi\/smz3-universe\/releases\/download\/[^/]+\/$/.test(base)) return;
      document.querySelectorAll('a[data-asset]').forEach(a => { a.href = base + a.dataset.asset; });
    }).catch(() => { $('dl-version').textContent = 'see GitHub'; });
  }

  // ---------- rankings ----------
  const pills = [...document.querySelectorAll('.pill[data-game]')];
  const boards = {};   // game -> runs, or null when the call failed
  let board = 'smz3';
  function renderBoard() {
    const rows = $('board-rows'), note = $('board-note'), runs = boards[board];
    rows.replaceChildren();
    if (runs === undefined) { note.textContent = 'Loading…'; note.hidden = false; return; }
    if (runs === null) { note.textContent = UNAVAILABLE; note.hidden = false; return; }
    if (!runs.length) { note.textContent = 'No times yet.'; note.hidden = false; return; }
    note.hidden = true;
    runs.slice(0, 10).forEach((r, i) => {
      const tr = el('tr');
      tr.append(el('td', 'rank', i + 1), el('td', 'player', r.player || 'Player'),
        el('td', 'country', /^[A-Z]{2}$/.test(r.country || '') ? r.country : '--'), el('td', 'time', hms(Number(r.timeMs) || 0)));
      rows.append(tr);
    });
  }
  function showBoard(game) {
    board = game;
    pills.forEach(p => p.setAttribute('aria-pressed', p.dataset.game === game ? 'true' : 'false'));
    renderBoard();
    if (boards[game] !== undefined) return;
    getJson('/api/ladder?game=' + encodeURIComponent(game) + '&kind=original').then(d => {
      boards[game] = d && Array.isArray(d.runs) ? d.runs : null;
    }).catch(() => { boards[game] = null; }).then(() => { if (board === game) renderBoard(); });
  }
  pills.forEach(p => p.addEventListener('click', () => {
    if (boards[p.dataset.game] === null) delete boards[p.dataset.game];   // a failed table is asked for again
    showBoard(p.dataset.game);
  }));
  let boardsStarted = false;
  onShow.rankings = () => { if (!boardsStarted) { boardsStarted = true; showBoard(board); } };

  const start = location.hash.slice(1);
  if (!select(start)) select('games');

  loadDaily();
  loadGoals();
  loadVersion();
  setInterval(renderDaily, 30000);
  setInterval(loadDaily, 5 * 60000);
})();
