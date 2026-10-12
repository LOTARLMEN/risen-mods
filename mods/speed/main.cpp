// Speed: toggle game speed with a hotkey (like Gothic's Z-mod speedup). Settings: mods\speed.ini (hot-reloaded)
// or the Risen Mods menu (F10, menu.dll), which also draws the hourglass while the speed-up is on.
// Uses eCTimer::GetInstance().SetTimeScaling(float), exported by Engine.dll; called on the game thread only.

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "common.h"
#include "hub_client.h"

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
    "; 1 = normal speed while a dialog is running (the speed-up comes back after it)\r\n"
    "NormalInDialogs=0\r\n"
    "\r\n"
    "[Steps]\r\n"
    "; Keys that change the speed by StepUp / StepDown, between x1 and x10. Faster also switches the speed-up on.\r\n"
    "; The chosen speed is saved as Multiplier.\r\n"
    "Enabled=0\r\n"
    "FasterKey=NUMPAD+\r\n"
    "SlowerKey=NUMPAD-\r\n"
    "StepUp=0.5\r\n"
    "StepDown=0.5\r\n"
    "\r\n"
    "[Indicator]\r\n"
    "; Hourglass + x3 in the top-left corner while the speed-up is on (needs the Risen Mods menu, menu.dll)\r\n"
    "Show=1\r\n";

struct Config {
    bool enabled = true;
    float multiplier = 3.0f;
    int toggleKey = 'Z';
    bool normalInDialogs = false;
    bool steps = false;
    int fasterKey = VK_ADD;
    int slowerKey = VK_SUBTRACT;
    float stepUp = 0.5f;
    float stepDown = 0.5f;
    bool indicator = true;
};

static Config g_config;
static WNDPROC g_origProc;

constexpr UINT WM_SPEEDMOD_TOGGLE = WM_APP + 0x5201;
constexpr UINT WM_SPEEDMOD_TICK = WM_APP + 0x5202;
constexpr UINT WM_SPEEDMOD_RELOAD = WM_APP + 0x5203;
constexpr UINT WM_SPEEDMOD_STEP = WM_APP + 0x5204; // wParam 1 = faster, 0 = slower

static float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void ConfigLoad()
{
    Config c;
    c.enabled = IniInt("Speed", "Enabled", 1) != 0;
    c.multiplier = Clamp(IniFloat("Speed", "Multiplier", 3.0f), 0.1f, 10.0f);
    c.toggleKey = IniKey("Speed", "ToggleKey", 'Z');
    c.normalInDialogs = IniInt("Speed", "NormalInDialogs", 0) != 0;
    c.steps = IniInt("Steps", "Enabled", 0) != 0;
    c.fasterKey = IniKey("Steps", "FasterKey", VK_ADD);
    c.slowerKey = IniKey("Steps", "SlowerKey", VK_SUBTRACT);
    c.stepUp = Clamp(IniFloat("Steps", "StepUp", 0.5f), 0.1f, 5.0f);
    c.stepDown = Clamp(IniFloat("Steps", "StepDown", 0.5f), 0.1f, 5.0f);
    c.indicator = IniInt("Indicator", "Show", 1) != 0;
    g_config = c;
    Log("config: enabled=%d x%.2f key=0x%02X dialogs=%s | steps=%d 0x%02X 0x%02X | indicator=%d", c.enabled,
        c.multiplier, c.toggleKey, c.normalInDialogs ? "normal" : "fast", c.steps, c.fasterKey, c.slowerKey,
        c.indicator);
}

using GetInstanceFn = void* (*)();
using SetTimeScalingFn = void (*)(void* self, float scale);
using GetTimeScalingFn = float (*)(const void* self);

static GetInstanceFn g_timerInstance;
static SetTimeScalingFn g_setTimeScale;
static GetTimeScalingFn g_getTimeScale;
static bool (*g_isDialogCamera)(); // Script.dll Entity::IsDialogCameraActive, may be missing
static bool g_active;
static bool g_inDialog; // sped up, but held at normal speed for a dialog

