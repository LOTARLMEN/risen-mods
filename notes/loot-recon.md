# Loot recon: QuickLoot / AutoLoot (Steam Risen x64)

Static analysis only (exports + `dumpbin /disasm`); nothing here has been tested in game yet.
Things marked **guess** are inferences that still need checking.
RVAs are for the current Steam build. Resolve functions by **mangled name via GetProcAddress**, the same way `mods/speed` does; don't hardcode RVAs.

Tools:
- `tools/pe_exports.py <dll> [filter]` lists the exports. The full demangled lists are in `notes/exports/*.txt`, one export per line: `RVA<TAB>mangled<TAB>demangled`.
- `tools/pe_disasm.py <dll> <export-substring|0xRVA> [lines]` disassembles one function with dumpbin and annotates IAT calls, local exports and string literals.

## 0. The script layer (Script.dll): the API to use

Script.dll exports a C++ "script API". Script_Game.dll is written against it, and we can use it too.

- `class Entity` is **8 bytes**: `{ eCEntity* }`.
  - ctor `??0Entity@@QEAA@XZ` sets it to null. `AttachTo(eCEntity*)` stores the pointer. `operator=` copies 8 bytes. The dtor is empty (`ret 0`).
  - `Entity::operator eCEntity*` and `GetInstance()` both return the stored pointer.
  - In C++ we can declare `struct Entity { void* e; };` ourselves. Because the real class has user-defined ctor/dtor, MSVC x64 returns it through a hidden pointer:
    `Entity* (__cdecl *GetPlayer)(Entity* ret)`, `Entity* (__fastcall *GetOwner)(const Entity* self, Entity* ret)`.
    When it is passed **by value** (for example `GUI2::PrintItemLog(Entity,int)`), the callee receives a pointer to a temporary copy.
- `PSInventory`, `PSItem`, `PSLock`, `PSInteraction`, `PSRoutine`, `PSNpc` and so on are all the **same 8-byte view** of the same `eCEntity*`.
  All their `operator=` share one body at 0x1e6f40, and the script code calls `PSInventory::GetStackCount` with `rcx = &entity`.
  So `reinterpret_cast<PSInventory*>(&entity)` works, and `PSX::IsValid()` checks whether the entity has that property set.
- Globals exported unmangled from Script.dll: `None` (an `Entity`, null), `gui2` (`GUI2`), `world` (`World`), `tutorial`, `input`, `music`, `achievement`.
- `bTObjArray<Entity>` is 16 bytes: `{ Entity* data; uint32 count; uint32 capacity; }`.
  - It is allocated through `bCMemoryAdmin`. To free one returned by value, call `?Free@bCMemoryAdmin@@QEAAXPEAX@Z` on `?GetInstance@bCMemoryAdmin@@SAAEAV1@XZ` (SharedBase). That is what the inline dtor does.
- `bCString` (SharedBase):
  - ctor `??0bCString@@QEAA@PEBD@Z`
  - dtor `??1bCString@@QEAA@XZ`
  - `?GetText@bCString@@QEBAPEBDXZ`
  - Size is 8 bytes (**guess**, the class holds one data pointer).

## 1. Getting the player

| What | Export | Notes |
|---|---|---|
| Script layer | Script.dll `?GetPlayer@Entity@@SA?AV1@XZ` — `static Entity Entity::GetPlayer()` | Body is `gCSession::GetInstance().GetPlayer()` stored into `*ret`. |
| | Script.dll `?GetPlayer@World@@QEBA?AVEntity@@XZ` on global `world` | Used by `BtnEvent_OnClick_LootAll`. |
| | `?GetOriginalPlayer@Entity@@SA?AV1@XZ`, `?GetControledNPC@Entity@@SA?AV1@XZ` | For possession/control cases. |
| Engine level | Game.dll `?GetInstance@gCSession@@SAAEAV1@XZ` → `gCSession&` | Already used by `mods/speed/panel.cpp`. |
| | Game.dll `?GetPlayer@gCSession@@QEBAPEAVeCDynamicEntity@@XZ` → `eCDynamicEntity*` | Pointer is used directly as `eCEntity*` (single inheritance). |
| | `Entity::IsPlayer()` `?IsPlayer@Entity@@QEBA_NXZ` | |

