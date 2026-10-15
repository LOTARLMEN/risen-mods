// UI lab: developer-only mod for TODO 5.12 (native "Mods" page). Not packaged (no README).
// Every game page (gEHudPage 1..29) is dumped to the log once, the first time it is visible: the window tree with
// RTTI class, name, ID, position, size and text. Then the page is saved with eCGuiWindow2::SaveWindowAsClass
// (bin\ui_<page>.xml, if the engine writes there) to see the layout format. [Debug] DumpKey dumps every loaded page
// again. All game calls run on the game thread (posted window messages) and behind SEH: this is recon code.
// Settings: mods\ui_lab.ini.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.h"

static const char kDefaultIni[] =
    "; UI lab (developer only).\r\n"
    "[Debug]\r\n"
    "; Dump every loaded game page to the log again\r\n"
    "DumpKey=F11\r\n"
    "; Save visible pages with SaveWindowAsClass on first sight\r\n"
    "SaveXml=0\r\n";

constexpr int kPageCount = 30;
constexpr UINT WM_UILAB_POLL = WM_APP + 0x5801;
constexpr UINT WM_UILAB_DUMP = WM_APP + 0x5802;

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
    bool (*saveAsClass)(void* wnd, const char* path);
    const char* (*stringText)(const void* str);        // bCString
    const wchar_t* (*unicodeText)(const void* str);    // bCUnicodeString
    void (*unicodeFree)(void* str);
    void* (*unicodeNew)(void* self, const wchar_t* text);
    void* (*stringNew)(void* self, const char* text);
    void* (*clone)(void* wnd, void* parent);
    void (*setText)(void* wnd, const void* unicode);
    void (*setPosition)(void* wnd, const Point* pos);
    void (*setSize)(void* wnd, const Point* size);
    void (*setOnClick)(void* button, const void* proxy);
    const void* (*scriptName)(const void* proxy); // bCString
    void* (*proxyNew)(void* self, const void* name);
    int64_t (*proxyCall)(const void* self, void* selfEntity, void* otherEntity, int64_t param);
    bool (*guiOpen)(void* gui, int page);
    void* gui2;
};

static Api g_api;
static bool g_missing;
static WNDPROC g_origProc;
static bool g_dumped[kPageCount];
static bool g_saveXml;

template <typename T>
static void Resolve(HMODULE mod, const char* modName, T& fn, const char* name)
{
    fn = mod ? reinterpret_cast<T>(GetProcAddress(mod, name)) : nullptr;
    if (!fn) {
        Log("api: %s!%s not found", modName, name);
        g_missing = true;
    }
}