// Script.dll may load after us: looked up on first use.
static bool InDialog()
{
    static bool tried;
    if (!g_isDialogCamera && !tried) {
        if (HMODULE script = GetModuleHandleA("Script.dll")) {
            tried = true;
            g_isDialogCamera =
                reinterpret_cast<bool (*)()>(GetProcAddress(script, "?IsDialogCameraActive@Entity@@SA_NXZ"));
            if (!g_isDialogCamera)
                Log("speed: Entity::IsDialogCameraActive not found, NormalInDialogs has no effect");
        }
    }
    return g_isDialogCamera && g_isDialogCamera();
}

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
    g_inDialog = false;
    float scale = g_active ? g_config.multiplier : 1.0f;
    g_setTimeScale(g_timerInstance(), scale);
    Log("speed: %s (x%.2f)", g_active ? "ON" : "OFF", scale);
}

// Step keys: the multiplier goes up / down by the configured step, x1 .. x10 (never slower than normal).
// Faster while off switches the speed-up on first. The new value is saved as Multiplier (menu slider follows).
static void SpeedStep(int dir)
{
    if (!g_timerInstance || !g_config.enabled || !g_config.steps)
        return;
    if (!g_active) {
        if (dir > 0)
            SpeedToggle();
        return;
    }
    float m = g_config.multiplier;
    float next = Clamp(m + (dir > 0 ? g_config.stepUp : -g_config.stepDown), 1.0f, 10.0f);
    next = std::round(next * 10.0f) / 10.0f; // keep the shown value clean (x2.5, not x2.4999)
    if (std::fabs(next - m) < 0.001f)
        return; // at x1 or x10 already
    g_config.multiplier = next;
    IniWriteFloat("Speed", "Multiplier", next);
    if (!g_inDialog)
        g_setTimeScale(g_timerInstance(), next);
    Log("speed: step to x%.2f", next);
}

static void SpeedTick()
{
    // While the menu is open it holds the time scale near zero (pause) and restores it on close.
    if (!g_timerInstance || !g_active || HubBusy())
        return;
    void* timer = g_timerInstance();
    if (!g_config.enabled) {
        g_active = false;
        g_inDialog = false;
        g_setTimeScale(timer, 1.0f);
        Log("speed: OFF (disabled)");
        return;
    }
    bool dialog = g_config.normalInDialogs && InDialog();
    if (dialog != g_inDialog) {
        g_inDialog = dialog;
        if (dialog)
            g_setTimeScale(timer, 1.0f);
        Log("speed: %s", dialog ? "dialog - normal speed" : "dialog over - sped up again");
    }
    if (g_inDialog)
        return;
    // Re-apply when the game reset the scale to normal (loading etc.) or the multiplier changed.
    // Values below 1.0 are the game's own slow motion - leave them alone.
    float current = g_getTimeScale(timer);
    if (current >= 0.999f && std::fabs(current - g_config.multiplier) > 0.001f)
        g_setTimeScale(timer, g_config.multiplier);
}

// ---------------------------------------------------------------------------------------------
// Risen Mods menu

