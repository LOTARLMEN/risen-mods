#include "game.h"

#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cstring>

#include "common.h"
#include "iat_patch.h"

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
    Resolve(script, "Script", g.invTemplateItem, "?GetTemplateItem@PSInventory@@QEBA?AVEntity@@H@Z");
    Resolve(script, "Script", g.invStackType, "?GetStackType@PSInventory@@QEBA?AW4gEStackType@@H@Z");
    Resolve(script, "Script", g.invAmount, "?GetAmount@PSInventory@@QEBAHH@Z");
    Resolve(script, "Script", g.invCategory, "?GetCategory@PSInventory@@QEBA?AW4gEItemCategory@@H@Z");
    Resolve(script, "Script", g.invTransfer, "?TransferAmountTo@PSInventory@@QEAA_NHHAEAVEntity@@@Z");
    Resolve(script, "Script", g.canInteractWith, "?CanInteractWith@PSInteraction@@QEAA_NVEntity@@@Z");
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
    Resolve(script, "Script", g.lockSetStatus, "??4PropertyStatus@PSLock@@QEAAAEAV01@AEBW4gELockStatus@@@Z");
    Resolve(script, "Script", g.lockKey, "?GetKey@PSLock@@QEBA?AVTemplate@@XZ");
    Resolve(script, "Script", g.invHasItems, "?HasItems@PSInventory@@QEBA_NAEBVTemplate@@H@Z");
    Resolve(script, "Script", g.templateValid, "?IsValid@Template@@QEBA_NXZ");
    Resolve(script, "Script", g.templateFree, "??1Template@@QEAA@XZ");
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

int64_t ScriptCall(const char* script, void* self, void* other, int64_t param)
{
    // eCScriptProxyScript is { vftable, eSScriptBase* } (16 bytes); built once per name, never destroyed (lives as
    // long as the game). The bCString is only needed by the constructor's name lookup.
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
        g_game.stringNew(name, script);
        g_game.proxyNew(p->data, name);
        g_game.stringFree(name);
        p->name = script;
    }
    return g_game.proxyCall(p->data, self, other, param);
}

int64_t ScriptAutoLoot(const Entity& receiver, const Entity& container)
{
    return ScriptCall("AutoLoot", receiver.e, container.e, 0);
}

bool GeneratePlunder(Entity& player, Entity& body)
{
    // The player asks about the body, like the focus does (verified in game: a vulture got its meat).
    int before = g_game.invStackCount(&body);
    Entity other = body;
    g_game.canInteractWith(&player, &other);
    return g_game.invStackCount(&body) != before;
}

int64_t LootContainer(Entity& receiver, Entity& container)
{
    // What Script_Game's AutoLoot does, step by step: last stack first, the game's loot filter decides (it skips
    // equipped / natural weapons and quest items), "+ item" line, then the transfer.
    int64_t moved = 0;
    for (int i = g_game.invStackCount(&container) - 1; i >= 0; --i) {
        if (ScriptCall("InvFilter_Loot", container.e, nullptr, i) <= 0)
            continue;
        int amount = g_game.invAmount(&container, i);
        Entity tpl;
        g_game.invTemplateItem(&container, &tpl, i);
        g_game.printItemLog(g_game.gui2, &tpl, amount);
        if (g_game.invTransfer(&container, i, amount, &receiver))
            ++moved;
    }
    return moved;
}

int64_t LootQuestStacks(Entity& receiver, Entity& container)
{
    int64_t moved = 0;
    for (int i = g_game.invStackCount(&container) - 1; i >= 0; --i) {
        if (g_game.invStackType(&container, i) != 0) // equipped and the like
            continue;
        Entity tpl;
        g_game.invTemplateItem(&container, &tpl, i);
        if (!tpl || !g_game.itemValid(&tpl) || !g_game.itemMission(&tpl))
            continue;
        int amount = g_game.invAmount(&container, i);
        g_game.printItemLog(g_game.gui2, &tpl, amount);
        if (g_game.invTransfer(&container, i, amount, &receiver))
            ++moved;
    }
    return moved;
}

bool UnlockWithKey(Entity& player, Entity& chest)
{
    alignas(16) char key[64] = {}; // Template, size unknown: room to spare
    g_game.lockKey(&chest, key);
    bool has = g_game.templateValid(key) && g_game.invHasItems(&player, key, 1);
    g_game.templateFree(key);
    if (!has)
        return false;
    int open = 1;
    g_game.lockSetStatus(&chest, &open);
    return g_game.lockStatus(&chest) == 1;
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

// Copies of effects (quiet / tinted) ------------------------------------------------------------------------
// gCEffectSystem keeps the effect definitions (gCEffectCommandSequence2) in a hash map by lower-case name.
// For a quiet or tinted effect we deep-copy the definition once, drop its PlaySound commands if asked, and while
// starting the effect swap the copy into the map node for the duration of the call (the started effect keeps a
// reference to the sequence it was started from). The original, used by the game's own spells, is never changed.
//
// Tint: the copy's gCEffectCommandSpawnEntity commands are remembered. Their OnStart (vtable slot, shared by all
// effects) is wrapped: while one of ours runs, the entity it spawns (gCEffectCommandEntity::Spawn ->
// eCEntity::PatchWithTemplate, Game.dll's Engine import) gets its particle colour scale recoloured after it was
// copied from the template. The template itself is not touched.
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
    void** spawnVtable;
    void* spawnOnStart;
    Effect* (*startEffectMatrix)(Effect* ret, const void* name, const float* matrix);
    bool (*patchWithTemplate)(void* self, const void* tpl, bool);
    void* (*propertySet)(const void* entity, int type);
    void* (*firstChild)(void* node);
    void* (*nextNeighbour)(void* node);
};

