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
- [ ] Test after rebinding: RMB loot (does it interfere with blocking/Action 2? skip in combat / weapon drawn?)
- [x] gui2.PrintItemLog shows the native "+ item" feed on the left for ground pickups - user likes it, no own feed
- [ ] Chests: dump near locked/unlocked/owned chests -> confirm PSLock status values (1 = open?) and owner
- [ ] Skip while GUI2 is browsing (inventory/trade/loot window), in combat, dialogs (IsDialogCameraActive done)
- [ ] Owned items: check merchant goods / items in houses report an owner
- [ ] Quest items: pick up a quest item -> quest advances?
- [ ] Hand icon right of the hourglass while AutoLoot is on (comes with the hub below)
- [ ] README + package

## Stage 5.5 — Mod hub: one F10 window for all mods (user's idea, next session)
Each mod DLL has its own ImGui copy -> mods can't draw into another mod's window. Plan:
- [ ] `mods/menu` (hub) owns the D3D9 overlay, ImGui, input capture, F10 window and the status icon row
- [ ] C API exported by the hub (GetProcAddress from mods): register a mod page with declarative settings
      (toggle mod on/off, checkbox, slider, key bind, language) + get/set callbacks; status icon (hourglass, hand)
- [ ] Speed: move its panel + indicator onto the hub API; keep working without the hub (ini only)
- [ ] AutoLoot: page + hand icon + item feed
- [ ] Window, Fast Start: pages (toggles need restart -> show a note)
- [ ] Language RU/EN shared by all pages
- [ ] Later: item category filters for AutoLoot; style the window like Risen (dark/parchment, font)
- [ ] Idea: with ReShade Addon present, also offer the pages in ReShade's menu (`reshade::register_overlay`)

## Maybe later
- [ ] Native "Mods" tab in the game's own options menu (`GUI2.dll` + `gui2.pak`; few exported symbols → heavy RE, fragile across game updates)

## Stage 6 — Release
- [ ] README with install/uninstall steps
- [ ] Test full playthrough segments with saves (back up saves first)
