# World Debug 0.1.1 — repeatable physics checks and native free camera

User requested developer controls after confirming physical-ground contact and save/load (2026-10-15).
The shield was already resting, so that check did not show another fall. A separate DLL provides
repeatable in-game lift/freeze/release and the game's own free camera; no world data edits needed.

F10 → World debug / Отладка мира enables the tools; shipped source INI defaults to disabled.
The installed test INI explicitly enables them. Num Lock on, AutoLoot OFF:

- NUMPAD7: select the focused loose, unowned rigid-body item.
- NUMPAD9: raise it by 100 cm and freeze it. Repeated presses raise it further.
- NUMPAD5: freeze/release the selected item; releasing enables gravity and wakes it.
- NUMPAD0: toggle native freefly camera. Use native movement/look controls and verify return.

Settings expose all four keys, lift height 1..500 cm and camera speed 0.1..1000 cm/s (default 30).
Speed changes in F10/INI apply immediately to an active owned camera. Ctrl+click the F10 number
allows precise entry. 0.1 used 10..5000, default 300, and only applied speed on camera activation.
NUMPAD8 was avoided:
ConfigDefault.xml binds it to OnDebugToggleFreeCam. Whole-game TestMode/cheat keys are not enabled.
Selection survives moving the camera, but the item must remain within 20 m of the hero and within
processing range. NPCs, inventory/equipped/owned items and objects without rigid bodies refuse.
Static boards/barrels are not converted into dynamic objects. Disable returns a freecam enabled
by this mod; object physics/position changes remain and can persist in a normal game save.

All game API calls execute on the game window/main thread (WM_APP + 0x5901..3). No entity or
property-set pointers survive a command/tick. Selection contains 16 GUID-core bytes and a native
4-byte hash cache (bCPropertyID value, 20 bytes). SharedBase copy constructor 0xa61a0/operator=
0xa5950 confirm the layout and clear the hash cache. Native eCDynamicEntity::GetID supplies it;
eCSceneAdmin::GetEntity 0x697b80 resolves it on every command (the type parameter is unused here).
Selection additionally verifies GUID lookup returns the current focus before allowing any mutation.

Scene/camera admins are found afresh via eCModuleAdmin GetInstance/GetModuleCount/GetModule and
their exported primary vtables. No game object offsets or patched instructions are used. All 32
required exports are checked by the mod and independently by tools/check-world-debug.py.

PSRigidBody script wrappers use the Entity view (one pointer); exported IsValid/operator-> confirm
the body. Native script PropertyBodyFlag assignment calls Engine SetBodyFlag, whose property-change
notification 0x658980 removes/adds the actor flags through the state buffer. Freeze clears kinematic,
adds 0x7e frozen axes, stops motion and sleeps. Release clears only 0x7e/0x80/0x01, retaining unknown
flags, enables PhysicsEnabled, sets wake counter 1 and wakes. Lift uses PSRigidBody::SetPosition(Vec3),
then wakes the frozen actor for the queued pose update. User confirmed the core tools work in game.

Freecam calls gCCameraAdmin::OnToggleFreeFlyCam 0x7ac530 and SetFreeCamSpeed; GetDebugMode reads
its state. The engine copies/restores the camera pose itself. User confirmed freecam works, with
excessive speed the only reported issue. Disabling tools/leaving the world
returns a camera owned by this mod; engine resets can also turn it off.

Full build passed without errors/warnings; 32 required exports present in installed DLLs.
Deployed only bin/mods/world_debug.dll (2026-10-15 16:06:20) and world_debug.ini with Enabled=1.
Other game DLL/INI SHA256 unchanged; world data untouched. Backup/absence manifest:
backup/world-debug-20261015-160834/manifest.json. Original full bin backup also present.
Game launched through Steam 16:08:50; 16:09:13 log confirms API and scene/camera module lookup,
then main menu. Other deployed mod DLLs remain October 12-14 builds. No package/commit/push.

**Manual test confirmed 2026-10-15:** user reports all core tools working without problems, but
freecam excessively fast even after setting 10. Separate refusal and debug-action save/load cases
were not individually described. The board contact/save-load check predates these tools.

**0.1.1 speed correction:** ConfigLoad previously read the new value without sending it to an
active camera; the old speed remained until another toggle. Apply SetFreeCamSpeed immediately
on F10 callbacks / INI reloads. Lower default to 30, permit 0.1..1000 and preserve installed INI
settings (including the user's 10) on deployment rather than replacing them with test defaults.
Native code confirms speed units: Game SetFreeCamSpeed 0x7a83f0 writes camera config +0xe8;
sub_7b20e0 passes that and +0xec (acceleration modifier) into sub_7af1c0. Translation normalizes
direction and multiplies by GetFrameTimeInSeconds * speed; acceleration applies the native
modifier only when its action is held. World positions use centimetres. Inspection captured in
build/recon/debug-camera-speed{,-dispatch}.txt; runtime uses exports, no offsets or patches.
**Pending:** user retest of slower movement and immediate F10 speed changes in 0.1.1.

0.1.1 full C++ build passed without errors/warnings; all 32 exports checked. Installed ONLY
world_debug.dll build 2026-10-15 16:24:35 with game closed; existing INI SHA256 preserved,
speed=10, other DLL/INI SHA256 unchanged. Backup: backup/world-debug-20261015-162506/manifest.json.
Steam startup log 16:25:17 identifies 0.1.1 and 10.0 cm/s; 16:25:25 resolves API/scene/camera
and reaches the main menu. User deferred hands-on speed test to next session and requested a
local commit. Other installed mod DLLs remain October 12-14 builds; no package/release/push.
