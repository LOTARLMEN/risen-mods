# Geometry, collision metadata and placement (Rust CLI)

## Physical ground mode (2026-10-15)

```powershell
.\rust\target\release\risen.exe ground RisenMods_NewLayer_Test 2 --collision --surface-layer Levelmesh_Landscape_COL --gap -2 --dry-run
.\rust\target\release\risen.exe ground RisenMods_NewLayer_Test 33 --collision --align --samples 9 --max-rise 500 --surface-layer Levelmesh_Landscape_COL --gap -2 --out build\physical-ground.lrent
```

`--collision` selects physical geometry for BOTH the moving entity and its supports. The moving
entity must have enabled solid box, triangle-mesh or convex colliders. Dense local underside sampling
is enabled (default 7x7 grid plus collider vertices), even without --align; children follow through
the existing atomic world-pose setter. The sampled collider vertices/underside replace the render
model as the placement footprint. Support geometry includes actual XCOM meshes, oriented boxes and
analytic spheres/capsules; curved supports are not tessellated. Curved MOVING colliders currently
refuse because analytic underside sampling is not implemented. `--height` uses an explicit plane
with the physical moving geometry. Default ground and snap retain their previous behavior.

Supports must be serialized enabled/collision-enabled entities without a rigid-body PS. Rendering
need not be enabled: the landscape's separate `Levelmesh_Landscape_COL` layer participates. PS and
shape DisableCollision/DisableResponse and IgnoredByTraceRay filter support candidates; ray-ignore
does not erase a solid moving collider. The conservative support policy accepts static/dynamic/tree/
door shape groups (1,2,9,10,20); camera/weapon/trigger groups cannot become accidental ground.
This policy is not a reconstruction of every native collision-group interaction. XCOM resource
material flags are authoring defaults; native SetProprietaryShapes uses the serialized shape flags,
so these resource defaults are not applied again as runtime disables.

World matrices and primitive local centre/orientation apply exactly once; LastPosition is not a
local translation. Box extents are half sizes. Capsule height is its cylinder height along local Y,
with spherical end caps. Positive uniform scale is supported for primitives/convexes. Native
Engine.dll SetProprietaryShapes RVA 0x656ad0 computes scale from the world X basis; the triangle
branch 0x65774d refuses a scale delta >0.0001 (constant 0x9f1548). The CLI refuses scaled triangle
meshes too. Nonuniform/reflected/sheared poses, pre-74 primitive layouts and runtime AABB-adaptation
are unsupported, instead of inventing a physical result. An adapted shape's saved outer box can
exclude a distant candidate; an adapted candidate within the query refuses. No animation, scripted
motion, dynamic simulation or PhysX skin-width/contact solver is reproduced.

Physical bounds drive support culling; render/entity visual bounds do not exclude physical meshes.
Resources are indexed once by mounted compiled basename (ambiguities refuse) and decoded once per
operation. Unsupported or unreadable candidates make the search incomplete; use an explicit
supported --surface-layer or --height. Missing physical support never falls back to render meshes.
The existing two-sided, vertical, sampled grounding algorithm and its residual gaps remain: this
is more appropriate input geometry, not exact continuous contact/overlap detection.

74 workspace tests passed (one ignored helper). Eight new tests cover primitive poses/half-extents,
native serialization, analytic spherical/capsule intersections including tilt/range/distant positions,
PS/shape filtering and invalid flags, resource index/kind, transformed physical bounds, unsupported
scale/adaptation/curved moving geometry, and physical underside/support heights. Debug/release built;
release CLI 2026-10-15 15:41:43. Active-world search at board A found 5 candidates / 0 unavailable.
tools/test-collision-ground.ps1 checked physical placement, child local matrices/local*parent=world,
property/other-entity preservation, repeatability, visual control and safe slope/no-physical-support
refusal; installed VFS/bytes and unrelated SHA256 checks passed.

