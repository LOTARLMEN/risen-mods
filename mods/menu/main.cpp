// Risen Mods menu: one settings window (F10) for every mod that registers a page (shared/hub_api.h), and the
// status icons in the top-left corner (Speed's hourglass, AutoLoot's hand). The game is paused while the window
// is open and gets no input. Settings: mods\menu.ini (hot-reloaded).

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>

#include "common.h"
#include "input_capture.h"
#include "menu.h"

static const char kDefaultIni[] =
    "; Risen Mods menu settings. Changes are picked up while the game is running.\r\n"
    "; Keys: F1-F24, A-Z, 0-9, NUMPAD0-9, MOUSE3, MOUSE4, MOUSE5, or a hex code like 0x79.\r\n"
    "\r\n"
    "[Menu]\r\n"
    "; Opens the settings window of all mods (the game is paused while it is open)\r\n"
    "Key=F10\r\n"
    "; EN or RU (the globe button in the window switches it)\r\n"
    "Language=\r\n"
    "\r\n"
    "[Icons]\r\n"
    "; Status icons in the top-left corner (hourglass while sped up, hand while AutoLoot is on)\r\n"
    "Show=1\r\n"
    "; Size (1.0 = normal)\r\n"
    "Scale=1.0\r\n";

static MenuConfig g_config;
static WNDPROC g_origProc;
static std::atomic<bool> g_open, g_waitingKey;
static std::atomic<int> g_wheel; // mouse wheel while the window is open, WHEEL_DELTA units

constexpr UINT WM_MENU_OPEN = WM_APP + 0x5601; // wParam: 1 open, 0 close
constexpr UINT WM_MENU_RELOAD = WM_APP + 0x5602;
constexpr UINT WM_MENU_NATIVE = WM_APP + 0x5603; // NativeTick: the "Моды" tab in the game's options

static float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void ConfigLoad()
{
    MenuConfig c;
    c.key = IniKey("Menu", "Key", VK_F10);
    c.icons = IniInt("Icons", "Show", 1) != 0;
    c.iconScale = Clamp(IniFloat("Icons", "Scale", 1.0f), 0.3f, 4.0f);
    char lang[8];
    GetPrivateProfileStringA("Menu", "Language", "", lang, sizeof(lang), IniPath());
    c.russian = lang[0] ? _stricmp(lang, "RU") == 0 : PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
    g_config = c;
    Log("config: key=0x%02X lang=%s icons=%d scale=%.2f", c.key, c.russian ? "RU" : "EN", c.icons, c.iconScale);
}

const MenuConfig& MenuGetConfig() { return g_config; }

void MenuSetLanguage(bool russian)
{
    g_config.russian = russian;
    IniWriteString("Menu", "Language", russian ? "RU" : "EN");
}

// ---------------------------------------------------------------------------------------------
// Registry

static std::mutex g_registryLock;
static std::vector<const HubPage*> g_pages;
static std::vector<const HubStatus*> g_statuses;

void RegistryLock() { g_registryLock.lock(); }
void RegistryUnlock() { g_registryLock.unlock(); }
const std::vector<const HubPage*>& Pages() { return g_pages; }
const std::vector<const HubStatus*>& Statuses() { return g_statuses; }

static void RegisterPage(const HubPage* page)
{
    if (!page || !page->id)
        return;
    std::lock_guard<std::mutex> lock(g_registryLock);
    g_pages.erase(std::remove_if(g_pages.begin(), g_pages.end(),
                                 [&](const HubPage* p) { return strcmp(p->id, page->id) == 0; }),
                  g_pages.end());
    g_pages.push_back(page);
    std::stable_sort(g_pages.begin(), g_pages.end(), [](const HubPage* a, const HubPage* b) { return a->order < b->order; });
    Log("page: %s (%d items)", page->id, page->itemCount);
}

