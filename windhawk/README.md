# Windhawk port — Adwaita Shell as native explorer.exe mods

This directory reimplements the Electron prototype's shell surfaces as
native C++ [Windhawk](https://windhawk.net) mods that run inside (or next
to) `explorer.exe` — no Electron runtime required. Each mod is a single
self-contained `.wh.cpp` translation unit, following the standard Windhawk
mod format: a `==WindhawkMod==` metadata header, a `==WindhawkModSettings==`
block so options are editable in the Windhawk UI, and `Wh_ModInit()` /
`Wh_ModUninit()` / `Wh_ModSettingsChanged()` entry points.

The Electron prototype stays untouched; these mods are an alternative,
native implementation of the same design.

Two implementation styles live here side by side:

* **Focused per-feature mods** (`adwaita-*.wh.cpp`, except
  `adwaita-shell`) — small mods that each re-create one shell behavior on
  top of the native Windows shell: the clock, the Activities trigger,
  dynamic workspaces, and quick-settings routing. Enable any subset.
* **`adwaita-shell.wh.cpp`** — a single all-in-one mod that replaces the
  shell chrome outright: GNOME top bar, Activities overview with live DWM
  thumbnails, app grid, dock, workspaces, quick settings, calendar, and
  light/dark themes drawn as layered windows.

## Focused mods

| Mod | File | Target | Covers (Electron feature) |
| --- | --- | --- | --- |
| **Adwaita Clock** | `adwaita-clock.wh.cpp` | `explorer.exe` | Centered top-bar clock — renders `Wed Sep 30  9:41 AM`-style text in the taskbar clock (single line by default) |
| **Adwaita Activities** | `adwaita-activities.wh.cpp` | `explorer.exe` | Activities overview trigger — top-left hot corner + `Ctrl+Space` hotkey that open Task View / Start / Search |
| **Adwaita Workspaces** | `adwaita-workspaces.wh.cpp` | `explorer.exe` | Dynamic workspaces — always one empty trailing desktop, empty middle desktops removed; `Ctrl+Alt+←/→` switch, `Ctrl+Alt+Shift+←/→` move window |
| **Adwaita Quick Settings** | `adwaita-quick-settings.wh.cpp` | `explorer.exe` | Quick settings + calendar/notifications — clock click → `Win+N`, status icons click → `Win+A`, optional top-right hot corner |

### What each mod does

**adwaita-clock** — hooks `GetTimeFormatEx`/`GetDateFormatEx` in
`kernelbase.dll` and substitutes GNOME-style picture formats whenever the
call comes from the system tray clock. On Windows 11 the caller is detected
via `ClockSystemTrayIconDataModel::RefreshIcon` symbol hooks
(`SystemTray.dll` / `Taskbar.View.dll` / `ExplorerExtensions.dll`), with a
return-address module check as fallback; on Windows 10 via `ClockButton`
symbol hooks in `explorer.exe`. Tooltips are detected and left alone.

**adwaita-activities** — a worker thread inside explorer polls the cursor
position (50 ms) and fires the configured action after a configurable dwell
in the chosen corner, like GNOME's top-left Activities hot spot. Actions are
sent with `SendInput`: `Win+Tab` (Task View = overview + workspace strip),
`Win` (Start = app grid) or `Win+S` (search). A configurable hotkey
(`Ctrl+Space` by default, matching the prototype) does the same.

**adwaita-workspaces** — drives the real virtual desktop APIs
(`IVirtualDesktopManagerInternal` via `IServiceProvider` on the immersive
shell, plus the public `IVirtualDesktopManager`) from a background thread.
The internal interface is versioned by Windows build — both the IID and the
vtable slots move — so the mod probes the known manager IIDs (VD.ahk's
build table), reads the build number from `twinui.pcshell.dll`, and calls
through per-build vtable slots with the optional leading `HMONITOR`
argument. `SetWinEventHook` (foreground/show/hide/destroy) plus a light
1 s topology poll keep the workspace list normalized to GNOME rules, and
`RegisterHotKey` provides the directional shortcuts.

**adwaita-quick-settings** — subclasses `Shell_TrayWnd` and
`Shell_SecondaryTrayWnd` with
`WindhawkUtils::SetWindowSubclassFromAnyThread` and hit-tests clicks against
`TrayNotifyWnd` / `TrayClockWClass`, routing the clock to `Win+N` and the
status icons to `Win+A`. On Windows 11 the native taskbar already behaves
this way; the mod normalizes Windows 10 and adds the optional corner.

## `adwaita-shell.wh.cpp` (monolithic recreation)

A C++ port of the whole prototype as a single mod, injected into
`explorer.exe`, drawing a GNOME-style shell as topmost layered windows.

- **Top bar** — Activities button, focused-app label, centered clock, and a
  status area (Wi-Fi / volume / battery glyphs).
- **Activities overview** — full-screen dimmed stage with a search field, a
  live window spread rendered with real DWM thumbnails, a workspace strip,
  and the dock. Toggle with `Ctrl+Space` or the **Activities** button.