EffectInternals g_fx;
int g_fxState; // 0 not tried, 1 ok, -1 missing
bool g_tintHooks;

// eCParticle_PS (Engine.dll, Rev. 2045): property set type, UseColorScale, eCColorScale {items, count}.
constexpr int kParticlePs = 0x49;
constexpr size_t kUseColorScale = 0x290, kColorScaleItems = 0x4A8 + 8, kColorScaleCount = 0x4A8 + 0x10;
struct ColorKey {
    float t, r, g, b, a;
};

struct EffectCopy {
    char name[96];
    void* original;
    void* copy; // nullptr: could not be made, use the original
    bool quiet;
    float tint[3]; // 0 0 0: own colours
    void* spawns[8]; // the copy's spawn commands (tinted copies only)
};

EffectCopy g_copies[16];
const EffectCopy* g_spawning; // set while one of our spawn commands runs (game thread)

void TintEntity(void* entity, const float* tint, int depth, int* tinted)
{
    if (!entity || depth > 8)
        return;
    if (char* ps = static_cast<char*>(g_fx.propertySet(entity, kParticlePs))) {
        auto* keys = *reinterpret_cast<ColorKey**>(ps + kColorScaleItems);
        int count = *reinterpret_cast<int*>(ps + kColorScaleCount);
        static int logged;
        if (logged < 6) {
            ++logged;
            Log("effect tint: particles use=%d keys=%d first=(%.2f %.2f %.2f %.2f)", ps[kUseColorScale], count,
                count > 0 && keys ? keys[0].r : -1.0f, count > 0 && keys ? keys[0].g : -1.0f,
                count > 0 && keys ? keys[0].b : -1.0f, count > 0 && keys ? keys[0].a : -1.0f);
        }
        for (int i = 0; keys && i < count && i < 64; ++i) {
            ColorKey& k = keys[i];
            float v = k.r > k.g ? (k.r > k.b ? k.r : k.b) : (k.g > k.b ? k.g : k.b); // brightness
            k.r = tint[0] * v;
            k.g = tint[1] * v;
            k.b = tint[2] * v;
        }
        if (keys && count > 0)
            ++*tinted;
    }
    for (void* child = g_fx.firstChild(entity); child; child = g_fx.nextNeighbour(child))
        TintEntity(child, tint, depth + 1, tinted);
}

bool HookedPatchWithTemplate(void* self, const void* tpl, bool flag)
{
    bool ok = g_fx.patchWithTemplate(self, tpl, flag);
    if (ok && g_spawning) {
        int tinted = 0;
        TintEntity(self, g_spawning->tint, 0, &tinted);
        static int logged;
        if (logged < 6 || !tinted) {
            ++logged;
            Log("effect tint: %s (%.2f %.2f %.2f) -> %d particle sets", g_spawning->name, g_spawning->tint[0],
                g_spawning->tint[1], g_spawning->tint[2], tinted);
        }
    }
    return ok;
}

using OnStartFn = void (*)(const void* self, void* instance);

void HookedSpawnOnStart(const void* self, void* instance)
{
    const EffectCopy* mine = nullptr;
    for (const EffectCopy& c : g_copies)
        for (void* s : c.spawns)
            if (s && s == self)
                mine = &c;
    const EffectCopy* outer = g_spawning;
    g_spawning = mine;
    reinterpret_cast<OnStartFn>(g_fx.spawnOnStart)(self, instance);
    g_spawning = outer;
}

// Engine.dll's PatchWithTemplate import in Game.dll + the OnStart slot of gCEffectCommandSpawnEntity's vtable.
bool InstallTintHooks()
{
    HMODULE game = GetModuleHandleA("Game.dll");
    void** vt = g_fx.spawnVtable;
    int slot = -1;
    for (int i = 0; i < 64 && slot < 0; ++i)
        if (vt[i] == g_fx.spawnOnStart)
            slot = i;
    if (slot < 0) {
        Log("effect: spawn OnStart not in the vtable, no colours");
        return false;
    }
    if (!PatchImport(game, "Engine.dll", "?PatchWithTemplate@eCEntity@@QEAA_NAEBVeCTemplateEntity@@_N@Z",
                     reinterpret_cast<void*>(&HookedPatchWithTemplate))) {
        Log("effect: PatchWithTemplate import not found, no colours");
        return false;
    }
    DWORD old;
    VirtualProtect(&vt[slot], sizeof(void*), PAGE_READWRITE, &old);
    vt[slot] = reinterpret_cast<void*>(&HookedSpawnOnStart);
    VirtualProtect(&vt[slot], sizeof(void*), old, &old);
    Log("effect: colour hooks installed (OnStart slot %d)", slot);
    return true;
}

