// Fast Start: game window in ~6 s instead of ~30 s. Buffered .pak reads (fast_io.cpp), DxDiag stub (dxdiag_stub.cpp),
// compiled shader cache (shader_cache.cpp, mods\fast_start.shaders), no "Press START" screen (press_start.cpp).
// No settings needed; optional mods\fast_start.ini (also on the "Fast Start" tab of the Risen Mods menu):
//   [Performance]
//   FastPakReads=0   ; turn off buffered .pak reads
//   SkipDxDiag=0     ; turn off the DxDiag stub
//   ShaderCache=0    ; compile the shaders on every start, as the game does
//   SkipPressStart=0 ; show the "Press START" screen before the main menu

#include <windows.h>
#include <cstring>

#include "common.h"
#include "dxdiag_stub.h"
#include "fast_io.h"
#include "hub_client.h"
#include "press_start.h"
#include "shader_cache.h"

static const HubItem kItems[] = {
    {HubCheckbox, "Performance", "FastPakReads", "Fast reading of game archives", "Быстрое чтение архивов игры", 1,
     0, 0, 0, 0, nullptr, -1, "Saves ~17 s at startup", "Экономит ~17 с при запуске"},
    {HubCheckbox, "Performance", "SkipDxDiag", "Skip the DxDiag video card check", "Пропускать проверку DxDiag", 1,
     0, 0, 0, 0, nullptr, -1, "Saves ~7 s at startup", "Экономит ~7 с при запуске"},
    {HubCheckbox, "Performance", "ShaderCache", "Keep compiled shaders on disk", "Хранить скомпилированные шейдеры",
     1, 0, 0, 0, 0, nullptr, -1, "Saves ~1 s at startup (mods\\fast_start.shaders)",
     "Экономит ~1 с при запуске (mods\\fast_start.shaders)"},
    {HubCheckbox, "Performance", "SkipPressStart", "Skip the \"Press START\" screen", "Без экрана «Нажми кнопку START»",
     1, 0, 0, 0, 0, nullptr, -1, "Goes straight to the main menu, keyboard and mouse controls",
     "Сразу главное меню, управление клавиатурой и мышью"},
    {HubNote, nullptr, nullptr, "Changes take effect the next time the game starts.",
     "Изменения сработают при следующем запуске игры."},
};

static HubPage g_page = {"fast_start", 40, "Fast Start", "Быстрый старт", nullptr, kItems,
                         sizeof(kItems) / sizeof(kItems[0]), nullptr};

// The menu (menu.dll) is loaded after us: register once the game window exists.
static DWORD WINAPI RegisterPage(LPVOID)
{
    if (IniInt("Performance", "SkipPressStart", 1))
        PressStartSkipInstall();
    WaitForGameWindow();
    if (const HubApi* hub = HubGet())
        hub->registerPage(&g_page);
    Sleep(15000); // the menu shaders are compiled after the window shows up
    ShaderCacheLogStats();
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
        char cachePath[MAX_PATH];
        strcpy_s(cachePath, IniPath());
        strcpy_s(strrchr(cachePath, '.'), 9, ".shaders");
        bool shaders = IniInt("Performance", "ShaderCache", 1) && ShaderCacheInstall(cachePath);
        Log("Fast Start 1.3: fast pak reads %s, dxdiag stub %s, shader cache %s", fastIo ? "on" : "off",
            dxDiag ? "on" : "off", shaders ? "on" : "off");
        if (HANDLE t = CreateThread(nullptr, 0, RegisterPage, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