Installed 15:43: only boards [2]/[33], child barrels [3]/[34] and required aggregate bounds changed
in the existing 35-entity test layer. Board A y=65.28201 (+2.07447 cm), its old orientation retained.
Steep board y=325.83588 (+5.65546 cm), tilt about 29.723 degrees, normal
(0.09128679,0.86843157,-0.4873329), 85 samples, residual above gap=-2 about 9.682 cm (absolute
clearance up to about 7.682 cm). All other 30 non-root entities, property data and other game
data/INI/DLL hashes unchanged. Verified backup/world-restore:
`backup/world-edits/811942CC-14FE-4B98-AF92-B4D00A034CE6` (restore before DE9ED2C4...).
Stage: build/tests/collision-ground-20261015-154309/installed-test.json.
Game launched through Steam at 15:44:16; bin/Risen.exe running, new log confirms the main menu.
USER CONFIRMED (recorded 2026-10-15): boards touch the ground excellently and save/load changes
nothing. Physical-ground contact/persistence test PASSED. Walking/jumping not separately reported
in this confirmation. Shield was already resting there, so a fresh fall was not observed; do not
count it as a new dynamic-drop test. The requested world-debug tools now provide repeatable lift/
freeze/release and native freecam; their manual test is tracked in notes/world-debug.md.
No game DLL deployment during the physical-ground step;
bin DLLs remain October 12-14 builds, no commit/push.

## Cooked collision geometry decoded (2026-10-15)

`risen-formats::xcom::CollisionMesh::read` reads the physical triangle/convex resources without
loading a PhysX DLL. `geometry` includes their vertex/triangle counts, local bounds, resource index,
triangle-material histogram and resource material/disable/ray flags. Entity/template inspection
resolves the selected triangle OR convex array index; original `.xnvmsh` references now resolve to
`._xcom` instead of incorrectly appending `._xcom` to `.xnvmsh`.

```powershell
.\rust\target\release\risen.exe geometry Obj_Nat_DeadTree_Root_01_COL._xcom --json
.\rust\target\release\risen.exe geometry Obj_Dec_Ship_02_COL_CV._xcom --obj build\ship-collision.obj
.\rust\target\release\risen.exe geometry Obj_Nat_DeadTree_Root_01 --json
```

Direct `._xcom` OBJ exports contain physical polygons in local centimetres, Y up, with a separate
OBJ object per mesh. No axis interchange is applied. OBJ export of a template or entity still
exports its render model; primitive box/capsule/sphere colliders are available to physical ground,
but have no primitive OBJ export. Default `ground` and `snap` retain visual-mesh/AABB behavior;
`ground --collision` is described above. Reading physical polygons is complete for this
corpus; exact contact/overlap, collider filtering/transforms and physics simulation are separate work.

Supported layout: GR01CM00, eCCollisionMeshResource2 class data version 201; MESH version 1;
CVXM 3 with CLHL 0, CVHL 5 and VALE 2; optional SUPM 0 / GAUS 0 support map. Each nested PhysX
header supplies its own byte order. Triangle indices can be 8/16/32 bit; remaps/convex/flat parts
and OPC acceleration trees are skipped using their declared sizes. Convex disk polygon records
are 36 bytes (original 32-bit pointers), independent of the tool's host pointer size. Cooked geometry
is in metres and is multiplied by 100. Bounds are read separately from vertices. Tail: geometric
epsilon, bounding sphere, AABB, mass, inertia and centre of mass; triangles also have extra edge bytes.
Resource material records retain the raw surface byte and three booleans; per-triangle material
indices remain distinct, without guessing a collision-group interaction matrix.

Unknown versions/flags, inconsistent wrapper lengths, invalid counts/indices, nonfinite geometry,
malformed booleans and truncated streams refuse with an error. Acceleration-tree payloads and
skipped auxiliary topology are opaque: this reader does not validate every internal PhysX structure
or rebuild/cook collision files.

