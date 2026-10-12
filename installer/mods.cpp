#include "mods.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.h"
#include "net.h"

namespace {

const wchar_t kReleaseApi[] = L"https://api.github.com/repos/LOTARLMEN/risen-mods/releases/latest";
const char kLoaderMarker[] = "Risen mod loader"; // string inside our bin\version.dll
const char kMenuLogTitle[] = "Risen Mods Menu";  // menu.dll logs "Risen Mods Menu 1.1" at start

struct KnownMod {
    const char* id;
    const char* title;
    ModDescription text;
};
const KnownMod kKnown[] = {
    {"autoloot", "AutoLoot",
     {"Right mouse button loots the focused body, open chest or item at once, no loot window. F toggles automatic "
      "pickup of items around you (never someone else's things, nothing through walls). Taking what isn't yours "
      "is theft, as in the game: whoever sees it reacts.",
      "Правая кнопка мыши сразу забирает всё с тела, открытого сундука или предмета под прицелом, без окна лута. "
      "F включает автоподбор предметов вокруг (чужое и сквозь стены не берёт). Взять чужое - кража, как в "
      "игре: кто увидит, отреагирует."}},
    {"quick_lockpick", "Quick Lockpick",
     {"Right mouse button on a locked chest or door opens it at once, no minigame. The game's own skill check "
      "decides whether you can; a lockpick may break.",
      "Правая кнопка мыши на запертом сундуке или двери сразу открывает замок, без мини-игры. Можно ли вскрыть, "
      "решает сама игра по навыку; отмычка может сломаться."}},
    {"speed", "Speed",
     {"Z toggles a game speed-up (x3 by default) with an hourglass icon, like Gothic's Z-mod.",
      "Z включает ускорение игры (по умолчанию x3), на экране песочные часы. Как Z-mod в Готике."}},
    {"no_limits", "No Limits",
     {"Strength and dexterity go past the game's maximum of 200 (up to 1000, configurable), and teachers train "
      "them that far instead of their own limit.",
      "Сила и ловкость выше максимума игры 200 (до 1000, настраивается), и учителя учат до него, а не до своего "
      "предела."}},
    {"fast_start", "Fast Start",
     {"The game window appears in about 10 seconds instead of about 30.",
      "Игра запускается примерно за 10 секунд вместо 30."}},
    {"window", "Window",
     {"Borderless window over the whole monitor, no double cursor, instant Alt+Tab. Switches the game to "
      "windowed mode.",
      "Окно без рамки на весь монитор, без двойного курсора, мгновенный Alt+Tab. Переключает игру в оконный "
      "режим."}},
};

std::wstring Widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::wstring ModsDir(const std::wstring& game)
{
    return game + L"\\bin\\mods";
}

std::wstring InstalledIni(const std::wstring& game)
{
    return ModsDir(game) + L"\\installed.ini";
}

bool FileExists(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>* out)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        return false;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out->insert(out->end(), buf, buf + n);
    fclose(f);
    return true;
}

std::string Win32Error(const char* what, const std::wstring& path)
{
    DWORD code = GetLastError();
    char buf[512];
    snprintf(buf, sizeof(buf), "%s %ls (error %lu%s)", what, path.c_str(), code,
             code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED ? ", file in use or no access" : "");
    return buf;
}

// Writes next to the target and swaps it in, so a failed write never leaves half a dll.
bool WriteFileAtomic(const std::wstring& path, const std::vector<unsigned char>& data, std::string* error)
{
    std::vector<unsigned char> current;
    if (ReadFileBytes(path, &current) && current == data)
        return true; // same file already there (shared files of several mods)
    std::wstring temp = path + L".new";
    FILE* f = nullptr;
    if (_wfopen_s(&f, temp.c_str(), L"wb") != 0 || !f) {
        *error = Win32Error("can't write", temp);
        return false;
    }
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fclose(f) == 0 && ok;
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        *error = Win32Error("can't replace", path);
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

bool DeleteIfExists(const std::wstring& path, std::string* error)
{
    if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND)
        return true;
    if (error)
        *error = Win32Error("can't delete", path);
    return false;
}

bool ContainsBytes(const std::vector<unsigned char>& data, const char* text)
{
    size_t n = strlen(text);
    for (size_t i = 0; i + n <= data.size(); ++i)
        if (memcmp(&data[i], text, n) == 0)
            return true;
    return false;
}