static void RegisterStatus(const HubStatus* status)
{
    if (!status || !status->id || !status->query)
        return;
    std::lock_guard<std::mutex> lock(g_registryLock);
    g_statuses.erase(std::remove_if(g_statuses.begin(), g_statuses.end(),
                                    [&](const HubStatus* s) { return strcmp(s->id, status->id) == 0; }),
                     g_statuses.end());
    g_statuses.push_back(status);
    std::stable_sort(g_statuses.begin(), g_statuses.end(),
                     [](const HubStatus* a, const HubStatus* b) { return a->order < b->order; });
    Log("status icon: %s", status->id);
}

static bool IsBusy()
{
    return g_open || g_waitingKey;
}

static const HubApi kApi = {HUB_API_VERSION, RegisterPage, RegisterStatus, IsBusy};

extern "C" __declspec(dllexport) const HubApi* RisenHubApi()
{
    return &kApi;
}

// The menu's own page: the window key and the status icons.
static const HubItem kOwnItems[] = {
    {HubKey, "Menu", "Key", "Open this window", "Открыть это окно", VK_F10, 0, 0, 0, 0, nullptr, -1},
    {HubCheckbox, "Icons", "Show", "Status icons in the top-left corner", "Значки состояния в левом верхнем углу", 1,
     0, 0, 0, 0, nullptr, -1, "Hourglass while sped up, hand while AutoLoot is on",
     "Песочные часы при ускорении, рука при включённом автолуте"},
    {HubSlider, "Icons", "Scale", "Icon size", "Размер значков", 1.0f, 0.5f, 2.5f, 0.1f, 1.0f, "%.1f", 1},
    {HubNote, nullptr, nullptr, "The game is paused while this window is open.",
     "Пока окно открыто, игра стоит на паузе."},
};

static HubPage g_ownPage = {"menu", 1000, "Menu", "Меню", nullptr, kOwnItems, sizeof(kOwnItems) / sizeof(kOwnItems[0]),
                            ConfigLoad};

// ---------------------------------------------------------------------------------------------
// Pause: eCTimer (Engine.dll). gCSession::Pause blacks out the screen; a near-zero time scale keeps the picture.
// The Speed mod leaves scales below 1.0 alone (it takes them for the game's slow motion).

using GetInstanceFn = void* (*)();
using SetTimeScalingFn = void (*)(void* self, float scale);
using GetTimeScalingFn = float (*)(const void* self);

static GetInstanceFn g_timerInstance;
static SetTimeScalingFn g_setTimeScale;
static GetTimeScalingFn g_getTimeScale;
static float g_scaleBeforeFreeze = 1.0f;

static void TimerInit()
{
    HMODULE engine = GetModuleHandleA("Engine.dll");
    if (!engine)
        return;
    g_timerInstance = reinterpret_cast<GetInstanceFn>(GetProcAddress(engine, "?GetInstance@eCTimer@@SAAEAV1@XZ"));
    g_setTimeScale = reinterpret_cast<SetTimeScalingFn>(GetProcAddress(engine, "?SetTimeScaling@eCTimer@@QEAAXM@Z"));
    g_getTimeScale = reinterpret_cast<GetTimeScalingFn>(GetProcAddress(engine, "?GetTimeScaling@eCTimer@@QEBAMXZ"));
    if (!g_timerInstance || !g_setTimeScale || !g_getTimeScale) {
        g_timerInstance = nullptr;
        Log("pause: eCTimer exports not found, the game keeps running while the window is open");
    }
}

static void Freeze(bool freeze)
{
    if (!g_timerInstance)
        return;
    void* timer = g_timerInstance();
    if (freeze) {
        g_scaleBeforeFreeze = g_getTimeScale(timer);
        g_setTimeScale(timer, 0.001f);
    } else {
        g_setTimeScale(timer, g_scaleBeforeFreeze); // the Speed mod re-applies its own value if it changed
    }
}

// ---------------------------------------------------------------------------------------------
// Window state and input

bool MenuIsOpen() { return g_open; }
void MenuSetWaitingKey(bool waiting) { g_waitingKey = waiting; }
bool MenuIsWaitingKey() { return g_waitingKey; }

void MenuSetOpen(HWND hwnd, bool open)
{
    if (open == g_open || (open && !OverlayReady()))
        return;
    g_waitingKey = false;
    g_wheel = 0;
    if (open)
        WindowOpened();
    g_open = open;
    InputCaptureSet(hwnd, open);
    Freeze(open);
    Log("window: %s", open ? "open" : "closed");
}