Reference layouts: [SDK 2.8.1 CookedMeshReader, pinned source](https://github.com/sigmaco/physx-v2.8.1/blob/687e1e9cb20dca848b636bcf30313e921348ae14/Tools/CookedMesh/Reader/source/CookedMeshReader.cpp)
and [Baltram's XCOM wrapper reader](https://github.com/Baltram/rmtools/blob/5525421bf4b22636259bdc0d250ee96a5abcae66/mimicry/source/Mimicry/mi_xcomreader.cpp).
Only format layout/ABI were consulted; the shipped Rust code does not include these SDK binaries.

Independent corpus check: tools/xcom-native-check.cpp builds as x86 and loads the already installed
old editor's PhysXLoader.dll/PhysXCore.dll. Its loader could not initialize SDK 2.8.1, so the tool used
bundled PhysXCore 2.8.4.6 (API 0x02080400) directly. It writes native vertex/index buffers and precise
stream boundaries to scratch `.native` files; the Rust `xcom_check` example compares every coordinate
bit after the same metre-to-cm conversion, every triangle index, counts and array/material offsets.
All **637 resources / 1089 meshes (1087 triangle, 2 convex) / 339572 vertices / 565676 triangles**
matched with zero refusals. Every vertex also lies within the cooked AABB (0.1 cm tolerance).

```powershell
# Optional native reference check; no game launch or game writes.
.\tools\test-xcom.ps1 -PhysXDirectory 'D:\RisenModding\ed_new\Risen Editor'
# Without that optional argument, the corpus/CLI checks need no PhysX runtime.
```

Five new Rust tests cover endian/index-width combinations, both convex normal encodings and optional
support maps, separate triangle/convex indices, all truncated prefixes (also with repaired wrapper
lengths), malformed counts/geometry/versions/flags/materials, and original collision-name resolution.
All 66 workspace tests passed (one ignored process-guard helper). Debug/release built; release CLI
2026-10-15 15:14:01. tools/test-xcom.ps1 passed with native comparison, multi-mesh and convex OBJ
counts/indices, original .xnvmsh template lookup, retained board render/box behavior, overwrite and
truncated-resource refusal. Stage: build/tests/xcom-20261015-151445/result.json. Game data/INI/DLLs
and physics.pak SHA256 unchanged; no game launch, C++ DLL deploy, commit or push. Bin DLLs remain
October 12-14 builds. Next: explicit collider geometry provider for ground (including primitives,
entity/shape transforms and filtering), then contact/overlap queries.

## Latest result: snap, climbing and slope placement passed (2026-10-15)

User confirmed the installed two-support snap scene: good visual joins (attached screenshot), walking
and jumping work, saving/loading retains the scene. Recorded 13:34. The scoped snap test is complete.
Game closed at the user's request; process absence verified 13:34. Test data remains installed.

Ledge grabbing has a serialized object setting: **eCCollisionShape_PS.IsClimbable (bool)**.
All current test boards, barrels and shields have value 0 (readback in
build/tests/snap-current-props.json). This differs from walking on or jumping onto a collider.

Native evidence on Rev.2045:

- Game.dll gCCharacterMovement_PS::TestClimbMode RVA 0x8e94e0 traces physical geometry. At
  0x8e9842 it obtains the hit collider's source property set; at 0x8e985e it calls
  Engine.dll eCCollisionShape_PS::IsClimbable; a false result rejects the candidate at 0x8e9866.
  The function also checks surface normal/angle, CanStandAtGoal (call 0x8e9914), movement mode
  and further ray clearance. The flag alone does not guarantee a usable ledge.
- Engine.dll IsClimbable RVA 0x650fd0 reads the property byte at this+0x44 in its normal branch.
  There is also an owner-dependent true return when the owner's virtual call at +0x170 returns
  null. The exact owner method is not identified yet; avoid applying the simple flag rule to all
  entity types. GetIsClimbable/AccessIsClimbable export RVA 0x853e0 returns the property address.
- Character movement exposes ClimbHeightMin/Low/Mid/High, ClimbFrontDepth1/2/3,
  ClimbTargetGroundAngleMax and last-climb report getters. These are character configuration
  and diagnostic data, distinct from the environment object's IsClimbable flag.

Disassembly saved under build/recon/climb-TestClimbMode.txt and climb-IsClimbable.txt.
The confirmed climb comparisons and the height-ladder result are described below.

## Climbing permission command and installed comparison

```powershell
.\rust\target\release\risen.exe climbable RisenMods_NewLayer_Test 14 --enabled true --dry-run
.\rust\target\release\risen.exe climbable RisenMods_NewLayer_Test 14 --enabled false --out build\no-climb.lrent
```

Changes only the selected entity's existing eCCollisionShape_PS.IsClimbable property, without
changing children, collision shapes, physics flags or templates. Requires exactly one collision PS
and one canonical bool property; missing/duplicate/wrongly typed/malformed values refuse atomically.
`--enabled true|false` is required. Game writes share the verified project transaction, backup and
closed-game guard; `--out` scratch and `--dry-run` are supported. `geometry` reports the serialized
`collision_sets[].is_climbable` value, distinct from the owner's effective runtime policy.

Prepared/installed 2026-10-15 (recorded 13:42), tools/test-climbable.ps1 -Install:

- OFF tower [9..11], three Obj_Dec_Crate_01 boxes at x=-37850, z=-5300, base y=80.27632.
- ON tower [12..14], the same boxes at x=-37450, z=-5300, base y=114.43372,
  closer to the existing boards. IsClimbable=1 on all three crates, 0 on the control tower.
- Both towers are ~211 cm tall, identical yaw 0 and relative stacking, with base grounded at -2 cm
  and upper boxes snapped with gap 0. Separate grounding follows different terrain heights;
  location/approach geometry remains a possible confound. If ambiguous, toggle the same tower for
  a stronger comparison. Neither tower has rigid bodies. Existing nine-entity snap scene retained.

55 workspace tests passed (plus one ignored subprocess helper). CLI checks: one-byte toggle,
full-layer byte identity after undo, missing-collider refusal without output, dry-run, crate face
contacts, expected flag-only differences, unchanged existing non-root entities/properties,
staging/installed byte identity, VFS readback and unrelated game-data/INI/DLL SHA256.
Only RisenMods_NewLayer_Test.lrent changed. Backup/world-restore:
`backup/world-edits/FD581343-207A-488E-9096-E0507B917946` (restore before E0FE4009...).
Stage: build/tests/climbable-20261015-134041/installed-test.json.
USER CONFIRMED (recorded 2026-10-15 13:45): the ON tower can be climbed from both sides;
the OFF tower cannot. Saving/loading retains both behaviors. The observed animation is a
jump/vault onto the surface, not sustained hanging/pulling up. Permission and persistence test
PASSED; sustained ledge-grab animation remains a separate possible higher-platform test.
Native Script.dll exports g_strAction_ClimbLow/Mid/High; Game.dll has DetectClimbMode,
ProcessClimbStandard and ProcessClimbHigh. DetectClimbMode resets height parameters to
80/160/240/400 cm and chooses among modes with physical traces/clearance checks
(build/recon/climb-DetectClimbMode.txt). Thus the permission flag is distinct from movement
mode selection. No runtime mode was logged for this report, so do not assign an exact
Low/Mid/High mode or animation asset to the observed vault from appearance alone.
Rust release CLI 2026-10-15 13:39:24; game DLLs remain builds of October 12-14. No C++ deploy.

Run commands from `risen-mods`; native coordinates are centimetres, Y up.

## What the geometry describes

Render models, entity bounding boxes and collision shapes are separate data. A board has a static box
collider, but no `eCRigidBody_PS`; it remains at its saved transform. The round shield has a rigid body,
`PhysicsEnabled=1` and its own collision shape, but its template has `BodyFlag=126` (0x7e, FROZEN).
This locks all six motion axes: an enabled rigid body can stay suspended. Rigid-body flags/character movement can change behavior;
the presence of a rigid body alone is not a guarantee of free falling.

`geometry` reports model resources, vertex/triangle counts, actor nodes and individual collision
shapes, their bounds, collision groups, response/ray flags and collision mesh references. It does not
infer a collision-group interaction matrix or simulate physics.

```powershell
.\rust\target\release\risen.exe geometry Obj_Furn_Plank_Board_01 --json
.\rust\target\release\risen.exe geometry RisenMods_NewLayer_Test 2 --obj build\board-world.obj
.\rust\target\release\risen.exe geometry Wolf_Black --json --obj build\wolf-bind-pose.obj
```

OBJ exports refuse overwrites. Static meshes export their render polygons. Actors export raw bind-pose
polygons; bone/skin deformation is not applied. JSON retains node names, parent indices, raw bind
positions/quaternions and mesh-node bindings. Entity exports apply the entity world transform.

Checked corpus: 1665 static meshes, 3,306,600 vertices / 2,220,771 triangles. The one procedural
`SpeedTreeBillboardMesh_World` uses a different vertex layout and is refused explicitly. All 111 actor
resources read: 334,301 split vertices / 465,507 triangles / 4,142 nodes, including both byte orders.
Procedural SpeedTree models are listed as resources; their polygons are not decoded. Cooked PhysX
`._xcom` polygons are decoded/exported separately (see above). Primitive collider metadata is read,
but primitive collision shapes are not exported as render models.

Format reference: [Baltram/rmtools, pinned source](https://github.com/Baltram/rmtools/tree/5525421bf4b22636259bdc0d250ee96a5abcae66/mimicry/source/Mimicry).
Reference source files were downloaded into build/recon; no rmtools binaries were installed or run.

## Ground and deliberate intersections

```powershell
.\rust\target\release\risen.exe ground RisenMods_NewLayer_Test 2 --surface-layer Levelmesh_Landscape --gap -2 --dry-run
.\rust\target\release\risen.exe ground RisenMods_NewLayer_Test 2 --height 40 --out build\on-plane.lrent
.\rust\target\release\risen.exe snap RisenMods_NewLayer_Test 3 --against 2 --side top --gap -5 --out build\embedded.lrent
```

`ground` moves the entity and descendants vertically. With `--height` it aligns the render model's
lowest point to an explicit horizontal plane. Otherwise it traces visual static triangles beneath the
world render AABB center and four corners, requiring support at every sample, then uses the highest
sample. `--max-drop` defaults to 1000 cm, `--max-rise` to 100 cm. Positive gap leaves clearance;
negative gap intentionally sinks into the surface. It preserves rotations and child local transforms.

Use `--surface-layer Levelmesh_Landscape` for terrain specifically. Default search uses initial
`--world World` sector registration, enabled sector flags and the optional local list, excluding mounted
test/import projects. Dynamic/save-driven streaming state is unavailable to the offline CLI. Static
visible meshes can be searched even when their collision lives in a separate resource/entity.
Unreadable candidate meshes cause refusal instead of silently choosing incomplete support.

This is **visual five-point placement**, not exact PhysX contact: water/visible surfaces can differ
from physical support, bumps between samples can be missed, and a horizontal object can leave gaps
on sloped terrain. Automatic slope/normal alignment and exact cooked-collision queries remain future
work. Physics flags are retained; a dynamic item can move again when the game simulates it.

`snap` aligns world render-AABB faces on top/bottom/left/right/front/back. `--center` also centers on
the other two axes; otherwise tangential coordinates are retained. `--target-layer` selects another
layer. This is box-face alignment, not polygon/edge matching. Negative gaps allow intended overlaps;
the CLI does not block them. Snapping against oneself or one's descendants is refused.

Writes to game project files require the game closed and use verified backups/atomic replacement
with `world-restore`, including explicit `--out` destinations inside game data. Scratch outputs and
`--dry-run` support review without installing. Placement currently requires a decoded static render
model; animated/procedural placement needs a further geometry/contact provider.

## Explicit rigid-body motion

```powershell
.\rust\target\release\risen.exe physics RisenMods_NewLayer_Test 6 --mode dynamic --dry-run
.\rust\target\release\risen.exe physics RisenMods_NewLayer_Test 6 --mode dynamic --out build\dynamic.lrent
.\rust\target\release\risen.exe physics build\dynamic.lrent 6 --mode frozen --out build\frozen.lrent
```

Changes only the selected entity's existing `eCRigidBody_PS`, never its children or the template.
`dynamic` clears frozen axes (0x7e), kinematic (0x80) and disable-gravity (0x01), enables physics and
sets `WakeUpCounter=1`. `frozen` locks all six axes, clears kinematic, enables physics and sets the
counter to zero; the previous gravity policy is retained. Unknown flag bits, enum container version,
serialized pose, velocities, collider settings and other properties are retained. Static objects and
malformed/duplicate required properties are refused before mutation. No rigid bodies are synthesized.

`geometry` shows raw flags, frozen/partially locked axes, kinematic/gravity flags and wake counter.
Offline policy is distinct from runtime behavior: scripts, AI or physics filtering may override it.
Game writes use the same verified backup/closed-game guard as placement. Defaults for `spawn` are
unchanged. `tools/test-physics.ps1` checks the installed frozen shield; `-Install` also installs the
verified dynamic edit. The test requires the original seven-entity frozen contact setup.

## Bounds findings

Engine.dll Rev.2045 `eCDynamicEntity::OnUpdateBoundary` 0x69e070 merges property-set contributions.
The resolver now includes mesh/actor resources, unpopulated strip offsets (0x6fee20), decal size/offset
(0x4fe620), billboard half-diagonal/offset (0x4f1750), audio BoxShape or MinDistance*10 (0x4eacf0), and
collision outer boxes when no visual exists or OverrideEntityAABB=true (0x652b00).

Against first shipped instances, resource-only bounds match 1122/1591 templates; the new resolver
matches 1184. There are 94 newly matching templates and 32 formerly matching boxes that differ.
Some are genuine template/instance property differences: Sword_Bronco's template trail reaches z=90,
while its instance trail/bounds reach about 80.47. Using each instance's actual properties matches
1233/1591. Particle runtime extents, actor poses/body parts and procedural trees still need work;
remaining mismatches are not all parser bugs. Unresolved boxes remain explicitly approximate.

53 independent workspace tests pass. `geometry_check` and `bounds_check` are read-only corpus
examples. `tools/test-placement.ps1` verifies OBJ, hierarchy preservation, ground/snap/plane behavior
and unchanged game-data/INI/DLL hashes; run it against the original six-entity nested/yaw test setup.

## Installed contact test — player/terrain and dynamic shield contact passed

Prepared 2026-10-15 11:09 with `tools/install-placement-test.ps1`, using the verified staging output
`build/tests/placement-20261015-110511/grounded.lrent`. Only the existing test layer changed:

- Board A: (-37450,88.86705,-5700), down 31.13295 cm, including a 2 cm inset.
- Board B: (-36850,144.67282,-5700), up 24.67282 cm, including the same inset.
- Barrel children followed; their local transforms/rotations were preserved.
- New round shield [6], GUID {5877E235-280B-4F03-AF10-F16DF4E82114}, at (-37450,188.86705,-5700),
  1 m above board A; original shield [1] retained.

User confirmed 2026-10-15 (recorded 11:29): boards touch terrain; can walk on boards/barrels and jump
onto barrels. Save/load breaks nothing. Horizontal overhang on the slope is accepted pending support
beams/custom models. The shield stays suspended but can be picked up; this does not confirm a falling
item's contact with the board. Its template has FROZEN flags, identified after this report.

At 11:32 `tools/test-physics.ps1 -Install` changed only shield [6] to `BodyFlag=0`, `WakeUpCounter=1`,
retaining its position, all geometry and other entities. CLI staging/refreeze checks and all 53 tests
passed. Other game data/INI/DLL hashes unchanged; game closed during installation. Backup:
`backup/world-edits/E362247A-B2D7-4236-B71B-A303A275C881`. Staging:
`build/tests/physics-20261015-113139/dynamic-shield.lrent` and `installed-test.json`.

**User confirmed 2026-10-15 11:47:** shield now rests on the board; save/load breaks nothing.
The dynamic shield/static board contact test passed. Pickup after this dynamic retest was not explicitly
reported; frozen pickup passed earlier. This result does not establish all collision-group interactions
or exact offline PhysX queries. Test files remain installed. No DLLs or save files were edited by the CLI.

Next scoped check: `snap` joins and deliberate partial embedding of static models (`--gap 0` and
negative gaps), preferably a support against/under the board; verify appearance, player collision and
save/load in game. Broader cooked PhysX decoding, skin deformation and slope alignment remain separate
future work. Current release CLI is dated 2026-10-15 11:29:51; 53 tests passed. Read TODO and the
development log before resuming; no further game edits or commit/push were made when recording this result.

Rollback, from risen-mods with the game closed:

```powershell
.\rust\target\release\risen.exe world-restore backup\world-edits\F981321D-300A-4CEE-9D31-BC69D3873A5A
```

First restore E362247A... to undo dynamic mode and return the seven-entity frozen setup; then
F981321D... before the previous E18FA613... nested/yaw backup. F981321D... returns the six-entity
floating-board setup. Installed entity details are in the staging directory's `installed-test.json`.

Snap test prepared 2026-10-15 13:26 via tools/test-snap.ps1 -Install: added independent static barrels [7] and [8] beneath board B [4], at (-36825,30.476822,-5800) with gap 0 and (-36825,38.47682,-5600) with gap -8 cm. Their lower portions intersect terrain; support [8] has only its upper portion exposed. Contact equations, collision metadata, unchanged existing non-root entities/properties, staging/installed byte identity, VFS readback and unrelated game-data/INI/DLL hashes passed. Only the test layer changed; backup/world-edits/E0FE4009-EBD9-4E0B-ADBC-43DC08FC7855 restores the seven-entity dynamic shield scene (restore this before E362247A...). Stage: build/tests/snap-20261015-132526/installed-test.json. Game launched 13:27 through Steam and process/log confirmed; NEW game visual join, player collision and save/load remain awaiting user confirmation. DLLs remain deployed builds of October 12-14; no rebuild/commit/push.

## Installed automatic climb-height ladder (2026-10-15, prepared 13:53)

`tools/test-climb-heights.ps1 -Install` runs the scratch-only Rust example
`climb_height_stage`, then installs through the existing CLI verified project transaction.
18 new static crate entities, 33 total; original 15-entity scene retained byte-for-byte outside
root/context aggregate bounds. All new collision PS flags IsClimbable=1; no animation selector,
character movement settings or player-coordinate changes. Release CLI remains 13:39:24.

| Crates | Entities | x | z | Visual edge height front/back, cm |
| --- | --- | --- | --- | --- |
| 1 | 15 | -37850 | -4900 | 59.8 / 97.2 |
| 2 | 16..17 | -37450 | -4900 | 131.9 / 178.6 |
| 4 | 18..21 | -37050 | -4900 | 274.8 / 320.8 |
| 5 | 22..26 | -37850 | -4500 | 314.6 / 453.6 |
| 6 | 27..32 | -37450 | -4500 | 410.9 / 466.1 |

Front/back samples are at the tower center x, 80 cm beyond the +Z/-Z render faces.
The original three-crate enabled/disabled pair stays at z=-5300. Grounding uses the highest
of the footprint center/corners minus 2 cm; static crates are stacked without visual face gaps.
The initial more western shoreline layout was refused in scratch before writing game files.

Coordinates are centimetres, Y up. Native DetectClimbMode obtains the character world matrix,
then subtracts character position Y from the traced landing position Y (e.g. Game RVA
0x8e6971/0x8e6977, 0x8e86e5/0x8e86eb). Runtime collider hit points, actual standing/in-air
character Y and approach clearance determine the result; the object's saved origin and stack
count alone are insufficient. The visual sample table documents this layout and is not an exact
prediction of PhysX contacts or the selected animation. A high tower may be unreachable from
one side but reachable from higher terrain or a jump on the other; record actual observations.

Checks passed: canonical scratch-only output guard, VFS terrain world transform, sample support,
box colliders and climbing flags, exact preserved old entity bytes, stack contacts, binary round
trip, dry-run, installed bytes/VFS readback and unrelated game-data/INI/DLL SHA256. Existing
55 core tests passed in the preceding step; core code unchanged here. Only test .lrent changed,
with verified backup/world-restore `backup/world-edits/DD3140F6-2622-474D-A64A-E24ADD6C3CD0`
(restore before FD581343...). Stage: build/tests/climb-heights-20261015-135238/installed-test.json
and heights.csv. Game launched via Steam for this check; DLLs remain deployed October 12-14
builds. No C++ deployment, commit or push.

**USER CONFIRMED (recorded 2026-10-15 14:02):** every new tower was climbable. The smallest
used a jump/vault animation; higher towers used climbing. The tallest could be climbed only
from one side at a particular approach position. Saving/loading changed nothing. Automatic
height-dependent movement selection, high-ledge climbing and persistence tests PASSED.
This establishes that the same permission flag supports different native climbing motions;
the exact numeric modes/animation asset names were not logged. The tallest result also shows
that a >400 cm offline visual walk-up sample is not a universal reachability cutoff. Runtime
approach position, character Y (including a jump) and physical hit geometry may explain it;
their individual contributions were not measured. No new changes to game files/DLLs were
made when recording this report. Test towers remain installed.

## Slope-aware ground command (2026-10-15, prepared 14:20)

```powershell
.\rust\target\release\risen.exe ground RisenMods_NewLayer_Test 2 --align --surface-layer Levelmesh_Landscape --gap -2 --dry-run
.\rust\target\release\risen.exe ground RisenMods_NewLayer_Test 2 --align --samples 9 --max-slope 30 --surface-layer Levelmesh_Landscape --out build\sloped.lrent
.\rust\target\release\risen.exe ground RisenMods_NewLayer_Test 2 --samples 9 --surface-layer Levelmesh_Landscape --out build\dense-only.lrent
```

`--align` enables dense grounding and replaces pitch/roll with a fitted surface tilt, preserving
horizontal forward heading and positive axis scales. `--samples N` (3..17) uses an NxN grid on
the actual local mesh's lowest vertical triangle intersections, plus projected model vertices;
all model vertices also participate in the final clearance checks. Align defaults to 7 per axis.
Samples outside the local render mesh are skipped; holes in required terrain support refuse.
Without align/samples the existing five-point, vertical-only behavior is retained.
`--samples` alone improves support sampling while retaining the basis. `--height` supplies a
horizontal plane and can level an already tilted object with align. Dense-mode final vertical
shift also respects max-drop/max-rise. Gap remains vertical centimetres, not along the normal.

A centered f64 least-squares height plane defines the up normal, then heading-preserving tangent
axes are rebuilt; terrain is resampled after tilt until stable (up to 12 iterations). Default
`--max-slope 45` refuses steeper fitted planes. Missing support, degenerate footprint, shear,
reflection, invalid/nonfinite inputs, nonconvergence and unsuitable rigid-body world bases refuse
before committing. Candidate surfaces conservatively cover the model's rotated extent; triangles
outside this area are excluded from repeated height queries. Surface selection still uses visual
static meshes; it does not decode cooked collisions or reproduce PhysX. Discontinuous/curved ground
can leave gaps under a rigid object even after alignment. Vertex/grid sampling cannot guarantee
all unsampled contacts or fit a curved surface exactly. Large meshes cost additional sampling work.

`Lrent::set_world_matrix` applies the selected root world pose, derives its local pose through
the actual parent's inverse, rebuilds descendants from their unchanged locals, updates own/aggregate
bounds and embedded navigation/rigid-body poses. Rounded parent homogeneous elements are retained.
Validation/commit rollback keeps invalid transformations atomic; physics flag policy is unchanged.

61 workspace tests passed (one ignored subprocess helper). New fixtures cover distant world
coordinates, yaw/axis-scale preservation, plane contact/negative gap, repeated alignment,
a bump missed by five-point support, dense mode without tilt, missing support, slope limits,
shear/reflection/invalid parameters and nested pose changes with rigid/nav synchronization.
Release CLI: 2026-10-15 14:15:39. tools/test-ground-align.ps1 also checked both board headings,
repeatability, child locals/local*parent=world, scale/properties, slope refusal without output,
unchanged other entities/data/INI/DLL, installed byte identity and named VFS readback.

Installed ONLY board A [2] + its attached barrel [3] (and required aggregate bounds) in the existing
33-entity test layer. Board A position (-37450,63.207546,-5700), tilt 5.766 deg, up normal
(-0.05390177,0.99494123,-0.08477335). Barrel world position
(-37364.24,85.34694,-5670.781); its local matrix is unchanged, so it leans with the board.
67 unique model/grid samples, requested gap -2 cm, largest sampled underside residual 8.816 cm
above the requested gap (absolute maximum clearance about 6.816 cm). Scratch-only board B check:
10.392 deg tilt, 20.320 cm residual; game board B, its independent supports, shields and all towers
retain their previous data. Verified backup/world-restore:
`backup/world-edits/C1C74A65-5796-444C-BCFD-5B781F514F31` (restore before DD3140F6...).
Stage: build/tests/ground-align-20261015-141825/installed-test.json.

Game launched via Steam at 14:20:14; process confirmed. Manual NEW game test pending: inspect the
inclined first board and its leaning barrel, terrain contact, walk/jump on board/barrel, save/load
and recheck. No game DLLs deployed; bin still has October 12-14 builds. No commit/push.

## Stronger visible slope follow-up (2026-10-15, prepared 14:28)

User feedback on the first aligned board: 5.766 degrees is too subtle to judge, perhaps slightly
leaning. Do not mark the first slope test fully passed; no walk/jump/save-load report was supplied.
A natural slope near the tall test towers was selected for a clearer test. Some scratch candidates
were too steep (>45 deg), too uneven (large residual) or visually too shallow; none were installed.

Installed new board [33] + copied child barrel [34] (35 total entities), via:
`spawn Obj_Furn_Plank_Board_01 --layer RisenMods_NewLayer_Test --to -37650 350 -4300 --yaw 90`,
then `ground <scratch> 33 --align --samples 9 --surface-layer Levelmesh_Landscape --max-rise 500
--gap -2`. Board final position (-37650,320.18042,-4300), tilt 29.72677 deg, normal
(0.092219315,0.8683999,-0.48721367), 93 samples, max residual above requested gap 10.256 cm
(absolute max clearance about 8.256 cm). Original boards/barrels/shields/towers remain unchanged;
the new barrel's original local pose is retained and it leans with the board. Slope comes from
existing terrain; no artificial terrain or character movement settings were installed.

`tools/install-steep-ground-test.ps1 -StagedLayer build/tests/ground-steep-20261015-142412/aligned-5.lrent`
verified old non-root entities/properties, unique GUIDs, child local pose and local*parent=world,
installed byte identity/VFS readback and unrelated game data/INI/DLL hashes. Game closed during
installation; only the test layer changed. Verified backup/world-restore:
`backup/world-edits/DE9ED2C4-A0CD-4E07-9E9C-FF415E9C94A9` (restore before C1C74A65...).
Stage and installed-test.json: build/tests/ground-steep-20261015-142412.
Core unchanged, prior 61 tests passed; release CLI 14:15:39, deployed DLLs October 12-14.
Game restarted through Steam (14:28:46, process confirmed). NEW game manual check pending:
extra board uphill beside tall towers, clear slope/leaning barrel, terrain contact, walk/jump,
save/load/recheck. No commit/push.

**USER CONFIRMED (recorded 2026-10-15 14:40):** stronger slope construction looks acceptable,
can jump onto it; save/load breaks nothing. The slope-alignment scene/contact/persistence check
PASSED, closing the slope-aware ground task. This acceptance does not establish exact cooked
collision contact or remove the documented sampled residual gaps. Test data remains installed.
No game files/DLLs changed when recording the result; no rebuild/commit/push.