static const HubItem kItems[] = {
    {HubCheckbox, "Speed", "Enabled", "Speed-up enabled", "Ускорение включено", 1, 0, 0, 0, 0, nullptr, -1},
    {HubSlider, "Speed", "Multiplier", "Speed", "Скорость", 3.0f, 1.0f, 10.0f, 0.1f, 1.0f, "x%.1f", 0},
    {HubKey, "Speed", "ToggleKey", "Toggle key", "Клавиша", 'Z', 0, 0, 0, 0, nullptr, 0,
     "Avoid F3, F5-F9, F12: the game uses them", "Не берите F3, F5-F9, F12: их использует игра"},
    {HubCheckbox, "Speed", "NormalInDialogs", "Normal speed in dialogs", "Обычная скорость в диалогах", 0, 0, 0, 0,
     0, nullptr, 0, "The speed-up pauses while you talk and comes back after the dialog",
     "Ускорение замирает на время разговора и возвращается после диалога"},
    {HubCheckbox, "Indicator", "Show", "Hourglass while sped up", "Песочные часы при ускорении", 1, 0, 0, 0, 0,
     nullptr, 0},
    {HubHeader, nullptr, nullptr, "Step keys", "Клавиши шага"},
    /* 6 */ {HubCheckbox, "Steps", "Enabled", "Change the speed with keys", "Менять скорость клавишами", 0, 0, 0, 0,
             0, nullptr, 0, "Between x1 and x10. Faster also switches the speed-up on.",
             "От x1 до x10. «Быстрее» заодно включает ускорение."},
    {HubKey, "Steps", "FasterKey", "Faster", "Быстрее", VK_ADD, 0, 0, 0, 0, nullptr, 6},
    {HubSlider, "Steps", "StepUp", "Step faster", "Шаг быстрее", 0.5f, 0.1f, 5.0f, 0.1f, 1.0f, "+%.1f", 6},
    {HubKey, "Steps", "SlowerKey", "Slower", "Медленнее", VK_SUBTRACT, 0, 0, 0, 0, nullptr, 6},
    {HubSlider, "Steps", "StepDown", "Step slower", "Шаг медленнее", 0.5f, 0.1f, 5.0f, 0.1f, 1.0f, "-%.1f", 6},
};

static HubPage g_page = {"speed", 10, "Speed", "Ускорение", nullptr, kItems, sizeof(kItems) / sizeof(kItems[0]),
                         ConfigLoad};

static bool StatusQuery(char* text, int size)
{
    if (!g_active || !g_config.indicator)
        return false;
    if (g_inDialog && size > 1) { // held at normal speed for a dialog: dimmed hourglass
        *text++ = HUB_STATUS_DIM;
        --size;
    }
    snprintf(text, size, "x%.2f", g_config.multiplier);
    for (char* end = text + strlen(text) - 1; *end == '0' || *end == '.'; --end) { // "x2.00" -> "x2"
        bool dot = *end == '.';
        *end = 0;
        if (dot)
            break;
    }
    return true;
}

static const HubStatus kStatus = {"speed", 10, HubIconHourglass, StatusQuery};

// ---------------------------------------------------------------------------------------------

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SPEEDMOD_TOGGLE:
        SpeedToggle();
        return 0;
    case WM_SPEEDMOD_STEP:
        SpeedStep(static_cast<int>(wp) ? 1 : -1);
        return 0;
    case WM_SPEEDMOD_TICK:
        SpeedTick();
        return 0;
    case WM_SPEEDMOD_RELOAD:
        ConfigLoad();
        return 0;
    }
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

// Edge detection for a hotkey polled with GetAsyncKeyState.
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
    if (!SpeedInit())
        return 0;
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    if (const HubApi* hub = HubGet()) {
        hub->registerPage(&g_page);
        hub->registerStatus(&kStatus);
    }
    Log("speed: ready (%s)", HubGet() ? "menu found" : "no menu.dll: ini only, no hourglass");

    KeyEdge toggle, faster, slower;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool focused = GetForegroundWindow() == hwnd;
        bool busy = HubBusy();
        if (toggle.Pressed(g_config.toggleKey, focused) && !busy)
            PostMessageW(hwnd, WM_SPEEDMOD_TOGGLE, 0, 0); // the timer is touched on the game thread only
        bool steps = g_config.steps;
        if (faster.Pressed(steps ? g_config.fasterKey : 0, focused) && !busy)
            PostMessageW(hwnd, WM_SPEEDMOD_STEP, 1, 0);
        if (slower.Pressed(steps ? g_config.slowerKey : 0, focused) && !busy)
            PostMessageW(hwnd, WM_SPEEDMOD_STEP, 0, 0);
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
        Log("Speed 1.5");
        g_page.iniPath = IniPath(kDefaultIni);
        ConfigLoad();
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
