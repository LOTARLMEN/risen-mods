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
- [x] Proxy DLL (`version.dll`) that loads with the game and writes a log file — built, awaiting in-game test
- [ ] Call `eCTimer::GetInstance()` and log `GetTimeScaling()` — proves calling game exports works
- [ ] `.ini` reader with hot reload

## Stage 2.5 — Borderless window (quick win)
Exclusive fullscreen + ReShade (D3D9) hangs / black screen on Alt+Tab (seen 2026-10-11, hang in `IDirect3DDevice9::Reset`).
Engine has no borderless option (`<Window FullScreen=... Left/Top/Right/Bottom>` only).
- [ ] On startup: strip window border (`WS_POPUP`), size to monitor, keep `FullScreen="false"`
- [ ] `.ini`: `Borderless=1`
- [ ] Hide Windows cursor over the game window (windowed mode shows OS cursor + game cursor), clip cursor to window while focused
- [ ] Investigate slow startup: ~8 s of D3D11/D3D12 device probing (18x `D3D11CreateDevice`) in a D3D9 game, not caused by Steam overlay — find caller, maybe skip

## Stage 3 — Speed Hack mod
- [ ] Hotkey toggles `SetTimeScaling(multiplier)` ↔ `1.0`
- [ ] `.ini`: `SpeedMultiplier` (default 2.0), `SpeedToggleKey`
- [ ] Optional: second hotkey / step up-down keys
- [ ] Check side effects: physics at high speed, dialogs, cutscenes, combat slomo (`gCCombatMoveMelee::SetSlomoTimeScale`)
- [ ] Optional: on-screen indicator when speedup is active

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
