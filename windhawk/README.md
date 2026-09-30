# Adwaita Shell — Windhawk mod

A C++ port of the [Adwaita Shell](../README.md) Electron prototype as a single
[Windhawk](https://windhawk.net/) mod for Windows. The mod is injected into
`explorer.exe` and draws a GNOME-style shell as topmost layered windows — no
Electron runtime required.

## Feature set

- **Top bar** — Activities button, focused-app label, centered clock, and a
  status area (Wi-Fi / volume / battery glyphs).
- **Activities overview** — full-screen dimmed stage with a search field, a
  live window spread rendered with real DWM thumbnails (previews stay live),
  a workspace strip, and the dock. Toggle with `Ctrl+Space` or the
  **Activities** button.
- **App grid** — real applications discovered from the Windows Start Menu
  (`.lnk` shortcuts); typing searches, `Enter` launches the top match.
- **Dock** — floating Dash with pinned apps and running windows (running apps
  show a dot). Click focuses or launches.
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
  notification list; shell events (workspace switches, app launches) appear
  as banners and are collected there.
- **Light & dark** — the Dark Style toggle re-skins every shell surface live.

Optionally hides the Windows taskbar while the shell is running (on by
default); it is restored when the mod is unloaded or the setting is turned
off.

## Install

1. Install [Windhawk](https://windhawk.net/) (v1.4 or newer, 64-bit Windows
   10/11).
2. Open the Windhawk **Settings → Advanced** page and enable the mod editor
   (or install the mod manually — below).
3. Create a new mod and paste the contents of
   [`adwaita-shell.wh.cpp`](adwaita-shell.wh.cpp) into the editor, or copy the
   file into Windhawk's custom mods folder:
   `%ProgramFiles%\Windhawk\Engine\Mods\` is *not* the right place — instead,
   in the Windhawk UI choose **Mods → bottom → "Create new mod"**, paste the
   source, and click **Compile Mod** → **Save** → enable it.
4. The shell appears immediately: a top bar across the primary monitor, and
   (by default) the Windows taskbar hidden.

To revert to the normal Windows shell at any time, disable the mod in
Windhawk — every change is cleaned up on unload.

## Settings

| Setting | Default | Purpose |
| --- | --- | --- |
| Hide the Windows taskbar | on | Keeps `Shell_TrayWnd`/secondary trays hidden via `ShowWindow`/`SetWindowPos` hooks. |
| Show the dock on the desktop | on | Floats the Dash on the desktop; off = overview only (pure GNOME behavior). |
| Dark style | off | Starts in the dark Adwaita variant. |
| Top bar height | 40 | Pixels, 28–56. |
| Overview hotkey | `ctrl+space` | `ctrl`/`alt`/`shift`/`win` + a key, e.g. `ctrl+alt+o`. |
| Pinned dock apps | _(empty)_ | Semicolon-separated `.lnk` paths or Start Menu app names. |

## Controls

| Action | How |
| --- | --- |
| Open/close overview | `Ctrl+Space` or click **Activities** |
| Search apps | Type in the overview; `Enter` launches top result |
| Show app grid | Grid icon in the dock, or any search text |
| Switch workspace | Workspace strip or `Ctrl+Alt+Left`/`Right` |
| Move window to workspace | `Ctrl+Alt+Shift+Left`/`Right` or drag its preview onto a tile |
| Close a window | `X` on its preview in the overview |
| Close overview/popovers | `Esc` or click the dimmed backdrop |
| Exit the shell | Quick settings → power → **Exit shell** |

## How it works

- Single-file mod (`adwaita-shell.wh.cpp`) following the windhawk-mods
  conventions: `// ==WindhawkMod==` metadata, a `WindhawkModReadme` block,
  declarative `WindhawkModSettings`, and the `Wh_ModInit` / `Wh_ModUninit` /
  `Wh_ModSettingsChanged` entry points.
- All UI runs on a dedicated thread inside `explorer.exe`; windows are plain
  layered topmost `HWND`s drawn with GDI (no assets needed).
- Window previews are real `DwmRegisterThumbnail` destinations, so they are
  live and hardware-accelerated.
- App discovery walks the user + common `Start Menu\Programs` folders for
  `.lnk` files; icons resolve through `IShellLink`/`SHGetFileInfo`; launching
  uses `ShellExecute`.
- Workspaces are managed internally (windows are hidden/shown per workspace,
  GNOME-style) so no undocumented `IVirtualDesktopManagerInternal` is needed.
- `Wh_SetFunctionHook` is applied to `ShowWindow` and `SetWindowPos` so the
  native taskbar stays hidden while the shell is active.

## Limitations

- Wi-Fi / Bluetooth / Airplane tiles deep-link into Windows Settings; toggling
  radios requires a medium-IL-escaping API that explorer cannot use.
- The notification center collects shell-generated events; intercepting WinRT
  toasts is out of scope.
- Shell UI is primary-monitor only.
- The brightness slider applies a gamma dim (works everywhere); per-monitor
  backlight control is not attempted.

## Building outside Windhawk (verification only)

Windhawk compiles mods itself with its bundled Clang. To sanity-check the
source locally, any MinGW-w64 Clang targeting `x86_64-windows-gnu` works:

```powershell
x86_64-w64-mingw32-clang++ -shared -std=c++20 -DUNICODE -D_UNICODE `
    -include windhawk.h `              # Wh_* declarations (see below)
    adwaita-shell.wh.cpp -o adwaita-shell.dll `
    -ldwmapi -lgdi32 -lole32 -loleaut32 -lpowrprof -lshlwapi -lshell32 -luuid
```

`windhawk.h` is provided by the Windhawk mod compiler; for an offline check
declare the `Wh_*` prototypes you use (see the wiki:
<https://github.com/ramensoftware/windhawk/wiki>).
