#pragma once

// The game's script API (Script.dll), resolved by mangled export name. See notes/loot-recon.md.
// Game thread only. Every function pointer is checked by GameApiInit; when it returns false nothing may be called.

#include <cstdint>

// Script.dll's Entity / PSInventory / PSItem / PSLock: an 8-byte view of an eCEntity*.
struct Entity {
    void* e = nullptr;
    explicit operator bool() const { return e != nullptr; }
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
    // PSInventory
    bool (*invValid)(const Entity*);
    int (*invStackCount)(const Entity*);
    int (*invInsertStack)(Entity* self, const Entity* item);
    // PSItem
    bool (*itemValid)(const Entity*);
    bool (*itemMission)(const Entity*);
    unsigned long (*itemAmount)(const Entity*);
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
