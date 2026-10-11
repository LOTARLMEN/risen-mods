# Risen (2023, x64) mods — TODO

Target: Steam Risen, 64-bit THQ Nordic re-release (`D:\SteamLibrary\steamapps\common\Risen`).
Project lives in `Risen
isen-mods` (inside the game folder). Approach: native C++ DLL loaded into `Risen.exe`, calling functions exported by the game DLLs
(`Engine.dll`, `Game.dll`, `bin/scripts/Script_Game.dll` export thousands of named C++ symbols).

Goals:
1. **QuickLoot + AutoLoot** — like Gothic's AutoLoot by Hedin: loot nearby corpses/chests with one key,
   auto-pick up items from the ground, no pickup animation.
2. **Speed Hack** — like Gothic's Z-mod speedup: toggle game speed by hotkey, multiplier configurable in `.ini`.

Layout (since 2026-10-11): `loader/` builds `bin\version.dll` (proxy of the system version.dll) which loads every
`bin\mods\*.dll` in alphabetical order from its DllMain. One folder per mod in `mods/` -> `bin\mods\<mod>.dll`,
settings `bin\mods\<mod>.ini`, log `bin\mods\<mod>.log`. Shared code in `shared/` (log, ini, keys, game window, IAT).
`build.bat [deploy]` builds everything; `tools\package.ps1` makes `dist\Risen<Mod>-<ver>.zip` for every mod with a README.
Mods: fast-start, window (borderless + cursor), speed (F4), startup-trace (developer only, not deployed/packed).

## Faster game startup — DONE 2026-10-11: window at 9.7 s instead of 32.3 s
Measured with `mods/startup-trace` (copy `startup_trace.dll` into `bin\mods` -> `bin\mods\startup_trace.log`: DLL loads with call stacks,
main-thread stack samples every 50 ms, FFCore file I/O stats). Findings:
- ~17 s: FileSystem2.dll reads .pak archives with ~8M ReadFile calls of 1-64 bytes, each after a seek
  (library/strings/templates/speech_english.pak). Fix `src/fast_io.cpp`: IAT hooks of FFCore g_FF* in FileSystem2,
  64 KB read window per read-only .pak handle, position kept in memory -> 4244 real reads. `fast_start.ini [Performance] FastPakReads`
- ~7 s: Engine.dll runs DxDiag (CLSID_DxDiagProvider) only to read `DxDiag_DisplayDevices.N.szDisplayMemoryLocalized`;
  DxDiag Initialize creates D3D11/D3D12 devices (the "18x D3D11CreateDevice" in ReShade log). Fix `src/dxdiag_stub.cpp`:
  CoCreateInstance hook returns a stub provider filled from DXGI (dedicated+shared MB, same value). `SkipDxDiag`
- ReShade itself costs only ~0.3 s (+ ~1 s update check over WinINet: ReShade.ini CheckForUpdates=0)
- [x] User test 2026-10-11: saves load, quicksave (F8)/quickload, new game, running around the start beach — all OK
- [ ] Check loading screens (save load / level change) also got faster
- [ ] Remaining ~3 s: D3DX shader compilation at startup (d3dx9_39) — could cache compiled shaders, low priority
- [ ] Disable ReShade update check (ask user)
- [x] Separate mods for friends: `dist/RisenFastStart-1.1.zip`, `RisenWindow-1.0.zip`, `RisenSpeed-1.0.zip` (loader + one mod each)
- [x] User test after the split: F4 speed toggle, borderless, Alt+Tab (automated run only checked startup 9.2 s + logs)
- [ ] Fast Start: get feedback from friends (other PCs, GOG version?)

## Stage 0 — Setup
- [x] Create project folder `Risen\risen-mods` (inside the game folder)
- [x] `git init`
- [x] Install universal-modder plugin (`/plugin marketplace add rehan-remade/universal-modder`)
- [x] Install C++ toolchain — VS Build Tools 2022, MSVC 14.44.35207
- [ ] Install Ghidra (+ JDK) — ask before downloading
- [ ] Back up original `bin/` (copy to `risen-mods\backup\bin-original`)

