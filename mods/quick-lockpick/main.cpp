// Quick Lockpick: look at a locked chest or door, press a key - the lock opens at once, no minigame.
// The game's own checks decide whether the player can open it (script CanOpenLock: lockpick skill against the lock's
// difficulty, a lockpick or the key in the inventory, key-only locks) and speak the hero's line when not.
// With the key the lock just opens. Otherwise the lockpick may break on the way (chance by lockpick skill, F10),
// the lock opens anyway. Optionally the chest is looted right away (the game's "AutoLoot" script).
// Locks that belong to someone are left to the game's minigame: the theft reaction of witnesses is not ours to fake.
// What the game does when the minigame succeeds (Script_Game LockPickLoop / Unlock, Rev. 2045): PSLock status = 1,
// Achievement::OnHandleLockpick, hero line "OpenLock", effect eff_ani_human_interact_lockpick_end_01. A broken
// lockpick: DeleteItems(It_Lockpick, 1), effect ..._broke_01, line "LockpickBroke" ("LastLockpick" for the last one).
// Settings: mods\quick_lockpick.ini (hot-reloaded). Game calls run on the game thread (posted window messages).

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.h"
#include "hub_client.h"

static const char kDefaultIni[] =
    "; Quick Lockpick mod settings. Changes are picked up while the game is running.\r\n"
    "; Keys: F1-F24, A-Z, 0-9, NUMPAD0-9, MOUSE2-5, or a hex code like 0x47.\r\n"
    "\r\n"
    "[QuickLockpick]\r\n"
    "; Look at a locked chest or door and press the key: it opens at once, no minigame. The game decides whether\r\n"
    "; you can (lockpick skill, a lockpick or the key). Locks that belong to someone are left to the game's minigame.\r\n"
    "Enabled=1\r\n"
    "Key=MOUSE2\r\n"
    "Chests=1\r\n"
    "Doors=1\r\n"
    "; Loot the chest right after opening it\r\n"
    "LootAfter=1\r\n"
    "; Chance (percent) that the lockpick breaks, by lockpick skill level. The lock opens anyway.\r\n"
    "BreakChance1=50\r\n"
    "BreakChance2=30\r\n"
    "BreakChance3=15\r\n";

struct Config {
    bool enabled = true;
    int key = VK_RBUTTON;
    bool chests = true;
    bool doors = true;
    bool lootAfter = true;
    int breakChance[4] = {50, 50, 30, 15}; // by skill level, [0] unused (the game refuses without the skill)
};

static Config g_config;
static WNDPROC g_origProc;
constexpr UINT WM_LOCKPICK_USE = WM_APP + 0x5701;
constexpr UINT WM_LOCKPICK_RELOAD = WM_APP + 0x5702;

static int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void ConfigLoad()
{
    Config c;
    c.enabled = IniInt("QuickLockpick", "Enabled", 1) != 0;
    c.key = IniKey("QuickLockpick", "Key", VK_RBUTTON);
    c.chests = IniInt("QuickLockpick", "Chests", 1) != 0;
    c.doors = IniInt("QuickLockpick", "Doors", 1) != 0;
    c.lootAfter = IniInt("QuickLockpick", "LootAfter", 1) != 0;
    c.breakChance[1] = ClampInt(IniInt("QuickLockpick", "BreakChance1", 50), 0, 100);
    c.breakChance[2] = ClampInt(IniInt("QuickLockpick", "BreakChance2", 30), 0, 100);
    c.breakChance[3] = ClampInt(IniInt("QuickLockpick", "BreakChance3", 15), 0, 100);
    c.breakChance[0] = c.breakChance[1];
    g_config = c;
    Log("config: enabled=%d key=0x%02X chests=%d doors=%d loot=%d break=%d/%d/%d%%", c.enabled, c.key, c.chests,
        c.doors, c.lootAfter, c.breakChance[1], c.breakChance[2], c.breakChance[3]);
}

// ---------------------------------------------------------------------------------------------
// Game API (Script.dll, Engine.dll, SharedBase.dll exports; Script_Game.dll's hero line helper by RVA)

struct Entity {
    void* e = nullptr;
    explicit operator bool() const { return e != nullptr; }
};

