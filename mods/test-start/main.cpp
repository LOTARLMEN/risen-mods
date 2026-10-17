// Developer-only "Test" start (TODO 5.23). No README: tools\package.ps1 never ships it.
// The F10 menu (menu.dll) adds a "Test" button next to "New Game" in DLG_MENU_GAME while this DLL is loaded and
// calls RisenTestArm(true) before the vanilla OnClick_NewGame; the vanilla button calls RisenTestArm(false).
// An armed New Game then
// - gets [Test] World instead of "World": gCHUDFileManager::GetDefaultWorldName (Game +0x87bd70, called only by
//   NewGame) re-parses eCApplication::GetCommandLineA, and that one call site gets a copy with -worldfile=<World>
//   (Risen.exe hard-codes -worldfile=World; the engine's own string is left alone);
// - skips the new-game intro video (EndProgress PlayVideo(<lang>_Intro.bik), Game +0x87ac0f);
// - skips the wake-up scene PS_Gamestart (Rtn_Player asks ExistsGameVariable("Gamestart"), Script_Game +0x1af175).
// After the skipped intro (= loading done) World Debug, if loaded, gets RisenDebugTestStart: debugging on,
// [Debug] TargetHour and clear weather once the hero exists.
// The world name is served once; the skips stay on until the next New Game. All three are IAT hooks with a
// return-address check, installed on the first arm after the call sites' bytes are verified; no code is patched.

#include <windows.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

#include "common.h"
#include "iat_patch.h"

namespace {

const char kIni[] = "; Developer test start: the \"Test\" button next to \"New Game\" (needs menu.dll).\r\n"
                    "[Test]\r\n"
                    "; World from Projects.prj loaded by the Test button.\r\n"
                    "World=Testlevel_Plane\r\n";

using CommandLineFn = const void* (*)(const void* app);
using PlayVideoFn = void (*)(void* app, const void* name, bool wait);
using ExistsVarFn = bool (*)(const void* name);
using StringNewFn = void* (*)(void* self, const char* text);
using StringAssignFn = void* (*)(void* self, const char* text);
using StringTextFn = const char* (*)(const void* self);

CommandLineFn g_commandLine;
PlayVideoFn g_playVideo;
ExistsVarFn g_existsVar;
StringNewFn g_stringNew;
StringAssignFn g_stringAssign;
StringTextFn g_stringText;
uintptr_t g_worldReturn, g_introReturn, g_gamestartReturn;

int g_state; // 0 not tried, 1 ready, -1 failed
bool g_armed;   // the next New Game is a test start
bool g_testRun; // the current New Game was a test start: skip intro and wake-up scene
char g_world[64];
alignas(8) char g_line[16]; // our bCString copy of the command line, kept for the whole game

const void* CommandLine(const void* app)
{
    const void* line = g_commandLine(app);
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != g_worldReturn)
        return line;
    // The only caller is NewGame: every New Game, test or not, passes here.
    g_testRun = g_armed;
    if (!g_armed)
        return line;
    g_armed = false;
    const char* text = g_stringText(line);
    const char* token = text ? strstr(text, "-worldfile=World") : nullptr;
    if (!token || (token[16] && token[16] != ' ')) {
        Log("test start: -worldfile=World not in the command line, normal New Game");
        g_testRun = false;
        return line;
    }
    char next[512];
    snprintf(next, sizeof(next), "%.*s-worldfile=%s%s", static_cast<int>(token - text), text, g_world, token + 16);
    g_stringAssign(g_line, next);
    Log("test start: new game in world '%s'", g_world);
    return g_line;
}

void PlayVideo(void* app, const void* name, bool wait)
{
    if (g_testRun && reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_introReturn) {
        Log("test start: intro video skipped");
        // Loading is done: World Debug (if loaded) turns itself on with TargetHour and clear weather.
        HMODULE debug = GetModuleHandleA("world_debug.dll");
        if (auto ready = debug ? reinterpret_cast<void (*)()>(GetProcAddress(debug, "RisenDebugTestStart")) : nullptr)
            ready();
        return;
    }
    g_playVideo(app, name, wait);
}

bool ExistsGameVariable(const void* name)
{
    if (g_testRun && reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_gamestartReturn) {
        static bool logged;
        if (!logged) {
            logged = true;
            Log("test start: wake-up scene PS_Gamestart skipped");
        }
        return true;
    }
    return g_existsVar(name);
}

bool Matches(const uint8_t* code, uintptr_t rva, const uint8_t* bytes, size_t n)
{
    return memcmp(code + rva, bytes, n) == 0;
}

template <typename T>
bool Resolve(const char* dll, T& fn, const char* name)
{
    HMODULE mod = GetModuleHandleA(dll);
    fn = mod ? reinterpret_cast<T>(GetProcAddress(mod, name)) : nullptr;
    if (!fn)
        Log("test start: %s!%s not found", dll, name);
    return fn != nullptr;
}

// Installs one hook; `original` gets whatever the IAT held (another mod's hook is chained).
template <typename T>
bool Hook(HMODULE mod, const char* dll, const char* name, T& original, T hook)
{
    if (!Resolve(dll, original, name))
        return false;
    void* previous = PatchImport(mod, dll, name, reinterpret_cast<void*>(hook));
    if (!previous) {
        Log("test start: %s import not found", name);
        return false;
    }
    original = reinterpret_cast<T>(previous);
    return true;
}