The player inventory entity is named `"PC_Hero"`. Game.dll compares the owner name with it (see section 3.4).

## 2. Enumerating nearby entities

There is no exported "entities in radius" query in Engine or Game; I searched Collect/Query/Radius/Overlap/Sphere. Use the script layer instead:

- `static bTObjArray<Entity> Entity::GetEntities()` — `?GetEntities@Entity@@SA?AV?$bTObjArray@VEntity@@@@XZ`
  - Calls `UpdateEntityLists()` and returns a **copy** of `Entity::ms_arrEntities`, which you must free.
- `bTObjArray<Entity> Entity::GetEntitiesByDistance() const` — `?GetEntitiesByDistance@Entity@@QEBA?AV?$bTObjArray@VEntity@@@@XZ`
  - Copies the same list and qsorts it by distance from `this` (`SortEntites`, comparator global). That costs O(n log n) per call.
- To avoid allocating, call `?UpdateEntityLists@Entity@@KAXXZ` (protected static, but exported) and then read `?ms_arrEntities@Entity@@2V?$bTObjArray@VEntity@@@@A` directly.
  - The rebuild is cached per frame: it compares `eCTimer::GetMasterFrameCounter()` with `Entity::ms_u32LastFrame`.
  - The NPC subset is in `ms_arrNPCs` and `?GetNPCs@Entity@@SA?AV?$bTObjArray@VEntity@@@@XZ`.
- What is in the list (from the disassembly of `UpdateEntityLists`):
  - It iterates `gCInteractionAdmin::gCIterator`, i.e. every entity with a `gCInteraction_PS` that is registered with the interaction admin.
  - It filters by entity flags and skips items where `gCItem_PS::IsEquipped`.
  - So the list holds NPCs, monsters, world items, chests and other interactables. **Guess:** the admin only holds entities in the processing range/ROI around the player (`AddInteractionInROI` / `RemoveInteractionInROI`), so the list is already local.
- Distance:
  - `float Entity::GetDistanceTo(const Entity&) const` — `?GetDistanceTo@Entity@@QEBAMAEBV1@@Z`
  - `GetSquareDistanceTo` is also exported.
  - `bCVector Entity::GetPosition() const` — `?GetPosition@Entity@@QEBA?AVbCVector@@XZ`, returned through a hidden pointer; 3 floats.
- Classification (all `QEBA_NXZ`, `bool() const`): `IsItem`, `IsNPC`, `IsDead`, `IsDown`, `IsCharacter`, `IsInteractObject`, `IsPlayer`.
- Low-level alternative in Game.dll:
  - `gCInteractionAdmin::GetInstance` `?GetInstance@gCInteractionAdmin@@SAAEAV1@XZ`
  - `gCIterator` ctor `??0gCIterator@gCInteractionAdmin@@QEAA@XZ` (or `(gEInteractionUseType)`), `IsValid`, `operator++` `??EgCIterator@gCInteractionAdmin@@QEAAAEAV01@XZ`, `operator*` → `gCInteraction_PS*&`
  - `eCEntityPropertySet::GetEntity`
  - More work and no benefit over the script layer.

## 3. Loot-related functions

Script functions in Script_Game.dll come in two shapes.

1. **Plain script**: `__int64 __cdecl Fn(gCScriptProcessingUnit* spu, void* self, void* other, __int64 param)`, for example `?AutoLoot@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z`.
   - **Verified:** `self` and `other` are really `Entity*` (pointers to the 8-byte wrapper). If one is null, the function falls back to `spu->GetSelfEntity()` / `spu->GetOtherEntity()`.
   - So it is close to the `GEInt Fn(gCScriptProcessingUnit*, GEU32*, GEU32*, GEInt)` you guessed, with 64-bit ints and Entity pointers.
