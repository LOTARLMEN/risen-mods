#pragma once

// Code shared by all mods. Each mod DLL links its own copy, so the state below is per mod.
// A mod named "speed" lives in Risen\bin\mods\speed.dll and uses mods\speed.ini and mods\speed.log.

#include <windows.h>

// Call first thing in DllMain(DLL_PROCESS_ATTACH). Sets up the paths and starts a fresh log.
void ModInit(HINSTANCE instance, const char* name);
// Same with an explicit path without extension: "<base>.log", "<base>.ini".
void ModInitAt(const char* basePath);

void Log(const char* fmt, ...);

// Path of the mod's ini. If it doesn't exist yet, it is created from defaultText (pass nullptr to skip).
const char* IniPath(const char* defaultText = nullptr);
int IniInt(const char* section, const char* key, int def);
float IniFloat(const char* section, const char* key, float def);
// Key name like "F4", "MOUSE3", "NUMPAD+" or "0x73" -> virtual key code; def when missing or unknown.
int IniKey(const char* section, const char* key, int def);
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
WNDPROC SubclassWindow(HWND hwnd, WNDPROC proc);
LRESULT CallOriginalWndProc(WNDPROC orig, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
