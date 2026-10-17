# Landscape and custom model feasibility — 2026-10-16

The offline resource path works: four newly authored static render meshes, matching cooked triangle
collisions and appended resource cache headers. The x64 game loaded/rendered the custom models, but
the first visual test exposed inverted faces and unwanted clearance. A repair is installed below;
the corrected appearance, player collision and save/load await user confirmation. This is a research
probe, not a general importer/editor.

| Operation | Evidence and remaining work |
|---|---|
| Resize an existing instance | Uniform native model/collider scales already used in the confirmed height-correction scene. A public scale command is still absent. Nonuniform entity transforms cannot be assumed to scale physics correctly. |
| Resize the asset itself | Probe stretches a wedge to 300 x 40 x 200 cm by baking coordinates, with freshly generated normals, bounds, cache header and cooked collision. Entity scale can stay 1, avoiding the native triangle-collider scaling restriction. Engine test pending. |
| Cut/trim a model | The probe constructs the closed result of cutting its planar wedge at z=100 cm. This special case is also an affine shortening; it does **not** implement arbitrary clipping, cap triangulation, UV interpolation or cutting original assets. Those need an editing/import pipeline. |
| Add a custom static model | A new 200 x 80 x 200 cm wedge has its own ._xmsh, ._xcom and cache name, reusing one native material. Render triangles match physical triangles. This does not yet import an arbitrary OBJ/Blender file or add new materials/textures. |
| Change landscape geometry | A separate 200 x 200 cm, 3 x 3 grid has its centre raised 35 cm; both visual and physical surfaces agree. This proves generation/cooking of a terrain-like triangle resource, **not** editing the installed island. Original landscape seams, material blending, LOD, bounds and navigation remain to investigate. |

## Dependencies found

- Visual resources use `GR01MS02` and an inline `eCMeshResource2` object. Submesh properties
  reference actual `._xmat` materials; a texture path in an OBJ does not define the engine material.
  Probe material `Arch_Comp_Housestuff_03_Diffuse_01._xmat` is mounted in the installed VFS.
- `compiled_meshes.bin` is a `GENOMFLE` archive with a string table. Body: `GH04`, u32 count;
  per entry: revision (`MS02`), accessor property object, string-table reference to resource stem,
  u32 flags and two 8-byte times. All 1666 original entries use `MS02`.
  Rev. 2045 native load: `eCResourceCache2::LoadResources` at Engine+0x686690;
  entry read at +0x686882..0x68692f. SaveResources entry write at +0x686267..0x6862ed.
  Both cached and inline headers contain submesh counts/bounds; update both alongside raw geometry.
  Native `eCSubMesh::OnRead` +0x5ca350 confirms the submesh tree class-data layout.
- Original cache object/body/archive round trip is byte-identical. The appended cache has
  1670 headers and preserves every original entry byte and original string-table index.
  This establishes serialization; actual engine resource registration/rendering is still pending.
- Cooked triangle collision cannot be safely modified by replacing just vertex bytes: it contains
  bounds and acceleration data. The available `PhysXCooking.dll` can cook a newly authored mesh
  in a separate x86 process. Its output was accepted by the installed editor's native PhysX 2.8.4
  core and decoded independently by the project's Rust XCOM reader. No old DLL is loaded into Risen.
- Collider cooking uses metres; visual geometry, bounds and CLI coordinates use centimetres, Y up.
  No axis swap is applied to the already native coordinates. Triangle winding is preserved.
- Native triangle colliders reject non-unit instance scale (Engine+0x65774d, see geometry-placement.md).
  Baked geometry + fresh cooking is a promising route for stretching, including nonuniform changes;
  do not generalize the existing uniform primitive/convex checks to arbitrary entity transforms.

## Reproduce the scratch-only check

From repository root, with the existing release CLI built:

```powershell
.\tools\test-mesh-assets.ps1
# Optional alternate independent PhysX 2.x installation:
.\tools\test-mesh-assets.ps1 -PhysXDirectory 'D:\RisenModding\ed_new\Risen Editor'
```

The runner extracts the effective VFS donor/cache, builds the x86 cooking/reference helpers,
generates the resources, compares native geometry with authored triangles, and runs two Rust
examples. It verifies before/after SHA256 of installed DLL/INI/world/resource/cache files.
It neither installs resources nor launches the game. Output: `build/tests/asset-probe-<time>/`.

