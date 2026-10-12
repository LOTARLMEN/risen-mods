// Risen Mods installer: one window in the game's style (parchment, cursor and fonts read from the game's archives
// once the game folder is known). Lists the mods of the latest GitHub release with checkboxes, installs, updates
// and removes them. Downloads and file work run on a worker thread.
//
// Command line (also for testing without the window):
//   --game <folder>      use this game folder instead of searching for it
//   --zips <folder>      take Risen<Mod>-<version>.zip files from a folder instead of GitHub
//   --list               write the offered / installed mods to the log and exit
//   --install a,b        install or update these mods (ids: autoloot, speed, ...) and exit
//   --remove a,b         remove these mods and exit
//   --lang en|ru         interface language (default: the Windows language)
//   --screenshot <file>  save the window as .bmp once the mod list is shown, then exit (for development)
// Log: %TEMP%\RisenModsInstaller.log

#include <windows.h>
#include <d3d9.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "game.h"
#include "game_assets.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "mods.h"
#include "risen_style.h"
#include "self_update.h"
#include "version.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

const char kVersion[] = INSTALLER_VERSION;
const wchar_t kReleasesPage[] = L"https://github.com/LOTARLMEN/risen-mods/releases/latest";
const float kWidth = 760.0f, kHeight = 880.0f; // window size at 100% scaling

// ---------------------------------------------------------------------------------------------
// State shared with the worker thread (g_lock around everything below up to g_busy).

std::mutex g_lock;
std::wstring g_game;            // game folder, empty = not found
GameCheck g_gameCheck = GameCheck::NoExe;
std::vector<Mod> g_mods;
std::vector<bool> g_want;       // checkbox per mod
Mod g_menu;                     // the F10 menu: no checkbox, installed / updated with the mods
std::string g_tag;              // release tag
std::string g_fetchError;       // last failed fetch
std::string g_message;          // result of the last apply
bool g_messageError;
bool g_fetched;
std::atomic<bool> g_busy;       // worker running
std::atomic<bool> g_gameRunning;
FetchProgress g_progress;

// Self-update (under g_lock): the newer exe of the release, waiting for the "Update installer" button.
std::vector<unsigned char> g_newExe;
std::string g_newVersion;
bool g_selfUpdateFailed; // offer the releases page instead
bool g_commandMode;      // --list / --install / --remove: no self-update

std::wstring g_autoGame;  // found at start (exe folder, Steam); UI thread only
std::wstring g_localZips; // --zips
std::wstring g_screenshot; // --screenshot
bool g_russian;

// ---------------------------------------------------------------------------------------------
// Window, D3D9 and the look.

HWND g_hwnd;
IDirect3D9* g_d3d;
IDirect3DDevice9* g_device;
D3DPRESENT_PARAMETERS g_pp;
IDirect3DTexture9* g_atlas; // the game's GUI atlas, null = plain parchment colours
ImFont* g_font;
HCURSOR g_cursor; // the game's arrow, null = system arrow
float g_scale = 1.0f;
float g_headerHeight;    // pixels from the top that drag the window
float g_headerButtonsX;  // ...except from here to the right (language, close)
bool g_styleLoaded;      // game fonts / atlas tried for the current game folder

const char* T(const char* en, const char* ru)
{
    return g_russian ? ru : en;
}

std::string Narrow(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// The game's arrow as a real Windows cursor (no lag behind the mouse), scaled by whole pixels.
HCURSOR MakeGameCursor(const std::vector<unsigned char>& atlas, int atlasWidth, int factor)
{
    const AtlasRect& r = kAtlasCursor;
    int sw = static_cast<int>(r.x1 - r.x0), sh = static_cast<int>(r.y1 - r.y0);
    int w = sw * factor, h = sh * factor;
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = w;
    bi.bV5Height = -h; // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color)
        return nullptr;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t from = (static_cast<size_t>(r.y0 + y / factor) * atlasWidth + static_cast<size_t>(r.x0) + x / factor) * 4;
            memcpy(static_cast<unsigned char*>(bits) + (static_cast<size_t>(y) * w + x) * 4, &atlas[from], 4);
        }
    std::vector<unsigned char> maskBits(static_cast<size_t>((w + 15) / 16 * 2) * h, 0);
    HBITMAP mask = CreateBitmap(w, h, 1, 1, maskBits.data());
    ICONINFO info = {FALSE, 0, 0, mask, color};
    HCURSOR cursor = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return cursor;
}