## Stage 1 — Recon
- [ ] List DLLs imported by `Risen.exe`, pick a proxy-DLL candidate for loading our DLL
      (NOTE: `bin/d3d9.dll` is now ReShade 6.6.2 Addon build — don't use d3d9 as proxy; consider a ReShade addon `.addon64` instead)
- [ ] Dump all exports of `Engine.dll`, `Game.dll`, `Script_Game.dll`, `SharedBase.dll` into `notes/exports/`
- [ ] Find how to get the player entity (e.g. `gCSession`, `eCEntity` getters)
- [ ] Find a per-frame hook point (to poll hotkeys and run logic on the game thread)
- [ ] Study loot-related exports in `Script_Game.dll`:
      `Interact_Player_Loot`, `Interact_Player_AutoLoot`, `AutoLoot`, `BtnEvent_OnClick_LootAll`,
      `CanInteract_Player_Loot`, `AssessPlayerLooting`, `CanInteract_Player_TakeItem_*`
- [ ] Study time exports in `Engine.dll`: `eCTimer::GetInstance`, `eCTimer::SetTimeScaling(float)`,
      `eCTimer::GetTimeScaling`; console command `timer.fTimeScale`

## Stage 2 — Proof of concept
- [x] Proxy DLL (`version.dll`) that loads with the game and writes a log file — verified in game 2026-10-11
- [ ] Call `eCTimer::GetInstance()` and log `GetTimeScaling()` — proves calling game exports works
- [x] `.ini` reader with hot reload

## Stage 2.5 — Borderless window (quick win)
Exclusive fullscreen + ReShade (D3D9) hangs / black screen on Alt+Tab (seen 2026-10-11, hang in `IDirect3DDevice9::Reset`).
Engine has no borderless option (`<Window FullScreen=... Left/Top/Right/Bottom>` only).
- [x] On startup: strip window border (`WS_POPUP`), size to monitor, keep `FullScreen="false"`
- [x] `.ini`: `Borderless=1`
- [x] Hide Windows cursor over the game window (windowed mode shows OS cursor + game cursor), clip cursor to window while focused
- [x] Investigate slow startup: D3D11/D3D12 probing comes from DxDiag run by Engine.dll — stubbed (see top)

## Stage 3 — Speed Hack mod
Implemented in `src/main.cpp` (F4 toggles `eCTimer::SetTimeScaling`), verified in game 2026-10-11.
- [x] Hotkey toggles `SetTimeScaling(multiplier)` <-> `1.0`, `.ini` hot reload
- [x] **On-screen indicator** top-left while speed-up is on: hourglass icon + `x2` / `xN` — session 3 (2026-10-11):
  - [x] Dear ImGui in `third_party/imgui` (pinned release), built into the speed mod
  - [x] `shared/d3d9_hook`: IAT-hook `Direct3DCreate9` in Engine.dll -> `IDirect3D9::CreateDevice` -> device vtable
        `Present` + `Reset`; works with and without ReShade (bin\d3d9.dll)
  - [x] Draw the indicator with ImGui (no input), handle device Reset
  - [x] Test: F4 on/off, Alt+Tab while sped up, minimize/restore, ReShade overlay still fine (user-tested OK 2026-10-11)
- [x] **Speed settings from inside the game** — session 4 (2026-10-11), user-tested OK (incl. dialogs):
  - [x] Recon: Engine.dll reads input via Raw Input (RegisterRawInputDevices + WM_INPUT), mouse look via
        SetCursorPos/ClipCursor -> `shared/input_capture` swallows WM_INPUT/key/mouse messages and no-ops those calls
  - [x] F10 panel (`mods/speed/panel.cpp`), Esc/F10/Close; game time frozen via eCTimer scale 0.001
        (`gCSession::Pause` blacked out the screen)
  - [x] Enabled, multiplier slider, rebind toggle/panel keys, indicator on/off + size, RU/EN switch (globe button)
  - [x] Defaults changed to Z / x3 (user's request); `dist/RisenSpeed-1.2.zip` for a friend
  - [ ] Friend's feedback; panel not yet tested in exclusive fullscreen / without the Window mod
  - [ ] Later grows into the Stage 5.5 settings window for all mods
- [x] `.ini`: `[Speed] Multiplier` (default 3.0), `ToggleKey` (default Z), `[Panel] Key/Language`
- [ ] Optional: second hotkey / step up-down keys
- [ ] Check side effects: physics at high speed, dialogs, cutscenes, combat slomo (`gCCombatMoveMelee::SetSlomoTimeScale`)

## Stage 4/5 — AutoLoot mod (`mods/autoloot`, QuickLoot is part of it) — 0.1 works in game 2026-10-11
Recon: `notes/loot-recon.md`. Script.dll `Entity` API + game script `AutoLoot(player, container)` via eCScriptProxyScript.
Keys (user's choice): MOUSE2 = QuickLoot of the FOCUSED body/open chest/item only (World::GetFocus), no loot window;
not while the weapon is drawn (PSCombatSystem::GetCombatMode != 0, guess). F = AutoLoot on/off: radius pickup of
ground items (3 m, never owned/mission), also works in combat stance.
Debug: `[Debug] DumpKey` logs everything around the player.
- [x] Dump verified: names, npc/dead/down, items, owner, inventory stacks; Sara (down) is skipped
- [x] QuickLoot of a dead body, AutoLoot pickup of gold/potions/weapons (no crash)
- [x] Test after rebinding (2026-10-11, user: works great): RMB on focused body/item/open chest, no loot with weapon drawn,
      nothing in dialogs / on empty focus, F toggle. Sara's hut chest correctly refused (owned or locked)
- [x] gui2.PrintItemLog shows the native "+ item" feed on the left for ground pickups - user likes it, no own feed
- [ ] Chests: dump (K) near locked/unlocked/owned chests -> confirm PSLock status values (1 = open?) and owner
      (low priority: sailor chest looted, Sara's chest refused - behaviour already correct in practice)
- [ ] Skip while GUI2 is browsing (inventory/trade/loot window), in combat, dialogs (IsDialogCameraActive done)
- [ ] Owned items: check merchant goods / items in houses report an owner
- [ ] Quest items: pick up a quest item -> quest advances?
- [ ] Hand icon right of the hourglass while AutoLoot is on (comes with the hub below)
- [ ] README + package

## Stage 5.5 — Mod hub: one F10 window for all mods (session 5, 2026-10-11)
Each mod DLL has its own ImGui copy -> mods can't draw into another mod's window. Design: `mods/menu` -> `menu.dll`
owns D3D9 overlay, ImGui, input capture, pause, F10 window and the status icon row. Mods register pages with
declarative items bound to keys of their own ini (`shared/hub_api.h`, C ABI, `RisenHubApi` export); the hub writes the
ini and calls the mod's onChanged on the game thread. Without menu.dll mods keep working via ini.
- [x] `shared/hub_api.h` + `shared/hub_client` (lazy GetProcAddress, HubBusy() for hotkeys)
- [x] `mods/menu`: overlay + window with a tab per mod, checkbox/slider/key/header/note items, RU/EN globe, pause
- [x] Status icons row: hourglass (Speed, moved from the speed mod), hand (AutoLoot active)
- [x] Speed 1.3: drop own ImGui panel/indicator, register page + status
- [x] AutoLoot 0.3: page (keys, radius, ground/chests) + hand icon; NEW auto-loot of bodies / open chests in radius
- [x] Window, Fast Start: pages (Fast Start needs a restart -> note)
- [x] User test 2026-10-11: all tabs, icons (hand liked), radius bodies/chests - "works great"
- [x] Fixed window size + mouse wheel scroll (user-tested)
- [x] package.ps1: menu.dll in every zip (user agreed: shared like version.dll, newest wins); READMEs Speed 1.3/AutoLoot 0.3
- [x] Risen style (user: "просто офигенно"): game cursor + parchment frame (9-slice) from images.pak atlas
      GUI_TexturePage01_REDESIGN, Quorum + FreeSans fonts from gui2.pak, read at runtime (mods/menu/game_assets)
- [x] AutoLoot 0.4: game effect eff_magic_telekinesis_target_once_01 where something was looted (user: "офигенно"),
      silent (quiet copy of the sequence without PlaySound, swapped into the effect map only for our start),
      scaled via the StartEffect matrix (default 0.15, F10 slider). Debug EffectTestKey cycles candidates. Was planned as:
      Script.dll: static `EffectSystem::StartEffect(bCString const&, bCVector const&)` (?StartEffect@EffectSystem@@SA?AVEffect@@AEBVbCString@@AEBVbCVector@@@Z),
      `Entity::GetPosition` (?GetPosition@Entity@@QEBA?AVbCVector@@XZ) - take it BEFORE InsertStack. Pick a short effect name from
      data\compiled\effects.pak (sparkle / glint). Fallback: Engine eCCameraBase::WorldToScreen + ImGui sparkle.
- [ ] Later: item category filters for AutoLoot
- [ ] Idea: with ReShade Addon present, also offer the pages in ReShade's menu (`reshade::register_overlay`)

## Stage 5.7 — No training limits (user's request 2026-10-11, small mod `mods/no-limits`)
Risen caps how far attributes (strength, dexterity, ...) and skills can be trained (max value / per-teacher limit).
Goal: remove the caps, nothing else. Ultra simple, page in the F10 menu with an on/off toggle.
- [x] No Limits 0.1 (strength + dexterity only, user's/friend's choice): global max 200 (gCSkills_PS::ms_arrSkillInfos,
      weapons/magic 10); teacher cap = gCSkillRange max checked in gCInfoConditionCanTeachSkill::IsSatisfied (Game.dll
      +0x8a1630). In-memory patch at +0x8a16fa -> cave (ml64) compares with the global max for skills 7/8; bytes verified
      first. Patch confirmed applied in game; [ ] user test at a strength/dexterity teacher past his old limit
- [x] Menu bug: items without the parent field had parent 0 -> first checkbox locked itself (Speed "Enabled",
      Window "Borderless"); menu now accepts only parents above the item. Speed 1.4, Window 1.2
- [ ] (old) Recon: where the cap lives - teacher dialog scripts in Script_Game.dll (CON_teach & co), template/.pak data
      (per-teacher max level), or a hard max in the attribute code (PSSkills / gCSkillValue?)
- [ ] Pick the smallest hook (patch the max-value check or the teacher limit table), learning point costs unchanged
- [ ] Test: train past the old cap at a teacher; saves stay compatible with the mod removed

## Stage 5.8 — Installer (user's idea 2026-10-11)
One small `RisenModsInstaller.exe` (C++, no runtime, same toolchain - user agreed C++ over Go/Rust) that installs
selected mods from the public repo's latest GitHub release (github.com/LOTARLMEN/risen-mods).
Look: the same Risen style as the F10 menu (user's wish). Plan: own folder `installer/` built by build.bat into
build\RisenModsInstaller.exe; a plain Win32 window (fixed size, no frame or own frame) with a D3D9 device +
Dear ImGui (imgui_impl_win32 + imgui_impl_dx9, already in third_party). Reuse mods/menu/game_assets (G3V0 .pak
reader, parchment 9-slice, game cursor, Quorum/FreeSans fonts) - needs a variant of GamePath() that takes the found
game folder instead of "parent of the module's bin\"; ideally move game_assets + the ImGui style setup into
shared/ so menu.dll and the installer use one copy. Fallback when the game folder is not found yet: default ImGui
style with parchment colours, then switch to the game art once the folder is known.
Suggested order (one or two sessions):
  1. installer skeleton: window + D3D9 + ImGui + Risen style from a hard-coded game folder
  2. game folder detection (Steam) + "game is running" check
  3. GitHub latest release -> mod list with checkboxes, install / update / remove
  4. polish: progress, errors, RU/EN globe like the menu, README, release asset
- [ ] Find Risen: Steam registry path -> steamapps/libraryfolders.vdf -> appmanifest_40300.acf; manual folder pick fallback
- [ ] GitHub API `releases/latest` (WinHTTP), list assets Risen<Mod>-<ver>.zip, show checkboxes + installed / new version
      (installed version from bin\mods\<mod>.log first line or a small bin\mods\installed.ini written by the installer)
- [ ] Refuse while Risen.exe is running; download, unzip (Windows Shell / minizip) into the game folder, keep user .ini files
- [ ] Uninstall per mod and "remove all" (version.dll + bin\mods); Window mod: offer to set FullScreen="false"
- [ ] Build in build.bat, ship as its own release asset; README section; unsigned exe -> SmartScreen warning note
- [ ] Optional later: self-update check

## Maybe later
- [ ] Native "Mods" tab in the game's own options menu (`GUI2.dll` + `gui2.pak`; few exported symbols → heavy RE, fragile across game updates)

## Stage 6 — Release
- [ ] README with install/uninstall steps
- [ ] Test full playthrough segments with saves (back up saves first)
