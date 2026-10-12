// AutoLoot: QuickLoot (one key loots every dead body around the player through the game's own "AutoLoot"
// script) and AutoLoot (picks up loose items nearby, no animation, never owned items).
// Settings: mods\autoloot.ini (hot-reloaded). All game calls run on the game thread (posted window messages).

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "game.h"
#include "hub_client.h"

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
    "; Also loot dead bodies / open chests that belong to nobody within the radius\r\n"
    "Bodies=0\r\n"
    "Chests=0\r\n"
    "; 1 = only in front of you: a cone around the camera's view, ConeAngle degrees wide (things right at your feet\r\n"
    "; are always taken). 0 = the whole circle around you\r\n"
    "Cone=1\r\n"
    "ConeAngle=120\r\n"
    "\r\n"
    "[Filter]\r\n"
    "; What AutoLoot picks up from the ground (QuickLoot always takes what you look at; bodies and chests are\r\n"
    "; emptied by the game's own loot script, all of it)\r\n"
    "Gold=1\r\n"
    "Ammo=1\r\n"
    "Weapons=1\r\n"
    "Armor=1\r\n"
    "Consumables=1\r\n"
    "Magic=1\r\n"
    "Written=1\r\n"
    "Misc=1\r\n"
    "Other=1\r\n"
    "\r\n"
    "[Effect]\r\n"
    "; A short game effect where something was looted\r\n"
    "Enabled=1\r\n"
    "; Effect name from the game (eff_* in data\\compiled\\library.pak)\r\n"
    "Name=eff_magic_telekinesis_target_once_01\r\n"
    "; Height above the looted object, cm\r\n"
    "Height=0\r\n"
    "; Size (1 = as in the game) and the effect's own sound (0 = silent)\r\n"
    "Size=0.15\r\n"
    "Sound=0\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; Writes everything around the player to autoloot.log\r\n"
    "DumpKey=\r\n"
    "DumpRadius=1500\r\n"
    "; Plays the next of a list of candidate effects at the focus / the player (name goes to autoloot.log)\r\n"
    "EffectTestKey=\r\n";

struct Config {
    bool quickLoot = true;
    int quickKey = VK_RBUTTON;
    bool quickGround = true;
    bool quickChests = true;
    bool autoLoot = true;
    bool autoActive = false;
    int autoKey = 'F';
    float autoRadius = 300.0f;
    bool autoBodies = false;
    bool autoChests = false;
    bool autoCone = true;
    float coneAngle = 120.0f;
    bool takeGold = true, takeAmmo = true, takeWeapons = true, takeArmor = true, takeConsumables = true,
         takeMagic = true, takeWritten = true, takeMisc = true, takeOther = true;
    bool effect = true;
    char effectName[96] = {};
    float effectHeight = 0.0f;
    float effectSize = 0.15f;
    bool effectSound = false;
    int dumpKey = 0;
    float dumpRadius = 1500.0f;
    int effectTestKey = 0;
};

static Config g_config;
static bool g_autoActive;
static WNDPROC g_origProc;

