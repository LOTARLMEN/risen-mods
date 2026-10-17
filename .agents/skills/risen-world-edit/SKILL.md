---
name: risen-world-edit
description: Change the Risen world or resources with the Rust CLI (risen.exe) - layers, entities, ground/contact placement, custom meshes/colliders - through dry-run, staging, transactional install and restore. Use for any edit under ../data (lrent/sec/wrldatasc/tple/._xmsh/._xcom/mesh caches) or rolling such edits back.
---

# World and asset edits

Details: `notes/world-editing.md`, `notes/vfs-rev2045.md`, `notes/geometry-placement.md`, `notes/asset-editing.md`.
CLI: build `cargo build --release` in `rust\` (GNU toolchain on D:, see repo AGENTS.md), run
`.\rust\target\release\risen.exe --help` from the repo root. Units: centimetres, Y up.

## Rules
- The game reads the mounted VFS. Resolve names with `risen vfs` / `risen-formats::vfs`; an ambiguous short
  name must refuse. Edits are loose overrides under `..\data\`; original PAKs are never modified.
- Loose files load only with `[Root] NoPhysical=false` in `..\bin\mountlist_packed.ini`. The CLI never
  changes it; back the file up before changing it yourself.
- Writing only while `bin\Risen.exe` is closed (check by full path; skill `risen-deploy-test` step 2).

## Workflow
1. Inspect: `entities <layer>`, `vfs`, `contact ... --json`, `ground ... --dry-run --json`.
2. Stage: run the mutating command with `--dry-run`, then with `--out`/`--out-dir` into `build\tests\<task>-<time>\`.
   A mutating command without `--out` may write into the game - check its help first.
3. Validate the staged result: parse back, byte-identical round trips for untouched data
   (`lrent-check`, `tple-check`, `projects-check`, `world-data-check`), relevant `tools\test-*.ps1`.
4. Install transactionally:
   - world files: `layer-create` / scene installers -> `backup\world-edits\<id>` with manifest;
   - meshes/colliders/caches: `asset-install <bundle.json> [--dry-run]` -> `backup\asset-edits\<id>`.
     `compiled_meshes.bin` and `compiled_collision_meshes.bin` must both match the installed resources.
5. Verify: readback, SHA256 of unrelated DLL/INI/data unchanged. Report backup ids.

## Restore
- `risen world-restore 'backup\world-edits\<id>'` / `risen asset-restore 'backup\asset-edits\<id>'` - scopes differ,
  never cross them.
- Backups chain: restore the NEWEST first. Later edits block a restore; find out why instead of adding `--force`.
- Current chain is recorded in TODO.md / DEVELOPMENT_LOG.md (e.g. "restore before ...") - follow it.

## Placement limits
ground/contact/sweep are geometric checks on serialized poses, not PhysX simulation. Respect native
scale limits (uniform positive scale; triangle meshes can't be scaled). Mesh and collider can differ.

## Hands-on
Automated checks are not a pass. Ask the user to check look, walking/jumping/collision and save/load
(new game when the change affects saved state) and record exactly what they confirmed.
