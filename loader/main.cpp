// Risen mod loader. Placed in Risen\bin as version.dll (SharedBase.dll imports VERSION.dll and the game folder is
// searched first), forwards the real version.dll API and loads every Risen\bin\mods\*.dll in alphabetical order.
// Mods are loaded right here in DllMain, before the game starts loading its data, so they can patch it early.
// Starts the shared log of the whole family (Risen\bin\mods\RisenMods.log, see common.h) and the crash reports
// (crash_log.h).

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

#include "common.h"
#include "crash_log.h"
#include "world_trace.h"

// Windows version as the kernel reports it (GetVersionEx lies to programs without a manifest).
static void LogSystem()
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v = {sizeof(v)};
    auto get = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (get && get(&v) == 0)
        Log("windows %lu.%lu build %lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    Log("game %s", exe);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH && reserved) { // the process ends normally (a crash never gets here)
        Log("game closed normally");
        return TRUE;
    }
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(instance);

    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(instance, dir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(dir, L'\\'))
        *slash = 0;
    std::wstring modsDir = std::wstring(dir) + L"\\mods";

    CreateDirectoryW(modsDir.c_str(), nullptr);
    char modsDirA[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, modsDir.c_str(), -1, modsDirA, MAX_PATH, nullptr, nullptr);
    LogStartSession(modsDirA);
    SYSTEMTIME now;
    GetLocalTime(&now);
    Log("Risen mod loader 1.2 diagnostics, %04d-%02d-%02d", now.wYear, now.wMonth, now.wDay);
    LogSystem();
    CrashLogInstall();

    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((modsDir + L"\\*.dll").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                names.push_back(fd.cFileName);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });

    for (const std::wstring& name : names) {
        HMODULE mod = LoadLibraryW((modsDir + L"\\" + name).c_str());
        char utf8[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, utf8, MAX_PATH, nullptr, nullptr);
        if (mod)
            Log("loaded %s", utf8);
        else
            Log("FAILED to load %s (error %lu)", utf8, GetLastError());
    }
    if (names.empty())
        Log("no mods in %ls", modsDir.c_str());
    CrashLogStartWorker();
    WorldTraceStart();
    return TRUE;
}
