// ==WindhawkMod==
// @id              adwaita-activities
// @name            Adwaita Activities (hot corner + overview)
// @description     GNOME-style Activities trigger: push the cursor into a screen corner (top-left by default) or press a hotkey to open the overview (Task View), Start, or Search
// @version         1.0
// @author          CommunityPoke
// @github          https://github.com/CommunityPoke
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -luser32
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0.
//
// Part of the Windhawk port of the "Adwaita Shell" Electron prototype:
// https://github.com/CommunityPokeOrg/gnome-shell-windows

// ==WindhawkModReadme==
/*
# Adwaita Activities (hot corner + overview)

GNOME Shell opens the Activities overview when the pointer hits the top-left
corner of the screen, or when the user presses Super. The Electron prototype
in this repo maps that to `Ctrl+Space` and an "Activities" button.

This mod brings the same trigger to the real Windows shell, running inside
explorer.exe:

* **Hot corner** — park the cursor in a screen corner (top-left by default,
  like GNOME) for a short dwell time to fire the configured action.
* **Hotkey** — an optional keyboard shortcut (default `Ctrl+Space`, matching
  the Electron prototype; the Super key is owned by Windows).

## Actions

* `taskView` (default) — the closest Windows equivalent of the Activities
  overview: window spread + virtual desktop strip (`Win+Tab`).
* `start` — the Start menu, the closest equivalent of the app grid.
* `search` — Windows Search (`Win+S`), matching the overview's type-to-search.
* `none` — disables the hot corner (the hotkey still works).

The corner is detected against the physical monitor edge, so it still works
when a fullscreen app covers the taskbar.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- Action: taskView
  $name: Action
  $description: What to do when the hot corner or the hotkey fires.
  $options:
  - taskView: Open overview (Task View, Win+Tab)
  - start: Open app grid (Start menu)
  - search: Open search (Win+S)
  - none: Disabled (hot corner only; hotkey still applies)
- Corner: topLeft
  $name: Hot corner
  $options:
  - topLeft: Top-left (GNOME default)
  - topRight: Top-right
  - bottomLeft: Bottom-left
  - bottomRight: Bottom-right
- CornerSize: 4
  $name: Corner size (px)
  $description: Size in pixels of the square activation zone in the corner.
- DwellMs: 250
  $name: Dwell time (ms)
  $description: >-
    How long the cursor must stay inside the corner before the action fires.
    Prevents accidental triggers when the cursor just passes through.
- HotkeyEnabled: true
  $name: Enable hotkey
  $description: >-
    Bind the overview hotkey (Ctrl+Space, same as the Electron prototype's
    Activites toggle).
- Hotkey: ctrl+space
  $name: Hotkey
  $description: >-
    Modifier+key combination, e.g. "ctrl+space", "ctrl+alt+space",
    "alt+f1". Modifiers: ctrl, alt, shift. Key: a letter, digit, F1-F24,
    space, tab, home, end, pageup, pagedown, insert, delete, or arrows.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <atomic>

using WindhawkUtils::StringSetting;

namespace {

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };
enum class Action { TaskView, Start, Search, None };

struct {
    Action action;
    Corner corner;
    int cornerSize;
    int dwellMs;
    bool hotkeyEnabled;
    StringSetting hotkey;
} g_settings;

enum { WM_RELOAD = WM_APP, WM_TRIGGER = WM_APP + 1 };
const int kHotkeyId = 1;

HANDLE g_thread = nullptr;
DWORD g_threadId = 0;
std::atomic<bool> g_unloading{false};

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void SendKeyCombo(std::initializer_list<WORD> keys) {
    INPUT inputs[8] = {};
    size_t count = 0;

    for (WORD vk : keys) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = vk;
        count++;
    }
    size_t down = count;
    for (size_t i = 0; i < down; i++) {
        inputs[count] = inputs[down - 1 - i];
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }

    SendInput((UINT)count, inputs, sizeof(INPUT));
}

void RunAction(Action action) {
    switch (action) {
        case Action::TaskView:
            // The Activities overview: window spread + workspace strip.
            SendKeyCombo({VK_LWIN, VK_TAB});
            break;
        case Action::Start:
            // The app grid.
            SendKeyCombo({VK_LWIN});
            break;
        case Action::Search:
            SendKeyCombo({VK_LWIN, 'S'});
            break;
        case Action::None:
            break;
    }
}

// ---------------------------------------------------------------------------
// Corner detection
// ---------------------------------------------------------------------------

// Returns the rect of the configured corner zone on the monitor that contains
// the point.
bool GetCornerZone(POINT pt, RECT* zone) {
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfo(monitor, &mi)) {
        return false;
    }

    RECT rc = mi.rcMonitor;
    int s = g_settings.cornerSize;
    if (s < 1) {
        s = 1;
    }

    switch (g_settings.corner) {
        case Corner::TopLeft:
            *zone = {rc.left, rc.top, rc.left + s, rc.top + s};
            break;
        case Corner::TopRight:
            *zone = {rc.right - s, rc.top, rc.right, rc.top + s};
            break;
        case Corner::BottomLeft:
            *zone = {rc.left, rc.bottom - s, rc.left + s, rc.bottom};
            break;
        case Corner::BottomRight:
            *zone = {rc.right - s, rc.bottom - s, rc.right, rc.bottom};
            break;
    }
    return true;
}

// Margin around the corner zone the cursor must leave before the corner can
// trigger again.
void GetResetZone(const RECT& zone, RECT* reset) {
    *reset = zone;
    InflateRect(reset, 32, 32);
}

// ---------------------------------------------------------------------------
// Hotkey parsing
// ---------------------------------------------------------------------------

struct ParsedHotkey {
    UINT modifiers = 0;
    UINT vk = 0;
};

WORD KeyNameToVk(const WCHAR* name) {
    if (!name[0]) {
        return 0;
    }
    if (!name[1]) {
        WCHAR c = name[0];
        if (c >= L'a' && c <= L'z') {
            return (WORD)(c - L'a' + 'A');
        }
        if (c >= L'0' && c <= L'9') {
            return (WORD)c;
        }
        return 0;
    }

    struct {
        PCWSTR name;
        WORD vk;
    } names[] = {
        {L"space", VK_SPACE},     {L"tab", VK_TAB},
        {L"enter", VK_RETURN},    {L"escape", VK_ESCAPE},
        {L"esc", VK_ESCAPE},      {L"backspace", VK_BACK},
        {L"delete", VK_DELETE},   {L"del", VK_DELETE},
        {L"insert", VK_INSERT},   {L"ins", VK_INSERT},
        {L"home", VK_HOME},       {L"end", VK_END},
        {L"pageup", VK_PRIOR},    {L"pagedown", VK_NEXT},
        {L"up", VK_UP},           {L"down", VK_DOWN},
        {L"left", VK_LEFT},       {L"right", VK_RIGHT},
    };
    for (const auto& n : names) {
        if (_wcsicmp(name, n.name) == 0) {
            return n.vk;
        }
    }

    if ((name[0] == L'f' || name[0] == L'F') && name[1] >= L'1' &&
        name[1] <= L'9') {
        int num = _wtoi(name + 1);
        if (num >= 1 && num <= 24) {
            return (WORD)(VK_F1 + num - 1);
        }
    }

    return 0;
}

bool ParseHotkey(PCWSTR str, ParsedHotkey* out) {
    *out = {};
    if (!str) {
        return false;
    }

    WCHAR buf[64];
    wcsncpy_s(buf, str, ARRAYSIZE(buf) - 1);

    UINT modifiers = 0;
    WORD vk = 0;
    int keyCount = 0;

    WCHAR* ctx = nullptr;
    for (WCHAR* tok = wcstok_s(buf, L"+", &ctx); tok;
         tok = wcstok_s(nullptr, L"+", &ctx)) {
        while (*tok == L' ') {
            tok++;
        }
        if (_wcsicmp(tok, L"ctrl") == 0 || _wcsicmp(tok, L"control") == 0) {
            modifiers |= MOD_CONTROL;
        } else if (_wcsicmp(tok, L"alt") == 0) {
            modifiers |= MOD_ALT;
        } else if (_wcsicmp(tok, L"shift") == 0) {
            modifiers |= MOD_SHIFT;
        } else {
            vk = KeyNameToVk(tok);
            keyCount++;
        }
    }

    if (!vk || keyCount != 1) {
        return false;
    }

    out->modifiers = modifiers | MOD_NOREPEAT;
    out->vk = vk;
    return true;
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

void LoadSettings() {
    PCWSTR action = Wh_GetStringSetting(L"Action");
    g_settings.action = Action::TaskView;
    if (_wcsicmp(action, L"start") == 0) {
        g_settings.action = Action::Start;
    } else if (_wcsicmp(action, L"search") == 0) {
        g_settings.action = Action::Search;
    } else if (_wcsicmp(action, L"none") == 0) {
        g_settings.action = Action::None;
    }
    Wh_FreeStringSetting(action);

    PCWSTR corner = Wh_GetStringSetting(L"Corner");
    g_settings.corner = Corner::TopLeft;
    if (_wcsicmp(corner, L"topRight") == 0) {
        g_settings.corner = Corner::TopRight;
    } else if (_wcsicmp(corner, L"bottomLeft") == 0) {
        g_settings.corner = Corner::BottomLeft;
    } else if (_wcsicmp(corner, L"bottomRight") == 0) {
        g_settings.corner = Corner::BottomRight;
    }
    Wh_FreeStringSetting(corner);

    g_settings.cornerSize = Wh_GetIntSetting(L"CornerSize");
    g_settings.dwellMs = Wh_GetIntSetting(L"DwellMs");
    g_settings.hotkeyEnabled = Wh_GetIntSetting(L"HotkeyEnabled") != 0;
    g_settings.hotkey = StringSetting::make(L"Hotkey");
}

DWORD WINAPI WorkerThread(LPVOID) {
    LoadSettings();

    MSG msg;
    PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);  // Force the queue.

    // Hotkey registration. Registering on a NULL hwnd delivers WM_HOTKEY to
    // this thread's message queue.
    auto RegisterOverviewHotkey = [&]() {
        ParsedHotkey hk;
        if (g_settings.hotkeyEnabled &&
            ParseHotkey(g_settings.hotkey.get(), &hk)) {
            if (!RegisterHotKey(nullptr, kHotkeyId, hk.modifiers, hk.vk)) {
                Wh_Log(L"RegisterHotKey failed for '%s': %u",
                       g_settings.hotkey.get(), GetLastError());
            }
        }
    };
    RegisterOverviewHotkey();

    // Poll the cursor position with a timer; 50 ms is far below any
    // perceptible corner latency and keeps CPU use negligible. For a
    // NULL-hwnd timer Windows generates the ID itself and returns it from
    // SetTimer; WM_TIMER wParam carries that generated ID, not the one
    // passed in.
    UINT_PTR pollTimer = SetTimer(nullptr, 0, 50, nullptr);

    DWORD dwellStart = 0;
    bool armed = true;

    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.hwnd == nullptr) {
            switch (msg.message) {
                case WM_HOTKEY:
                    if (msg.wParam == kHotkeyId) {
                        RunAction(g_settings.action != Action::None
                                      ? g_settings.action
                                      : Action::TaskView);
                    }
                    break;

                case WM_TIMER:
                    if (msg.wParam != pollTimer) {
                        break;
                    }
                    if (g_settings.action == Action::None) {
                        break;
                    }

                    POINT pt;
                    if (!GetCursorPos(&pt)) {
                        break;
                    }

                    RECT zone;
                    if (!GetCornerZone(pt, &zone)) {
                        break;
                    }

                    RECT resetZone;
                    GetResetZone(zone, &resetZone);
                    if (!armed) {
                        if (!PtInRect(&resetZone, pt)) {
                            armed = true;
                        }
                        break;
                    }

                    if (PtInRect(&zone, pt)) {
                        if (!dwellStart) {
                            dwellStart = GetTickCount();
                        } else if (GetTickCount() - dwellStart >=
                                   (DWORD)g_settings.dwellMs) {
                            RunAction(g_settings.action);
                            armed = false;
                            dwellStart = 0;
                        }
                    } else {
                        dwellStart = 0;
                    }
                    break;

                case WM_RELOAD:
                    UnregisterHotKey(nullptr, kHotkeyId);
                    LoadSettings();
                    RegisterOverviewHotkey();
                    armed = true;
                    dwellStart = 0;
                    break;
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterHotKey(nullptr, kHotkeyId);
    KillTimer(nullptr, pollTimer);
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Windhawk lifecycle
// ---------------------------------------------------------------------------

BOOL Wh_ModInit() {
    Wh_Log(L">");

    g_thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, &g_threadId);
    if (!g_thread) {
        Wh_Log(L"CreateThread failed");
        return FALSE;
    }

    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L">");

    g_unloading = true;

    if (g_thread) {
        PostThreadMessage(g_threadId, WM_QUIT, 0, 0);
        if (WaitForSingleObject(g_thread, 5000) == WAIT_TIMEOUT) {
            Wh_Log(L"Worker thread didn't exit; unloading anyway");
        }
        CloseHandle(g_thread);
        g_thread = nullptr;
        g_threadId = 0;
    }
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    Wh_Log(L">");

    if (g_threadId) {
        PostThreadMessage(g_threadId, WM_RELOAD, 0, 0);
    }

    *bReload = FALSE;
    return TRUE;
}
