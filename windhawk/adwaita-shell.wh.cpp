// ==WindhawkMod==
// @id              adwaita-shell
// @name            Adwaita Shell
// @description     A GNOME-inspired desktop shell for Windows: top bar, Activities overview with live window previews, app grid, dock, dynamic workspaces, quick settings, calendar and notification popover
// @version         1.0
// @author          CommunityPoke
// @github          https://github.com/CommunityPokeOrg/gnome-shell-windows
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -ldwmapi -lgdi32 -lole32 -loleaut32 -lpowrprof -lshlwapi -lshell32 -luuid
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0,
// matching the parent project (CommunityPokeOrg/gnome-shell-windows).
//
// This mod reimplements the "Adwaita Shell" Electron prototype as a native
// Windhawk mod injected into explorer.exe. It draws its own UI as topmost
// layered windows: a GNOME-style top bar, an Activities overview with live
// DWM window thumbnails, a search-driven app grid fed from the real Start
// Menu, a Dash-style dock, internal dynamic workspaces (windows are hidden
// and shown per workspace), a quick-settings panel (dark style, do not
// disturb, volume, brightness gamma, power menu) and a calendar /
// notification-center popover.

// ==WindhawkModReadme==
/*
# Adwaita Shell

A GNOME-style desktop shell for Windows, as a single Windhawk mod injected
into `explorer.exe`. Port of the Electron prototype in this repository.

## Features

- **Top bar** — Activities button, focused-app label, centered clock, status
  area (Wi-Fi / volume / battery) drawn in the Adwaita visual language.
- **Activities overview** — full-screen dimmed stage with a search field, a
  live window spread (real DWM thumbnails — previews stay live), a workspace
  strip, and the dock. Toggle with `Ctrl+Space` or the Activities button.
- **App grid** — apps discovered from the real Windows Start Menu (`.lnk`
  shortcuts); typing in the overview searches them, Enter launches the top
  match.
- **Dock** — floating Dash with pinned favorites and running apps; running
  apps get a dot. Click focuses or launches.
- **Workspaces** — dynamic workspaces, GNOME-style: a `+` workspace appears
  when the last one is occupied, empty middle workspaces collapse. Switch
  from the strip or `Ctrl+Alt+Left/Right`; move the focused window with
  `Ctrl+Alt+Shift+Left/Right`, or drag a window preview onto a workspace.
- **Quick settings** — Dark Style, Do Not Disturb (Focus Assist via toast
  suppression), Wi-Fi/Bluetooth/Airplane rows (open the matching Settings
  pages), volume slider (real master volume), brightness slider (gamma dim),
  battery readout, and a power menu (Lock / Suspend / Show desktop / Exit).
- **Calendar & notifications** — click the clock for a real calendar plus a
  notification list with Do Not Disturb; shell events (workspace switches,
  launches) appear as banners and are collected in the list.
- **Light & dark** — the Dark Style toggle re-skins every surface live and
  applies the Windows dark app theme.

The Windows taskbar can optionally be hidden while the shell runs (enabled by
default); it is restored when the mod unloads.

## Controls

| Action | How |
| --- | --- |
| Open/close overview | `Ctrl+Space` or click **Activities** |
| Search apps / windows | Type in the overview |
| Launch top search result | `Enter` |
| Show app grid | Grid icon or any search text in the overview |
| Switch workspace | Workspace strip, or `Ctrl+Alt+Left/Right` |
| Move focused window | `Ctrl+Alt+Shift+Left/Right`, or drag a preview onto a workspace |
| Close a window | `X` on its preview in the overview |
| Close overview / popovers | `Esc` or click the dimmed backdrop |
| Quit the shell | Quick settings -> power -> **Exit shell** |

## Notes and limitations

- Windows on non-active workspaces are hidden via `ShowWindow`, matching
  GNOME's per-workspace window isolation. They stay running.
- Wi-Fi, Bluetooth and Airplane tiles are launchers into Windows Settings;
  radios cannot be toggled from a medium-IL explorer process.
- The notification center collects shell-generated events. Intercepting
  WinRT toast notifications is out of scope; use the action center for those.
- Overview hotkey is configurable in the mod settings (default `ctrl+space`).

## Source structure

Single mod file, organized in sections: settings -> theme -> helpers ->
app discovery -> workspaces -> top bar -> overview -> dock -> popovers ->
banner -> UI thread -> taskbar hooks -> Windhawk entry points.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- hideTaskbar: true
  $name: Hide the Windows taskbar
  $description: >-
    Hides the native taskbar while the shell is active (Shell_TrayWnd and the
    secondary taskbars). It is restored when the mod is unloaded or the
    option is turned off.
- dockOnDesktop: true
  $name: Show the dock on the desktop
  $description: >-
    GNOME only shows the dash inside the overview. When enabled, the dock also
    floats on the desktop while the overview is closed.
- darkStyle: false
  $name: Dark style
  $description: >-
    Starts the shell in the dark Adwaita style. The quick-settings toggle
    updates this at runtime and applies the Windows dark app theme.
- topBarHeight: 40
  $name: Top bar height
  $description: Height of the GNOME top bar in pixels (28-56).
- overviewHotkey: ctrl+space
  $name: Overview hotkey
  $description: >-
    Keyboard shortcut that toggles the Activities overview, e.g.
    "ctrl+space", "ctrl+alt+o", "win+space" (modifiers: ctrl, alt, shift, win).
- pinnedApps: ""
  $name: Pinned dock apps
  $description: >-
    Apps pinned to the dock before any running apps. Semicolon-separated;
    each entry is either a full path to a .lnk file or a Start Menu app name
    (matched case-insensitively). Example:
    "Notepad; C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Firefox.lnk"
*/
// ==/WindhawkModSettings==

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <powrprof.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wlanapi.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

#define WM_APP_SHELL_SETTINGS (WM_APP + 1)

#ifndef DWM_TNP_SOURCECLIENTAREA_ONLY
#define DWM_TNP_SOURCECLIENTAREA_ONLY 0x00000010
#endif

static void RegisterHotkeys();
static void SetTrayVisible(bool visible);

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

struct ShellSettings {
    bool hideTaskbar = true;
    bool dockOnDesktop = true;
    bool darkStyle = false;
    int topBarHeight = 40;
    std::wstring overviewHotkey = L"ctrl+space";
    std::wstring pinnedApps;
};

static ShellSettings g_settings;
static volatile LONG g_shellActive = 0;  // hides taskbar while set

static void LoadSettings() {
    g_settings.hideTaskbar = Wh_GetIntSetting(L"hideTaskbar") != 0;
    g_settings.dockOnDesktop = Wh_GetIntSetting(L"dockOnDesktop") != 0;
    g_settings.darkStyle = Wh_GetIntSetting(L"darkStyle") != 0;
    g_settings.topBarHeight =
        std::clamp(Wh_GetIntSetting(L"topBarHeight"), 28, 56);

    if (PCWSTR hotkey = Wh_GetStringSetting(L"overviewHotkey")) {
        g_settings.overviewHotkey = hotkey;
        Wh_FreeStringSetting(hotkey);
    }
    if (PCWSTR pinned = Wh_GetStringSetting(L"pinnedApps")) {
        g_settings.pinnedApps = pinned;
        Wh_FreeStringSetting(pinned);
    }
}

// ---------------------------------------------------------------------------
// Theme (Adwaita palette)
// ---------------------------------------------------------------------------

struct Theme {
    COLORREF barBg, barFg;
    COLORREF stageBg;      // overview dim color
    COLORREF card, cardHover, cardBorder;
    COLORREF text, textDim;
    COLORREF accent;       // GNOME blue 3584e4
    COLORREF dockBg;
    COLORREF danger;       // close button
    bool dark;
};

static Theme g_theme;

static void ApplyTheme() {
    if (g_settings.darkStyle) {
        g_theme = {RGB(0x1e, 0x1e, 0x1e), RGB(0xff, 0xff, 0xff),
                   RGB(0x1b, 0x1b, 0x1b),
                   RGB(0x2d, 0x2d, 0x2d), RGB(0x3a, 0x3a, 0x3a),
                   RGB(0x4f, 0x4f, 0x4f), RGB(0xff, 0xff, 0xff),
                   RGB(0xa0, 0xa0, 0xa0), RGB(0x35, 0x84, 0xe4),
                   RGB(0x16, 0x16, 0x16), RGB(0xe0, 0x1b, 0x24), true};
    } else {
        g_theme = {RGB(0xf6, 0xf5, 0xf4), RGB(0x2e, 0x34, 0x36),
                   RGB(0x30, 0x30, 0x30),
                   RGB(0xff, 0xff, 0xff), RGB(0xec, 0xeb, 0xe9),
                   RGB(0xd6, 0xd3, 0xd0), RGB(0x2e, 0x34, 0x36),
                   RGB(0x77, 0x76, 0x7b), RGB(0x35, 0x84, 0xe4),
                   RGB(0xdd, 0xdc, 0xda), RGB(0xe0, 0x1b, 0x24), false};
    }
}

// ---------------------------------------------------------------------------
// Small GDI helpers
// ---------------------------------------------------------------------------