2. **AI function** (Interact_\*, PreInteract_\*, \_AI_\*): `bool __cdecl Fn(bTObjStack<gScriptRunTimeSingleState>&, gCScriptProcessingUnit*)`.
   - These are coroutines: each element is 0x58 bytes and holds a resume-state counter; the top element at -0x30 points to args where `+0x20` is the self `eCEntityProxy` and `+0x08` is the other.
   - They play animations through `gCScriptProcessingUnit::sAIPlayAniInstr` and return over several frames.
   - **Do not call these directly.**

### 3.1 Corpses

- `CanInteract_Player_Loot` (0x132100): returns 1 when the target has `PSRoutine` and its `AIMode` is 11 or 12. **Guess:** those are Down/Dead; use `Entity::IsDead()` / `IsDown()`.
- `AutoLoot` (0x1321e0) takes self = receiver (player) and other = container (corpse or chest). It:
  1. loops over `other.Inventory` stacks from last to first;
  2. calls the script `InvFilter_Loot(other, None, stackIndex)`, which skips non-lootable stack types and mission items and handles `It_Gold` (needs re-checking);
  3. calls `gui2.PrintItemLog(templateItem, amount)` (the on-screen "+N item" log);
  4. calls `other.Inventory.TransferAmountTo(stack, amount, self)` (`?TransferAmountTo@PSInventory@@QEAA_NHHAEAVEntity@@@Z`);
  5. returns the number of stacks moved.
- The **game itself calls `AutoLoot(player, corpse)` outside any interaction**:
  - from `CheckAutoLootWater` / `CheckAutoLootRagdoll` (a corpse falls into water or lava);
  - from `OnExitProcessingRange`;
  - from `Interact_Player_AutoLoot` (an AI function: AutoLoot, then prints "NothingToGet" if the result was 0, then `PSRoutine::ContinueRoutine`).
- `PreInteract_Player_Loot` / `Interact_Player_Loot` are the loot **window** path:
  - `GUI2::OpenLoot`, camera `cam_normal_fixnosync`, controller script `OnControllerLoot` / `OnControllerLootLoop`, waits while `GUI2::IsBrowsingLoot`.
  - `BtnEvent_OnClick_LootAll` = `World::GetInteract()` + `PSInventory::SetMaxSelection()` + `PSInventory::LootSelections(Entity&)` + `GUI2::Close`.
- `AssessPlayerLooting` (0xbe900) is an NPC-side perception script.
  - It checks whether the NPC sees the player's **current interaction** (`PSInteraction::GetCurrentTarget` / `UseType` / `IsOnDestination`) and then calls `AssessTheft`.
  - **Guess:** looting through `AutoLoot` without starting an interaction is invisible to witnesses, so it also lets the player rob knocked-out ("Down") NPCs without consequences. I'd only loot `IsDead()` targets, or skip Down humans.
- Tutorial hooks: `TE_Condition_Loot` / `TE_Completed_Loot`, and `Tutorial::StopEvent("loot")` is called by `Interact_Player_AutoLoot`. Cosmetic.

### 3.2 Chests and locks

- `CanInteract_Player_Chest` (0x10ee40) = script `CanInteract_Player(player, chest)` && script `CanOpenLock(player, chest)`.
- `CanOpenLock` (0x10ebf0):
  - If there is no `PSLock`, or `PSLock.Status == 1`, the chest can be opened. **Guess:** 1 = unlocked.
  - Otherwise it checks the key (`PSLock::GetKey` + `PSInventory::HasItems`), then `Difficulty` against `PSSkills::GetTotal` (lockpick skill), then whether `It_Lockpick` is in the inventory.
  - On failure it **prints a message** (NoKey / Impossible / TooDifficult / NoLockpick…), so don't call it in a loop.
  - For QuickLoot read the status directly: `PSLock::IsValid` `?IsValid@PSLock@@QEBA_NXZ`, then `??BPropertyStatus@PSLock@@QEBA?BW4gELockStatus@@XZ` (call with `&entity`), and require status == 1.
