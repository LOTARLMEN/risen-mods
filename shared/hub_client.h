#pragma once

// Mod side of the Risen Mods menu (hub_api.h).

#include "hub_api.h"

// The menu's API, or nullptr when menu.dll isn't loaded (or too old). Call after the game window exists: by then
// the loader has loaded every mod.
const HubApi* HubGet();
// The menu is open / waits for a key: skip hotkeys. False without the menu.
bool HubBusy();