// Game fonts, parchment and cursor from the game folder (once per folder).
void LoadGameStyle()
{
    g_styleLoaded = true;
    std::wstring game;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        game = g_game;
    }
    if (game.empty())
        return;
    GameAssetsSetRoot(game.c_str());
    if (!g_atlas)
        g_atlas = GameLoadTexture(g_device, kAtlasPak, kAtlasName);
    if (!g_cursor) {
        std::vector<unsigned char> pixels;
        int w, h;
        if (GameLoadImage(kAtlasPak, kAtlasName, &pixels, &w, &h)) {
            int factor = static_cast<int>(g_scale * 0.9f + 0.5f);
            g_cursor = MakeGameCursor(pixels, w, factor < 1 ? 1 : factor);
        }
    }
    ImFont* font = LoadGameFont("QUORUMM.TTF", 20.0f, false);
    if (font) {
        LoadGameFont("FreeSans.ttf", 20.0f, true);
        g_font = font;
    }
    Log("style: atlas %s, cursor %s, game font %s", g_atlas ? "ok" : "-", g_cursor ? "ok" : "-", font ? "ok" : "-");
}

// ---------------------------------------------------------------------------------------------
// Worker thread jobs.

void StartJob(void (*job)())
{
    if (g_busy.exchange(true))
        return;
    std::thread([job] {
        job();
        g_busy = false;
        if (g_hwnd)
            InvalidateRect(g_hwnd, nullptr, FALSE);
    }).detach();
}

void FetchJob()
{
    g_progress.done = 0;
    g_progress.total = 0;
    std::vector<Mod> mods;
    std::string tag, error;
    ReleaseInstaller installer;
    Mod menu;
    bool ok = FetchMods(g_localZips, &mods, &menu, &tag, &installer, &error);
    if (ok && !g_commandMode && !installer.source.empty())
        std::thread([installer] { // in the background: the mod list is usable meanwhile
            std::vector<unsigned char> exe;
            std::string version, err;
            FetchProgress progress;
            if (FindInstallerUpdate(installer, &exe, &version, &err, &progress)) {
                std::lock_guard<std::mutex> lock(g_lock);
                g_newExe = std::move(exe);
                g_newVersion = version;
            } else if (!err.empty()) {
                Log("self-update check: %s", err.c_str());
            }
            if (g_hwnd)
                InvalidateRect(g_hwnd, nullptr, FALSE);
        }).detach();
    std::lock_guard<std::mutex> lock(g_lock);
    if (!ok) {
        g_fetchError = error;
        Log("fetch FAILED: %s", error.c_str());
        return;
    }
    if (!g_game.empty()) {
        ScanInstalled(g_game, &mods);
        ScanMenu(g_game, &menu);
    }
    g_menu = std::move(menu);
    g_want.clear();
    for (const Mod& m : mods) // ticked = installed; the user ticks what to add
        g_want.push_back(!m.installed.empty());
    g_mods = std::move(mods);
    g_tag = tag;
    g_fetchError.clear();
    g_fetched = true;
}

bool g_updateMode; // UpdateJob: only update installed mods; ApplyJob: only install / remove by the checkboxes
int g_removeIndex = -1; // RemoveJob: the mod to remove (its "Remove" button)
int g_askRemove = -1;   // row whose "Remove" waits for the confirmation popup

void ApplyJob()
{
    std::lock_guard<std::mutex> lock(g_lock); // the UI only reads while this runs
    g_message.clear();
    g_messageError = false;
    if (GameRunning()) {
        g_message = T("Close Risen first.", "Сначала закройте Risen.");
        g_messageError = true;
        return;
    }
    int installed = 0, updated = 0, removed = 0;
    bool windowed = false;
    g_progress.done = 0;
    g_progress.total = 0;
    std::string error;
    for (size_t i = 0; i < g_mods.size() && error.empty(); ++i) {
        const Mod& m = g_mods[i];
        bool want = g_want[i], has = !m.installed.empty();
        if (g_updateMode ? want && has && ModNeedsUpdate(m) : want && !has && !m.version.empty()) {
            if (LoadModFiles(&g_mods[i], &error, &g_progress) && InstallMod(g_game, m, &error)) {
                ++(has ? updated : installed);
                if (m.id == "window")
                    windowed |= SetWindowedMode(g_game);
            }
        } else if (!g_updateMode && !want && has) {
            if (RemoveMod(g_game, m.id, &error))
                ++removed;
        }
    }
    // The menu: added with the first mod, updated with any install or update (not on a plain removal).
    const char* menuDone = nullptr;
    if (error.empty() && (installed + updated > 0 || g_updateMode)) {
        ScanInstalled(g_game, &g_mods);
        ScanMenu(g_game, &g_menu);
        if (MenuNeedsWork(g_mods, g_menu) && LoadModFiles(&g_menu, &error, &g_progress) &&
            InstallMod(g_game, g_menu, &error))
            menuDone = g_menu.installed.empty() ? T(" The F10 menu is installed.", " Меню F10 установлено.")
                                                : T(" The F10 menu is updated.", " Меню F10 обновлено.");
    }
    CleanupIfEmpty(g_game);
    ScanInstalled(g_game, &g_mods);
    ScanMenu(g_game, &g_menu);
    for (size_t i = 0; i < g_mods.size(); ++i) {
        if (i >= g_want.size())
            g_want.push_back(false);
        g_want[i] = !g_mods[i].installed.empty();
    }
    // Mods that are gone from the release and were removed: drop their rows.
    for (size_t i = g_mods.size(); i-- > 0;)
        if (g_mods[i].version.empty() && g_mods[i].installed.empty()) {
            g_mods.erase(g_mods.begin() + i);
            g_want.erase(g_want.begin() + i);
        }

    char text[512];
    if (!error.empty()) {
        snprintf(text, sizeof(text), "%s %s", T("Error:", "Ошибка:"), error.c_str());
        g_messageError = true;
    } else {
        snprintf(text, sizeof(text), T("Done: %d installed, %d updated, %d removed.",
                                       "Готово: установлено %d, обновлено %d, удалено %d."),
                 installed, updated, removed);
        if (menuDone)
            strcat_s(text, menuDone);
        if (windowed)
            strcat_s(text, T(" The game is switched to windowed mode.", " Игра переключена в оконный режим."));
    }
    g_message = text;
    Log("apply: %s", text);
}

