#include "input_capture.h"

#include <atomic>

#include "common.h"
#include "iat_patch.h"

namespace {

std::atomic<bool> g_captured;
POINT g_savedCursor;
decltype(&SetCursorPos) g_origSetCursorPos = SetCursorPos;
decltype(&ClipCursor) g_origClipCursor = ClipCursor;

BOOL WINAPI HookedSetCursorPos(int x, int y)
{
    return g_captured ? TRUE : g_origSetCursorPos(x, y);
}

BOOL WINAPI HookedClipCursor(const RECT* rect)
{
    return g_captured ? TRUE : g_origClipCursor(rect);
}

void Patch(HMODULE mod, const char* name, void* hook, void** orig)
{
    if (void* prev = PatchImport(mod, "USER32.dll", name, hook))
        *orig = prev;
}

} // namespace

bool InputCaptureInstall()
{
    HMODULE engine = GetModuleHandleA("Engine.dll");
    if (!engine)
        return false;
    Patch(engine, "SetCursorPos", HookedSetCursorPos, reinterpret_cast<void**>(&g_origSetCursorPos));
    Patch(engine, "ClipCursor", HookedClipCursor, reinterpret_cast<void**>(&g_origClipCursor));
    Patch(GetModuleHandleA(nullptr), "ClipCursor", HookedClipCursor, reinterpret_cast<void**>(&g_origClipCursor));
    return true;
}

void InputCaptureSet(HWND hwnd, bool on, bool restoreCursor)
{
    if (on == g_captured)
        return;
    if (on) {
        GetCursorPos(&g_savedCursor);
        g_captured = true;
        // The game may have clipped the cursor to a tiny area for mouse look: give it the whole window.
        RECT r;
        GetClientRect(hwnd, &r);
        MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
        g_origClipCursor(&r);
    } else {
        g_captured = false;
        if (restoreCursor)
            g_origSetCursorPos(g_savedCursor.x, g_savedCursor.y); // no camera jump on the first mouse-look frame
    }
}

bool InputCaptured()
{
    return g_captured;
}

bool InputCaptureFilter(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
    if (!g_captured)
        return false;
    bool input = msg == WM_INPUT || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
                 (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
    if (!input)
        return false;
    // DefWindowProc still does the system part: WM_INPUT cleanup, Alt+F4, Alt+Tab.
    *result = IsWindowUnicode(hwnd) ? DefWindowProcW(hwnd, msg, wp, lp) : DefWindowProcA(hwnd, msg, wp, lp);
    return true;
}
