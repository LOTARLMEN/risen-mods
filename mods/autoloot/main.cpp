// AutoLoot: QuickLoot (one key loots every dead body around the player through the game's own "AutoLoot"
// script) and AutoLoot (picks up loose items nearby, no animation, never owned items).
// Settings: mods\autoloot.ini (hot-reloaded). All game calls run on the game thread (posted window messages).

#include <windows.h>
#include <algorithm>
#include <vector>

#include "common.h"
#include "game.h"

static const char kDefaultIni[] =
    "; AutoLoot mod settings. Changes are picked up while the game is running.\r\n"
    "; Keys: F1-F24, A-Z, 0-9, NUMPAD0-9, MOUSE3, MOUSE4, MOUSE5, or a hex code like 0x46.\r\n"
    "; Free keys in Risen: F, G, H, J, K, N, O, P, V, X, Z (Z is the Speed mod's default).\r\n"
    "; MOUSE2 (right button) is also the game's Action 2: looting only happens when there is something to loot.\r\n"
    "; Distances are in centimetres (500 = 5 m).\r\n"
    "\r\n"
    "[QuickLoot]\r\n"
    "; The key loots what you are looking at (the game's focus) at once, without the loot window:\r\n"
    "; dead bodies, open chests that belong to nobody, loose items. Not while the weapon is drawn.\r\n"
    "Enabled=1\r\n"
    "Key=MOUSE2\r\n"
    "; Also items on the ground / chests (0 = only dead bodies)\r\n"
    "GroundItems=1\r\n"
    "Chests=1\r\n"
    "\r\n"
    "[AutoLoot]\r\n"
    "; Pick up loose items around you automatically (items that belong to someone are never taken)\r\n"
    "Enabled=1\r\n"
    "; On at game start (the toggle key switches it while playing)\r\n"
    "Active=0\r\n"
    "ToggleKey=F\r\n"
    "Radius=300\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; Writes everything around the player to autoloot.log\r\n"
    "DumpKey=\r\n"
    "DumpRadius=1500\r\n";

struct Config {
    bool quickLoot = true;
    int quickKey = VK_RBUTTON;
    bool quickGround = true;
    bool quickChests = true;
    bool autoLoot = true;
    bool autoActive = false;
    int autoKey = 'F';
    float autoRadius = 300.0f;
    int dumpKey = 0;
    float dumpRadius = 1500.0f;
};

static Config g_config;
static bool g_autoActive;
static WNDPROC g_origProc;

constexpr UINT WM_LOOT_QUICK = WM_APP + 0x5401;
constexpr UINT WM_LOOT_AUTO_TOGGLE = WM_APP + 0x5402;
constexpr UINT WM_LOOT_AUTO_TICK = WM_APP + 0x5403;
constexpr UINT WM_LOOT_DUMP = WM_APP + 0x5404;
constexpr UINT WM_LOOT_RELOAD = WM_APP + 0x5405;

static float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void ConfigLoad(bool first)
{
    Config c;
    c.quickLoot = IniInt("QuickLoot", "Enabled", 1) != 0;
    c.quickKey = IniKey("QuickLoot", "Key", VK_RBUTTON);
    c.quickGround = IniInt("QuickLoot", "GroundItems", 1) != 0;
    c.quickChests = IniInt("QuickLoot", "Chests", 1) != 0;
    c.autoLoot = IniInt("AutoLoot", "Enabled", 1) != 0;
    c.autoActive = IniInt("AutoLoot", "Active", 0) != 0;
    c.autoKey = IniKey("AutoLoot", "ToggleKey", 'F');
    c.autoRadius = Clamp(IniFloat("AutoLoot", "Radius", 300.0f), 100.0f, 3000.0f);
    c.dumpKey = IniKey("Debug", "DumpKey", 0);
    c.dumpRadius = Clamp(IniFloat("Debug", "DumpRadius", 1500.0f), 100.0f, 100000.0f);
    g_config = c;
    if (first)
        g_autoActive = c.autoActive;
    Log("config: quick=%d key=0x%02X ground=%d chests=%d | auto=%d active=%d key=0x%02X r=%.0f | dump=0x%02X r=%.0f",
        c.quickLoot, c.quickKey, c.quickGround, c.quickChests, c.autoLoot, g_autoActive, c.autoKey, c.autoRadius,
        c.dumpKey, c.dumpRadius);
}

