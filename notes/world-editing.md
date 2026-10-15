# Separate world layers (Rust CLI)

Run from `risen-mods`. The CLI finds the game in parent directories, or use `--game <folder>`.

Latest work: [geometry, collision metadata, ground/snap and the installed contact test](geometry-placement.md).

```powershell
.\rust\target\release\risen.exe world-sectors World --json
.\rust\target\release\risen.exe projects Game_Start_Item --json
.\rust\target\release\risen.exe world-data-check
```

`world-sectors` reads the `.wrldatasc` referenced by the world's `.wrl`, or an explicit `.wrldatasc` file.
The lossless parser preserves original comments, section order, line endings and unrelated data.
Its add/update/remove methods affect only `[Sector.List]`.

## Create a separate layer

```powershell
.\rust\target\release\risen.exe layer-create My_Items --dry-run
.\rust\target\release\risen.exe layer-create My_Items --out-dir build\my-items
.\rust\target\release\risen.exe layer-create My_Items --spawn It_Shield_Round --to -36950 200 -5100
```

Names: 1..64 ASCII letters, digits or underscores, starting with a letter or underscore. Windows device
names and the engine's special `SysDyn*` names are refused, as are existing mounted sector/layer names.
`--world` defaults to `World`; `--like` defaults to `Game_Start_Item`. The donor provides this installation's
layer/context/sector header versions and properties. Its world entities are discarded, the DRE receives a
new GUID, and unused strings are removed. A donor with a nontrivial root or unreadable header is refused.

Creation writes three files:

- `data/common/projects/World/RisenMods/My_Items.lrent` — new root, optionally the first entity;
- `data/common/projects/World/RisenMods/My_Items.sec` — enabled persistent sector referencing that layer;
- `data/common/projects/World/World.wrldatasc` — original entries plus `My_Items=true`.

The world's `.wrl` is retained. If a corresponding `_local.wrldatasc` is mounted, it is also updated so the
new sector is enabled there. For another world, the new sector/layer directory follows that world's `.wrl`.
Other edits/spawns can target the new layer by name. `--out-dir` creates the same paths under a staging root.
`--dry-run` writes nothing. Direct installation requires `NoPhysical=false` in `bin/mountlist_packed.ini`;
the CLI does not change that setting. Remapped project mount paths are currently refused.

All output is parsed before writing. `layer-create` verifies backups in `risen-mods/backup/world-edits/<id>`
before changing files, writes each file atomically and rolls back completed writes if a later write fails.
The process guard checks the full game executable path: the CLI's `risen.exe` has the same basename as the game.

```powershell
.\rust\target\release\risen.exe world-restore 'backup\world-edits\<id>'
```

Restore keeps old loose files exactly or removes new overrides to expose their archive originals. It checks
all affected files before restoring and refuses subsequent edits unless `--force` is supplied. Close the game
before installation or restoration. The backup manifest and before/after snapshots are retained.

## Current in-game test

`tools/test-new-layer.ps1 -StageOnly` constructs and restores a staged layer, without modifying game data.
Without `-StageOnly`, it then installs `RisenMods_NewLayer_Test` with a round shield at `(-36950, 200, -5100)`
near Sara. It checks the layer's root/item, unchanged original sector entries and SHA256 of unrelated loose
project files. It prints the backup and restore command. DLLs and save files are not modified by the script.

Prepared 2026-10-15 09:13; **in-game test passed**, confirmed by the user on 2026-10-15. The shield appeared
on the ground and stayed there after save/load. Pickup worked (inventory screenshot supplied), and it remained
in inventory after another save/load. This verifies loading the new independent layer and item persistence.
Existing saves loading a newly registered sector are a separate check.

Installed backup: `backup/world-edits/B40E0E21-4562-4710-9884-73E01506F057`.
Shield GUID: `{01FC69BC-9A93-4219-BD9A-4E6DF55E7470}`.

## Nested moves and spawn heading

