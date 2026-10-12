#pragma once

// Risen Mods menu (bin\mods\menu.dll): one in-game settings window (F10) for all mods and the status icons in the
// top-left corner. A mod describes its settings as a list of items bound to keys of its own ini; the menu draws
// them, writes the ini and then calls the mod's onChanged on the game thread.
// Plain C ABI: every mod is a separate DLL with its own CRT and ImGui. Mods keep working without the menu (ini
// only) - get the API through hub_client.h, which returns nullptr when menu.dll isn't installed.

#define HUB_API_VERSION 1

enum HubItemType {
    HubHeader,   // label as a section title with a line above
    HubNote,     // grey text
    HubCheckbox, // ini value 0 / 1
    HubSlider,   // ini value is a number; shown = ini * displayScale
    HubKey,      // ini value is a key name ("F", "MOUSE2", "F10")
};

struct HubItem {
    int type;            // HubItemType
    const char* section; // ini section and key (not used by HubHeader / HubNote)
    const char* key;
    const char* labelEn;
    const char* labelRu;
    float def;           // value when the ini has none: checkbox 0/1, slider (ini units), key (virtual key code)
    float min, max;      // slider range, shown units
    float step;          // slider: shown value is rounded to this (0 = no rounding)
    float displayScale;  // slider: shown = ini * displayScale (0 means 1)
    const char* format;  // slider: printf format of the shown value ("%.1f" when null)
    int parent;          // index of a checkbox item ABOVE this one that has to be on for this one to be editable, -1 = none
    const char* tipEn;   // tooltip, may be null
    const char* tipRu;
};

struct HubPage {
    const char* id; // unique: "speed", "autoloot"
    int order;      // tabs left to right
    const char* titleEn;
    const char* titleRu;
    const char* iniPath;
    const HubItem* items;
    int itemCount;
    void (*onChanged)(); // game thread, after the menu saved a value; may be null (ini hot reload)
};

enum HubIcon { HubIconHourglass, HubIconHand };

#define HUB_STATUS_DIM '\x01'

struct HubStatus {
    const char* id;
    int order; // left to right
    int icon;  // HubIcon
    // Game thread, every frame: true shows the icon; text (empty by default) is drawn next to it, e.g. "x3".
    // Text starting with HUB_STATUS_DIM draws the icon dimmed and still (on, but paused); the marker isn't shown.
    bool (*query)(char* text, int size);
};

struct HubApi {
    int version; // HUB_API_VERSION
    // Any thread. The structures and strings must stay valid (static data in the mod).
    void (*registerPage)(const HubPage* page);
    void (*registerStatus)(const HubStatus* status);
    // Any thread: the menu is open or waits for a key - mods must ignore their hotkeys meanwhile.
    bool (*isBusy)();
};

// Exported by menu.dll.
extern "C" typedef const HubApi* (*RisenHubApiFn)();
