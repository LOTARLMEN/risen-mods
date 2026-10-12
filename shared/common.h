#pragma once

// Code shared by all mods. Each mod DLL links its own copy, so the state below is per mod.
// A mod named "speed" lives in Risen\bin\mods\speed.dll and uses mods\speed.ini.
//
// Log: the whole family writes to ONE file, Risen\bin\mods\RisenMods.log, every line tagged with the mod:
//   12:00:01.234 [speed] ...
// The loader starts it on every launch and keeps the previous launch as RisenMods.prev.log (so a crash survives the
// restart), and writes crashes there with a module+offset stack. A player only ever sends these two files.
// Each launch is capped at kLogMaxBytes; crash reports are written even past the cap.

#include <windows.h>

constexpr long long kLogMaxBytes = 2 * 1024 * 1024;

// Call first thing in DllMain(DLL_PROCESS_ATTACH): the mod's tag in the shared log and its ini path.
void ModInit(HINSTANCE instance, const char* name);
// Standalone programs (the installer): own fresh "<base>.log" and "<base>.ini" instead of the shared log.
void ModInitAt(const char* basePath);
// Loader only: modsDir\RisenMods.log -> RisenMods.prev.log, a fresh log tagged "loader", old per-mod logs removed.
void LogStartSession(const char* modsDir);

void Log(const char* fmt, ...);
// Same, ignoring the size cap (crash reports).
void LogAlways(const char* fmt, ...);
// The log file this module writes to.
const char* LogPath();

// Path of the mod's ini. If it doesn't exist yet, it is created from defaultText (pass nullptr to skip).
const char* IniPath(const char* defaultText = nullptr);
int IniInt(const char* section, const char* key, int def);
float IniFloat(const char* section, const char* key, float def);
// Text value into buf (def when missing).
void IniString(const char* section, const char* key, const char* def, char* buf, int size);
// Key name like "F4", "MOUSE3", "NUMPAD+" or "0x73" -> virtual key code; def when missing or unknown.
int IniKey(const char* section, const char* key, int def);
// Key name -> virtual key code, 0 when empty or unknown.
int ParseKey(const char* name);
// Virtual key code -> name accepted by IniKey ("F4", "MOUSE3", "0x73" for keys without a name).
const char* KeyName(int vk, char* buf, size_t size);
void IniWriteString(const char* section, const char* key, const char* value);
void IniWriteFloat(const char* section, const char* key, float value);
void IniWriteKey(const char* section, const char* key, int vk);
// True once after the ini file was modified (checked by write time).
bool IniChanged();

// The game's main window ("RISEN, V1.0.0 ..."), or nullptr while it doesn't exist yet.
HWND FindGameWindow();
// Blocks until the game window exists.
HWND WaitForGameWindow();

// Replaces the game window's procedure; returns the previous one for CallOriginalWndProc.
// Every mod posts its own WM_APP + 0xNNxx messages to the shared window, one block per mod - a duplicate is
// swallowed by whichever mod sees it first: speed 0x52xx, window 0x53xx, autoloot 0x54xx, no-limits 0x55xx,
// menu 0x56xx, quick-lockpick 0x57xx.
WNDPROC SubclassWindow(HWND hwnd, WNDPROC proc);
LRESULT CallOriginalWndProc(WNDPROC orig, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