// ---------------------------------------------------------------------------------------------
// Game-thread actions

struct Nearby {
    Entity e;
    float distance;
};

// Entities around the player closer than radius, nearest first. The list is the game's interaction list
// (NPCs, items, chests, other interactables in the processing range).
static std::vector<Nearby> CollectNearby(const Entity& player, float radius)
{
    std::vector<Nearby> out;
    g_game.updateEntityLists();
    const EntityArray& all = *g_game.entities;
    for (uint32_t i = 0; i < all.count; ++i) {
        Entity e = all.data[i];
        if (!e || e.e == player.e)
            continue;
        float d = g_game.distanceTo(&player, &e);
        if (d <= radius)
            out.push_back({e, d});
    }
    std::sort(out.begin(), out.end(), [](const Nearby& a, const Nearby& b) { return a.distance < b.distance; });
    return out;
}

static bool GameReady(Entity* player)
{
    if (!GameApiInit())
        return false;
    *player = Player();
    return player->e && !g_game.isDead(player) && !g_game.isDialogCamera();
}

static int CombatMode(const Entity& player)
{
    return g_game.combatValid(&player) ? g_game.combatMode(&player) : 0;
}

static void Dump()
{
    Entity player;
    if (!GameReady(&player)) {
        Log("dump: no player / dead / dialog");
        return;
    }
    std::vector<Nearby> list = CollectNearby(player, g_config.dumpRadius);
    Log("dump: %u entities in list, %zu within %.0f, player combat mode %d", g_game.entities->count, list.size(),
        g_config.dumpRadius, CombatMode(player));
    for (const Nearby& n : list) {
        const Entity& e = n.e;
        char name[96];
        EntityName(e, name, sizeof(name));
        bool inv = g_game.invValid(&e), item = g_game.itemValid(&e), lock = g_game.lockValid(&e);
        bool npc = g_game.isNpc(&e); // IsDead/IsDown only for NPCs: they read the NPC's routine
        Log("  %6.0f %-40s npc=%d dead=%d down=%d item=%d obj=%d owner=%d inv=%d stacks=%d itemPS=%d mission=%d "
            "amount=%lu slot=%d lock=%d status=%d",
            n.distance, name, npc, npc && g_game.isDead(&e), npc && g_game.isDown(&e), g_game.isItem(&e),
            g_game.isInteractObject(&e), HasOwner(e), inv, inv ? g_game.invStackCount(&e) : -1, item,
            item ? g_game.itemMission(&e) : 0, item ? g_game.itemAmount(&e) : 0,
            item ? g_game.attachedToSlot(const_cast<Entity*>(&e)) : 0, lock, lock ? g_game.lockStatus(&e) : -1);
    }
}

// Loose item nobody owns, not a quest item, not hanging in a slot (torches in holders etc.).
static bool IsFreeGroundItem(Entity e)
{
    return g_game.isItem(&e) && g_game.itemValid(&e) && !g_game.itemMission(&e) && !g_game.attachedToSlot(&e) &&
           !HasOwner(e);
}

// Dead NPC or monster with something on it. Knocked-out people are left alone (looting them is theft).
static bool IsDeadBody(const Entity& e)
{
    return g_game.isNpc(&e) && g_game.isDead(&e) && g_game.invValid(&e) && g_game.invStackCount(&e) > 0;
}

// Container that isn't an NPC or item, has stacks, belongs to nobody and is not locked.
// Guess until checked in game: PSLock status 1 = open.
static bool IsOpenChest(const Entity& e)
{
    if (g_game.isNpc(&e) || g_game.isItem(&e) || !g_game.invValid(&e) || g_game.invStackCount(&e) <= 0 || HasOwner(e))
        return false;
    return !g_game.lockValid(&e) || g_game.lockStatus(&e) == 1;
}

