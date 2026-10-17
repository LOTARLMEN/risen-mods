# Live bridge — drive the running game from a shell (TODO 7.1)

Dev-only, never packaged (no README). Steam Risen x64 Rev. 2045; every verb below was checked in game on
2026-10-17 (see DEVELOPMENT_LOG.md).

## Parts
| File | Role | Replace while the game runs? |
|---|---|---|
| `bin\mods\live_bridge.dll` (`mods/live-bridge`) | host: named pipe, game-thread hand-over, `reload` | no (loaded by the loader) |
| `bin\mods\live\live_verbs.dll` (`mods/live-verbs`) | every verb, API resolution, JSON | **yes**, then `risen live reload` |
| `risen live` (`rust/risen-cli/src/live.rs`) | client | — |

The host loads a shadow copy `live_verbs.loaded-<pid>-<n>.dll`, so the deployed file is never locked; `reload`
copies + loads the new file first and frees the old copy only if the new one is compatible (exports
`LiveHandle`, `LiveVersion`, `LiveReplySize`). Old copies are deleted at the next start. `build.bat deploy`
puts `live_verbs.dll` into `bin\mods\live\` (the loader must never load it: it is not a mod).

Rules for `live_verbs.dll`: no hooks, threads, window subclassing or hub registrations, and no engine pointer
kept between calls — it can be unloaded after any request. Anything that needs a hook (frame capture via the
D3D9 hook, IAT hooks) belongs to the host, which then needs a game restart to update.

## Protocol
Pipe `\\.\pipe\RisenLive`, local clients only, one client at a time. One JSON line in, one JSON line out:
`{"cmd":"<verb>","args":["..",..]}` → `{"ok":true,...}` / `{"ok":false,"err":"..."}`. At most 8 args,
63 bytes each. Requests run in the game window procedure; if the game thread does not answer in 2 s (loading
screen) the reply is `timeout`. Every request + reply (first 300 chars) + time is one line in `RisenMods.log`.
The game never sends anything by itself.

## Verbs
Positions are cm, Y up; yaw degrees, 0 = +Z, 90 = +X (confirmed by walking). `~` = relative (`~0 ~200 ~0`).

| Verb | Reply / effect |
|---|---|
| `ping` | verbs version, game build check (PE timestamps of Game/Script/Engine) |
| `status` | world (`G3_Startup` = main menu), hero pos + yaw or null, dialog, day, time |
| `teleport x y z [yaw]` | Entity::MoveTo on the hero; camera follows, physics/navigation in sync |
| `look <yaw\|~d>` | turn the hero in place |
| `near [--radius 2000] [--limit 20] [--kind item\|npc\|object] [--name part] [--all]` | nearest entities: guid, name, kind, pos, distance; default = spawned (processing range) and not carried |
| `show <guid>` | one entity: dynamic, processing, carried, pos, yaw, rotation rows (with scale), distance |
| `move <guid> x y z` | Entity::MoveTo, rotation kept; collision follows (user jumped onto a moved crate) |
| `rotate <guid> <yaw\|~d>` | around world Y, tilt and scale kept |
| `time HH:MM` | world clock (saved with the game, affects NPC schedules) |
| `weather on\|off\|list\|<state>` | via World Debug (must be on; a test start enables it). `Stormy_Rain` = rain + thunderstorm, `Foggy` = fog |
| `reload` | host verb: load a new `live_verbs.dll` |

Refusals: no hero / dialog camera; `move`/`rotate` refuse the hero, carried items, not spawned entities and
anything farther than 100 m. Changes are live only: they vanish unless the user saves; a save keeps them
(not tested for moved objects yet).

## Engine facts used (Rev. 2045)
- World name: `eCProcessibleElement::GetFileBaseName` on `gCWorld::GetCurrentWorld()` (base at offset 0,
  the copied bCString is at +0x18 — bytes checked).
- Pose: Script `Entity::GetPose` / `Entity::MoveTo(bCMatrix)`; bCMatrix = 64 bytes row-major, X axis +0x00,
  Z axis (at) +0x20, translation +0x30. Translation is cross-checked with `GetPosition` before writing.
  MoveTo = `SetToWorldMatrix` + `PSRigidBody::SetPose` + `PSNavigation::ResetAll` + `StopMovement`
  (the vanilla `Entity::Teleport` calls it).
- All entities: Game `gCWorld::EnumEntities(bool(*)(eCEntity*,void*), ctx, 1)` — enabled sectors, every layer,
  statics included; false from the callback stops. World ≈ 19800 entities, well under 16 ms.
- GUID: virtual `GetID`; its slot (41) is found at init in both `??_7eCEntity@@6B@` and
  `??_7eCDynamicEntity@@6B@`. Printed like `risen-formats::props::format_guid` (`{XXXXXXXX-...}`).
- Spawned vs loaded: entities of disabled sectors stay loaded (Testlevel_Plane lists NPCs nobody sees) but
  `Entity::IsProcessingRangeEntered` is false. Carried = item with `Entity::GetOwner` or
  `PSInteraction::IsAttachedToSlot` (hero's `Hero_Head_Player`, `It_Armor_Player`).
- Script `Entity::GetEntities` / `GetEntitiesByDistance` only cover gCInteractionAdmin entities (interactive
  objects, items, NPCs), not statics — not used.