static HFONT FontOf(int size, int weight = FW_NORMAL) {
    return CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void FillRound(HDC dc, RECT r, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

static void FrameRound(HDC dc, RECT r, int radius, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void TextAt(HDC dc, RECT r, PCWSTR text, COLORREF color, HFONT font,
                   UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                                DT_END_ELLIPSIS) {
    int oldMode = SetBkMode(dc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(dc, color);
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    DrawTextW(dc, text, -1, &r, flags);
    if (oldFont)
        SelectObject(dc, oldFont);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
}

static bool PtIn(const RECT& r, POINT p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

// Simple glyphs drawn with GDI so the shell needs no image assets.
static void DrawGlyph(HDC dc, RECT r, int glyph, COLORREF color) {
    enum { GLYPH_WIFI, GLYPH_VOLUME, GLYPH_BATTERY, GLYPH_GRID, GLYPH_SEARCH,
           GLYPH_POWER, GLYPH_MOON, GLYPH_BELL, GLYPH_PLANE, GLYPH_BT,
           GLYPH_SUN };
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HBRUSH fillBrush = CreateSolidBrush(color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int w = std::min(r.right - r.left, r.bottom - r.top) / 2;
    switch (glyph) {
    case GLYPH_WIFI:
        for (int i = 1; i <= 3; i++) {
            Arc(dc, cx - w * i, cy - w * i + w / 2, cx + w * i,
                cy + w * i + w / 2, cx - w * i / 2, cy - w * i / 2 + w / 2,
                cx + w * i / 2, cy - w * i / 2 + w / 2);
        }
        break;
    case GLYPH_VOLUME:
        {
            SelectObject(dc, fillBrush);
            POINT tri[] = {{cx - w, cy}, {cx - w / 3, cy - w / 3},
                           {cx - w / 3, cy + w / 3}};
            Polygon(dc, tri, 3);
            Rectangle(dc, cx - w - w / 3, cy - w / 3, cx - w, cy + w / 3);
            SelectObject(dc, oldBrush);
            Arc(dc, cx - w, cy - w, cx + w * 2, cy + w, cx + w / 4,
                cy - w / 2, cx + w / 4, cy + w / 2);
        }
        break;
    case GLYPH_BATTERY:
        RoundRect(dc, cx - w, cy - w / 2, cx + w, cy + w / 2, 3, 3);
        Rectangle(dc, cx + w, cy - w / 6, cx + w + w / 5, cy + w / 6);
        break;
    case GLYPH_GRID:
        for (int i = -1; i <= 1; i++)
            for (int j = -1; j <= 1; j++)
                Rectangle(dc, cx + i * w - w / 5, cy + j * w - w / 5,
                          cx + i * w + w / 5, cy + j * w + w / 5);
        break;
    case GLYPH_SEARCH:
        Ellipse(dc, cx - w / 2, cy - w / 2, cx + w / 4, cy + w / 4);
        MoveToEx(dc, cx + w / 4, cy + w / 4, nullptr);
        LineTo(dc, cx + w / 2, cy + w / 2);
        break;
    case GLYPH_POWER:
        Arc(dc, cx - w / 2, cy - w / 2 + 2, cx + w / 2, cy + w / 2 + 2,
            cx + w / 4, cy - w / 2 + 2, cx - w / 4, cy - w / 2 + 2);
        MoveToEx(dc, cx, cy - w / 2, nullptr);
        LineTo(dc, cx, cy + 1);
        break;
    case GLYPH_MOON:
        Arc(dc, cx - w / 2, cy - w / 2, cx + w / 2, cy + w / 2, cx + w / 3,
            cy - w / 3, cx + w / 3, cy + w / 3);
        break;
    case GLYPH_BELL:
        Arc(dc, cx - w / 2, cy - w / 2, cx + w / 2, cy + w / 2, cx + w / 3,
            cy + w / 3, cx - w / 3, cy + w / 3);
        MoveToEx(dc, cx - w / 3, cy + w / 3, nullptr);
        LineTo(dc, cx + w / 3, cy + w / 3);
        break;
    case GLYPH_PLANE:
        MoveToEx(dc, cx, cy - w, nullptr);
        LineTo(dc, cx + w / 3, cy + w / 2);
        LineTo(dc, cx, cy + w / 3);
        LineTo(dc, cx - w / 3, cy + w / 2);
        LineTo(dc, cx, cy - w);
        break;
    case GLYPH_BT:
        MoveToEx(dc, cx - w / 3, cy - w / 2, nullptr);
        LineTo(dc, cx + w / 3, cy + w / 6);
        LineTo(dc, cx - w / 3, cy + w / 2);
        MoveToEx(dc, cx + w / 3, cy - w / 2, nullptr);
        LineTo(dc, cx - w / 3, cy + w / 6);
        LineTo(dc, cx + w / 3, cy + w / 2);
        MoveToEx(dc, cx, cy - w, nullptr);
        LineTo(dc, cx, cy + w);
        break;
    case GLYPH_SUN:
        Ellipse(dc, cx - w / 3, cy - w / 3, cx + w / 3, cy + w / 3);
        for (int i = 0; i < 8; i++) {
            double a = i * 3.14159265 / 4;
            MoveToEx(dc, (int)(cx + cos(a) * w * 0.55),
                     (int)(cy + sin(a) * w * 0.55), nullptr);
            LineTo(dc, (int)(cx + cos(a) * w * 0.9),
                   (int)(cy + sin(a) * w * 0.9));
        }
        break;
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(fillBrush);
    DeleteObject(pen);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static const wchar_t* kClsTopBar = L"AdwaitaShell.TopBar";
static const wchar_t* kClsOverview = L"AdwaitaShell.Overview";
static const wchar_t* kClsDock = L"AdwaitaShell.Dock";
static const wchar_t* kClsPopover = L"AdwaitaShell.Popover";
static const wchar_t* kClsBanner = L"AdwaitaShell.Banner";

enum PopoverKind { POP_NONE = 0, POP_QUICK, POP_CALENDAR };

struct AppEntry {
    std::wstring name;
    std::wstring lnkPath;
    HICON icon = nullptr;
};

struct Notification {
    std::wstring title;
    std::wstring body;
    SYSTEMTIME when;
};

static HWND g_topBar, g_overview, g_dock, g_popover, g_banner;
static HANDLE g_uiThread;
static DWORD g_uiTid;
static bool g_running;

static std::vector<AppEntry> g_apps;      // Start Menu apps
static std::vector<AppEntry> g_pinned;    // resolved pinned apps
static std::vector<Notification> g_notes;
static bool g_dnd;

static std::vector<std::vector<HWND>> g_workspaces = {{}};
static int g_activeWs = 0;
static std::vector<HWND> g_windows;       // windows on the active workspace
static HWND g_foreground;

static bool g_overviewOpen, g_gridMode;
static std::wstring g_search;
static PopoverKind g_popKind = POP_NONE;

static IAudioEndpointVolume* g_volume;    // cached master volume

#define IDT_CLOCK 1
#define IDT_POLL 2
#define IDT_BANNER 3
#define IDT_THUMB 4

#define IDC_SEARCH 1001

#define HOTKEY_OVERVIEW 1
#define HOTKEY_WS_PREV 2
#define HOTKEY_WS_NEXT 3
#define HOTKEY_MOVE_PREV 4
#define HOTKEY_MOVE_NEXT 5

// ---------------------------------------------------------------------------
// Window enumeration / focus helpers
// ---------------------------------------------------------------------------

static bool IsShellWindow(HWND hwnd) {
    return hwnd == g_topBar || hwnd == g_overview || hwnd == g_dock ||
           hwnd == g_popover || hwnd == g_banner;
}

static bool IsAppWindow(HWND hwnd) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsShellWindow(hwnd))
        return false;
    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
        return false;
    if (GetWindow(hwnd, GW_OWNER) && !(ex & WS_EX_APPWINDOW))
        return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked,
                                      sizeof(cloaked))) &&
        cloaked)
        return false;
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
        !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd") ||
        !wcscmp(cls, L"MultitaskingViewFrame"))
        return false;
    if (!(ex & WS_EX_APPWINDOW) && GetWindowTextLengthW(hwnd) == 0)
        return false;
    return true;
}

static BOOL CALLBACK EnumAppWindowsProc(HWND hwnd, LPARAM lp) {
    if (IsAppWindow(hwnd))
        reinterpret_cast<std::vector<HWND>*>(lp)->push_back(hwnd);
    return TRUE;
}

static std::vector<HWND> EnumerateAppWindows() {
    std::vector<HWND> out;
    EnumWindows(EnumAppWindowsProc, reinterpret_cast<LPARAM>(&out));
    return out;
}

static void FocusAppWindow(HWND hwnd) {
    if (IsIconic(hwnd))
        ShowWindowAsync(hwnd, SW_RESTORE);
    HWND fg = GetForegroundWindow();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD tid = GetCurrentThreadId();
    AttachThreadInput(tid, fgTid, TRUE);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    AttachThreadInput(tid, fgTid, FALSE);
}

static HICON WindowIcon(HWND hwnd) {
    HICON icon = nullptr;
    SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 100,
                        reinterpret_cast<PDWORD_PTR>(&icon));
    if (!icon)
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100,
                            reinterpret_cast<PDWORD_PTR>(&icon));
    if (!icon)
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100,
                            reinterpret_cast<PDWORD_PTR>(&icon));
    if (!icon)
        icon = reinterpret_cast<HICON>(
            GetClassLongPtrW(hwnd, GCLP_HICONSM));
    if (!icon)
        icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    if (!icon)
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    return icon;
}

// ---------------------------------------------------------------------------
// Notifications (shell-generated) + banner
// ---------------------------------------------------------------------------

static void Notify(PCWSTR title, PCWSTR body) {
    Notification n;
    n.title = title;
    n.body = body ? body : L"";
    GetLocalTime(&n.when);
    g_notes.insert(g_notes.begin(), n);
    if (g_notes.size() > 50)
        g_notes.pop_back();
    if (!g_dnd && g_banner) {
        // The wndproc reads g_notes.front() when painting.
        ShowWindow(g_banner, SW_SHOWNA);
        SetWindowPos(g_banner, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetTimer(g_banner, IDT_BANNER, 4000, nullptr);
        InvalidateRect(g_banner, nullptr, TRUE);
    }
    if (g_popover && g_popKind == POP_CALENDAR)
        InvalidateRect(g_popover, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// System toggles / power
// ---------------------------------------------------------------------------

static void BroadcastImmersiveChange() {
    DWORD_PTR result;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
                        SMTO_ABORTIFHUNG, 200, &result);
}

static void SetDarkAppsTheme(bool dark) {
    HKEY key;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
        ERROR_SUCCESS) {
        DWORD v = dark ? 0 : 1;
        RegSetValueExW(key, L"AppsUseLightTheme", 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&v), sizeof(v));
        RegCloseKey(key);
        BroadcastImmersiveChange();
    }
}

static void SetDoNotDisturb(bool dnd) {
    g_dnd = dnd;
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\"
                        L"Notifications\\Settings",
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) == ERROR_SUCCESS) {
        DWORD v = dnd ? 0 : 1;
        RegSetValueExW(key, L"NOC_GLOBAL_SETTING_TOASTS_ENABLED", 0,
                       REG_DWORD, reinterpret_cast<const BYTE*>(&v),
                       sizeof(v));
        RegCloseKey(key);
    }
    if (g_popover && g_popKind == POP_CALENDAR)
        InvalidateRect(g_popover, nullptr, TRUE);
}

static void InitAudio() {
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator))))
        return;
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                    &device)) &&
        device) {
        device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                         reinterpret_cast<void**>(&g_volume));
        device->Release();
    }
    enumerator->Release();
}