- `PreInteract_Player_Chest` (0x10ef60): goto, kneel animation, `AssessTheft` broadcast (see 3.3), AI function `Unlock`, `g_strAction_OpenChest` animation, then the loot window. `Interact_Player_Chest` (0x10f910) handles the window and the close animation.
- Chest ownership: `Entity::GetOwner()` `?GetOwner@Entity@@QEBA?AV1@XZ` = `PSInteraction::GetOwner()` (an `eCEntityProxy` in the interaction PS). A non-None owner means the chest belongs to someone, so looting it is theft.

### 3.3 Ground items (AutoLoot)

- `Interact_Player_TakeItem` (0x1215f0, AI function) does, in order:
  1. `Tutorial::StopEvent("takeitem")`;
  2. AI `SlowDown`;
  3. camera;
  4. `Take`/`Reach` animation (Begin);
  5. effect `eff_item_<material>_transfer_01`;
  6. **theft broadcast**: the internal helper `sub_ed3e0(…, item, eCScriptProxyScript("AssessTheft"))`, which walks `Entity::GetNPCs()` and runs perception scripts (StartSayEndAttack, `PSNpc::LastPlayerCrime` and so on). It is **not exported**;
  7. `player.Inventory.InsertStack(item)` `?InsertStack@PSInventory@@QEAAHAEBVEntity@@@Z`;
  8. `PSInteraction::EndInteraction`;
  9. End animation;
  10. script `ResumePlayerTask`.
- `_AI_TakeItem` (0xb0760) is the same thing for NPCs.
- `AssessTheft` (0xc1740) is exported as a plain script: `?AssessTheft@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z`. It is **per observer**; the broadcast loop over NPCs is internal.
- Item-specific CanInteract scripts:
  - `CanInteract_Player_TakeItem_NotInSlot` (`PSInteraction::IsAttachedToSlot`), `_TorchBurning`, and `_Skull_*` / `_Bust_Oldmine` / `_VulcanTemple_Breakthrough` (quest objects).
  - Read them generically with `PSInteraction::GetCurrentCanInteractScript()` `?GetCurrentCanInteractScript@PSInteraction@@QEAA?AVeCScriptProxyScript@@XZ` and `GetCurrentInteractScript()` → `gCScriptProxyAIFunction`. Get the name via `eCScriptProxy::GetScriptName` `?GetScriptName@eCScriptProxy@@QEBAAEBVbCString@@XZ`.
- Useful item queries:
  - `PSItem::IsMissionItem`, `GetCategory`, `GetAmount`, `IsEquipped`, `GetEquipOwner`
  - `PSInteraction::IsAttachedToSlot()`, `CanInteractWith(Entity)`, `BeginInteraction(Entity[, gEInteractionType])`

### 3.4 Inventory transfer and quest hooks

- Script layer (Script.dll `PSInventory`):
  - `GetStackCount`, `GetAmount(int)`, `GetTemplateItem(int)` → Entity, `GetStackType`
  - `TransferAmountTo(int stack, int amount, Entity& to)`, `InsertStack(const Entity&)` → int, `CreateItems(Template const&, int, gEStackType)`
  - `HasItems`, `FindStackIndex`, `DeleteStack`, `LootSelections(Entity&)`, `SetMaxSelection`