// "RisenFastStart-1.2.zip" -> version "1.2"; false for other assets (the installer exe, ...).
bool ParseAssetName(const std::string& name, std::string* version)
{
    if (name.size() < 10 || name.compare(0, 5, "Risen") != 0 || name.compare(name.size() - 4, 4, ".zip") != 0)
        return false;
    size_t dash = name.rfind('-');
    if (dash == std::string::npos || dash < 6)
        return false;
    *version = name.substr(dash + 1, name.size() - 4 - dash - 1);
    return !version->empty() && strspn(version->c_str(), "0123456789.") == version->size();
}

// Every "key": "value" string in a JSON text (GitHub's answer has no escapes in these values).
std::vector<std::string> JsonStrings(const std::string& json, const char* key)
{
    std::vector<std::string> values;
    std::string quoted = std::string("\"") + key + "\"";
    for (size_t at = json.find(quoted); at != std::string::npos; at = json.find(quoted, at + 1)) {
        size_t p = at + quoted.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == ':'))
            ++p;
        if (p >= json.size() || json[p] != '"')
            continue;
        size_t end = json.find('"', p + 1);
        if (end != std::string::npos)
            values.push_back(json.substr(p + 1, end - p - 1));
    }
    return values;
}

std::string TitleFromId(const std::string& id)
{
    for (const KnownMod& k : kKnown)
        if (id == k.id)
            return k.title;
    std::string title;
    bool upper = true;
    for (char c : id) {
        if (c == '_') {
            title += ' ';
            upper = true;
        } else {
            title += upper ? static_cast<char>(toupper(static_cast<unsigned char>(c))) : c;
            upper = false;
        }
    }
    return title;
}

// "RisenFastStart-1.2.zip" -> "fast_start": the known title without spaces, else CamelCase -> snake_case.
std::string IdFromAssetName(const std::string& name)
{
    std::string base = name.substr(5, name.rfind('-') - 5); // after "Risen", before "-<version>"
    for (const KnownMod& k : kKnown) {
        std::string title;
        for (const char* c = k.title; *c; ++c)
            if (*c != ' ')
                title += *c;
        if (_stricmp(title.c_str(), base.c_str()) == 0)
            return k.id;
    }
    std::string id;
    for (size_t i = 0; i < base.size(); ++i) {
        if (i > 0 && isupper(static_cast<unsigned char>(base[i])))
            id += '_';
        id += static_cast<char>(tolower(static_cast<unsigned char>(base[i])));
    }
    return id;
}

} // namespace

ModDescription DescribeMod(const std::string& id)
{
    for (const KnownMod& k : kKnown)
        if (id == k.id)
            return k.text;
    return {"", ""};
}

int CompareVersions(const std::string& a, const std::string& b)
{
    const char *pa = a.c_str(), *pb = b.c_str();
    while (*pa || *pb) {
        long va = strtol(pa, const_cast<char**>(&pa), 10), vb = strtol(pb, const_cast<char**>(&pb), 10);
        if (va != vb)
            return va < vb ? -1 : 1;
        if (*pa) // '.' or garbage: step over it
            ++pa;
        if (*pb)
            ++pb;
    }
    return 0;
}

bool ModNeedsUpdate(const Mod& mod)
{
    return !mod.installed.empty() && !mod.version.empty() &&
           (mod.installed == "?" || CompareVersions(mod.installed, mod.version) < 0);
}

Mod MenuMod()
{
    Mod menu;
    menu.id = "menu";
    menu.title = "Menu";
    return menu;
}