- **App grid** — real applications discovered from the Windows Start Menu
  (`.lnk` shortcuts); typing searches, `Enter` launches the top match.
- **Dock** — floating Dash with pinned apps and running windows (running
  apps show a dot). Click focuses or launches.
- **Dynamic workspaces** — a `+` workspace appears when the last one is
  occupied; empty middle workspaces collapse. Switch from the strip or with
  `Ctrl+Alt+Left`/`Right`; move the focused window with
  `Ctrl+Alt+Shift+Left`/`Right`, or drag a window preview onto a workspace
  tile.
- **Quick settings** — Dark Style (applies the Windows dark app theme), Do
  Not Disturb, Wi-Fi / Bluetooth / Airplane shortcuts to their Settings
  pages, a real master-volume slider, a brightness gamma slider, battery
  readout, and a power menu (Lock / Suspend / Show desktop / Exit shell).
- **Calendar & notifications** — click the clock for a calendar plus a
  notification list; shell events appear as banners and are collected there.
- **Light & dark** — the Dark Style toggle re-skins every shell surface
  live.

Optionally hides the Windows taskbar while the shell is running (on by
default); it is restored when the mod is unloaded or the setting is turned
off.

| Setting | Default | Purpose |
| --- | --- | --- |
| Hide the Windows taskbar | on | Keeps `Shell_TrayWnd`/secondary trays hidden via `ShowWindow`/`SetWindowPos` hooks. |
| Show the dock on the desktop | on | Floats the Dash on the desktop; off = overview only (pure GNOME behavior). |
| Dark style | off | Starts in the dark Adwaita variant. |
| Top bar height | 40 | Pixels, 28–56. |
| Overview hotkey | `ctrl+space` | `ctrl`/`alt`/`shift`/`win` + a key. |
| Pinned dock apps | _(empty)_ | Semicolon-separated `.lnk` paths or Start Menu app names. |

Internals: all UI runs on a dedicated thread inside `explorer.exe`;
windows are plain layered topmost `HWND`s drawn with GDI; previews are real
`DwmRegisterThumbnail` destinations; app discovery walks the Start Menu for
`.lnk` files and launches via `ShellExecute`; workspaces are managed
internally (windows hidden/shown per workspace), so no undocumented
virtual-desktop interfaces are needed; `Wh_SetFunctionHook` on
`ShowWindow`/`SetWindowPos` keeps the native taskbar hidden.

Limitations: radios deep-link into Windows Settings (toggling requires a
medium-IL-escaping API); the notification center collects shell-generated
events only; shell UI is primary-monitor only; the brightness slider applies
a gamma dim rather than backlight control.

## Installing

1. Install [Windhawk](https://windhawk.net) (tested with v1.7).
2. In the Windhawk UI, open the mod editor ("Create a new mod" /
   `EditorWorkspace`) and copy a `.wh.cpp` file in, or drop the file into
   `%ProgramData%\Windhawk\EditorWorkspace` and open it from the editor.
3. Compile and enable the mod. Restart `explorer.exe` if the mod doesn't
   attach immediately (`windhawk.exe -restart` also works).

All mods are independent — enable any subset, but don't run
`adwaita-shell` together with the focused mods that cover the same surface
(e.g. its own workspaces/hotkey and `adwaita-workspaces`/`adwaita-activities`
would both respond to the same shortcuts).

## Compatibility

Windows 10 and Windows 11 x64. Symbol-based hooks (clock internals,
`ClockButton`) rely on Microsoft's public symbol server, which Windhawk
downloads automatically. `adwaita-workspaces` needs the virtual desktop COM
interfaces — build-aware vtable dispatch covers Windows 10 (17763+), Server
2019/2022, and Windows 11 (22000 through 26100+).

## Not ported (by the focused mods)

Deliberately left out, since a `.wh.cpp` injected into explorer can't
sensibly host them: the built-in demo apps (Files, Terminal, Text Editor,
Calculator, …) and full Adwaita theming of the taskbar (that requires XAML
resource surgery, which upstream covers with dedicated styler mods). The
focused mods cover the shell *behavior*; `adwaita-shell` additionally
recreates the shell *chrome* (overview UI, app grid, dock, themes).

## Verifying / compiling a mod by hand

Windhawk compiles mods with its bundled clang in
`C:\Program Files\Windhawk\Compiler`:

```powershell
cd "C:\Program Files\Windhawk\Compiler"
.\bin\clang++.exe -x c++ -std=c++23 -target x86_64-w64-mingw32 `
  -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 `
  -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 `
  -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD -DWH_EDITING `
  -include windhawk_api.h `
  -fsyntax-only path\to\adwaita-clock.wh.cpp
```

`-DWH_EDITING` supplies the placeholder mod id so the file parses outside
the real engine; the engine defines the real `WH_MOD_ID` when it builds the
mod for injection.
