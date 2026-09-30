// Adwaita Shell — Electron main process.
// Creates a fullscreen, frameless shell window and exposes a small IPC
// surface (system info, installed Windows apps, app launching, power).

const { app, BrowserWindow, ipcMain, shell, screen } = require('electron');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { exec } = require('child_process');

let shellWindow = null;

function createShell() {
  const primary = screen.getPrimaryDisplay();
  const { width, height } = primary.bounds;

  shellWindow = new BrowserWindow({
    x: primary.bounds.x,
    y: primary.bounds.y,
    width,
    height,
    frame: false,
    fullscreen: true,
    autoHideMenuBar: true,
    backgroundColor: '#1e1e1e',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  shellWindow.loadFile(path.join(__dirname, 'renderer', 'index.html'));

  // Esc is handled in-page; allow quitting via the power menu (ipc) or Alt+F4.
  shellWindow.on('closed', () => {
    shellWindow = null;
  });
}

// --- Installed-app discovery -------------------------------------------------
// Scans the Windows Start Menu shortcut folders for .lnk files so the app
// grid can launch real applications. Returns [] on non-Windows platforms.
function scanStartMenu() {
  const roots = [
    process.env.APPDATA &&
      path.join(process.env.APPDATA, 'Microsoft', 'Windows', 'Start Menu', 'Programs'),
    'C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs',
  ].filter(Boolean);

  const found = new Map();
  const skip = new Set(['uninstall', 'help', 'documentation', 'readme', 'license']);
  const IGNORE = new Set(['desktop.ini']);

  const walk = (dir, depth) => {
    if (depth > 3) return;
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const e of entries) {
      const full = path.join(dir, e.name);
      if (e.isDirectory()) {
        walk(full, depth + 1);
      } else if (e.name.toLowerCase().endsWith('.lnk') && !IGNORE.has(e.name.toLowerCase())) {
        const name = e.name.replace(/\.lnk$/i, '');
        if (skip.has(name.toLowerCase().split(' ')[0])) continue;
        if (!found.has(name)) found.set(name, full);
      }
    }
  };

  roots.forEach((r) => walk(r, 0));
  return [...found.entries()]
    .map(([name, lnk]) => ({ name, path: lnk }))
    .sort((a, b) => a.name.localeCompare(b.name));
}

// --- IPC -------------------------------------------------------------------
ipcMain.handle('shell:info', () => ({
  platform: process.platform,
  hostname: os.hostname(),
  username: os.userInfo().username,
  electron: process.versions.electron,
}));

ipcMain.handle('shell:list-apps', () => scanStartMenu());

ipcMain.handle('shell:launch', async (_evt, payload) => {
  if (!payload || !payload.path) return { ok: false };
  try {
    // shell.openPath resolves .lnk shortcuts correctly on Windows.
    const err = await shell.openPath(payload.path);
    return err ? { ok: false, error: err } : { ok: true };
  } catch (e) {
    return { ok: false, error: String(e) };
  }
});

ipcMain.handle('shell:power', (_evt, action) => {
  switch (action) {
    case 'quit':
      app.quit();
      return { ok: true };
    case 'minimize':
      if (shellWindow) shellWindow.minimize();
      return { ok: true };
    case 'lock':
      // Lock the workstation underneath the shell.
      if (process.platform === 'win32') exec('rundll32.exe user32.dll,LockWorkStation');
      return { ok: true };
    case 'suspend':
      if (process.platform === 'win32') exec('rundll32.exe powrprof.dll,SetSuspendState 0,1,0');
      return { ok: true };
    default:
      return { ok: false };
  }
});

ipcMain.handle('shell:devtools', () => {
  if (shellWindow) shellWindow.webContents.toggleDevTools();
});

app.whenReady().then(() => {
  createShell();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createShell();
  });
});

app.on('window-all-closed', () => {
  app.quit();
});
