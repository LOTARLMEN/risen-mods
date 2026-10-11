#include "game.h"

#include <shobjidl.h>
#include <tlhelp32.h>
#include <cstdio>
#include <vector>

#include "common.h"

namespace {

const wchar_t kSteamAppId[] = L"40300";

bool FileExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Parent(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring ReadRegString(HKEY root, const wchar_t* key, const wchar_t* value)
{
    wchar_t buf[MAX_PATH];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return std::wstring();
    std::wstring s = buf;
    for (wchar_t& c : s)
        if (c == L'/')
            c = L'\\';
    return s;
}

std::string ReadTextFile(const std::wstring& path)
{
    std::string text;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        return text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    fclose(f);
    return text;
}

std::wstring Utf8ToWide(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// Every value of "key" in a Valve KeyValues text ("key"<tabs>"value"), with \\ unescaped.
std::vector<std::string> VdfValues(const std::string& text, const char* key)
{
    std::vector<std::string> values;
    std::string quoted = std::string("\"") + key + "\"";
    for (size_t at = text.find(quoted); at != std::string::npos; at = text.find(quoted, at + 1)) {
        size_t open = text.find('"', at + quoted.size());
        size_t eol = text.find('\n', at);
        if (open == std::string::npos || open > eol)
            continue;
        std::string value;
        for (size_t i = open + 1; i < text.size() && text[i] != '"'; ++i) {
            if (text[i] == '\\' && i + 1 < text.size())
                ++i;
            value += text[i];
        }
        values.push_back(value);
    }
    return values;
}

std::wstring FromSteam()
{
    std::wstring steam = ReadRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty())
        steam = ReadRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (steam.empty())
        return std::wstring();
    std::vector<std::wstring> libraries = {steam};
    for (const std::string& path : VdfValues(ReadTextFile(steam + L"\\steamapps\\libraryfolders.vdf"), "path"))
        libraries.push_back(Utf8ToWide(path));
    for (const std::wstring& lib : libraries) {
        std::wstring apps = lib + L"\\steamapps\\";
        std::string manifest = ReadTextFile(apps + L"appmanifest_" + kSteamAppId + L".acf");
        std::vector<std::string> dirs = VdfValues(manifest, "installdir");
        if (dirs.empty())
            continue;
        std::wstring folder = apps + L"common\\" + Utf8ToWide(dirs[0]);
        Log("steam: Risen in %ls", folder.c_str());
        if (CheckGameFolder(folder) != GameCheck::NoExe)
            return folder;
    }
    return std::wstring();
}

} // namespace

GameCheck CheckGameFolder(const std::wstring& folder)
{
    std::wstring exe = folder + L"\\bin\\Risen.exe";
    HANDLE f = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return GameCheck::NoExe;
    // PE header: e_lfanew at 0x3C, machine right after "PE\0\0".
    unsigned char head[0x400] = {};
    DWORD read = 0;
    ReadFile(f, head, sizeof(head), &read, nullptr);
    CloseHandle(f);
    DWORD pe = *reinterpret_cast<DWORD*>(head + 0x3C);
    if (read < 0x40 || pe + 6 > read || memcmp(head + pe, "PE\0\0", 4) != 0)
        return GameCheck::NoExe;
    return *reinterpret_cast<WORD*>(head + pe + 4) == IMAGE_FILE_MACHINE_AMD64 ? GameCheck::Ok : GameCheck::Not64Bit;
}

std::wstring FindGameFolder()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = Parent(exe);
    // Placed into the game folder or into its bin\ (or a folder next to bin\, like risen-mods\).
    for (const std::wstring& candidate : {dir, Parent(dir)})
        if (!candidate.empty() && FileExists(candidate + L"\\bin\\Risen.exe"))
            return candidate;
    return FromSteam();
}

bool GameRunning()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry = {sizeof(entry)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &entry); ok && !found; ok = Process32NextW(snap, &entry))
        found = _wcsicmp(entry.szExeFile, L"Risen.exe") == 0;
    CloseHandle(snap);
    return found;
}

std::wstring PickFolder(HWND owner, const wchar_t* title)
{
    std::wstring result;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return result;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title);
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))) {
        wchar_t* path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            result = path;
            CoTaskMemFree(path);
        }
        item->Release();
    }
    dialog->Release();
    return result;
}