static float GetMasterVolume() {
    float level = 0.f;
    if (g_volume)
        g_volume->GetMasterVolumeLevelScalar(&level);
    return level;
}

static void SetMasterVolume(float level) {
    if (g_volume)
        g_volume->SetMasterVolumeLevelScalar(
            std::clamp(level, 0.f, 1.f), nullptr);
}

// Dims the primary display via the gamma ramp (the "brightness" slider).
// Case-insensitive substring search (mingw lacks wcsistr).
static const wchar_t* wcsistr(const wchar_t* hay, const wchar_t* needle) {
    if (!*needle)
        return hay;
    for (; *hay; hay++) {
        const wchar_t* h = hay;
        const wchar_t* n = needle;
        while (*h && *n && towlower(*h) == towlower(*n)) {
            h++;
            n++;
        }
        if (!*n)
            return hay;
    }
    return nullptr;
}

static void ApplyBrightness(float v) {
    v = std::clamp(v, 0.35f, 1.0f);
    WORD ramp[3][256];
    for (int i = 0; i < 256; i++) {
        WORD scaled = static_cast<WORD>(std::min<int>(65535, i * 257 * v));
        ramp[0][i] = ramp[1][i] = ramp[2][i] = scaled;
    }
    HDC dc = GetDC(nullptr);
    SetDeviceGammaRamp(dc, ramp);
    ReleaseDC(nullptr, dc);
}

static bool WifiPresent() {
    HMODULE wlan = LoadLibraryW(L"wlanapi.dll");
    if (!wlan)
        return false;
    using WlanOpenHandle_t = DWORD(WINAPI*)(DWORD, PVOID, PDWORD, PHANDLE);
    using WlanEnumInterfaces_t =
        DWORD(WINAPI*)(HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST*);
    auto open = reinterpret_cast<WlanOpenHandle_t>(
        GetProcAddress(wlan, "WlanOpenHandle"));
    auto enumIf = reinterpret_cast<WlanEnumInterfaces_t>(
        GetProcAddress(wlan, "WlanEnumInterfaces"));
    bool present = false;
    HANDLE h;
    DWORD ver;
    if (open && enumIf && open(2, nullptr, &ver, &h) == ERROR_SUCCESS) {
        PWLAN_INTERFACE_INFO_LIST list = nullptr;
        if (enumIf(h, nullptr, &list) == ERROR_SUCCESS && list) {
            present = list->dwNumberOfItems > 0;
            using WlanFreeMemory_t = VOID(WINAPI*)(PVOID);
            auto freeMem = reinterpret_cast<WlanFreeMemory_t>(
                GetProcAddress(wlan, "WlanFreeMemory"));
            if (freeMem)
                freeMem(list);
        }
        using WlanCloseHandle_t = DWORD(WINAPI*)(HANDLE, PVOID);
        auto close = reinterpret_cast<WlanCloseHandle_t>(
            GetProcAddress(wlan, "WlanCloseHandle"));
        if (close)
            close(h, nullptr);
    }
    FreeLibrary(wlan);
    return present;
}

static void EnableShutdownPrivilege() {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return;
    LUID luid;
    if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &luid)) {
        TOKEN_PRIVILEGES tp{1, {{luid, SE_PRIVILEGE_ENABLED}}};
        AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
    }
    CloseHandle(token);
}

// ---------------------------------------------------------------------------
// App discovery (Start Menu .lnk scan, like the Electron prototype)
// ---------------------------------------------------------------------------

static void ScanLnkDir(const wchar_t* dir, int depth,
                       std::vector<AppEntry>& out) {
    if (depth > 3)
        return;
    wchar_t glob[MAX_PATH];
    swprintf(glob, MAX_PATH, L"%s\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(glob, &fd);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        wchar_t full[MAX_PATH];
        swprintf(full, MAX_PATH, L"%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanLnkDir(full, depth + 1, out);
            continue;
        }
        size_t len = wcslen(fd.cFileName);
        if (len < 5 || _wcsicmp(fd.cFileName + len - 4, L".lnk") ||
            !_wcsicmp(fd.cFileName, L"desktop.ini"))
            continue;
        std::wstring name(fd.cFileName, len - 4);
        std::wstring firstWord = name.substr(0, name.find(L' '));
        std::wstring lower = firstWord;
        for (auto& c : lower)
            c = towlower(c);
        if (lower == L"uninstall" || lower == L"help" ||
            lower == L"documentation" || lower == L"readme" ||
            lower == L"license")
            continue;
        bool dup = false;
        for (auto& e : out)
            if (!_wcsicmp(e.name.c_str(), name.c_str())) {
                dup = true;
                break;
            }
        if (!dup)
            out.push_back({name, full, nullptr});
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

// Resolves a .lnk to its target path for icon extraction.
static bool ResolveLnk(const std::wstring& lnk, std::wstring& target) {
    IShellLinkW* link = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_ALL,
                                   IID_IShellLinkW,
                                   reinterpret_cast<void**>(&link)))) {
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(
                IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            if (SUCCEEDED(file->Load(lnk.c_str(), STGM_READ))) {
                wchar_t path[MAX_PATH];
                WIN32_FIND_DATAW fd;
                if (SUCCEEDED(link->GetPath(path, MAX_PATH, &fd,
                                            SLGP_RAWPATH)) &&
                    path[0]) {
                    target = path;
                    ok = true;
                }
            }
            file->Release();
        }
        link->Release();
    }
    return ok;
}

static HICON AppIcon(const AppEntry& app) {
    std::wstring target;
    HICON icon = nullptr;
    if (ResolveLnk(app.lnkPath, target)) {
        SHFILEINFOW info{};
        if (SHGetFileInfoW(target.c_str(), 0, &info, sizeof(info),
                           SHGFI_ICON | SHGFI_LARGEICON))
            icon = info.hIcon;
    }
    if (!icon) {
        SHFILEINFOW info{};
        if (SHGetFileInfoW(app.lnkPath.c_str(), FILE_ATTRIBUTE_NORMAL, &info,
                           sizeof(info),
                           SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES))
            icon = info.hIcon;
    }
    if (!icon)
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    return icon;
}

static void ScanStartMenu() {
    g_apps.clear();
    wchar_t dir[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, dir)))
        ScanLnkDir(dir, 0, g_apps);
    if (SUCCEEDED(
            SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, dir)))
        ScanLnkDir(dir, 0, g_apps);
    std::sort(g_apps.begin(), g_apps.end(), [](const AppEntry& a,
                                               const AppEntry& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

static void ResolvePinnedApps() {
    g_pinned.clear();
    size_t start = 0;
    while (true) {
        size_t sep = g_settings.pinnedApps.find(L';', start);
        std::wstring item = g_settings.pinnedApps.substr(
            start, sep == std::wstring::npos ? sep : sep - start);
        // trim
        size_t a = item.find_first_not_of(L" \t");
        size_t b = item.find_last_not_of(L" \t");
        if (a != std::wstring::npos) {
            item = item.substr(a, b - a + 1);
            AppEntry entry;
            entry.name = item;
            if (item.size() > 4 &&
                !_wcsicmp(item.c_str() + item.size() - 4, L".lnk") &&
                GetFileAttributesW(item.c_str()) != INVALID_FILE_ATTRIBUTES) {
                entry.lnkPath = item;
                entry.name = item.substr(0, item.size() - 4);
                size_t slash = entry.name.find_last_of(L"\\/");
                if (slash != std::wstring::npos)
                    entry.name = entry.name.substr(slash + 1);
            } else {
                for (auto& app : g_apps)
                    if (_wcsicmp(app.name.c_str(), item.c_str()) == 0) {
                        entry = app;
                        break;
                    }
            }
            if (!entry.lnkPath.empty())
                g_pinned.push_back(entry);
        }
        if (sep == std::wstring::npos)
            break;
        start = sep + 1;
    }
    for (auto& app : g_pinned)
        app.icon = AppIcon(app);
}

static void LaunchApp(const AppEntry& app) {
    if (app.lnkPath.empty())
        return;
    INT_PTR r = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", app.lnkPath.c_str(), nullptr,
                      nullptr, SW_SHOWNORMAL));
    if (r > 32)
        Notify(app.name.c_str(), L"Launched");
}

// ---------------------------------------------------------------------------
// Workspaces (dynamic, internal: windows are hidden per workspace)
// ---------------------------------------------------------------------------

static bool IsInWorkspaceList(HWND hwnd) {
    for (auto& ws : g_workspaces)
        for (HWND w : ws)
            if (w == hwnd)
                return true;
    return false;
}

static void CompactWorkspaces() {
    for (auto& ws : g_workspaces)
        ws.erase(std::remove_if(ws.begin(), ws.end(),
                                [](HWND w) { return !IsWindow(w); }),
                 ws.end());
    // Remove empty workspaces except keep one trailing empty.
    for (int i = static_cast<int>(g_workspaces.size()) - 1; i >= 0; i--) {
        bool keep = (i == static_cast<int>(g_workspaces.size()) - 1) ||
                    !g_workspaces[i].empty();
        if (!keep) {
            g_workspaces.erase(g_workspaces.begin() + i);
            if (i <= g_activeWs)
                g_activeWs--;
        }
    }
    if (g_workspaces.empty())
        g_workspaces.push_back({});
    if (g_workspaces.back().size() > 0)
        g_workspaces.push_back({});  // trailing "+" workspace
    g_activeWs = std::clamp(g_activeWs, 0,
                            static_cast<int>(g_workspaces.size()) - 1);
}

static void ApplyWorkspaceVisibility() {
    for (size_t i = 0; i < g_workspaces.size(); i++) {
        bool active = static_cast<int>(i) == g_activeWs;
        for (HWND w : g_workspaces[i]) {
            if (!IsWindow(w))
                continue;
            if (active) {
                if (!IsWindowVisible(w))
                    ShowWindowAsync(w, SW_SHOWNA);
            } else if (IsWindowVisible(w)) {
                ShowWindowAsync(w, SW_HIDE);
            }
        }
    }
}

static void SwitchWorkspace(int index) {
    CompactWorkspaces();
    if (index < 0 || index >= static_cast<int>(g_workspaces.size()) ||
        index == g_activeWs)
        return;
    g_activeWs = index;
    ApplyWorkspaceVisibility();
    g_windows = g_workspaces[g_activeWs];
    wchar_t buf[32];
    swprintf(buf, 32, L"Workspace %d", g_activeWs + 1);
    Notify(buf, L"");
    if (g_overviewOpen)
        InvalidateRect(g_overview, nullptr, TRUE);
}

