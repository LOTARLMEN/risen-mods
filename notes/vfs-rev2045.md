# Game VFS — Steam Risen x64 Rev. 2045, 2026-10-15

`risen-formats/src/vfs.rs` resolves effective files from `bin/mountlist_packed.ini`.
It indexes directory metadata only; archive payloads are read/decompressed on demand.

## Verified loading rules

- `RootPath` is relative to the mountlist folder (`bin`), normally `..`.
- The engine processes `Bin`, `Physical`, then `Packed` sections. A line can map a
  virtual prefix to a physical path (`ini=data/ini`). `$(localappdata)` and other
  environment variables are expanded. `speech_*` expands matching archive/directory names.
- `NoPhysical` and `NoPacked` control directory/archive sources in `Packed`.
  They do not disable explicitly mounted `Physical` or `Bin` directories.
- A packed mount loads its `.pak`, then contiguous `.p00`, `.p01`, etc. A missing
  base prevents the chain from loading; a missing numbered volume ends the chain.
- For the **same full virtual path**, the later archive wins, including when its
  entry FILETIME is older or equal. Loose files override packed files.
- New loose files participate in enumeration and lookup even when the base archive
  has no entry for them. Unmounted archives and loose directories are ignored.
- Current installation: 23 mounted archives out of 25. `data/common/strings.pak`
  and `data/common/gui2_back.pak` are not mounted by the game's list. The compiled
  string tables provide Russian/Polish columns.

Evidence: Engine.dll `eCVirtualFileSystem::Mount` RVA `0x734440`,
`ProcessMountList` `0x734bd0`; FFC.dll `MountPackFileChain` `0x38be0`.
`tools/vfs-native-probe.py` also checks actual FFC/FileSystem2 behavior on generated
archives in `build/tests/vfs-native`, in a separate process. It preloads Windows'
system `version.dll` before game libraries so the mod loader is not invoked.
Six priority cases pass with the native cache disabled and enabled, including an
older patch, equal dates, loose overrides and a gap in the chain.

## Lookup and scope

Virtual paths use `/` internally, accept `\` and `#G3:/`, and compare ASCII case
insensitively. A basename must be unique within its requested resource scope.
Ambiguous names require a full path; `..` traversal is rejected.

The native **global basename cache** can arbitrate files at different virtual
paths. This implementation deliberately uses explicit scopes and refuses
ambiguity; it does not claim to reproduce that cache's separate conflict rules.
Wildcard aliases with different source/destination patterns and unknown mountlist
sections fail explicitly rather than being silently interpreted.

## CLI

Run from `risen-mods` (or pass `--game <game folder>`):

```powershell
.\rust\target\release\risen.exe vfs It_Axe_RisenMods_Test --json
.\rust\target\release\risen.exe entities Game_Start_Item --json
.\rust\target\release\risen.exe find It_Axe_RisenMods_Test --json
.\rust\target\release\risen.exe templates It_Axe_RisenMods_Test --json
.\rust\target\release\risen.exe spawn It_Axe_RisenMods_Test --layer Game_Start_Item --to -37000 200 -5100 --dry-run
```

`layers`, `entities`, `find`, `templates`, edits, template copying, spawning,
localized tables and mesh-bound lookups use effective mounted data. Default edits
write to the matching physical override path, also for aliased mounts. With
`NoPhysical=true`, named edits require `--out` instead of overwriting ignored loose
files. Explicit local `.lrent`/`.tple` paths remain available.

`pak-list`, `pak-extract`, archive round trips, string walking and world-format
corpus checks continue to inspect original archive data.

`delete` scans its edited layer in memory and skips the original file using its
canonical path. Other-layer reference scans use VFS; naming the same layer through
an explicit path no longer causes a false reference warning.

## Checks

```powershell
# From rust/; set RUSTUP_HOME / CARGO_HOME as described in TODO if needed.
cargo test -p risen-formats --test vfs
```

Eight independent G3V0 fixture tests cover loose/new files, case and separators,
archive precedence/chain gaps, unmounted/orphan archives, flags and physical
mounts, wildcards, ambiguity, aliases/write destinations and traversal rejection.

Real installation checks also passed: named layer JSON equals the explicit loose
file; the new club and its creator template are found; spawning by the new name
uses the template or an existing loose instance; deletion into scratch files
produces no false reference warning. SHA256 of all loose `.lrent`, `.tple` and
`.tab` game files remained unchanged throughout these CLI checks.
