// ==WindhawkMod==
// @id              adwaita-clock
// @name            Adwaita Clock (GNOME top-bar clock)
// @description     Replaces the taskbar clock with the GNOME top-bar clock format, e.g. "Wed Sep 30  9:41 AM" on a single line
// @version         1.0
// @author          CommunityPoke
// @github          https://github.com/CommunityPoke
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lversion
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0.
//
// Part of the Windhawk port of the "Adwaita Shell" Electron prototype:
// https://github.com/CommunityPokeOrg/gnome-shell-windows

// ==WindhawkModReadme==
/*
# Adwaita Clock (GNOME top-bar clock)

Renders the taskbar clock the way GNOME Shell draws the clock in the center of
its top bar: abbreviated weekday and date followed by the time, e.g.:

    Wed Sep 30  9:41 AM

By default the date and time are combined into a single line (the GNOME top
bar has a one-line clock). Disable "Combine date and time" to keep the
taskbar's two-line layout, in which case the GNOME date format is used for the
second line.

The clock's tooltip is left untouched.

## How it works

* On Windows 11, the mod hooks `GetTimeFormatEx`/`GetDateFormatEx` in
  kernelbase.dll and rewrites the format while the system tray clock
  (`SystemTray.dll` / `Taskbar.View.dll` / `ExplorerExtensions.dll`) is
  refreshing, matching how the tray calls into those APIs.
* On Windows 10, symbol hooks on `explorer.exe`'s `ClockButton` detect the
  clock update (and tooltip) threads and the same format hooks do the
  rest.

After changing the format settings, the new text shows up on the next clock
refresh (up to a minute). A best-effort repaint of the Windows 10 clock window
is also issued.

Only Windows 10/11 x64 are supported.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- CombineLines: true
  $name: Combine date and time
  $description: >-
    Show the GNOME-style single line "%date%  %time%" as the clock text and
    collapse the second taskbar line. When disabled, the first line keeps the
    time format below and the second line shows the GNOME date format.
- TimeFormat: h':'mm tt
  $name: Time format
  $description: >-
    GetTimeFormatEx picture string used for the time part. Leave empty for the
    system default. Syntax:
    https://learn.microsoft.com/windows/win32/api/datetimeapi/nf-datetimeapi-gettimeformatex
- DateFormat: ddd MMM d
  $name: Date format
  $description: >-
    GetDateFormatEx picture string used for the date part (GNOME shows
    "ddd MMM d", e.g. "Wed Sep 30"). Leave empty for the system default.
    Syntax:
    https://learn.microsoft.com/windows/win32/intl/day--month--year--and-era-format-pictures
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <atomic>

using WindhawkUtils::StringSetting;

enum class WinVersion {
    Unsupported,
    Win10,
    Win11,
};

WinVersion g_winVersion = WinVersion::Unsupported;

struct {
    StringSetting timeFormat;
    StringSetting dateFormat;
    bool combineLines;
} g_settings;

// Thread ids captured while the system tray code path is running.
std::atomic<DWORD> g_trayClockThreadId{0};   // Win11 ClockSystemTrayIconDataModel
std::atomic<DWORD> g_clockButtonThreadId{0}; // Win10 ClockButton
std::atomic<DWORD> g_tooltipThreadId{0};     // Win10 ClockButton tooltip
std::atomic<int> g_inToolTipString{0};       // Win11 GetTimeToolTipString*
std::atomic<int> g_dateFormatCallIndex{0};   // Date calls per Win10 update

std::atomic<bool> g_initialized{false};
std::atomic<bool> g_unloading{false};
std::atomic<bool> g_systemTrayModuleHooked{false};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

VS_FIXEDFILEINFO* GetModuleVersionInfo(HMODULE hModule, UINT* puPtrLen) {
    void* pFixedFileInfo = nullptr;
    UINT uPtrLen = 0;

    HRSRC hResource =
        FindResource(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
    if (hResource) {
        HGLOBAL hGlobal = LoadResource(hModule, hResource);
        if (hGlobal) {
            void* pData = LockResource(hGlobal);
            if (pData) {
                if (!VerQueryValue(pData, L"\\", &pFixedFileInfo, &uPtrLen) ||
                    uPtrLen == 0) {
                    pFixedFileInfo = nullptr;
                    uPtrLen = 0;
                }
            }
        }
    }

    if (puPtrLen) {
        *puPtrLen = uPtrLen;
    }

    return (VS_FIXEDFILEINFO*)pFixedFileInfo;
}

WinVersion GetExplorerVersion() {
    VS_FIXEDFILEINFO* fixedFileInfo = GetModuleVersionInfo(nullptr, nullptr);
    if (!fixedFileInfo) {
        return WinVersion::Unsupported;
    }

    WORD major = HIWORD(fixedFileInfo->dwFileVersionMS);
    WORD build = HIWORD(fixedFileInfo->dwFileVersionLS);

    Wh_Log(L"Explorer version: %u.%u.%u.%u", major,
           LOWORD(fixedFileInfo->dwFileVersionMS), build,
           LOWORD(fixedFileInfo->dwFileVersionLS));

    if (major != 10) {
        return WinVersion::Unsupported;
    }
    return build < 22000 ? WinVersion::Win10 : WinVersion::Win11;
}

// Returns the base name of the module containing the return address, if any.
// Used as a fallback gate when symbol hooks aren't available.
bool IsWin11TrayModuleAddress(void* address) {
    HMODULE module = nullptr;
    if (!RtlPcToFileHeader(address, (void**)&module) || !module) {
        return false;
    }

    WCHAR path[MAX_PATH];
    switch (GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        case 0:
        case ARRAYSIZE(path):
            return false;
    }

    PCWSTR name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;

    return _wcsicmp(name, L"SystemTray.dll") == 0 ||
           _wcsicmp(name, L"Taskbar.View.dll") == 0 ||
           _wcsicmp(name, L"ExplorerExtensions.dll") == 0;
}

bool IsClockFormattingCall(void* returnAddress) {
    if (g_inToolTipString.load() > 0) {
        return false;
    }

    DWORD tid = GetCurrentThreadId();
    if (g_tooltipThreadId.load() == tid) {
        return false;
    }

    if (g_winVersion >= WinVersion::Win11) {
        return g_trayClockThreadId.load() == tid ||
               IsWin11TrayModuleAddress(returnAddress);
    }

    return g_clockButtonThreadId.load() == tid;
}

// Fills the caller's buffer with a string built from the GNOME-style formats.
// Returns the amount written (or the amount required when cchStr is 0), in
// characters including the terminating null, like Get*FormatEx.
int FormatCombinedDateTime(LPWSTR buffer, int cchStr) {
    WCHAR datePart[128] = L"";
    WCHAR timePart[64] = L"";

    SYSTEMTIME st;
    GetLocalTime(&st);

    if (wcslen(g_settings.dateFormat.get()) > 0) {
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st,
                        g_settings.dateFormat.get(), datePart,
                        ARRAYSIZE(datePart), nullptr);
    } else {
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr,
                        datePart, ARRAYSIZE(datePart), nullptr);
    }

    if (wcslen(g_settings.timeFormat.get()) > 0) {
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st,
                        g_settings.timeFormat.get(), timePart,
                        ARRAYSIZE(timePart));
    } else {
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr,
                        timePart, ARRAYSIZE(timePart));
    }

    // GNOME separates the date and the time with two spaces.
    int needed = (int)wcslen(datePart) + 2 + (int)wcslen(timePart) + 1;
    if (!cchStr) {
        return needed;
    }
    if (cchStr < needed) {
        return 0;
    }

    swprintf_s(buffer, cchStr, L"%s  %s", datePart, timePart);
    return needed;
}

// Writes an empty string so a secondary line collapses.
int FormatEmpty(LPWSTR buffer, int cchStr) {
    if (!cchStr) {
        return 1;
    }
    buffer[0] = L'\0';
    return 1;
}

void LoadSettings() {
    g_settings.timeFormat = StringSetting::make(L"TimeFormat");
    g_settings.dateFormat = StringSetting::make(L"DateFormat");
    g_settings.combineLines = Wh_GetIntSetting(L"CombineLines") != 0;
}

void RefreshClockWindow() {
    // Windows 10: repaint the classic clock window immediately.
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) {
        return;
    }
    HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
    if (!notify) {
        return;
    }
    HWND clock = FindWindowExW(notify, nullptr, L"TrayClockWClass", nullptr);
    if (clock) {
        InvalidateRect(clock, nullptr, TRUE);
    }
}

// ---------------------------------------------------------------------------
// Format hooks (kernelbase)
// ---------------------------------------------------------------------------

using GetTimeFormatEx_t = decltype(&GetTimeFormatEx);
GetTimeFormatEx_t GetTimeFormatEx_Original;
int WINAPI GetTimeFormatEx_Hook(LPCWSTR lpLocaleName,
                                DWORD dwFlags,
                                const SYSTEMTIME* lpTime,
                                LPCWSTR lpFormat,
                                LPWSTR lpTimeStr,
                                int cchTime) {
    if (!g_unloading.load() &&
        IsClockFormattingCall(__builtin_return_address(0))) {
        if (g_settings.combineLines) {
            return FormatCombinedDateTime(lpTimeStr, cchTime);
        }
        if (wcslen(g_settings.timeFormat.get()) > 0) {
            return GetTimeFormatEx_Original(lpLocaleName, dwFlags, lpTime,
                                            g_settings.timeFormat.get(),
                                            lpTimeStr, cchTime);
        }
    }

    return GetTimeFormatEx_Original(lpLocaleName, dwFlags, lpTime, lpFormat,
                                    lpTimeStr, cchTime);
}

using GetDateFormatEx_t = decltype(&GetDateFormatEx);
GetDateFormatEx_t GetDateFormatEx_Original;
int WINAPI GetDateFormatEx_Hook(LPCWSTR lpLocaleName,
                                DWORD dwFlags,
                                const SYSTEMTIME* lpDate,
                                LPCWSTR lpFormat,
                                LPWSTR lpDateStr,
                                int cchDate,
                                LPCWSTR lpCalendar) {
    if (!g_unloading.load() &&
        IsClockFormattingCall(__builtin_return_address(0))) {
        if (g_settings.combineLines) {
            // The GNOME top bar has a single-line clock; collapse the
            // date/secondary lines.
            return FormatEmpty(lpDateStr, cchDate);
        }
        // Only the first date call of a Win10 update produces a visible line.
        int index = g_winVersion == WinVersion::Win10
                        ? g_dateFormatCallIndex.fetch_add(1)
                        : 0;
        if (index == 0 && wcslen(g_settings.dateFormat.get()) > 0) {
            return GetDateFormatEx_Original(lpLocaleName, dwFlags, lpDate,
                                            g_settings.dateFormat.get(),
                                            lpDateStr, cchDate, lpCalendar);
        }
        if (index > 0) {
            return FormatEmpty(lpDateStr, cchDate);
        }
    }

    return GetDateFormatEx_Original(lpLocaleName, dwFlags, lpDate, lpFormat,
                                    lpDateStr, cchDate, lpCalendar);
}

// ---------------------------------------------------------------------------
// Windows 10 ClockButton symbol hooks (explorer.exe)
// ---------------------------------------------------------------------------

using ClockButton_UpdateTextStringsIfNecessary_t =
    unsigned int(WINAPI*)(void* pThis, bool* param);
ClockButton_UpdateTextStringsIfNecessary_t
    ClockButton_UpdateTextStringsIfNecessary_Original;
unsigned int WINAPI
ClockButton_UpdateTextStringsIfNecessary_Hook(void* pThis, bool* param) {
    g_clockButtonThreadId = GetCurrentThreadId();
    g_dateFormatCallIndex = 0;
    return ClockButton_UpdateTextStringsIfNecessary_Original(pThis, param);
}

using ClockButton_v_GetTooltipText_t =
    long(WINAPI*)(void* pThis, HINSTANCE**, WCHAR**, WCHAR*, unsigned __int64);
ClockButton_v_GetTooltipText_t ClockButton_v_GetTooltipText_Original;
long WINAPI ClockButton_v_GetTooltipText_Hook(void* pThis,
                                              HINSTANCE** hInstance,
                                              WCHAR** ppszText,
                                              WCHAR* pszText,
                                              unsigned __int64 size) {
    g_tooltipThreadId = GetCurrentThreadId();
    long ret = ClockButton_v_GetTooltipText_Original(pThis, hInstance, ppszText,
                                                   pszText, size);
    g_tooltipThreadId = 0;
    return ret;
}

bool HookWin10TaskbarSymbols() {
    WindhawkUtils::SYMBOL_HOOK explorerExeHooks[] = {
        {
            {LR"(private: unsigned int __cdecl ClockButton::UpdateTextStringsIfNecessary(bool *))"},
            &ClockButton_UpdateTextStringsIfNecessary_Original,
            ClockButton_UpdateTextStringsIfNecessary_Hook,
        },
        {
            {LR"(protected: virtual long __cdecl ClockButton::v_GetTooltipText(struct HINSTANCE__ * *,unsigned short * *,unsigned short *,unsigned __int64))"},
            &ClockButton_v_GetTooltipText_Original,
            ClockButton_v_GetTooltipText_Hook,
            true,  // Optional; tooltip just keeps its own formats without it.
        },
    };

    if (!WindhawkUtils::HookSymbols(GetModuleHandle(nullptr),
                                    explorerExeHooks,
                                    ARRAYSIZE(explorerExeHooks))) {
        Wh_Log(L"HookSymbols failed");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Windows 11 SystemTray symbol hooks (SystemTray.dll / Taskbar.View.dll /
// ExplorerExtensions.dll). All optional: without them, the return-address
// module check still gates the format hooks.
// ---------------------------------------------------------------------------

using ClockSystemTrayIconDataModel_RefreshIcon_t =
    void(WINAPI*)(void* pThis, void* clockUpdate);
ClockSystemTrayIconDataModel_RefreshIcon_t
    ClockSystemTrayIconDataModel_RefreshIcon_Original;
ClockSystemTrayIconDataModel_RefreshIcon_t
    ClockSystemTrayIconDataModel2_RefreshIcon_Original;
void WINAPI ClockSystemTrayIconDataModel_RefreshIcon_Hook(void* pThis,
                                                          void* clockUpdate) {
    g_trayClockThreadId = GetCurrentThreadId();
    ClockSystemTrayIconDataModel_RefreshIcon_Original(pThis, clockUpdate);
}
void WINAPI ClockSystemTrayIconDataModel2_RefreshIcon_Hook(
    void* pThis,
    void* clockUpdate) {
    g_trayClockThreadId = GetCurrentThreadId();
    ClockSystemTrayIconDataModel2_RefreshIcon_Original(pThis, clockUpdate);
}

// hstring is a single-pointer struct returned in RAX, so `void*` is an
// ABI-compatible return type for these hooks.
using ClockSystemTrayIconDataModel_GetTimeToolTipString_t =
    void*(WINAPI*)(void* pThis, const SYSTEMTIME* st1, const SYSTEMTIME* st2,
                   void* clockUpdate);
ClockSystemTrayIconDataModel_GetTimeToolTipString_t
    ClockSystemTrayIconDataModel_GetTimeToolTipString_Original;
ClockSystemTrayIconDataModel_GetTimeToolTipString_t
    ClockSystemTrayIconDataModel_GetTimeToolTipString2_Original;
ClockSystemTrayIconDataModel_GetTimeToolTipString_t
    ClockSystemTrayIconDataModel_GetTimeToolTipString_2_Original;
ClockSystemTrayIconDataModel_GetTimeToolTipString_t
    ClockSystemTrayIconDataModel2_GetTimeToolTipString_Original;
ClockSystemTrayIconDataModel_GetTimeToolTipString_t
    ClockSystemTrayIconDataModel2_GetTimeToolTipString2_Original;
ClockSystemTrayIconDataModel_GetTimeToolTipString_t
    ClockSystemTrayIconDataModel2_GetTimeToolTipString_2_Original;

#define DEFINE_TOOLTIP_HOOK(name, original)                                \
    void* WINAPI name(void* pThis, const SYSTEMTIME* st1,                  \
                      const SYSTEMTIME* st2, void* clockUpdate) {          \
        g_inToolTipString.fetch_add(1);                                    \
        void* ret = original(pThis, st1, st2, clockUpdate);                \
        g_inToolTipString.fetch_sub(1);                                    \
        return ret;                                                        \
    }

DEFINE_TOOLTIP_HOOK(ClockSystemTrayIconDataModel_GetTimeToolTipString_Hook,
                    ClockSystemTrayIconDataModel_GetTimeToolTipString_Original)
DEFINE_TOOLTIP_HOOK(ClockSystemTrayIconDataModel_GetTimeToolTipString2_Hook,
                    ClockSystemTrayIconDataModel_GetTimeToolTipString2_Original)
DEFINE_TOOLTIP_HOOK(ClockSystemTrayIconDataModel_GetTimeToolTipString_2_Hook,
                    ClockSystemTrayIconDataModel_GetTimeToolTipString_2_Original)
DEFINE_TOOLTIP_HOOK(ClockSystemTrayIconDataModel2_GetTimeToolTipString_Hook,
                    ClockSystemTrayIconDataModel2_GetTimeToolTipString_Original)
DEFINE_TOOLTIP_HOOK(ClockSystemTrayIconDataModel2_GetTimeToolTipString2_Hook,
                    ClockSystemTrayIconDataModel2_GetTimeToolTipString2_Original)
DEFINE_TOOLTIP_HOOK(ClockSystemTrayIconDataModel2_GetTimeToolTipString_2_Hook,
                    ClockSystemTrayIconDataModel2_GetTimeToolTipString_2_Original)

bool HookSystemTraySymbols(HMODULE module) {
    WindhawkUtils::SYMBOL_HOOK symbolHooks[] = {
        {
            {LR"(private: void __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel::RefreshIcon(class SystemTrayTelemetry::ClockUpdate &))"},
            &ClockSystemTrayIconDataModel_RefreshIcon_Original,
            ClockSystemTrayIconDataModel_RefreshIcon_Hook,
            true,
        },
        {
            {LR"(private: void __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel2::RefreshIcon(class SystemTrayTelemetry::ClockUpdate &))"},
            &ClockSystemTrayIconDataModel2_RefreshIcon_Original,
            ClockSystemTrayIconDataModel2_RefreshIcon_Hook,
            true,  // Added with feature flag 38762814
        },
        {
            {LR"(private: struct winrt::hstring __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel::GetTimeToolTipString(struct _SYSTEMTIME const &,struct _SYSTEMTIME const &,class SystemTrayTelemetry::ClockUpdate &))"},
            &ClockSystemTrayIconDataModel_GetTimeToolTipString_Original,
            ClockSystemTrayIconDataModel_GetTimeToolTipString_Hook,
            true,
        },
        {
            {LR"(private: struct winrt::hstring __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel::GetTimeToolTipString2(struct _SYSTEMTIME const &,struct _SYSTEMTIME const &,class SystemTrayTelemetry::ClockUpdate &))"},
            &ClockSystemTrayIconDataModel_GetTimeToolTipString2_Original,
            ClockSystemTrayIconDataModel_GetTimeToolTipString2_Hook,
            true,
        },
        {
            {LR"(private: struct winrt::hstring __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel2::GetTimeToolTipString(struct _SYSTEMTIME const &,struct _SYSTEMTIME const &,class SystemTrayTelemetry::ClockUpdate &))"},
            &ClockSystemTrayIconDataModel2_GetTimeToolTipString_Original,
            ClockSystemTrayIconDataModel2_GetTimeToolTipString_Hook,
            true,
        },
        {
            {LR"(private: struct winrt::hstring __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel2::GetTimeToolTipString2(struct _SYSTEMTIME const &,struct _SYSTEMTIME const &,class SystemTrayTelemetry::ClockUpdate &))"},
            &ClockSystemTrayIconDataModel2_GetTimeToolTipString2_Original,
            ClockSystemTrayIconDataModel2_GetTimeToolTipString2_Hook,
            true,
        },
        {
            {LR"(private: struct winrt::hstring __cdecl winrt::SystemTray::implementation::ClockSystemTrayIconDataModel::GetTimeToolTipString(struct _SYSTEMTIME *,struct _TIME_DYNAMIC_ZONE_INFORMATION *,class SystemTrayTelemetry::ClockUpdate &))"},
            &ClockSystemTrayIconDataModel_GetTimeToolTipString_2_Original,
            ClockSystemTrayIconDataModel_GetTimeToolTipString_2_Hook,
            true,  // Until Windows 11 version 21H2.
        },
    };

    if (!WindhawkUtils::HookSymbols(module, symbolHooks,
                                    ARRAYSIZE(symbolHooks))) {
        Wh_Log(L"HookSymbols failed for system tray module");
        return false;
    }

    return true;
}

HMODULE GetSystemTrayModuleHandle() {
    HMODULE module = GetModuleHandle(L"SystemTray.dll");
    if (!module) {
        module = GetModuleHandle(L"Taskbar.View.dll");
        if (module) {
            // Starting with Taskbar.View.dll 2604.8002.200.6000, the
            // SystemTray types moved into SystemTray.dll.
            VS_FIXEDFILEINFO* fixedFileInfo =
                GetModuleVersionInfo(module, nullptr);
            WORD moduleMajor =
                fixedFileInfo ? HIWORD(fixedFileInfo->dwFileVersionMS) : 0;
            if (!moduleMajor || moduleMajor >= 2604) {
                module = nullptr;
            }
        }
    }
    if (!module) {
        module = GetModuleHandle(L"ExplorerExtensions.dll");
    }

    return module;
}

void HandleLoadedModuleIfSystemTray(HMODULE module, LPCWSTR lpLibFileName) {
    if (g_winVersion < WinVersion::Win11 || g_unloading.load() ||
        g_systemTrayModuleHooked.load() || !module) {
        return;
    }

    if (GetSystemTrayModuleHandle() != module) {
        return;
    }

    bool expected = false;
    if (!g_systemTrayModuleHooked.compare_exchange_strong(expected, true)) {
        return;
    }

    Wh_Log(L"System tray module loaded");

    if (HookSystemTraySymbols(module)) {
        Wh_ApplyHookOperations();
    }
}

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_Original;
HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR lpLibFileName,
                                   HANDLE hFile,
                                   DWORD dwFlags) {
    HMODULE module = LoadLibraryExW_Original(lpLibFileName, hFile, dwFlags);

    HandleLoadedModuleIfSystemTray(module, lpLibFileName);

    return module;
}

// ---------------------------------------------------------------------------
// Windhawk lifecycle
// ---------------------------------------------------------------------------

BOOL Wh_ModInit() {
    Wh_Log(L">");

    LoadSettings();

    g_winVersion = GetExplorerVersion();
    if (g_winVersion == WinVersion::Unsupported) {
        Wh_Log(L"Unsupported Windows version");
        return FALSE;
    }

    if (g_winVersion >= WinVersion::Win11) {
        if (HMODULE module = GetSystemTrayModuleHandle()) {
            g_systemTrayModuleHooked = true;
            HookSystemTraySymbols(module);  // Optional; fallback remains.
        } else {
            Wh_Log(L"System tray module not loaded yet");
        }
    } else {
        if (!HookWin10TaskbarSymbols()) {
            return FALSE;
        }
    }

    HMODULE kernelBaseModule = GetModuleHandle(L"kernelbase.dll");

    auto pKernelBaseLoadLibraryExW = (decltype(&LoadLibraryExW))GetProcAddress(
        kernelBaseModule, "LoadLibraryExW");
    WindhawkUtils::SetFunctionHook(pKernelBaseLoadLibraryExW,
                                   LoadLibraryExW_Hook,
                                   &LoadLibraryExW_Original);

    // Use GetProcAddress so the kernel32.dll stubs aren't hooked instead.
    WindhawkUtils::SetFunctionHook(
        (GetTimeFormatEx_t)GetProcAddress(kernelBaseModule, "GetTimeFormatEx"),
        GetTimeFormatEx_Hook, &GetTimeFormatEx_Original);
    WindhawkUtils::SetFunctionHook(
        (GetDateFormatEx_t)GetProcAddress(kernelBaseModule, "GetDateFormatEx"),
        GetDateFormatEx_Hook, &GetDateFormatEx_Original);

    g_initialized = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    Wh_Log(L">");

    if (g_winVersion >= WinVersion::Win11 &&
        !g_systemTrayModuleHooked.load()) {
        if (HMODULE module = GetSystemTrayModuleHandle()) {
            if (!g_systemTrayModuleHooked.exchange(true)) {
                Wh_Log(L"Got system tray module");

                if (HookSystemTraySymbols(module)) {
                    Wh_ApplyHookOperations();
                }
            }
        }
    }
}

void Wh_ModUninit() {
    Wh_Log(L">");

    g_unloading = true;

    // Force a refresh so the original text comes back.
    RefreshClockWindow();
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    Wh_Log(L">");

    LoadSettings();
    RefreshClockWindow();

    *bReload = FALSE;
    return TRUE;
}
