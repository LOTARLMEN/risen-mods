// Speed: toggle game speed with a hotkey (like Gothic's Z-mod speedup). Settings: mods\speed.ini (hot-reloaded).
// Uses eCTimer::GetInstance().SetTimeScaling(float), exported by Engine.dll; called on the game thread only.

#include <windows.h>
#include <cmath>

#include "common.h"
#include "overlay.h"

static const char kDefaultIni[] =
    "; Speed mod settings. Changes are picked up while the game is running.\r\n"
    "; Keys: F1-F24, A-Z, 0-9, NUMPAD0-9, NUMPAD+, NUMPAD-, NUMPAD*, NUMPAD/, SPACE, TAB, INSERT, DELETE,\r\n"
    ";       HOME, END, PAGEUP, PAGEDOWN, BACKSPACE, MOUSE3, MOUSE4, MOUSE5, or a hex code like 0x73.\r\n"
    "; Avoid F3, F5-F9, F12: the game uses them (F8 quicksave, F9 quickload, debug keys).\r\n"
    "\r\n"
    "[Speed]\r\n"
    "Enabled=1\r\n"
    "; Game speed while speed-up is on (1.0 = normal, max 10)\r\n"
    "Multiplier=2.0\r\n"
    "ToggleKey=F4\r\n"
    "\r\n"
    "[Indicator]\r\n"
    "; Hourglass + x2 in the top-left corner while the speed-up is on\r\n"
    "Show=1\r\n"
    "; Size (1.0 = normal)\r\n"
    "Scale=1.0\r\n";

struct Config {
    bool enabled = true;
    float multiplier = 2.0f;
    int toggleKey = VK_F4;
    bool indicator = true;
    float indicatorScale = 1.0f;
};

static Config g_config;
static WNDPROC g_origProc;

constexpr UINT WM_SPEEDMOD_TOGGLE = WM_APP + 0x5201;
constexpr UINT WM_SPEEDMOD_TICK = WM_APP + 0x5202;

static void ConfigLoad()
{
    Config c;
    c.enabled = IniInt("Speed", "Enabled", 1) != 0;
    c.multiplier = IniFloat("Speed", "Multiplier", 2.0f);
    if (c.multiplier < 0.1f) c.multiplier = 0.1f;
    if (c.multiplier > 10.0f) c.multiplier = 10.0f;
    c.toggleKey = IniKey("Speed", "ToggleKey", VK_F4);
    c.indicator = IniInt("Indicator", "Show", 1) != 0;
    c.indicatorScale = IniFloat("Indicator", "Scale", 1.0f);
    if (c.indicatorScale < 0.3f) c.indicatorScale = 0.3f;
    if (c.indicatorScale > 4.0f) c.indicatorScale = 4.0f;
    g_config = c;
    Log("config: enabled=%d x%.2f key=0x%02X indicator=%d scale=%.2f", c.enabled, c.multiplier, c.toggleKey,
        c.indicator, c.indicatorScale);
}

using GetInstanceFn = void* (*)();
using SetTimeScalingFn = void (*)(void* self, float scale);
using GetTimeScalingFn = float (*)(const void* self);

static GetInstanceFn g_timerInstance;
static SetTimeScalingFn g_setTimeScale;
static GetTimeScalingFn g_getTimeScale;
static bool g_active;

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

static void SpeedTick()
{
    if (!g_timerInstance || !g_active)
        return;
    void* timer = g_timerInstance();
    if (!g_config.enabled) {
        g_active = false;
        g_setTimeScale(timer, 1.0f);
        Log("speed: OFF (disabled in ini)");
        return;
    }
    // Re-apply when the game reset the scale to normal (loading etc.) or the multiplier changed in the ini.
    // Values below 1.0 are the game's own slow motion - leave them alone.
    float current = g_getTimeScale(timer);
    if (current >= 0.999f && std::fabs(current - g_config.multiplier) > 0.001f)
        g_setTimeScale(timer, g_config.multiplier);
}

bool SpeedIsActive() { return g_active; }
float SpeedMultiplier() { return g_config.multiplier; }
bool OverlayEnabled() { return g_config.indicator; }
float OverlayScale() { return g_config.indicatorScale; }

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SPEEDMOD_TOGGLE:
        SpeedToggle();
        return 0;
    case WM_SPEEDMOD_TICK:
        SpeedTick();
        return 0;
    }
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

static DWORD WINAPI Worker(LPVOID)
{
    HWND hwnd = WaitForGameWindow();
    if (!SpeedInit())
        return 0;
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    Log("speed: ready");

    bool keyWasDown = false;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool focused = GetForegroundWindow() == hwnd;
        bool keyDown = focused && (GetAsyncKeyState(g_config.toggleKey) & 0x8000);
        if (keyDown && !keyWasDown)
            PostMessageW(hwnd, WM_SPEEDMOD_TOGGLE, 0, 0); // the timer is touched on the game thread only
        keyWasDown = keyDown;
        if (tick % 5 == 0)
            PostMessageW(hwnd, WM_SPEEDMOD_TICK, 0, 0);
        if (tick % 20 == 0 && IniChanged())
            ConfigLoad();
        Sleep(50);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "speed");
        Log("Speed 1.1");
        IniPath(kDefaultIni);
        ConfigLoad();
        // Before the game creates its D3D9 device, which happens after the mod loader has run.
        Log("overlay: d3d9 hook %s", OverlayInstall() ? "installed" : "FAILED (no indicator)");
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
