// Risen (2023, x64) mods: borderless window, hidden system cursor, game speed toggle.
// Loaded as version.dll from Risen\bin. Settings: bin\risen_mods.ini, log: bin\risen_mods.log.

#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------------------------
// Log

static char g_logPath[MAX_PATH];
static char g_iniPath[MAX_PATH];
static CRITICAL_SECTION g_logLock;

static void PathNextToExe(char* out, const char* fileName)
{
    GetModuleFileNameA(nullptr, out, MAX_PATH);
    char* slash = strrchr(out, '\\');
    char* dst = slash ? slash + 1 : out;
    strcpy_s(dst, MAX_PATH - (dst - out), fileName);
}

static void Log(const char* fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_logLock);
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") == 0 && f) {
        fprintf(f, "%02d:%02d:%02d.%03d  %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
        fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------------------------------------
// Config (risen_mods.ini, reloaded when the file changes)

struct Config {
    bool borderless = true;
    bool hideSystemCursor = true;
    bool clipCursor = true;
    bool speedEnabled = true;
    float speedMultiplier = 2.0f;
    int speedToggleKey = VK_F4;
};

static Config g_config;
static FILETIME g_iniWriteTime;

static const char kDefaultIni[] =
    "; Risen mods settings. Changes are picked up while the game is running.\r\n"
    "; Keys: F1-F24, A-Z, 0-9, NUMPAD0-9, NUMPAD+, NUMPAD-, NUMPAD*, NUMPAD/, SPACE, TAB, INSERT, DELETE,\r\n"
    ";       HOME, END, PAGEUP, PAGEDOWN, BACKSPACE, MOUSE3, MOUSE4, MOUSE5, or a hex code like 0x73.\r\n"
    "; Avoid F3, F5-F9, F12: the game uses them (quicksave/quickload, debug keys).\r\n"
    "\r\n"
    "[Window]\r\n"
    "; Borderless window covering the whole monitor. Needs FullScreen=\"false\" in data\\ini\\ConfigLocal.xml\r\n"
    "Borderless=1\r\n"
    "; Hide the Windows cursor over the game (the game draws its own)\r\n"
    "HideSystemCursor=1\r\n"
    "; Keep the mouse inside the game window while it is active\r\n"
    "ClipCursor=1\r\n"
    "\r\n"
    "[Speed]\r\n"
    "Enabled=1\r\n"
    "; Game speed while speed-up is on (1.0 = normal, max 10)\r\n"
    "Multiplier=2.0\r\n"
    "ToggleKey=F4\r\n";

static int ParseKeyName(const char* raw)
{
    char name[32];
    size_t n = 0;
    for (const char* p = raw; *p && n < sizeof(name) - 1; ++p)
        if (*p != ' ' && *p != '\t')
            name[n++] = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
    name[n] = 0;
    if (!n)
        return 0;
    if (name[0] == '0' && name[1] == 'X')
        return static_cast<int>(strtol(name + 2, nullptr, 16));
    if (n == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')))
        return name[0];
    if (name[0] == 'F' && n <= 3) {
        int f = atoi(name + 1);
        if (f >= 1 && f <= 24)
            return VK_F1 + f - 1;
    }
    if (strncmp(name, "NUMPAD", 6) == 0 && n == 7) {
        char c = name[6];
        if (c >= '0' && c <= '9') return VK_NUMPAD0 + (c - '0');
        if (c == '+') return VK_ADD;
        if (c == '-') return VK_SUBTRACT;
        if (c == '*') return VK_MULTIPLY;
        if (c == '/') return VK_DIVIDE;
    }
    static const struct { const char* name; int vk; } table[] = {
        {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
        {"HOME", VK_HOME}, {"END", VK_END}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
        {"BACKSPACE", VK_BACK}, {"CAPSLOCK", VK_CAPITAL}, {"ENTER", VK_RETURN},
        {"MOUSE3", VK_MBUTTON}, {"MMB", VK_MBUTTON}, {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2},
    };
    for (auto& k : table)
        if (strcmp(name, k.name) == 0)
            return k.vk;
    return 0;
}

static void ConfigLoad()
{
    Config c;
    char buf[64];
    c.borderless = GetPrivateProfileIntA("Window", "Borderless", 1, g_iniPath) != 0;
    c.hideSystemCursor = GetPrivateProfileIntA("Window", "HideSystemCursor", 1, g_iniPath) != 0;
    c.clipCursor = GetPrivateProfileIntA("Window", "ClipCursor", 1, g_iniPath) != 0;
    c.speedEnabled = GetPrivateProfileIntA("Speed", "Enabled", 1, g_iniPath) != 0;

    GetPrivateProfileStringA("Speed", "Multiplier", "2.0", buf, sizeof(buf), g_iniPath);
    c.speedMultiplier = static_cast<float>(atof(buf));
    if (c.speedMultiplier < 0.1f) c.speedMultiplier = 0.1f;
    if (c.speedMultiplier > 10.0f) c.speedMultiplier = 10.0f;

    GetPrivateProfileStringA("Speed", "ToggleKey", "F4", buf, sizeof(buf), g_iniPath);
    if (int vk = ParseKeyName(buf))
        c.speedToggleKey = vk;
    else
        Log("config: unknown ToggleKey '%s', using F4", buf);

    g_config = c;
    Log("config: borderless=%d hideCursor=%d clipCursor=%d speed=%d x%.2f key=0x%02X",
        c.borderless, c.hideSystemCursor, c.clipCursor, c.speedEnabled, c.speedMultiplier, c.speedToggleKey);
}

static bool IniWriteTime(FILETIME* out)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &data))
        return false;
    *out = data.ftLastWriteTime;
    return true;
}