void SelfUpdateJob()
{
    std::lock_guard<std::mutex> lock(g_lock);
    std::string error;
    if (ApplyInstallerUpdate(g_newExe, &error)) {
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0); // the new exe is starting
        return;
    }
    Log("self-update FAILED: %s", error.c_str());
    g_message = std::string(T("Couldn't update the installer: ", "Не удалось обновить установщик: ")) + error;
    g_messageError = true;
    g_selfUpdateFailed = true;
    g_newVersion.clear();
}

void InstallJob()
{
    g_updateMode = false;
    ApplyJob();
}

void RemoveJob()
{
    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (size_t i = 0; i < g_want.size(); ++i) // only this one: unticked = remove, nothing to install
            g_want[i] = static_cast<int>(i) == g_removeIndex ? false : !g_mods[i].installed.empty();
    }
    InstallJob();
}

void UpdateJob()
{
    g_updateMode = true;
    ApplyJob();
}

void SetGameFolder(std::wstring folder)
{
    // A pick of Risen\bin means Risen.
    if (CheckGameFolder(folder) == GameCheck::NoExe) {
        size_t slash = folder.find_last_of(L"\\/");
        if (slash != std::wstring::npos && CheckGameFolder(folder.substr(0, slash)) != GameCheck::NoExe)
            folder.resize(slash);
    }
    std::lock_guard<std::mutex> lock(g_lock);
    g_game = folder;
    g_gameCheck = folder.empty() ? GameCheck::NoExe : CheckGameFolder(folder);
    if (g_gameCheck == GameCheck::NoExe)
        g_game.clear();
    Log("game folder: %ls (%s)", folder.c_str(),
        g_gameCheck == GameCheck::Ok ? "ok" : g_gameCheck == GameCheck::Not64Bit ? "32-bit" : "no bin\\Risen.exe");
    if (!g_game.empty() && g_fetched) {
        ScanInstalled(g_game, &g_mods);
        ScanMenu(g_game, &g_menu);
        g_want.resize(g_mods.size());
        for (size_t i = 0; i < g_mods.size(); ++i)
            g_want[i] = !g_mods[i].installed.empty();
    }
    g_styleLoaded = false;
}

// ---------------------------------------------------------------------------------------------
// UI

ImVec4 kGood = ImVec4(0.18f, 0.38f, 0.10f, 1.0f);
ImVec4 kBad = ImVec4(0.62f, 0.10f, 0.05f, 1.0f);

// Right-aligned on the checkbox line, rightInset pixels left free for the "Remove" button.
void StatusText(const Mod& m, bool want, float rightInset)
{
    char text[128];
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    bool has = !m.installed.empty();
    if (!has && m.version.empty())
        return;
    if (!has) {
        snprintf(text, sizeof(text), "%s %s", want ? T("will install", "будет установлен") : T("not installed", "не установлен"),
                 m.version.c_str());
        if (want)
            color = kGood;
    } else if (!want) {
        snprintf(text, sizeof(text), "%s", T("will be removed", "будет удалён"));
        color = kBad;
    } else if (ModNeedsUpdate(m)) {
        if (m.installed == "?") // installed by hand from a zip
            snprintf(text, sizeof(text), "%s %s", T("installed, update to", "установлен, обновление до"),
                     m.version.c_str());
        else
            snprintf(text, sizeof(text), "%s %s -> %s", T("update", "обновление"), m.installed.c_str(),
                     m.version.c_str());
        color = kGood;
    } else if (m.version.empty()) {
        snprintf(text, sizeof(text), "%s %s", T("installed, not in the release", "установлен, нет в релизе"),
                 m.installed.c_str());
    } else {
        snprintf(text, sizeof(text), "%s %s", T("installed", "установлен"), m.installed.c_str());
    }
    float w = ImGui::CalcTextSize(text).x;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - w - rightInset);
    ImGui::TextColored(color, "%s", text);
}

