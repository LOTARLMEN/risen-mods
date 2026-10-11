// Risen mod loader. Placed in Risen\bin as version.dll (SharedBase.dll imports VERSION.dll and the game folder is
// searched first), forwards the real version.dll API and loads every Risen\bin\mods\*.dll in alphabetical order.
// Mods are loaded right here in DllMain, before the game starts loading its data, so they can patch it early.
// Log: Risen\bin\mods\loader.log

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

#include "common.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(instance);

    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(instance, dir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(dir, L'\\'))
        *slash = 0;
    std::wstring modsDir = std::wstring(dir) + L"\\mods";

    // Log next to the mods; ModInit takes the directory from a module path, so give it a file inside mods\.
    CreateDirectoryW(modsDir.c_str(), nullptr);
    char logBase[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, (modsDir + L"\\loader").c_str(), -1, logBase, MAX_PATH, nullptr, nullptr);
    ModInitAt(logBase);
    Log("Risen mod loader 1.0");

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
    return TRUE;
}
