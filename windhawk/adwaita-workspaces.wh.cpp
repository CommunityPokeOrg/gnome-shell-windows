// ==WindhawkMod==
// @id              adwaita-workspaces
// @name            Adwaita Workspaces (GNOME dynamic virtual desktops)
// @description     Makes Windows virtual desktops behave like GNOME workspaces: one empty workspace always trails, empty middle workspaces are removed, and Ctrl+Alt+Arrow hotkeys switch/move windows between them
// @version         1.0
// @author          CommunityPoke
// @github          https://github.com/CommunityPoke
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -luuid -luser32 -ldwmapi
// ==/WindhawkMod==

// Source code is published under The GNU General Public License v3.0.
//
// Part of the Windhawk port of the "Adwaita Shell" Electron prototype:
// https://github.com/CommunityPokeOrg/gnome-shell-windows
//
// Virtual desktop GUIDs/vtables are the well-known values also used by
// VD.ahk (https://github.com/FuPeiJiang/VD.ahk) and Windhawk mods such as
// gnome-dynamic-desktops.

// ==WindhawkModReadme==
/*
# Adwaita Workspaces (GNOME dynamic virtual desktops)

Reproduces the workspace behavior of the Electron prototype (and of GNOME
Shell) on top of real Windows virtual desktops:

* **Dynamic workspaces** — there is always exactly one empty workspace at the
  end. Moving or opening a window there immediately grows the strip; a
  workspace that becomes empty in the middle is removed; with no windows at
  all the system shrinks back to a single workspace.
* **Directional switching** — `Ctrl+Alt+Left/Right` moves between workspaces,
  like the prototype's `Ctrl+Alt+←/→`.
* **Move window** — `Ctrl+Alt+Shift+Left/Right` moves the focused window to
  the adjacent workspace and (optionally) follows it, like the prototype's
  `Ctrl+Alt+Shift+←/→`.

Windows 10 and Windows 11 are supported; both the current (26100+) and the
older virtual desktop COM interfaces are handled.

**Warning:** while enabled, your virtual desktops are managed — custom
desktop names and extra desktops you created are normalized away, matching
GNOME behavior.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- DynamicDesktops: true
  $name: Dynamic workspaces
  $description: >-
    Keep exactly one empty workspace at the end and remove empty workspaces
    in the middle, like GNOME. Disable to only get the hotkeys.
- DelayedDeletion: true
  $name: Don't delete empty workspace immediately
  $description: >-
    When a workspace becomes empty while you're on it, it is only removed
    after you switch to another workspace. When disabled it is removed
    as soon as it empties.
- SwitchHotkeys: true
  $name: Ctrl+Alt+Left/Right workspace switching
- MoveHotkeys: true
  $name: Ctrl+Alt+Shift+Left/Right window moving
- FollowAfterMove: true
  $name: Follow moved window
  $description: >-
    When a window is moved to an adjacent workspace with
    Ctrl+Alt+Shift+Left/Right, also switch to that workspace (the GNOME
    behavior).
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>

#include <dwmapi.h>
#include <ObjectArray.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Virtual desktop COM interfaces
// ---------------------------------------------------------------------------

const GUID CLSID_ImmersiveShell = {
    0xc2f03a33, 0x21f5, 0x47fa,
    {0xb4, 0xbb, 0x15, 0x63, 0x62, 0xa2, 0xf2, 0x39}};
const GUID CLSID_VirtualDesktopManagerInternal = {
    0xc5e0cdca, 0x7b6e, 0x41b2,
    {0x9f, 0xc4, 0xd9, 0x39, 0x75, 0xcc, 0x46, 0x7b}};

// Windows 11 26100+ layout.
const GUID IID_IVirtualDesktop_New = {
    0x3f07f4be, 0xb107, 0x441a,
    {0xaf, 0x0f, 0x39, 0xd8, 0x25, 0x29, 0x07, 0x2c}};
const GUID IID_IVirtualDesktopManagerInternal_New = {
    0x53f5ca0b, 0x158f, 0x4124,
    {0x90, 0x0c, 0x05, 0x71, 0x58, 0x06, 0x0b, 0x27}};

// Older Windows 10/11 layout (VD.ahk values).
const GUID IID_IVirtualDesktop_Old = {
    0xff72ffdd, 0xbe7e, 0x43fc,
    {0x9c, 0x03, 0xad, 0x81, 0x68, 0x1e, 0x88, 0xe4}};
const GUID IID_IVirtualDesktopManagerInternal_Old = {
    0xf31574d6, 0xb682, 0x4cdc,
    {0xbd, 0x56, 0x18, 0x27, 0x86, 0x0a, 0xbe, 0xc6}};

// 26100+ vtable: both IVirtualDesktop variants only need GetId at index 4.
DECLARE_INTERFACE_IID_(IVirtualDesktop, IUnknown,
                       "3f07f4be-b107-441a-af0f-39d82529072c") {
    STDMETHOD(Dummy3)() PURE;
    STDMETHOD(GetId)(GUID * pGuid) PURE;
};

DECLARE_INTERFACE_IID_(IVirtualDesktopManagerInternalNew, IUnknown,
                       "53f5ca0b-158f-4124-900c-057158060b27") {
    STDMETHOD(Dummy3)() PURE;
    STDMETHOD(MoveViewToDesktop)(IUnknown* pView,
                                 IVirtualDesktop* pDesktop) PURE;
    STDMETHOD(Dummy5)() PURE;
    STDMETHOD(GetCurrentDesktop)(IVirtualDesktop * *desktop) PURE;
    STDMETHOD(GetDesktops)(IObjectArray * *desktops) PURE;
    STDMETHOD(Dummy8)() PURE;
    STDMETHOD(SwitchDesktop)(IVirtualDesktop * desktop) PURE;
    STDMETHOD(Dummy10)() PURE;
    STDMETHOD(CreateDesktop)(IVirtualDesktop * *newDesktop) PURE;
    STDMETHOD(Dummy12)() PURE;
    STDMETHOD(RemoveDesktop)
    (IVirtualDesktop* desktop, IVirtualDesktop* fallback) PURE;
};

// Pre-26100 vtable.
DECLARE_INTERFACE_IID_(IVirtualDesktopManagerInternalOld, IUnknown,
                       "f31574d6-b682-4cdc-bd56-1827860abec6") {
    STDMETHOD(GetCount)(int* count) PURE;
    STDMETHOD(MoveViewToDesktop)(IUnknown* pView,
                                 IVirtualDesktop* pDesktop) PURE;
    STDMETHOD(CanViewMoveDesktops)(IUnknown* pView) PURE;
    STDMETHOD(GetCurrentDesktop)(IVirtualDesktop * *desktop) PURE;
    STDMETHOD(GetDesktops)(IObjectArray * *desktops) PURE;
    STDMETHOD(DummyGetAdjacent)() PURE;
    STDMETHOD(SwitchDesktop)(IVirtualDesktop * desktop) PURE;
    STDMETHOD(CreateDesktop)(IVirtualDesktop * *newDesktop) PURE;
    STDMETHOD(RemoveDesktop)
    (IVirtualDesktop* desktop, IVirtualDesktop* fallback) PURE;
    STDMETHOD(FindDesktop)(const GUID* id, IVirtualDesktop** desktop) PURE;
};

IVirtualDesktopManagerInternalNew* g_desktopManagerNew = nullptr;
IVirtualDesktopManagerInternalOld* g_desktopManagerOld = nullptr;
IVirtualDesktopManager* g_publicDesktopManager = nullptr;  // SDK type.

const IID& DesktopIID() {
    return g_desktopManagerNew ? IID_IVirtualDesktop_New
                               : IID_IVirtualDesktop_Old;
}

HRESULT VdGetCurrentDesktop(IVirtualDesktop** pp) {
    if (g_desktopManagerNew) {
        return g_desktopManagerNew->GetCurrentDesktop(pp);
    }
    return g_desktopManagerOld->GetCurrentDesktop(pp);
}
HRESULT VdGetDesktops(IObjectArray** pp) {
    if (g_desktopManagerNew) {
        return g_desktopManagerNew->GetDesktops(pp);
    }
    return g_desktopManagerOld->GetDesktops(pp);
}
HRESULT VdSwitchDesktop(IVirtualDesktop* d) {
    if (g_desktopManagerNew) {
        return g_desktopManagerNew->SwitchDesktop(d);
    }
    return g_desktopManagerOld->SwitchDesktop(d);
}
HRESULT VdCreateDesktop(IVirtualDesktop** pp) {
    if (g_desktopManagerNew) {
        return g_desktopManagerNew->CreateDesktop(pp);
    }
    return g_desktopManagerOld->CreateDesktop(pp);
}
HRESULT VdRemoveDesktop(IVirtualDesktop* d, IVirtualDesktop* fallback) {
    if (g_desktopManagerNew) {
        return g_desktopManagerNew->RemoveDesktop(d, fallback);
    }
    return g_desktopManagerOld->RemoveDesktop(d, fallback);
}

void CleanupCOM() {
    if (g_desktopManagerNew) {
        g_desktopManagerNew->Release();
        g_desktopManagerNew = nullptr;
    }
    if (g_desktopManagerOld) {
        g_desktopManagerOld->Release();
        g_desktopManagerOld = nullptr;
    }
    if (g_publicDesktopManager) {
        g_publicDesktopManager->Release();
        g_publicDesktopManager = nullptr;
    }
}

bool InitializeCOM() {
    if ((g_desktopManagerNew || g_desktopManagerOld) &&
        g_publicDesktopManager) {
        return true;
    }

    CleanupCOM();

    IServiceProvider* pServiceProvider = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ImmersiveShell, nullptr,
                                  CLSCTX_LOCAL_SERVER,
                                  __uuidof(IServiceProvider),
                                  (void**)&pServiceProvider);
    if (SUCCEEDED(hr) && pServiceProvider) {
        if (FAILED(pServiceProvider->QueryService(
                CLSID_VirtualDesktopManagerInternal,
                IID_IVirtualDesktopManagerInternal_New,
                (void**)&g_desktopManagerNew))) {
            // Fall back to the pre-26100 interface.
            pServiceProvider->QueryService(
                CLSID_VirtualDesktopManagerInternal,
                IID_IVirtualDesktopManagerInternal_Old,
                (void**)&g_desktopManagerOld);
        }
        pServiceProvider->Release();
    }

    CoCreateInstance(__uuidof(VirtualDesktopManager), nullptr,
                     CLSCTX_INPROC_SERVER, __uuidof(IVirtualDesktopManager),
                     (void**)&g_publicDesktopManager);

    if ((!g_desktopManagerNew && !g_desktopManagerOld) ||
        !g_publicDesktopManager) {
        Wh_Log(L"Virtual desktop COM init failed");
        CleanupCOM();
        return false;
    }

    Wh_Log(L"Virtual desktop ABI: %s",
           g_desktopManagerNew ? L"26100+" : L"legacy");
    return true;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

struct {
    bool dynamicDesktops;
    bool delayedDeletion;
    bool switchHotkeys;
    bool moveHotkeys;
    bool followAfterMove;
} g_settings;

void LoadSettings() {
    g_settings.dynamicDesktops =
        Wh_GetIntSetting(L"DynamicDesktops") != 0;
    g_settings.delayedDeletion =
        Wh_GetIntSetting(L"DelayedDeletion") != 0;
    g_settings.switchHotkeys = Wh_GetIntSetting(L"SwitchHotkeys") != 0;
    g_settings.moveHotkeys = Wh_GetIntSetting(L"MoveHotkeys") != 0;
    g_settings.followAfterMove =
        Wh_GetIntSetting(L"FollowAfterMove") != 0;
}

// ---------------------------------------------------------------------------
// Window -> desktop bookkeeping
// ---------------------------------------------------------------------------

bool IsShellClass(HWND hwnd) {
    static const wchar_t* kShellClasses[] = {
        L"Progman",
        L"WorkerW",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"Windows.UI.Core.CoreWindow",
        L"XamlExplorerHostIslandWindow",
        L"MultitaskingViewFrame",
        L"NotifyIconOverflowWindow",
        L"Windows.UI.Composition.DesktopWindowContentBridge",
    };

    WCHAR className[64];
    if (!GetClassNameW(hwnd, className, ARRAYSIZE(className))) {
        return false;
    }
    for (const wchar_t* name : kShellClasses) {
        if (_wcsicmp(className, name) == 0) {
            return true;
        }
    }
    return false;
}

bool IsAppWindow(HWND hwnd, const GUID& currentDesktopId) {
    if (!IsWindowVisible(hwnd)) {
        return false;
    }
    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
        return false;
    }
    if (GetWindow(hwnd, GW_OWNER)) {
        return false;
    }
    if (hwnd == GetShellWindow() || IsShellClass(hwnd)) {
        return false;
    }

    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked,
                                        sizeof(cloaked)))) {
        if (cloaked & 1) {  // DWM_CLOAKED_APP: background UWP ghost.
            return false;
        }
        if (cloaked & 2) {  // DWM_CLOAKED_SHELL: window on another desktop.
            GUID winDesktopId{};
            if (SUCCEEDED(g_publicDesktopManager->GetWindowDesktopId(
                    hwnd, &winDesktopId)) &&
                IsEqualGUID(winDesktopId, currentDesktopId)) {
                return false;
            }
        }
    }

    return true;
}

struct EnumCtx {
    std::vector<GUID> windowDesktops;
    int candidates = 0;
    GUID currentDesktopId{};
};

BOOL CALLBACK CollectWindowDesktops(HWND hwnd, LPARAM lParam) {
    auto* ctx = (EnumCtx*)lParam;
    if (IsAppWindow(hwnd, ctx->currentDesktopId)) {
        ctx->candidates++;
        GUID id{};
        if (SUCCEEDED(
                g_publicDesktopManager->GetWindowDesktopId(hwnd, &id))) {
            ctx->windowDesktops.push_back(id);
        }
    }
    return TRUE;
}

// ---------------------------------------------------------------------------
// GNOME workspace normalization
// ---------------------------------------------------------------------------

GUID g_lastActiveDesktop{};
std::atomic<bool> g_evaluating{false};

void EvaluateDesktops() {
    if (g_evaluating.exchange(true)) {
        return;
    }
    struct Guard {
        ~Guard() { g_evaluating = false; }
    } guard;

    if (!g_settings.dynamicDesktops || !InitializeCOM()) {
        return;
    }

    IObjectArray* pDesktops = nullptr;
    if (FAILED(VdGetDesktops(&pDesktops)) || !pDesktops) {
        Wh_Log(L"GetDesktops failed, skipping");
        CleanupCOM();
        return;
    }

    UINT count = 0;
    pDesktops->GetCount(&count);
    if (count == 0) {
        pDesktops->Release();
        return;
    }

    IVirtualDesktop* pCurrent = nullptr;
    GUID currentId{};
    if (FAILED(VdGetCurrentDesktop(&pCurrent)) || !pCurrent ||
        FAILED(pCurrent->GetId(&currentId))) {
        if (pCurrent) {
            pCurrent->Release();
        }
        pDesktops->Release();
        return;
    }
    pCurrent->Release();

    bool switchedToAnotherDesktop =
        !IsEqualGUID(currentId, g_lastActiveDesktop);
    if (switchedToAnotherDesktop) {
        g_lastActiveDesktop = currentId;
    }

    EnumCtx ctx;
    ctx.currentDesktopId = currentId;
    EnumWindows(CollectWindowDesktops, (LPARAM)&ctx);

    if (ctx.candidates > 0 && ctx.windowDesktops.empty()) {
        Wh_Log(L"No windows mapped to desktops, skipping to be safe");
        pDesktops->Release();
        return;
    }

    std::vector<IVirtualDesktop*> desktops;
    std::vector<GUID> ids;
    std::vector<int> windowCounts;

    for (UINT i = 0; i < count; i++) {
        IVirtualDesktop* pDesktop = nullptr;
        if (FAILED(pDesktops->GetAt(i, DesktopIID(),
                                   (void**)&pDesktop)) ||
            !pDesktop) {
            continue;
        }
        GUID id{};
        if (FAILED(pDesktop->GetId(&id))) {
            pDesktop->Release();
            continue;
        }
        desktops.push_back(pDesktop);
        ids.push_back(id);
        windowCounts.push_back(
            (int)std::count_if(ctx.windowDesktops.begin(),
                               ctx.windowDesktops.end(), [&](const GUID& w) {
                                   return IsEqualGUID(w, id);
                               }));
    }
    pDesktops->Release();

    int total = (int)desktops.size();
    if (total == 0) {
        return;
    }

    int lastPopulated = -1;
    for (int i = total - 1; i >= 0; i--) {
        if (windowCounts[i] > 0) {
            lastPopulated = i;
            break;
        }
    }

    // GNOME: exactly one empty workspace after the last populated one.
    int desired = lastPopulated >= 0 ? lastPopulated + 2 : 1;

    if (total < desired) {
        IVirtualDesktop* pNew = nullptr;
        if (SUCCEEDED(VdCreateDesktop(&pNew)) && pNew) {
            Wh_Log(L"Created trailing empty workspace");
            pNew->Release();
        }
    } else {
        for (int i = total - 1; i >= desired; i--) {
            bool isCurrent = IsEqualGUID(ids[i], currentId);
            if (g_settings.delayedDeletion && isCurrent &&
                lastPopulated >= 0) {
                // Keep the current empty trailing workspace until the user
                // leaves it.
                continue;
            }
            IVirtualDesktop* fallback =
                desktops[lastPopulated >= 0 ? lastPopulated : 0];
            Wh_Log(L"Removing empty trailing workspace %d", i + 1);
            VdRemoveDesktop(desktops[i], fallback);
        }
    }

    // Empty workspaces wedged between populated ones collapse, GNOME-style.
    if (lastPopulated > 0) {
        IVirtualDesktop* fallback = desktops[lastPopulated];
        for (int i = 0; i < lastPopulated; i++) {
            if (windowCounts[i] != 0) {
                continue;
            }
            bool isCurrent = IsEqualGUID(ids[i], currentId);
            if (g_settings.delayedDeletion &&
                (isCurrent || !switchedToAnotherDesktop)) {
                continue;
            }
            Wh_Log(L"Removing empty middle workspace %d", i + 1);
            VdRemoveDesktop(desktops[i], fallback);
        }
    }

    for (IVirtualDesktop* d : desktops) {
        d->Release();
    }
}

// ---------------------------------------------------------------------------
// Directional hotkeys
// ---------------------------------------------------------------------------

// Collects the ordered desktop list; returns the index of the current
// desktop or -1.
int GetDesktopList(std::vector<IVirtualDesktop*>* out,
                   std::vector<GUID>* ids) {
    out->clear();
    ids->clear();

    if (!InitializeCOM()) {
        return -1;
    }

    IObjectArray* pDesktops = nullptr;
    if (FAILED(VdGetDesktops(&pDesktops)) || !pDesktops) {
        return -1;
    }

    IVirtualDesktop* pCurrent = nullptr;
    GUID currentId{};
    bool haveCurrent =
        SUCCEEDED(VdGetCurrentDesktop(&pCurrent)) && pCurrent &&
        SUCCEEDED(pCurrent->GetId(&currentId));
    if (pCurrent) {
        pCurrent->Release();
    }

    UINT count = 0;
    pDesktops->GetCount(&count);

    int currentIndex = -1;
    for (UINT i = 0; i < count; i++) {
        IVirtualDesktop* pDesktop = nullptr;
        if (FAILED(pDesktops->GetAt(i, DesktopIID(),
                                   (void**)&pDesktop)) ||
            !pDesktop) {
            continue;
        }
        GUID id{};
        pDesktop->GetId(&id);
        if (haveCurrent && IsEqualGUID(id, currentId)) {
            currentIndex = (int)out->size();
        }
        out->push_back(pDesktop);
        ids->push_back(id);
    }
    pDesktops->Release();

    return currentIndex;
}

void SwitchDesktopRelative(int delta) {
    std::vector<IVirtualDesktop*> desktops;
    std::vector<GUID> ids;
    int current = GetDesktopList(&desktops, &ids);
    int target = current + delta;

    if (current >= 0 && target >= 0 && target < (int)desktops.size()) {
        Wh_Log(L"Switching to workspace %d", target + 1);
        VdSwitchDesktop(desktops[target]);
    }

    for (IVirtualDesktop* d : desktops) {
        d->Release();
    }
}

void MoveForegroundWindowRelative(int delta) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return;
    }

    std::vector<IVirtualDesktop*> desktops;
    std::vector<GUID> ids;
    int current = GetDesktopList(&desktops, &ids);
    int target = current + delta;

    if (current >= 0 && target >= 0 && target < (int)desktops.size()) {
        Wh_Log(L"Moving window to workspace %d", target + 1);
        if (SUCCEEDED(g_publicDesktopManager->MoveWindowToDesktop(
                hwnd, ids[target])) &&
            g_settings.followAfterMove) {
            VdSwitchDesktop(desktops[target]);
        }
    }

    for (IVirtualDesktop* d : desktops) {
        d->Release();
    }
}

// ---------------------------------------------------------------------------
// Worker thread: WinEvent hooks + timers + hotkeys
// ---------------------------------------------------------------------------

enum {
    WM_RELOAD = WM_APP,
    kHotkeySwitchLeft = 1,
    kHotkeySwitchRight,
    kHotkeyMoveLeft,
    kHotkeyMoveRight,
};

const UINT_PTR kEvaluateTimer = 1;   // Debounced evaluation.
const UINT_PTR kPollTimer = 2;       // Light poll for external changes.

HANDLE g_thread = nullptr;
DWORD g_threadId = 0;
std::atomic<bool> g_unloading{false};

void ScheduleEvaluate() {
    if (g_threadId) {
        // Coalesce bursts of events into a single evaluation.
        KillTimer(nullptr, kEvaluateTimer);
        SetTimer(nullptr, kEvaluateTimer, 150, nullptr);
    }
}

void CALLBACK WinEventProc(HWINEVENTHOOK hook,
                           DWORD event,
                           HWND hwnd,
                           LONG idObject,
                           LONG idChild,
                           DWORD thread,
                           DWORD time) {
    if (idObject != OBJID_WINDOW || !hwnd) {
        return;
    }
    ScheduleEvaluate();
}

// Cheap probe used by the poll timer: returns false when the desktop
// topology (count or current desktop) changed since last time.
bool DesktopTopologyUnchanged() {
    static UINT lastCount = 0;

    if (!g_desktopManagerNew && !g_desktopManagerOld) {
        return true;
    }

    IObjectArray* pDesktops = nullptr;
    if (FAILED(VdGetDesktops(&pDesktops)) || !pDesktops) {
        return true;
    }
    UINT count = 0;
    pDesktops->GetCount(&count);
    pDesktops->Release();

    IVirtualDesktop* pCurrent = nullptr;
    GUID currentId{};
    bool haveCurrent =
        SUCCEEDED(VdGetCurrentDesktop(&pCurrent)) && pCurrent &&
        SUCCEEDED(pCurrent->GetId(&currentId));
    if (pCurrent) {
        pCurrent->Release();
    }

    bool unchanged = haveCurrent &&
                     IsEqualGUID(currentId, g_lastActiveDesktop) &&
                     count == lastCount;
    lastCount = count;
    return unchanged;
}

void RegisterHotkeys() {
    UnregisterHotKey(nullptr, kHotkeySwitchLeft);
    UnregisterHotKey(nullptr, kHotkeySwitchRight);
    UnregisterHotKey(nullptr, kHotkeyMoveLeft);
    UnregisterHotKey(nullptr, kHotkeyMoveRight);

    if (g_settings.switchHotkeys) {
        RegisterHotKey(nullptr, kHotkeySwitchLeft,
                       MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_LEFT);
        RegisterHotKey(nullptr, kHotkeySwitchRight,
                       MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_RIGHT);
    }
    if (g_settings.moveHotkeys) {
        RegisterHotKey(nullptr, kHotkeyMoveLeft,
                       MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                       VK_LEFT);
        RegisterHotKey(nullptr, kHotkeyMoveRight,
                       MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,
                       VK_RIGHT);
    }
}

DWORD WINAPI WorkerThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadSettings();
    InitializeCOM();

    MSG msg;
    PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);  // Force the queue.

    RegisterHotkeys();

    HWINEVENTHOOK hookFg =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    HWINEVENTHOOK hookShowHide =
        SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr,
                        WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    HWINEVENTHOOK hookShow =
        SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                        WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    // Catch desktop switches performed outside this mod (Win+Tab, trackpad
    // gestures) so dynamic normalization still runs.
    SetTimer(nullptr, kPollTimer, 1000, nullptr);

    EvaluateDesktops();
    Wh_Log(L"Workspace thread started");

    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.hwnd == nullptr) {
            switch (msg.message) {
                case WM_HOTKEY:
                    switch (msg.wParam) {
                        case kHotkeySwitchLeft:
                            SwitchDesktopRelative(-1);
                            break;
                        case kHotkeySwitchRight:
                            SwitchDesktopRelative(+1);
                            break;
                        case kHotkeyMoveLeft:
                            MoveForegroundWindowRelative(-1);
                            break;
                        case kHotkeyMoveRight:
                            MoveForegroundWindowRelative(+1);
                            break;
                    }
                    ScheduleEvaluate();
                    break;

                case WM_TIMER:
                    if (msg.wParam == kEvaluateTimer) {
                        KillTimer(nullptr, kEvaluateTimer);
                        EvaluateDesktops();
                    } else if (msg.wParam == kPollTimer) {
                        if (!DesktopTopologyUnchanged()) {
                            Wh_Log(L"Desktop topology changed");
                            EvaluateDesktops();
                        }
                    }
                    break;

                case WM_RELOAD:
                    LoadSettings();
                    RegisterHotkeys();
                    EvaluateDesktops();
                    break;
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hookFg) {
        UnhookWinEvent(hookFg);
    }
    if (hookShowHide) {
        UnhookWinEvent(hookShowHide);
    }
    if (hookShow) {
        UnhookWinEvent(hookShow);
    }
    KillTimer(nullptr, kEvaluateTimer);
    KillTimer(nullptr, kPollTimer);

    CleanupCOM();
    CoUninitialize();
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