bool Install()
{
    HMODULE game = GetModuleHandleA("Game.dll"), scriptGame = GetModuleHandleA("Script_Game.dll");
    if (!game || !scriptGame)
        return false;
    auto gameCode = reinterpret_cast<const uint8_t*>(game);
    auto scriptCode = reinterpret_cast<const uint8_t*>(scriptGame);
    // Rev. 2045 call sites: call qword ptr [rip+X] into the IAT, next to the instruction that identifies it.
    const uint8_t worldCall[] = {0xff, 0x15, 0xb4, 0x01, 0x24, 0x00};    // +0x87bd96 GetCommandLineA
    const uint8_t worldName[] = {0x48, 0x8d, 0x15, 0x47, 0x05, 0x40, 0x00}; // +0x87bdba lea "worldfile"
    const uint8_t introCall[] = {0xff, 0x15, 0x63, 0x13, 0x24, 0x00};    // +0x87ac0f PlayVideo
    const uint8_t introName[] = {0x48, 0x8d, 0x15, 0xf6, 0x17, 0x40, 0x00}; // +0x87abd3 lea "_Intro.bik"
    const uint8_t startCall[] = {0xff, 0x15, 0xcd, 0x3e, 0x05, 0x00};    // +0x1af175 ExistsGameVariable
    const uint8_t startName[] = {0x48, 0x8d, 0x15, 0x61, 0xd5, 0x0b, 0x00}; // +0x1af160 lea "Gamestart"
    if (!Matches(gameCode, 0x87bd96, worldCall, sizeof(worldCall)) ||
        !Matches(gameCode, 0x87bdba, worldName, sizeof(worldName)) ||
        strcmp(reinterpret_cast<const char*>(gameCode + 0xc7c308), "worldfile") != 0 ||
        !Matches(gameCode, 0x87ac0f, introCall, sizeof(introCall)) ||
        !Matches(gameCode, 0x87abd3, introName, sizeof(introName)) ||
        strcmp(reinterpret_cast<const char*>(gameCode + 0xc7c3d0), "_Intro.bik") != 0 ||
        !Matches(scriptCode, 0x1af175, startCall, sizeof(startCall)) ||
        !Matches(scriptCode, 0x1af160, startName, sizeof(startName)) ||
        strcmp(reinterpret_cast<const char*>(scriptCode + 0x26c6c8), "Gamestart") != 0) {
        Log("test start: unexpected game code (other version?), Test button disabled");
        return false;
    }
    if (!Resolve("SharedBase.dll", g_stringNew, "??0bCString@@QEAA@PEBD@Z") ||
        !Resolve("SharedBase.dll", g_stringAssign, "??4bCString@@QEAAAEAV0@PEBD@Z") ||
        !Resolve("SharedBase.dll", g_stringText, "?GetText@bCString@@QEBAPEBDXZ"))
        return false;
    g_stringNew(g_line, "");
    g_worldReturn = reinterpret_cast<uintptr_t>(gameCode + 0x87bd9c);
    g_introReturn = reinterpret_cast<uintptr_t>(gameCode + 0x87ac15);
    g_gamestartReturn = reinterpret_cast<uintptr_t>(scriptCode + 0x1af17b);
    // The world name last: without it the skips never run.
    return Hook(game, "Engine.dll", "?PlayVideo@eCApplication@@QEAAXAEBVbCString@@_N@Z", g_playVideo, PlayVideo) &&
           Hook(scriptGame, "Script.dll", "?ExistsGameVariable@InfoSystem@@SA_NAEBVbCString@@@Z", g_existsVar,
                ExistsGameVariable) &&
           Hook(game, "Engine.dll", "?GetCommandLineA@eCApplication@@QEBAAEBVbCString@@XZ", g_commandLine,
                CommandLine);
}

bool ValidWorld(const char* world)
{
    if (!*world)
        return false;
    for (const char* c = world; *c; ++c)
        if (!isalnum(static_cast<unsigned char>(*c)) && *c != '_')
            return false;
    return true;
}

} // namespace

// Called by menu.dll on the game thread: true before the Test button's New Game, false before the vanilla one.
// Returns whether the next New Game will be a test start.
extern "C" __declspec(dllexport) bool RisenTestArm(bool arm)
{
    if (!arm) {
        if (g_armed)
            Log("test start: disarmed (vanilla New Game)");
        g_armed = false;
        return false;
    }
    if (!g_state) {
        g_state = Install() ? 1 : -1;
        Log("test start: hooks %s", g_state > 0 ? "installed" : "NOT installed");
    }
    IniString("Test", "World", "Testlevel_Plane", g_world, sizeof(g_world));
    if (g_state < 0 || !ValidWorld(g_world)) {
        if (g_state > 0)
            Log("test start: bad [Test] World '%s'", g_world);
        return false;
    }
    g_armed = true;
    Log("test start: armed, world '%s'", g_world);
    return true;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "test_start");
        IniPath(kIni);
        Log("Risen Test Start 0.1 (dev)");
    }
    return TRUE;
}
