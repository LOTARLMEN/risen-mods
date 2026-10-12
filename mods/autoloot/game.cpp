#include "game.h"

#include <windows.h>
#include <cstdio>
#include <cmath>
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
    Resolve(script, "Script", g.getPosition, "?GetPosition@Entity@@QEBA?AVbCVector@@XZ");
    Resolve(script, "Script", g.startEffect, "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVbCVector@@@Z");
    Resolve(script, "Script", g.existsEffect, "?ExistsEffect@EffectSystem@@SA_NAEBVbCString@@@Z");
    Resolve(script, "Script", g.invValid, "?IsValid@PSInventory@@QEBA_NXZ");
    Resolve(script, "Script", g.invStackCount, "?GetStackCount@PSInventory@@QEBAHXZ");
    Resolve(script, "Script", g.invInsertStack, "?InsertStack@PSInventory@@QEAAHAEBVEntity@@@Z");
    Resolve(script, "Script", g.itemValid, "?IsValid@PSItem@@QEBA_NXZ");
    Resolve(script, "Script", g.itemMission, "?IsMissionItem@PSItem@@QEBA_NXZ");
    Resolve(script, "Script", g.itemAmount, "?GetAmount@PSItem@@QEBAKXZ");
    Resolve(script, "Script", g.itemCategory, "?GetCategory@PSItem@@QEBA?AW4gEItemCategory@@XZ");
    Resolve(script, "Script", g.itemDropped, "??BPropertyDropped@PSItem@@QEBA?B_NXZ");
    Resolve(script, "Script", g.projValid, "?IsValid@PSProjectile@@QEBA_NXZ");
    Resolve(script, "Script", g.projActive, "?IsActive@PSProjectile@@QEBA_NXZ");
    Resolve(script, "Script", g.atVector, "?GetAtVector@Entity@@QEBA?AVbCVector@@XZ");
    Resolve(script, "Script", g.cameraPose, "?GetCameraPose@Entity@@SA?AVbCMatrix@@XZ");
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

Vec3 EntityPosition(const Entity& e)
{
    Vec3 v{};
    g_game.getPosition(&e, &v);
    return v;
}

static bool Flat(float x, float z, float* outX, float* outZ)
{
    float len = sqrtf(x * x + z * z);
    if (len < 0.1f)
        return false;
    *outX = x / len;
    *outZ = z / len;
    return true;
}

bool ViewDirection(const Entity& player, float* x, float* z)
{
    alignas(16) float m[16] = {};
    g_game.cameraPose(m); // rows: right, up, at, position (Y-up)
    if (Flat(m[8], m[10], x, z))
        return true;
    Vec3 at{};
    g_game.atVector(&player, &at);
    return Flat(at.x, at.z, x, z);
}

bool EffectExists(const char* name)
{
    alignas(8) char str[16] = {};
    g_game.stringNew(str, name);
    bool ok = g_game.existsEffect(str);
    g_game.stringFree(str);
    return ok;
}