bool FxInit()
{
    if (g_fxState)
        return g_fxState > 0;
    HMODULE game = GetModuleHandleA("Game.dll"), script = GetModuleHandleA("Script.dll");
    HMODULE base = GetModuleHandleA("SharedBase.dll"), engine = GetModuleHandleA("Engine.dll");
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
    if (g_fxState < 0) {
        Log("effect: quiet / scaled effects unavailable, plain effects only");
        return false;
    }
    // Colours are optional: without them effects keep their own.
    g_missing = false;
    Resolve(game, "Game", f.spawnVtable, "??_7gCEffectCommandSpawnEntity@@6B@");
    Resolve(game, "Game", f.spawnOnStart, "?OnStart@gCEffectCommandSpawnEntity@@UEBAXAEAUgSEffectCommandInstance@@@Z");
    Resolve(engine, "Engine", f.patchWithTemplate, "?PatchWithTemplate@eCEntity@@QEAA_NAEBVeCTemplateEntity@@_N@Z");
    Resolve(engine, "Engine", f.propertySet,
            "?GetPropertySet@eCEntity@@QEBAPEBVeCEntityPropertySet@@W4eEPropertySetType@@@Z");
    Resolve(engine, "Engine", f.firstChild, "?GetFirstChild@eCNode@@QEAAPEAV1@XZ");
    Resolve(engine, "Engine", f.nextNeighbour, "?GetNextNeighbour@eCNode@@QEAAPEAV1@XZ");
    g_tintHooks = !g_missing && InstallTintHooks();
    return true;
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

bool SameTint(const float* a, const float* b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

// The copy of the named effect for these settings, made on first use. nullptr when there is nothing to swap.
EffectCopy* Copy(const char* name, void** slot, bool quiet, const float* tint)
{
    for (EffectCopy& c : g_copies)
        if (c.name[0] && _stricmp(c.name, name) == 0 && c.original == *slot && c.quiet == quiet &&
            SameTint(c.tint, tint))
            return c.copy ? &c : nullptr;
    EffectCopy* c = &g_copies[0];
    for (EffectCopy& free : g_copies)
        if (!free.name[0]) {
            c = &free;
            break;
        }
    *c = EffectCopy{}; // all taken (the ini was edited many times): the first is reused, its old copy leaked
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->original = *slot;
    c->quiet = quiet;
    memcpy(c->tint, tint, sizeof(c->tint));
    c->copy = g_fx.sequenceCreate(); // kept forever (one reference is ours)
    if (!c->copy) {
        Log("effect: could not copy %s", name);
        return nullptr;
    }
    g_fx.copyFrom(c->copy, c->original, false);
    int n = g_fx.itemCount(c->copy), removed = 0, shared = 0, spawns = 0;
    for (int i = 0; i < n; ++i)
        if (g_fx.item(c->copy, i) == g_fx.item(c->original, i))
            ++shared;
    if (shared) { // a shallow copy: removing commands would touch the original's objects
        Log("effect: copy of %s shares %d commands, using the original", name, shared);
        c->copy = nullptr;
        return nullptr;
    }
    bool tinted = tint[0] || tint[1] || tint[2];
    for (int i = n - 1; i >= 0; --i) {
        void* cmd = g_fx.item(c->copy, i);
        if (!cmd)
            continue;
        void* vt = *static_cast<void**>(cmd);
        if (quiet && vt == g_fx.playSoundVtable && g_fx.removeItem(c->copy, i))
            ++removed;
        else if (tinted && vt == g_fx.spawnVtable && spawns < 8)
            c->spawns[spawns++] = cmd;
    }
    Log("effect: copy of %s: %d commands, %d sounds removed, %d spawn commands, tint (%.2f %.2f %.2f)", name, n,
        removed, spawns, tint[0], tint[1], tint[2]);
    return c;
}

} // namespace

bool PlayEffect(const char* name, const Vec3& position, float scale, bool quiet, const float* tint)
{
    static const float kOwn[3] = {};
    alignas(8) char str[16] = {};
    g_game.stringNew(str, name);
    Effect fx;
    if (FxInit()) {
        // bCMatrix: 4x4 rows, translation in the last row
        float m[16] = {scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, position.x, position.y, position.z, 1};
        bool tinted = g_tintHooks && tint && (tint[0] || tint[1] || tint[2]);
        void** slot = quiet || tinted ? FindSequenceSlot(name) : nullptr;
        EffectCopy* c = slot ? Copy(name, slot, quiet, tinted ? tint : kOwn) : nullptr;
        if (c)
            *slot = c->copy;
        g_fx.startEffectMatrix(&fx, str, m);
        if (c)
            *slot = c->original;
    } else {
        g_game.startEffect(&fx, str, &position);
    }
    g_game.stringFree(str);
    return fx.e != nullptr;
}
