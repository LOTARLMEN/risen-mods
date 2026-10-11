#pragma once

// Shared between the menu mod's files. Game thread only unless noted.

#include <windows.h>
#include <vector>

#include "hub_api.h"

struct MenuConfig {
    int key = VK_F10;
    bool russian = false;
    bool icons = true;
    float iconScale = 1.0f;
};

// main.cpp
const MenuConfig& MenuGetConfig();
void MenuSetLanguage(bool russian); // saved to menu.ini
// Registered pages / status icons sorted by order. Lock around every use (mods register from their threads).
void RegistryLock();
void RegistryUnlock();
const std::vector<const HubPage*>& Pages();
const std::vector<const HubStatus*>& Statuses();
void MenuSetOpen(HWND hwnd, bool open);
bool MenuIsOpen();    // any thread
void MenuSetWaitingKey(bool waiting); // any thread reads it: hotkeys are off while a key is being rebound
bool MenuIsWaitingKey();
float MenuTakeWheel(); // wheel notches since the last call (positive = away from the user)

// overlay.cpp: Dear ImGui on the game's frame. Call OverlayInstall from DllMain.
bool OverlayInstall();
bool OverlayReady();
void* OverlayAtlas(); // IDirect3DTexture9* of the game's GUI atlas (game_assets.h), null = plain look

// window.cpp: the settings window, inside an ImGui frame.
void WindowOpened(); // re-reads every page's ini
void WindowDraw(float scale);