// Quiet copies of effects -------------------------------------------------------------------------------
// gCEffectSystem keeps the effect definitions (gCEffectCommandSequence2) in a hash map by lower-case name.
// For a quiet effect we deep-copy the definition once, drop its PlaySound commands, and while starting the effect
// swap the copy into the map node for the duration of the call (the started effect keeps a reference to the
// sequence it was started from). The original, used by the game's own spells, is never changed.
namespace {

struct EffectInternals {
    void* (*moduleInstance)();
    void* (*moduleSystem)(const void* module);
    void* (*systemItems)(void* system); // eTDocObjectMap: buckets at +0x10, bucket count at +0x18
    void* (*sequenceCreate)();
    int (*copyFrom)(void* self, const void* other, bool);
    int (*itemCount)(const void* seq);
    void* (*item)(const void* seq, int index);
    bool (*removeItem)(void* seq, int index);
    const void* playSoundVtable;
    Effect* (*startEffectMatrix)(Effect* ret, const void* name, const float* matrix);
};

EffectInternals g_fx;
int g_fxState; // 0 not tried, 1 ok, -1 missing

bool FxInit()
{
    if (g_fxState)
        return g_fxState > 0;
    HMODULE game = GetModuleHandleA("Game.dll"), script = GetModuleHandleA("Script.dll");
    HMODULE base = GetModuleHandleA("SharedBase.dll");
    g_missing = false;
    EffectInternals& f = g_fx;
    Resolve(game, "Game", f.moduleInstance, "?GetInstance@gCEffectModule@@SAAEAV1@XZ");
    Resolve(game, "Game", f.moduleSystem, "?GetSystem@gCEffectModule@@QEBAPEAVgCEffectSystem@@XZ");
    Resolve(game, "Game", f.systemItems,
            "?GetItems@gCEffectSystem@@QEAAAEAV?$eTDocObjectMap@VgCEffectCommandSequence2@@@@XZ");
    Resolve(game, "Game", f.sequenceCreate, "?CreateObject@gCEffectCommandSequence2@@SAPEAVbCObjectBase@@XZ");
    Resolve(base, "SharedBase", f.copyFrom, "?CopyFrom@bCObjectBase@@UEAA?AW4bEDoNotOverwriteMethod@@AEBV1@_N@Z");
    Resolve(game, "Game", f.itemCount, "?GetItemCount@gCEffectCommandSequence2@@QEBAHXZ");
    Resolve(game, "Game", f.item, "?GetItem@gCEffectCommandSequence2@@QEBAPEAVgCEffectCommand@@H@Z");
    Resolve(game, "Game", f.removeItem, "?RemoveItem@gCEffectCommandSequence2@@QEAA_NH@Z");
    Resolve(game, "Game", f.playSoundVtable, "??_7gCEffectCommandPlaySound@@6B@");
    Resolve(script, "Script", f.startEffectMatrix,
            "?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVbCMatrix@@@Z");
    g_fxState = g_missing ? -1 : 1;
    if (g_fxState < 0)
        Log("effect: quiet / scaled effects unavailable, plain effects only");
    return g_fxState > 0;
}

// Map node: { bCString key; gCEffectCommandSequence2* value; ...; node* next at +0x18 }
void** FindSequenceSlot(const char* name)
{
    void* system = g_fx.moduleSystem(g_fx.moduleInstance());
    if (!system)
        return nullptr;
    char* map = static_cast<char*>(g_fx.systemItems(system));
    char** buckets = *reinterpret_cast<char***>(map + 0x10);
    uint32_t count = *reinterpret_cast<uint32_t*>(map + 0x18);
    for (uint32_t i = 0; buckets && i < count; ++i)
        for (char* node = buckets[i]; node; node = *reinterpret_cast<char**>(node + 0x18)) {
            const char* key = g_game.stringText(node);
            if (key && _stricmp(key, name) == 0)
                return reinterpret_cast<void**>(node + 8);
        }
    return nullptr;
}

struct QuietCopy {
    char name[96];
    void* original;
    void* copy; // nullptr: could not be made, use the original
};

QuietCopy g_quiet[4];

// The quiet copy of the named effect, made on first use. nullptr when there is nothing to swap.
QuietCopy* Quiet(const char* name, void** slot)
{
    for (QuietCopy& q : g_quiet)
        if (q.name[0] && _stricmp(q.name, name) == 0 && q.original == *slot)
            return q.copy ? &q : nullptr;
    QuietCopy* q = &g_quiet[0];
    for (QuietCopy& c : g_quiet)
        if (!c.name[0]) {
            q = &c;
            break;
        }
    snprintf(q->name, sizeof(q->name), "%s", name);
    q->original = *slot;
    q->copy = g_fx.sequenceCreate(); // kept forever (one reference is ours)
    if (!q->copy) {
        Log("effect: could not copy %s", name);
        return nullptr;
    }
    g_fx.copyFrom(q->copy, q->original, false);
    int n = g_fx.itemCount(q->copy), removed = 0, shared = 0;
    for (int i = 0; i < n; ++i)
        if (g_fx.item(q->copy, i) == g_fx.item(q->original, i))
            ++shared;
    if (shared) { // a shallow copy: removing commands would touch the original's objects
        Log("effect: copy of %s shares %d commands, keeping the sound", name, shared);
        q->copy = nullptr;
        return nullptr;
    }
    for (int i = n - 1; i >= 0; --i) {
        void* cmd = g_fx.item(q->copy, i);
        if (cmd && *static_cast<void**>(cmd) == g_fx.playSoundVtable && g_fx.removeItem(q->copy, i))
            ++removed;
    }
    Log("effect: quiet copy of %s: %d commands, %d sounds removed", name, n, removed);
    return q;
}

} // namespace

bool PlayEffect(const char* name, const Vec3& position, float scale, bool quiet)
{
    alignas(8) char str[16] = {};
    g_game.stringNew(str, name);
    Effect fx;
    if (FxInit()) {
        // bCMatrix: 4x4 rows, translation in the last row
        float m[16] = {scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, position.x, position.y, position.z, 1};
        void** slot = quiet ? FindSequenceSlot(name) : nullptr;
        QuietCopy* q = slot ? Quiet(name, slot) : nullptr;
        if (q)
            *slot = q->copy;
        g_fx.startEffectMatrix(&fx, str, m);
        if (q)
            *slot = q->original;
    } else {
        g_game.startEffect(&fx, str, &position);
    }
    g_game.stringFree(str);
    return fx.e != nullptr;
}