struct Api {
    Entity* (*getPlayer)(Entity* ret);
    bool (*isDead)(const Entity*);
    bool (*isNpc)(const Entity*);
    bool (*isItem)(const Entity*);
    bool (*isDialogCamera)();
    Entity* (*getOwner)(const Entity*, Entity* ret);
    void* (*getName)(const Entity*, void* retString);
    int (*randomNumber)(int max); // static, 0..max-1
    void* world;
    Entity* (*worldFocus)(const void* world, Entity* ret);
    bool (*combatValid)(const Entity*);
    int (*combatMode)(const Entity*);
    bool (*invValid)(const Entity*);
    int (*invFindStack)(const Entity* self, const void* tpl, int, bool);
    bool (*invDelete)(Entity* self, int stack, int amount);
    bool (*invHasItems)(const Entity* self, const void* tpl, int amount);
    int (*skillTotal)(const Entity*, int skill);
    bool (*lockValid)(const Entity*);
    int (*lockStatus)(const Entity*);
    void* (*lockSetStatus)(Entity* self, const int* status);
    int (*lockDifficulty)(const Entity*);
    void* (*lockKey)(const Entity* self, void* retTemplate);
    void* (*templateNew)(void* self, const char* name);
    bool (*templateValid)(const void* tpl);
    void (*templateFree)(void* tpl);
    void* achievement;
    void (*onHandleLockpick)(void* achievement);
    void* (*startEffect)(void* ret, const void* name, const Entity* at);
    void* (*stringNew)(void* self, const char* text);
    const char* (*stringText)(const void*);
    void (*stringFree)(void*);
    void* (*proxyNew)(void* self, const void* name);
    int64_t (*proxyCall)(const void* self, void* selfEntity, void* otherEntity, int64_t param);
    // Script_Game.dll (not exported): the hero speaks SVM_<voice>_<name><NN>, NN random below variants; the
    // string is passed by value (the function destroys it). Null on another game version: then no line.
    bool (*heroLine)(void* unused, void* name, int variants);
};

static Api g_api;
static bool g_missing;
constexpr int kSkillLockpick = 0x1B;
constexpr uintptr_t kHeroLineRva = 0xefef0;
static const uint8_t kHeroLineStart[] = {0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x57, 0x48, 0x8B, 0xEC, 0x48,
                                         0x83, 0xEC, 0x70, 0x48, 0x89, 0xB4, 0x24, 0x98, 0x00, 0x00, 0x00};

template <typename T>
static void Resolve(HMODULE mod, const char* modName, T& fn, const char* name)
{
    fn = mod ? reinterpret_cast<T>(GetProcAddress(mod, name)) : nullptr;
    if (!fn) {
        Log("game api: %s!%s not found", modName, name);
        g_missing = true;
    }
}

