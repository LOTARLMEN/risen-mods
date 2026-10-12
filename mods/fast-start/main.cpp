// Fast Start: game window in ~10 s instead of ~30 s. Buffered .pak reads (fast_io.cpp) + DxDiag stub (dxdiag_stub.cpp).
// No settings needed; optional mods\fast_start.ini (also on the "Fast Start" tab of the Risen Mods menu):
//   [Performance]
//   FastPakReads=0   ; turn off buffered .pak reads
//   SkipDxDiag=0     ; turn off the DxDiag stub

#include <windows.h>

#include "common.h"
#include "dxdiag_stub.h"
#include "fast_io.h"
#include "hub_client.h"

static const HubItem kItems[] = {
    {HubCheckbox, "Performance", "FastPakReads", "Fast reading of game archives", "Быстрое чтение архивов игры", 1,
     0, 0, 0, 0, nullptr, -1, "Saves ~17 s at startup", "Экономит ~17 с при запуске"},
    {HubCheckbox, "Performance", "SkipDxDiag", "Skip the DxDiag video card check", "Пропускать проверку DxDiag", 1,
     0, 0, 0, 0, nullptr, -1, "Saves ~7 s at startup", "Экономит ~7 с при запуске"},
    {HubNote, nullptr, nullptr, "Changes take effect the next time the game starts.",
     "Изменения сработают при следующем запуске игры."},
};

static HubPage g_page = {"fast_start", 40, "Fast Start", "Быстрый старт", nullptr, kItems,
                         sizeof(kItems) / sizeof(kItems[0]), nullptr};

// The menu (menu.dll) is loaded after us: register once the game window exists.
static DWORD WINAPI RegisterPage(LPVOID)
{
    WaitForGameWindow();
    if (const HubApi* hub = HubGet())
        hub->registerPage(&g_page);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "fast_start");
        g_page.iniPath = IniPath();
        // The loader runs us before the game starts loading its data, so the hooks are in place in time.
        bool fastIo = IniInt("Performance", "FastPakReads", 1) && FastIoInstall();
        bool dxDiag = IniInt("Performance", "SkipDxDiag", 1) && DxDiagStubInstall();
        Log("Fast Start 1.2.1: fast pak reads %s, dxdiag stub %s", fastIo ? "on" : "off", dxDiag ? "on" : "off");
        if (HANDLE t = CreateThread(nullptr, 0, RegisterPage, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