constexpr UINT WM_LOOT_QUICK = WM_APP + 0x5401;
constexpr UINT WM_LOOT_AUTO_TOGGLE = WM_APP + 0x5402;
constexpr UINT WM_LOOT_AUTO_TICK = WM_APP + 0x5403;
constexpr UINT WM_LOOT_DUMP = WM_APP + 0x5404;
constexpr UINT WM_LOOT_RELOAD = WM_APP + 0x5405;
constexpr UINT WM_LOOT_EFFECT_TEST = WM_APP + 0x5406;

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
    c.autoBodies = IniInt("AutoLoot", "Bodies", 0) != 0;
    c.autoChests = IniInt("AutoLoot", "Chests", 0) != 0;
    c.autoCone = IniInt("AutoLoot", "Cone", 1) != 0;
    c.coneAngle = Clamp(IniFloat("AutoLoot", "ConeAngle", 120.0f), 30.0f, 360.0f);
    c.takeGold = IniInt("Filter", "Gold", 1) != 0;
    c.takeAmmo = IniInt("Filter", "Ammo", 1) != 0;
    c.takeWeapons = IniInt("Filter", "Weapons", 1) != 0;
    c.takeArmor = IniInt("Filter", "Armor", 1) != 0;
    c.takeConsumables = IniInt("Filter", "Consumables", 1) != 0;
    c.takeMagic = IniInt("Filter", "Magic", 1) != 0;
    c.takeWritten = IniInt("Filter", "Written", 1) != 0;
    c.takeMisc = IniInt("Filter", "Misc", 1) != 0;
    c.takeOther = IniInt("Filter", "Other", 1) != 0;
    c.effect = IniInt("Effect", "Enabled", 1) != 0;
    IniString("Effect", "Name", "eff_magic_telekinesis_target_once_01", c.effectName, sizeof(c.effectName));
    c.effectHeight = Clamp(IniFloat("Effect", "Height", 0.0f), -500.0f, 500.0f);
    c.effectSize = Clamp(IniFloat("Effect", "Size", 0.15f), 0.1f, 3.0f);
    c.effectSound = IniInt("Effect", "Sound", 0) != 0;
    c.effectTestKey = IniKey("Debug", "EffectTestKey", 0);
    c.dumpKey = IniKey("Debug", "DumpKey", 0);
    c.dumpRadius = Clamp(IniFloat("Debug", "DumpRadius", 1500.0f), 100.0f, 100000.0f);
    g_config = c;
    if (first)
        g_autoActive = c.autoActive;
    Log("config: quick=%d key=0x%02X ground=%d chests=%d | auto=%d active=%d key=0x%02X r=%.0f bodies=%d chests=%d | "
        "cone=%d %.0f | take gold=%d ammo=%d weap=%d armor=%d cons=%d magic=%d written=%d misc=%d other=%d | "
        "effect=%d %s h=%.0f size=%.2f sound=%d | dump=0x%02X r=%.0f fxtest=0x%02X",
        c.quickLoot, c.quickKey, c.quickGround, c.quickChests, c.autoLoot, g_autoActive, c.autoKey, c.autoRadius,
        c.autoBodies, c.autoChests, c.autoCone, c.coneAngle, c.takeGold, c.takeAmmo, c.takeWeapons, c.takeArmor,
        c.takeConsumables, c.takeMagic, c.takeWritten, c.takeMisc, c.takeOther, c.effect, c.effectName, c.effectHeight, c.effectSize, c.effectSound, c.dumpKey, c.dumpRadius, c.effectTestKey);
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

// An arrow / bolt still in flight (also the player's own: it used to be picked up right after the shot).
static bool IsFlying(const Entity& e)
{
    return g_game.projValid(&e) && g_game.projActive(&e);
}

// Taking it would be theft. The game's AssessTheft: an item someone dropped is never theft, otherwise it may be
// when the item has an owner - we stay on the safe side and skip every owned item.
static bool WouldBeTheft(const Entity& e)
{
    return HasOwner(e) && !(g_game.itemValid(&e) && g_game.itemDropped(&e));
}

// Loose item that may be taken: not theft, not a quest item, not hanging in a slot (torches in holders etc.),
// not an arrow in flight.
static bool IsFreeGroundItem(Entity e)
{
    return g_game.isItem(&e) && g_game.itemValid(&e) && !g_game.itemMission(&e) && !g_game.attachedToSlot(&e) &&
           !IsFlying(e) && !WouldBeTheft(e);
}

// Within the view cone (or the cone is off). Things right at the player's feet always count.
static bool InFront(const Entity& player, const Entity& e, float distance)
{
    if (!g_config.autoCone || g_config.coneAngle >= 359.0f || distance < 100.0f)
        return true;
    float vx, vz;
    if (!ViewDirection(player, &vx, &vz))
        return true;
    Vec3 p = EntityPosition(player), t = EntityPosition(e);
    float dx = t.x - p.x, dz = t.z - p.z, len = sqrtf(dx * dx + dz * dz);
    if (len < 1.0f)
        return true;
    return (dx * vx + dz * vz) / len >= cosf(g_config.coneAngle * 0.5f * 3.14159265f / 180.0f);
}

static bool NameStarts(const char* name, const char* prefix)
{
    return _strnicmp(name, prefix, strlen(prefix)) == 0;
}

