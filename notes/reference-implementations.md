# Reference implementations — "how is X done here"

Start here before writing a new hook, button, page or world edit: find the closest row, read the reference code,
copy its pattern. Every entry was verified in game on Steam Risen x64 Rev. 2045 unless marked otherwise.
Add a row when a new technique is confirmed (user-tested or log-verified) — one line of "how" + where + gotchas;
long findings stay in the topic notes linked here.

## Process and infrastructure
| Need | Reference | How / gotchas |
|---|---|---|
| New mod DLL | any `mods/<name>/main.cpp`, e.g. `mods/test-start/main.cpp` | Folder `mods/foo-bar` → `bin\mods\foo_bar.dll`, built by `build.bat`'s loop. `DllMain`: `ModInit(instance, "foo_bar")`, then `Log("Risen Foo 0.1")`. No README.txt = dev-only, `tools\package.ps1` skips it. |
| Log, INI | `shared/common.h` | One shared `bin\mods\RisenMods.log`, lines tagged `[mod]`. `IniPath(kDefaultIni)` creates `mods\<mod>.ini` from defaults; `IniInt/IniString/IniKey`; `IniChanged()` for hot reload (`mods/menu/main.cpp`). Never overwrite an installed user INI on deploy. |
| Run on the game thread | `mods/world-debug/main.cpp` (`SubclassWindow`, `WM_DEBUG_*`) | Subclass the game window, post `WM_APP + 0xNNxx` from a worker; each mod has its own 0xNNxx block (list in `shared/common.h`). Engine calls only from the window proc / script callbacks. |
| Keys, input capture | `shared/input_capture.h`, `IniKey` | Input capture blocks the game while rebinding (`mods/menu/native_page.cpp`, `ReleaseInputAfterRebind`). |
| ImGui overlay | `shared/d3d9_hook.h`, `mods/menu/overlay.cpp` | `Direct3DCreate9` IAT hook in Engine.dll; callbacks Init/Draw/BeforeReset/AfterReset. Game look via `shared/risen_style.h`. |
| F10 menu page for a mod | `shared/hub_client.h`, `shared/hub_api.h` | `HubGet()` after the game window exists; the page's items are INI keys, menu.dll writes them and calls `onChanged`. `HubBusy()` → skip hotkeys. |
| Crash evidence | `loader/`, `notes/crash-diagnostics.md`, skill `risen-crash-triage` | Crash lines with module+offset stack in the log, reports in `bin\mods\crashes\`. A FIRST-CHANCE line alone is no crash. |

## Hooks (no executable bytes patched)
| Need | Reference | How / gotchas |
|---|---|---|
| Replace an imported engine function | `shared/iat_patch.h` `PatchImport`; `mods/autoloot/focus_name.cpp` | Patch the importing module's IAT; keep the returned previous entry as "original" (chains with other mods). |
| Hook only ONE call site | `mods/test-start/main.cpp` (`CommandLine`, `PlayVideo`, `ExistsGameVariable`), `mods/world-debug/main.cpp` `CameraRotationInit` | IAT hook + `_ReturnAddress() == module + <rva after the call>`. Before patching, `memcmp` the call bytes (`ff 15 rel32`) and a neighbouring identifying instruction / string; on mismatch log "unexpected code (other version?)" and skip. |
| Find callers of a function | `python -I tools/pe_disasm.py ../bin/<dll> <rva\|export> [lines]`, `tools/pe_symbol.py` | Disassembly annotates imports and strings. For xrefs, scan the code section for `E8/E9 rel32` to the target (small script; not in tools yet). |
| Call a game script (ScriptCall) | `mods/autoloot/game.cpp` `ScriptCall` | `eCScriptProxyScript(bCString)` + `operator()(self, other, param)`; e.g. `CanOpenLock`, `AutoLoot`, `InvFilter_Loot`. |
| Strings | `??0bCString@@QEAA@PEBD@Z` (ctor), `??4bCString@@QEAAAEAV0@PEBD@Z` (assign), `?GetText@bCString@@QEBAPEBDXZ`, `??1bCString@@QEAA@XZ` | bCString = one pointer (allocate 16 bytes, aligned 8). Functions returning bCString by value take a hidden return pointer. `bCUnicodeString` the same for GUI text. |
| Change what New Game loads | `mods/test-start/main.cpp` | `GetDefaultWorldName` (Game +0x87bd70, only caller NewGame) parses `eCApplication::GetCommandLineA`; serve a copy with `-worldfile=<World>` at that call site only. Launch options can't do it (Risen.exe hard-codes `-worldfile=World`). |

## Native game GUI
| Need | Reference | How / gotchas |
|---|---|---|
| Own script names for GUI handlers | `scripts/risenmods/main.cpp` → `bin\scripts\Script_RisenMods.dll` | `ScriptInit` descriptor adds `RM_*` names; every call is forwarded to `RisenScriptEvent` exported by menu.dll. Add a name in BOTH `RM_SCRIPT(...)` and `kScripts`. Existing names win, so always prefix `RM_`. |
| Add a button / tab to a vanilla page | `mods/menu/native_page.cpp` `BuildTab` (options tab), `BuildTestButton` (DLG_MENU_GAME) | Find the page (`gCHudSystem2::FindPage(id)`, ids in `notes/ui-recon.md`), find the template by class + exact position/size (`FindTemplate`), `Clone(template, page)`, `SetOnClick(clone, "RM_...")`, `SetText`, re-layout with `Place` (1365-wide coordinates, `g_dx` for 4:3), `BringToFront`. Check the expected layout first, log and skip otherwise. Re-check every tick that the clone is still a child of the page (pages can be rebuilt) and re-set the text when the page is shown. |
| Wrap a vanilla button's action | `RM_OnClick_Settings`, `RM_OnClick_NewGame` in `native_page.cpp` | Repoint the vanilla button's OnClick to our `RM_` name, do our part, then `CallVanilla("<vanilla script>", self, other, args)`. Read the vanilla name with `OnClickName` and verify it before repointing. |
| Start a New Game without "Start a new game?" | `StartNewGame` in `native_page.cpp` | Vanilla `OnClick_NewGame` (Script_Game +0x1547e0) always asks on PC (`GUI2::ModalNewGame`); its yes-path is `GUI2::SetPageMode(7, true)` + `World::NewGame` (exports of Script.dll; `gui2` / `world` are data exports: GetProcAddress gives the object). |
| Own page inside a vanilla page | `native_page.cpp` (Mods tab → "Параметры игры" page in mods mode) | Hide the page's own controls, show cloned label + `gCSpinButton` rows (`RM_OnSpin_NN`, one name per row), give them back on leave. Don't hide a button inside its own click handler: queue the action for the next tick. Radio buttons of a group can fire a stray click on another one: debounce. |
| Inspect the live GUI tree | `mods/ui-lab/`, `notes/ui-tree-rev2045.txt` | Dev mod dumping pages/windows with RTTI class, id, position, size, text. |
| Dev-only UI | `BuildTestButton` + `mods/test-start/` | The UI shows only while the dev DLL is loaded (`GetModuleHandleA("test_start.dll")` + its export); release zips never contain it. |

## World, assets, data (Rust CLI `risen.exe`)
| Need | Reference | How / gotchas |
|---|---|---|
| Read game files the way the game does | `rust/risen-formats/src/vfs.rs`, `risen vfs`, `notes/vfs-rev2045.md` | Mounted VFS priorities, not "all PAKs". Loose overrides need `[Root] NoPhysical=false`. |
| Add/move/remove entities, layers | `risen layer-create`, `notes/world-editing.md`, skill `risen-world-edit` | Loose files under `..\data\`, backup in `backup\world-edits\<id>`, undo `world-restore`. `--dry-run` / `--out-dir build\tests\...` first. |
| Enable/disable world sectors (strip a world) | `risen sector-set`, `rust/risen-cli/src/project.rs` `plan_sectors` | Test world = `Testlevel_Plane` stripped to hero, plane, nav, sky (TODO 5.23). |
| Place on the ground / contacts | `notes/geometry-placement.md`, `tools/test-*.ps1` | cm, Y up; mesh and collider can differ. |
| Own models / collision | `risen asset-install` / `asset-restore`, `notes/asset-editing.md` | Both compiled mesh/collision catalogues must match; asset backups ≠ world backups. |

## Dev workflows
| Need | Reference |
|---|---|
| Build → deploy → launch → read log | skill `risen-deploy-test`; `tools/deploy-world-debug.ps1` |
| Start a quick test game | DLG_MENU_GAME → "Тест"/"Test" (needs `bin\mods\test_start.dll` + menu.dll): `[Test] World` from `test_start.ini`, no intro video, no wake-up scene. Normal "Новая игра" loads the island. |
| Object/camera/time controls in game | `mods/world-debug/`, `notes/world-debug.md` |
| RE patch procedure | skill `risen-re-patch` |