- Engine layer (Game.dll `gCInventory_PS`): `InsertStack(eCEntity*)`, `ChangeItemAmount(int,int)`, `TransferItemsTo/TransferStackTo/TransferAllTo(gCInventory_PS*, …)`, `NotifyListeners`, `OnStackChange`, `Add/RemoveListener`.
- **Verified:**
  - `gCInventory_PS::ChangeItemAmount`, which `InsertStack` calls at its end, and `gCInventory_PS::TransferItemsTo` compare the owner name with `"PC_Hero"`.
  - If it matches, they call the script **`OnPlayer_Change_ItemAmount`** through a static `eCScriptProxyScript` (initialisers at Game.dll 0x319a20 / 0x328c00).
  - So both the AutoLoot transfer path and the InsertStack path fire the player's item-change script. **Guess:** quest item-collection logic also runs from there and/or from inventory listeners.
  - `InsertStack` also removes the world entity (`MoveToWorldNode` + `eCEntity::Kill`). After that the `Entity` you hold is dead, so don't touch it again in the same loop.

## 4. Calling scripts from our DLL

Two ways, both cheap.

1. **Through the engine's script proxy (recommended).** This is exactly how the game calls `AutoLoot` from water/ragdoll handling.
   ```cpp
   alignas(8) char proxy[16];   // eCScriptProxyScript = { vftable, eSScriptBase* }
   alignas(8) char name[8];     // bCString
   bCString_ctor(name, "AutoLoot");                 // SharedBase ??0bCString@@QEAA@PEBD@Z
   ProxyScript_ctor(proxy, name);                   // Engine ??0eCScriptProxyScript@@QEAA@AEBVbCString@@@Z
   __int64 n = ProxyScript_call(proxy, playerEnt, corpseEnt, 0);
                                                    // Engine ??ReCScriptProxyScript@@QEBA_JPEAVeCEntity@@0_J@Z
                                                    // operator()(eCEntity* self, eCEntity* other, __int64) const
   ProxyScript_dtor(proxy); bCString_dtor(name);    // ??1eCScriptProxyScript@@QEAA@XZ, ??1bCString@@QEAA@XZ
   ```
   - `operator()` jumps to `CallScript`, which looks up module `"gCScriptAdmin"` once and calls its vfunc +0x1A0 `(script, self, other, param)`. That runs the script inside a proper `gCScriptProcessingUnit`.
   - Create the proxy once and reuse it; the name lookup goes through the `eCScriptFunctionMap` hash.
   - Here self/other are raw `eCEntity*`, not `Entity*`.