- `probe-mesh-assets.py`: explicitly limited procedural format/cooking probe; OBJ source exports,
  `.input` metre-based cooking input, `._xmsh`, matching cache, final `._xcom` wrappers.
  Stage must be a child of this repository's `build/tests`; generated assets use exclusive creation.
- `mesh-cook-check.cpp`: calls the installed cooking DLL exports in an x86 offline helper;
  validates binary input sizes/indices/finite coordinates. `build-mesh-cook-check.bat` builds it.
- Existing `xcom-native-check`: separately loads native PhysX and produces reference buffers.
- `mesh_cache_check`: original/appended object/body/archive byte-identical round trips.
- `mesh_asset_check`: reads both mesh formats; matches render/native/Rust triangles, every native
  vertex/index/stream boundary, local bounds and independent expected top heights 40/20/20/35 cm.

Final successful check: `build/tests/asset-probe-20261016-095609/result.json`.
Four resources, 32 authored/native triangles. Original 1666 cache headers + 4 new headers.
All installed game DLL/INI/world/resource/cache SHA256 unchanged. No scene installed, game not
launched, no hands-on confirmation. Release CLI remains build 2026-10-16 07:40:06;
installed loader 1.2 diagnostics remains 08:50:40, World Debug 0.1.2 remains October 15 18:54:53.

## Next test and implementation boundary

1. Independent layer/sector and asset transaction are now implemented and installed (below).
   Existing `world-restore` retains its project-only scope; use `asset-restore` for asset backups.
2. Start with a new game: inspect visibility/materials/face culling and walk/jump across the ramp
   and raised patch; then save/load. Repeat with an existing save separately. Native PhysX success
   alone does not establish compatibility with the game's runtime or renderer.
3. After the engine test, move supported serialization/editing into risen-formats and expose a
   staged CLI importer. Preserve normals/tangents, UVs, vertex colours and submesh material groups
   for original assets. General clipping needs intersection vertices and closing caps.
4. For the original island, map paired render/collision tiles, shared boundary vertices, material
   blends (including vertex colours/alpha), LOD and navigation dependencies before modifying tiles.
   Animated models (`._xmac`), skeletons and skinning require a separate pipeline.

## Separate scene installed — hands-on check pending

The original placement below was superseded by the isolated retest at 10:55 (see next section).

2026-10-16 10:20: installed `RisenMods_Assets_Test` (DRE + four custom static entities), sector
and world registration. Thirteen loose files total: four visual meshes, four collision meshes,
both resource caches, .lrent/.sec/.wrldatasc. Existing `RisenMods_NewLayer_Test` (45 entities)
is byte-identical: SHA256 `588077C82053DD11E087C1D95B1E45D4E1EC76D2B26AB532170CB75C184F7C38`.
All unrelated DLL/INI/world/resource/cache hashes unchanged; source PAK archives untouched.

New finding: physics has its own `compiled_collision_meshes.bin`, same GH04 archive/entry layout,
revision `CM00`, class `eCCollisionMeshResource2`. All 637 original headers round-trip identically;
new cache has 641 headers. Preserve original bytes/indices and append four matching physical headers.

| Object | X | Y | Z | Dimensions, cm |
|---|---:|---:|---:|---|
| Ramp | -37400 | 64.57919 | -6200 | 200 x 80 x 200 |
| Stretch | -37000 | 75.348495 | -6200 | 300 x 40 x 200 |
| Trim | -36600 | 67.85452 | -6200 | 200 x 40 x 100 |
| Terrain patch | -36200 | 45.200676 | -6600 | 200 x 35 x 200 |

Sites near the starting beach were checked against original test-object bounds and 25 physical
terrain samples each. Each base is 20 cm above the highest sampled ground beneath its footprint;
the raised edges are intentional so the new resources remain visible. This is not a grounding/contact
test and not an edit of the island. Jump onto each surface and check walking, both sides and save/load.
All entity matrices have unit scale, fresh GUIDs and a single enabled native v74 triangle collider.
Render and physical top heights agree after serialization/world transforms.

Stage: `build/tests/asset-probe-20261016-095221/`;
installation evidence: `bundle-20261016-102009/installed-test.json` and installed-entities.json.
Backup: `backup/asset-edits/27976494-684B-46D1-910B-A95331CDA177`.
Before the hands-on test, eight current save files were copied and SHA256-verified under that backup's
`savegames-before-manual-test/` (separate savegames-manifest.json); originals were not modified.