// Mouse wheel for scrolling the window. The game reads the mouse with Raw Input, so the wheel is taken from
// WM_INPUT; WM_MOUSEWHEEL counts only as long as no raw wheel event was seen (no double scrolling).
static bool g_rawWheelSeen;

static void CollectWheel(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INPUT) {
        RAWINPUT raw;
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) !=
                static_cast<UINT>(-1) &&
            raw.header.dwType == RIM_TYPEMOUSE && (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)) {
            g_rawWheelSeen = true;
            g_wheel += static_cast<short>(raw.data.mouse.usButtonData);
        }
    } else if (msg == WM_MOUSEWHEEL && !g_rawWheelSeen) {
        g_wheel += GET_WHEEL_DELTA_WPARAM(wp);
    }
}

float MenuTakeWheel()
{
    return static_cast<float>(g_wheel.exchange(0)) / WHEEL_DELTA;
}

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_open)
        CollectWheel(msg, wp, lp);
    // Esc on a mod's page in the game's options goes back to the mod list instead of closing the options.
    static bool escUsed;
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE && !g_open && NativeOnEscape()) {
        escUsed = true;
        return 0;
    }
    if (msg == WM_MOUSEWHEEL && !g_open && NativeOnWheel(GET_WHEEL_DELTA_WPARAM(wp)))
        return 0;
    if (msg == WM_KEYUP && wp == VK_ESCAPE && escUsed) {
        escUsed = false;
        return 0;
    }
    LRESULT result;
    if (InputCaptureFilter(hwnd, msg, wp, lp, &result))
        return result;
    switch (msg) {
    case WM_MENU_OPEN:
        MenuSetOpen(hwnd, wp != 0);
        return 0;
    case WM_MENU_RELOAD:
        ConfigLoad();
        return 0;
    case WM_MENU_NATIVE:
        NativeTick();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && g_open)
            MenuSetOpen(hwnd, false); // Alt+Tab: give the game its input back
        break;
    }
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

struct KeyEdge {
    int key = 0;
    bool wasDown = false;
    bool Pressed(int vk, bool focused)
    {
        bool down = vk && focused && (GetAsyncKeyState(vk) & 0x8000);
        bool pressed = down && !wasDown && vk == key; // a key just rebound while held doesn't fire
        key = vk;
        wasDown = down;
        return pressed;
    }
};

static DWORD WINAPI Worker(LPVOID)
{
    HWND hwnd = WaitForGameWindow();
    TimerInit();
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    Log("menu: ready");

    KeyEdge open, escape;
    bool wasWaiting = false;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool focused = GetForegroundWindow() == hwnd;
        // Esc that cancelled a key rebind must not also close the window: skip one tick after waiting ends.
        bool waiting = g_waitingKey, busyKey = waiting || wasWaiting;
        wasWaiting = waiting;
        if (open.Pressed(g_config.key, focused) && !busyKey)
            PostMessageW(hwnd, WM_MENU_OPEN, !g_open, 0);
        if (escape.Pressed(VK_ESCAPE, focused) && g_open && !busyKey)
            PostMessageW(hwnd, WM_MENU_OPEN, 0, 0);
        if (tick % 20 == 0 && IniChanged())
            PostMessageW(hwnd, WM_MENU_RELOAD, 0, 0);
        if (tick % 3 == 0) // ~0.1 s: key rebinding on the native page polls the keyboard
            PostMessageW(hwnd, WM_MENU_NATIVE, 0, 0);
        Sleep(30);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "menu");
        Log("Risen Mods Menu 1.1.2");
        g_ownPage.iniPath = IniPath(kDefaultIni);
        ConfigLoad();
        RegisterPage(&g_ownPage);
        // Before the game creates its D3D9 device, which happens after the mod loader has run.
        Log("overlay: d3d9 hook %s", OverlayInstall() ? "installed" : "FAILED (no window, no icons)");
        Log("input capture: %s", InputCaptureInstall() ? "installed" : "FAILED");
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
