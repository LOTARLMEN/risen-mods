#pragma once

// The installer's model: mods offered by the latest GitHub release (or a local folder of zips) and what is
// installed in the game folder. Every zip has the layout of the game folder: bin\version.dll (loader),
// bin\mods\<id>.dll, bin\mods\menu.dll (F10 menu shared by all mods), README.txt.
// Installed versions are kept in bin\mods\installed.ini ([Mods] <id>=<version>).

#include <atomic>
#include <string>
#include <vector>

#include "zip.h"

struct Mod {
    std::string id;       // dll name without extension: "autoloot", "fast_start"
    std::string title;    // "AutoLoot", "Fast Start"
    std::string version;  // offered version, empty = not in the release (installed by hand / older release)
    std::wstring source;  // zip URL or local path; downloaded only when installing
    std::vector<ZipEntry> files; // filled by LoadModFiles
    std::string installed; // installed version, "?" = dll present but version unknown, empty = not installed
};

struct ModDescription {
    const char* en;
    const char* ru;
};
ModDescription DescribeMod(const std::string& id);

// -1 / 0 / 1 for dotted versions ("1.10" > "1.9").
int CompareVersions(const std::string& a, const std::string& b);
bool ModNeedsUpdate(const Mod& mod);

struct FetchProgress {
    std::atomic<long long> done{0}, total{0}; // bytes downloaded / expected
};

// RisenModsInstaller.exe of the release (self_update.h).
struct ReleaseInstaller {
    std::wstring source; // URL or local path (--zips), empty = not in the release
    std::string sha256;  // lowercase hex, empty when GitHub didn't list it
};

// Lists the mods of the latest release (or the *.zip files in localZips when not empty); one small request,
// the archives themselves are fetched by LoadModFiles. The mod id comes from the asset name
// ("RisenFastStart-1.2.zip" -> fast_start). False + error text on failure.
bool FetchMods(const std::wstring& localZips, std::vector<Mod>* mods, std::string* releaseTag,
               ReleaseInstaller* installer, std::string* error);

// Downloads / reads the mod's zip and unpacks it into Mod::files (checks that it holds bin\mods\<id>.dll).
bool LoadModFiles(Mod* mod, std::string* error, FetchProgress* progress);

// Fills Mod::installed from the game folder and adds rows for installed mods missing from the list.
void ScanInstalled(const std::wstring& game, std::vector<Mod>* mods);

// Writes the mod's files (shared files are overwritten, the user's .ini files are never touched).
bool InstallMod(const std::wstring& game, const Mod& mod, std::string* error);
// Deletes the mod's dll, readme and log; its .ini (settings) stays. False when a file is locked.
bool RemoveMod(const std::wstring& game, const std::string& id, std::string* error);
// When no mod dll is left in bin\mods: removes menu.dll, bin\version.dll (only our loader) and installed.ini.
void CleanupIfEmpty(const std::wstring& game);

// The Window mod needs the game in windowed mode: FullScreen="false" in data\ini\ConfigLocal.xml (a backup
// ConfigLocal.xml.bak is made first). True when it was changed.
bool SetWindowedMode(const std::wstring& game);