`move --by` and `move --to` use world coordinates for any nested entity. The subtree moves together;
the selected entity's local translation is adjusted through its parent's inverse basis. Descendant local
matrices stay unchanged. Physics/navigation positions and ancestor/context geometry bounds are updated.
Rounded homogeneous matrix elements in shipped doors are preserved. Extra authored/cached geometry
volumes are kept conservatively when they differ from the normal own-box + child-box union.

```powershell
# Scratch outputs only; these commands do not install the edits.
.\rust\target\release\risen.exe move Swamp_Outdoor_Dyn 9 --by 64 -32 16 --out build\nested.lrent
.\rust\target\release\risen.exe spawn It_Axe_Ogre --layer Game_Start_Item --from-template --to -36950 200 -5100 --yaw 90 --out build\yaw.lrent
```

`spawn --yaw <degrees>` sets an absolute world heading about +Y: 0 faces +Z, +90 faces +X. Negative angles
and angles outside 0..360 work. Pitch, roll and scale are retained; children turn about the root's position.
The option applies to existing-instance copies, cross-layer imports and template construction, including
explicit `.tple` files and every entity made by `--count`. Without it, the original rotation is retained.
World/local matrices, shape/subtree bounds and both rigid-body motion quaternions remain synchronized.
Malformed hierarchies, singular parents, nonfinite input and geometry overflow are refused without a
partial edit. A rigid body with shear or reflected scale is refused when setting yaw.

`tools/test-transforms.ps1` checks nested --by/--to, rounded door matrices and all spawn paths using
`build/tests/transforms` outputs; game data/INI/DLL SHA256 must remain unchanged. It uses the installed
`RisenMods_NewLayer_Test` shield layer and optionally the new club template from previous confirmed tests.
The in-game check below passed visual loading and save/load, confirmed by the user.

### Passed nested move/yaw test (2026-10-15)

`tools/test-nested-yaw.ps1 -StageOnly` stages and verifies restoration without changing the game.
Without that switch it installs into the existing shield-only `RisenMods_NewLayer_Test` layer.
The source is the shipped Monastery_Dyn plank #1625 with child barrel #1626 (static decoration).
Only the test `.lrent` changes; the original shield, sector/world registration and all DLLs remain intact.

- Group A: plank at `(-37450,120,-5700)`, barrel moved 140 cm along the plank's local X using nested `move`.
- Group B: `spawn` copies that modified group to `(-36850,120,-5700)` with absolute `--yaw 90`.

Start a **new game** and look near Sara for two planks with a barrel near one end of each. The planks
should point in different directions, roughly perpendicular. Save/load and check that both arrangements
remain. These barrels are scenery and are not intended for pickup. The user confirmed both planks with
barrels on 2026-10-15 10:15; the supplied screenshot shows the different group orientations. Visual loading
passed. At 10:17 the user confirmed that everything stayed in place after save/load: the moved nested
barrels and differently oriented groups persist correctly. Test data remains installed.

From `risen-mods`, with the game closed:

```powershell
.\rust\target\release\risen.exe world-restore backup\world-edits\E18FA613-B903-4627-B806-F4FAC3365E1C
```

This restores the shield-only layer. Restore this newer test before using the older layer-creation backup.
Installed entity details: `build/tests/nested-yaw-20261015-100926/installed-entities.json`.

## Verification

50 tests pass: geometry/placement/collision/bounds, transforms, VFS, binary world/sector records, lossless text edits and multi-file backup/rollback
(including partial write failure, later edits and process-path matching). The process test launches its ignored helper.
All 5 shipped `.wrldatasc` files round-trip exactly. `layer_create_check` builds empty layers from all 410
shipped donors, verifies unique new root GUIDs, finite bounds, readback and exact first-use string tables.
`transform_check` covers every one of the 902 nested entities across those layers, plus representative
top-level objects/parents/rigid bodies: 1686 subtree moves and 1686 yaw edits pass with matrix consistency,
binary readback and unrelated data preserved. Source layers are never written. `--census-only` prints
the original matrix/bounds findings without running edits.

```powershell
# From risen-mods/rust, with the configured Rust environment:
cargo test --workspace
cargo run -p risen-formats --example layer_create_check -- ../../data/common/projects.pak
cargo run -p risen-formats --release --example transform_check -- ../../data/common/projects.pak
```
