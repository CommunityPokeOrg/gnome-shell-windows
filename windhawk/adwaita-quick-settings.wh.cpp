// ==WindhawkMod==
// @id              adwaita-quick-settings
// @name            Adwaita Quick Settings (tray click routing + corner)
// @description     GNOME-style taskbar: clicking the clock opens notifications/calendar, clicking the status area opens quick settings, plus an optional top-right hot corner for quick settings
// @version         1.0
// @author          CommunityPoke
// @github          https://github.com/CommunityPoke
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -luser32 -lcomctl32
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0.
//
// Part of the Windhawk port of the "Adwaita Shell" Electron prototype:
// https://github.com/CommunityPokeOrg/gnome-shell-windows

// ==WindhawkModReadme==
/*
# Adwaita Quick Settings (tray click routing + corner)

GNOME Shell splits its top-right status area into two popovers:

* clicking the **clock** opens the calendar + notification list;
* clicking the **status icons** (network / volume / battery) opens the
  quick settings panel.

This mod subclasses `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` and maps
those clicks to the Windows equivalents:

* clock -> Notification Center + calendar (`Win+N`)
* status area -> Quick Settings / Action Center (`Win+A`)

On Windows 10 this replaces the stock per-icon flyouts with the combined
GNOME-style panels. On Windows 11 the native taskbar already routes these
clicks the same way, so the mod mainly adds the hot corner there.

## Hot corner

An optional hot corner (top-right by default, matching where GNOME puts the
status area on the top bar) opens quick settings after a short dwell.

## Roadmap

A real GNOME quick-settings popover (Adwaita-styled toggles and sliders)
would need a custom overlay window; this mod wires the shell interaction and
lets Windows' own panel do the rendering.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- ClockClick: notificationCenter
  $name: Clock click action
  $description: What clicking the taskbar clock opens.
  $options:
  - notificationCenter: Notifications + calendar (Win+N)
  - quickSettings: Quick settings (Win+A)
  - native: Keep the Windows behavior
- StatusClick: quickSettings
  $name: Status area click action
  $description: >-
    What clicking the network/volume/battery area of the notification tray
    opens.
  $options:
  - quickSettings: Quick settings (Win+A)
  - notificationCenter: Notifications + calendar (Win+N)
  - native: Keep the Windows behavior
- CornerEnabled: false
  $name: Quick-settings hot corner
  $description: >-
    Open quick settings when the cursor dwells in the corner chosen below.
- Corner: topRight
  $name: Hot corner position
  $options:
  - topRight: Top-right
  - topLeft: Top-left
  - bottomRight: Bottom-right
  - bottomLeft: Bottom-left
- CornerSize: 4
  $name: Corner size (px)
- DwellMs: 350
  $name: Corner dwell time (ms)
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <windowsx.h>

#include <atomic>

namespace {

enum class ClickAction { QuickSettings, NotificationCenter, Native };
enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

struct {
    ClickAction clockClick;
    ClickAction statusClick;
    bool cornerEnabled;
    Corner corner;
    int cornerSize;
    int dwellMs;
} g_settings;

HANDLE g_thread = nullptr;
DWORD g_threadId = 0;
std::atomic<bool> g_unloading{false};

// Windows ignores the requested ID for NULL-hwnd timers and generates one,
// returned by SetTimer; WM_TIMER wParam carries the generated ID.

enum { WM_RELOAD = WM_APP };

// ---------------------------------------------------------------------------
// Panel launchers
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

void OpenQuickSettings() {
    SendKeyCombo({VK_LWIN, 'A'});
}

void OpenNotificationCenter() {
    SendKeyCombo({VK_LWIN, 'N'});
}

void RunClickAction(ClickAction action) {
    switch (action) {
        case ClickAction::QuickSettings:
            OpenQuickSettings();
            break;
        case ClickAction::NotificationCenter:
            OpenNotificationCenter();
            break;
        case ClickAction::Native:
            break;
    }
}

// ---------------------------------------------------------------------------
// Tray hit testing
// ---------------------------------------------------------------------------

HWND FindNotifyWindow(HWND trayWnd) {
    // Primary:  Shell_TrayWnd -> TrayNotifyWnd
    // Secondary: Shell_SecondaryTrayWnd -> WorkerW -> TrayNotifyWnd
    HWND notify = FindWindowExW(trayWnd, nullptr, L"TrayNotifyWnd", nullptr);
    if (!notify) {
        HWND worker = FindWindowExW(trayWnd, nullptr, L"WorkerW", nullptr);
        if (worker) {
            notify =
                FindWindowExW(worker, nullptr, L"TrayNotifyWnd", nullptr);
        }
    }
    return notify;
}

// What part of the tray got the click: 0 = none/pass-through, 1 = clock,
// 2 = status icons area.
int HitTestTray(HWND trayWnd, POINT screenPt) {
    HWND notify = FindNotifyWindow(trayWnd);
    if (!notify) {
        return 0;
    }

    RECT notifyRect;
    if (!GetWindowRect(notify, &notifyRect) ||
        !PtInRect(&notifyRect, screenPt)) {
        return 0;
    }

    HWND clock = FindWindowExW(notify, nullptr, L"TrayClockWClass", nullptr);
    if (clock) {
        RECT clockRect;
        if (GetWindowRect(clock, &clockRect) &&
            PtInRect(&clockRect, screenPt)) {
            return 1;
        }
    }

    // Inside TrayNotifyWnd but not on the clock: the status icons / chevron.
    return 2;
}

LRESULT CALLBACK TraySubclassProc(HWND hWnd,
                                  UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: {
            if (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP) {
                break;  // Leave right-clicks (context menus) alone.
            }

            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT screenPt = pt;
            ClientToScreen(hWnd, &screenPt);

            int hit = HitTestTray(hWnd, screenPt);
            ClickAction action = ClickAction::Native;
            if (hit == 1) {
                action = g_settings.clockClick;
            } else if (hit == 2) {
                action = g_settings.statusClick;
            }

            if (action == ClickAction::Native) {
                break;
            }

            if (uMsg == WM_LBUTTONUP) {
                RunClickAction(action);
            }

            // Swallow both ends of the click so the native flyout doesn't
            // also open.
            return 0;
        }
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassAllTrays() {
    HWND tray = nullptr;
    while ((tray = FindWindowExW(nullptr, tray, L"Shell_TrayWnd",
                                 nullptr)) != nullptr) {
        WindhawkUtils::SetWindowSubclassFromAnyThread(tray,
                                                      TraySubclassProc, 0);
    }
    HWND secondary = nullptr;
    while ((secondary = FindWindowExW(nullptr, secondary,
                                     L"Shell_SecondaryTrayWnd",
                                     nullptr)) != nullptr) {
        WindhawkUtils::SetWindowSubclassFromAnyThread(
            secondary, TraySubclassProc, 0);
    }
}

void UnsubclassAllTrays() {
    HWND tray = nullptr;
    while ((tray = FindWindowExW(nullptr, tray, L"Shell_TrayWnd",
                                 nullptr)) != nullptr) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(
            tray, TraySubclassProc);
    }
    HWND secondary = nullptr;
    while ((secondary = FindWindowExW(nullptr, secondary,
                                     L"Shell_SecondaryTrayWnd",
                                     nullptr)) != nullptr) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(
            secondary, TraySubclassProc);
    }
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

ClickAction ParseClickAction(PCWSTR value, ClickAction fallback) {
    if (_wcsicmp(value, L"quickSettings") == 0) {
        return ClickAction::QuickSettings;
    }
    if (_wcsicmp(value, L"notificationCenter") == 0) {
        return ClickAction::NotificationCenter;
    }
    if (_wcsicmp(value, L"native") == 0) {
        return ClickAction::Native;
    }
    return fallback;
}

void LoadSettings() {
    PCWSTR clock = Wh_GetStringSetting(L"ClockClick");
    g_settings.clockClick =
        ParseClickAction(clock, ClickAction::NotificationCenter);
    Wh_FreeStringSetting(clock);

    PCWSTR status = Wh_GetStringSetting(L"StatusClick");
    g_settings.statusClick =
        ParseClickAction(status, ClickAction::QuickSettings);
    Wh_FreeStringSetting(status);

    g_settings.cornerEnabled = Wh_GetIntSetting(L"CornerEnabled") != 0;

    PCWSTR corner = Wh_GetStringSetting(L"Corner");
    g_settings.corner = Corner::TopRight;
    if (_wcsicmp(corner, L"topLeft") == 0) {
        g_settings.corner = Corner::TopLeft;
    } else if (_wcsicmp(corner, L"bottomRight") == 0) {
        g_settings.corner = Corner::BottomRight;
    } else if (_wcsicmp(corner, L"bottomLeft") == 0) {
        g_settings.corner = Corner::BottomLeft;
    }
    Wh_FreeStringSetting(corner);

    g_settings.cornerSize = Wh_GetIntSetting(L"CornerSize");
    g_settings.dwellMs = Wh_GetIntSetting(L"DwellMs");
    if (g_settings.cornerSize < 1) {
        g_settings.cornerSize = 1;
    }
}

// ---------------------------------------------------------------------------
// Worker thread: subclasses trays, polls the hot corner
// ---------------------------------------------------------------------------

bool GetCornerZone(POINT pt, RECT* zone) {
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfo(monitor, &mi)) {
        return false;
    }

    RECT rc = mi.rcMonitor;
    int s = g_settings.cornerSize;

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

DWORD WINAPI WorkerThread(LPVOID) {
    LoadSettings();
    SubclassAllTrays();

    MSG msg;
    PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);  // Force the queue.

    UINT_PTR cornerTimer = SetTimer(nullptr, 0, 50, nullptr);

    DWORD dwellStart = 0;
    bool armed = true;

    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.hwnd == nullptr) {
            switch (msg.message) {
                case WM_TIMER:
                    if (msg.wParam != cornerTimer) {
                        break;
                    }
                    // Resubclass trays created later (e.g. a new monitor's
                    // secondary taskbar).
                    SubclassAllTrays();

                    if (!g_settings.cornerEnabled) {
                        break;
                    }

                    {
                        POINT pt;
                        if (!GetCursorPos(&pt)) {
                            break;
                        }
                        RECT zone;
                        if (!GetCornerZone(pt, &zone)) {
                            break;
                        }

                        RECT resetZone = zone;
                        InflateRect(&resetZone, 32, 32);
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
                                OpenQuickSettings();
                                armed = false;
                                dwellStart = 0;
                            }
                        } else {
                            dwellStart = 0;
                        }
                    }
                    break;

                case WM_RELOAD:
                    LoadSettings();
                    SubclassAllTrays();
                    armed = true;
                    dwellStart = 0;
                    break;
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    KillTimer(nullptr, cornerTimer);
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

    UnsubclassAllTrays();
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    Wh_Log(L">");

    if (g_threadId) {
        PostThreadMessage(g_threadId, WM_RELOAD, 0, 0);
    }

    *bReload = FALSE;
    return TRUE;
}
