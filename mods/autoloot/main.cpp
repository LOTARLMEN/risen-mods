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
#include "focus_name.h"
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
    "; dead bodies, open chests that belong to nobody, loose items, quest items too. Not while the weapon is drawn.\r\n"
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
    "; With Bodies=1: also people knocked out in a non-lethal fight, when the game lets you search them\r\n"
    "KnockedOut=1\r\n"
    "; 1 = only in front of you: a cone around the camera's view, ConeAngle degrees wide (things right at your feet\r\n"
    "; are always taken). 0 = the whole circle around you\r\n"
    "Cone=1\r\n"
    "ConeAngle=120\r\n"
    "; Also quest items (on the ground, in bodies and chests). Chests you unlocked yourself are always left to you.\r\n"
    "QuestItems=0\r\n"
    "\r\n"
    "[Indicator]\r\n"
    "; Hand in the top-left corner while AutoLoot is on (needs the Risen Mods menu, menu.dll)\r\n"
    "Show=1\r\n"
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
    "[Empty]\r\n"
    "; Adds a mark to the name of an empty chest or dead body you look at, so it needn't be opened\r\n"
    "Mark=1\r\n"
    "; The mark; empty = (Пусто) / (Empty) by the menu's language\r\n"
    "Text=\r\n"
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
    bool autoDown = true; // knocked-out people too (with autoBodies)
    bool autoChests = false;
    bool autoCone = true;
    float coneAngle = 120.0f;
    bool autoQuest = false;
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
    bool emptyMark = true;
    bool indicator = true; // hand icon (menu.dll draws it)
    wchar_t emptyText[48] = {};
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
    c.autoDown = IniInt("AutoLoot", "KnockedOut", 1) != 0;
    c.autoChests = IniInt("AutoLoot", "Chests", 0) != 0;
    c.autoCone = IniInt("AutoLoot", "Cone", 1) != 0;
    c.coneAngle = Clamp(IniFloat("AutoLoot", "ConeAngle", 120.0f), 30.0f, 360.0f);
    c.autoQuest = IniInt("AutoLoot", "QuestItems", 0) != 0;
    c.indicator = IniInt("Indicator", "Show", 1) != 0;
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
    c.emptyMark = IniInt("Empty", "Mark", 1) != 0;
    char text[96];
    IniString("Empty", "Text", "", text, sizeof(text));
    if (!text[0]) { // the menu's language, from menu.ini next to ours
        char menuIni[MAX_PATH], lang[8];
        snprintf(menuIni, sizeof(menuIni), "%s", IniPath());
        if (char* slash = strrchr(menuIni, '\\'))
            snprintf(slash + 1, sizeof(menuIni) - (slash + 1 - menuIni), "menu.ini");
        GetPrivateProfileStringA("Menu", "Language", "EN", lang, sizeof(lang), menuIni);
        snprintf(text, sizeof(text), "%s", _stricmp(lang, "RU") == 0 ? "(Пусто)" : "(Empty)");
    }
    c.emptyText[0] = L' ';
    MultiByteToWideChar(CP_UTF8, 0, text, -1, c.emptyText + 1, 47);
    c.dumpKey = IniKey("Debug", "DumpKey", 0);
    c.dumpRadius = Clamp(IniFloat("Debug", "DumpRadius", 1500.0f), 100.0f, 100000.0f);
    g_config = c;
    if (first)
        g_autoActive = c.autoActive;
    Log("config: quick=%d key=0x%02X ground=%d chests=%d | auto=%d active=%d key=0x%02X r=%.0f bodies=%d chests=%d | "
        "cone=%d %.0f quest=%d | take gold=%d ammo=%d weap=%d armor=%d cons=%d magic=%d written=%d misc=%d other=%d | "
        "effect=%d %s h=%.0f size=%.2f sound=%d | dump=0x%02X r=%.0f fxtest=0x%02X",
        c.quickLoot, c.quickKey, c.quickGround, c.quickChests, c.autoLoot, g_autoActive, c.autoKey, c.autoRadius,
        c.autoBodies, c.autoChests, c.autoCone, c.coneAngle, c.autoQuest, c.takeGold, c.takeAmmo, c.takeWeapons, c.takeArmor,
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

// Loose item that can be taken: not hanging in a slot (torches in holders etc.), not an arrow in flight.
// Theft (WouldBeTheft) and quest items (QuestItemOk) are checked separately.
static bool IsGroundItem(Entity e)
{
    return g_game.isItem(&e) && g_game.itemValid(&e) && !g_game.attachedToSlot(&e) && !IsFlying(e);
}

// QuickLoot of something that would be theft: allowed, but first the game's own theft check runs, as when picking
// it up by hand - NPCs who see it react. False (nothing may be taken) when this game version has no broadcast.
static bool TheftAllowed(const Entity& e, const char* name)
{
    if (!WouldBeTheft(e))
        return true;
    if (!ReportTheft(e)) {
        Log("quickloot: %s would be theft, left alone (unknown game version)", name);
        return false;
    }
    Log("quickloot: %s is theft (owner %d, security %d), reported to the NPCs around", name, HasOwner(e),
        SecurityLevel(e));
    return true;
}

// A quest item is taken only when the game says the player may take it now (special quest objects like the skulls
// have their own CanInteract scripts that decide this).
static bool QuestItemOk(Entity& player, Entity e)
{
    if (!g_game.itemMission(&e))
        return true;
    Entity other = e;
    if (g_game.canInteractWith(&player, &other))
        return true;
    char name[96];
    Log("quest item %s: the game does not let it be taken now", EntityName(e, name, sizeof(name)));
    return false;
}

// Why QuickLoot did not take the focus, for the log.
static const char* WhyNot(const Entity& e, char* buf, size_t size)
{
    bool item = g_game.isItem(&e), ps = item && g_game.itemValid(&e), inv = g_game.invValid(&e);
    bool lock = g_game.lockValid(&e), npc = g_game.isNpc(&e);
    snprintf(buf, size, "item=%d itemPS=%d mission=%d slot=%d flying=%d owner=%d dropped=%d npc=%d dead=%d inv=%d "
             "stacks=%d lock=%d status=%d security=%d", item, ps, ps && g_game.itemMission(&e),
             item && g_game.attachedToSlot(const_cast<Entity*>(&e)), IsFlying(e), HasOwner(e),
             ps && g_game.itemDropped(&e), npc, npc && g_game.isDead(&e), inv, inv ? g_game.invStackCount(&e) : -1,
             lock, lock ? g_game.lockStatus(&e) : -1, SecurityLevel(e));
    return buf;
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

// Sparkle colours by what was picked up (RGB 0..1; black = the effect's own colours). Ordered by importance: a
// chest or body sparkles in the colour of the most important thing taken from it.
enum SparkleKind { SparkleOwn, SparkleConsumable, SparkleMagic, SparkleArmor, SparkleWeapon, SparkleQuest, SparkleKinds };
static const float kSparkleColors[SparkleKinds][3] = {
    {0, 0, 0},          // own: gold coins, written, misc, everything else
    {0.3f, 1.0f, 0.3f}, // potions, food, plants
    {0.8f, 0.3f, 1.0f}, // scrolls, runes
    {0.5f, 0.7f, 1.0f}, // armor
    {1.0f, 0.3f, 0.2f}, // weapons, ammo
    {1.0f, 0.8f, 0.2f}, // quest items
};
static const char* const kSparkleNames[SparkleKinds] = {"own", "consumable", "magic", "armor", "weapon", "quest"};
constexpr float kContainerSparkleSize = 1.25f; // chests and bodies: a bigger flash

static SparkleKind ItemSparkle(const Entity& e);

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

static SparkleKind ItemSparkle(const Entity& e)
{
    char name[96];
    EntityName(e, name, sizeof(name));
    if (g_game.itemMission(&e))
        return SparkleQuest;
    if (NameStarts(name, "It_Arrow") || NameStarts(name, "It_Bolt"))
        return SparkleWeapon;
    switch (g_game.itemCategory(&e)) {
    case ItemCatWeapon: return SparkleWeapon;
    case ItemCatArmor: return SparkleArmor;
    case ItemCatConsumable: return SparkleConsumable;
    case ItemCatMagic: return SparkleMagic;
    default: return SparkleOwn;
    }
}

// The most important of what the loot will take from a container (call before looting it).
static SparkleKind ContainerSparkle(Entity& c, bool quest)
{
    SparkleKind best = SparkleOwn;
    for (int i = 0, n = g_game.invStackCount(&c); i < n && best < SparkleQuest; ++i) {
        Entity tpl;
        g_game.invTemplateItem(&c, &tpl, i);
        if (!tpl || !g_game.itemValid(&tpl))
            continue;
        bool mission = g_game.invStackType(&c, i) == 0 && g_game.itemMission(&tpl);
        if (mission ? !quest : ScriptCall("InvFilter_Loot", c.e, nullptr, i) <= 0)
            continue;
        SparkleKind kind = ItemSparkle(tpl);
        if (kind > best)
            best = kind;
    }
    return best;
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
            "amount=%lu cat=%d dropped=%d flying=%d front=%d slot=%d lock=%d status=%d security=%d sight=%d "
            "y=%.0f",
            n.distance, name, npc, npc && g_game.isDead(&e), npc && g_game.isDown(&e), g_game.isItem(&e),
            g_game.isInteractObject(&e), HasOwner(e), inv, inv ? g_game.invStackCount(&e) : -1, item,
            item ? g_game.itemMission(&e) : 0, item ? g_game.itemAmount(&e) : 0, item ? g_game.itemCategory(&e) : -1,
            item ? g_game.itemDropped(&e) : 0, IsFlying(e), InFront(player, e, n.distance),
            item ? g_game.attachedToSlot(const_cast<Entity*>(&e)) : 0, lock, lock ? g_game.lockStatus(&e) : -1,
            SecurityLevel(e), LineOfSight(player, e), EntityPosition(e).y);
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

// Debug: plays the loot effect in each colour, one after another, at the focus target (or the player).
static void EffectTest()
{
    static int next;
    Entity player;
    if (!GameReady(&player))
        return;
    Entity target;
    g_game.worldFocus(g_game.world, &target);
    int kind = next++ % SparkleKinds;
    Vec3 at = EntityPosition(target ? target : player);
    bool started = PlayEffect(g_config.effectName, at, g_config.effectSize, !g_config.effectSound, kSparkleColors[kind]);
    Log("effect test: %s %s at %s (%.0f %.0f %.0f) started=%d", g_config.effectName, kSparkleNames[kind],
        target ? "focus" : "player", at.x, at.y, at.z, started);
}

// Dead NPC or monster with something on it. Knocked-out people are left alone (looting them is theft).
static bool IsDeadBody(const Entity& e)
{
    return g_game.isNpc(&e) && g_game.isDead(&e) && g_game.invValid(&e) && g_game.invStackCount(&e) > 0;
}

// Knocked out (non-lethal fight) and carrying something. Only QuickLoot takes from them, and only when the game
// itself lets the player interact (the same check the focus makes before LMB opens the loot window).
static bool IsDownBody(Entity& player, const Entity& e)
{
    if (!g_game.isNpc(&e) || g_game.isDead(&e) || !g_game.isDown(&e) || !g_game.invValid(&e) ||
        g_game.invStackCount(&e) <= 0)
        return false;
    Entity other = e;
    return g_game.canInteractWith(&player, &other);
}

// Chests seen locked. Once the player unlocks one (key, lockpick), it is theirs to open: AutoLoot used to empty
// it the moment the lock opened, right under the open loot window.
static std::unordered_map<void*, bool> g_wasLocked;

static bool IsLocked(const Entity& e)
{
    return g_game.lockValid(&e) && g_game.lockStatus(&e) != 1;
}

// Container that isn't an NPC or item, has stacks and is not locked. PSLock status 1 = open (checked in game).
// Whose it is and where it stands (theft) is checked separately.
static bool IsOpenChest(const Entity& e)
{
    if (g_game.isNpc(&e) || g_game.isItem(&e) || !g_game.invValid(&e) || g_game.invStackCount(&e) <= 0)
        return false;
    return !g_game.lockValid(&e) || g_game.lockStatus(&e) == 1;
}

// Something the loot would take: what the game's loot filter passes, or a quest stack.
static bool HasLoot(Entity& e)
{
    for (int i = 0, n = g_game.invStackCount(&e); i < n; ++i) {
        if (ScriptCall("InvFilter_Loot", e.e, nullptr, i) > 0)
            return true;
        Entity tpl;
        g_game.invTemplateItem(&e, &tpl, i);
        if (g_game.invStackType(&e, i) == 0 && tpl && g_game.itemValid(&tpl) && g_game.itemMission(&tpl))
            return true;
    }
    return false;
}

// Focus name suffix: " (Empty)" for a chest or dead body with nothing to take.
static const wchar_t* EmptySuffix()
{
    if (!g_config.emptyMark || !GameApiInit())
        return nullptr;
    Entity e;
    g_game.worldFocus(g_game.world, &e);
    if (!e || g_game.isItem(&e) || !g_game.invValid(&e) || (g_game.isNpc(&e) && !g_game.isDead(&e)))
        return nullptr;
    return HasLoot(e) ? nullptr : g_config.emptyText;
}

// The configured effect at a looted spot (for items the position is taken before they leave the world).
static void Sparkle(Vec3 at, SparkleKind kind = SparkleOwn, float size = 1.0f)
{
    if (!g_config.effect || !g_config.effectName[0])
        return;
    at.y += g_config.effectHeight; // the engine is Y-up
    if (!PlayEffect(g_config.effectName, at, g_config.effectSize * size, !g_config.effectSound, kSparkleColors[kind]))
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
    SparkleKind kind = ItemSparkle(e);
    int slot = g_game.invInsertStack(&player, &e); // the world item is gone after this
    Sparkle(at, kind);
    Log("%s: %s x%lu (category %d) -> stack %d", who, name, amount, category, slot);
}

// repeat = the key is held: the same as a press, but silent about what can't be looted (no log spam).
static void QuickLoot(bool repeat)
{
    Entity player;
    if (!g_config.quickLoot || !GameReady(&player))
        return;
    Entity target;
    g_game.worldFocus(g_game.world, &target);
    if (!target)
        return;
    if (int mode = CombatMode(player)) { // weapon drawn: no looting in a fight, the key is Action 2 there
        if (!repeat)
            Log("quickloot: skipped, combat mode %d", mode);
        return;
    }
    char name[96], why[256];
    EntityName(target, name, sizeof(name));
    if (IsGroundItem(target)) {
        if (!g_config.quickGround)
            return;
        if (QuestItemOk(player, target) && TheftAllowed(target, name))
            PickUp(player, target, "quickloot");
        return;
    }
    bool down = IsDownBody(player, target);
    bool body = down || IsDeadBody(target), chest = !body && g_config.quickChests && IsOpenChest(target);
    // A locked chest nobody owns, and the player has its key: open it with the key, like the game would.
    if (!body && !chest && g_config.quickChests && IsLocked(target) && !g_game.isNpc(&target) && !HasOwner(target) &&
        g_game.invValid(&target) && UnlockWithKey(player, target)) {
        Log("quickloot: %s unlocked with the key", name);
        chest = IsOpenChest(target);
    }
    if (!body && !chest) {
        if (!repeat)
            Log("quickloot: focus %s is not lootable (%s)", name, WhyNot(target, why, sizeof(why)));
        return;
    }
    if (repeat && !HasLoot(target))
        return; // already emptied by the first press
    if (chest && !TheftAllowed(target, name))
        return;
    SparkleKind kind = ContainerSparkle(target, true);
    int64_t moved = ScriptAutoLoot(player, target), quest = LootQuestStacks(player, target);
    if (moved + quest > 0)
        Sparkle(EntityPosition(target), kind, kContainerSparkleSize);
    Log("quickloot: %s %s -> %lld stacks, %lld quest", down ? "knocked-out" : body ? "body" : "chest", name, moved,
        quest);
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
    bool plunder = false; // GeneratePlunder added stacks
    int plunderTries = 0; // GeneratePlunder asked (a few times until it gives something)
    int tries = 0;
    DWORD next = 0;
    DWORD firstDead = 0; // when it was first seen dead, for the delay in the log
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
    if (g_wasLocked.size() > 5000)
        g_wasLocked.clear();
    for (const Nearby& n : CollectNearby(player, g_config.autoRadius)) {
        if (IsLocked(n.e) && !g_game.isNpc(&n.e))
            g_wasLocked[n.e.e] = true; // remembered even outside the cone: the player may turn to it later
        if (!InFront(player, n.e, n.distance))
            continue;
        if (IsGroundItem(n.e)) {
            if (g_game.itemMission(&n.e) && !g_config.autoQuest)
                continue;
            if (Wanted(n.e) && Resting(n.e, now) && !WouldBeTheft(n.e) && LineOfSight(player, n.e) &&
                QuestItemOk(player, n.e))
                PickUp(player, n.e, "autoloot");
            continue;
        }
        Entity container = n.e;
        // Animals and monsters get their plunder (meat, skins...) only when first focused: ask the game the same
        // question the focus asks, a few times until it gives something (it may not be ready right away). Bodies
        // already carry stacks before that (claws, natural weapons), so the stack count says nothing.
        if (g_config.autoBodies && g_game.isNpc(&container) && g_game.isDead(&container) &&
            g_game.invValid(&container)) {
            LootTry& t = g_looted[container.e];
            if (!t.firstDead)
                t.firstDead = now;
            if (!t.plunder && t.plunderTries < 5) {
                ++t.plunderTries;
                if ((t.plunder = GeneratePlunder(player, container))) {
                    char name[96];
                    Log("autoloot: plunder created for %s (ask %d, %lu ms after it was seen dead)",
                        EntityName(container, name, sizeof(name)), t.plunderTries, now - t.firstDead);
                }
            }
        }
        bool body = g_config.autoBodies && (IsDeadBody(n.e) || (g_config.autoDown && IsDownBody(player, n.e)));
        bool chest = !body && g_config.autoChests && !g_wasLocked.count(n.e.e) && IsOpenChest(n.e) &&
                     !WouldBeTheft(n.e);
        if (!body && !chest)
            continue;
        LootTry& t = g_looted[n.e.e];
        if (t.done || now < t.next || !LineOfSight(player, n.e))
            continue;
        char name[96];
        EntityName(n.e, name, sizeof(name));
        // Not sure yet whether the game's AutoLoot script gives anything while the player's weapon is drawn: when
        // it doesn't, the same steps are done by hand.
        SparkleKind kind = ContainerSparkle(container, g_config.autoQuest);
        int64_t moved = ScriptAutoLoot(player, container);
        const char* how = "script";
        if (moved <= 0 && combat) {
            moved = LootContainer(player, container);
            how = "by hand, weapon drawn";
        }
        if (g_config.autoQuest)
            moved += LootQuestStacks(player, container);
        if (moved > 0)
            Sparkle(EntityPosition(n.e), kind, kContainerSparkleSize);
        // Nothing taken: try again in a second, a few times, then leave what the game won't give.
        t.done = moved > 0 || ++t.tries >= 8;
        t.next = now + 1000;
        Log("autoloot: %s %s -> %lld stacks (%s, try %d, %lu ms after it was seen dead)", body ? "body" : "chest",
            name, moved, how, t.tries + (moved > 0), body && t.firstDead ? now - t.firstDead : 0);
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
    /*10 */ {HubCheckbox, "AutoLoot", "KnockedOut", "Also people knocked out", "И вырубленных в драке", 1, 0, 0, 0,
             0, nullptr, 9, "Knocked out in a non-lethal fight, only when the game lets you search them",
             "Побеждённых в драке не насмерть - только когда игра сама даёт их обыскать"},
    /*11 */ {HubCheckbox, "AutoLoot", "Chests", "Also empty open chests", "Опустошать открытые сундуки", 0, 0, 0, 0,
             0, nullptr, 6, "Only chests that belong to nobody and are not locked",
             "Только ничьи и не запертые сундуки"},
    /*12 */ {HubCheckbox, "AutoLoot", "Active", "On at game start", "Включён при запуске игры", 0, 0, 0, 0, 0,
             nullptr, 6},
    /*13 */ {HubCheckbox, "AutoLoot", "Cone", "Only in front of you", "Только впереди", 1, 0, 0, 0, 0, nullptr, 6,
             "A cone around where the camera looks; off = the whole circle. Things at your feet are always taken.",
             "Конус по направлению камеры; выключено = весь круг. То, что прямо под ногами, берётся всегда."},
    /*14 */ {HubSlider, "AutoLoot", "ConeAngle", "Cone width", "Ширина конуса", 120, 30, 360, 5, 0, "%.0f°", 13},
    /*15 */ {HubCheckbox, "AutoLoot", "QuestItems", "Also quest items", "Подбирать и квестовые предметы", 0, 0, 0, 0,
             0, nullptr, 6, "On the ground, in bodies and chests. Chests you unlocked yourself are left to you.",
             "С земли, из трупов и сундуков. Сундуки, которые ты отпер сам, автолут не трогает."},
    /*16 */ {HubCheckbox, "Indicator", "Show", "Hand in the corner while it is on", "Рука в углу, пока он включён", 1,
             0, 0, 0, 0, nullptr, 6, "The status icon in the top-left corner (needs the Risen Mods menu)",
             "Значок в левом верхнем углу (нужно меню Risen Mods)"},
    /*17 */ {HubHeader, nullptr, nullptr, "What AutoLoot picks up", "Что подбирает автолут"},
    /*18 */ {HubNote, nullptr, nullptr, "Items on the ground. QuickLoot always takes what you look at.",
             "Предметы на земле. Быстрый лут всегда берёт то, на что смотришь."},
    /*19 */ {HubCheckbox, "Filter", "Gold", "Gold", "Золото", 1, 0, 0, 0, 0, nullptr, 6},
    /*20 */ {HubCheckbox, "Filter", "Ammo", "Arrows and bolts", "Стрелы и болты", 1, 0, 0, 0, 0, nullptr, 6},
    /*21 */ {HubCheckbox, "Filter", "Weapons", "Weapons", "Оружие", 1, 0, 0, 0, 0, nullptr, 6},
    /*22 */ {HubCheckbox, "Filter", "Armor", "Armour", "Броня", 1, 0, 0, 0, 0, nullptr, 6},
    /*23 */ {HubCheckbox, "Filter", "Consumables", "Potions, food, plants", "Зелья, еда, растения", 1, 0, 0, 0, 0,
             nullptr, 6},
    /*24 */ {HubCheckbox, "Filter", "Magic", "Scrolls and runes", "Свитки и руны", 1, 0, 0, 0, 0, nullptr, 6},
    /*25 */ {HubCheckbox, "Filter", "Written", "Letters, books, maps", "Письма, книги, карты", 1, 0, 0, 0, 0,
             nullptr, 6},
    /*26 */ {HubCheckbox, "Filter", "Misc", "Misc (trophies, ore, tools...)", "Разное (трофеи, руда, инструменты...)",
             1, 0, 0, 0, 0, nullptr, 6},
    /*27 */ {HubCheckbox, "Filter", "Other", "Everything else", "Всё остальное", 1, 0, 0, 0, 0, nullptr, 6},
    /*28 */ {HubHeader, nullptr, nullptr, "Effect", "Эффект"},
    /*29 */ {HubCheckbox, "Effect", "Enabled", "Sparkle where something was looted", "Искра там, где что-то залутано",
             1, 0, 0, 0, 0, nullptr, -1, "A short game effect at the item, body or chest",
             "Короткий игровой эффект у предмета, трупа или сундука"},
    /*30 */ {HubSlider, "Effect", "Size", "Size", "Размер", 0.15f, 0.1f, 2.0f, 0.05f, 0, "%.2f", 29},
    /*31 */ {HubCheckbox, "Effect", "Sound", "Effect sound", "Звук эффекта", 0, 0, 0, 0, 0, nullptr, 29},
    /*32 */ {HubHeader, nullptr, nullptr, "Empty", "Пустые"},
    /*33 */ {HubCheckbox, "Empty", "Mark", "Mark empty chests and bodies", "Помечать пустые сундуки и трупы", 1, 0,
             0, 0, 0, nullptr, -1, "\"(Empty)\" after the name of what you look at, when there is nothing to take",
             "\"(Пусто)\" после названия того, на что смотришь, если там нечего брать"},
};

static void OnMenuChanged()
{
    ConfigLoad(false);
}

static HubPage g_page = {"autoloot", 20, "AutoLoot", "Автолут", nullptr, kItems, sizeof(kItems) / sizeof(kItems[0]),
                         OnMenuChanged};

static bool StatusQuery(char*, int)
{
    return g_config.autoLoot && g_autoActive && g_config.indicator;
}

static const HubStatus kStatus = {"autoloot", 20, HubIconHand, StatusQuery};

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LOOT_QUICK:
        QuickLoot(wp != 0);
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

constexpr DWORD kQuickRepeatMs = 250;

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
    InstallFocusNameHook(EmptySuffix); // still in the main menu: no focus name is being built
    if (const HubApi* hub = HubGet()) {
        hub->registerPage(&g_page);
        hub->registerStatus(&kStatus);
    }
    Log("autoloot: ready (%s)", HubGet() ? "menu found" : "no menu.dll: ini only");

    KeyEdge quick, toggle, dump, fxTest;
    DWORD quickLast = 0; // last QuickLoot while the key is held
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool focused = GetForegroundWindow() == hwnd;
        bool busy = HubBusy(); // the menu is open: its clicks and key rebinds are not ours
        // Held: again every kQuickRepeatMs, whatever the focus is by then (one item after another).
        if (quick.Pressed(g_config.quickKey, focused) && !busy) {
            PostMessageW(hwnd, WM_LOOT_QUICK, 0, 0);
            quickLast = GetTickCount();
        } else if (quick.wasDown && !busy && GetTickCount() - quickLast >= kQuickRepeatMs) {
            PostMessageW(hwnd, WM_LOOT_QUICK, 1, 0);
            quickLast = GetTickCount();
        }
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
        Log("AutoLoot 0.8.1");
        g_page.iniPath = IniPath(kDefaultIni);
        ConfigLoad(true);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
