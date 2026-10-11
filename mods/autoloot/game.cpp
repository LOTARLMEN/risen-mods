#include "game.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "common.h"

GameApi g_game;

namespace {

bool g_missing;

template <typename T>
void Resolve(HMODULE mod, const char* modName, T& fn, const char* name)
{
    fn = mod ? reinterpret_cast<T>(GetProcAddress(mod, name)) : nullptr;
    if (!fn) {
        Log("game api: %s!%s not found", modName, name);
        g_missing = true;
    }
}

} // namespace

bool GameApiInit()
{
    static int state; // 0 not tried, 1 ok, -1 failed for good
    if (state)
        return state > 0;
    HMODULE script = GetModuleHandleA("Script.dll");
    if (!script)
        return false; // not loaded yet: try again later
    HMODULE engine = GetModuleHandleA("Engine.dll");
    HMODULE base = GetModuleHandleA("SharedBase.dll");
    GameApi& g = g_game;

    Resolve(script, "Script", g.getPlayer, "?GetPlayer@Entity@@SA?AV1@XZ");
    Resolve(script, "Script", g.updateEntityLists, "?UpdateEntityLists@Entity@@KAXXZ");
    Resolve(script, "Script", g.entities, "?ms_arrEntities@Entity@@2V?$bTObjArray@VEntity@@@@A");
    Resolve(script, "Script", g.isDead, "?IsDead@Entity@@QEBA_NXZ");
    Resolve(script, "Script", g.isDown, "?IsDown@Entity@@QEBA_NXZ");
    Resolve(script, "Script", g.isNpc, "?IsNPC@Entity@@QEBA_NXZ");
    Resolve(script, "Script", g.isItem, "?IsItem@Entity@@QEBA_NXZ");
    Resolve(script, "Script", g.isInteractObject, "?IsInteractObject@Entity@@QEBA_NXZ");
    Resolve(script, "Script", g.distanceTo, "?GetDistanceTo@Entity@@QEBAMAEBV1@@Z");
    Resolve(script, "Script", g.getOwner, "?GetOwner@Entity@@QEBA?AV1@XZ");
    Resolve(script, "Script", g.getName, "?GetName@Entity@@QEBA?AVbCString@@XZ");
    Resolve(script, "Script", g.isDialogCamera, "?IsDialogCameraActive@Entity@@SA_NXZ");
    Resolve(script, "Script", g.invValid, "?IsValid@PSInventory@@QEBA_NXZ");
    Resolve(script, "Script", g.invStackCount, "?GetStackCount@PSInventory@@QEBAHXZ");
    Resolve(script, "Script", g.invInsertStack, "?InsertStack@PSInventory@@QEAAHAEBVEntity@@@Z");
    Resolve(script, "Script", g.itemValid, "?IsValid@PSItem@@QEBA_NXZ");
    Resolve(script, "Script", g.itemMission, "?IsMissionItem@PSItem@@QEBA_NXZ");
    Resolve(script, "Script", g.itemAmount, "?GetAmount@PSItem@@QEBAKXZ");
    Resolve(script, "Script", g.lockValid, "?IsValid@PSLock@@QEBA_NXZ");
    Resolve(script, "Script", g.lockStatus, "??BPropertyStatus@PSLock@@QEBA?BW4gELockStatus@@XZ");
    Resolve(script, "Script", g.combatValid, "?IsValid@PSCombatSystem@@QEBA_NXZ");
    Resolve(script, "Script", g.combatMode, "?GetCombatMode@PSCombatSystem@@QEBA?AW4gECombatMode@@XZ");
    Resolve(script, "Script", g.gui2, "gui2");
    Resolve(script, "Script", g.world, "world");
    Resolve(script, "Script", g.worldFocus, "?GetFocus@World@@QEBA?AVEntity@@XZ");
    Resolve(script, "Script", g.printItemLog, "?PrintItemLog@GUI2@@QEAAXVEntity@@H@Z");
    Resolve(script, "Script", g.attachedToSlot, "?IsAttachedToSlot@PSInteraction@@QEAA_NXZ");
    Resolve(base, "SharedBase", g.stringNew, "??0bCString@@QEAA@PEBD@Z");
    Resolve(base, "SharedBase", g.stringText, "?GetText@bCString@@QEBAPEBDXZ");
    Resolve(base, "SharedBase", g.stringFree, "??1bCString@@QEAA@XZ");
    Resolve(engine, "Engine", g.proxyNew, "??0eCScriptProxyScript@@QEAA@AEBVbCString@@@Z");
    Resolve(engine, "Engine", g.proxyCall, "??ReCScriptProxyScript@@QEBA_JPEAVeCEntity@@0_J@Z");

    state = g_missing ? -1 : 1;
    Log("game api: %s", state > 0 ? "ready" : "INCOMPLETE (game updated?), loot features disabled");
    return state > 0;
}

Entity Player()
{
    Entity p;
    g_game.getPlayer(&p);
    return p;
}

const char* EntityName(const Entity& e, char* buf, size_t size)
{
    alignas(8) char str[16] = {};
    g_game.getName(&e, str);
    const char* text = g_game.stringText(str);
    snprintf(buf, size, "%s", text ? text : "?");
    g_game.stringFree(str);
    return buf;
}

bool HasOwner(const Entity& e)
{
    Entity owner;
    g_game.getOwner(&e, &owner);
    return owner.e != nullptr;
}

int64_t ScriptAutoLoot(const Entity& receiver, const Entity& container)
{
    // eCScriptProxyScript is { vftable, eSScriptBase* } (16 bytes); built once, never destroyed (lives as long
    // as the game). The bCString is only needed by the constructor's name lookup.
    alignas(16) static char proxy[64];
    static bool built;
    if (!built) {
        alignas(8) char name[16] = {};
        g_game.stringNew(name, "AutoLoot");
        g_game.proxyNew(proxy, name);
        g_game.stringFree(name);
        built = true;
    }
    return g_game.proxyCall(proxy, receiver.e, container.e, 0);
}
