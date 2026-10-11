# Risen (2023, x64) mods — TODO

Target: Steam Risen, 64-bit THQ Nordic re-release (`D:\SteamLibrary\steamapps\common\Risen`).
Project lives in `Risen
isen-mods` (inside the game folder). Approach: native C++ DLL loaded into `Risen.exe`, calling functions exported by the game DLLs
(`Engine.dll`, `Game.dll`, `bin/scripts/Script_Game.dll` export thousands of named C++ symbols).

Goals:
1. **QuickLoot + AutoLoot** — like Gothic's AutoLoot by Hedin: loot nearby corpses/chests with one key,
   auto-pick up items from the ground, no pickup animation.
2. **Speed Hack** — like Gothic's Z-mod speedup: toggle game speed by hotkey, multiplier configurable in `.ini`.

Both mods share one loader + one `.ini` (`risen_mods.ini` next to `Risen.exe`), hot-reloaded.

## Faster game startup — DONE 2026-10-11: window at 9.7 s instead of 32.3 s
Measured with `src/startup_trace.cpp` (`[Debug] StartupTrace=1` -> `bin/risen_startup.log`: DLL loads with call stacks,
main-thread stack samples every 50 ms, FFCore file I/O stats). Findings:
- ~17 s: FileSystem2.dll reads .pak archives with ~8M ReadFile calls of 1-64 bytes, each after a seek
  (library/strings/templates/speech_english.pak). Fix `src/fast_io.cpp`: IAT hooks of FFCore g_FF* in FileSystem2,
  64 KB read window per read-only .pak handle, position kept in memory -> 4244 real reads. `[Performance] FastPakReads`
- ~7 s: Engine.dll runs DxDiag (CLSID_DxDiagProvider) only to read `DxDiag_DisplayDevices.N.szDisplayMemoryLocalized`;
  DxDiag Initialize creates D3D11/D3D12 devices (the "18x D3D11CreateDevice" in ReShade log). Fix `src/dxdiag_stub.cpp`:
  CoCreateInstance hook returns a stub provider filled from DXGI (dedicated+shared MB, same value). `[Performance] SkipDxDiag`
- ReShade itself costs only ~0.3 s (+ ~1 s update check over WinINet: ReShade.ini CheckForUpdates=0)
- [x] User test 2026-10-11: saves load, quicksave (F8)/quickload, new game, running around the start beach — all OK
- [ ] Check loading screens (save load / level change) also got faster
- [ ] Remaining ~3 s: D3DX shader compilation at startup (d3dx9_39) — could cache compiled shaders, low priority
- [ ] Disable ReShade update check (ask user)

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
- [ ] **On-screen indicator** top-left while speed-up is on: hourglass icon + `x2` / `xN`
      (needs drawing in D3D9: hook `IDirect3DDevice9::EndScene`/`Present`, or a ReShade add-on overlay when ReShade is present)
- [ ] **Speed settings from inside the game** (multiplier, toggle key, on/off) — ini is hot-reloaded, so the in-game UI
      just edits values and writes `risen_mods.ini`; part of Stage 5.5 settings window, can ship earlier as a small speed panel
- [x] `.ini`: `[Speed] Multiplier` (default 2.0), `ToggleKey` (default F4)
- [ ] Optional: second hotkey / step up-down keys
- [ ] Check side effects: physics at high speed, dialogs, cutscenes, combat slomo (`gCCombatMoveMelee::SetSlomoTimeScale`)

## Stage 4 — QuickLoot mod
- [ ] Enumerate entities in radius around player
- [ ] Filter lootable: dead NPCs/monsters, unlocked chests (respect locks — no free lockpicking)
- [ ] Transfer all items to player inventory using the game's own loot logic (keep quest triggers firing)
- [ ] `.ini`: `QuickLootKey` (default MMB), `QuickLootRadius`
- [ ] Message on screen: what was looted

## Stage 5 — AutoLoot (ground items)
- [ ] Auto-pick up loose items in radius, periodically or on key
- [ ] Skip items in shops / owned items (no auto-stealing) — respect ownership
- [ ] Quest items: pick up via the normal interaction path so scripts trigger
- [ ] `.ini`: `AutoLootEnabled`, `AutoLootToggleKey` (default O), `AutoLootRadius`, item category filters
- [ ] Remove / skip pickup animation

## Stage 5.5 — In-game settings window (Dear ImGui overlay, D3D9)
Game renders via `d3d9.dll` (imported by `Engine.dll`) → hook `IDirect3DDevice9::EndScene`/`Reset`, draw ImGui.
Two hosts for the same settings UI (user's idea):
- ReShade (Addon build) present → register an overlay via ReShade add-on API (`reshade::register_overlay`), settings live in ReShade's menu (Home → Add-ons)
- no ReShade → our own ImGui overlay opened by hotkey
- [ ] Hook D3D9 device, init ImGui (dx9 + win32 backends), block game input while the window is open
- [ ] Hotkey to open "Mods" window (default F10, configurable), pause game while open
- [ ] Speed: on/off, multiplier slider, rebind toggle key ("press a key…")
- [ ] AutoLoot: on/off, toggle key, radius slider, item category filters
- [ ] QuickLoot: key, radius
- [ ] Save to / load from `risen_mods.ini`
- [ ] Style the window to match Risen (dark/parchment palette, font)

## Maybe later
- [ ] Native "Mods" tab in the game's own options menu (`GUI2.dll` + `gui2.pak`; few exported symbols → heavy RE, fragile across game updates)

## Stage 6 — Release
- [ ] README with install/uninstall steps
- [ ] Test full playthrough segments with saves (back up saves first)
