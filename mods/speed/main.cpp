// Speed: toggle game speed with a hotkey (like Gothic's Z-mod speedup). Settings: mods\speed.ini (hot-reloaded)
// or the in-game panel (F10). Uses eCTimer::GetInstance().SetTimeScaling(float), exported by Engine.dll;
// called on the game thread only.

#include <windows.h>
#include <cmath>

#include "common.h"
#include "input_capture.h"
#include "speed.h"

static const char kDefaultIni[] =
    "; Speed mod settings. Changes are picked up while the game is running.\r\n"
    "; Keys: F1-F24, A-Z, 0-9, NUMPAD0-9, NUMPAD+, NUMPAD-, NUMPAD*, NUMPAD/, SPACE, TAB, INSERT, DELETE,\r\n"
    ";       HOME, END, PAGEUP, PAGEDOWN, BACKSPACE, MOUSE3, MOUSE4, MOUSE5, or a hex code like 0x73.\r\n"
    "; Avoid F3, F5-F9, F12: the game uses them (F8 quicksave, F9 quickload, debug keys).\r\n"
    "\r\n"
    "[Speed]\r\n"
    "Enabled=1\r\n"
    "; Game speed while speed-up is on (1.0 = normal, max 10)\r\n"
    "Multiplier=3.0\r\n"
    "ToggleKey=Z\r\n"
    "\r\n"
    "[Indicator]\r\n"
    "; Hourglass + x3 in the top-left corner while the speed-up is on\r\n"
    "Show=1\r\n"
    "; Size (1.0 = normal)\r\n"
    "Scale=1.0\r\n"
    "\r\n"
    "[Panel]\r\n"
    "; Opens the settings window in game (the game is paused while it is open)\r\n"
    "Key=F10\r\n"
    "; Window language: EN or RU (the globe button in the window switches it)\r\n"
    "Language=\r\n";

static SpeedConfig g_config;
static WNDPROC g_origProc;

constexpr UINT WM_SPEEDMOD_TOGGLE = WM_APP + 0x5201;
constexpr UINT WM_SPEEDMOD_TICK = WM_APP + 0x5202;
constexpr UINT WM_SPEEDMOD_RELOAD = WM_APP + 0x5203;
constexpr UINT WM_SPEEDMOD_PANEL = WM_APP + 0x5204; // wParam: 1 open, 0 close

static float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void ConfigLoad()
{
    SpeedConfig c;
    c.enabled = IniInt("Speed", "Enabled", 1) != 0;
    c.multiplier = Clamp(IniFloat("Speed", "Multiplier", 3.0f), 0.1f, 10.0f);
    c.toggleKey = IniKey("Speed", "ToggleKey", 'Z');
    c.indicator = IniInt("Indicator", "Show", 1) != 0;
    c.indicatorScale = Clamp(IniFloat("Indicator", "Scale", 1.0f), 0.3f, 4.0f);
    c.panelKey = IniKey("Panel", "Key", VK_F10);
    char lang[8];
    GetPrivateProfileStringA("Panel", "Language", "", lang, sizeof(lang), IniPath());
    c.russian = lang[0] ? _stricmp(lang, "RU") == 0 : PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
    g_config = c;
    Log("config: enabled=%d x%.2f key=0x%02X indicator=%d scale=%.2f panel=0x%02X lang=%s", c.enabled,
        c.multiplier, c.toggleKey, c.indicator, c.indicatorScale, c.panelKey, c.russian ? "RU" : "EN");
}

const SpeedConfig& SpeedGetConfig() { return g_config; }

void SpeedApplyLive(const SpeedConfig& c) { g_config = c; }

void SpeedSetConfig(const SpeedConfig& c)
{
    g_config = c;
    IniWriteString("Speed", "Enabled", c.enabled ? "1" : "0");
    IniWriteFloat("Speed", "Multiplier", c.multiplier);
    IniWriteKey("Speed", "ToggleKey", c.toggleKey);
    IniWriteString("Indicator", "Show", c.indicator ? "1" : "0");
    IniWriteFloat("Indicator", "Scale", c.indicatorScale);
    IniWriteKey("Panel", "Key", c.panelKey);
    IniWriteString("Panel", "Language", c.russian ? "RU" : "EN");
    Log("config: saved from panel");
}

using GetInstanceFn = void* (*)();
using SetTimeScalingFn = void (*)(void* self, float scale);
using GetTimeScalingFn = float (*)(const void* self);

static GetInstanceFn g_timerInstance;
static SetTimeScalingFn g_setTimeScale;
static GetTimeScalingFn g_getTimeScale;
static bool g_active;
static bool g_frozen;
static float g_scaleBeforeFreeze = 1.0f;