// Draws the close cross; true when clicked.
bool CloseButton(float size)
{
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - size);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##close", ImVec2(size, size));
    ImU32 bg = ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive
                                  : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered
                                                           : ImGuiCol_Button);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size), bg, ImGui::GetStyle().FrameRounding);
    float m = size * 0.3f, line = size / 14.0f;
    ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddLine(ImVec2(p.x + m, p.y + m), ImVec2(p.x + size - m, p.y + size - m), fg, line);
    dl->AddLine(ImVec2(p.x + size - m, p.y + m), ImVec2(p.x + m, p.y + size - m), fg, line);
    return clicked;
}

// The menu's line under the mods: no checkbox, it follows the mods (call under g_lock).
void MenuRow()
{
    const Mod& m = g_menu;
    if (m.version.empty() && m.installed.empty())
        return; // an older release without a menu zip
    bool anyMod = false;
    for (const Mod& mod : g_mods)
        anyMod |= !mod.installed.empty();
    char text[128];
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    if (MenuNeedsWork(g_mods, m)) {
        if (m.installed.empty())
            snprintf(text, sizeof(text), "%s %s", T("missing, will install", "нет, будет установлено"), m.version.c_str());
        else if (m.installed == "?")
            snprintf(text, sizeof(text), "%s %s", T("installed, update to", "установлено, обновление до"), m.version.c_str());
        else
            snprintf(text, sizeof(text), "%s %s -> %s", T("update", "обновление"), m.installed.c_str(), m.version.c_str());
        color = kGood;
    } else if (!m.installed.empty()) {
        snprintf(text, sizeof(text), "%s %s", T("installed", "установлено"), m.installed.c_str());
    } else {
        snprintf(text, sizeof(text), "%s", anyMod ? "" : T("comes with the first mod", "ставится с первым модом"));
    }
    float indent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::AlignTextToFramePadding();
    ImGui::Indent(indent);
    ImGui::TextUnformatted(T("F10 menu", "Меню F10"));
    float w = ImGui::CalcTextSize(text).x;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - w);
    ImGui::TextColored(color, "%s", text);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushFont(nullptr, 17.0f * g_scale);
    ImGui::TextWrapped("%s", T("Settings of all mods in the game and the status icons. Installed and removed together "
                               "with the mods.",
                               "Настройки всех модов прямо в игре и значки на экране. Ставится и удаляется вместе с "
                               "модами."));
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Unindent(indent);
}

