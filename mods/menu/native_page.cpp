// The "Моды" tab in the game's own options menu (TODO 5.12): the same pages and items as the F10 window, drawn by
// the game's GUI (Steam Rev. 2045, notes/ui-recon.md sections 7-8).
// - DLG_MENU_OPTIONS (gEHudPage 18) gets a 6th tab, a clone of "Производительность" with OnClick = RM_OnClick_Mods;
//   the tab row is laid out again, centred on a wider background.
// - The tab opens the "Параметры игры" sub-page (22) in "mods mode": its own controls are hidden and our clones of
//   them are shown instead: a list of mod buttons; a button opens the mod's rows of label + spin button
//   (‹ value ›), "Back" or Esc returns to the list.
// - A spin button calls its OnSpin script with {int action; bCUnicodeString text}: action 3 only asks for the
//   text, any other action is a click on an arrow. Each row has its own script name (RM_OnSpin_NN).
// - The script names come from bin\scripts\Script_RisenMods.dll, which forwards them to RisenScriptEvent below.
// - Leaving the page (another tab, closing the options) or the "Параметры игры" tab gives the page its own
//   controls back.
// Windows are found by class and position, every call is checked against the expected layout and wrapped in SEH:
// on another game version nothing is added.

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

#include "common.h"
#include "input_capture.h"
#include "menu.h"

