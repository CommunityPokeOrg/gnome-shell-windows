/* Adwaita Shell — built-in app registry.
   Each app defines a tile color, a glyph (letter or short text), whether it is
   pinned to the dock, and a render(el, shell) function that fills the window
   content area. `shell` exposes { notify, openApp, info }. */

(function () {
  const el = (tag, cls, html) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (html !== undefined) n.innerHTML = html;
    return n;
  };

  const esc = (s) => s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  /* ---------------- Files ---------------- */
  function renderFiles(root) {
    const places = ['Home', 'Desktop', 'Documents', 'Downloads', 'Pictures', 'Music', 'Videos'];
    const folders = [
      ['Desktop', '#62a0ea'], ['Documents', '#62a0ea'], ['Downloads', '#62a0ea'],
      ['Pictures', '#62a0ea'], ['Music', '#62a0ea'], ['Videos', '#62a0ea'],
      ['Projects', '#62a0ea'],
      ['notes.txt', '#8b8b8b'], ['wallpaper.png', '#c061cb'], ['budget.xlsx', '#33d17a'],
    ];
    const pane = el('div', 'files-pane');
    const side = el('div', 'files-side');
    places.forEach((p, i) => side.appendChild(el('div', 'fs-item' + (i === 0 ? ' sel' : ''), esc(p))));
    const main = el('div', 'files-main');
    folders.forEach(([name, color]) => {
      const card = el('div', 'file-card');
      card.appendChild(el('div', 'f-ico', '')).style.background = color;
      card.lastChild.style.borderRadius = '6px';
      card.appendChild(el('div', 'f-name', esc(name)));
      main.appendChild(card);
    });
    pane.appendChild(side); pane.appendChild(main);
    root.appendChild(pane);
  }

  /* ---------------- Terminal ---------------- */
  function renderTerminal(root, shell) {
    const pane = el('div', 'term-pane');
    root.appendChild(pane);
    const dir = '~';
    const print = (text, cls) => {
      pane.appendChild(el('div', 't-line' + (cls ? ' ' + cls : ''), text));
      pane.scrollTop = pane.scrollHeight;
    };
    const promptHtml = () =>
      `<span class="t-prompt">${esc(shell.info.username || 'user')}@${esc(shell.info.hostname || 'windows')}</span> <span class="t-dir">${dir}</span> $ `;
    const newPrompt = () => {
      const line = el('div', 't-line');
      line.innerHTML = promptHtml();
      const input = el('input', 'term-input');
      input.setAttribute('spellcheck', 'false');
      line.appendChild(input);
      pane.appendChild(line);
      pane.scrollTop = pane.scrollHeight;
      input.focus();
      pane.onclick = () => input.focus();
      input.addEventListener('keydown', (e) => {
        if (e.key !== 'Enter') return;
        const cmd = input.value.trim();
        input.disabled = true;
        input.parentNode.innerHTML = promptHtml() + esc(cmd);
        run(cmd);
      });
    };
    const run = (cmd) => {
      const [c, ...args] = cmd.split(/\s+/);
      switch (c) {
        case '': break;
        case 'help':
          print('Commands: help, ls, neofetch, echo, date, whoami, open &lt;app&gt;, notify &lt;msg&gt;, clear');
          break;
        case 'ls':
          print('Desktop  Documents  Downloads  Pictures  Music  Videos  Projects  notes.txt  wallpaper.png');
          break;
        case 'echo': print(esc(args.join(' '))); break;
        case 'date': print(new Date().toString()); break;
        case 'whoami': print(esc(shell.info.username || 'user')); break;
        case 'clear': pane.innerHTML = ''; break;
        case 'neofetch':
          print(`<span class="t-prompt">       ▄▄▄        </span> ${esc(shell.info.username || 'user')}@${esc(shell.info.hostname || 'windows')}`);
          print(`<span class="t-prompt">     ▄█████▄      </span> ─────────────────────`);
          print(`<span class="t-prompt">    ███   ███     </span> OS: Adwaita Shell (Windows ${esc(shell.info.platform)})`);
          print(`<span class="t-prompt">    ███   ███     </span> Shell: adwaita-shell 0.1.0`);
          print(`<span class="t-prompt">     ▀█████▀      </span> DE: GNOME-inspired`);
          print(`<span class="t-prompt">       ▀▀▀        </span> Runtime: Electron ${esc(shell.info.electron || '')}`);
          break;
        case 'open': {
          const target = args.join(' ').toLowerCase();
          const appId = Object.keys(AppRegistry).find(
            (k) => k === target || AppRegistry[k].name.toLowerCase() === target
          );
          if (appId) { print(`Opening ${AppRegistry[appId].name}…`); shell.openApp(appId); }
          else print(`open: no such app: ${esc(args.join(' '))}`);
          break;
        }
        case 'notify':
          shell.notify('terminal', 'Terminal', args.join(' ') || 'Hello from the terminal');
          print('Notification sent.');
          break;
        default:
          print(`${esc(c)}: command not found. Try 'help'.`);
      }
      newPrompt();
    };
    print('Adwaita Shell terminal — type <b>help</b> to get started.');
    newPrompt();
  }

  /* ---------------- Text Editor ---------------- */
  function renderEditor(root) {
    const pane = el('div', 'editor-pane');
    const bar = el('div', 'editor-toolbar');
    ['Open', 'Save', 'B', 'I', 'U'].forEach((t) => bar.appendChild(el('button', '', t)));
    const ta = el('textarea');
    ta.value = 'Welcome to Text Editor.\n\nThis is a live, editable document — type away.\n';
    pane.appendChild(bar); pane.appendChild(ta);
    root.appendChild(pane);
  }

  /* ---------------- Calculator ---------------- */
  function renderCalc(root) {
    const pane = el('div', 'calc-pane');
    const disp = el('div', 'calc-disp', '0');
    const grid = el('div', 'calc-grid');
    pane.appendChild(disp); pane.appendChild(grid);
    root.appendChild(pane);
    let expr = '';
    // Minimal recursive-descent arithmetic evaluator (no eval — CSP-safe).
    const evaluate = (s) => {
      let i = 0;
      const peek = () => s[i];
      const parseExpr = () => {
        let v = parseTerm();
        while (peek() === '+' || peek() === '-') v = s[i++] === '+' ? v + parseTerm() : v - parseTerm();
        return v;
      };
      const parseTerm = () => {
        let v = parseFactor();
        while (peek() === '*' || peek() === '/') v = s[i++] === '*' ? v * parseFactor() : v / parseFactor();
        return v;
      };
      const parseFactor = () => {
        if (peek() === '-') { i++; return -parseFactor(); }
        if (peek() === '(') { i++; const v = parseExpr(); if (peek() === ')') i++; return v; }
        const m = /^[0-9]*\.?[0-9]+/.exec(s.slice(i));
        if (!m) throw new Error('syntax');
        i += m[0].length;
        return parseFloat(m[0]);
      };
      const v = parseExpr();
      if (i !== s.length || !isFinite(v)) throw new Error('syntax');
      return v;
    };
    const keys = ['C', '(', ')', '÷', '7', '8', '9', '×', '4', '5', '6', '−', '1', '2', '3', '+', '0', '.', '⌫', '='];
    const sym = { '÷': '/', '×': '*', '−': '-' };
    keys.forEach((k) => {
      const b = el('button', '0123456789.'.includes(k) ? '' : (k === '=' ? 'eq' : 'op'), k);
      b.onclick = () => {
        if (k === 'C') expr = '';
        else if (k === '⌫') expr = expr.slice(0, -1);
        else if (k === '=') {
          try { expr = String(+evaluate(expr).toPrecision(12)); }
          catch { expr = ''; disp.textContent = 'Error'; return; }
        } else expr += sym[k] || k;
        disp.textContent = expr || '0';
      };
      grid.appendChild(b);
    });
  }

  /* ---------------- Settings ---------------- */
  function renderSettings(root, shell) {
    const pane = el('div', 'set-pane');
    const side = el('div', 'set-side');
    ['Wi-Fi', 'Bluetooth', 'Appearance', 'Notifications', 'Displays', 'Power', 'About'].forEach((s, i) =>
      side.appendChild(el('div', 's-item' + (i === 2 ? ' sel' : ''), s)));
    const main = el('div', 'set-main');
    main.appendChild(el('h3', '', 'Appearance'));
    const mkRow = (label, sub, on, cb) => {
      const r = el('div', 'set-row');
      const l = el('div', '', `<div>${label}</div><div class="sub">${sub}</div>`);
      const t = el('button', 'toggle' + (on ? ' on' : ''));
      t.onclick = () => { t.classList.toggle('on'); if (cb) cb(t.classList.contains('on')); };
      r.appendChild(l); r.appendChild(t);
      return r;
    };
    main.appendChild(mkRow('Dark style', 'Use dark colors for applications and shell', true, (on) => shell.setDark(on)));
    main.appendChild(mkRow('Animations', 'Interface animation effects', true));
    main.appendChild(mkRow('Auto-hide the dock', 'Hide the dock when a window approaches it', false));
    main.appendChild(el('h3', '', 'Notifications'));
    main.appendChild(mkRow('Do Not Disturb', 'Suppress notification banners', false, (on) => shell.setDnd(on)));
    main.appendChild(mkRow('Show banners in overview', '', true));
    pane.appendChild(side); pane.appendChild(main);
    root.appendChild(pane);
  }

  /* ---------------- Web ---------------- */
  function renderWeb(root) {
    const pane = el('div', 'web-pane');
    const bar = el('div', 'web-bar');
    bar.appendChild(el('div', 'url', '🔒 adwaita://start'));
    const home = el('div', 'web-home');
    home.appendChild(el('h1', '', 'Web'));
    home.appendChild(el('div', 'web-search', 'Search the web or type an address'));
    pane.appendChild(bar); pane.appendChild(home);
    root.appendChild(pane);
  }

  /* ---------------- Photos ---------------- */
  function renderPhotos(root) {
    const pane = el('div', 'photos-pane');
    const hues = [214, 280, 340, 20, 60, 140, 180, 250, 10, 90, 160, 300];
    hues.forEach((h, i) => {
      const p = el('div', 'photo');
      p.style.background = `linear-gradient(${120 + i * 25}deg, hsl(${h},55%,45%), hsl(${(h + 40) % 360},60%,30%))`;
      pane.appendChild(p);
    });
    root.appendChild(pane);
  }

  /* ---------------- Music ---------------- */
  function renderMusic(root) {
    const pane = el('div', 'music-pane');
    pane.appendChild(el('div', 'music-art', '♪'));
    pane.appendChild(el('div', 'music-title', 'Midnight Drive'));
    pane.appendChild(el('div', 'music-sub', 'Synthwave Essentials — Neon Pulse'));
    const ctl = el('div', 'music-ctl');
    ['⏮', '▶', '⏭'].forEach((t, i) => {
      const b = el('button', i === 1 ? 'play' : '', t);
      b.onclick = () => { b.textContent = b.textContent === '▶' ? '⏸' : '▶'; };
      ctl.appendChild(b);
    });
    pane.appendChild(ctl);
    root.appendChild(pane);
  }

  /* ---------------- Software ---------------- */
  function renderSoftware(root) {
    const pane = el('div', 'sw-pane');
    pane.appendChild(el('div', 'sw-hero', 'Featured: Adwaita Shell'));
    const apps = [
      ['Files', '#62a0ea', 'Browse and manage your files'],
      ['Terminal', '#241f31', 'Command-line interface'],
      ['Text Editor', '#3584e4', 'Simple text editing'],
      ['Calculator', '#e66100', 'Arithmetic made easy'],
      ['Music', '#9141ac', 'Play your audio collection'],
      ['Photos', '#c01c28', 'View and organize pictures'],
    ];
    apps.forEach(([n, c, d]) => {
      const r = el('div', 'sw-row');
      const t = el('div', 'tile', n[0]); t.style.background = c;
      const m = el('div', 'meta', `<div class="n">${n}</div><div class="d">${d}</div>`);
      const b = el('button', 'inst', 'Installed');
      r.appendChild(t); r.appendChild(m); r.appendChild(b);
      pane.appendChild(r);
    });
    root.appendChild(pane);
  }

  /* ---------------- Registry ---------------- */
  window.AppRegistry = {
    files:     { name: 'Files',       color: '#62a0ea', glyph: 'F',  fav: true,  render: renderFiles },
    terminal:  { name: 'Terminal',    color: '#241f31', glyph: '&gt;_', fav: true, render: renderTerminal },
    editor:    { name: 'Text Editor', color: '#3584e4', glyph: 'T',  fav: true,  render: renderEditor },
    web:       { name: 'Web',         color: '#1c71d8', glyph: 'W',  fav: true,  render: renderWeb },
    calc:      { name: 'Calculator',  color: '#e66100', glyph: 'C',  fav: true,  render: renderCalc },
    settings:  { name: 'Settings',    color: '#77767b', glyph: '⚙',  fav: true,  render: renderSettings },
    photos:    { name: 'Photos',      color: '#c01c28', glyph: 'P',  fav: false, render: renderPhotos },
    music:     { name: 'Music',       color: '#9141ac', glyph: 'M',  fav: false, render: renderMusic },
    software:  { name: 'Software',    color: '#33a17d', glyph: 'S',  fav: false, render: renderSoftware },
  };
})();