void DrawUi()
{
    ImGuiIO& io = ImGui::GetIO();
    float s = g_scale;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    int colors = PushParchmentColors();
    ImVec2 padding(40.0f * s, 42.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * s, 5.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 12.0f * s);
    ImGui::PushFont(g_font, 20.0f * s);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##installer", nullptr, flags);
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    if (g_atlas)
        DrawParchmentFrame(bg, reinterpret_cast<ImTextureID>(g_atlas), ImVec2(0, 0), io.DisplaySize, s * 1.2f);
    else
        bg->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(222, 204, 165, 255));

    std::unique_lock<std::mutex> lock(g_lock, std::try_to_lock); // the worker holds it while applying
    bool busy = g_busy;

    // Header: title, language, close. Dragging the header moves the window (WM_NCHITTEST).
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, kParchmentTitle);
    ImGui::PushFont(nullptr, 30.0f * s);
    ImGui::TextUnformatted("Risen Mods");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", kVersion);
    float frame = ImGui::GetFrameHeight();
    if (LanguageButton(g_russian, frame + ImGui::GetStyle().ItemSpacing.x))
        g_russian = !g_russian;
    g_headerButtonsX = ImGui::GetItemRectMin().x;
    if (CloseButton(frame))
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    g_headerHeight = ImGui::GetItemRectMax().y + 4.0f * s;

    // A newer installer in the release.
    if (lock.owns_lock() && (!g_newVersion.empty() || g_selfUpdateFailed)) {
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        if (g_selfUpdateFailed) {
            ImGui::TextColored(kBad, "%s", T("Download the new installer by hand:", "Скачайте новый установщик вручную:"));
            ImGui::SameLine();
            if (ImGui::Button(T("Open the releases page", "Открыть страницу релиза")))
                ShellExecuteW(nullptr, L"open", kReleasesPage, nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            ImGui::TextColored(kGood, T("A new installer version is out: %s", "Вышла новая версия установщика: %s"),
                               g_newVersion.c_str());
            ImGui::SameLine();
            ImGui::BeginDisabled(busy);
            if (ImGui::Button(T("Update installer", "Обновить установщик")))
                StartJob(SelfUpdateJob);
            ImGui::EndDisabled();
        }
    }

    // Game folder.
    ImGui::SeparatorText(T("Game folder", "Папка игры"));
    if (!lock.owns_lock()) {
        ImGui::TextDisabled("%s", T("Working...", "Работаю..."));
    } else {
        if (g_game.empty()) {
            ImGui::TextColored(kBad, "%s", T("Risen not found. Choose the folder with bin\\Risen.exe.",
                                             "Risen не найден. Укажите папку, в которой есть bin\\Risen.exe."));
        } else {
            std::string path = Narrow(g_game);
            ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - frame * 5.0f);
            ImGui::TextUnformatted(path.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - frame * 4.5f);
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(T("Change...", "Выбрать..."), ImVec2(frame * 4.5f, 0))) {
            lock.unlock();
            std::wstring folder = PickFolder(g_hwnd, g_russian ? L"Папка Risen" : L"Risen folder");
            if (!folder.empty())
                SetGameFolder(folder);
            lock.lock();
        }
        // After a wrong pick by hand: back to the folder the start-up search finds.
        if (!g_autoGame.empty() && _wcsicmp(g_autoGame.c_str(), g_game.c_str()) != 0) {
            const char* findText = T("Find the game automatically", "Найти игру автоматически");
            float findW = ImGui::CalcTextSize(findText).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - findW);
            if (ImGui::Button(findText, ImVec2(findW, 0))) {
                lock.unlock();
                std::wstring found = FindGameFolder();
                if (!found.empty()) {
                    g_autoGame = found;
                    SetGameFolder(found);
                }
                lock.lock();
            }
        }
        ImGui::EndDisabled();
        if (g_gameCheck == GameCheck::Not64Bit)
            ImGui::TextColored(kBad, "%s", T("This is the old 32-bit Risen: the mods need the 64-bit version (2023).",
                                             "Это старая 32-битная версия Risen: моды работают только с 64-битной (2023)."));
        if (g_gameRunning)
            ImGui::TextColored(kBad, "%s", T("Risen is running - close it to install or remove mods.",
                                             "Risen запущен - закройте игру, чтобы ставить или удалять моды."));
    }

    // Mods.
    char header[96];
    snprintf(header, sizeof(header), "%s%s%s", T("Mods", "Моды"), lock.owns_lock() && !g_tag.empty() ? "  -  " : "",
             lock.owns_lock() ? g_tag.c_str() : "");
    ImGui::SeparatorText(header);
    float footer = ImGui::GetFrameHeight() * 2.0f + ImGui::GetTextLineHeightWithSpacing() * 2.0f +
                   ImGui::GetStyle().ItemSpacing.y * 3.0f;
    bool canWrite = lock.owns_lock() && !busy && !g_game.empty() && !g_gameRunning && g_gameCheck == GameCheck::Ok;
    ImGui::BeginChild("##mods", ImVec2(0, -footer));
    if (lock.owns_lock()) {
        if (!g_fetched) {
            if (busy) {
                long long done = g_progress.done, total = g_progress.total;
                ImGui::TextUnformatted(g_localZips.empty() ? T("Getting the mod list from GitHub...",
                                                               "Получаю список модов с GitHub...")
                                                           : T("Reading the archives...", "Читаю архивы..."));
                char overlay[64];
                snprintf(overlay, sizeof(overlay), "%.1f MB", done / 1048576.0);
                ImGui::ProgressBar(total > 0 ? static_cast<float>(done) / total : -1.0f * static_cast<float>(ImGui::GetTime()),
                                   ImVec2(-FLT_MIN, 0), overlay);
            } else if (!g_fetchError.empty()) {
                ImGui::TextColored(kBad, "%s", T("Couldn't get the mod list:", "Не удалось получить список модов:"));
                ImGui::TextWrapped("%s", g_fetchError.c_str());
                if (ImGui::Button(T("Try again", "Повторить")))
                    StartJob(FetchJob);
            }
        } else {
            for (size_t i = 0; i < g_mods.size(); ++i) {
                const Mod& m = g_mods[i];
                ImGui::PushID(static_cast<int>(i));
                // Ticks choose what to install; an installed mod stays ticked and has its own "Remove" button.
                bool want = g_want[i], has = !m.installed.empty();
                ImGui::BeginDisabled(busy || g_game.empty() || has);
                if (ImGui::Checkbox(m.title.c_str(), &want))
                    g_want[i] = want;
                ImGui::EndDisabled();
                const char* removeText = T("Remove", "Удалить");
                float removeW = ImGui::CalcTextSize(removeText).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                StatusText(m, want, has ? removeW + ImGui::GetStyle().ItemSpacing.x : 0.0f);
                if (has) {
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - removeW);
                    ImGui::BeginDisabled(!canWrite);
                    if (ImGui::Button(removeText, ImVec2(removeW, 0)))
                        g_askRemove = static_cast<int>(i);
                    ImGui::EndDisabled();
                }
                ModDescription d = DescribeMod(m.id);
                if (d.en[0]) {
                    ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::PushFont(nullptr, 17.0f * s);
                    ImGui::TextWrapped("%s", T(d.en, d.ru));
                    ImGui::PopFont();
                    ImGui::PopStyleColor();
                    ImGui::Unindent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
                }
                ImGui::Spacing();
                ImGui::PopID();
            }
            MenuRow();
        }
    }
    ImGui::EndChild();

    // "Remove <mod>?" confirmation.
    if (g_askRemove >= 0) {
        ImGui::OpenPopup("##confirm_remove");
        g_removeIndex = g_askRemove;
        g_askRemove = -1;
    }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * s, 20.0f * s));
    if (ImGui::BeginPopupModal("##confirm_remove", nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        if (lock.owns_lock() && g_removeIndex >= 0 && g_removeIndex < static_cast<int>(g_mods.size())) {
            ImGui::Text(T("Remove %s?", "Удалить %s?"), g_mods[g_removeIndex].title.c_str());
            ImGui::TextDisabled("%s", T("Its settings stay, a later install picks them up.",
                                        "Настройки останутся, при повторной установке подхватятся."));
            ImGui::Spacing();
            float bw = ImGui::GetFontSize() * 6.0f;
            ImGui::BeginDisabled(!canWrite);
            if (ImGui::Button(T("Remove", "Удалить"), ImVec2(bw, 0))) {
                StartJob(RemoveJob);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(T("Cancel", "Отмена"), ImVec2(bw, 0)))
                ImGui::CloseCurrentPopup();
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    // Footer: what will happen, the button, the result of the last run.
    ImGui::Separator();
    if (lock.owns_lock()) {
        int install = 0, update = 0, remove = 0;
        for (size_t i = 0; i < g_mods.size(); ++i) {
            const Mod& m = g_mods[i];
            bool has = !m.installed.empty();
            if (g_want[i] && !has && !m.version.empty())
                ++install;
            else if (!g_want[i] && has)
                ++remove;
            if (has && ModNeedsUpdate(m))
                ++update;
        }
        if (MenuNeedsWork(g_mods, g_menu))
            ++update;
        ImGui::PushTextWrapPos(0.0f);
        if (!g_message.empty())
            ImGui::TextColored(g_messageError ? kBad : kGood, "%s", g_message.c_str());
        else if (install + remove == 0 && std::find(g_want.begin(), g_want.end(), false) == g_want.end())
            ImGui::TextDisabled("%s", T("All mods are installed. In the game: F10 opens their settings.",
                                        "Все моды установлены. В игре: F10 - их настройки."));
        else if (install + remove == 0)
            ImGui::TextDisabled("%s", T("Tick the mods you want and press Install. In the game: F10 opens the "
                                        "settings of all mods.",
                                        "Отметьте нужные моды и нажмите \"Установить\". В игре: F10 - настройки "
                                        "всех модов."));
        else
            ImGui::TextDisabled("%s", T("Your settings are kept on update and removal.",
                                        "Ваши настройки сохраняются при обновлении и удалении."));
        ImGui::PopTextWrapPos();
        // Buttons only when there is something to do: install by the checkboxes, updates separately.
        float buttonH = ImGui::GetFrameHeight() * 1.4f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        bool both = install + remove > 0 && update > 0;
        float updateW = both ? (ImGui::GetContentRegionAvail().x - spacing) * 0.35f : -FLT_MIN;
        ImGui::BeginDisabled(!canWrite);
        if (install + remove > 0) {
            char button[128];
            if (remove == 0)
                snprintf(button, sizeof(button), T("Install (%d)", "Установить (%d)"), install);
            else if (install == 0)
                snprintf(button, sizeof(button), T("Remove (%d)", "Удалить (%d)"), remove);
            else
                snprintf(button, sizeof(button), T("Install %d, remove %d", "Установить %d, удалить %d"), install,
                         remove);
            float w = both ? ImGui::GetContentRegionAvail().x - spacing - updateW : -FLT_MIN;
            if (ImGui::Button(button, ImVec2(w, buttonH)))
                StartJob(InstallJob);
            if (both)
                ImGui::SameLine();
        }
        if (update > 0) {
            char button[64];
            snprintf(button, sizeof(button), T("Update (%d)", "Обновить (%d)"), update);
            if (ImGui::Button(button, ImVec2(updateW, buttonH)))
                StartJob(UpdateJob);
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("%s", T("Downloading and installing...", "Скачиваю и устанавливаю..."));
        char overlay[64];
        long long done = g_progress.done, total = g_progress.total;
        snprintf(overlay, sizeof(overlay), "%.1f MB", done / 1048576.0);
        ImGui::ProgressBar(total > 0 ? static_cast<float>(done) / total : -1.0f * static_cast<float>(ImGui::GetTime()),
                           ImVec2(-FLT_MIN, 0), overlay);
    }
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleVar(7);
    ImGui::PopStyleColor(colors);
}

// ---------------------------------------------------------------------------------------------
// D3D9

bool CreateDevice(HWND hwnd)
{
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d)
        return false;
    g_pp = {};
    g_pp.Windowed = TRUE;
    g_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_pp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    return SUCCEEDED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                         &g_pp, &g_device)) ||
           SUCCEEDED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                         &g_pp, &g_device));
}