static void ConfigInit()
{
    if (GetFileAttributesA(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        if (fopen_s(&f, g_iniPath, "wb") == 0 && f) {
            fwrite(kDefaultIni, 1, sizeof(kDefaultIni) - 1, f);
            fclose(f);
            Log("config: created default risen_mods.ini");
        }
    }
    IniWriteTime(&g_iniWriteTime);
    ConfigLoad();
}

static void ConfigReloadIfChanged()
{
    FILETIME now;
    if (!IniWriteTime(&now) || CompareFileTime(&now, &g_iniWriteTime) == 0)
        return;
    g_iniWriteTime = now;
    Sleep(50); // let the editor finish writing
    ConfigLoad();
}

// ---------------------------------------------------------------------------------------------
// Speed: eCTimer::GetInstance().SetTimeScaling(float), exported by Engine.dll. Game thread only.

using GetInstanceFn = void* (*)();
using SetTimeScalingFn = void (*)(void* self, float scale);
using GetTimeScalingFn = float (*)(const void* self);

static GetInstanceFn g_timerInstance;
static SetTimeScalingFn g_setTimeScale;
static GetTimeScalingFn g_getTimeScale;
static bool g_speedActive;

static void SpeedInit()
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
        return;
    }
    Log("speed: eCTimer found");
}

static void SpeedToggle()
{
    if (!g_timerInstance || (!g_config.speedEnabled && !g_speedActive))
        return;
    g_speedActive = !g_speedActive;
    float scale = g_speedActive ? g_config.speedMultiplier : 1.0f;
    g_setTimeScale(g_timerInstance(), scale);
    Log("speed: %s (x%.2f)", g_speedActive ? "ON" : "OFF", scale);
}

static void SpeedTick()
{
    if (!g_timerInstance || !g_speedActive)
        return;
    void* timer = g_timerInstance();
    if (!g_config.speedEnabled) {
        g_speedActive = false;
        g_setTimeScale(timer, 1.0f);
        Log("speed: OFF (disabled in ini)");
        return;
    }
    // Re-apply when the game reset the scale to normal (loading etc.) or the multiplier changed in the ini.
    // Values below 1.0 are the game's own slow motion - leave them alone.
    float current = g_getTimeScale(timer);
    if (current >= 0.999f && std::fabs(current - g_config.speedMultiplier) > 0.001f)
        g_setTimeScale(timer, g_config.speedMultiplier);
}

// ---------------------------------------------------------------------------------------------
// Window: subclassed window procedure (runs on the game thread), borderless, cursor.

