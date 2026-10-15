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
// Item values in ini units (read once per page, shared with the window); writing saves the ini + onChanged.
std::vector<float>& MenuValues(const HubPage* page);
void MenuWriteValue(const HubPage& page, int index, float value);

// native_page.cpp: the "Моды" tab in the game's own options menu (TODO 5.12 M3-M6). Game thread.
void NativeTick();
bool NativeOnEscape();
bool NativeOnWheel(int delta); // true = the wheel scrolled a long mod's rows // true = Esc used (back to the mod list / rebind cancelled), don't pass it to the game