// The [Filter] switches, for radius pickup only.
static bool Wanted(const Entity& e)
{
    char name[96];
    EntityName(e, name, sizeof(name));
    if (_stricmp(name, "It_Gold") == 0 || NameStarts(name, "It_GoldBag"))
        return g_config.takeGold;
    if (NameStarts(name, "It_Arrow") || NameStarts(name, "It_Bolt"))
        return g_config.takeAmmo;
    switch (g_game.itemCategory(&e)) {
    case ItemCatWeapon: return g_config.takeWeapons;
    case ItemCatArmor: return g_config.takeArmor;
    case ItemCatConsumable: return g_config.takeConsumables;
    case ItemCatMagic: return g_config.takeMagic;
    case ItemCatWritten: return g_config.takeWritten;
    case ItemCatMisc: return g_config.takeMisc;
    default: return g_config.takeOther;
    }
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
    alignas(16) float cam[16] = {};
    g_game.cameraPose(cam);
    Vec3 at{};
    g_game.atVector(&player, &at);
    float vx = 0, vz = 0;
    bool view = ViewDirection(player, &vx, &vz);
    Log("dump: camera right (%.2f %.2f %.2f) up (%.2f %.2f %.2f) at (%.2f %.2f %.2f), player at (%.2f %.2f %.2f), "
        "view %d (%.2f %.2f)", cam[0], cam[1], cam[2], cam[4], cam[5], cam[6], cam[8], cam[9], cam[10], at.x, at.y,
        at.z, view, vx, vz);
    for (const Nearby& n : list) {
        const Entity& e = n.e;
        char name[96];
        EntityName(e, name, sizeof(name));
        bool inv = g_game.invValid(&e), item = g_game.itemValid(&e), lock = g_game.lockValid(&e);
        bool npc = g_game.isNpc(&e); // IsDead/IsDown only for NPCs: they read the NPC's routine
        Log("  %6.0f %-40s npc=%d dead=%d down=%d item=%d obj=%d owner=%d inv=%d stacks=%d itemPS=%d mission=%d "
            "amount=%lu cat=%d dropped=%d flying=%d front=%d slot=%d lock=%d status=%d",
            n.distance, name, npc, npc && g_game.isDead(&e), npc && g_game.isDown(&e), g_game.isItem(&e),
            g_game.isInteractObject(&e), HasOwner(e), inv, inv ? g_game.invStackCount(&e) : -1, item,
            item ? g_game.itemMission(&e) : 0, item ? g_game.itemAmount(&e) : 0, item ? g_game.itemCategory(&e) : -1,
            item ? g_game.itemDropped(&e) : 0, IsFlying(e), InFront(player, e, n.distance),
            item ? g_game.attachedToSlot(const_cast<Entity*>(&e)) : 0, lock, lock ? g_game.lockStatus(&e) : -1);
        if (!inv || !npc || !g_game.isDead(&e))
            continue;
        // What the game's loot filter (used by its AutoLoot script) says about each stack of a dead body.
        for (int i = 0, n = g_game.invStackCount(&e); i < n && i < 32; ++i) {
            Entity tpl;
            g_game.invTemplateItem(&e, &tpl, i);
            char stackName[96];
            EntityName(tpl, stackName, sizeof(stackName));
            Log("      stack %d %-32s type=%d cat=%d amount=%d lootFilter=%lld", i, stackName, g_game.invStackType(&e, i),
                g_game.invCategory(&e, i), g_game.invAmount(&e, i), ScriptCall("InvFilter_Loot", e.e, nullptr, i));
        }
    }
}