constexpr UINT WM_RISENMODS_TOGGLE_SPEED = WM_APP + 0x5201;
constexpr UINT WM_RISENMODS_TICK = WM_APP + 0x5202;

static WNDPROC g_origProc;
static bool g_unicodeWindow;

static HWND FindGameWindow()
{
    // Title comes from <Window Name="RISEN"> in data\ini\ConfigDefault.xml.
    HWND hwnd = nullptr;
    while ((hwnd = FindWindowExA(nullptr, hwnd, nullptr, "RISEN")) != nullptr) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd))
            return hwnd;
    }
    return nullptr;
}

static void ClipToWindow(HWND hwnd, bool clip)
{
    if (!clip || !g_config.clipCursor) {
        ClipCursor(nullptr);
        return;
    }
    RECT r;
    GetClientRect(hwnd, &r);
    MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    ClipCursor(&r);
}

static void EnforceBorderless(HWND hwnd)
{
    if (!g_config.borderless || IsIconic(hwnd))
        return;
    const LONG_PTR frame = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    const LONG_PTR exFrame = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& m = mi.rcMonitor;
    RECT wr;
    GetWindowRect(hwnd, &wr);

    bool hasFrame = (style & frame) || (exStyle & exFrame);
    bool wrongRect = wr.left != m.left || wr.top != m.top || wr.right != m.right || wr.bottom != m.bottom;
    if (!hasFrame && !wrongRect)
        return;
    if (hasFrame) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~frame) | WS_POPUP);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle & ~exFrame);
    }
    SetWindowPos(hwnd, nullptr, m.left, m.top, m.right - m.left, m.bottom - m.top,
                 SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    if (GetForegroundWindow() == hwnd)
        ClipToWindow(hwnd, true);
    Log("window: borderless %ldx%ld at %ld,%ld", m.right - m.left, m.bottom - m.top, m.left, m.top);
}

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_RISENMODS_TOGGLE_SPEED:
        SpeedToggle();
        return 0;
    case WM_RISENMODS_TICK:
        EnforceBorderless(hwnd);
        SpeedTick();
        return 0;
    case WM_SETCURSOR:
        if (g_config.hideSystemCursor && LOWORD(lp) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_ACTIVATE:
        ClipToWindow(hwnd, LOWORD(wp) != WA_INACTIVE);
        break;
    }
    return g_unicodeWindow ? CallWindowProcW(g_origProc, hwnd, msg, wp, lp)
                           : CallWindowProcA(g_origProc, hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------------------------
// Worker thread

static DWORD WINAPI Worker(LPVOID)
{
    Log("risen_mods loaded");
    ConfigInit();

    HWND hwnd = nullptr;
    while (!(hwnd = FindGameWindow()))
        Sleep(200);

    SpeedInit();
    g_unicodeWindow = IsWindowUnicode(hwnd) != FALSE;
    LONG_PTR newProc = reinterpret_cast<LONG_PTR>(HookedWndProc);
    g_origProc = reinterpret_cast<WNDPROC>(g_unicodeWindow ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, newProc)
                                                           : SetWindowLongPtrA(hwnd, GWLP_WNDPROC, newProc));
    Log("window: hooked %p (%s)", hwnd, g_unicodeWindow ? "unicode" : "ansi");

    bool keyWasDown = false;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool focused = GetForegroundWindow() == hwnd;
        bool keyDown = focused && (GetAsyncKeyState(g_config.speedToggleKey) & 0x8000);
        if (keyDown && !keyWasDown)
            PostMessageW(hwnd, WM_RISENMODS_TOGGLE_SPEED, 0, 0);
        keyWasDown = keyDown;

        if (tick % 5 == 0)
            PostMessageW(hwnd, WM_RISENMODS_TICK, 0, 0);
        if (tick % 20 == 0)
            ConfigReloadIfChanged();
        Sleep(50);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_logLock);
        PathNextToExe(g_logPath, "risen_mods.log");
        PathNextToExe(g_iniPath, "risen_mods.ini");
        FILE* f = nullptr;
        if (fopen_s(&f, g_logPath, "w") == 0 && f) // fresh log every launch
            fclose(f);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