// No animation: straight into the inventory, with the game's "+ item" line on screen.
static void PickUp(Entity& player, Entity e, const char* who)
{
    char name[96];
    EntityName(e, name, sizeof(name));
    unsigned long amount = g_game.itemAmount(&e);
    Entity copy = e;
    g_game.printItemLog(g_game.gui2, &copy, static_cast<int>(amount));
    int slot = g_game.invInsertStack(&player, &e); // the world item is gone after this
    Log("%s: %s x%lu -> stack %d", who, name, amount, slot);
}

static void QuickLoot()
{
    Entity player;
    if (!g_config.quickLoot || !GameReady(&player))
        return;
    Entity target;
    g_game.worldFocus(g_game.world, &target);
    if (!target)
        return;
    if (int mode = CombatMode(player)) { // weapon drawn: no looting in a fight, the key is Action 2 there
        Log("quickloot: skipped, combat mode %d", mode);
        return;
    }
    char name[96];
    EntityName(target, name, sizeof(name));
    if (IsFreeGroundItem(target)) {
        if (g_config.quickGround)
            PickUp(player, target, "quickloot");
        return;
    }
    bool body = IsDeadBody(target), chest = !body && g_config.quickChests && IsOpenChest(target);
    if (!body && !chest) {
        Log("quickloot: focus %s is not lootable", name);
        return;
    }
    int64_t moved = ScriptAutoLoot(player, target);
    Log("quickloot: %s %s -> %lld stacks", body ? "body" : "chest", name, moved);
}

static void AutoLootTick()
{
    Entity player;
    if (!g_config.autoLoot || !g_autoActive || !GameReady(&player))
        return;
    for (const Nearby& n : CollectNearby(player, g_config.autoRadius))
        if (IsFreeGroundItem(n.e))
            PickUp(player, n.e, "autoloot");
}

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LOOT_QUICK:
        QuickLoot();
        return 0;
    case WM_LOOT_AUTO_TOGGLE:
        if (g_config.autoLoot) {
            g_autoActive = !g_autoActive;
            Log("autoloot: %s", g_autoActive ? "ON" : "OFF");
        }
        return 0;
    case WM_LOOT_AUTO_TICK:
        AutoLootTick();
        return 0;
    case WM_LOOT_DUMP:
        Dump();
        return 0;
    case WM_LOOT_RELOAD:
        ConfigLoad(false);
        return 0;
    }
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

struct KeyEdge {
    int key = 0;
    bool wasDown = false;
    bool Pressed(int vk, bool focused)
    {
        bool down = vk && focused && (GetAsyncKeyState(vk) & 0x8000);
        bool pressed = down && !wasDown && vk == key;
        key = vk;
        wasDown = down;
        return pressed;
    }
};

static DWORD WINAPI Worker(LPVOID)
{
    HWND hwnd = WaitForGameWindow();
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    Log("autoloot: ready");

    KeyEdge quick, toggle, dump;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool focused = GetForegroundWindow() == hwnd;
        if (quick.Pressed(g_config.quickKey, focused))
            PostMessageW(hwnd, WM_LOOT_QUICK, 0, 0);
        if (toggle.Pressed(g_config.autoKey, focused))
            PostMessageW(hwnd, WM_LOOT_AUTO_TOGGLE, 0, 0);
        if (dump.Pressed(g_config.dumpKey, focused))
            PostMessageW(hwnd, WM_LOOT_DUMP, 0, 0);
        if (tick % 8 == 0 && g_autoActive)
            PostMessageW(hwnd, WM_LOOT_AUTO_TICK, 0, 0); // ~4 times a second
        if (tick % 30 == 0 && IniChanged())
            PostMessageW(hwnd, WM_LOOT_RELOAD, 0, 0);
        Sleep(30);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "autoloot");
        Log("AutoLoot 0.2");
        IniPath(kDefaultIni);
        ConfigLoad(true);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