static bool ApiInit()
{
    static int state; // 0 not tried, 1 ok, -1 failed for good
    if (state)
        return state > 0;
    HMODULE script = GetModuleHandleA("Script.dll");
    HMODULE scriptGame = GetModuleHandleA("Script_Game.dll");
    if (!script || !scriptGame)
        return false; // not loaded yet
    HMODULE engine = GetModuleHandleA("Engine.dll"), base = GetModuleHandleA("SharedBase.dll");
    Api& a = g_api;
    Resolve(script, "Script", a.getPlayer, "?GetPlayer@Entity@@SA?AV1@XZ");
    Resolve(script, "Script", a.isDead, "?IsDead@Entity@@QEBA_NXZ");
    Resolve(script, "Script", a.isNpc, "?IsNPC@Entity@@QEBA_NXZ");
    Resolve(script, "Script", a.isItem, "?IsItem@Entity@@QEBA_NXZ");
    Resolve(script, "Script", a.isDialogCamera, "?IsDialogCameraActive@Entity@@SA_NXZ");
    Resolve(script, "Script", a.getOwner, "?GetOwner@Entity@@QEBA?AV1@XZ");
    Resolve(script, "Script", a.getName, "?GetName@Entity@@QEBA?AVbCString@@XZ");
    Resolve(script, "Script", a.randomNumber, "?GetRandomNumber@Entity@@SAHH@Z");
    Resolve(script, "Script", a.world, "world");
    Resolve(script, "Script", a.worldFocus, "?GetFocus@World@@QEBA?AVEntity@@XZ");
    Resolve(script, "Script", a.combatValid, "?IsValid@PSCombatSystem@@QEBA_NXZ");
    Resolve(script, "Script", a.combatMode, "?GetCombatMode@PSCombatSystem@@QEBA?AW4gECombatMode@@XZ");
    Resolve(script, "Script", a.invValid, "?IsValid@PSInventory@@QEBA_NXZ");
    Resolve(script, "Script", a.invFindStack, "?FindStackIndex@PSInventory@@QEBAHAEBVTemplate@@H_N@Z");
    Resolve(script, "Script", a.invDelete, "?DeleteItems@PSInventory@@QEAA_NHH@Z");
    Resolve(script, "Script", a.invHasItems, "?HasItems@PSInventory@@QEBA_NAEBVTemplate@@H@Z");
    Resolve(script, "Script", a.skillTotal, "?GetTotal@PSSkills@@QEBAHW4gESkill@@@Z");
    Resolve(script, "Script", a.lockValid, "?IsValid@PSLock@@QEBA_NXZ");
    Resolve(script, "Script", a.lockStatus, "??BPropertyStatus@PSLock@@QEBA?BW4gELockStatus@@XZ");
    Resolve(script, "Script", a.lockSetStatus, "??4PropertyStatus@PSLock@@QEAAAEAV01@AEBW4gELockStatus@@@Z");
    Resolve(script, "Script", a.lockDifficulty, "??BPropertyDifficulty@PSLock@@QEBA?BHXZ");
    Resolve(script, "Script", a.lockKey, "?GetKey@PSLock@@QEBA?AVTemplate@@XZ");
    Resolve(script, "Script", a.templateNew, "??0Template@@QEAA@PEBD@Z");
    Resolve(script, "Script", a.templateValid, "?IsValid@Template@@QEBA_NXZ");
    Resolve(script, "Script", a.templateFree, "??1Template@@QEAA@XZ");
    Resolve(script, "Script", a.achievement, "achievement");
    Resolve(script, "Script", a.onHandleLockpick, "?OnHandleLockpick@Achievement@@QEAAXXZ");
    Resolve(script, "Script", a.startEffect, "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVEntity@@@Z");
    Resolve(base, "SharedBase", a.stringNew, "??0bCString@@QEAA@PEBD@Z");
    Resolve(base, "SharedBase", a.stringText, "?GetText@bCString@@QEBAPEBDXZ");
    Resolve(base, "SharedBase", a.stringFree, "??1bCString@@QEAA@XZ");
    Resolve(engine, "Engine", a.proxyNew, "??0eCScriptProxyScript@@QEAA@AEBVbCString@@@Z");
    Resolve(engine, "Engine", a.proxyCall, "??ReCScriptProxyScript@@QEBA_JPEAVeCEntity@@0_J@Z");
    // Optional: the hero's lines, only on the expected Script_Game.dll
    auto* line = reinterpret_cast<uint8_t*>(scriptGame) + kHeroLineRva;
    if (memcmp(line, kHeroLineStart, sizeof(kHeroLineStart)) == 0)
        a.heroLine = reinterpret_cast<decltype(a.heroLine)>(line);
    else
        Log("game api: Script_Game.dll is not the expected version (Steam Rev. 2045), no hero lines");
    state = g_missing ? -1 : 1;
    Log("game api: %s", state > 0 ? "ready" : "INCOMPLETE (game updated?), mod disabled");
    return state > 0;
}

// eCScriptProxyScript by name, built once per name and kept (see autoloot/game.cpp).
static int64_t ScriptCall(const char* script, void* self, void* other, int64_t param)
{
    struct Proxy {
        const char* name;
        alignas(16) char data[64];
    };
    static Proxy proxies[4];
    Proxy* p = nullptr;
    for (Proxy& q : proxies)
        if (q.name == script || !q.name) {
            p = &q;
            break;
        }
    if (!p)
        return -1;
    if (!p->name) {
        alignas(8) char name[16] = {};
        g_api.stringNew(name, script);
        g_api.proxyNew(p->data, name);
        g_api.stringFree(name);
        p->name = script;
    }
    return g_api.proxyCall(p->data, self, other, param);
}

