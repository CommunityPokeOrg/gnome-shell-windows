# Adwaita Shell — a GNOME-inspired desktop shell for Windows

A working prototype of a GNOME-style desktop environment rendered as a
frameless, fullscreen Electron app on Windows. It recreates the GNOME Shell
experience — top bar, Activities overview, app grid, dock, dynamic workspaces,
quick settings, and the calendar/notification popover — in the Adwaita visual
language.

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-3584e4)
![Runtime](https://img.shields.io/badge/runtime-Electron-2b2e3b)

> **Native port:** a C++ [Windhawk](https://windhawk.net/) mod that runs the
> same shell inside `explorer.exe` lives in [`windhawk/`](windhawk/README.md)
> — no Electron required.

## Features

- **Top bar** — Activities button, focused-app menu, centered clock, and a
  status area (network / volume / battery).
- **Activities overview** — search field, live window spread (scaled previews
  of open windows), and a workspace strip; press `Ctrl+Space` or click
  **Activities**. (The Super key is reserved by Windows.)
- **App grid / launcher** — nine built-in demo apps plus real programs
  discovered from the Windows Start Menu (`.lnk` shortcuts) that launch via
  the OS.
- **Dock** — floating Dash-style dock with pinned favorites, running-app
  dots, launch bounce animation, and hover tooltips. Click focuses,
  minimizes, or launches.
- **Workspaces** — dynamic virtual desktops: a `+` workspace appears when the
  last one has windows. Switch from the overview strip or with
  `Ctrl+Alt+←/→`; move the focused window with `Ctrl+Alt+Shift+←/→`, or drag
  a window thumbnail onto a workspace in the overview.
- **Quick settings** — Wi-Fi, Bluetooth, Dark Style, and Airplane toggles;
  volume and brightness sliders (brightness actually dims the wallpaper);
  battery readout and a power menu (Lock / Suspend / Minimize / Exit).
- **Calendar & notifications** — click the clock for a real calendar plus a
  notification tray with Do Not Disturb. Timed banners slide down from the
  top bar.
- **Demo apps** — Files, a working fake Terminal (`help`, `neofetch`,
  `open files`, `notify …`), Text Editor, a real Calculator, Settings
  (with a working Dark Style toggle), Web, Photos, Music, and Software —
  each in a draggable, minimizable, maximizable Adwaita window.
- **Light & dark** — the Dark Style quick-setting re-skins the whole shell
  (windows, popovers, dock) live.

## Run it

Requires Node.js 20+ on Windows 10/11.

```powershell
git clone https://github.com/CommunityPokeOrg/gnome-shell-windows
cd gnome-shell-windows
npm install
npm start
```

The shell opens fullscreen and frameless. Exit it via the status menu →
power button → **Exit Shell…**, or `Alt+F4`. `npm run dist` produces an NSIS
installer and a portable `.exe` via electron-builder.

## Controls

| Action | How |
| --- | --- |
| Open/close overview | `Ctrl+Space` or click **Activities** |
| Show app grid | Grid icon in the overview dash, or start typing |
| Switch workspace | Overview strip, or `Ctrl+Alt+←/→` |
| Move window to workspace | `Ctrl+Alt+Shift+←/→`, or drag its thumbnail |
| Close overview / popovers | `Esc` |
| Minimize / maximize | Titlebar buttons or double-click the titlebar |
| Quit the shell | Status menu → ⏻ → **Exit Shell…** |

## Architecture

```
main.js            Electron main process: frameless fullscreen window,
                   Start Menu .lnk discovery, app launching, power actions
preload.js         contextBridge: typed shellAPI surface, no nodeIntegration
renderer/
  index.html       Shell DOM skeleton (topbar, dock, overview, popovers)
  styles.css       Adwaita design tokens + every component's styling
  apps.js          Built-in app registry + per-app window content renderers
  app.js           Window manager, workspaces, overview, dock, notifications
```

The window manager is self-contained in `renderer/app.js`: windows are
absolutely-positioned DOM nodes inside the shell surface with drag, focus,
minimize/maximize, and per-workspace membership. The overview renders scaled
clones of live window contents, so previews stay current. Real Start Menu
apps are read by the main process (`fs.readdirSync` over the two Programs
folders) and launched with `shell.openPath`, keeping the renderer sandboxed.

## Roadmap ideas

- Real window thumbnail embedding (host actual HWNDs)
- Shell-integration notifications via WinRT toast APIs
- Persisted settings, custom wallpapers, multi-monitor support
