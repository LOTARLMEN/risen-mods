// Script_RisenMods.dll (bin\scripts): registers our own script names with the game, so the game's GUI can call us
// like its own handlers (OnClick_*, OnSpin_*, OnPaint_*). Game.dll loads every bin\scripts\*script_*.dll and calls
// its ScriptInit (gCScriptAdmin::LoadScriptDLL, Rev. 2045): the returned descriptor is 4 arrays {T* data; int count;
// int cap;} - AI states, AI functions, AI callbacks, scripts - of {bCString name; bCString second; fn}. A name the
// game already has is skipped (first one wins), so we only add names, prefixed RM_.
// This DLL stays dumb: every script is forwarded to RisenScriptEvent of menu.dll (or ui_lab.dll while developing).

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "common.h"

// Signature of every game script: __int64 f(gCScriptProcessingUnit*, void* self, void* other, __int64 args).
typedef int64_t (*ScriptFn)(void* unit, void* self, void* other, int64_t args);
// Exported by menu.dll / ui_lab.dll.
typedef int64_t (*RisenScriptEventFn)(const char* name, void* unit, void* self, void* other, int64_t args);

static int64_t Forward(const char* name, void* unit, void* self, void* other, int64_t args)
{
    static const char* const kModules[] = {"menu.dll", "ui_lab.dll"};
    for (const char* dll : kModules)
        if (HMODULE m = GetModuleHandleA(dll))
            if (auto fn = reinterpret_cast<RisenScriptEventFn>(GetProcAddress(m, "RisenScriptEvent")))
                return fn(name, unit, self, other, args);
    Log("%s: no handler (menu.dll missing?)", name);
    return 0;
}

#define RM_SCRIPT(fn) \
    static int64_t fn(void* unit, void* self, void* other, int64_t args) { return Forward(#fn, unit, self, other, args); }

// A spin button's script gets no hint which control called it, so every row of our page has its own name.
RM_SCRIPT(RM_Test)
RM_SCRIPT(RM_OnClick_Mods)
RM_SCRIPT(RM_OnClick_Settings)
RM_SCRIPT(RM_OnSpin_00)
RM_SCRIPT(RM_OnSpin_01)
RM_SCRIPT(RM_OnSpin_02)
RM_SCRIPT(RM_OnSpin_03)
RM_SCRIPT(RM_OnSpin_04)
RM_SCRIPT(RM_OnSpin_05)
RM_SCRIPT(RM_OnSpin_06)
RM_SCRIPT(RM_OnSpin_07)
RM_SCRIPT(RM_OnSpin_08)
RM_SCRIPT(RM_OnSpin_09)
RM_SCRIPT(RM_OnSpin_10)
RM_SCRIPT(RM_OnSpin_11)
RM_SCRIPT(RM_OnSpin_12)
RM_SCRIPT(RM_OnSpin_13)
RM_SCRIPT(RM_OnSpin_14)
RM_SCRIPT(RM_OnSpin_15)
RM_SCRIPT(RM_OnClick_Back)
RM_SCRIPT(RM_OnClick_Item_00)
RM_SCRIPT(RM_OnClick_Item_01)
RM_SCRIPT(RM_OnClick_Item_02)
RM_SCRIPT(RM_OnClick_Item_03)
RM_SCRIPT(RM_OnClick_Item_04)
RM_SCRIPT(RM_OnClick_Item_05)
RM_SCRIPT(RM_OnClick_Item_06)
RM_SCRIPT(RM_OnClick_Item_07)
RM_SCRIPT(RM_OnClick_Item_08)
RM_SCRIPT(RM_OnClick_Item_09)
RM_SCRIPT(RM_OnClick_Item_10)
RM_SCRIPT(RM_OnClick_Item_11)
RM_SCRIPT(RM_OnClick_Item_12)
RM_SCRIPT(RM_OnClick_Item_13)

struct Script {
    const char* name;
    ScriptFn fn;
};

#define RM_ENTRY(fn) {#fn, fn}
static const Script kScripts[] = {
    RM_ENTRY(RM_Test),      RM_ENTRY(RM_OnClick_Mods), RM_ENTRY(RM_OnClick_Settings), RM_ENTRY(RM_OnSpin_00),
    RM_ENTRY(RM_OnSpin_01), RM_ENTRY(RM_OnSpin_02),    RM_ENTRY(RM_OnSpin_03),        RM_ENTRY(RM_OnSpin_04),
    RM_ENTRY(RM_OnSpin_05), RM_ENTRY(RM_OnSpin_06),    RM_ENTRY(RM_OnSpin_07),        RM_ENTRY(RM_OnSpin_08),
    RM_ENTRY(RM_OnSpin_09), RM_ENTRY(RM_OnSpin_10),    RM_ENTRY(RM_OnSpin_11),        RM_ENTRY(RM_OnSpin_12),
    RM_ENTRY(RM_OnSpin_13), RM_ENTRY(RM_OnSpin_14),    RM_ENTRY(RM_OnSpin_15),        RM_ENTRY(RM_OnClick_Back),
    RM_ENTRY(RM_OnClick_Item_00), RM_ENTRY(RM_OnClick_Item_01), RM_ENTRY(RM_OnClick_Item_02),
    RM_ENTRY(RM_OnClick_Item_03), RM_ENTRY(RM_OnClick_Item_04), RM_ENTRY(RM_OnClick_Item_05),
    RM_ENTRY(RM_OnClick_Item_06), RM_ENTRY(RM_OnClick_Item_07), RM_ENTRY(RM_OnClick_Item_08),
    RM_ENTRY(RM_OnClick_Item_09), RM_ENTRY(RM_OnClick_Item_10), RM_ENTRY(RM_OnClick_Item_11),
    RM_ENTRY(RM_OnClick_Item_12), RM_ENTRY(RM_OnClick_Item_13),
};
constexpr int kScriptCount = sizeof(kScripts) / sizeof(kScripts[0]);

struct Entry {
    void* name;   // bCString (one pointer)
    void* second; // bCString
    ScriptFn fn;
};
static_assert(sizeof(Entry) == 0x18, "descriptor element");

template <typename T>
struct Array {
    T* data;
    int count;
    int cap;
};

struct Descriptor {
    Array<Entry> aiStates, aiFunctions, aiCallbacks, scripts;
};

static Entry g_entries[kScriptCount];
static Descriptor g_descriptor;

extern "C" __declspec(dllexport) const Descriptor* ScriptInit()
{
    if (g_descriptor.scripts.count)
        return &g_descriptor;
    HMODULE base = GetModuleHandleA("SharedBase.dll");
    auto stringNew = base ? reinterpret_cast<void* (*)(void*, const char*)>(
                                GetProcAddress(base, "??0bCString@@QEAA@PEBD@Z"))
                          : nullptr;
    if (!stringNew) {
        Log("ScriptInit: SharedBase.dll!bCString(char const*) not found, no scripts");
        return nullptr; // the game shows a message box and goes on without us
    }
    // The strings live as long as the game (the function map keeps copies anyway).
    for (int i = 0; i < kScriptCount; ++i) {
        stringNew(&g_entries[i].name, kScripts[i].name);
        stringNew(&g_entries[i].second, kScripts[i].name);
        g_entries[i].fn = kScripts[i].fn;
    }
    g_descriptor.scripts = {g_entries, kScriptCount, kScriptCount};
    Log("ScriptInit: %d scripts", kScriptCount);
    return &g_descriptor;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInitIn(instance, "script_risenmods", "..\\mods");
        Log("Risen Mods scripts 0.1 (dev)");
    }
    return TRUE;
}
