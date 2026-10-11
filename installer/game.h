#pragma once

// Finding the Risen folder and checking it.

#include <windows.h>
#include <string>

// Folder with bin\Risen.exe: the folder the installer runs from (or its parent), then Steam (registry ->
// libraryfolders.vdf -> appmanifest_40300.acf). Empty when not found.
std::wstring FindGameFolder();

enum class GameCheck { Ok, NoExe, Not64Bit };
GameCheck CheckGameFolder(const std::wstring& folder);

// True while a Risen.exe process runs.
bool GameRunning();

// Folder picker; empty when cancelled.
std::wstring PickFolder(HWND owner, const wchar_t* title);