// Debug: plays the candidates one after another at the focus target (or the player) to pick a loot effect.
static void EffectTest()
{
    static const char* const kCandidates[] = {
        "eff_magic_telekinesis_target_once_01", "eff_magic_magicopen_chest_01", "eff_magic_light_end_01",
        "eff_magic_illusion_target_end_01", "eff_magic_summoned_end_01", "eff_magic_teleport_end_01",
        "eff_magic_heal_target_small_01", "eff_magic_protect_end_01", "eff_magic_speed_end_01",
        "eff_script_itemgiven_01", "eff_script_goldgiven_01", "eff_ui_trade_01",
    };
    static int next;
    Entity player;
    if (!GameReady(&player))
        return;
    Entity target;
    g_game.worldFocus(g_game.world, &target);
    const char* name = kCandidates[next++ % (sizeof(kCandidates) / sizeof(kCandidates[0]))];
    Vec3 at = EntityPosition(target ? target : player);
    bool exists = EffectExists(name), started = exists && PlayEffect(name, at, g_config.effectSize, !g_config.effectSound);
    Log("effect test: %s at %s (%.0f %.0f %.0f) exists=%d started=%d", name, target ? "focus" : "player", at.x, at.y,
        at.z, exists, started);
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

// The configured effect at a looted spot (for items the position is taken before they leave the world).
static void Sparkle(Vec3 at)
{
    if (!g_config.effect || !g_config.effectName[0])
        return;
    at.y += g_config.effectHeight; // the engine is Y-up
    if (!PlayEffect(g_config.effectName, at, g_config.effectSize, !g_config.effectSound))
        Log("effect: %s did not start", g_config.effectName);
}

// No animation: straight into the inventory, with the game's "+ item" line on screen.
static void PickUp(Entity& player, Entity e, const char* who)
{
    char name[96];
    EntityName(e, name, sizeof(name));
    unsigned long amount = g_game.itemAmount(&e);
    int category = g_game.itemCategory(&e);
    Entity copy = e;
    g_game.printItemLog(g_game.gui2, &copy, static_cast<int>(amount));
    Vec3 at = EntityPosition(e);
    int slot = g_game.invInsertStack(&player, &e); // the world item is gone after this
    Sparkle(at);
    Log("%s: %s x%lu (category %d) -> stack %d", who, name, amount, category, slot);
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
    if (moved > 0)
        Sparkle(EntityPosition(target));
    Log("quickloot: %s %s -> %lld stacks", body ? "body" : "chest", name, moved);
}

// Items are only taken once they lie still: seen at the same spot on two ticks in a row (~0.25 s). Catches anything
// moving - arrows in flight (ours and the enemies'), thrown or falling things - even if PSProjectile says otherwise.
struct Seen {
    Vec3 at;
    DWORD when;
};
static std::unordered_map<void*, Seen> g_seen;

static bool Resting(const Entity& e, DWORD now)
{
    Vec3 at = EntityPosition(e);
    if (g_seen.size() > 2000)
        g_seen.clear();
    auto it = g_seen.find(e.e);
    bool still = false;
    if (it != g_seen.end() && now - it->second.when < 1000) {
        float dx = at.x - it->second.at.x, dy = at.y - it->second.at.y, dz = at.z - it->second.at.z;
        still = dx * dx + dy * dy + dz * dz < 5.0f * 5.0f;
    }
    g_seen[e.e] = {at, now};
    return still;
}

// Bodies / chests the AutoLoot script ran on: whatever it leaves behind (stacks it won't take) must not make us
// call it again four times a second.
struct LootTry {
    bool done = false;
    bool generated = false; // GeneratePlunder asked
    int tries = 0;
    DWORD next = 0;
};
static std::unordered_map<void*, LootTry> g_looted;

static void AutoLootTick()
{
    Entity player;
    if (!g_config.autoLoot || !g_autoActive || !GameReady(&player))
        return;
    DWORD now = GetTickCount();
    bool combat = CombatMode(player) != 0;
    if (g_looted.size() > 1000)
        g_looted.clear();
    for (const Nearby& n : CollectNearby(player, g_config.autoRadius)) {
        if (!InFront(player, n.e, n.distance))
            continue;
        if (IsFreeGroundItem(n.e)) {
            if (Wanted(n.e) && Resting(n.e, now))
                PickUp(player, n.e, "autoloot");
            continue;
        }
        Entity container = n.e;
        // Animals and monsters get their plunder (meat, skins...) only when first focused: ask the game the same
        // question the focus asks, once per body.
        if (g_config.autoBodies && g_game.isNpc(&container) && g_game.isDead(&container) &&
            g_game.invValid(&container)) {
            LootTry& t = g_looted[container.e];
            if (!t.generated) {
                t.generated = true;
                if (GeneratePlunder(player, container)) {
                    char name[96];
                    Log("autoloot: plunder created for %s", EntityName(container, name, sizeof(name)));
                }
            }
        }
        bool body = g_config.autoBodies && IsDeadBody(n.e);
        bool chest = !body && g_config.autoChests && IsOpenChest(n.e);
        if (!body && !chest)
            continue;
        LootTry& t = g_looted[n.e.e];
        if (t.done || now < t.next)
            continue;
        char name[96];
        EntityName(n.e, name, sizeof(name));
        // Not sure yet whether the game's AutoLoot script gives anything while the player's weapon is drawn: when
        // it doesn't, the same steps are done by hand.
        int64_t moved = ScriptAutoLoot(player, container);
        const char* how = "script";
        if (moved <= 0 && combat) {
            moved = LootContainer(player, container);
            how = "by hand, weapon drawn";
        }
        if (moved > 0)
            Sparkle(EntityPosition(n.e));
        // Nothing taken: try again in a second, a few times, then leave what the game won't give.
        t.done = moved > 0 || ++t.tries >= 8;
        t.next = now + 1000;
        Log("autoloot: %s %s -> %lld stacks (%s, try %d)", body ? "body" : "chest", name, moved,
            how, t.tries + (moved > 0));
    }
}

// ---------------------------------------------------------------------------------------------
// Risen Mods menu

static const HubItem kItems[] = {
    /* 0 */ {HubHeader, nullptr, nullptr, "QuickLoot", "Быстрый лут"},
    /* 1 */ {HubCheckbox, "QuickLoot", "Enabled", "Loot what you look at with a key",
             "Лутать то, на что смотришь, по клавише", 1, 0, 0, 0, 0, nullptr, -1,
             "Dead bodies, open chests, items: everything at once, no loot window",
             "Трупы, открытые сундуки, предметы: всё сразу, без окна лута"},
    /* 2 */ {HubKey, "QuickLoot", "Key", "Key", "Клавиша", VK_RBUTTON, 0, 0, 0, 0, nullptr, 1,
             "Not while the weapon is drawn: then the key works as usual in the game",
             "С обнажённым оружием не лутает: клавиша работает как обычно в игре"},
    /* 3 */ {HubCheckbox, "QuickLoot", "GroundItems", "Items on the ground", "Предметы на земле", 1, 0, 0, 0, 0,
             nullptr, 1},
    /* 4 */ {HubCheckbox, "QuickLoot", "Chests", "Open chests", "Открытые сундуки", 1, 0, 0, 0, 0, nullptr, 1},
    /* 5 */ {HubHeader, nullptr, nullptr, "AutoLoot", "Автолут"},
    /* 6 */ {HubCheckbox, "AutoLoot", "Enabled", "Pick up loose items around you", "Подбирать предметы вокруг", 1,
             0, 0, 0, 0, nullptr, -1,
             "Switched on and off with the key below. Never takes what would be theft, nor arrows in flight.",
             "Включается и выключается клавишей ниже. Не берёт то, что было бы кражей, и летящие стрелы."},
    /* 7 */ {HubKey, "AutoLoot", "ToggleKey", "On / off key", "Клавиша вкл/выкл", 'F', 0, 0, 0, 0, nullptr, 6},
    /* 8 */ {HubSlider, "AutoLoot", "Radius", "Radius", "Радиус", 300, 1.0f, 30.0f, 0.5f, 0.01f, "%.1f m", 6},
    /* 9 */ {HubCheckbox, "AutoLoot", "Bodies", "Also search dead bodies", "Обыскивать трупы", 0, 0, 0, 0, 0,
             nullptr, 6},
    /*10 */ {HubCheckbox, "AutoLoot", "Chests", "Also empty open chests", "Опустошать открытые сундуки", 0, 0, 0, 0,
             0, nullptr, 6, "Only chests that belong to nobody and are not locked",
             "Только ничьи и не запертые сундуки"},
    /*11 */ {HubCheckbox, "AutoLoot", "Active", "On at game start", "Включён при запуске игры", 0, 0, 0, 0, 0,
             nullptr, 6},
    /*12 */ {HubCheckbox, "AutoLoot", "Cone", "Only in front of you", "Только впереди", 1, 0, 0, 0, 0, nullptr, 6,
             "A cone around where the camera looks; off = the whole circle. Things at your feet are always taken.",
             "Конус по направлению камеры; выключено = весь круг. То, что прямо под ногами, берётся всегда."},
    /*13 */ {HubSlider, "AutoLoot", "ConeAngle", "Cone width", "Ширина конуса", 120, 30, 360, 5, 0, "%.0f°", 12},
    /*14 */ {HubHeader, nullptr, nullptr, "What AutoLoot picks up", "Что подбирает автолут"},
    /*15 */ {HubNote, nullptr, nullptr, "Items on the ground. QuickLoot always takes what you look at.",
             "Предметы на земле. Быстрый лут всегда берёт то, на что смотришь."},
    /*16 */ {HubCheckbox, "Filter", "Gold", "Gold", "Золото", 1, 0, 0, 0, 0, nullptr, 6},
    /*17 */ {HubCheckbox, "Filter", "Ammo", "Arrows and bolts", "Стрелы и болты", 1, 0, 0, 0, 0, nullptr, 6},
    /*18 */ {HubCheckbox, "Filter", "Weapons", "Weapons", "Оружие", 1, 0, 0, 0, 0, nullptr, 6},
    /*19 */ {HubCheckbox, "Filter", "Armor", "Armour", "Броня", 1, 0, 0, 0, 0, nullptr, 6},
    /*20 */ {HubCheckbox, "Filter", "Consumables", "Potions, food, plants", "Зелья, еда, растения", 1, 0, 0, 0, 0,
             nullptr, 6},
    /*21 */ {HubCheckbox, "Filter", "Magic", "Scrolls and runes", "Свитки и руны", 1, 0, 0, 0, 0, nullptr, 6},
    /*22 */ {HubCheckbox, "Filter", "Written", "Letters, books, maps", "Письма, книги, карты", 1, 0, 0, 0, 0,
             nullptr, 6},
    /*23 */ {HubCheckbox, "Filter", "Misc", "Misc (trophies, ore, tools...)", "Разное (трофеи, руда, инструменты...)",
             1, 0, 0, 0, 0, nullptr, 6},
    /*24 */ {HubCheckbox, "Filter", "Other", "Everything else", "Всё остальное", 1, 0, 0, 0, 0, nullptr, 6},
    /*25 */ {HubHeader, nullptr, nullptr, "Effect", "Эффект"},
    /*26 */ {HubCheckbox, "Effect", "Enabled", "Sparkle where something was looted", "Искра там, где что-то залутано",
             1, 0, 0, 0, 0, nullptr, -1, "A short game effect at the item, body or chest",
             "Короткий игровой эффект у предмета, трупа или сундука"},
    /*27 */ {HubSlider, "Effect", "Size", "Size", "Размер", 0.15f, 0.1f, 2.0f, 0.05f, 0, "%.2f", 26},
    /*28 */ {HubCheckbox, "Effect", "Sound", "Effect sound", "Звук эффекта", 0, 0, 0, 0, 0, nullptr, 26},
};

static void OnMenuChanged()
{
    ConfigLoad(false);
}

static HubPage g_page = {"autoloot", 20, "AutoLoot", "Автолут", nullptr, kItems, sizeof(kItems) / sizeof(kItems[0]),
                         OnMenuChanged};

static bool StatusQuery(char*, int)
{
    return g_config.autoLoot && g_autoActive;
}

static const HubStatus kStatus = {"autoloot", 20, HubIconHand, StatusQuery};

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
    case WM_LOOT_EFFECT_TEST:
        EffectTest();
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
    if (const HubApi* hub = HubGet()) {
        hub->registerPage(&g_page);
        hub->registerStatus(&kStatus);
    }
    Log("autoloot: ready (%s)", HubGet() ? "menu found" : "no menu.dll: ini only");

    KeyEdge quick, toggle, dump, fxTest;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool focused = GetForegroundWindow() == hwnd;
        bool busy = HubBusy(); // the menu is open: its clicks and key rebinds are not ours
        if (quick.Pressed(g_config.quickKey, focused) && !busy)
            PostMessageW(hwnd, WM_LOOT_QUICK, 0, 0);
        if (toggle.Pressed(g_config.autoKey, focused) && !busy)
            PostMessageW(hwnd, WM_LOOT_AUTO_TOGGLE, 0, 0);
        if (dump.Pressed(g_config.dumpKey, focused) && !busy)
            PostMessageW(hwnd, WM_LOOT_DUMP, 0, 0);
        if (fxTest.Pressed(g_config.effectTestKey, focused) && !busy)
            PostMessageW(hwnd, WM_LOOT_EFFECT_TEST, 0, 0);
        if (tick % 8 == 0 && g_autoActive && !busy)
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
        Log("AutoLoot 0.6");
        g_page.iniPath = IniPath(kDefaultIni);
        ConfigLoad(true);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