static void MoveFocusedWindow(int delta) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsAppWindow(hwnd))
        return;
    int to = g_activeWs + delta;
    CompactWorkspaces();
    if (to < 0 || to >= static_cast<int>(g_workspaces.size()))
        return;
    for (auto& ws : g_workspaces)
        ws.erase(std::remove(ws.begin(), ws.end(), hwnd), ws.end());
    g_workspaces[to].push_back(hwnd);
    SwitchWorkspace(to);
}

// Pull newly created windows into the active workspace.
static void RefreshWindows() {
    std::vector<HWND> current = EnumerateAppWindows();
    for (HWND w : current)
        if (!IsInWorkspaceList(w) && IsWindowVisible(w))
            g_workspaces[g_activeWs].push_back(w);
    CompactWorkspaces();
    g_windows = g_workspaces[g_activeWs];
    g_foreground = GetForegroundWindow();
}

// ---------------------------------------------------------------------------
// Hit regions (shared by overview, dock, popovers)
// ---------------------------------------------------------------------------

struct HitRegion {
    RECT rect;
    int kind;    // meaning depends on the window
    int index;   // window/app/workspace index, or sub-item id
};

// ---------------------------------------------------------------------------
// Overview
// ---------------------------------------------------------------------------

struct Thumb {
    HWND src;
    HTHUMBNAIL handle;
    RECT cell;
};

static std::vector<Thumb> g_thumbs;
static std::vector<HitRegion> g_overviewHits;
static int g_dragWindow = -1;  // index into g_windows while dragging
static POINT g_dragPt;
static bool g_dragMoved;

static void ClearThumbnails() {
    for (auto& t : g_thumbs)
        if (t.handle)
            DwmUnregisterThumbnail(t.handle);
    g_thumbs.clear();
}

// Layout: returns client rects for search, spread area, strip, dock strip.
static void OverviewLayout(RECT client, RECT& search, RECT& spread,
                           RECT& strip, RECT& dock) {
    search = {client.right / 2 - 220, 24, client.right / 2 + 220, 24 + 36};
    dock = {0, client.bottom - 96, client.right, client.bottom - 8};
    strip = {client.right - 92, search.bottom + 16,
             client.right - 12, dock.top - 8};
    spread = {40, search.bottom + 24, strip.left - 24, dock.top - 12};
}

static RECT SpreadCell(RECT spread, int count, int index, RECT cell) {
    // rows/cols chosen to best fill the spread area
    int cols = count <= 1 ? 1 : count <= 4 ? 2 : count <= 9 ? 3 : 4;
    int rows = (count + cols - 1) / cols;
    int cw = (spread.right - spread.left) / cols;
    int ch = (spread.bottom - spread.top) / rows;
    int r = index / cols, c = index % cols;
    cell.left = spread.left + c * cw + 12;
    cell.top = spread.top + r * ch + 12;
    cell.right = spread.left + (c + 1) * cw - 12;
    cell.bottom = spread.top + (r + 1) * ch - 30;
    return cell;
}

static void RebuildThumbnails(RECT client) {
    ClearThumbnails();
    if (!g_overviewOpen || g_gridMode)
        return;
    RECT search, spread, strip, dock;
    OverviewLayout(client, search, spread, strip, dock);
    int count = static_cast<int>(g_windows.size());
    for (int i = 0; i < count; i++) {
        Thumb t{};
        t.src = g_windows[i];
        SpreadCell(spread, count, i, t.cell);
        if (SUCCEEDED(DwmRegisterThumbnail(g_overview, t.src, &t.handle)) &&
            t.handle) {
            DWM_THUMBNAIL_PROPERTIES p{};
            p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE |
                        DWM_TNP_SOURCECLIENTAREA_ONLY;
            p.rcDestination = t.cell;
            p.fVisible = TRUE;
            p.fSourceClientAreaOnly = TRUE;
            DwmUpdateThumbnailProperties(t.handle, &p);
        }
        g_thumbs.push_back(t);
    }
}

enum HitKind {
    HK_NONE, HK_CLOSE_WIN, HK_SELECT_WIN, HK_WORKSPACE, HK_GRID_ICON,
    HK_GRID_APP, HK_DOCK_APP
};

static void ToggleOverview(bool open);

// ---------------------------------------------------------------------------
// Window procedures
// ---------------------------------------------------------------------------

static void PositionTopBar() {
    MONITORINFO mi{sizeof(mi)};
    HMONITOR mon = MonitorFromWindow(g_topBar, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfoW(mon, &mi);
    SetWindowPos(g_topBar, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left,
                 g_settings.topBarHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void PositionDock() {
    if (!g_dock)
        return;
    int itemW = 56, pad = 12;
    int count = static_cast<int>(g_pinned.size()) +
                static_cast<int>(g_windows.size());
    if (!count) {
        ShowWindow(g_dock, SW_HIDE);
        return;
    }
    int w = pad * 2 + count * itemW;
    int h = itemW + 16;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY),
                    &mi);
    int x = (mi.rcWork.left + mi.rcWork.right - w) / 2;
    int y = mi.rcWork.bottom - h - 12;
    SetWindowPos(g_dock, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE |
                     (g_settings.dockOnDesktop && !g_overviewOpen
                          ? SWP_SHOWWINDOW
                          : SWP_HIDEWINDOW));
    InvalidateRect(g_dock, nullptr, TRUE);
}

static void ClosePopover() {
    if (g_popover)
        ShowWindow(g_popover, SW_HIDE);
    g_popKind = POP_NONE;
}

static void ToggleOverview(bool open) {
    g_overviewOpen = open;
    if (!g_overview)
        return;
    if (open) {
        RefreshWindows();
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY),
                        &mi);
        SetWindowPos(g_overview, HWND_TOPMOST, mi.rcMonitor.left,
                     mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_SHOWWINDOW);
        SetForegroundWindow(g_overview);
        SetFocus(GetDlgItem(g_overview, IDC_SEARCH));
        RECT rc;
        GetClientRect(g_overview, &rc);
        RebuildThumbnails(rc);
        InvalidateRect(g_overview, nullptr, TRUE);
    } else {
        ClearThumbnails();
        ShowWindow(g_overview, SW_HIDE);
        g_gridMode = false;
        g_search.clear();
        if (HWND edit = GetDlgItem(g_overview, IDC_SEARCH))
            SetWindowTextW(edit, L"");
        if (g_foreground && IsWindow(g_foreground))
            FocusAppWindow(g_foreground);
    }
    PositionDock();
    InvalidateRect(g_topBar, nullptr, TRUE);
}

static void TogglePopover(PopoverKind kind, RECT anchor) {
    if (g_popKind == kind) {
        ClosePopover();
        return;
    }
    g_popKind = kind;
    int w = 360;
    int h = kind == POP_QUICK ? 300 : 380;
    int x = std::min(std::max<int>(anchor.right - w, 8),
                     GetSystemMetrics(SM_CXSCREEN) - w - 8);
    int y = anchor.bottom + 6;
    SetWindowPos(g_popover, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_popover, nullptr, TRUE);
}

// --- Top bar ---------------------------------------------------------------

