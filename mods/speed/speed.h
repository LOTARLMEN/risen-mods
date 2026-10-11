#pragma once

// Shared between the speed mod's files. Game thread only unless noted.

#include <windows.h>

struct SpeedConfig {
    bool enabled = true;
    float multiplier = 3.0f;
    int toggleKey = 'Z';
    bool indicator = true;
    float indicatorScale = 1.0f;
    int panelKey = VK_F10;
    bool russian = false; // panel language
};

// main.cpp
const SpeedConfig& SpeedGetConfig();
// Applies the settings and writes them to speed.ini.
void SpeedSetConfig(const SpeedConfig& c);
// Applies without saving (slider being dragged).
void SpeedApplyLive(const SpeedConfig& c);
bool SpeedIsActive();
// Stops game time while the panel is open (eCTimer scale ~0) and restores it afterwards.
void SpeedFreeze(bool freeze);

// overlay.cpp: indicator + panel drawn with Dear ImGui on the game's frame. Call OverlayInstall from DllMain.
bool OverlayInstall();
bool OverlayReady(); // ImGui is up: the panel can be shown

// panel.cpp: settings window.
bool PanelIsOpen();     // any thread
bool PanelIsWaitingKey(); // any thread: "press a key" for rebinding, hotkeys are off meanwhile
void PanelSetOpen(HWND hwnd, bool open);
void PanelDraw(float scale); // inside an ImGui frame
