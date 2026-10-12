#pragma once

// The game's script API (Script.dll), resolved by mangled export name. See notes/loot-recon.md.
// Game thread only. Every function pointer is checked by GameApiInit; when it returns false nothing may be called.

#include <cstdint>

// Script.dll's Entity / PSInventory / PSItem / PSLock: an 8-byte view of an eCEntity*.
struct Entity {
    void* e = nullptr;
    explicit operator bool() const { return e != nullptr; }
};

// gEItemCategory values, from Script_Game's InvFilter_Category_* (the inventory tabs). 0 / 4 not seen in a filter.
enum ItemCategory { ItemCatNone = 0, ItemCatWeapon = 1, ItemCatArmor = 2, ItemCatConsumable = 3, ItemCatMagic = 5,
                    ItemCatMisc = 6, ItemCatWritten = 7 };

struct Vec3 {
    float x, y, z;
};

// Script.dll's Effect: handle of a running effect (gCEffect*), no destructor - effects end on their own.
struct Effect {
    void* e = nullptr;
};

// bTObjArray<Entity>
struct EntityArray {
    Entity* data;
    uint32_t count;
    uint32_t capacity;
};

struct GameApi {
    // Entity
    Entity* (*getPlayer)(Entity* ret);
    void (*updateEntityLists)();
    EntityArray* entities; // Entity::ms_arrEntities, rebuilt by updateEntityLists (once per frame)
    bool (*isDead)(const Entity*);
    bool (*isDown)(const Entity*);
    bool (*isNpc)(const Entity*);
    bool (*isItem)(const Entity*);
    bool (*isInteractObject)(const Entity*);
    float (*distanceTo)(const Entity*, const Entity*);
    Entity* (*getOwner)(const Entity*, Entity* ret);
    void* (*getName)(const Entity*, void* retString); // bCString
    bool (*isDialogCamera)();
    Vec3* (*getPosition)(const Entity*, Vec3* ret);
    // EffectSystem (static): effects by name from library.pak, e.g. "eff_magic_telekinesis_target_once_01"
    Effect* (*startEffect)(Effect* ret, const void* name, const Vec3* position);
    bool (*existsEffect)(const void* name);
    // PSInventory
    bool (*invValid)(const Entity*);
    int (*invStackCount)(const Entity*);
    int (*invInsertStack)(Entity* self, const Entity* item);
    Entity* (*invTemplateItem)(const Entity* self, Entity* ret, int stack);
    int (*invStackType)(const Entity* self, int stack); // gEStackType, 0 = normal (equipped etc. otherwise)
    int (*invAmount)(const Entity* self, int stack);
    int (*invCategory)(const Entity* self, int stack);
    bool (*invTransfer)(Entity* self, int stack, int amount, Entity* target);
    // PSInteraction::CanInteractWith(Entity) - what the focus asks; for animals and monsters the body's
    // CanInteract_Player_<kind> script also creates the plunder (meat, skins...) the first time.
    bool (*canInteractWith)(Entity* self, Entity* otherCopy);
    // PSItem
    bool (*itemValid)(const Entity*);
    bool (*itemMission)(const Entity*);
    unsigned long (*itemAmount)(const Entity*);
    int (*itemCategory)(const Entity*); // gEItemCategory, see ItemCategory below
    bool (*itemDropped)(const Entity*); // PSItem::PropertyDropped: thrown away by someone, taking it is not theft
    // PSProjectile: an arrow / bolt in flight is an item in the world too
    bool (*projValid)(const Entity*);
    bool (*projActive)(const Entity*);
    // Where the player and the camera look
    Vec3* (*atVector)(const Entity*, Vec3* ret);
    float* (*cameraPose)(float* retMatrix); // static, bCMatrix 4x4 rows
    // PSLock
    bool (*lockValid)(const Entity*);
    int (*lockStatus)(const Entity*); // PSLock::PropertyStatus -> gELockStatus (guess: called on the entity view)
    // GUI2 (global "gui2"): the game's "+ item" log on screen. Entity is passed by value = pointer to a copy.
    void* gui2;
    // World (global "world"): the entity under the player's focus (what the "use" key would act on)
    void* world;
    Entity* (*worldFocus)(const void* world, Entity* ret);
    void (*printItemLog)(void* gui, Entity* item, int amount);
    // PSCombatSystem: weapon drawn = combat mode != 0 (guess: 0 is gECombatMode_None)
    bool (*combatValid)(const Entity*);
    int (*combatMode)(const Entity*);
    // PSInteraction
    bool (*attachedToSlot)(Entity*);
    // bCString (SharedBase)
    void* (*stringNew)(void* self, const char* text);
    const char* (*stringText)(const void*);
    void (*stringFree)(void*);
    // eCScriptProxyScript (Engine.dll): runs a script by name, like the game does for AutoLoot in water/lava
    void* (*proxyNew)(void* self, const void* name);
    int64_t (*proxyCall)(const void* self, void* selfEntity, void* otherEntity, int64_t param);
};

extern GameApi g_game;

// Resolves everything once Script.dll is loaded. Logs what is missing.
bool GameApiInit();

// Convenience wrappers.
Entity Player();
const char* EntityName(const Entity& e, char* buf, size_t size);
bool HasOwner(const Entity& e);
// The game's "AutoLoot" script: moves the container's lootable stacks to the receiver, prints the item log,
// fires the quest hooks. Returns the number of stacks moved.
int64_t ScriptAutoLoot(const Entity& receiver, const Entity& container);
// Makes the game create a body's plunder (normally done when the player first focuses it). True when stacks
// were added.
bool GeneratePlunder(Entity& player, Entity& body);
// The same done by hand, for when the script gives nothing (player's weapon drawn). Returns stacks moved.
int64_t LootContainer(Entity& receiver, Entity& container);
// Any game script by name (Script_Game's exported scripts: "InvFilter_Loot", ...). Up to 4 different names.
int64_t ScriptCall(const char* script, void* self, void* other, int64_t param);
Vec3 EntityPosition(const Entity& e);
// Horizontal (x, z) view direction, unit length: the camera's, else the player's facing. False when neither is known.
bool ViewDirection(const Entity& player, float* x, float* z);
bool EffectExists(const char* name);
// Starts a one-shot effect at a world position, scaled; quiet = without its sounds (the game's own uses of the
// effect keep them). False when the effect system is not there or the name is unknown.
bool PlayEffect(const char* name, const Vec3& position, float scale = 1.0f, bool quiet = false);
