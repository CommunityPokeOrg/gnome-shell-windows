/* Adwaita Shell — shell logic: window manager, workspaces, dock, overview,
   quick settings, calendar + notifications. */

(function () {
  const $ = (s) => document.querySelector(s);
  const el = (tag, cls, html) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (html !== undefined) n.innerHTML = html;
    return n;
  };

  /* ---------------- SVG icon set ---------------- */
  const S = 'xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"';
  const ICONS = {
    wifi: `<svg ${S}><path d="M8 12.8a1.2 1.2 0 1 0 0 .01zM4.9 10.4l1.4 1.4a2.4 2.4 0 0 1 3.4 0l1.4-1.4a4.4 4.4 0 0 0-6.2 0zM2.4 7.9l1.4 1.4a5.9 5.9 0 0 1 8.4 0l1.4-1.4a7.9 7.9 0 0 0-11.2 0zM0 5.4l1.4 1.4a9.3 9.3 0 0 1 13.2 0L16 5.4a11.3 11.3 0 0 0-16 0z"/></svg>`,
    volume: `<svg ${S}><path d="M8 2L4.5 5H1v6h3.5L8 14V2zm3 3.2a3.9 3.9 0 0 1 0 5.6l-1-1a2.4 2.4 0 0 0 0-3.6l1-1zm1.9-2a6.4 6.4 0 0 1 0 9.6l-1-1a4.9 4.9 0 0 0 0-7.6l1-1z"/></svg>`,
    battery: `<svg ${S}><path d="M1 4h12a1 1 0 0 1 1 1v1h1v4h-1v1a1 1 0 0 1-1 1H1a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1zm1 2v4h9V6H2z"/></svg>`,
    bluetooth: `<svg ${S}><path d="M7.2 0l4 4-2.8 2.8L11.2 9.6l-4 4-.7-3.4-2.6 2.6-1-1 3.3-3.2L2.9 5.4l1-1 2.6 2.6L5.8 3.6 7.2 0zm1 3.4v2.2L9.6 4 8.2 3.4zm0 7v2.2L9.6 9.6l-1.4.8z" transform="translate(1,-1) scale(.9)"/></svg>`,
    moon: `<svg ${S}><path d="M13.5 9.8A6.5 6.5 0 0 1 6.2 2.5 6.5 6.5 0 1 0 13.5 9.8z"/></svg>`,
    plane: `<svg ${S}><path d="M8 1l1 5 6 2v1.5L9 8v4l1.8 1.3V15L8 14l-2.8 1v-1.7L7 12V8L1 9.5V8l6-2 1-5z"/></svg>`,
    sun: `<svg ${S}><path d="M8 11a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM7 0h2v3H7zM7 13h2v3H7zM0 7h3v2H0zM13 7h3v2h-3zM2.2 3.6l2.1 2.1-1.4 1.4-2.1-2.1zM11.7 8.9l2.1 2.1-1.4 1.4-2.1-2.1zM3.6 13.8l2.1-2.1 1.4 1.4-2.1 2.1zM8.9 4.3l2.1-2.1 1.4 1.4-2.1 2.1z"/></svg>`,
    power: `<svg ${S}><path d="M7 0h2v8H7zM4.3 2.6L2.9 4a6 6 0 1 0 10.2 0l-1.4-1.4A4.5 4.5 0 1 1 4.3 2.6z"/></svg>`,
    lock: `<svg ${S}><path d="M8 1a3.5 3.5 0 0 0-3.5 3.5V7H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V8a1 1 0 0 0-1-1h-1.5V4.5A3.5 3.5 0 0 0 8 1zm0 2A1.5 1.5 0 0 1 9.5 4.5V7h-3V4.5A1.5 1.5 0 0 1 8 3z"/></svg>`,
    dash: `<svg ${S}><rect x="2" y="7" width="12" height="2" rx="1"/></svg>`,
    search: `<svg ${S} fill="none" stroke="currentColor" stroke-width="1.8"><circle cx="7" cy="7" r="4.5"/><path d="M10.5 10.5L15 15" stroke-linecap="round"/></svg>`,
    grid: `<svg ${S}><g><rect x="1" y="1" width="4" height="4" rx="1"/><rect x="6" y="1" width="4" height="4" rx="1"/><rect x="11" y="1" width="4" height="4" rx="1"/><rect x="1" y="6" width="4" height="4" rx="1"/><rect x="6" y="6" width="4" height="4" rx="1"/><rect x="11" y="6" width="4" height="4" rx="1"/><rect x="1" y="11" width="4" height="4" rx="1"/><rect x="6" y="11" width="4" height="4" rx="1"/><rect x="11" y="11" width="4" height="4" rx="1"/></g></svg>`,
  };
  document.querySelectorAll('.ic[data-ic]').forEach((n) => { n.innerHTML = ICONS[n.dataset.ic] || ''; });

  /* ---------------- State ---------------- */
  const state = {
    workspaces: [[]],            // array of arrays of window objects
    activeWs: 0,
    z: 10,
    focused: null,               // window object
    overviewOpen: false,
    gridOpen: false,
    notifications: [],
    dnd: false,
    windowsApps: [],             // real Start Menu apps
    info: {},
    winSeq: 0,
  };

  const desktop = $('#desktop');
  const dockEl = $('#dock');
  const dashEl = $('#dash');
  const overviewEl = $('#overview');
  const gridWrap = $('#app-grid-wrap');
  const gridEl = $('#app-grid');
  const spreadEl = $('#window-spread');
  const stripEl = $('#workspace-strip');

  const popovers = ['#calendar-popover', '#quicksettings', '#power-menu'];
  const closePopovers = (except) =>
    popovers.forEach((s) => { if (s !== except) $(s).classList.add('hidden'); });
  const togglePopover = (sel) => {
    const p = $(sel);
    const wasHidden = p.classList.contains('hidden');
    closePopovers();
    p.classList.toggle('hidden', !wasHidden);
  };

  /* ---------------- Clock ---------------- */
  const fmtClock = () => {
    const d = new Date();
    const days = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    let h = d.getHours(); const ap = h >= 12 ? 'PM' : 'AM'; h = h % 12 || 12;
    const m = String(d.getMinutes()).padStart(2, '0');
    return `${days[d.getDay()]} ${months[d.getMonth()]} ${d.getDate()}  ${h}:${m} ${ap}`;
  };
  const tick = () => { $('#clock-text').textContent = fmtClock(); };
  tick(); setInterval(tick, 5000);

  /* ---------------- Window manager ---------------- */
  function openApp(appId) {
    const app = AppRegistry[appId];
    if (!app) return;
    // Focus an existing window of this app if there is one.
    for (let i = 0; i < state.workspaces.length; i++) {
      const w = state.workspaces[i].find((w) => w.appId === appId);
      if (w) {
        if (i !== state.activeWs) switchWs(i);
        w.minimized = false;
        renderDesktop();
        focusWindow(w);
        return;
      }
    }
    const idx = state.workspaces[state.activeWs].length;
    const W = Math.min(820, innerWidth - 220);
    const H = Math.min(560, innerHeight - 220);
    const win = {
      id: ++state.winSeq,
      appId,
      title: app.name,
      x: 90 + (idx % 6) * 36,
      y: 30 + (idx % 6) * 30,
      w: W, h: H,
      minimized: false,
      maximized: false,
    };
    state.workspaces[state.activeWs].push(win);
    renderDesktop();
    focusWindow(win);
    renderDock();
    bounceDock(appId);
  }

  function tileHtml(app, size) {
    const t = el('div', 'tile', app.glyph);
    t.style.background = app.color;
    return t;
  }

  function renderDesktop() {
    normalizeWs();
    desktop.innerHTML = '';
    const ws = state.workspaces[state.activeWs];
    ws.forEach((w) => {
      if (w.minimized) return;
      const app = AppRegistry[w.appId];
      const node = el('section', 'app-window');
      node.style.left = w.x + 'px';
      node.style.top = w.y + 'px';
      node.style.width = w.w + 'px';
      node.style.height = w.h + 'px';
      node.style.zIndex = w.z || 1;
      if (w.maximized) node.classList.add('maximized');
      node.dataset.winId = w.id;

      const bar = el('header', 'titlebar');
      const icon = el('span', 'tb-icon', app.glyph);
      icon.style.background = app.color;
      bar.appendChild(icon);
      bar.appendChild(el('div', 'tb-title', w.title));
      const btns = el('div', 'win-btns');
      const bMin = el('button', 'win-btn', '–');
      const bMax = el('button', 'win-btn', w.maximized ? '❐' : '□');
      const bClose = el('button', 'win-btn close', '✕');
      bMin.title = 'Minimize'; bMax.title = 'Maximize'; bClose.title = 'Close';
      bMin.onclick = (e) => { e.stopPropagation(); w.minimized = true; if (state.focused === w) state.focused = null; renderDesktop(); renderDock(); };
      bMax.onclick = (e) => { e.stopPropagation(); toggleMax(w); };
      bClose.onclick = (e) => { e.stopPropagation(); closeWindow(w); };
      btns.append(bMin, bMax, bClose);
      bar.appendChild(btns);
      node.appendChild(bar);

      const content = el('div', 'win-content');
      node.appendChild(content);
      app.render(content, shellApiFacade);
      w.node = node;
      w.content = content;

      node.addEventListener('pointerdown', () => focusWindow(w));
      bar.addEventListener('pointerdown', (e) => startDrag(e, w, node));
      bar.addEventListener('dblclick', (e) => { if (!e.target.closest('.win-btn')) toggleMax(w); });
      desktop.appendChild(node);
      applyMax(w, node);
      if (state.focused !== w) node.classList.add('unfocused');
    });
    updateAppMenu();
  }

  function applyMax(w, node) {
    if (w.maximized) {
      node.style.left = '0px'; node.style.top = '0px';
      node.style.width = desktop.clientWidth + 'px';
      node.style.height = desktop.clientHeight + 'px';
    }
  }

  function toggleMax(w) {
    w.maximized = !w.maximized;
    renderDesktop();
  }

  function closeWindow(w) {
    const ws = state.workspaces[state.activeWs];
    const i = ws.indexOf(w);
    if (i >= 0) ws.splice(i, 1);
    if (state.focused === w) state.focused = null;
    renderDesktop(); renderDock();
  }

  function focusWindow(w) {
    if (w.minimized) { w.minimized = false; renderDesktop(); }
    state.focused = w;
    w.z = ++state.z;
    desktop.querySelectorAll('.app-window').forEach((n) => {
      const isTarget = +n.dataset.winId === w.id;
      n.style.zIndex = isTarget ? w.z : n.style.zIndex;
      n.classList.toggle('unfocused', !isTarget);
    });
    updateAppMenu();
  }

  function startDrag(e, w, node) {
    if (w.maximized || e.target.closest('.win-btn')) return;
    const startX = e.clientX - w.x;
    const startY = e.clientY - w.y;
    const move = (ev) => {
      w.x = Math.min(Math.max(ev.clientX - startX, -w.w + 80), desktop.clientWidth - 80);
      w.y = Math.min(Math.max(ev.clientY - startY, 0), desktop.clientHeight - 40);
      node.style.left = w.x + 'px';
      node.style.top = w.y + 'px';
    };
    const up = () => {
      document.removeEventListener('pointermove', move);
      document.removeEventListener('pointerup', up);
    };
    document.addEventListener('pointermove', move);
    document.addEventListener('pointerup', up);
  }

  function updateAppMenu() {
    const m = $('#appmenu');
    if (state.focused && !state.overviewOpen) {
      const app = AppRegistry[state.focused.appId];
      $('#appmenu-name').textContent = app.name;
      const ic = $('#appmenu-icon');
      ic.textContent = app.glyph.replace(/<[^>]*>/g, '').slice(0, 1);
      ic.style.background = app.color;
      m.classList.remove('hidden');
    } else {
      m.classList.add('hidden');
    }
  }

  /* ---------------- Dock / dash ---------------- */
  function runningApps() {
    const set = new Map();
    state.workspaces.forEach((ws) => ws.forEach((w) => set.set(w.appId, w)));
    return set;
  }

  function buildDock(container, inOverview) {
    container.innerHTML = '';
    const running = runningApps();
    const favs = Object.entries(AppRegistry).filter(([, a]) => a.fav).map(([id]) => id);
    const extras = [...running.keys()].filter((id) => !favs.includes(id));

    const addItem = (appId) => {
      const app = AppRegistry[appId];
      const item = el('button', 'dock-item' + (running.has(appId) ? ' running' : ''));
      item.dataset.appId = appId;
      item.appendChild(tileHtml(app));
      item.appendChild(el('span', 'running-dot'));
      item.appendChild(el('span', 'dock-tip', app.name));
      item.onclick = () => onDockClick(appId);
      container.appendChild(item);
    };

    favs.forEach(addItem);
    if (extras.length) {
      container.appendChild(el('div', 'dock-sep'));
      extras.forEach(addItem);
    }
    if (inOverview) {
      container.appendChild(el('div', 'dock-sep'));
      const gridBtn = el('button', 'dock-item');
      gridBtn.innerHTML = `<span class="tile" style="background:#5e5c64"><span class="ic" data-ic="grid" style="width:22px;height:22px">${ICONS.grid}</span></span>`;
      gridBtn.appendChild(el('span', 'dock-tip', 'Show Apps'));
      gridBtn.onclick = () => setGrid(true);
      container.appendChild(gridBtn);
    }
  }

  function onDockClick(appId) {
    const running = runningApps();
    const w = running.get(appId);
    if (!w) { openApp(appId); return; }
    const wsIdx = state.workspaces.findIndex((ws) => ws.includes(w));
    if (state.focused === w && !w.minimized && wsIdx === state.activeWs) {
      w.minimized = true; state.focused = null; renderDesktop(); renderDock();
    } else {
      if (wsIdx !== state.activeWs) switchWs(wsIdx);
      if (w.minimized) { w.minimized = false; renderDesktop(); }
      focusWindow(w);
    }
  }

  function renderDock() {
    buildDock(dockEl, false);
    buildDock(dashEl, true);
  }

  function bounceDock(appId) {
    document.querySelectorAll(`.dock-item[data-app-id="${appId}"] .tile`).forEach((t) => {
      const p = t.parentElement;
      p.classList.add('launching');
      setTimeout(() => p.classList.remove('launching'), 550);
    });
  }

  /* ---------------- Workspaces ---------------- */
  // GNOME-style dynamic workspaces: empty workspaces between non-empty ones
  // are removed, and there is always exactly one empty workspace at the end.
  function normalizeWs() {
    for (let i = state.workspaces.length - 2; i >= 0; i--) {
      const hasLater = state.workspaces.slice(i + 1).some((w) => w.length);
      if (state.workspaces[i].length === 0 && hasLater) {
        state.workspaces.splice(i, 1);
        if (state.activeWs > i) state.activeWs--;
        else if (state.activeWs === i) state.activeWs = Math.min(i, state.workspaces.length - 1);
      }
    }
    const last = state.workspaces[state.workspaces.length - 1];
    if (!last || last.length) state.workspaces.push([]);
  }

  function switchWs(i) {
    if (i < 0 || i >= state.workspaces.length) return;
    state.activeWs = i;
    state.focused = null;
    renderDesktop(); renderDock();
    if (state.overviewOpen) renderOverview();
  }

  function moveWindowToWs(w, target) {
    if (target === state.activeWs) return;
    const src = state.workspaces[state.activeWs];
    const i = src.indexOf(w);
    if (i < 0 || !state.workspaces[target]) return;
    src.splice(i, 1);
    state.workspaces[target].push(w);
    if (state.focused === w) state.focused = null;
    renderDesktop(); renderDock(); renderOverview();
  }

  /* ---------------- Overview ---------------- */
  function setOverview(open) {
    state.overviewOpen = open;
    overviewEl.classList.toggle('hidden', !open);
    dockEl.classList.toggle('hidden', open);
    closePopovers();
    if (open) {
      $('#search').value = '';
      renderOverview();
      setTimeout(() => $('#search').focus(), 30);
    } else {
      setGrid(false);
      updateAppMenu();
    }
  }

  function setGrid(open) {
    state.gridOpen = open;
    gridWrap.classList.toggle('hidden', !open);
    $('#overview-body').classList.toggle('hidden', open);
    if (open) renderGrid($('#search').value.trim());
  }

  function renderOverview() {
    // Window spread for the active workspace.
    spreadEl.innerHTML = '';
    const ws = state.workspaces[state.activeWs];
    if (!ws.length) {
      spreadEl.appendChild(el('div', 'spread-empty', 'No open windows on this workspace'));
    }
    ws.forEach((w) => {
      const app = AppRegistry[w.appId];
      const th = el('div', 'wincthumb');
      th.draggable = true;
      const body = el('div', 'thumb-body');
      if (w.content) {
        const clone = w.content.cloneNode(true);
        const scale = Math.min(300 / Math.max(w.w, 1), 175 / Math.max(w.h, 1));
        clone.style.cssText = `position:absolute;left:0;top:0;width:${w.w}px;height:${w.h}px;transform:scale(${scale});transform-origin:top left;pointer-events:none;`;
        body.appendChild(clone);
      }
      const t = el('div', 'thumb-title');
      const ic = el('span', 'appicon-sm', app.glyph.replace(/<[^>]*>/g, '').slice(0, 1));
      ic.style.background = app.color;
      t.appendChild(ic); t.appendChild(el('span', '', w.title));
      const x = el('button', 'thumb-close', '✕');
      x.onclick = (e) => { e.stopPropagation(); closeWindow(w); renderOverview(); };
      th.appendChild(body); th.appendChild(t); th.appendChild(x);
      th.onclick = () => { setOverview(false); focusWindow(w); };
      th.addEventListener('dragstart', (e) => e.dataTransfer.setData('text/win', String(w.id)));
      spreadEl.appendChild(th);
    });

    // Workspace strip.
    stripEl.innerHTML = '';
    state.workspaces.forEach((ws, i) => {
      const t = el('div', 'ws-thumb' + (i === state.activeWs ? ' active' : ''));
      ws.forEach((w) => {
        const m = el('div', 'ws-mini');
        const sx = 132 / Math.max(desktop.clientWidth, 1);
        const sy = 82 / Math.max(desktop.clientHeight, 1);
        m.style.left = Math.max(0, w.x * sx) + 'px';
        m.style.top = Math.max(0, w.y * sy) + 'px';
        m.style.width = Math.min(132, w.w * sx) + 'px';
        m.style.height = Math.min(82, w.h * sy) + 'px';
        t.appendChild(m);
      });
      t.appendChild(el('span', 'ws-label', String(i + 1)));
      t.onclick = () => switchWs(i);
      t.addEventListener('dragover', (e) => { e.preventDefault(); t.classList.add('drop-hover'); });
      t.addEventListener('dragleave', () => t.classList.remove('drop-hover'));
      t.addEventListener('drop', (e) => {
        e.preventDefault(); t.classList.remove('drop-hover');
        const id = +e.dataTransfer.getData('text/win');
        const w = state.workspaces[state.activeWs].find((x) => x.id === id);
        if (w) moveWindowToWs(w, i);
      });
      stripEl.appendChild(t);
    });
  }

  function renderGrid(filter) {
    gridEl.innerHTML = '';
    const f = (filter || '').toLowerCase();
    const builtins = Object.entries(AppRegistry)
      .filter(([, a]) => !f || a.name.toLowerCase().includes(f));
    const winapps = state.windowsApps
      .filter((a) => !f || a.name.toLowerCase().includes(f))
      .slice(0, 64);

    builtins.forEach(([id, a]) => {
      const item = el('button', 'app-grid-item');
      item.appendChild(tileHtml(a));
      item.appendChild(el('span', 'lbl', a.name));
      item.onclick = () => { setOverview(false); openApp(id); };
      gridEl.appendChild(item);
    });
    winapps.forEach((a) => {
      const item = el('button', 'app-grid-item winapp');
      const t = el('div', 'tile', a.name[0].toUpperCase());
      item.appendChild(t);
      item.appendChild(el('span', 'lbl', a.name));
      item.title = a.path;
      item.onclick = async () => {
        setOverview(false);
        const r = await window.shellAPI.launch(a.path);
        notify('software', r.ok ? 'Launched' : 'Launch failed',
          r.ok ? a.name : (r.error || a.name));
      };
      gridEl.appendChild(item);
    });
    if (!builtins.length && !winapps.length) {
      gridEl.appendChild(el('div', 'grid-empty', `No results for "${filter}"`));
    }
  }

  /* ---------------- Calendar + notifications ---------------- */
  let calDate = new Date();
  function renderCalendar() {
    const months = ['January', 'February', 'March', 'April', 'May', 'June', 'July',
      'August', 'September', 'October', 'November', 'December'];
    $('#cal-title').textContent = `${months[calDate.getMonth()]} ${calDate.getFullYear()}`;
    const grid = $('#cal-grid');
    grid.innerHTML = '';
    ['S', 'M', 'T', 'W', 'T', 'F', 'S'].forEach((d) => grid.appendChild(el('div', 'dow', d)));
    const first = new Date(calDate.getFullYear(), calDate.getMonth(), 1);
    const today = new Date();
    for (let i = 0; i < 42; i++) {
      const d = new Date(first);
      d.setDate(1 - first.getDay() + i);
      const cls = 'day' + (d.getMonth() !== calDate.getMonth() ? ' other' : '') +
        (d.toDateString() === today.toDateString() ? ' today' : '');
      grid.appendChild(el('div', cls, String(d.getDate())));
      if (d.getMonth() > calDate.getMonth() && d.getDay() === 6) break;
    }
  }

  function renderNotifs() {
    const list = $('#notif-list');
    list.innerHTML = '';
    if (!state.notifications.length) {
      list.appendChild(el('div', 'notif-empty', 'No notifications'));
      return;
    }
    state.notifications.slice().reverse().forEach((n) => {
      const app = AppRegistry[n.appId] || { name: 'Shell', color: '#5e5c64', glyph: 'A' };
      const row = el('div', 'notif');
      const ic = el('div', 'n-ico', (app.glyph || 'A').replace(/<[^>]*>/g, '').slice(0, 1));
      ic.style.background = app.color;
      const body = el('div', '', `<div class="n-title">${n.title}</div><div class="n-body">${n.body}</div>`);
      const time = el('div', 'n-time', n.time);
      row.append(ic, body, time);
      list.appendChild(row);
    });
  }

  function notify(appId, title, body) {
    const time = new Date().toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
    state.notifications.push({ appId, title, body, time });
    renderNotifs();
    if (state.dnd) return;
    const app = AppRegistry[appId] || { name: 'Shell', color: '#5e5c64', glyph: 'A' };
    const b = el('div', 'banner');
    const ic = el('div', 'n-ico', (app.glyph || 'A').replace(/<[^>]*>/g, '').slice(0, 1));
    ic.style.background = app.color;
    b.appendChild(ic);
    b.appendChild(el('div', '', `<div class="n-title">${title}</div><div class="n-body">${body}</div>`));
    b.onclick = () => { dismiss(); togglePopover('#calendar-popover'); };
    $('#banners').appendChild(b);
    const dismiss = () => {
      b.classList.add('out');
      setTimeout(() => b.remove(), 200);
    };
    setTimeout(dismiss, 4500);
  }

  /* ---------------- Quick settings ---------------- */
  const qsState = { wifi: true, bt: false, dark: true, air: false };
  function renderQs() {
    const set = (id, on, sub) => {
      const t = $(id);
      t.classList.toggle('on', on);
      if (sub !== undefined) t.querySelector('.qs-sub').textContent = sub;
    };
    set('#qs-wifi', qsState.wifi, qsState.wifi ? 'Connected' : 'Off');
    set('#qs-bt', qsState.bt, qsState.bt ? 'On' : 'Off');
    set('#qs-dark', qsState.dark, qsState.dark ? 'On' : 'Off');
    set('#qs-air', qsState.air, qsState.air ? 'On' : 'Off');
    $('#st-wifi').style.opacity = qsState.wifi ? 1 : 0.35;
  }
  const qsBind = (id, key, sub) => {
    $(id).onclick = () => {
      qsState[key] = !qsState[key];
      if (key === 'dark') setDark(qsState.dark);
      renderQs();
    };
  };
  function setDark(on) {
    qsState.dark = on;
    document.body.classList.toggle('light', !on);
    renderQs();
  }

  function sliderFill(input) {
    const pct = ((input.value - input.min) / (input.max - input.min)) * 100;
    input.style.setProperty('--fill', pct + '%');
    return pct;
  }

  /* ---------------- Modal ---------------- */
  function modal(title, text, buttons) {
    const scrim = $('#modal-scrim');
    const box = $('#modal-box');
    box.innerHTML = `<h2>${title}</h2><p>${text}</p>`;
    const row = el('div', 'btn-row');
    buttons.forEach(([label, cls, cb]) => {
      const b = el('button', 'btn ' + cls, label);
      b.onclick = () => { scrim.classList.add('hidden'); if (cb) cb(); };
      row.appendChild(b);
    });
    box.appendChild(row);
    scrim.classList.remove('hidden');
  }

  /* ---------------- Shell facade for apps ---------------- */
  const shellApiFacade = {
    notify,
    openApp,
    setDark,
    setDnd: (on) => { state.dnd = on; $('#dnd-toggle').checked = on; },
    info: {},
  };

  /* ---------------- Events ---------------- */
  $('#activities-btn').onclick = () => setOverview(!state.overviewOpen);
  $('#clock-btn').onclick = (e) => { e.stopPropagation(); renderCalendar(); renderNotifs(); togglePopover('#calendar-popover'); };
  $('#status-btn').onclick = (e) => { e.stopPropagation(); togglePopover('#quicksettings'); };
  $('#cal-prev').onclick = () => { calDate.setMonth(calDate.getMonth() - 1); renderCalendar(); };
  $('#cal-next').onclick = () => { calDate.setMonth(calDate.getMonth() + 1); renderCalendar(); };
  $('#dnd-toggle').onchange = (e) => { state.dnd = e.target.checked; };
  $('#clear-notifs').onclick = () => { state.notifications = []; renderNotifs(); };
  $('#qs-settings-btn').onclick = () => { closePopovers(); openApp('settings'); };
  $('#qs-power-btn').onclick = (e) => { e.stopPropagation(); togglePopover('#power-menu'); };

  document.querySelectorAll('.pm-item').forEach((b) => {
    b.onclick = async () => {
      const act = b.dataset.act;
      closePopovers();
      if (act === 'quit') {
        modal('Exit Adwaita Shell?', 'The shell will close and you will return to Windows.',
          [['Cancel', '', null], ['Exit Shell', 'danger', () => window.shellAPI.power('quit')]]);
      } else if (act === 'suspend' || act === 'lock') {
        modal(
          act === 'lock' ? 'Lock screen?' : 'Suspend?',
          act === 'lock' ? 'The Windows session will be locked.' : 'The system will attempt to sleep.',
          [['Cancel', '', null], [act === 'lock' ? 'Lock' : 'Suspend', 'accent', () => window.shellAPI.power(act)]]
        );
      } else {
        await window.shellAPI.power(act);
      }
    };
  });

  $('#search').addEventListener('input', (e) => {
    const v = e.target.value.trim();
    setGrid(true);
    renderGrid(v);
  });
  $('#search').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      const first = gridEl.querySelector('.app-grid-item');
      if (first) first.click();
    }
  });

  document.addEventListener('pointerdown', (e) => {
    if (!e.target.closest('.popover, .tb-btn')) closePopovers();
    if (!e.target.closest('#modal-box')) $('#modal-scrim').classList.add('hidden');
  });

  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      if (state.gridOpen && state.overviewOpen) { setGrid(false); return; }
      if (state.overviewOpen) { setOverview(false); return; }
      closePopovers();
      $('#modal-scrim').classList.add('hidden');
    }
    if (e.ctrlKey && e.altKey && e.key === 'ArrowRight') switchWs(state.activeWs + 1 < state.workspaces.length ? state.activeWs + 1 : state.activeWs);
    if (e.ctrlKey && e.altKey && e.key === 'ArrowLeft') switchWs(state.activeWs - 1);
    if (e.ctrlKey && e.altKey && e.shiftKey && (e.key === 'ArrowRight' || e.key === 'ArrowLeft') && state.focused) {
      const t = state.activeWs + (e.key === 'ArrowRight' ? 1 : -1);
      moveWindowToWs(state.focused, t);
    }
    // Ctrl+Space toggles overview (Super is reserved by Windows).
    if (e.ctrlKey && e.code === 'Space') { e.preventDefault(); setOverview(!state.overviewOpen); }
  });

  /* ---------------- Boot ---------------- */
  async function boot() {
    try {
      shellApiFacade.info = await window.shellAPI.info();
      state.windowsApps = await window.shellAPI.listApps();
    } catch { /* non-electron preview */ }
    renderDock();
    renderOverview();
    renderCalendar();
    renderNotifs();
    renderQs();
    ['#vol-slider', '#bright-slider'].forEach((id) => {
      const s = $(id);
      sliderFill(s);
      s.addEventListener('input', () => {
        sliderFill(s);
        if (id === '#bright-slider') {
          $('#wallpaper').style.filter = `brightness(${0.4 + (s.value / 100) * 0.8})`;
        }
      });
    });
    qsBind('#qs-wifi', 'wifi'); qsBind('#qs-bt', 'bt');
    qsBind('#qs-dark', 'dark'); qsBind('#qs-air', 'air');

    // Seed demo state: one open window + staggered welcome notifications.
    openApp('files');
    setTimeout(() => notify('software', 'Updates available', '3 app updates are ready to install'), 1800);
    setTimeout(() => notify('files', 'Backup complete', 'Your Documents folder was backed up'), 5200);
    setTimeout(() => notify('settings', 'Welcome to Adwaita Shell', 'Click Activities or press Ctrl+Space for the overview'), 8600);
  }

  boot();
})();