bool FetchMods(const std::wstring& localZips, std::vector<Mod>* mods, Mod* menu, std::string* releaseTag,
               ReleaseInstaller* installer, std::string* error)
{
    *menu = MenuMod();
    struct Asset {
        std::string name, version;
        std::wstring source; // URL or file path
    };
    std::vector<Asset> assets;
    if (!localZips.empty()) {
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW((localZips + L"\\*.zip").c_str(), &fd);
        if (find == INVALID_HANDLE_VALUE) {
            *error = "no zips in the given folder";
            return false;
        }
        do {
            char name[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, MAX_PATH, nullptr, nullptr);
            Asset a{name};
            if (ParseAssetName(a.name, &a.version)) {
                a.source = localZips + L"\\" + fd.cFileName;
                assets.push_back(a);
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
        *releaseTag = "local";
        std::wstring exe = localZips + L"\\RisenModsInstaller.exe";
        if (FileExists(exe))
            installer->source = exe; // no checksum: always compared by version
    } else {
        std::vector<unsigned char> body;
        if (!HttpGet(kReleaseApi, &body, error))
            return false;
        std::string json(body.begin(), body.end());
        std::vector<std::string> tags = JsonStrings(json, "tag_name");
        *releaseTag = tags.empty() ? "?" : tags[0];
        for (const std::string& url : JsonStrings(json, "browser_download_url")) {
            Asset a{url.substr(url.rfind('/') + 1)};
            if (ParseAssetName(a.name, &a.version)) {
                a.source = Widen(url);
                assets.push_back(a);
            }
        }
        // The installer exe: its URL and the "digest": "sha256:..." GitHub lists in the same asset object.
        size_t name = json.find("\"RisenModsInstaller.exe\"");
        if (name != std::string::npos) {
            size_t url = json.find("\"browser_download_url\"", name), digest = json.find("\"digest\"", name);
            for (const std::string& u : JsonStrings(json.substr(url == std::string::npos ? name : url), "browser_download_url"))
                if (u.size() > 23 && u.compare(u.size() - 23, 23, "/RisenModsInstaller.exe") == 0) {
                    installer->source = Widen(u);
                    break;
                }
            if (digest != std::string::npos && digest < url) {
                std::vector<std::string> d = JsonStrings(json.substr(digest, url - digest), "digest");
                if (!d.empty() && d[0].compare(0, 7, "sha256:") == 0)
                    installer->sha256 = d[0].substr(7);
            }
        }
        Log("release %s: %zu mod zips, installer %s", releaseTag->c_str(), assets.size(),
            installer->source.empty() ? "-" : installer->sha256.empty() ? "(no checksum)" : installer->sha256.c_str());
    }
    if (assets.empty()) {
        *error = "the release has no mod archives";
        return false;
    }

    for (const Asset& a : assets) {
        Mod mod;
        mod.id = IdFromAssetName(a.name);
        mod.title = TitleFromId(mod.id);
        mod.version = a.version;
        mod.source = a.source;
        Log("release: %s %s", mod.id.c_str(), mod.version.c_str());
        if (mod.id == "menu") {
            if (CompareVersions(menu->version, mod.version) < 0) {
                menu->version = mod.version;
                menu->source = mod.source;
            }
            continue;
        }
        // Two archives of one mod (shouldn't happen in a release): keep the newer one.
        bool replaced = false;
        for (Mod& m : *mods)
            if (m.id == mod.id) {
                if (CompareVersions(m.version, mod.version) < 0)
                    m = std::move(mod);
                replaced = true;
            }
        if (!replaced)
            mods->push_back(std::move(mod));
    }
    return true;
}

bool LoadModFiles(Mod* mod, std::string* error, FetchProgress* progress)
{
    if (!mod->files.empty())
        return true;
    std::vector<unsigned char> zip;
    bool url = mod->source.compare(0, 8, L"https://") == 0;
    if (url ? !HttpGet(mod->source, &zip, error, &progress->done, &progress->total)
            : !ReadFileBytes(mod->source, &zip)) {
        if (!url)
            *error = "can't read the archive";
        *error = mod->title + ": " + *error;
        return false;
    }
    std::vector<ZipEntry> files;
    if (!ZipExtract(zip, &files, error)) {
        *error = mod->title + ": " + *error;
        return false;
    }
    bool hasDll = false;
    for (const ZipEntry& e : files)
        hasDll |= _stricmp(e.name.c_str(), ("bin/mods/" + mod->id + ".dll").c_str()) == 0;
    if (!hasDll) {
        *error = mod->title + ": the archive has no bin\\mods\\" + mod->id + ".dll";
        return false;
    }
    mod->files = std::move(files);
    Log("downloaded %s %s (%zu files)", mod->id.c_str(), mod->version.c_str(), mod->files.size());
    return true;
}

// Every mod logs "<Title> <version>" at start ("AutoLoot 0.4", "Fast Start 1.2: ..."), so the string is inside its
// dll: this finds the version of mods unpacked by hand from a zip. Empty when not found (older builds).
std::string DllVersion(const std::wstring& dll, const std::string& title)
{
    std::vector<unsigned char> data;
    if (!ReadFileBytes(dll, &data))
        return std::string();
    std::string needle = title + " ";
    for (size_t i = 0; i + needle.size() < data.size(); ++i) {
        if (memcmp(&data[i], needle.data(), needle.size()) != 0)
            continue;
        size_t p = i + needle.size(), end = p;
        while (end < data.size() && (isdigit(data[end]) || data[end] == '.'))
            ++end;
        std::string version(data.begin() + p, data.begin() + end);
        if (end > p && isdigit(data[p]) && version.find('.') != std::string::npos && version.back() != '.')
            return version;
    }
    return std::string();
}

void ScanInstalled(const std::wstring& game, std::vector<Mod>* mods)
{
    std::wstring ini = InstalledIni(game);
    for (Mod& m : *mods)
        m.installed.clear();
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((ModsDir(game) + L"\\*.dll").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        char name[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, MAX_PATH, nullptr, nullptr);
        std::string id(name, strlen(name) - 4);
        for (char& c : id)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (id == "menu")
            continue;
        wchar_t version[64];
        GetPrivateProfileStringW(L"Mods", Widen(id).c_str(), L"?", version, 64, ini.c_str());
        char v[64];
        WideCharToMultiByte(CP_UTF8, 0, version, -1, v, 64, nullptr, nullptr);
        Mod* mod = nullptr;
        for (Mod& m : *mods)
            if (m.id == id)
                mod = &m;
        if (!mod) { // installed, but not offered (developer build, mod dropped from the release)
            mods->push_back(Mod());
            mod = &mods->back();
            mod->id = id;
            mod->title = TitleFromId(id);
        }
        // The dll itself is the truth (it may have been replaced by hand), then installed.ini.
        std::string fromDll = DllVersion(ModsDir(game) + L"\\" + fd.cFileName, mod->title);
        mod->installed = fromDll.empty() ? v : fromDll;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

void ScanMenu(const std::wstring& game, Mod* menu)
{
    menu->installed.clear();
    std::wstring dll = ModsDir(game) + L"\\menu.dll";
    if (!FileExists(dll))
        return;
    menu->installed = DllVersion(dll, kMenuLogTitle);
    if (menu->installed.empty()) {
        wchar_t version[64];
        GetPrivateProfileStringW(L"Mods", L"menu", L"?", version, 64, InstalledIni(game).c_str());
        char v[64];
        WideCharToMultiByte(CP_UTF8, 0, version, -1, v, 64, nullptr, nullptr);
        menu->installed = v;
    }
}

bool MenuNeedsWork(const std::vector<Mod>& mods, const Mod& menu)
{
    if (menu.version.empty())
        return false; // an older release: menu.dll comes inside the mod zips
    bool anyMod = false;
    for (const Mod& m : mods)
        anyMod |= !m.installed.empty();
    return anyMod && (menu.installed.empty() || ModNeedsUpdate(menu));
}

bool InstallMod(const std::wstring& game, const Mod& mod, std::string* error)
{
    CreateDirectoryW((game + L"\\bin").c_str(), nullptr);
    CreateDirectoryW(ModsDir(game).c_str(), nullptr);
    for (const ZipEntry& e : mod.files) {
        std::string rel = e.name;
        if (rel == "README.txt") {
            rel = "bin/mods/" + mod.id + ".README.txt"; // every zip has one: keep them apart
        } else if (rel.compare(0, 4, "bin/") != 0 || rel.find("..") != std::string::npos ||
                   rel.find(':') != std::string::npos) {
            Log("install %s: skipped %s", mod.id.c_str(), rel.c_str());
            continue; // only files inside bin\ are ever written
        }
        if (rel.size() >= 4 && _stricmp(rel.c_str() + rel.size() - 4, ".ini") == 0 &&
            FileExists(game + L"\\" + Widen(rel)))
            continue; // the user's settings win over an ini shipped in a zip
        std::wstring path = game + L"\\" + Widen(rel);
        for (wchar_t& c : path)
            if (c == L'/')
                c = L'\\';
        // Folders inside bin\ (bin\mods only today).
        for (size_t slash = path.find(L'\\', game.size() + 1); slash != std::wstring::npos;
             slash = path.find(L'\\', slash + 1))
            CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);

        if (rel == "bin/version.dll" && FileExists(path)) {
            std::vector<unsigned char> current;
            ReadFileBytes(path, &current);
            if (!ContainsBytes(current, kLoaderMarker)) { // another program's version.dll: keep a copy
                std::wstring backup = path + L".other";
                CopyFileW(path.c_str(), backup.c_str(), FALSE);
                Log("install: foreign bin\\version.dll saved as version.dll.other");
            }
        }
        if (!WriteFileAtomic(path, e.data, error))
            return false;
    }
    if (!WritePrivateProfileStringW(L"Mods", Widen(mod.id).c_str(), Widen(mod.version).c_str(),
                                    InstalledIni(game).c_str())) {
        *error = Win32Error("can't write", InstalledIni(game));
        return false;
    }
    Log("installed %s %s", mod.id.c_str(), mod.version.c_str());
    return true;
}

bool RemoveMod(const std::wstring& game, const std::string& id, std::string* error)
{
    std::wstring base = ModsDir(game) + L"\\" + Widen(id);
    if (!DeleteIfExists(base + L".dll", error))
        return false;
    DeleteIfExists(base + L".README.txt", nullptr);
    DeleteIfExists(base + L".log", nullptr);
    WritePrivateProfileStringW(L"Mods", Widen(id).c_str(), nullptr, InstalledIni(game).c_str());
    Log("removed %s", id.c_str());
    return true;
}

void CleanupIfEmpty(const std::wstring& game)
{
    std::wstring dir = ModsDir(game);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"\\*.dll").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (_wcsicmp(fd.cFileName, L"menu.dll") != 0) {
                FindClose(find);
                return; // a mod is left
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    DeleteIfExists(dir + L"\\menu.dll", nullptr);
    DeleteIfExists(dir + L"\\menu.README.txt", nullptr);
    DeleteIfExists(dir + L"\\menu.log", nullptr);
    DeleteIfExists(dir + L"\\loader.log", nullptr);
    DeleteIfExists(dir + L"\\RisenMods.log", nullptr);      // the shared log (loader 1.1+)
    DeleteIfExists(dir + L"\\RisenMods.prev.log", nullptr);
    DeleteIfExists(InstalledIni(game), nullptr);
    std::wstring loader = game + L"\\bin\\version.dll";
    std::vector<unsigned char> data;
    if (ReadFileBytes(loader, &data) && ContainsBytes(data, kLoaderMarker)) {
        DeleteIfExists(loader, nullptr);
        std::wstring other = loader + L".other"; // put back what was there before us
        if (FileExists(other))
            MoveFileW(other.c_str(), loader.c_str());
    }
    RemoveDirectoryW(dir.c_str()); // only when empty: the users' .ini files stay
    Log("cleanup: no mods left, loader and menu removed");
}

bool SetWindowedMode(const std::wstring& game)
{
    std::wstring path = game + L"\\data\\ini\\ConfigLocal.xml";
    std::vector<unsigned char> bytes;
    ReadFileBytes(path, &bytes);
    std::string xml(bytes.begin(), bytes.end());
    const std::string attr = "FullScreen=\"";
    size_t at = xml.find(attr);
    if (at != std::string::npos) {
        size_t end = xml.find('"', at + attr.size());
        if (end == std::string::npos || xml.compare(at + attr.size(), end - at - attr.size(), "false") == 0)
            return false; // already windowed
        xml.replace(at + attr.size(), end - at - attr.size(), "false");
    } else if ((at = xml.find("<local>")) != std::string::npos) {
        xml.insert(at + 7, "\r\n  <Engine>\r\n    <Window FullScreen=\"false\">\r\n    </Window>\r\n  </Engine>");
    } else {
        xml = "<!-- Values from ConfigDefault.xml can be overridden here -->\r\n<local>\r\n  <Engine>\r\n"
              "    <Window FullScreen=\"false\">\r\n    </Window>\r\n  </Engine>\r\n</local>\r\n";
    }
    std::wstring backup = path + L".bak";
    if (!bytes.empty() && !FileExists(backup))
        CopyFileW(path.c_str(), backup.c_str(), TRUE);
    std::string error;
    bool ok = WriteFileAtomic(path, std::vector<unsigned char>(xml.begin(), xml.end()), &error);
    Log("windowed mode: %s", ok ? "set FullScreen=false" : error.c_str());
    return ok;
}