namespace {

struct Point {
    int x, y;
};

struct Api {
    void* (*getSystem)();
    void* (*findPage)(void* system, int page);
    bool (*isVisible)(void* system, int page);
    void* (*firstChild)(void* wnd);
    void* (*nextSibling)(void* wnd);
    uint16_t (*getId)(void* wnd);
    Point* (*getPosition)(void* wnd, Point* ret);
    Point* (*getSize)(void* wnd, Point* ret);
    void* (*getText)(void* wnd, void* retUnicode);
    void (*setText)(void* wnd, const void* unicode);
    void (*setPosition)(void* wnd, const Point* pos);
    void (*setSize)(void* wnd, const Point* size);
    void (*setVisible)(void* wnd, const bool* visible);
    void (*setDisabled)(void* wnd, const bool* disabled);
    void (*setId)(void* wnd, const uint16_t* id);
    void* (*clone)(void* wnd, void* parent);
    void (*setOnClick)(void* button, const void* proxy);
    void (*setOnSpin)(void* spin, const void* proxy);
    void (*setSpinBuddy)(void* spin, const uint16_t* id);
    const void* (*scriptName)(const void* proxy); // bCString
    void* (*proxyNew)(void* self, const void* name);
    int64_t (*proxyCall)(const void* self, void* selfEntity, void* otherEntity, int64_t param);
    bool (*guiOpen)(void* gui, int page);
    void (*setPageMode)(void* gui, const int* mode, bool flag);
    void (*newGame)(void* world);
    void* gui2;
    void* world;
    const char* (*stringText)(const void* str);
    void* (*stringNew)(void* self, const char* text);
    const wchar_t* (*unicodeText)(const void* str);
    void* (*unicodeNew)(void* self, const wchar_t* text);
    void* (*unicodeAssign)(void* self, const wchar_t* text);
    void (*unicodeFree)(void* str);
    void (*bringToFront)(void* cfxWnd); // FFC.dll CFXWnd
};

Api g;
bool g_missing;

constexpr int kPageMain = 13, kPageGame = 14, kPageStart = 26, kPageOptions = 18, kPageSettings = 22;
constexpr int kRows = 14; // row 0 = the mod selector
constexpr int kRowTop = 180, kRowStep = 30;
constexpr uint16_t kIdBase = 2100; // our spin buttons' IDs (the page's own are 1001..1010)
constexpr int kSpinOffset = 0xF0;  // gCSpinButton::GetSpinBuddy = lea rax,[rcx+0F0h]
constexpr int kOnClickOffset = 0x150; // eCScriptRadioButton::GetOnClick = lea rax,[rcx+150h]

template <typename T>
void Resolve(HMODULE mod, const char* modName, T& fn, const char* name)
{
    fn = mod ? reinterpret_cast<T>(GetProcAddress(mod, name)) : nullptr;
    if (!fn) {
        Log("native: %s!%s not found", modName, name);
        g_missing = true;
    }
}

bool ApiInit()
{
    static int state;
    if (state)
        return state > 0;
    HMODULE game = GetModuleHandleA("Game.dll"), engine = GetModuleHandleA("Engine.dll");
    HMODULE base = GetModuleHandleA("SharedBase.dll"), script = GetModuleHandleA("Script.dll");
    HMODULE ffc = GetModuleHandleA("FFC.dll");
    if (!game || !engine || !base || !script || !ffc)
        return false;
    Resolve(game, "Game", g.getSystem, "?GetSystem@gCHudModule2@@SAPEAVgCHudSystem2@@XZ");
    Resolve(game, "Game", g.findPage, "?FindPage@gCHudSystem2@@QEBAPEAVgCHudPage2@@W4gEHudPage@@@Z");
    Resolve(game, "Game", g.isVisible, "?IsVisible@gCHudSystem2@@QEBA_NW4gEHudPage@@@Z");
    Resolve(game, "Game", g.setOnSpin, "?SetOnSpin@gCSpinButton@@QEAAXAEBVeCScriptProxyScript@@@Z");
    Resolve(game, "Game", g.setSpinBuddy, "?SetSpinBuddy@gCSpinButton@@QEAAXAEBG@Z");
    Resolve(engine, "Engine", g.firstChild, "?GetFirstChild@eCGuiWindow2@@QEBAPEAV1@XZ");
    Resolve(engine, "Engine", g.nextSibling, "?GetNextSibling@eCGuiWindow2@@QEBAPEAV1@XZ");
    Resolve(engine, "Engine", g.getId, "?GetID@eCGuiWindow2@@QEBAGXZ");
    Resolve(engine, "Engine", g.getPosition, "?GetPosition@eCGuiWindow2@@QEBA?AVbCPoint@@XZ");
    Resolve(engine, "Engine", g.getSize, "?GetSize@eCGuiWindow2@@QEBA?AVbCPoint@@XZ");
    Resolve(engine, "Engine", g.getText, "?GetText@eCGuiWindow2@@QEBA?AVbCUnicodeString@@XZ");
    Resolve(engine, "Engine", g.setText, "?SetText@eCGuiWindow2@@QEAAXAEBVbCUnicodeString@@@Z");
    Resolve(engine, "Engine", g.setPosition, "?SetPosition@eCGuiWindow2@@QEAAXAEBVbCPoint@@@Z");
    Resolve(engine, "Engine", g.setSize, "?SetSize@eCGuiWindow2@@QEAAXAEBVbCPoint@@@Z");
    Resolve(engine, "Engine", g.setVisible, "?SetVisible@eCGuiWindow2@@QEAAXAEB_N@Z");
    Resolve(engine, "Engine", g.setDisabled, "?SetDisabled@eCGuiWindow2@@QEAAXAEB_N@Z");
    Resolve(engine, "Engine", g.setId, "?SetID@eCGuiWindow2@@QEAAXAEBG@Z");
    Resolve(engine, "Engine", g.clone, "?Clone@eCGuiWindow2@@QEAAPEAV1@PEAV1@@Z");
    Resolve(engine, "Engine", g.setOnClick, "?SetOnClick@eCScriptRadioButton@@QEAAXAEBVeCScriptProxyScript@@@Z");
    Resolve(engine, "Engine", g.scriptName, "?GetScriptName@eCScriptProxy@@QEBAAEBVbCString@@XZ");
    Resolve(engine, "Engine", g.proxyNew, "??0eCScriptProxyScript@@QEAA@AEBVbCString@@@Z");
    Resolve(engine, "Engine", g.proxyCall, "??ReCScriptProxyScript@@QEBA_JPEAVeCEntity@@0_J@Z");
    Resolve(script, "Script", g.guiOpen, "?Open@GUI2@@QEAA_NW4gEHudPage@@@Z");
    Resolve(script, "Script", g.setPageMode, "?SetPageMode@GUI2@@QEAAXAEBW4gEPageMode@@_N@Z");
    Resolve(script, "Script", g.newGame, "?NewGame@World@@QEAAXXZ");
    Resolve(script, "Script", g.gui2, "gui2");
    Resolve(script, "Script", g.world, "world");
    Resolve(base, "SharedBase", g.stringText, "?GetText@bCString@@QEBAPEBDXZ");
    Resolve(base, "SharedBase", g.stringNew, "??0bCString@@QEAA@PEBD@Z");
    Resolve(base, "SharedBase", g.unicodeText, "?GetText@bCUnicodeString@@QEBAPEB_WXZ");
    Resolve(base, "SharedBase", g.unicodeNew, "??0bCUnicodeString@@QEAA@PEB_W@Z");
    Resolve(base, "SharedBase", g.unicodeAssign, "??4bCUnicodeString@@QEAAAEAV0@PEB_W@Z");
    Resolve(base, "SharedBase", g.unicodeFree, "??1bCUnicodeString@@QEAA@XZ");
    Resolve(ffc, "FFC", g.bringToFront, "?BringToFront@CFXWnd@@QEAAXXZ");
    state = g_missing ? -1 : 1;
    Log("native: game api %s", state > 0 ? "ready" : "INCOMPLETE (game updated?), no Mods tab");
    return state > 0;
}

// ---------------------------------------------------------------------------------------------
// Window helpers. All behind SEH: this is undocumented engine code.

const char* Rtti(void* obj)
{
    __try {
        auto* vtbl = *reinterpret_cast<uintptr_t**>(obj);
        auto* col = reinterpret_cast<const uint32_t*>(vtbl[-1]);
        if (col[0] != 1)
            return "";
        uintptr_t image = reinterpret_cast<uintptr_t>(col) - col[5];
        return reinterpret_cast<const char*>(image + col[3] + 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "";
    }
}

bool Is(void* wnd, const char* cls)
{
    return strcmp(Rtti(wnd), cls) == 0;
}

void* Child(void* wnd, bool first)
{
    __try {
        return first ? g.firstChild(wnd) : g.nextSibling(wnd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

std::vector<void*> Children(void* wnd)
{
    std::vector<void*> list;
    for (void* w = Child(wnd, true); w && list.size() < 500; w = Child(w, false))
        list.push_back(w);
    return list;
}

bool HasChild(void* parent, void* child)
{
    for (void* w = Child(parent, true); w; w = Child(w, false))
        if (w == child)
            return true;
    return false;
}

struct Geometry {
    Point pos = {}, size = {};
    uint16_t id = 0;
};

Geometry Geo(void* wnd)
{
    Geometry r;
    __try {
        g.getPosition(wnd, &r.pos);
        g.getSize(wnd, &r.size);
        r.id = g.getId(wnd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = Geometry();
    }
    return r;
}

bool HasText(void* wnd)
{
    alignas(16) char str[32] = {};
    bool has = false;
    __try {
        g.getText(wnd, str);
        const wchar_t* t = g.unicodeText(str);
        has = t && *t;
        g.unicodeFree(str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return has;
}

const wchar_t* Wide(const char* utf8)
{
    static wchar_t buf[256];
    buf[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, buf, 256);
    buf[255] = 0;
    return buf;
}

void SetText(void* wnd, const wchar_t* text)
{
    alignas(16) char str[32] = {};
    __try {
        g.unicodeNew(str, text);
        g.setText(wnd, str);
        g.unicodeFree(str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void Show(void* wnd, bool visible)
{
    if (!wnd)
        return;
    __try {
        g.setVisible(wnd, &visible);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void Disable(void* wnd, bool disabled)
{
    if (!wnd)
        return;
    __try {
        g.setDisabled(wnd, &disabled);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// A clone is added under its siblings (the parchment panel covers it): bring it up. The window's CFXWnd is at
// +0xA0 (every eCGuiWindow2 method starts with mov rcx,[rcx+0A0h]).
void BringToFront(void* wnd)
{
    if (!wnd)
        return;
    __try {
        if (void* cfx = *reinterpret_cast<void**>(reinterpret_cast<char*>(wnd) + 0xA0))
            g.bringToFront(cfx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// All our x coordinates are for the 1365 px wide 16:9 screen; on 4:3 / 5:4 the GUI is 1024 wide and the vanilla
// layout sits g_dx = -170 px to the left (ConfigDefault.xml GUI.Aspect*.VirtalWidth).
int g_dx;

void Place(void* wnd, int x, int y, int w, int h)
{
    Point pos = {x + g_dx, y}, size = {w, h};
    __try {
        g.setSize(wnd, &size);
        g.setPosition(wnd, &pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void* Clone(void* wnd, void* parent)
{
    __try {
        return g.clone(wnd, parent);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

const char* OnClickName(void* button)
{
    __try {
        const void* name = g.scriptName(reinterpret_cast<char*>(button) + kOnClickOffset);
        const char* s = name ? g.stringText(name) : nullptr;
        return s ? s : "";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "";
    }
}

// eCScriptProxyScript by name, built once and kept for the whole game (names must be static strings).
void* Proxy(const char* name)
{
    struct Slot {
        const char* name;
        alignas(16) char data[64];
    };
    static Slot slots[64];
    for (Slot& s : slots) {
        if (s.name == name)
            return s.data;
        if (!s.name) {
            alignas(8) char str[16] = {};
            g.stringNew(str, name);
            g.proxyNew(s.data, str);
            s.name = name; // the bCString is leaked on purpose: a few bytes, once per name
            return s.data;
        }
    }
    return nullptr;
}

bool SetOnClick(void* button, const char* script)
{
    __try {
        g.setOnClick(button, Proxy(script));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetSpin(void* spin, uint16_t buddy, const char* script)
{
    __try {
        g.setSpinBuddy(spin, &buddy);
        if (script)
            g.setOnSpin(spin, Proxy(script));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SetId(void* wnd, uint16_t id)
{
    __try {
        g.setId(wnd, &id);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint16_t SpinBuddy(void* spin)
{
    __try {
        return *reinterpret_cast<uint16_t*>(reinterpret_cast<char*>(spin) + kSpinOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Our controls. Two views on the settings page:
// - the list: one button per registered mod (clones of the page's "Apply" button);
// - a mod: its items as rows (label + ‹ value ›), a "Back" button and, for long mods, a page spin. Esc = Back.

const char* Text(const char* en, const char* ru)
{
    return MenuGetConfig().russian && ru ? ru : en;
}

constexpr int kButtonTop = 190, kButtonStep = 52, kButtonWidth = 420, kButtonHeight = 44;
constexpr int kBottom = 603;  // the Back button and the page spin
constexpr int kPagerRow = kRows; // its script RM_OnSpin_14
constexpr int kBackLeft = 298, kBackWidth = 200; // bottom-left corner of the parchment (282..1082 x 140..640)

struct Row {
    void* header = nullptr;
    void* label = nullptr;
    void* left = nullptr;
    void* value = nullptr;
    void* right = nullptr;
};

struct Ui {
    void* options = nullptr;  // page 18
    void* settings = nullptr; // page 22
    void* tab = nullptr;
    void* title = nullptr;
    Row rows[kRows + 1];      // + the page spin (no label)
    void* buttons[kRows] = {};
    void* back = nullptr;
    std::vector<void*> vanilla; // page 22's own controls, hidden while ours are shown
};

Ui g_ui;
bool g_built, g_failed, g_active;

std::vector<const HubPage*> g_mods; // snapshot of the registry when the page opens
int g_mod = -1;                     // -1 = the list
int g_first;                        // a long mod scrolls: first item shown (wheel 2 rows, page spin kRows)
int g_rowItem[kRows];               // item index of each row, -1 = empty
int g_waitRow = -1;                 // row waiting for a key
ULONGLONG g_switched;                // when the view last changed
int g_pending = -2;                 // view to open on the next tick (-1 list, -2 none): see RisenScriptEvent
bool g_keyDown[256];
int g_releaseKey; // the key just bound: input stays captured until it is up (RMB-up = "back" too)
bool g_captured; // the game gets no input while a key is being rebound (RMB = "back" in its menus)

char g_spinNames[kRows + 1][16];
char g_buttonNames[kRows][20];

void* FindTemplate(const std::vector<void*>& list, const char* cls, int x, int y, int w, int h)
{
    for (void* wnd : list) {
        Geometry geo = Geo(wnd);
        if (geo.pos.x == x + g_dx && geo.pos.y == y && geo.size.x == w && geo.size.y == h && Is(wnd, cls))
            return wnd;
    }
    return nullptr;
}

bool BuildTab()
{
    std::vector<void*> tabs;
    void* background = nullptr;
    void* settingsTab = nullptr;
    for (void* w : Children(g_ui.options)) {
        Geometry geo = Geo(w);
        if (geo.pos.y == 710 && geo.size.y == 23 && Is(w, ".?AVeCScriptRadioButton@@")) {
            tabs.push_back(w);
            if (strcmp(OnClickName(w), "OnClick_Menu_Settings") == 0)
                settingsTab = w;
        } else if (geo.pos.x == 234 + g_dx && geo.pos.y == 706 && geo.size.x == 896 && geo.size.y == 32) {
            background = w;
        }
    }
    if (tabs.size() != 5 || !background || !settingsTab) {
        Log("native: options tab row not as expected (%zu tabs, background %p, settings tab %p)", tabs.size(),
            background, settingsTab);
        return false;
    }
    for (size_t i = 0; i < tabs.size(); ++i)
        for (size_t j = i + 1; j < tabs.size(); ++j)
            if (Geo(tabs[j]).pos.x < Geo(tabs[i]).pos.x)
                std::swap(tabs[i], tabs[j]);
    void* tab = Clone(tabs.back(), g_ui.options);
    if (!tab || !SetOnClick(tab, "RM_OnClick_Mods") || !SetOnClick(settingsTab, "RM_OnClick_Settings"))
        return false;
    SetText(tab, Wide(Text("Mods", "Моды")));
    tabs.push_back(tab);
    // Vanilla: 164 px tabs, 8 px gaps, 24 px margins on an 896 px background. Six tabs don't fit: widen the
    // background instead of narrowing the tabs (that cuts "Производительность"), centred on the screen.
    // 1072 px is wider than the 1024 px 4:3 screen: there 4 px gaps and 6 px margins (1016 px).
    constexpr int kWidth = 164;
    bool narrow = g_dx < 0;
    int gap = narrow ? 4 : 8, margin = narrow ? 6 : 24;
    int back = 6 * kWidth + 5 * gap + 2 * margin, backLeft = (1365 - back) / 2;
    Place(background, backLeft, 706, back, 32);
    for (size_t i = 0; i < tabs.size(); ++i)
        Place(tabs[i], backLeft + margin + static_cast<int>(i) * (kWidth + gap), 710, kWidth, 23);
    BringToFront(tab);
    g_ui.tab = tab;
    return true;
}

// Developer only, while bin\mods\test_start.dll is loaded (TODO 5.23): a "Test" button right of "New Game" in
// DLG_MENU_GAME. Both buttons call the vanilla OnClick_NewGame; before it, test_start arms (Test) or disarms
// (New Game) the test world. The row's 4 buttons of 205 px become 5 of 164 px on the same 896 px background.
using TestArmFn = bool (*)(bool arm);

TestArmFn TestArm()
{
    HMODULE m = GetModuleHandleA("test_start.dll");
    return m ? reinterpret_cast<TestArmFn>(GetProcAddress(m, "RisenTestArm")) : nullptr;
}

struct TestUi {
    void* page = nullptr; // page 14
    void* button = nullptr;
    bool failed = false;
};

TestUi g_test;

void BuildTestButton(void* system)
{
    g_test = TestUi();
    g_test.page = g.findPage(system, kPageGame);
    std::vector<void*> row;
    void* background = nullptr;
    void* newGame = nullptr;
    for (void* w : g_test.page ? Children(g_test.page) : std::vector<void*>()) {
        Geometry geo = Geo(w);
        if (geo.pos.y == 710 && geo.size.x == 205 && geo.size.y == 23 && Is(w, ".?AVeCScriptRadioButton@@")) {
            row.push_back(w);
            if (strcmp(OnClickName(w), "OnClick_NewGame") == 0)
                newGame = w;
        } else if (geo.pos.x == 234 + g_dx && geo.pos.y == 706 && geo.size.x == 896 && geo.size.y == 32) {
            background = w;
        }
    }
    if (row.size() != 4 || !background || !newGame) {
        Log("native: game menu row not as expected (%zu buttons, background %p, New Game %p), no Test button",
            row.size(), background, newGame);
        g_test.failed = true;
        return;
    }
    for (size_t i = 0; i < row.size(); ++i)
        for (size_t j = i + 1; j < row.size(); ++j)
            if (Geo(row[j]).pos.x < Geo(row[i]).pos.x)
                std::swap(row[i], row[j]);
    void* test = Clone(newGame, g_test.page);
    if (!test || !SetOnClick(test, "RM_OnClick_Test") || !SetOnClick(newGame, "RM_OnClick_NewGame")) {
        Log("native: Test button not created");
        g_test.failed = true;
        return;
    }
    SetText(test, Wide(Text("Test", "Тест")));
    row.insert(std::find(row.begin(), row.end(), newGame) + 1, test);
    constexpr int kWidth = 164, kGap = 8;
    constexpr int kLeft = 234 + (896 - 5 * kWidth - 4 * kGap) / 2;
    for (size_t i = 0; i < row.size(); ++i)
        Place(row[i], kLeft + static_cast<int>(i) * (kWidth + kGap), 710, kWidth, 23);
    BringToFront(test);
    g_test.button = test;
    Log("native: Test button added (dev, test_start.dll)");
}

bool BuildRow(Row& row, int r, void* header, void* label, void* left, void* value, void* right)
{
    int y = r == kPagerRow ? kBottom - 2 : kRowTop + r * kRowStep;
    uint16_t id = static_cast<uint16_t>(kIdBase + r);
    snprintf(g_spinNames[r], sizeof(g_spinNames[r]), "RM_OnSpin_%02d", r);
    if (r != kPagerRow) {
        row.header = Clone(header, g_ui.settings);
        row.label = Clone(label, g_ui.settings);
        if (!row.header || !row.label)
            return false;
        Place(row.header, 315, y, 250, 25);
        Place(row.label, 353, y, 307, 24);
    }
    row.left = Clone(left, g_ui.settings);
    row.value = Clone(value, g_ui.settings);
    row.right = Clone(right, g_ui.settings);
    if (!row.left || !row.value || !row.right || !SetId(row.value, id) || !SetSpin(row.value, id, g_spinNames[r]) ||
        !SetSpin(row.left, id, nullptr) || !SetSpin(row.right, id, nullptr))
        return false;
    Place(row.left, 662, y + 2, 16, 22);
    Place(row.value, 678, y + 2, 253, 22);
    Place(row.right, 931, y + 2, 16, 22);
    return true;
}

bool BuildPage()
{
    std::vector<void*> list = Children(g_ui.settings);
    void* title = FindTemplate(list, ".?AVeCGuiStatic2@@", 522, 139, 310, 24);
    void* header = FindTemplate(list, ".?AVeCGuiStatic2@@", 315, 180, 250, 25);
    void* label = FindTemplate(list, ".?AVeCGuiStatic2@@", 353, 218, 307, 24);
    void* value = FindTemplate(list, ".?AVgCSpinButton@@", 678, 220, 253, 22);
    void* left = FindTemplate(list, ".?AVgCSpinButton@@", 662, 220, 16, 22);
    void* right = FindTemplate(list, ".?AVgCSpinButton@@", 931, 220, 16, 22);
    void* apply = FindTemplate(list, ".?AVeCScriptRadioButton@@", 662, 528, 285, 28);
    if (!title || !header || !label || !value || !left || !right || !apply || SpinBuddy(left) != Geo(value).id ||
        SpinBuddy(right) != Geo(value).id) {
        Log("native: settings page not as expected (title %p header %p label %p spin %p %p %p apply %p)", title,
            header, label, left, value, right, apply);
        return false;
    }
    // The page's own controls: everything but the parchment panel and the pictures (statics without text).
    for (void* w : list)
        if (!Is(w, ".?AVeCGuiPanel2@@") && !(Is(w, ".?AVeCGuiStatic2@@") && !HasText(w)))
            g_ui.vanilla.push_back(w);

    g_ui.title = Clone(title, g_ui.settings);
    if (!g_ui.title)
        return false;
    for (int r = 0; r <= kRows; ++r)
        if (!BuildRow(g_ui.rows[r], r, header, label, left, value, right))
            return false;
    for (int i = 0; i < kRows; ++i) {
        snprintf(g_buttonNames[i], sizeof(g_buttonNames[i]), "RM_OnClick_Item_%02d", i);
        void* b = Clone(apply, g_ui.settings);
        if (!b || !SetOnClick(b, g_buttonNames[i]))
            return false;
        Place(b, -2000, kButtonTop + i * kButtonStep, kButtonWidth, kButtonHeight);
        g_ui.buttons[i] = b;
    }
    g_ui.back = Clone(apply, g_ui.settings);
    if (!g_ui.back || !SetOnClick(g_ui.back, "RM_OnClick_Back"))
        return false;
    Place(g_ui.back, 353, kBottom, 285, 28);
    return true;
}

std::vector<void*> Ours()
{
    std::vector<void*> list = {g_ui.title, g_ui.back};
    for (Row& row : g_ui.rows)
        for (void* w : {row.header, row.label, row.left, row.value, row.right})
            if (w)
                list.push_back(w);
    for (void* b : g_ui.buttons)
        list.push_back(b);
    return list;
}

void ShowOurs(bool visible)
{
    for (void* w : Ours())
        Show(w, visible);
}

void Build(void* system)
{
    g_ui = Ui();
    g_ui.options = g.findPage(system, kPageOptions);
    g_ui.settings = g.findPage(system, kPageSettings);
    if (!g_ui.options || !g_ui.settings) {
        Log("native: options pages not found");
        g_failed = true;
        return;
    }
    g_dx = (Geo(g_ui.options).size.x - 1365) / 2;
    // The page first: without it there is no point in a tab.
    if (!BuildPage() || !BuildTab()) {
        ShowOurs(false);
        Log("native: no Mods tab");
        g_failed = true;
        return;
    }
    ShowOurs(false);
    g_built = true;
    Geometry screen = Geo(g_ui.options);
    Log("native: Mods tab added (%zu own controls on the settings page, screen %dx%d, dx %d)", g_ui.vanilla.size(),
        screen.size.x, screen.size.y, g_dx);
}

// ---------------------------------------------------------------------------------------------
// Content

const HubPage* Mod()
{
    return g_mod >= 0 && g_mod < static_cast<int>(g_mods.size()) ? g_mods[g_mod] : nullptr;
}

bool ParentOff(const HubPage* page, const HubItem& item)
{
    if (item.parent < 0 || item.parent >= page->itemCount)
        return false;
    return MenuValues(page)[item.parent] == 0.0f;
}

const wchar_t* ValueText(const HubPage* page, int index, int row)
{
    static wchar_t buf[128];
    const HubItem& item = page->items[index];
    float v = MenuValues(page)[index];
    char text[96] = "";
    switch (item.type) {
    case HubCheckbox:
        return Wide(v != 0.0f ? Text("On", "Вкл") : Text("Off", "Выкл"));
    case HubSlider: {
        float scale = item.displayScale != 0.0f ? item.displayScale : 1.0f;
        snprintf(text, sizeof(text), item.format ? item.format : "%.1f", v * scale);
        break;
    }
    case HubKey:
        if (row == g_waitRow)
            return Wide(Text("Press a key...", "Нажмите клавишу..."));
        KeyName(static_cast<int>(v), text, sizeof(text));
        break;
    }
    wcscpy_s(buf, Wide(text));
    return buf;
}

const wchar_t* PagerText()
{
    static wchar_t buf[64];
    char text[64];
    const HubPage* mod = Mod();
    int count = mod ? mod->itemCount : 0, parts = (count + kRows - 1) / kRows;
    int last = g_first + kRows < count ? g_first + kRows : count;
    int part = last >= count ? parts : g_first / kRows + 1; // scrolled to the end = the last page
    snprintf(text, sizeof(text), "%s %d / %d", Text("Page", "Страница"), part, parts);
    wcscpy_s(buf, Wide(text));
    return buf;
}

void ShowRow(Row& row, bool header, bool label, bool spin, bool disabled)
{
    Show(row.header, header);
    Show(row.label, label);
    for (void* w : {row.left, row.value, row.right}) {
        Show(w, spin);
        Disable(w, disabled);
    }
}

// Texts, visibility and disabled states of the current view.
void Fill()
{
    const HubPage* mod = Mod();
    char title[128];
    if (mod)
        snprintf(title, sizeof(title), "%s · %s", Text("Mods", "Моды"), Text(mod->titleEn, mod->titleRu));
    else
        snprintf(title, sizeof(title), "%s", Text("Mods", "Моды"));
    SetText(g_ui.title, Wide(title));
    Show(g_ui.title, true);

    // The list
    for (int i = 0; i < kRows; ++i) {
        bool shown = !mod && i < static_cast<int>(g_mods.size());
        if (shown)
            SetText(g_ui.buttons[i], Wide(Text(g_mods[i]->titleEn, g_mods[i]->titleRu)));
        // A hidden button is also moved off the screen: a pressed one keeps drawing itself
        Place(g_ui.buttons[i], shown ? (1365 - kButtonWidth) / 2 : -2000, kButtonTop + i * kButtonStep,
              kButtonWidth, kButtonHeight);
        Show(g_ui.buttons[i], shown);
    }

    // A mod
    for (int r = 0; r < kRows; ++r) {
        Row& row = g_ui.rows[r];
        bool header = false, label = false, spin = false, disabled = false;
        g_rowItem[r] = -1;
        int index = g_first + r;
        if (mod && index < mod->itemCount) {
            const HubItem& item = mod->items[index];
            g_rowItem[r] = index;
            const char* text = Text(item.labelEn, item.labelRu);
            if (item.type == HubHeader) {
                SetText(row.header, Wide(text));
                header = true;
            } else {
                SetText(row.label, Wide(text));
                label = true;
                // A note spans the row: no value
                Place(row.label, 353, kRowTop + r * kRowStep, item.type == HubNote ? 600 : 307, 24);
                if (item.type != HubNote) {
                    SetText(row.value, ValueText(mod, index, r));
                    spin = true;
                    disabled = ParentOff(mod, item);
                }
            }
        }
        ShowRow(row, header, label, spin, disabled);
    }
    bool pager = false; // user's choice: long mods scroll with the wheel, no "Page 1 / 3" spin
    if (pager)
        SetText(g_ui.rows[kPagerRow].value, PagerText());
    ShowRow(g_ui.rows[kPagerRow], false, false, pager, false);
    if (mod)
        SetText(g_ui.back, Wide(Text("Back", "Назад")));
    Place(g_ui.back, mod ? kBackLeft : -2000, kBottom + 3, kBackWidth, 28);
    Show(g_ui.back, mod != nullptr);
}

void StopWaiting()
{
    if (g_waitRow < 0)
        return;
    g_waitRow = -1;
    MenuSetWaitingKey(false);
    if (g_captured && !g_releaseKey) {
        g_captured = false;
        InputCaptureSet(FindGameWindow(), false, false);
    }
}

void OpenMod(int index)
{
    StopWaiting();
    g_mod = index;
    g_first = 0;
    g_switched = GetTickCount64();
    const HubPage* mod = Mod();
    Fill();
    Log("native: %s", mod ? mod->id : "list");
}

void Enter()
{
    StopWaiting();
    RegistryLock();
    g_mods = Pages();
    RegistryUnlock();
    // The menu's own page (F10 key, icon size) stays in the F10 window only; mods have their own icon switches.
    g_mods.erase(std::remove_if(g_mods.begin(), g_mods.end(),
                                [](const HubPage* p) { return strcmp(p->id, "menu") == 0; }),
                 g_mods.end());
    if (g_mods.size() > kRows)
        g_mods.resize(kRows);
    for (void* w : g_ui.vanilla)
        Show(w, false);
    g_active = true;
    OpenMod(-1);
    // Clones are created under their siblings (the parchment panel would cover them)
    for (void* w : Ours())
        BringToFront(w);
}

void Leave()
{
    StopWaiting();
    ShowOurs(false);
    for (void* w : g_ui.vanilla)
        Show(w, true);
    g_active = false;
    g_pending = -2;
    // Off the screen too: a pressed button keeps drawing itself
    for (int i = 0; i < kRows; ++i)
        Place(g_ui.buttons[i], -2000, kButtonTop + i * kButtonStep, kButtonWidth, kButtonHeight);
    Place(g_ui.back, -2000, kBottom + 3, kBackWidth, 28);
    Log("native: mods page left");
}

// Moves the rows of a long mod by delta items (clamped).
void Scroll(int delta)
{
    const HubPage* mod = Mod();
    if (!mod || mod->itemCount <= kRows)
        return;
    int first = g_first + delta, max = mod->itemCount - kRows;
    first = first < 0 ? 0 : first > max ? max : first;
    if (first == g_first)
        return;
    StopWaiting();
    g_first = first;
    Fill();
}

// A click on an arrow of row r. action: the game's gESpinButtonAction (logged until we know which is which).
void Change(int r, int action)
{
    int dir = action == 0 ? -1 : 1; // gESpinButtonAction: 0 = left arrow, 2 = right arrow (3 = text query)
    const HubPage* mod = Mod();
    if (!mod)
        return;
    if (r == kPagerRow) {
        Scroll(dir * kRows);
        return;
    }
    int index = g_rowItem[r];
    if (index < 0)
        return;
    const HubItem& item = mod->items[index];
    if (ParentOff(mod, item))
        return;
    float v = MenuValues(mod)[index];
    switch (item.type) {
    case HubCheckbox:
        v = v != 0.0f ? 0.0f : 1.0f;
        break;
    case HubSlider: {
        float scale = item.displayScale != 0.0f ? item.displayScale : 1.0f;
        float step = item.step > 0.0f ? item.step : (item.max - item.min) / 20.0f;
        float shown = v * scale + dir * step;
        shown = shown < item.min ? item.min : shown > item.max ? item.max : shown;
        v = shown / scale;
        break;
    }
    case HubKey:
        for (int vk = 0; vk < 256; ++vk)
            g_keyDown[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        g_waitRow = r;
        MenuSetWaitingKey(true);
        Fill();
        return;
    default:
        return;
    }
    MenuValues(mod)[index] = v;
    MenuWriteValue(*mod, index, v);
    Fill(); // children of a checkbox change their disabled state
}

void PollKey()
{
    const HubPage* mod = Mod();
    if (g_waitRow < 0 || !mod)
        return;
    // Only after the click on the spin is released: the game must see that button go up
    if (!g_captured && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        g_captured = true;
        InputCaptureSet(FindGameWindow(), true);
    }
    int found = 0;
    for (int vk = 1; vk < 256; ++vk) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool pressed = down && !g_keyDown[vk];
        g_keyDown[vk] = down;
        // Not bindable: left mouse (clicks the menu), generic Shift/Ctrl/Alt, Windows keys.
        bool skip = vk == VK_LBUTTON || (vk >= VK_SHIFT && vk <= VK_MENU) || vk == VK_LWIN || vk == VK_RWIN ||
                    vk == VK_APPS;
        if (pressed && !skip && !found)
            found = vk;
    }
    if (!found)
        return;
    int r = g_waitRow;
    g_releaseKey = found;
    StopWaiting();
    if (found != VK_ESCAPE && g_rowItem[r] >= 0) {
        MenuValues(mod)[g_rowItem[r]] = static_cast<float>(found);
        MenuWriteValue(*mod, g_rowItem[r], static_cast<float>(found));
    }
    Fill();
}

int64_t OnSpin(int r, int64_t args)
{
    if (!g_active || r < 0 || r > kRows || !args)
        return 0;
    int action = *reinterpret_cast<int*>(args);
    static int logged;
    if (logged < 12) {
        ++logged;
        Log("native: OnSpin row %d action %d", r, action);
    }
    if (action != 3)
        Change(r, action);
    // The spin button shows the text we put into args (and copies it to its slave).
    const wchar_t* text = L"";
    if (r == kPagerRow)
        text = PagerText();
    else if (Mod() && g_rowItem[r] >= 0)
        text = ValueText(Mod(), g_rowItem[r], r);
    __try {
        g.unicodeAssign(reinterpret_cast<char*>(args) + 8, text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

// After a rebind: give the game its input back once the bound key is released.
void ReleaseInputAfterRebind()
{
    if (!g_releaseKey || (GetAsyncKeyState(g_releaseKey) & 0x8000))
        return;
    g_releaseKey = 0;
    if (g_captured) {
        g_captured = false;
        InputCaptureSet(FindGameWindow(), false, false);
    }
}

bool OpenSettingsPage()
{
    __try {
        return g.guiOpen(g.gui2, kPageSettings);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int64_t CallVanilla(const char* script, void* self, void* other, int64_t args)
{
    __try {
        return g.proxyCall(Proxy(script), self, other, args);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("native: %s exception", script);
        return 0;
    }
}

// What Script_Game OnClick_NewGame (+0x1547e0) does after "Start a new game?" was answered yes (keyboard/mouse
// path: GUI2::ModalNewGame, then SetPageMode(7, true) and World::NewGame), without the question.
int64_t StartNewGame()
{
    __try {
        const int mode = 7;
        g.setPageMode(g.gui2, &mode, true);
        g.newGame(g.world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("native: new game exception");
    }
    return 0;
}

} // namespace

void NativeTick()
{
    if (g_failed || !ApiInit())
        return;
    void* system = g.getSystem();
    if (!system)
        return;
    if (g_built) {
        // The game may rebuild its pages (language change?): then our clones are gone - start over.
        if (g.findPage(system, kPageOptions) != g_ui.options || g.findPage(system, kPageSettings) != g_ui.settings ||
            !HasChild(g_ui.options, g_ui.tab) || !HasChild(g_ui.settings, g_ui.back)) {
            Log("native: options pages were rebuilt, adding the tab again");
            g_built = g_active = false;
            StopWaiting();
        }
    }
    if (!g_built) {
        if (!g.isVisible(system, kPageMain) && !g.isVisible(system, kPageStart) &&
            !g.isVisible(system, kPageOptions))
            return; // the GUI isn't loaded yet
        Build(system);
        if (!g_built)
            return;
    }
    // The layout assumes the 1365x768 virtual screen (16:9): log other sizes (resolution / aspect change)
    static Point screenSize;
    Point size = Geo(g_ui.options).size;
    if (size.x != screenSize.x || size.y != screenSize.y) {
        if (screenSize.x)
            Log("native: screen %dx%d -> %dx%d (dx %d; vanilla label x %d, our tab x %d, our title x %d)",
                screenSize.x, screenSize.y, size.x, size.y, g_dx,
                g_ui.vanilla.empty() ? 0 : Geo(g_ui.vanilla.front()).pos.x, Geo(g_ui.tab).pos.x,
                Geo(g_ui.title).pos.x);
        screenSize = size;
    }
    // A text set while the options were hidden isn't drawn until the tab repaints: set it again on every show.
    static bool optionsShown;
    bool options = g.isVisible(system, kPageOptions);
    if (options && !optionsShown) {
        SetText(g_ui.tab, Wide(Text("Mods", "Моды")));
        BringToFront(g_ui.tab); // the clone lies under the row's background strip until clicked
    }
    optionsShown = options;
    if (!g_test.failed && TestArm()) {
        if (g_test.button && (g.findPage(system, kPageGame) != g_test.page || !HasChild(g_test.page, g_test.button))) {
            Log("native: game menu was rebuilt, adding the Test button again");
            g_test.button = nullptr;
        }
        if (!g_test.button)
            BuildTestButton(system);
        static bool gameShown;
        bool game = g.isVisible(system, kPageGame);
        if (game && !gameShown && g_test.button) {
            SetText(g_test.button, Wide(Text("Test", "Тест")));
            BringToFront(g_test.button);
        }
        gameShown = game;
    }
    ReleaseInputAfterRebind();
    if (g_active) {
        if (!g.isVisible(system, kPageSettings)) {
            Leave();
        } else if (g_pending != -2) {
            int view = g_pending;
            g_pending = -2;
            OpenMod(view);
        } else {
            PollKey();
        }
    }
}

bool NativeOnWheel(int delta)
{
    if (!g_active || !Mod() || Mod()->itemCount <= kRows)
        return false;
    Scroll(delta > 0 ? -2 : 2); // away from the user = up
    return true;
}

bool NativeOnEscape()
{
    if (!g_active)
        return false;
    if (g_waitRow >= 0) {
        StopWaiting();
        Fill();
        return true;
    }
    if (g_mod >= 0) {
        OpenMod(-1);
        return true;
    }
    return false; // the list: Esc does what the game does
}

// Called by bin\scripts\Script_RisenMods.dll for every RM_* script (game thread).
extern "C" __declspec(dllexport) int64_t RisenScriptEvent(const char* name, void* unit, void* self, void* other,
                                                          int64_t args)
{
    if (strcmp(name, "RM_Test") == 0)
        return 42;
    bool test = strcmp(name, "RM_OnClick_Test") == 0;
    if (test || strcmp(name, "RM_OnClick_NewGame") == 0) {
        if (!ApiInit())
            return 0;
        TestArmFn arm = TestArm();
        bool armed = arm && arm(test);
        Log("native: %s clicked%s", test ? "Test" : "New Game", test && !armed ? ", test start NOT available" : "");
        if (test && !armed)
            return 0;
        if (test)
            return StartNewGame();
        return CallVanilla("OnClick_NewGame", self, other, args);
    }
    if (!g_built || !ApiInit())
        return 0;
    if (strcmp(name, "RM_OnClick_Mods") == 0) {
        if (!g_active) {
            bool opened = OpenSettingsPage();
            Log("native: Mods tab clicked, settings page %s", opened ? "opened" : "NOT opened");
            Enter();
        }
        return 0;
    }
    if (strcmp(name, "RM_OnClick_Settings") == 0) {
        if (g_active)
            Leave();
        return CallVanilla("OnClick_Menu_Settings", self, other, args);
    }
    if (!g_active)
        return 0;
    if (strncmp(name, "RM_OnClick_Item_", 16) == 0) {
        int i = atoi(name + 16);
        // Not right here: a button hidden inside its own click handler draws itself again (pressed)
        if (g_mod < 0 && i >= 0 && i < static_cast<int>(g_mods.size()))
            g_pending = i;
        return 0;
    }
    if (strcmp(name, "RM_OnClick_Back") == 0) {
        // The buttons are radio buttons of one group: Back seems to get a click when another one is pressed
        if (GetTickCount64() - g_switched < 500) {
            Log("native: Back right after a switch ignored");
            return 0;
        }
        g_pending = -1;
        return 0;
    }
    if (strncmp(name, "RM_OnSpin_", 10) == 0)
        return OnSpin(atoi(name + 10), args);
    (void)unit;
    return 0;
}