// The back buffer as a 32-bit .bmp.
bool SaveScreenshot(const std::wstring& path)
{
    IDirect3DSurface9 *back = nullptr, *copy = nullptr;
    D3DSURFACE_DESC desc;
    bool ok = false;
    if (SUCCEEDED(g_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && SUCCEEDED(back->GetDesc(&desc)) &&
        SUCCEEDED(g_device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy,
                                                        nullptr)) &&
        SUCCEEDED(g_device->GetRenderTargetData(back, copy))) {
        D3DLOCKED_RECT lock;
        FILE* f = nullptr;
        if (SUCCEEDED(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY))) {
            if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
                BITMAPINFOHEADER bi = {sizeof(bi), static_cast<LONG>(desc.Width), -static_cast<LONG>(desc.Height), 1, 32};
                DWORD pixels = desc.Width * desc.Height * 4;
                BITMAPFILEHEADER bf = {0x4D42, sizeof(bf) + sizeof(bi) + pixels, 0, 0, sizeof(bf) + sizeof(bi)};
                fwrite(&bf, sizeof(bf), 1, f);
                fwrite(&bi, sizeof(bi), 1, f);
                for (UINT y = 0; y < desc.Height; ++y)
                    fwrite(static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch, 4, desc.Width, f);
                ok = fclose(f) == 0;
            }
            copy->UnlockRect();
        }
    }
    if (copy)
        copy->Release();
    if (back)
        back->Release();
    Log("screenshot %ls: %s", path.c_str(), ok ? "saved" : "FAILED");
    return ok;
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    if (g_device->Reset(&g_pp) == D3DERR_INVALIDCALL)
        Log("d3d9: Reset failed");
    ImGui_ImplDX9_CreateDeviceObjects();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return 1;
    switch (msg) {
    case WM_NCHITTEST: {
        POINT p = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
        ScreenToClient(hwnd, &p);
        if (p.y >= 0 && p.y < g_headerHeight && p.x < g_headerButtonsX)
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT || LOWORD(lp) == HTCAPTION) {
            SetCursor(g_cursor ? g_cursor : LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_SIZE:
        if (g_device && wp != SIZE_MINIMIZED) {
            g_pp.BackBufferWidth = LOWORD(lp);
            g_pp.BackBufferHeight = HIWORD(lp);
            ResetDevice();
        }
        return 0;
    case WM_DPICHANGED: {
        g_scale = HIWORD(wp) / 96.0f;
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

float SystemScale()
{
    using GetDpiForSystemFn = UINT(WINAPI*)();
    auto getDpi = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    return getDpi ? getDpi() / 96.0f : 1.0f;
}

int RunWindow()
{
    using SetContextFn = BOOL(WINAPI*)(HANDLE);
    if (auto setContext = reinterpret_cast<SetContextFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
        setContext(reinterpret_cast<HANDLE>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    g_scale = SystemScale();

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0, 0, GetModuleHandleW(nullptr)};
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1)); // installer.rc
    wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wc.lpszClassName = L"RisenModsInstaller";
    RegisterClassExW(&wc);
    int w = static_cast<int>(kWidth * g_scale), h = static_cast<int>(kHeight * g_scale);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (h > work.bottom - work.top)
        h = work.bottom - work.top;
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Risen Mods", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
                             work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                             w, h, nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDevice(g_hwnd)) {
        MessageBoxW(nullptr, L"Direct3D 9 is not available.", L"Risen Mods", MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // WM_SETCURSOR sets the game's arrow
    g_font = LoadSystemFont("segoeui.ttf", 20.0f); // the game's font comes with LoadGameStyle
    if (!g_font)
        g_font = io.Fonts->AddFontDefault();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(g_device);

    StartJob(FetchJob);
    DWORD lastCheck = 0;
    int screenshotFrames = 0;
    for (;;) {
        // Sleep while nothing happens; redraw on input, every 250 ms (worker progress, game running) or at once.
        bool animate = g_busy || !g_screenshot.empty();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, animate ? 0 : 250, QS_ALLINPUT);
        MSG msg;
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            quit |= msg.message == WM_QUIT;
        }
        if (quit)
            break;
        if (IsIconic(g_hwnd))
            continue;
        if (GetTickCount() - lastCheck > 1000) {
            lastCheck = GetTickCount();
            g_gameRunning = GameRunning();
        }
        if (!g_styleLoaded)
            LoadGameStyle();

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawUi();
        ImGui::EndFrame();
        g_device->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(22, 17, 12), 1.0f, 0);
        if (SUCCEEDED(g_device->BeginScene())) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_device->EndScene();
        }
        if (!g_screenshot.empty() && !g_busy && (g_fetched || !g_fetchError.empty()) && ++screenshotFrames > 5) {
            SaveScreenshot(g_screenshot);
            PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            g_screenshot.clear();
        }
        HRESULT hr = g_device->Present(nullptr, nullptr, nullptr, nullptr);
        if (hr == D3DERR_DEVICELOST && g_device->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
            ResetDevice();
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_atlas)
        g_atlas->Release();
    g_device->Release();
    g_d3d->Release();
    DestroyWindow(g_hwnd);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Command line mode

std::vector<std::string> SplitIds(const wchar_t* list)
{
    std::vector<std::string> ids;
    std::string all = Narrow(list);
    size_t start = 0;
    while (start <= all.size()) {
        size_t comma = all.find(',', start);
        std::string id = all.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!id.empty())
            ids.push_back(id);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return ids;
}

int RunCommand(const std::vector<std::string>& install, const std::vector<std::string>& remove)
{
    FetchJob();
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_fetched || g_game.empty()) {
        Log("command: %s", g_game.empty() ? "no game folder" : "no mod list");
        return 2;
    }
    for (size_t i = 0; i < g_mods.size(); ++i) {
        const Mod& m = g_mods[i];
        Log("mod %-10s offered %-5s installed %s", m.id.c_str(), m.version.empty() ? "-" : m.version.c_str(),
            m.installed.empty() ? "-" : m.installed.c_str());
        g_want[i] = !m.installed.empty();
        for (const std::string& id : install)
            if (id == m.id)
                g_want[i] = true;
        for (const std::string& id : remove)
            if (id == m.id)
                g_want[i] = false;
    }
    Log("menu %-9s offered %-5s installed %s", "", g_menu.version.empty() ? "-" : g_menu.version.c_str(),
        g_menu.installed.empty() ? "-" : g_menu.installed.c_str());
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    strcat_s(temp, "RisenModsInstaller");
    ModInitAt(temp);
    Log("Risen Mods installer %s", kVersion);
    DeleteOldInstaller(); // left by a self-update
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_russian = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring game;
    std::vector<std::string> install, remove;
    bool command = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        bool hasValue = i + 1 < argc;
        if (a == L"--game" && hasValue)
            game = argv[++i];
        else if (a == L"--zips" && hasValue)
            g_localZips = argv[++i];
        else if (a == L"--install" && hasValue)
            install = SplitIds(argv[++i]), command = true;
        else if (a == L"--remove" && hasValue)
            remove = SplitIds(argv[++i]), command = true;
        else if (a == L"--lang" && hasValue)
            g_russian = _wcsicmp(argv[++i], L"ru") == 0;
        else if (a == L"--screenshot" && hasValue)
            g_screenshot = argv[++i];
        else if (a == L"--list")
            command = true;
    }
    LocalFree(argv);
    g_autoGame = FindGameFolder();
    SetGameFolder(game.empty() ? g_autoGame : game);

    g_commandMode = command;
    if (!command)
        return RunWindow();
    int code = RunCommand(install, remove);
    if (code == 0 && (!install.empty() || !remove.empty())) {
        InstallJob(); // new mods and removals
        if (!g_messageError && !install.empty())
            UpdateJob(); // installed mods with a newer version
        code = g_messageError ? 1 : 0;
    }
    return code;
}