static const char* EntityName(const Entity& e, char* buf, size_t size)
{
    alignas(8) char str[16] = {};
    g_api.getName(&e, str);
    const char* text = g_api.stringText(str);
    snprintf(buf, size, "%s", text ? text : "?");
    g_api.stringFree(str);
    return buf;
}

static void Effect(const char* name, const Entity& at)
{
    alignas(8) char str[16] = {};
    alignas(8) char fx[16] = {};
    g_api.stringNew(str, name);
    g_api.startEffect(fx, str, &at);
    g_api.stringFree(str);
}

static void HeroLine(const char* name, int variants)
{
    if (!g_api.heroLine)
        return;
    alignas(8) char str[16] = {};
    g_api.stringNew(str, name);
    g_api.heroLine(nullptr, str, variants); // frees the string
}

// Template of an item by name, e.g. "It_Lockpick". Free with templateFree.
struct Tpl {
    alignas(16) char data[64] = {}; // Template, size unknown: room to spare
    explicit Tpl(const char* name) { g_api.templateNew(data, name); }
    Tpl() = default;
    ~Tpl() { g_api.templateFree(data); }
};

static bool HasKey(const Entity& player, const Entity& lock)
{
    Tpl key;
    g_api.lockKey(&lock, key.data);
    return g_api.templateValid(key.data) && g_api.invHasItems(&player, key.data, 1);
}

// ---------------------------------------------------------------------------------------------
// The quick pick

static void UseLockpick()
{
    if (!g_config.enabled || !ApiInit())
        return;
    Entity player;
    g_api.getPlayer(&player);
    if (!player || g_api.isDead(&player) || g_api.isDialogCamera())
        return;
    Entity target;
    g_api.worldFocus(g_api.world, &target);
    // Only locked (status 0, as the game's Unlock wants it) chests and doors
    if (!target || g_api.isNpc(&target) || g_api.isItem(&target) || !g_api.lockValid(&target) ||
        g_api.lockStatus(&target) != 0)
        return;
    if (g_api.combatValid(&player) && g_api.combatMode(&player) != 0)
        return; // weapon drawn: the key is the game's again
    char name[96];
    EntityName(target, name, sizeof(name));
    bool chest = g_api.invValid(&target);
    if (chest ? !g_config.chests : !g_config.doors)
        return;
    Entity owner;
    g_api.getOwner(&target, &owner);
    if (owner) {
        Log("%s: belongs to someone, left to the game's minigame", name);
        return;
    }
    // The game's checks; on "no" it already had the hero say why.
    if (ScriptCall("CanOpenLock", player.e, target.e, 0) <= 0) {
        Log("%s: the game says no (difficulty %d, skill %d)", name, g_api.lockDifficulty(&target),
            g_api.skillTotal(&player, kSkillLockpick));
        return;
    }
    const char* how = "key";
    bool broke = false, last = false;
    if (!HasKey(player, target) && g_api.lockDifficulty(&target) != 0) {
        Tpl pick("It_Lockpick");
        int stack = g_api.invFindStack(&player, pick.data, -1, false);
        if (stack < 0) {
            Log("%s: no lockpick after all", name);
            return;
        }
        int skill = ClampInt(g_api.skillTotal(&player, kSkillLockpick), 0, 3);
        int chance = g_config.breakChance[skill];
        broke = g_api.randomNumber(100) < chance;
        if (broke) {
            g_api.invDelete(&player, stack, 1);
            last = !g_api.invHasItems(&player, pick.data, 1);
        }
        how = broke ? "lockpick, broke" : "lockpick";
        Log("%s: difficulty %d, skill %d, break chance %d%%", name, g_api.lockDifficulty(&target), skill, chance);
    }
    int open = 1;
    g_api.lockSetStatus(&target, &open); // fires OnLockStatusChanged like the game
    if (g_api.lockStatus(&target) != 1) {
        Log("%s: the lock did not open", name);
        return;
    }
    if (strcmp(how, "key") != 0)
        g_api.onHandleLockpick(g_api.achievement);
    if (broke) {
        Effect("eff_ani_human_interact_lockpick_broke_01", player);
        if (last)
            HeroLine("LastLockpick", 1);
        else
            HeroLine("LockpickBroke", 3);
    } else if (strcmp(how, "key") != 0) {
        HeroLine("OpenLock", 5);
    }
    Effect("eff_ani_human_interact_lockpick_end_01", player);
    int64_t looted = 0;
    if (chest && g_config.lootAfter)
        looted = ScriptCall("AutoLoot", player.e, target.e, 0);
    Log("%s %s opened (%s)%s, looted %lld stacks", chest ? "chest" : "door", name, how, last ? ", last lockpick" : "",
        looted);
}