static bool SpeedInit()
{
    HMODULE engine = GetModuleHandleA("Engine.dll");
    if (engine) {
        g_timerInstance = reinterpret_cast<GetInstanceFn>(GetProcAddress(engine, "?GetInstance@eCTimer@@SAAEAV1@XZ"));
        g_setTimeScale = reinterpret_cast<SetTimeScalingFn>(GetProcAddress(engine, "?SetTimeScaling@eCTimer@@QEAAXM@Z"));
        g_getTimeScale = reinterpret_cast<GetTimeScalingFn>(GetProcAddress(engine, "?GetTimeScaling@eCTimer@@QEBAMXZ"));
    }
    if (!g_timerInstance || !g_setTimeScale || !g_getTimeScale) {
        g_timerInstance = nullptr;
        Log("speed: eCTimer exports not found (game updated?), speed-up disabled");
        return false;
    }
    return true;
}

static void SpeedToggle()
{
    if (!g_timerInstance || (!g_config.enabled && !g_active))
        return;
    g_active = !g_active;
    float scale = g_active ? g_config.multiplier : 1.0f;
    g_setTimeScale(g_timerInstance(), scale);
    Log("speed: %s (x%.2f)", g_active ? "ON" : "OFF", scale);
}

void SpeedFreeze(bool freeze)
{
    if (!g_timerInstance || freeze == g_frozen)
        return;
    void* timer = g_timerInstance();
    g_frozen = freeze;
    if (freeze) {
        // gCSession::Pause blacks out the screen; a near-zero time scale keeps the picture.
        g_scaleBeforeFreeze = g_getTimeScale(timer);
        g_setTimeScale(timer, 0.001f);
    } else {
        g_active = g_active && g_config.enabled;
        float scale = g_scaleBeforeFreeze < 0.999f ? g_scaleBeforeFreeze // the game's own slow motion
                      : g_active ? g_config.multiplier : 1.0f;
        g_setTimeScale(timer, scale);
    }
    Log("speed: %s", freeze ? "frozen" : "unfrozen");
}

static void SpeedTick()
{
    if (!g_timerInstance || !g_active || g_frozen)
        return;
    void* timer = g_timerInstance();
    if (!g_config.enabled) {
        g_active = false;
        g_setTimeScale(timer, 1.0f);
        Log("speed: OFF (disabled)");
        return;
    }
    // Re-apply when the game reset the scale to normal (loading etc.) or the multiplier changed.
    // Values below 1.0 are the game's own slow motion - leave them alone.
    float current = g_getTimeScale(timer);
    if (current >= 0.999f && std::fabs(current - g_config.multiplier) > 0.001f)
        g_setTimeScale(timer, g_config.multiplier);
}

bool SpeedIsActive() { return g_active; }

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT result;
    if (InputCaptureFilter(hwnd, msg, wp, lp, &result))
        return result;
    switch (msg) {
    case WM_SPEEDMOD_TOGGLE:
        SpeedToggle();
        return 0;
    case WM_SPEEDMOD_TICK:
        SpeedTick();
        return 0;
    case WM_SPEEDMOD_RELOAD:
        ConfigLoad();
        return 0;
    case WM_SPEEDMOD_PANEL:
        PanelSetOpen(hwnd, wp != 0);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && PanelIsOpen())
            PanelSetOpen(hwnd, false); // Alt+Tab: give the game its input back
        break;
    }
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

// Edge detection for a hotkey polled with GetAsyncKeyState.
struct KeyEdge {
    int key = 0;
    bool wasDown = false;
    bool Pressed(int vk, bool focused)
    {
        bool down = focused && (GetAsyncKeyState(vk) & 0x8000);
        bool pressed = down && !wasDown && vk == key; // a key just rebound while held doesn't fire
        key = vk;
        wasDown = down;
        return pressed;
    }
};

static DWORD WINAPI Worker(LPVOID)
{
    HWND hwnd = WaitForGameWindow();
    if (!SpeedInit())
        return 0;
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    Log("speed: ready");

    KeyEdge toggle, panel, escape;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool focused = GetForegroundWindow() == hwnd;
        bool waiting = PanelIsWaitingKey(); // the panel itself reads the next key
        bool open = PanelIsOpen();
        if (toggle.Pressed(g_config.toggleKey, focused) && !open && !waiting)
            PostMessageW(hwnd, WM_SPEEDMOD_TOGGLE, 0, 0); // the timer is touched on the game thread only
        if (panel.Pressed(g_config.panelKey, focused) && !waiting)
            PostMessageW(hwnd, WM_SPEEDMOD_PANEL, !open, 0);
        if (escape.Pressed(VK_ESCAPE, focused) && open && !waiting)
            PostMessageW(hwnd, WM_SPEEDMOD_PANEL, 0, 0);
        if (tick % 5 == 0)
            PostMessageW(hwnd, WM_SPEEDMOD_TICK, 0, 0);
        if (tick % 20 == 0 && IniChanged())
            PostMessageW(hwnd, WM_SPEEDMOD_RELOAD, 0, 0);
        Sleep(30);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "speed");
        Log("Speed 1.2");
        IniPath(kDefaultIni);
        ConfigLoad();
        // Before the game creates its D3D9 device, which happens after the mod loader has run.
        Log("overlay: d3d9 hook %s", OverlayInstall() ? "installed" : "FAILED (no indicator, no panel)");
        Log("input capture: %s", InputCaptureInstall() ? "installed" : "FAILED");
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