static bool ApiInit()
{
    static int state;
    if (state)
        return state > 0;
    HMODULE game = GetModuleHandleA("Game.dll"), engine = GetModuleHandleA("Engine.dll");
    HMODULE base = GetModuleHandleA("SharedBase.dll");
    if (!game || !engine || !base || !GetModuleHandleA("Script.dll"))
        return false;
    Api& a = g_api;
    Resolve(game, "Game", a.getSystem, "?GetSystem@gCHudModule2@@SAPEAVgCHudSystem2@@XZ");
    Resolve(game, "Game", a.findPage, "?FindPage@gCHudSystem2@@QEBAPEAVgCHudPage2@@W4gEHudPage@@@Z");
    Resolve(game, "Game", a.isVisible, "?IsVisible@gCHudSystem2@@QEBA_NW4gEHudPage@@@Z");
    Resolve(engine, "Engine", a.firstChild, "?GetFirstChild@eCGuiWindow2@@QEBAPEAV1@XZ");
    Resolve(engine, "Engine", a.nextSibling, "?GetNextSibling@eCGuiWindow2@@QEBAPEAV1@XZ");
    Resolve(engine, "Engine", a.getId, "?GetID@eCGuiWindow2@@QEBAGXZ");
    Resolve(engine, "Engine", a.getPosition, "?GetPosition@eCGuiWindow2@@QEBA?AVbCPoint@@XZ");
    Resolve(engine, "Engine", a.getSize, "?GetSize@eCGuiWindow2@@QEBA?AVbCPoint@@XZ");
    Resolve(engine, "Engine", a.getText, "?GetText@eCGuiWindow2@@QEBA?AVbCUnicodeString@@XZ");
    Resolve(engine, "Engine", a.saveAsClass, "?SaveWindowAsClass@eCGuiWindow2@@QEAA_NPEBD@Z");
    Resolve(base, "SharedBase", a.stringText, "?GetText@bCString@@QEBAPEBDXZ");
    Resolve(base, "SharedBase", a.unicodeText, "?GetText@bCUnicodeString@@QEBAPEB_WXZ");
    Resolve(base, "SharedBase", a.unicodeFree, "??1bCUnicodeString@@QEAA@XZ");
    Resolve(base, "SharedBase", a.unicodeNew, "??0bCUnicodeString@@QEAA@PEB_W@Z");
    Resolve(base, "SharedBase", a.stringNew, "??0bCString@@QEAA@PEBD@Z");
    Resolve(engine, "Engine", a.clone, "?Clone@eCGuiWindow2@@QEAAPEAV1@PEAV1@@Z");
    Resolve(engine, "Engine", a.setText, "?SetText@eCGuiWindow2@@QEAAXAEBVbCUnicodeString@@@Z");
    Resolve(engine, "Engine", a.setPosition, "?SetPosition@eCGuiWindow2@@QEAAXAEBVbCPoint@@@Z");
    Resolve(engine, "Engine", a.setSize, "?SetSize@eCGuiWindow2@@QEAAXAEBVbCPoint@@@Z");
    Resolve(engine, "Engine", a.setOnClick, "?SetOnClick@eCScriptRadioButton@@QEAAXAEBVeCScriptProxyScript@@@Z");
    Resolve(engine, "Engine", a.scriptName, "?GetScriptName@eCScriptProxy@@QEBAAEBVbCString@@XZ");
    Resolve(engine, "Engine", a.proxyNew, "??0eCScriptProxyScript@@QEAA@AEBVbCString@@@Z");
    Resolve(engine, "Engine", a.proxyCall, "??ReCScriptProxyScript@@QEBA_JPEAVeCEntity@@0_J@Z");
    HMODULE script = GetModuleHandleA("Script.dll");
    Resolve(script, "Script", a.guiOpen, "?Open@GUI2@@QEAA_NW4gEHudPage@@@Z");
    Resolve(script, "Script", a.gui2, "gui2");
    state = g_missing ? -1 : 1;
    Log("api: %s", state > 0 ? "ready" : "INCOMPLETE");
    return state > 0;
}

// MSVC x64 RTTI: vtable[-1] = CompleteObjectLocator {sig, offset, cdOffset, typeDesc rva, hierarchy rva, self rva}.
static const char* RttiName(void* obj)
{
    __try {
        auto* vtbl = *reinterpret_cast<uintptr_t**>(obj);
        auto* col = reinterpret_cast<const uint32_t*>(vtbl[-1]);
        if (col[0] != 1)
            return "?";
        uintptr_t image = reinterpret_cast<uintptr_t>(col) - col[5];
        const char* name = reinterpret_cast<const char*>(image + col[3] + 0x10);
        return name; // ".?AVeCGuiStatic2@@"
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "<bad>";
    }
}

static void Utf8(const wchar_t* w, char* out, int size)
{
    out[0] = 0;
    if (w && *w)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, nullptr, nullptr);
    out[size - 1] = 0;
    for (char* p = out; *p; ++p)
        if (*p == '\r' || *p == '\n')
            *p = ' ';
}

