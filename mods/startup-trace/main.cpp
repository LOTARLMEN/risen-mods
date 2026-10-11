// Startup Trace (developer tool, not for release): where does the game spend its startup time.
// Put startup_trace.dll into mods\ to enable, remove it to disable. Output: mods\startup_trace.log.

#include <windows.h>

#include "common.h"
#include "startup_trace.h"

static DWORD WINAPI Worker(LPVOID)
{
    WaitForGameWindow();
    StartupTraceMark("game window found");
    StartupTraceStopSoon();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        char tracePath[MAX_PATH];
        GetModuleFileNameA(instance, tracePath, MAX_PATH);
        strcpy_s(strrchr(tracePath, '.'), 5, ".log");
        // Our own Log() would write to the same file name, so keep it for the window search only.
        ModInit(instance, "startup_trace_worker");
        StartupTraceInit(tracePath);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