```powershell
# Game must be closed. Refuses subsequent edits unless --force is explicitly selected.
.\rust\target\release\risen.exe asset-restore 'backup\asset-edits\27976494-684B-46D1-910B-A95331CDA177'

# Reproduction with a fresh successful tools/test-mesh-assets.ps1 stage:
.\tools\install-mesh-assets.ps1 -Stage 'build\tests\asset-probe-<time>'
.\tools\install-mesh-assets.ps1 -Stage 'build\tests\asset-probe-<time>' -Install
```

Restore the installed probe before generating another one: resource/cache names are intentionally
unique, and duplicate names/new sectors are refused. Saves are backed up separately; asset-restore
does not restore or delete save files.

The script prepares a bundle, exercises the real installer/restorer twice in an isolated game-shaped
tree, then optionally installs. It refuses stale VFS cache donors and snapshots every actual loose
preimage (including absence). CLI `asset-install <bundle.json> [--dry-run]` validates all supplied
formats and restricts destinations to project files, static mesh/collision resources and those two
specific caches. Bundle source paths cannot escape their directory or use reparse points. Before
writing, the current loose state must still match the staged baseline; verified snapshots/manifest
are written first. Installation failures roll back, and restore failures reinstate already-restored
files. Restore preflight refuses subsequent changes before touching any target. The legacy world
backup format remains readable; world/asset restore scopes cannot be mixed accidentally.

Validation: 107 workspace tests initially passed (one helper ignored); after adding restore-failure
coverage, all 10 active CLI tests passed again (108 active workspace tests total, unchanged library
tests). Release CLI built without warnings at 10:19:10. Final 13-file isolated install/restore and
installed VFS/parse/SHA256 checks passed. Visual and collision cache corpus round trips passed.
DLLs in bin unchanged: loader 1.2 diagnostics at 08:50:40, World Debug 0.1.2 at October 15 18:54:53;
all installed INIs preserved (startup reports freecam speed 819.20 cm/s).

Steam launch: game PID 17768 at 10:21:16; loader trace hooks/dump worker and World Debug APIs resolved,
main menu `DLG_MENU_MAIN` confirmed in log at 10:21:25, no new exception report in the inspected startup.
Previous logs preserved under the installation bundle. Startup does not prove world-resource rendering;
new-game/existing-save visibility, player collision and save/load remain awaiting user confirmation.
No mod DLL rebuild/deploy, package, commit or push.

## Isolated retest after confusing the old towers with the new assets

User screenshot/report 2026-10-16: old crate towers visible, can jump onto/walk on them, fourth new
model not identified. The screenshot depicts the previous climbing test; this is **not** confirmation
of custom mesh visibility/collision. The user confirmed closing the game. The 10:49:56 shutdown
log does show all four custom GUIDs/names/positions in the native layer: entity loading is confirmed,
rendering/interaction remains pending. A FIRST-CHANCE SharedBase+0x7caa9 at shutdown was preserved
with that log; the user's normal exit is not labelled a confirmed new crash or a crash fix.

At 10:55, `tools/isolate-mesh-assets.ps1 -Install` disabled `RisenMods_NewLayer_Test` in World.wrldatasc
and its .sec (enabled byte/startup flag); the original 45-entity .lrent remains byte-identical. Four
custom models were moved into the cleared old tower area, retaining GUIDs/unit scale/collider state.
`ground --collision --surface-layer Levelmesh_Landscape_COL --samples 7 --gap 20` sets heights with
sampled clearance; no continuous contact or flat-edge-to-ground claim.

| Object | X | Y | Z |
|---|---:|---:|---:|
| Ramp | -37850 | 178.98422 | -4900 |
| Stretch | -37450 | 230.23862 | -4900 |
| Trim | -37050 | 258.69183 | -4900 |
| Terrain patch | -37450 | 289.43353 | -4500 |

Only three files changed: old sector .sec, world sector list, custom-assets .lrent. All resources,
caches, installed DLL/INI and other layers unchanged. Parsed/VFS readback, GUID/scale/active triangle
collider checks, full staged/installed file equality and unrelated hashes passed. The new backup must
be restored **before** the original 13-file asset installation backup, because they share registry/layer state.

```powershell
# With the game closed, restore the cluttered original test arrangement:
.\rust\target\release\risen.exe asset-restore 'backup\asset-edits\F6C70FF8-01B9-47B5-9341-AEC311B40972'
```

