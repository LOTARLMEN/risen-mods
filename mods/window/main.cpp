// Window: borderless window covering the whole monitor, hidden Windows cursor, mouse kept inside the game window.
// Needs FullScreen="false" in data\ini\ConfigLocal.xml. Settings: mods\window.ini (hot-reloaded).

#include <windows.h>

#include "common.h"

static const char kDefaultIni[] =
    "; Window mod settings. Changes are picked up while the game is running.\r\n"
    "; Needs FullScreen=\"false\" in data\\ini\\ConfigLocal.xml (windowed mode).\r\n"
    "\r\n"
    "[Window]\r\n"
    "; Borderless window covering the whole monitor\r\n"
    "Borderless=1\r\n"
    "; Hide the Windows cursor over the game (the game draws its own)\r\n"
    "HideSystemCursor=1\r\n"
    "; Keep the mouse inside the game window while it is active\r\n"
    "ClipCursor=1\r\n";

struct Config {
    bool borderless = true;
    bool hideSystemCursor = true;
    bool clipCursor = true;
};

static Config g_config;
static WNDPROC g_origProc;

constexpr UINT WM_WINDOWMOD_TICK = WM_APP + 0x5301;

static void ConfigLoad()
{
    Config c;
    c.borderless = IniInt("Window", "Borderless", 1) != 0;
    c.hideSystemCursor = IniInt("Window", "HideSystemCursor", 1) != 0;
    c.clipCursor = IniInt("Window", "ClipCursor", 1) != 0;
    g_config = c;
    Log("config: borderless=%d hideCursor=%d clipCursor=%d", c.borderless, c.hideSystemCursor, c.clipCursor);
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
    case WM_WINDOWMOD_TICK:
        EnforceBorderless(hwnd);
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
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

static DWORD WINAPI Worker(LPVOID)
{
    HWND hwnd = WaitForGameWindow();
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    Log("window: hooked");
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        PostMessageW(hwnd, WM_WINDOWMOD_TICK, 0, 0); // window changes happen on the game thread
        if (tick % 4 == 0 && IniChanged())
            ConfigLoad();
        Sleep(250);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "window");
        Log("Window 1.0");
        IniPath(kDefaultIni);
        ConfigLoad();
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