// Window name: GetName returns the bCString at +0x48 (the export is a folded "lea rax,[rcx+48h]").
static const char* WindowName(void* wnd)
{
    __try {
        const char* s = g_api.stringText(reinterpret_cast<char*>(wnd) + 0x48);
        return s ? s : "";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "<bad>";
    }
}

static void WindowText(void* wnd, char* out, int size)
{
    alignas(16) char str[32] = {};
    out[0] = 0;
    __try {
        g_api.getText(wnd, str);
        Utf8(g_api.unicodeText(str), out, size);
        g_api.unicodeFree(str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        strcpy_s(out, size, "<bad>");
    }
}

static bool WindowGeometry(void* wnd, Point& pos, Point& size, uint16_t& id)
{
    __try {
        g_api.getPosition(wnd, &pos);
        g_api.getSize(wnd, &size);
        id = g_api.getId(wnd);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void* Child(void* wnd, bool first)
{
    __try {
        return first ? g_api.firstChild(wnd) : g_api.nextSibling(wnd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("  <exception walking the tree>");
        return nullptr;
    }
}

static void DumpTree(void* wnd, int depth, int& count)
{
    for (; wnd && count < 3000; wnd = Child(wnd, false)) {
        ++count;
        Point pos = {}, size = {};
        uint16_t id = 0;
        char text[256];
        bool ok = WindowGeometry(wnd, pos, size, id);
        WindowText(wnd, text, sizeof(text));
        Log("%*s%s '%s' id=%u pos=%d,%d size=%dx%d%s%s%s", depth * 2, "", RttiName(wnd), WindowName(wnd), id, pos.x,
            pos.y, size.x, size.y, ok ? "" : " <geometry failed>", text[0] ? " text=" : "", text);
        if (depth < 16)
            DumpTree(Child(wnd, true), depth + 1, count);
    }
}

static void* System()
{
    __try {
        return g_api.getSystem();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static void* FindPage(void* system, int page)
{
    __try {
        return g_api.findPage(system, page);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool IsVisible(void* system, int page)
{
    __try {
        return g_api.isVisible(system, page);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SaveXml(void* wnd, int page)
{
    char path[64];
    sprintf_s(path, "ui_page_%02d.xml", page);
    __try {
        bool ok = g_api.saveAsClass(wnd, path);
        Log("page %d: SaveWindowAsClass(\"%s\") = %d", page, path, ok);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("page %d: SaveWindowAsClass exception", page);
    }
}

static void DumpPage(void* system, int page)
{
    void* wnd = FindPage(system, page);
    if (!wnd)
        return;
    int count = 0;
    Log("==== page %d %p visible=%d %s '%s'", page, wnd, IsVisible(system, page), RttiName(wnd), WindowName(wnd));
    DumpTree(Child(wnd, true), 1, count);
    Log("==== page %d: %d windows", page, count);
}

// ---------------------------------------------------------------------------------------------
// M2 / M3: our script names (bin\scripts\Script_RisenMods.dll) and a cloned "Моды" tab in DLG_MENU_OPTIONS.

constexpr int kPageOptions = 18, kPageSettings = 22;

// eCScriptProxyScript built from a name; kept for the whole game (the button keeps its own copy anyway).
static void* Proxy(const char* name)
{
    struct Slot {
        const char* name;
        alignas(16) char data[64];
    };
    static Slot slots[4];
    for (Slot& s : slots) {
        if (s.name == name)
            return s.data;
        if (!s.name) {
            alignas(8) char str[16] = {};
            g_api.stringNew(str, name);
            g_api.proxyNew(s.data, str);
            s.name = name; // the bCString is leaked on purpose: tiny, once
            return s.data;
        }
    }
    return nullptr;
}

static const char* OnClickName(void* button)
{
    __try {
        const void* name = g_api.scriptName(reinterpret_cast<char*>(button) + 0x150); // GetOnClick = lea [rcx+150h]
        const char* s = name ? g_api.stringText(name) : nullptr;
        return s ? s : "";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "<bad>";
    }
}

static void CallTestScript()
{
    __try {
        int64_t r = g_api.proxyCall(Proxy("RM_Test"), nullptr, nullptr, 7);
        Log("M2: ScriptCall(RM_Test) = %lld (42 = our handler ran)", r);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("M2: ScriptCall(RM_Test) exception");
    }
}

static void Place(void* wnd, int x, int y, int w, int h)
{
    Point pos = {x, y}, size = {w, h};
    g_api.setSize(wnd, &size);
    g_api.setPosition(wnd, &pos);
}

static void SetText(void* wnd, const wchar_t* text)
{
    alignas(16) char str[32] = {};
    g_api.unicodeNew(str, text);
    g_api.setText(wnd, str);
    g_api.unicodeFree(str);
}

static bool AddModsTabGuarded(void* page)
{
    void* tabs[8];
    int count = 0;
    for (void* w = Child(page, true); w; w = Child(w, false)) {
        Point pos = {}, size = {};
        uint16_t id;
        if (!WindowGeometry(w, pos, size, id) || pos.y != 710 || size.y != 23 ||
            strcmp(RttiName(w), ".?AVeCScriptRadioButton@@") != 0 || count == 8)
            continue;
        char text[64];
        WindowText(w, text, sizeof(text));
        Log("M3: tab x=%d '%s' OnClick=%s", pos.x, text, OnClickName(w));
        tabs[count++] = w;
    }
    if (count != 5) {
        Log("M3: expected 5 tabs at y=710, found %d - skip", count);
        return false;
    }
    // Left to right
    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j) {
            Point a, b, s;
            uint16_t id;
            WindowGeometry(tabs[i], a, s, id);
            WindowGeometry(tabs[j], b, s, id);
            if (b.x < a.x) {
                void* t = tabs[i];
                tabs[i] = tabs[j];
                tabs[j] = t;
            }
        }
    __try {
        void* mods = g_api.clone(tabs[count - 1], page);
        Log("M3: clone = %p %s", mods, mods ? RttiName(mods) : "");
        if (!mods)
            return false;
        tabs[count++] = mods;
        SetText(mods, L"Моды");
        g_api.setOnClick(mods, Proxy("RM_OnClick_Mods"));
        // Vanilla tabs are 164 px with 8 px gaps on an 896x32 background at (234,706), 24 px margins: 143 px tabs
        // cut "Производительность". Keep the tab size and widen the background instead: 6 tabs = 1024 px, the
        // background 1072 px, both centred on the 1365 px wide screen.
        constexpr int kWidth = 164, kGap = 8, kRow = 6 * kWidth + 5 * kGap, kBack = kRow + 48;
        constexpr int kBackLeft = (1365 - kBack) / 2, kLeft = kBackLeft + 24;
        for (void* w = Child(page, true); w; w = Child(w, false)) {
            Point pos = {}, size = {};
            uint16_t id;
            if (WindowGeometry(w, pos, size, id) && pos.x == 234 && pos.y == 706 && size.x == 896 && size.y == 32) {
                Place(w, kBackLeft, 706, kBack, 32);
                Log("M3: tab background widened to %d", kBack);
            }
        }
        for (int i = 0; i < count; ++i)
            Place(tabs[i], kLeft + i * (kWidth + kGap), 710, kWidth, 23);
        char text[64];
        WindowText(mods, text, sizeof(text));
        Log("M3: 'Моды' tab added: text='%s' OnClick=%s", text, OnClickName(mods));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("M3: exception while adding the tab");
        return false;
    }
}

// gCSpinButton fields (Game.dll accessors): +0xE0 SpinType, +0xF0 SpinBuddy (u16), +0xF2 slave ID, +0xF8 OnSpin.
static void DumpSpinsGuarded(void* page)
{
    __try {
        for (void* w = Child(page, true); w; w = Child(w, false)) {
            if (strcmp(RttiName(w), ".?AVgCSpinButton@@") != 0)
                continue;
            auto* b = reinterpret_cast<uint8_t*>(w);
            Point pos = {}, size = {};
            uint16_t id = 0;
            WindowGeometry(w, pos, size, id);
            const void* name = g_api.scriptName(b + 0xF8);
            const char* script = name ? g_api.stringText(name) : nullptr;
            Log("spin id=%u pos=%d,%d size=%dx%d type=%d buddy=%u slave=%u OnSpin=%s", id, pos.x, pos.y, size.x,
                size.y, *reinterpret_cast<int*>(b + 0xE0), *reinterpret_cast<uint16_t*>(b + 0xF0),
                *reinterpret_cast<uint16_t*>(b + 0xF2), script ? script : "");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("spin dump exception");
    }
}

static void AddModsTab(void* system)
{
    static bool done;
    if (done)
        return;
    done = true;
    if (void* page = FindPage(system, kPageSettings))
        DumpSpinsGuarded(page);
    CallTestScript();
    if (void* page = FindPage(system, kPageOptions))
        AddModsTabGuarded(page);
}

static void OpenSettingsPage()
{
    __try {
        bool ok = g_api.guiOpen(g_api.gui2, kPageSettings);
        Log("M3: gui2.Open(%d) = %d", kPageSettings, ok);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("M3: gui2.Open exception");
    }
}

// Called by bin\scripts\Script_RisenMods.dll for every RM_* script (game thread).
extern "C" __declspec(dllexport) int64_t RisenScriptEvent(const char* name, void* unit, void* self, void* other,
                                                          int64_t args)
{
    Log("script %s unit=%p self=%p other=%p args=%llx", name, unit, self, other, args);
    if (strcmp(name, "RM_Test") == 0)
        return 42;
    if (strcmp(name, "RM_OnClick_Mods") == 0 && ApiInit())
        OpenSettingsPage();
    return 0;
}

static void Poll()
{
    if (!ApiInit())
        return;
    void* system = System();
    if (!system)
        return;
    for (int page = 1; page < kPageCount; ++page) {
        if (g_dumped[page] || !IsVisible(system, page))
            continue;
        g_dumped[page] = true;
        DumpPage(system, page);
        if (g_saveXml)
            if (void* wnd = FindPage(system, page))
                SaveXml(wnd, page);
    }
    // The "Моды" tab experiment (AddModsTab) moved to menu.dll (mods/menu/native_page.cpp); kept here for reference.
    (void)&AddModsTab;
}

static void DumpAll()
{
    if (!ApiInit())
        return;
    void* system = System();
    Log("dump all: system=%p", system);
    if (system)
        for (int page = 1; page < kPageCount; ++page)
            DumpPage(system, page);
}

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_UILAB_POLL:
        Poll();
        return 0;
    case WM_UILAB_DUMP:
        DumpAll();
        return 0;
    }
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

static DWORD WINAPI Worker(LPVOID)
{
    HWND hwnd = WaitForGameWindow();
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    int key = IniKey("Debug", "DumpKey", VK_F11);
    g_saveXml = IniInt("Debug", "SaveXml", 0) != 0;
    Log("ready: DumpKey=0x%02X SaveXml=%d", key, g_saveXml);
    bool wasDown = false;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool down = key && GetForegroundWindow() == hwnd && (GetAsyncKeyState(key) & 0x8000);
        if (down && !wasDown)
            PostMessageW(hwnd, WM_UILAB_DUMP, 0, 0);
        wasDown = down;
        if (tick % 25 == 0) // ~0.75 s: a page is dumped a moment after it appears, when its layout is done
            PostMessageW(hwnd, WM_UILAB_POLL, 0, 0);
        Sleep(30);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "ui_lab");
        Log("Risen UI lab (dev)");
        IniPath(kDefaultIni);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