Stage/evidence: `build/tests/asset-isolated-20261016-105433/installed-test.json`.
Previous launch log and shutdown exception text preserved there. Source templates, save files,
loader/World Debug DLLs and the existing release CLI were not changed/rebuilt. Steam relaunch requested
at 10:56; first manual check should use **New Game**, excluding a saved old sector state.
Expect three low wedge shapes plus one raised patch, rather than stacks of crates. Visibility,
walking/jumping and subsequent save/load for these custom models still need user confirmation.

## Inverted render faces repaired — 2026-10-16 11:04

The second user screenshot identifies the actual custom models, but their exterior top/side faces
are missing and they look hollow/transparent. The initial visual test FAILED. Earlier triangle
comparison intentionally ignored winding when matching surfaces, so it did not detect this renderer
convention error; it must not be cited as evidence of correct rendering.

Independent measurement of the native board donor: all 60 nondegenerate triangles have geometric
cross-product opposite to the stored outward vertex normals (normalized dot approximately -1).
All 32 authored render triangles had dot +1. Fixed XMSH indices now follow the shipped convention;
outward normals, all vertex attributes, materials/bounds/headers are unchanged. PhysX retains its
authored winding and geometry. Render and physical formats have different index conventions.

`mesh_asset_check` now checks this orientation against the native donor in addition to comparing
render/physical triangles, native coordinate/index/stream readback, bounds and expected surface
heights. All four fixed files pass; the old faulty fixture is explicitly rejected. The corrected
procedural generator reproduces all four installed repairs byte-for-byte. Resource cache headers
remain valid because only raw visual indices changed; both caches and all physical resources are
byte-identical to their previous installed versions.

`tools/fix-mesh-assets.ps1 -Install` prepared/applied a five-file bundle after the user closed the
game: four XMSH index buffers and the custom .lrent. No horizontal moves or GUID/scale/PS changes.
Placement now uses `ground --collision --check-contact --correct-height --samples 9 --gap -2`
against physical landscape: conservative full-geometry vertical correction, then intentional 2 cm
embedding. The former +20 cm clearance is removed. New Y values: Ramp 156.98653, Stretch 208.24048,
Trim 236.69362, Terrain 267.43765. Local dimensions/X/Z remain as in the isolated layout above.
Final contact reports show overlap with terrain as expected for negative gap; this is not a claim
of uniform contact over an uneven terrain or a general penetration-depth solution.

Stage/evidence: `build/tests/asset-faces-20261016-110403/installed-test.json`, ground-1..4 reports,
old-winding-refusal.txt and independent check outputs. Installed bytes match staging; unrelated
DLL/INI/world/sector/cache/physical-resource SHA256 unchanged. Backup chain (restore newest first,
game closed): `009F8EB3-0CB7-48B7-8486-EE6C7B6813FC`, then `F6C70FF8-01B9-47B5-9341-AEC311B40972`,
then `27976494-684B-46D1-910B-A95331CDA177`, all under backup/asset-edits and using asset-restore.

Steam relaunch PID 23840 at 11:06:12; main menu confirmed 11:06:20. Repeat using **New Game** so a
saved old entity height does not override the repaired .lrent. Corrected visuals/walking/save-load
remain pending. Loader/World Debug DLLs and release CLI builds are unchanged. User explicitly
authorized the agent to close Risen itself after future completed user tests; that preference is
recorded in AGENTS.md. No package/commit/push.

## Primary references

- [Baltram's Risen static mesh documentation](https://www.baltr.de/rimy3d/help/en/_xmsh.htm):
  material naming, compiled cache registration and terrain vertex colours. Its console regeneration
  workflow depends on legacy Script_Extensions; do not assume it works on the x64 re-release.
- [rmtools XMSH writer](https://github.com/Baltram/rmtools/blob/5525421bf4b22636259bdc0d250ee96a5abcae66/mimicry/source/Mimicry/mi_xmshwriter.cpp),
  [XCOM writer](https://github.com/Baltram/rmtools/blob/5525421bf4b22636259bdc0d250ee96a5abcae66/mimicry/source/Mimicry/mi_xcomwriter.cpp),
  [cooking interface](https://github.com/Baltram/rmtools/blob/5525421bf4b22636259bdc0d250ee96a5abcae66/mimicry/source/Mimicry/mi_cooking.cpp).
  Downloaded reference sources remain under gitignored build/recon; the probe is independent code.