2. **Direct call** of the exported function through `GetProcAddress(GetModuleHandle("Script_Game.dll"), "?AutoLoot@@YA_JPEAVgCScriptProcessingUnit@@PEAX1_J@Z")`, as `AutoLoot(nullptr, &playerEntity, &corpseEntity, 0)`.
   - **Verified:** AutoLoot only touches `spu` when an arg is null.
   - Script_Game.dll lives in `bin\scripts\` and is loaded by `gCScriptAdmin::LoadScriptDLL`, so it may not be loaded when our DLL attaches. Resolve it lazily.
   - `ScriptInit` (an unmangled export) returns a static registration table, and lookup is by name.
- Don't call AI functions (`Interact_Player_Loot` and so on) directly; they need a running interaction state stack.
  - To get the full vanilla path with animation, start an interaction with `PSInteraction::BeginInteraction(Entity target)` on the player. That is the same as pressing Use, but the player must be in range and it can't be batched.
  - Game.dll also has `gCScriptProcessingUnit::CallAIFunction(const gCScriptProxyAIFunction&, gSArgsForScriptBase*)`, but it needs an SPU.

## 5. Per-frame hook on the game thread

- **Current:** the IDirect3DDevice9::Present hook (`shared/d3d9_hook`).
  - `mods/speed` already calls `gCSession::Pause/Resume` from there.
  - **Guess:** Risen renders on the main/game thread, so script calls from Present are probably OK. Make the calls at the start of our callback, not in the middle of ImGui drawing.
- **gCSession::Process:**
  - Game.dll `?Process@gCSession@@UEAAXXZ` (RVA 0x9711e0) is virtual, at **slot 41 (offset 0x148)** of `??_7gCSession@@6B@` (RVA 0xf4f678).
  - We have no detour library, so patch the vtable slot (VirtualProtect on .rdata). That gives a clean "after game logic" tick.
- Others:
  - `eCApplication::Process` / `OnProcess` / `Run` (Engine, `?GetInstance@eCApplication@@SAAEAV1@XZ`)
  - `eCTimer::Update(bool)` (Engine 0x4cf0e0)
  - Engine and Game export no hook API, so these would need code detours.
- Frame counter for throttling: `eCTimer::GetMasterFrameCounter()`. Time: `Entity::GetPlayingTimeInSeconds()` or `eCTimer::GetFrameTimeInSeconds()`.
- Skip work while `gCSession::IsPaused`, while `GUI2::IsBrowsingLoot` / `IsBrowsingInventory`, in dialogue (`Entity::IsDialogCameraActive`), and when the player is dead.

## 6. Recommended implementation

### QuickLoot (key)
1. On the key press (via the existing input capture), on the game-thread tick: get `player = Entity::GetPlayer()` and return if it is null or dead.
2. `UpdateEntityLists()`, then copy the `ms_arrEntities` pointers into a local vector. For each `e != player` with `GetDistanceTo(player) <= radius`:
   - **Corpse:** `IsNPC() && IsDead()`, `PSInventory::IsValid`, `GetStackCount() > 0`. Optionally skip `IsDown()` humans (see the 3.1 theft caveat).
   - **Chest:** not NPC or item, `PSInventory::IsValid` with stacks.
     - Skip it if it has a `PSLock` with `Status != 1`.
     - Skip it if `GetOwner() != None` (or handle the owner via AssessTheft; currently there is no clean exported broadcast).
     - **Guess:** further filtering by interaction UseType or CanInteract script name (`CanInteract_Player_Chest`) will be needed to exclude merchants' boxes, altars and so on.
3. Call the script `AutoLoot(player, target)` through `eCScriptProxyScript`. It gives the vanilla filter (`InvFilter_Loot`), the on-screen item log, and the `OnPlayer_Change_ItemAmount` quest hook. Add up the returned counts; if the total is 0, show "nothing to loot".
- Risks:
  - Chest open state and animation flags are not set: a chest looted this way may still look closed. Possible fix: `PSInteraction::PropertyInteractionCounter` (**guess**).
  - Chest-specific scripts in `PreInteract_Player_Chest`/`Unlock` are skipped. **Guess:** quest chests (`OnQuestSuccess_UnlockGregsTreasureChests`) work through the lock status, so skipping locked ones keeps them safe.

### AutoLoot (ground items)
1. Throttle to around 4 Hz. Candidates are `IsItem()`, within radius, not equipped, `PSItem::IsValid`, `!PSInteraction::IsAttachedToSlot()`.
2. Ownership: if `GetOwner()` is not `None`, skip the item and leave it for manual pickup. Then theft is impossible and no `AssessTheft` is needed.
3. Quest items:
   - Skip `PSItem::IsMissionItem()`, and skip items whose CanInteract or Interact script name is not the default (`Interact_Player_TakeItem` / default CanInteract).
   - Alternatively, for those, evaluate their CanInteract script through the proxy with `(player, item)` and pick them up only when it returns non-zero.
   - Note that CanInteract scripts can print messages.
4. Pick up with `player.Inventory.InsertStack(item)` (no animation), then `gui2.PrintItemLog(item, amount)` for the HUD line.
   - Read the amount via `PSItem::GetAmount` **before** inserting.
   - `InsertStack` kills the world entity, and the `OnPlayer_Change_ItemAmount` hook fires.
- Risks:
  - We skip `EndInteraction` / `ResumePlayerTask`. That is harmless because no interaction was started.
  - Picking up while in combat or a cutscene; gate on the states above.
  - Items in shops are owned, so the ownership rule covers them. Verify in game that merchant goods report an owner.

### Verification plan (in game)
Log `GetEntities` counts and distances, `IsDead/IsItem/GetOwner/PSLock` status values (to confirm that 1 = unlocked and the 11/12 AI modes), and whether quest pickups advance their quest (for example a skull item, or any quest that requires "bring X").