// ---------------------------------------------------------------------------------------------
// Risen Mods menu, window

static const HubItem kItems[] = {
    /* 0 */ {HubCheckbox, "QuickLockpick", "Enabled", "Open locks with a key, no minigame",
             "Вскрывать замки клавишей, без мини-игры", 1, 0, 0, 0, 0, nullptr, -1,
             "Look at a locked chest or door and press the key. The game decides whether you can: lockpick skill, "
             "a lockpick or the key. Locks that belong to someone are left to the game's minigame.",
             "Смотришь на запертый сундук или дверь и жмёшь клавишу. Можно ли - решает игра: навык взлома, отмычка "
             "или ключ. Чужие замки остаются обычной мини-игре."},
    /* 1 */ {HubKey, "QuickLockpick", "Key", "Key", "Клавиша", VK_RBUTTON, 0, 0, 0, 0, nullptr, 0,
             "Not while the weapon is drawn", "С обнажённым оружием не работает"},
    /* 2 */ {HubCheckbox, "QuickLockpick", "Chests", "Chests", "Сундуки", 1, 0, 0, 0, 0, nullptr, 0},
    /* 3 */ {HubCheckbox, "QuickLockpick", "Doors", "Doors", "Двери", 1, 0, 0, 0, 0, nullptr, 0},
    /* 4 */ {HubCheckbox, "QuickLockpick", "LootAfter", "Loot the chest right after opening",
             "Сразу лутать вскрытый сундук", 1, 0, 0, 0, 0, nullptr, 0},
    /* 5 */ {HubHeader, nullptr, nullptr, "Lockpick breaks", "Отмычка ломается"},
    /* 6 */ {HubNote, nullptr, nullptr, "Chance by lockpick skill level. The lock opens anyway.",
             "Шанс по уровню навыка взлома. Замок открывается в любом случае."},
    /* 7 */ {HubSlider, "QuickLockpick", "BreakChance1", "Skill 1", "Навык 1", 50, 0, 100, 5, 0, "%.0f%%", 0},
    /* 8 */ {HubSlider, "QuickLockpick", "BreakChance2", "Skill 2", "Навык 2", 30, 0, 100, 5, 0, "%.0f%%", 0},
    /* 9 */ {HubSlider, "QuickLockpick", "BreakChance3", "Skill 3", "Навык 3", 15, 0, 100, 5, 0, "%.0f%%", 0},
};

static void OnMenuChanged()
{
    ConfigLoad();
}

static HubPage g_page = {"quick-lockpick", 30, "Lockpick", "Взлом", nullptr, kItems,
                         sizeof(kItems) / sizeof(kItems[0]), OnMenuChanged};

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LOCKPICK_USE:
        UseLockpick();
        return 0;
    case WM_LOCKPICK_RELOAD:
        ConfigLoad();
        return 0;
    }
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

static DWORD WINAPI Worker(LPVOID)
{
    HWND hwnd = WaitForGameWindow();
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    if (const HubApi* hub = HubGet())
        hub->registerPage(&g_page);
    Log("ready (%s)", HubGet() ? "menu found" : "no menu.dll: ini only");
    bool wasDown = false;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        int key = g_config.key;
        bool down = key && GetForegroundWindow() == hwnd && (GetAsyncKeyState(key) & 0x8000);
        if (down && !wasDown && !HubBusy())
            PostMessageW(hwnd, WM_LOCKPICK_USE, 0, 0);
        wasDown = down;
        if (tick % 30 == 0 && IniChanged())
            PostMessageW(hwnd, WM_LOCKPICK_RELOAD, 0, 0);
        Sleep(30);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "quick_lockpick");
        Log("Risen Quick Lockpick 0.1.1");
        g_page.iniPath = IniPath(kDefaultIni);
        ConfigLoad();
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