static LRESULT CALLBACK TopBarProc(HWND hwnd, UINT msg, WPARAM wp,
                                   LPARAM lp) {
    enum Region { R_NONE, R_ACTIVITIES, R_CLOCK, R_STATUS };
    static HFONT font, fontBold;

    switch (msg) {
    case WM_CREATE:
        font = FontOf(15);
        fontBold = FontOf(15, FW_SEMIBOLD);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(g_theme.barBg);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        // Activities button
        RECT act = {4, 4, 104, rc.bottom - 4};
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);
        if (PtIn(act, cursor) || g_overviewOpen)
            FillRound(dc, act, 8, g_theme.cardHover);
        TextAt(dc, act, L"Activities", g_theme.barFg, fontBold,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Focused app label
        wchar_t title[128] = {};
        if (g_foreground && IsWindow(g_foreground))
            GetWindowTextW(g_foreground, title, 127);
        if (title[0]) {
            RECT label = {act.right + 12, 4, act.right + 300, rc.bottom - 4};
            TextAt(dc, label, title, g_theme.textDim, font);
        }

        // Centered clock
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t clock[64];
        swprintf(clock, 64, L"%s %d, %02d:%02d",
                 (const wchar_t*[]) {L"Jan", L"Feb", L"Mar", L"Apr", L"May",
                                     L"Jun", L"Jul", L"Aug", L"Sep", L"Oct",
                                     L"Nov", L"Dec"}[st.wMonth - 1],
                 st.wDay, st.wHour, st.wMinute);
        RECT clk = {rc.right / 2 - 120, 4, rc.right / 2 + 120,
                    rc.bottom - 4};
        TextAt(dc, clk, clock, g_theme.barFg, fontBold);

        // Status area (right side): glyphs + battery percent
        int sw = rc.bottom - 10;
        RECT status = {rc.right - 3 * (sw + 8) - 60, 5, rc.right - 8,
                       rc.bottom - 5};
        RECT wifiR = {status.left, status.top, status.left + sw,
                      status.bottom};
        RECT volR = {wifiR.right + 8, status.top, wifiR.right + 8 + sw,
                     status.bottom};
        RECT batR = {volR.right + 8, status.top, volR.right + 8 + sw,
                     status.bottom};
        DrawGlyph(dc, wifiR, 0, g_theme.barFg);   // wifi
        DrawGlyph(dc, volR, 1, g_theme.barFg);    // volume
        DrawGlyph(dc, batR, 2, g_theme.barFg);    // battery
        SYSTEM_POWER_STATUS sps;
        if (GetSystemPowerStatus(&sps) &&
            sps.BatteryLifePercent != 255 &&
            !(sps.BatteryFlag & 128)) {
            RECT pct = {batR.right + 4, status.top, batR.right + 44,
                        status.bottom};
            wchar_t b[8];
            swprintf(b, 8, L"%d%%", sps.BatteryLifePercent);
            TextAt(dc, pct, b, g_theme.barFg, font);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        int sw = rc.bottom - 10;
        RECT act = {4, 4, 104, rc.bottom - 4};
        RECT clk = {rc.right / 2 - 120, 4, rc.right / 2 + 120,
                    rc.bottom - 4};
        RECT status = {rc.right - 3 * (sw + 8) - 60, 5, rc.right - 8,
                       rc.bottom - 5};
        if (PtIn(act, p)) {
            ClosePopover();
            ToggleOverview(!g_overviewOpen);
        } else if (PtIn(clk, p)) {
            RECT anchor = clk;
            ClientToScreen(hwnd, reinterpret_cast<POINT*>(&anchor));
            ClientToScreen(hwnd, reinterpret_cast<POINT*>(&anchor) + 1);
            ToggleOverview(false);
            TogglePopover(POP_CALENDAR, anchor);
        } else if (PtIn(status, p)) {
            RECT anchor = status;
            ClientToScreen(hwnd, reinterpret_cast<POINT*>(&anchor));
            ClientToScreen(hwnd, reinterpret_cast<POINT*>(&anchor) + 1);
            ToggleOverview(false);
            TogglePopover(POP_QUICK, anchor);
        } else if (g_popKind != POP_NONE) {
            ClosePopover();
        }
        return 0;
    }

    case WM_HOTKEY:
        switch (wp) {
        case HOTKEY_OVERVIEW:
            ToggleOverview(!g_overviewOpen);
            return 0;
        case HOTKEY_WS_PREV:
            SwitchWorkspace(g_activeWs - 1);
            return 0;
        case HOTKEY_WS_NEXT:
            SwitchWorkspace(g_activeWs + 1);
            return 0;
        case HOTKEY_MOVE_PREV:
            MoveFocusedWindow(-1);
            return 0;
        case HOTKEY_MOVE_NEXT:
            MoveFocusedWindow(+1);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wp == IDT_CLOCK)
            InvalidateRect(hwnd, nullptr, TRUE);
        else if (wp == IDT_POLL) {
            RefreshWindows();
            PositionDock();
            InvalidateRect(hwnd, nullptr, TRUE);
            if (g_overviewOpen)
                InvalidateRect(g_overview, nullptr, TRUE);
        }
        return 0;

    case WM_APP_SHELL_SETTINGS:
        // Runs on the UI thread so hotkeys stay thread-owned.
        UnregisterHotKey(hwnd, HOTKEY_OVERVIEW);
        UnregisterHotKey(hwnd, HOTKEY_WS_PREV);
        UnregisterHotKey(hwnd, HOTKEY_WS_NEXT);
        UnregisterHotKey(hwnd, HOTKEY_MOVE_PREV);
        UnregisterHotKey(hwnd, HOTKEY_MOVE_NEXT);
        LoadSettings();
        ApplyTheme();
        RegisterHotkeys();
        PositionTopBar();
        PositionDock();
        SetTrayVisible(g_settings.hideTaskbar ? false : true);
        InvalidateRect(hwnd, nullptr, TRUE);
        if (g_popover)
            InvalidateRect(g_popover, nullptr, TRUE);
        return 0;

    case WM_DESTROY:
        DeleteObject(font);
        DeleteObject(fontBold);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- Overview ---------------------------------------------------------------

// Indices into g_apps matching the current search text.
static std::vector<int> FilteredApps() {
    std::vector<int> out;
    for (size_t i = 0; i < g_apps.size(); i++) {
        if (g_search.empty() ||
            wcsistr(g_apps[i].name.c_str(), g_search.c_str()))
            out.push_back((int)i);
        if (out.size() >= 60)
            break;
    }
    return out;
}

static LRESULT CALLBACK SearchEditProc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
    WNDPROC orig = reinterpret_cast<WNDPROC>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        auto apps = FilteredApps();
        if (!apps.empty()) {
            LaunchApp(g_apps[apps[0]]);
            ToggleOverview(false);
        }
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
        SendMessageW(g_overview, WM_COMMAND, 1, 0);  // close
        return 0;
    }
    if (msg == WM_CHAR && wp == VK_ESCAPE)
        return 0;
    return CallWindowProcW(orig, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK OverviewProc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp) {
    static HFONT font, fontBig, fontTitle;
    static HBRUSH editBrush;

    switch (msg) {
    case WM_CREATE: {
        font = FontOf(14);
        fontBig = FontOf(18);
        fontTitle = FontOf(13, FW_SEMIBOLD);
        editBrush = CreateSolidBrush(RGB(0x2d, 0x2d, 0x2d));
        HWND edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
            0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(IDC_SEARCH),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(edit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"Type to search"));
        SetWindowLongPtrW(edit, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(
                              SetWindowLongPtrW(edit, GWLP_WNDPROC,
                                                reinterpret_cast<LONG_PTR>(
                                                    SearchEditProc))));
        SetWindowFont(edit, FontOf(16), TRUE);
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT search, spread, strip, dock;
        OverviewLayout(rc, search, spread, strip, dock);
        if (HWND edit = GetDlgItem(hwnd, IDC_SEARCH))
            MoveWindow(edit, search.left, search.top,
                       search.right - search.left,
                       search.bottom - search.top, TRUE);
        RebuildThumbnails(rc);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, RGB(0xff, 0xff, 0xff));
        SetBkColor(dc, RGB(0x2d, 0x2d, 0x2d));
        return reinterpret_cast<LRESULT>(editBrush);
    }

    case WM_COMMAND: {
        if (wp == 1) {  // close request from the search edit
            ToggleOverview(false);
            return 0;
        }
        if (LOWORD(wp) == IDC_SEARCH && HIWORD(wp) == EN_CHANGE) {
            wchar_t buf[256];
            GetDlgItemTextW(hwnd, IDC_SEARCH, buf, 256);
            g_search = buf;
            g_gridMode = !g_search.empty();
            RECT rc;
            GetClientRect(hwnd, &rc);
            RebuildThumbnails(rc);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            ToggleOverview(false);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        // Regions were built during the last WM_PAINT.
        for (auto& h : g_overviewHits) {
            if (!PtIn(h.rect, p))
                continue;
            switch (h.kind) {
            case HK_CLOSE_WIN:
                if (h.index < (int)g_windows.size())
                    PostMessageW(g_windows[h.index], WM_CLOSE, 0, 0);
                break;
            case HK_SELECT_WIN: {
                RECT cell = h.rect;
                cell.bottom -= 22;  // shrink to the thumbnail itself
                if (PtIn(cell, p)) {
                    // Possible drag start; resolved as a click on mouse-up.
                    g_dragWindow = h.index;
                    g_dragPt = p;
                    g_dragMoved = false;
                    SetCapture(hwnd);
                } else if (h.index < (int)g_windows.size()) {
                    g_foreground = g_windows[h.index];
                    ToggleOverview(false);
                }
                break;
            }
            case HK_WORKSPACE:
                SwitchWorkspace(h.index);
                break;
            case HK_GRID_ICON:
                g_gridMode = true;
                g_search.clear();
                SetDlgItemTextW(hwnd, IDC_SEARCH, L"");
                InvalidateRect(hwnd, nullptr, TRUE);
                break;
            case HK_GRID_APP: {
                auto apps = FilteredApps();
                if (h.index < (int)apps.size()) {
                    LaunchApp(g_apps[apps[h.index]]);
                    ToggleOverview(false);
                }
                break;
            }
            case HK_DOCK_APP:
                if (h.index < (int)g_windows.size()) {
                    g_foreground = g_windows[h.index];
                    ToggleOverview(false);
                }
                break;
            }
            return 0;
        }
        // Clicked the backdrop: leave the grid, or close the overview.
        if (g_gridMode) {
            g_gridMode = false;
            g_search.clear();
            SetDlgItemTextW(hwnd, IDC_SEARCH, L"");
            RECT rc;
            GetClientRect(hwnd, &rc);
            RebuildThumbnails(rc);
            InvalidateRect(hwnd, nullptr, TRUE);
        } else {
            ToggleOverview(false);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_dragWindow >= 0) {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (abs(p.x - g_dragPt.x) + abs(p.y - g_dragPt.y) > 4)
                g_dragMoved = true;
            g_dragPt = p;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_dragWindow >= 0) {
            POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            bool moved = g_dragMoved;
            bool dropped = false;
            if (moved) {
                for (auto& h : g_overviewHits)
                    if (h.kind == HK_WORKSPACE && PtIn(h.rect, p) &&
                        h.index != g_activeWs &&
                        g_dragWindow < (int)g_windows.size()) {
                        HWND w = g_windows[g_dragWindow];
                        g_workspaces[g_activeWs].erase(
                            std::remove(g_workspaces[g_activeWs].begin(),
                                        g_workspaces[g_activeWs].end(), w),
                            g_workspaces[g_activeWs].end());
                        g_workspaces[h.index].push_back(w);
                        SwitchWorkspace(h.index);
                        dropped = true;
                        break;
                    }
            }
            if (!moved && !dropped &&
                g_dragWindow < (int)g_windows.size()) {
                // It was a click on the thumbnail: focus the window.
                g_foreground = g_windows[g_dragWindow];
                ToggleOverview(false);
            }
            g_dragWindow = -1;
            g_dragMoved = false;
            ReleaseCapture();
            RECT rc;
            GetClientRect(hwnd, &rc);
            RebuildThumbnails(rc);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;

    case WM_TIMER:
        if (wp == IDT_THUMB) {
            // Live thumbnails repaint themselves; refresh window list so
            // opened/closed windows appear while the overview is open.
            int before = (int)g_windows.size();
            RefreshWindows();
            if ((int)g_windows.size() != before) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                RebuildThumbnails(rc);
            }
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(g_theme.stageBg);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        g_overviewHits.clear();
        RECT search, spread, strip, dock;
        OverviewLayout(rc, search, spread, strip, dock);
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);

        if (!g_gridMode && g_windows.empty())
            TextAt(dc, spread, L"No open windows",
                   RGB(0xcc, 0xcc, 0xcc), fontBig);

        // Window spread: cells (thumbnails render via DWM), titles, close.
        if (!g_gridMode) {
            int count = (int)g_windows.size();
            for (int i = 0; i < count; i++) {
                RECT cell;
                SpreadCell(spread, count, i, cell);
                if (PtIn(cell, cursor))
                    FrameRound(dc, cell, 8, RGB(0xaa, 0xaa, 0xaa));
                // title bar strip
                RECT tr = {cell.left, cell.bottom, cell.right,
                           cell.bottom + 22};
                wchar_t title[96] = {};
                GetWindowTextW(g_windows[i], title, 95);
                if (HICON ic = WindowIcon(g_windows[i]))
                    DrawIconEx(dc, tr.left + 2, tr.top + 2, ic, 18, 18, 0,
                               nullptr, DI_NORMAL);
                tr.left += 24;
                TextAt(dc, tr, title, RGB(0xff, 0xff, 0xff), font,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                           DT_END_ELLIPSIS);
                // close button
                RECT xr = {cell.right - 24, cell.top - 4, cell.right - 4,
                           cell.top + 16};
                bool xhov = PtIn(xr, cursor);
                FillRound(dc, xr, 9,
                          xhov ? g_theme.danger : RGB(0x55, 0x55, 0x55));
                TextAt(dc, xr, L"X", RGB(0xff, 0xff, 0xff), font);
                g_overviewHits.push_back({xr, HK_CLOSE_WIN, i});
                RECT sel = cell;
                sel.bottom += 22;
                g_overviewHits.push_back({sel, HK_SELECT_WIN, i});
            }
        }

        // App grid.
        if (g_gridMode) {
            auto apps = FilteredApps();
            int cols = std::max(1L, (spread.right - spread.left) / 120);
            for (size_t i = 0; i < apps.size(); i++) {
                AppEntry& app = g_apps[apps[i]];
                int cw = (spread.right - spread.left) / cols;
                int r = (int)i / cols, c = (int)i % cols;
                RECT cell = {spread.left + c * cw + 8,
                             spread.top + r * 100 + 8,
                             spread.left + (c + 1) * cw - 8,
                             spread.top + r * 100 + 92};
                if (cell.bottom > spread.bottom)
                    break;
                if (PtIn(cell, cursor))
                    FillRound(dc, cell, 10, g_theme.cardHover);
                RECT ir = {cell.left + (cell.right - cell.left) / 2 - 24,
                           cell.top + 8,
                           cell.left + (cell.right - cell.left) / 2 + 24,
                           cell.top + 56};
                if (!app.icon)
                    app.icon = AppIcon(app);
                if (app.icon)
                    DrawIconEx(dc, ir.left, ir.top, app.icon, 48, 48, 0,
                               nullptr, DI_NORMAL);
                RECT nr = {cell.left + 4, cell.top + 58, cell.right - 4,
                           cell.bottom - 2};
                TextAt(dc, nr, app.name.c_str(),
                       RGB(0xff, 0xff, 0xff), font);
                g_overviewHits.push_back({cell, HK_GRID_APP, (int)i});
            }
        }

        // Workspace strip.
        CompactWorkspaces();
        int wsCount = (int)g_workspaces.size();
        int cellH = (int)std::min<long>(
            80, (strip.bottom - strip.top) / std::max(1, wsCount));
        for (int i = 0; i < wsCount; i++) {
            RECT cell = {strip.left, strip.top + i * cellH + 4, strip.right,
                         strip.top + (i + 1) * cellH - 4};
            bool active = i == g_activeWs;
            bool plus = i == wsCount - 1 && g_workspaces[i].empty();
            FillRound(dc, cell, 10,
                      active ? g_theme.accent
                             : PtIn(cell, cursor) ? g_theme.cardHover
                                                  : g_theme.card);
            if (plus) {
                TextAt(dc, cell, L"+", RGB(0xff, 0xff, 0xff), fontBig);
            } else {
                wchar_t num[8];
                swprintf(num, 8, L"%d", i + 1);
                TextAt(dc, cell, num, RGB(0xff, 0xff, 0xff), font);
                int dots = (int)g_workspaces[i].size();
                RECT dr = {cell.left + 8, cell.bottom - 12,
                           cell.left + 8 + dots * 10, cell.bottom - 6};
                for (int d = 0; d < dots && d < 6; d++) {
                    RECT dot = {dr.left + d * 10, dr.top, dr.left + d * 10 + 6,
                                dr.bottom};
                    FillRound(dc, dot, 6, RGB(0xff, 0xff, 0xff));
                }
            }
            g_overviewHits.push_back({cell, HK_WORKSPACE, i});
        }

        // Dock strip inside overview.
        FillRound(dc, dock, 14, g_theme.dockBg);
        {
            int itemW = 56;
            int x = dock.left + 12;
            for (size_t i = 0; i < g_pinned.size(); i++) {
                RECT cell = {x, dock.top + 8, x + itemW - 8,
                             dock.bottom - 8};
                if (PtIn(cell, cursor))
                    FillRound(dc, cell, 8, g_theme.cardHover);
                if (g_pinned[i].icon)
                    DrawIconEx(dc, cell.left + 6, cell.top + 8,
                               g_pinned[i].icon, 32, 32, 0, nullptr,
                               DI_NORMAL);
                x += itemW;
            }
            for (size_t i = 0; i < g_windows.size(); i++) {
                RECT cell = {x, dock.top + 8, x + itemW - 8,
                             dock.bottom - 8};
                if (PtIn(cell, cursor))
                    FillRound(dc, cell, 8, g_theme.cardHover);
                DrawIconEx(dc, cell.left + 6, cell.top + 8,
                           WindowIcon(g_windows[i]), 32, 32, 0, nullptr,
                           DI_NORMAL);
                // running dot
                RECT dot = {cell.left + (cell.right - cell.left) / 2 - 3,
                            cell.bottom - 10,
                            cell.left + (cell.right - cell.left) / 2 + 3,
                            cell.bottom - 4};
                FillRound(dc, dot, 6, g_theme.accent);
                g_overviewHits.push_back({cell, HK_DOCK_APP, (int)i});
                x += itemW;
            }
            // grid icon at the end
            RECT cell = {x, dock.top + 8, x + itemW - 8, dock.bottom - 8};
            if (PtIn(cell, cursor) || g_gridMode)
                FillRound(dc, cell, 8, g_theme.cardHover);
            RECT gr = {cell.left + 8, cell.top + 8, cell.right - 8,
                       cell.bottom - 8};
            DrawGlyph(dc, gr, 3, RGB(0xff, 0xff, 0xff));
            g_overviewHits.push_back({cell, HK_GRID_ICON, 0});
        }

        // dragged window marker
        if (g_dragWindow >= 0 && g_dragWindow < (int)g_windows.size()) {
            RECT dr = {g_dragPt.x - 60, g_dragPt.y - 40, g_dragPt.x + 60,
                       g_dragPt.y + 30};
            FillRound(dc, dr, 8, RGB(0x60, 0x60, 0x60));
            wchar_t title[64] = {};
            GetWindowTextW(g_windows[g_dragWindow], title, 63);
            TextAt(dc, dr, title, RGB(0xff, 0xff, 0xff), font);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        ClearThumbnails();
        DeleteObject(font);
        DeleteObject(fontBig);
        DeleteObject(fontTitle);
        DeleteObject(editBrush);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- Desktop dock ------------------------------------------------------------

static LRESULT CALLBACK DockProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HFONT font;
    switch (msg) {
    case WM_CREATE:
        font = FontOf(12);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(g_theme.dockBg);
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);
        int itemW = 56;
        int x = 12;
        for (auto& app : g_pinned) {
            RECT cell = {x, 8, x + itemW - 8, rc.bottom - 8};
            if (PtIn(cell, cursor))
                FillRound(dc, cell, 8, g_theme.cardHover);
            if (app.icon)
                DrawIconEx(dc, cell.left + 6, cell.top + 8, app.icon, 32, 32,
                           0, nullptr, DI_NORMAL);
            x += itemW;
        }
        for (HWND w : g_windows) {
            RECT cell = {x, 8, x + itemW - 8, rc.bottom - 8};
            if (PtIn(cell, cursor))
                FillRound(dc, cell, 8, g_theme.cardHover);
            DrawIconEx(dc, cell.left + 6, cell.top + 8, WindowIcon(w), 32, 32,
                       0, nullptr, DI_NORMAL);
            RECT dot = {cell.left + (cell.right - cell.left) / 2 - 3,
                        cell.bottom - 10,
                        cell.left + (cell.right - cell.left) / 2 + 3,
                        cell.bottom - 4};
            FillRound(dc, dot, 6, g_theme.accent);
            x += itemW;
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int itemW = 56;
        int idx = (p.x - 12) / itemW;
        if (idx >= 0) {
            if (idx < (int)g_pinned.size())
                LaunchApp(g_pinned[idx]);
            else if (idx < (int)(g_pinned.size() + g_windows.size()))
                FocusAppWindow(g_windows[idx - g_pinned.size()]);
        }
        return 0;
    }

    case WM_DESTROY:
        DeleteObject(font);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- Popovers (quick settings + calendar/notifications) ----------------------

enum QuickItem {
    QI_NONE,
    QI_DARK, QI_DND, QI_WIFI, QI_BT, QI_PLANE,
    QI_VOL_SLIDER, QI_BRIGHT_SLIDER,
    QI_LOCK, QI_SUSPEND, QI_DESKTOP, QI_EXIT
};

enum CalItem { CI_NONE = 100, CI_PREV, CI_NEXT, CI_DND };

static std::vector<HitRegion> g_popHits;
static int g_calMonthOffset;  // months back from today
static int g_dragSlider;      // QI_*_SLIDER while held

static void SetSliderFromPoint(POINT p) {
    for (auto& h : g_popHits)
        if (h.kind == g_dragSlider) {
            float v = (float)(p.x - h.rect.left) /
                      std::max(1L, h.rect.right - h.rect.left);
            if (h.kind == QI_VOL_SLIDER)
                SetMasterVolume(v);
            else if (h.kind == QI_BRIGHT_SLIDER)
                ApplyBrightness(v);
        }
}

static LRESULT CALLBACK PopoverProc(HWND hwnd, UINT msg, WPARAM wp,
                                    LPARAM lp) {
    static HFONT font, fontBig;

    switch (msg) {
    case WM_CREATE:
        font = FontOf(13);
        fontBig = FontOf(16, FW_SEMIBOLD);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        g_popHits.clear();
        FillRound(dc, rc, 14, g_theme.card);
        FrameRound(dc, rc, 14, g_theme.cardBorder);
        int pad = 14;
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);

        if (g_popKind == POP_QUICK) {
            // Toggle tiles row: Dark style, DND, Wi-Fi, BT, Airplane
            const wchar_t* names[] = {L"Dark Style", L"Do Not\nDisturb",
                                      L"Wi-Fi", L"Bluetooth", L"Airplane"};
            bool on[] = {g_settings.darkStyle, g_dnd, WifiPresent(), true,
                         false};
            int tw = (rc.right - pad * 2 - 8 * 4) / 5;
            for (int i = 0; i < 5; i++) {
                RECT cell = {pad + i * (tw + 8), pad,
                             pad + i * (tw + 8) + tw, pad + 56};
                bool hover = PtIn(cell, cursor);
                FillRound(dc, cell, 10,
                          on[i] ? g_theme.accent
                                : hover ? g_theme.cardHover
                                        : g_theme.stageBg);
                TextAt(dc, cell, names[i],
                       on[i] ? RGB(0xff, 0xff, 0xff) : g_theme.text, font);
                g_popHits.push_back({cell, QI_DARK + i, 0});
            }
            // Volume slider
            RECT vol = {pad + 24, pad + 76, rc.right - pad,
                        pad + 76 + 20};
            FillRound(dc, vol, 10, g_theme.stageBg);
            RECT fill = vol;
            fill.right =
                vol.left + (long)((vol.right - vol.left) * GetMasterVolume());
            FillRound(dc, fill, 10, g_theme.accent);
            RECT vr = {pad, vol.top - 4, pad + 20, vol.bottom + 4};
            DrawGlyph(dc, vr, 1 /* volume */, g_theme.text);
            g_popHits.push_back({vol, QI_VOL_SLIDER, 0});
            // Brightness slider
            RECT br = {pad + 24, vol.bottom + 18, rc.right - pad,
                       vol.bottom + 18 + 20};
            FillRound(dc, br, 10, g_theme.stageBg);
            RECT bf = br;
            bf.right =
                br.left + (long)((br.right - br.left) * 0.8);
            FillRound(dc, bf, 10, g_theme.accent);
            RECT sr = {pad, br.top - 4, pad + 20, br.bottom + 4};
            DrawGlyph(dc, sr, 10 /* sun */, g_theme.text);
            g_popHits.push_back({br, QI_BRIGHT_SLIDER, 0});
            // Battery + power row
            RECT bat = {pad, br.bottom + 16, rc.right - pad,
                        br.bottom + 44};
            SYSTEM_POWER_STATUS sps;
            wchar_t batText[48] = L"Power";
            if (GetSystemPowerStatus(&sps) &&
                sps.BatteryLifePercent != 255 &&
                !(sps.BatteryFlag & 128))
                swprintf(batText, 48, L"Battery %d%%%s",
                         sps.BatteryLifePercent,
                         (sps.BatteryFlag & 8) ? L" (charging)" : L"");
            TextAt(dc, bat, batText, g_theme.text, font,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            const wchar_t* pw[] = {L"Lock", L"Suspend", L"Desktop",
                                   L"Exit shell"};
            int pwWidth = (rc.right - pad * 2 - 24) / 4;
            for (int i = 0; i < 4; i++) {
                RECT cell = {pad + i * (pwWidth + 8),
                             bat.bottom + 8,
                             pad + i * (pwWidth + 8) + pwWidth,
                             bat.bottom + 8 + 34};
                if (PtIn(cell, cursor))
                    FillRound(dc, cell, 8, g_theme.cardHover);
                else
                    FrameRound(dc, cell, 8, g_theme.cardBorder);
                TextAt(dc, cell, pw[i], g_theme.text, font);
                g_popHits.push_back({cell, QI_LOCK + i, 0});
            }
        } else if (g_popKind == POP_CALENDAR) {
            // Month header
            SYSTEMTIME today;
            GetLocalTime(&today);
            int year = today.wYear;
            int month = today.wMonth - g_calMonthOffset;
            while (month < 1) {
                month += 12;
                year--;
            }
            while (month > 12) {
                month -= 12;
                year++;
            }
            static const wchar_t* monthNames[] = {
                L"January", L"February", L"March", L"April", L"May",
                L"June", L"July", L"August", L"September", L"October",
                L"November", L"December"};
            wchar_t header[48];
            swprintf(header, 48, L"%s %d", monthNames[month - 1], year);
            RECT hr = {pad + 60, pad, rc.right - pad - 60, pad + 30};
            TextAt(dc, hr, header, g_theme.text, fontBig);
            RECT prev = {pad, pad, pad + 40, pad + 30};
            RECT next = {rc.right - pad - 40, pad, rc.right - pad,
                         pad + 30};
            TextAt(dc, prev, L"<", g_theme.text, fontBig);
            TextAt(dc, next, L">", g_theme.text, fontBig);
            g_popHits.push_back({prev, CI_PREV, 0});
            g_popHits.push_back({next, CI_NEXT, 0});
            // Day grid
            const wchar_t* dow[] = {L"S", L"M", L"T", L"W", L"T", L"F",
                                    L"S"};
            int cellW = (rc.right - pad * 2) / 7;
            int y0 = pad + 36;
            for (int i = 0; i < 7; i++) {
                RECT cell = {pad + i * cellW, y0, pad + (i + 1) * cellW,
                             y0 + 18};
                TextAt(dc, cell, dow[i], g_theme.textDim, font);
            }
            SYSTEMTIME first = {};
            first.wYear = (WORD)year;
            first.wMonth = (WORD)month;
            first.wDay = 1;
            FILETIME ft;
            SystemTimeToFileTime(&first, &ft);
            // Weekday from FILETIME days: 1601-01-01 was a Monday.
            ULARGE_INTEGER ull = {};
            ull.LowPart = ft.dwLowDateTime;
            ull.HighPart = ft.dwHighDateTime;
            int days = (int)(ull.QuadPart / 864000000000ULL);
            int firstDow = (days + 1) % 7;  // 1601-01-01 was a Monday(1)
            static const int mdays[] = {31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};
            int dim = mdays[month - 1];
            bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
            if (month == 2 && leap)
                dim = 29;
            int cell = 0;
            for (int d = 1 - firstDow; d <= dim; d++, cell++) {
                if (d < 1)
                    continue;
                int row = cell / 7, col = cell % 7;
                RECT r = {pad + col * cellW, y0 + 20 + row * 26,
                          pad + (col + 1) * cellW, y0 + 20 + (row + 1) * 26};
                bool isToday = g_calMonthOffset == 0 && d == today.wDay;
                if (isToday)
                    FillRound(dc, r, 8, g_theme.accent);
                wchar_t num[4];
                swprintf(num, 4, L"%d", d);
                TextAt(dc, r, num,
                       isToday ? RGB(0xff, 0xff, 0xff) : g_theme.text,
                       font);
            }
            // DND row
            int y1 = y0 + 20 + 7 * 26 + 10;
            RECT dnd = {pad, y1, rc.right - pad, y1 + 26};
            if (PtIn(dnd, cursor))
                FillRound(dc, dnd, 8, g_theme.cardHover);
            TextAt(dc, dnd,
                   g_dnd ? L"Do Not Disturb: ON" : L"Do Not Disturb: OFF",
                   g_theme.text, font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            g_popHits.push_back({dnd, CI_DND, 0});
            // Notifications
            RECT nh = {pad, dnd.bottom + 8, rc.right - pad,
                       dnd.bottom + 28};
            TextAt(dc, nh, L"Notifications", g_theme.text, fontBig,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            int ny = nh.bottom + 4;
            if (g_notes.empty()) {
                RECT nr = {pad, ny, rc.right - pad, ny + 40};
                TextAt(dc, nr, L"No Notifications", g_theme.textDim, font);
            } else {
                for (size_t i = 0; i < g_notes.size() && i < 5; i++) {
                    RECT nr = {pad, ny, rc.right - pad, ny + 40};
                    FillRound(dc, nr, 8, g_theme.stageBg);
                    RECT tr = {nr.left + 10, nr.top + 2, nr.right - 10,
                               nr.top + 20};
                    TextAt(dc, tr, g_notes[i].title.c_str(), g_theme.text,
                           font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    RECT br = {nr.left + 10, nr.top + 20, nr.right - 10,
                               nr.bottom - 2};
                    TextAt(dc, br, g_notes[i].body.c_str(),
                           g_theme.textDim, font,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    ny += 44;
                }
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        InvalidateRect(hwnd, nullptr, FALSE);
        for (auto& h : g_popHits) {
            if (!PtIn(h.rect, p))
                continue;
            switch (h.kind) {
            case QI_DARK:
                g_settings.darkStyle = !g_settings.darkStyle;
                ApplyTheme();
                SetDarkAppsTheme(g_settings.darkStyle);
                InvalidateRect(g_topBar, nullptr, TRUE);
                PositionDock();
                break;
            case QI_DND:
                SetDoNotDisturb(!g_dnd);
                break;
            case QI_WIFI:
                ShellExecuteW(nullptr, L"open", L"ms-settings:network-wifi",
                              nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case QI_BT:
                ShellExecuteW(nullptr, L"open", L"ms-settings:bluetooth",
                              nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case QI_PLANE:
                ShellExecuteW(nullptr, L"open",
                              L"ms-settings:network-airplanemode", nullptr,
                              nullptr, SW_SHOWNORMAL);
                break;
            case QI_VOL_SLIDER:
            case QI_BRIGHT_SLIDER:
                g_dragSlider = h.kind;
                SetSliderFromPoint(p);
                SetCapture(hwnd);
                break;
            case QI_LOCK:
                ClosePopover();
                LockWorkStation();
                break;
            case QI_SUSPEND:
                ClosePopover();
                EnableShutdownPrivilege();
                SetSuspendState(FALSE, TRUE, FALSE);
                break;
            case QI_DESKTOP:
                ClosePopover();
                // Win+D: minimize all / show desktop
                keybd_event(VK_LWIN, 0, 0, 0);
                keybd_event('D', 0, 0, 0);
                keybd_event('D', 0, KEYEVENTF_KEYUP, 0);
                keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);
                break;
            case QI_EXIT:
                // Tears down the whole shell: the UI thread's cleanup
                // restores the taskbar, unregisters thumbnails and destroys
                // every shell window. The mod stays loaded but inert until
                // re-enabled.
                g_running = false;
                PostQuitMessage(0);
                break;
            case CI_PREV:
                g_calMonthOffset++;
                break;
            case CI_NEXT:
                g_calMonthOffset = std::max(0, g_calMonthOffset - 1);
                break;
            case CI_DND:
                SetDoNotDisturb(!g_dnd);
                break;
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_dragSlider) {
            SetSliderFromPoint({GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_dragSlider) {
            g_dragSlider = 0;
            ReleaseCapture();
        }
        return 0;

    case WM_KILLFOCUS:
        // keep popovers until explicit close; topbar clicks handle dismissal
        return 0;

    case WM_DESTROY:
        DeleteObject(font);
        DeleteObject(fontBig);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- Notification banner ------------------------------------------------------

static LRESULT CALLBACK BannerProc(HWND hwnd, UINT msg, WPARAM wp,
                                   LPARAM lp) {
    static HFONT font, fontTitle;
    switch (msg) {
    case WM_CREATE:
        font = FontOf(13);
        fontTitle = FontOf(14, FW_SEMIBOLD);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRound(dc, rc, 14, g_theme.card);
        FrameRound(dc, rc, 14, g_theme.cardBorder);
        if (!g_notes.empty()) {
            RECT tr = {16, 10, rc.right - 16, 30};
            TextAt(dc, tr, g_notes[0].title.c_str(), g_theme.text,
                   fontTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT br = {16, 32, rc.right - 16, rc.bottom - 8};
            TextAt(dc, br, g_notes[0].body.c_str(), g_theme.textDim,
                   font, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == IDT_BANNER) {
            KillTimer(hwnd, IDT_BANNER);
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        ShowWindow(hwnd, SW_HIDE);
        KillTimer(hwnd, IDT_BANNER);
        return 0;

    case WM_DESTROY:
        DeleteObject(font);
        DeleteObject(fontTitle);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// UI thread
// ---------------------------------------------------------------------------

static void RegisterHotkeys() {
    // Parse "ctrl+alt+shift+space" style setting.
    UINT mods = MOD_NOREPEAT;
    UINT vk = 0;
    std::wstring s = g_settings.overviewHotkey;
    for (auto& c : s)
        c = towlower(c);
    if (s.find(L"ctrl") != std::wstring::npos)
        mods |= MOD_CONTROL;
    if (s.find(L"alt") != std::wstring::npos)
        mods |= MOD_ALT;
    if (s.find(L"shift") != std::wstring::npos)
        mods |= MOD_SHIFT;
    if (s.find(L"win") != std::wstring::npos)
        mods |= MOD_WIN;
    size_t plus = s.find_last_of(L'+');
    std::wstring key = plus == std::wstring::npos ? s : s.substr(plus + 1);
    if (key == L"space")
        vk = VK_SPACE;
    else if (key == L"tab")
        vk = VK_TAB;
    else if (key == L"esc")
        vk = VK_ESCAPE;
    else if (key.size() == 1 && key[0] >= L'a' && key[0] <= L'z')
        vk = L'A' + (key[0] - L'a');
    else if (key.size() == 1 && key[0] >= L'0' && key[0] <= L'9')
        vk = L'0' + (key[0] - L'0');
    else if (key.size() >= 2 && key[0] == L'f')
        vk = VK_F1 + _wtoi(key.c_str() + 1) - 1;
    if (vk)
        RegisterHotKey(g_topBar, HOTKEY_OVERVIEW, mods, vk);
    RegisterHotKey(g_topBar, HOTKEY_WS_PREV,
                   MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_LEFT);
    RegisterHotKey(g_topBar, HOTKEY_WS_NEXT,
                   MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_RIGHT);
    RegisterHotKey(g_topBar, HOTKEY_MOVE_PREV,
                   MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                   VK_LEFT);
    RegisterHotKey(g_topBar, HOTKEY_MOVE_NEXT,
                   MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                   VK_RIGHT);
}

static void SetTrayVisible(bool visible) {
    if (HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr))
        ShowWindow(tray, visible ? SW_SHOW : SW_HIDE);
    if (HWND tray =
            FindWindowW(L"Shell_SecondaryTrayWnd", nullptr))
        ShowWindow(tray, visible ? SW_SHOW : SW_HIDE);
    for (HWND t = FindWindowExW(nullptr, nullptr,
                                L"Shell_SecondaryTrayWnd", nullptr);
         t; t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd",
                              nullptr))
        ShowWindow(t, visible ? SW_SHOW : SW_HIDE);
}

static DWORD WINAPI ShellUiThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ApplyTheme();
    InitAudio();
    ScanStartMenu();
    ResolvePinnedApps();
    RefreshWindows();
    EnableShutdownPrivilege();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = TopBarProc;
    wc.lpszClassName = kClsTopBar;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = OverviewProc;
    wc.lpszClassName = kClsOverview;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = DockProc;
    wc.lpszClassName = kClsDock;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = PopoverProc;
    wc.lpszClassName = kClsPopover;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = BannerProc;
    wc.lpszClassName = kClsBanner;
    RegisterClassExW(&wc);

    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
               WS_EX_LAYERED;
    g_topBar = CreateWindowExW(ex, kClsTopBar, L"", WS_POPUP | WS_VISIBLE,
                               0, 0, 10, 10, nullptr, nullptr,
                               wc.hInstance, nullptr);
    SetLayeredWindowAttributes(g_topBar, 0, 235, LWA_ALPHA);
    PositionTopBar();

    g_overview = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kClsOverview,
        L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance,
        nullptr);
    SetLayeredWindowAttributes(g_overview, 0, 242, LWA_ALPHA);

    g_dock = CreateWindowExW(ex, kClsDock, L"", WS_POPUP, 0, 0, 10, 10,
                             nullptr, nullptr, wc.hInstance, nullptr);
    SetLayeredWindowAttributes(g_dock, 0, 230, LWA_ALPHA);
    PositionDock();

    g_popover = CreateWindowExW(ex, kClsPopover, L"", WS_POPUP, 0, 0,
                                10, 10, nullptr, nullptr, wc.hInstance,
                                nullptr);
    SetLayeredWindowAttributes(g_popover, 0, 245, LWA_ALPHA);

    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY),
                    &mi);
    int bw = 340, bh = 96;
    g_banner = CreateWindowExW(
        ex, kClsBanner, L"", WS_POPUP,
        (mi.rcWork.left + mi.rcWork.right - bw) / 2,
        mi.rcWork.top + g_settings.topBarHeight + 8, bw, bh, nullptr,
        nullptr, wc.hInstance, nullptr);
    SetLayeredWindowAttributes(g_banner, 0, 245, LWA_ALPHA);

    RegisterHotkeys();
    SetTimer(g_topBar, IDT_CLOCK, 1000, nullptr);
    SetTimer(g_topBar, IDT_POLL, 1500, nullptr);
    SetTimer(g_overview, IDT_THUMB, 1000, nullptr);

    InterlockedExchange(&g_shellActive, 1);
    if (g_settings.hideTaskbar)
        SetTrayVisible(false);

    MSG msg;
    while (g_running && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    InterlockedExchange(&g_shellActive, 0);
    SetTrayVisible(true);
    ClearThumbnails();
    for (auto& app : g_apps)
        if (app.icon)
            DestroyIcon(app.icon);
    for (auto& app : g_pinned)
        if (app.icon)
            DestroyIcon(app.icon);
    if (g_volume)
        g_volume->Release();
    DestroyWindow(g_topBar);
    DestroyWindow(g_overview);
    DestroyWindow(g_dock);
    DestroyWindow(g_popover);
    DestroyWindow(g_banner);
    CoUninitialize();
    return 0;
}

// ---------------------------------------------------------------------------
// Hooks: keep the taskbar hidden while the shell is active
// ---------------------------------------------------------------------------

using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
static ShowWindow_t ShowWindow_Orig;
static BOOL WINAPI ShowWindow_Hook(HWND hwnd, int nCmdShow) {
    if (g_shellActive && g_settings.hideTaskbar && hwnd &&
        nCmdShow != SW_HIDE) {
        wchar_t cls[32] = {};
        if (GetClassNameW(hwnd, cls, 32) &&
            (!wcscmp(cls, L"Shell_TrayWnd") ||
             !wcscmp(cls, L"Shell_SecondaryTrayWnd")))
            return TRUE;
    }
    return ShowWindow_Orig(hwnd, nCmdShow);
}

using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
static SetWindowPos_t SetWindowPos_Orig;
static BOOL WINAPI SetWindowPos_Hook(HWND hwnd, HWND hwndAfter, int x, int y,
                                     int cx, int cy, UINT flags) {
    if (g_shellActive && g_settings.hideTaskbar && hwnd &&
        (flags & SWP_SHOWWINDOW)) {
        wchar_t cls[32] = {};
        if (GetClassNameW(hwnd, cls, 32) &&
            (!wcscmp(cls, L"Shell_TrayWnd") ||
             !wcscmp(cls, L"Shell_SecondaryTrayWnd")))
            flags = (flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW;
    }
    return SetWindowPos_Orig(hwnd, hwndAfter, x, y, cx, cy, flags);
}

// ---------------------------------------------------------------------------
// Windhawk entry points
// ---------------------------------------------------------------------------

extern "C" BOOL Wh_ModInit() {
    Wh_Log(L"Adwaita Shell: init");
    LoadSettings();
    g_running = true;

    Wh_SetFunctionHook(
        reinterpret_cast<void*>(GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "ShowWindow")),
        reinterpret_cast<void*>(ShowWindow_Hook),
        reinterpret_cast<void**>(&ShowWindow_Orig));
    Wh_SetFunctionHook(
        reinterpret_cast<void*>(GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "SetWindowPos")),
        reinterpret_cast<void*>(SetWindowPos_Hook),
        reinterpret_cast<void**>(&SetWindowPos_Orig));

    g_uiThread = CreateThread(nullptr, 0, ShellUiThread, nullptr, 0,
                              &g_uiTid);
    if (!g_uiThread) {
        Wh_Log(L"Adwaita Shell: failed to create UI thread");
        return FALSE;
    }
    return TRUE;
}

extern "C" void Wh_ModUninit() {
    Wh_Log(L"Adwaita Shell: uninit");
    g_running = false;
    if (g_uiTid)
        PostThreadMessageW(g_uiTid, WM_QUIT, 0, 0);
    if (g_uiThread) {
        WaitForSingleObject(g_uiThread, 8000);
        CloseHandle(g_uiThread);
        g_uiThread = nullptr;
    }
}

extern "C" void Wh_ModSettingsChanged() {
    Wh_Log(L"Adwaita Shell: settings changed");
    // The UI thread owns the windows and hotkeys; defer the reload to it.
    if (g_topBar)
        PostMessageW(g_topBar, WM_APP_SHELL_SETTINGS, 0, 0);
}
