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
- [ ] Remaining ~3 s: D3DX shader compilation at startup (d3dx9_39) - cache the compiled shaders on disk
      (user asked 2026-10-12 "can it be even faster?" - measure first with StartupTrace, then decide)
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
- [x] Speed 1.5 (user-tested 2026-10-12, "очаровательно"): **Step keys**, then changed on user's wish to StepUp/StepDown
      sliders (default 0.5, x1..x10, never below x1) instead of a fixed ladder. Original plan:: optional, off by default, own checkbox in F10 - "faster"/"slower"
      keys step the multiplier (e.g. x1.5 / x2 / x3 / x5), indicator shows the current value; toggle key unchanged
- [x] Option "no speed-up in dialogs" (NormalInDialogs; hourglass dimmed via HUB_STATUS_DIM text marker, menu.dll) (IsDialogCameraActive, already used by AutoLoot): user talked to an NPC sped up
      without problems, but is unsure it's safe for scripted dialogs/cutscenes
- [ ] Test in exclusive fullscreen (Window mod off) - menu, indicator, Alt+Tab
- [ ] Check side effects: physics at high speed, cutscenes, combat slomo (`gCCombatMoveMelee::SetSlomoTimeScale`)

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
- [ ] AutoLoot 0.5 built + deployed 2026-10-12, NOT yet tested in game: cone (camera pose row 2, fallback player
      at-vector; check with DumpKey that "view" matches where the camera looks), [Filter] categories (gEItemCategory
      1 weapon 2 armor 3 consumable 5 magic 6 misc 7 written; gold = It_Gold/It_GoldBag*, ammo = It_Arrow/It_Bolt),
      theft = owner && !PSItem::Dropped (from Script_Game AssessTheft), friend's bugs fixed: arrows in flight
      (PSProjectile::IsActive + item must rest 2 ticks) and radius bodies never looted (body marked done while still
      dying -> now retried every 1 s up to 8 times until the script moves something)
- [x] AutoLoot 0.6 (2026-10-12): radius bodies of animals/monsters were not looted - their plunder (meat, skins)
      is created by Script_Game CanInteract_Player_<Kind> (24 kinds, IsGeneratedPlunder flag) only when the player
      focuses the body. Fix: PSInteraction::CanInteractWith on the PLAYER with the body (verified: vulture meat).
      By-hand loot (InvFilter_Loot + TransferAmountTo) kept as fallback while the weapon is drawn - check the log
      ("script" vs "by hand") and drop it if the script always works
- [ ] **Theft protection (user's main concern, 2026-10-12)**: never auto-loot anything NPCs would treat as stolen.
      Ground pickup already skips items with an owner - verify it covers merchant goods, items in houses/huts,
      owned chests; find out whether ownership can end (owner dead, guild change) and loot it then. Look for the
      game's own "is stealing" check (what makes NPCs react) and use it instead of the raw owner field if it exists
- [ ] Quest items: pick up a quest item via RMB QuickLoot -> journal / quest advances like with the native pickup
- [ ] **Cone pickup (user's idea 2026-10-12)**: F10 "Area" = Cone (default) / Circle. Cone = items within the radius
      AND within +-N deg (slider, default ~60) of the view direction (camera forward, fallback player facing):
      dot(forward_xz, normalize(item - player)_xz) >= cos(N). Circle = current behaviour. Recon: camera forward
      (Engine eCCameraBase?) or Entity rotation/matrix in Script.dll
- [ ] **Item category filters (user: great idea, 2026-10-12)**: F10 checkboxes per category (gold, potions, food,
      plants, weapons, armour, ammo, scrolls/runes, keys/lockpicks, misc/trophies...), separate for radius AutoLoot
      and RMB QuickLoot? Recon: item category field on the item template (gCItem_PS?) and its values
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

## Stage 5.7 — No training limits (user's request 2026-10-11, small mod `mods/no-limits`)
Risen caps how far attributes (strength, dexterity, ...) and skills can be trained (max value / per-teacher limit).
Goal: remove the caps, nothing else. Ultra simple, page in the F10 menu with an on/off toggle.
- [x] No Limits 0.1 (strength + dexterity only, user's/friend's choice): global max 200 (gCSkills_PS::ms_arrSkillInfos,
      weapons/magic 10); teacher cap = gCSkillRange max checked in gCInfoConditionCanTeachSkill::IsSatisfied (Game.dll
      +0x8a1630). In-memory patch at +0x8a16fa -> cave (ml64) compares with the global max for skills 7/8; bytes verified
      first. Patch confirmed applied in game; [ ] user test at a strength/dexterity teacher past his old limit
- [x] Menu bug: items without the parent field had parent 0 -> first checkbox locked itself (Speed "Enabled",
      Window "Borderless"); menu now accepts only parents above the item. Speed 1.4, Window 1.2
- [x] **No Limits 0.2: past 200** (user, 2026-10-12: "думал ты снимешь в целом ограничения, чтобы качаться 200+").
      ms_arrSkillInfos lives in .bss, filled by Game.dll's static initializer (+0x478900; strength/dexterity
      min 0, max 200, default 100); all 361 refs read max from the table (GetTotal / GetSkillInfo / GetSkillMaxValue
      clamp with it), teach cost = fixed gCInfo::LearnPointCost per dialog option (no table to overflow). Mod writes
      MaxValue (ini/F10 slider 200-2000, ini up to 9999, default 1000) into entries 7/8 [+4], 200 back when off;
      table values checked first. In game: log shows max 1000 written + patch on.
- [ ] User test No Limits 0.2: teacher past 200, character screen shows > 200, potion of strength past 200, save /
      load keeps the value; J (DumpKey) logs base/total/max of the player
- [ ] Maybe: intelligence and other attributes too (user said "в целом ограничения прокачки") - ask
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
Session 7 (2026-10-11): installer 1.0 built (`installer/` -> `build\RisenModsInstaller.exe`, 0.9 MB). Shared code moved:
`shared/game_assets` (game folder settable, raw image access) and `shared/risen_style` (parchment frame, colours, game
fonts, globe button) - menu.dll uses them too (also fixed: blue checkbox bg, ImGui 1.92.9 ImGuiCol_CheckboxSelectedBg).
- [x] Find Risen: exe folder / its parent, then Steam (HKCU SteamPath -> libraryfolders.vdf -> appmanifest_40300.acf);
      "Change..." folder picker (bin\ accepted too); 64-bit check of bin\Risen.exe
- [x] GitHub `releases/latest` (WinHTTP), all Risen<Mod>-<ver>.zip downloaded at start and unpacked in memory
      (own inflate + zip reader, CRC checked); checkbox per mod, status not installed / will install / update / remove
- [x] Installed versions in `bin\mods\installed.ini`; dll present without an entry = "?" (offered as update)
- [x] Refuses while Risen.exe runs; atomic writes (.new + MoveFileEx), never touches existing .ini, README ->
      bin\mods\<id>.README.txt, only paths under bin\ are written
- [x] Remove per mod (dll, readme, log; .ini kept); last mod removed -> menu.dll, installed.ini, our version.dll
      (marker "Risen mod loader"; a foreign version.dll is kept as version.dll.other and restored)
- [x] Window mod: FullScreen="false" in data\ini\ConfigLocal.xml (+ .bak) on install
- [x] RU/EN globe, game cursor as a real HCURSOR, game icon from Risen.exe, drag by the header, per-monitor DPI
- [x] Command line for tests: --game, --zips <dist>, --list, --install a,b, --remove a,b, --lang, --screenshot;
      log %TEMP%\RisenModsInstaller.log. CLI-tested: install / update / remove+cleanup on a fake game folder,
      Steam detection, real GitHub release; screenshots RU/EN with and without game art
- [x] UX rework after user feedback: start = only the mod list (one API call, ~0.5 s), zips downloaded on install;
      nothing pre-ticked; "Install (N)" appears only when something is ticked; updates via a separate "Update (N)";
      each installed mod has a "Remove" button with a confirmation popup (settings kept)
- [x] Installed version read from the mod dll itself ("<Title> <version>" log string), then installed.ini, else "?"
      -> hand-installed old zips are offered as updates (tested with a patched "Speed 1.3" dll)
- [x] Own icon installer\installer.ico (tools\make_installer_icon.ps1, no game art) + VERSIONINFO via installer.rc
- [ ] Discord shows its game overlay over the installer (user, 2026-10-11): check whether it's the exe living in the
      Risen folder (Discord "Registered games") - run a copy from Downloads
- [ ] User test of the window: clicks, drag, folder picker, apply on the real game folder (overwrites dev dlls with release ones)
- [x] Published 2026-10-11: commit efafa2d, RisenModsInstaller.exe added to release v2026.10.11 (RU/EN notes, SmartScreen note); downloaded asset verified
- [x] Self-update (installer 1.1): release asset sha256 ("digest" in the API) vs own exe; differs -> download,
      VERSIONINFO compared (installer/version.h is the single version source) -> banner "new installer version" +
      button; swap self -> .old, new -> self, restart, .old deleted at start; failure -> "open the releases page".
      Tested with a 1.0.9 build updating itself from a local --zips folder with a 1.1 exe (click sent via PostMessage).
      Installer 1.0 has no updater: its users update by hand once. EVERY new release must carry RisenModsInstaller.exe
      (the installer reads releases/latest), raise INSTALLER_VERSION for every published installer change.
- [x] Hardening: /DEPENDENTLOADFLAG:0x800 (system DLLs only from System32 - no d3d9.dll/version.dll planted next to it)
- [x] Discord overlay over the installer - gone (user, 2026-10-12)
- [x] User test of the window, folder picker etc. - works (user, 2026-10-12)
- [x] **Menu as its own release asset (user's design 2026-10-12, final)** - done in session 9 (2026-10-12), installer 1.2.
      Built + CLI-tested on a fake folder (all 4 cases below pass), screenshots RU/EN; menu.dll is now "Risen Mods
      Menu 1.1" (dll string = installed version) so every older menu.dll is offered as an update. Menu row under
      the mods (no checkbox): "comes with the first mod" / installed / update / missing -> counted in "Update (N)".
      The menu is handled only when something is installed or updated (a plain removal never downloads it).
      Window height 800 -> 880 for the extra row. Not released yet; user test of the window pending.
      Mod zips WITHOUT menu.dll: version.dll + mod dll + README (loader stays: without it a hand-installed mod
      doesn't load; mods work without the menu via ini). Release gets RisenMenu-<ver>.zip (menu.dll + README; add
      mods/menu/README.txt, its title gives the version; package.ps1 packs it and drops menu.dll from mod zips).
      Installer 1.2: menu is not a checkbox - downloaded and installed with the first mod when missing, updated
      whenever the release has a newer one (installed version from the dll's "<Title> <version>" log string), also
      as its own "Menu update" line when no mod changed; removed with the last mod (already). Bump INSTALLER_VERSION.
      Old installer 1.1 lists "Menu" as a normal mod (user ticks it) - acceptable, it offers the self-update.
      Mod READMEs: manual install = mod zip (+ RisenMenu zip for the F10 window).
      Every mod README (RU + EN) starts with a short block: "easiest: RisenModsInstaller.exe from the releases page
      (link); F10 opens the settings menu of all mods" - manual install is the user's own choice after that.
      Test on a fake game folder (--zips): first mod -> menu added, second mod -> menu untouched, newer menu ->
      updated, last mod removed -> menu removed
- [x] **"Find the game automatically" button** (user, 2026-10-12): after picking a wrong folder by hand, one click
      reruns the start-up detection (exe folder, Steam libraries) and switches back to the found Risen.
      Done in 1.2: shown under "Change..." only while the chosen folder differs from the found one
- [x] Released v2026.10.12.4 (installer 1.2, RisenMenu-1.1.zip, No Limits 0.2, mod zips without menu.dll), commits 7e96435 + 699652f
- [x] Next release must carry the rebuilt menu.dll (blue checkbox fix, commit efafa2d) in every zip - the
      v2026.10.11 zips were built before the fix

## Stage 5.10 - German and Polish (user's request 2026-10-12, own session)
Menu, all mod pages and the installer in EN / RU / DE / PL.
- [ ] Keep the hub ABI (HubItem has labelEn/labelRu): extra languages from text files next to each mod
      (bin\mods\<mod>.de.txt / .pl.txt, "English text = translation" lines, shipped in the zips); menu looks a
      string up by its English text, falls back to English. Installer: its own table
- [ ] Fonts: check Quorum / FreeSans from gui2.pak have Latin Extended-A (ą ę ł ś ż ź ć ń ó, ä ö ü ß); add the
      ImGui glyph ranges; fallback font if missing
- [ ] Language picker: the globe becomes a 4-language dropdown (menu header + installer); menu.ini Language=en|ru|de|pl
- [ ] Translations written by Claude - ask the user to get a native speaker check if possible
- [ ] READMEs: keep RU + EN (DE/PL optional)

## Maybe later
- [ ] (unlikely) Native "Mods" tab in the game's own options menu (`GUI2.dll` + `gui2.pak`; few exported symbols → heavy RE, fragile across game updates)

## Stage 5.9 — Quick lockpick (user's idea 2026-10-12)
Walk up to a locked chest (or door?), press a key: if the player's lockpicking skill allows this lock, one lockpick is
spent and the lock opens without the lockpicking minigame; then loot as usual (RMB QuickLoot). Not enough skill or no
lockpick -> short message, nothing happens. Same theft rules as the game (picking an owned chest seen by NPCs).
- [ ] Recon: PSLock (status, difficulty), player's lockpicking skill value, lockpick item, the game's own unlock
      path (script / interaction) - prefer calling it so skill checks, XP and witnesses work as in vanilla
- [ ] Key in F10 (AutoLoot page or own small mod), hint when focused on a pickable chest
- [ ] Test: easy/hard lock, no lockpicks, owned chest near NPCs, save/load keeps the chest open
- [ ] User will report how the vanilla lockpicking looks once they reach it in the game (minigame, skill, lockpicks,
      "not enough skill" message) - start the stage after that

## Stage 5.11 — Pickpocket rework (user's idea 2026-10-12, own session, after 5.9)
Rework stealing from NPCs: RMB while standing BEHIND a person = a pickpocket attempt, success by chance.
Chance depends on the pickpocket skill and on how often the player has stolen (lately / from this NPC?) - the more
thefts, the lower the chance (NPCs get wary). Failure = caught, the game's own theft reaction (anger, fight, guards).
- [ ] Recon: vanilla pickpocket (skill PickPocket = 28 in the skill table, dialog option / interaction in
      Script_Game.dll, what it gives - one item or the NPC's whole inventory, how "caught" is triggered), "behind"
      test (player position vs NPC facing), NPC states where it must not work (fight, sleeping?, talking)
- [ ] Design with the user: chance formula (skill tiers, theft counter: global / per NPC, decay over game time),
      what is stolen, how failure looks, whether the vanilla dialog option stays
- [ ] Theft counter stored where? Saves can't get our data easily - a per-save side file or an existing game
      variable (recon)
- [ ] F10 page: key, base chance, penalty per theft; feedback on screen (success / caught)

## Stage 5.12 — Own stats in the character screen (user's idea 2026-10-12, research first)
Can mods add their own lines to the game's character / attributes screen (e.g. "Thefts: 12", pickpocket chance)?
- [ ] Recon: how GUI2.dll builds the character screen (layout from gui2.pak? script-filled list?), whether a line
      can be added through exported calls; fallback - our own ImGui panel drawn next to the game's screen while it
      is open (menu.dll overlay, detect the screen is open)

## Stage 5.13 — AutoLoot / QuickLoot fixes (user's report 2026-10-12)
- [x] Bug (0.7, user-tested OK): QuickLoot skips quest items (e.g. grain for a quest - every stalk needs a click). QuickLoot must take
      everything the vanilla pickup can take, quest items included. Find where our filter drops them (quest flag /
      item category check) and remove it for QuickLoot
- [x] Bug (0.7, user-tested OK; cause: the chests were LOCKED - unlocking set PSLock status 1 and the next tick
      emptied them under the loot window; fix: chests ever seen locked are never auto-looted): a chest AutoLoot skipped (looks like a quest chest) gets looted instantly by AutoLoot the moment the player
      opens it by hand. Find why: opening changes its state (lock / quest flag / "already opened") so it passes our
      filter. Expected: an opened-by-hand container that AutoLoot had skipped stays manual (or follows the same rule
      as before opening) - confirm the wanted behavior with the user
- [x] (0.7) Option in F10 (AutoLoot page): "Pick up quest items" (default off) - AutoLoot also takes quest items
- [ ] (0.7: plunder asked up to 5 times while the body is empty + "ms after it was seen dead" in the log - read
      the log after play) Investigate: AutoLoot sometimes reacts slowly (noticeable delay before it loots nearby items). Check scan
      interval, the order targets are processed, busy/animation waits, per-tick limits; log timings to find the cause
- [x] (0.7, user-tested OK: (b) done - Game.dll focus module (+0x83A2C0) converts eCLocString("FO_"+name); its
      Engine import ??BeCLocString hooked, suffix only for return address +0x83A3E3 (GetFocusName hook did NOT work, (16-byte
      prologue checked), " (Пусто)"/" (Empty)" by menu.ini language, [Empty] Mark/Text, F10 "Пустые"; (a) = the sparkle,
      (c) left: RMB on empty shows the mark already) Feature: show that a chest was looted. (a) Feedback when AutoLoot empties a chest - short on-screen message /
      marker at the chest. (b) Empty-container hint on focus: when the player looks at an empty chest, show "(Empty)" - user approved this exact look -
      (next to the game's focus name or our overlay) so it doesn't have to be opened. (c) RMB QuickLoot on an empty
      chest - message "Empty" instead of nothing. Recon: can we change the focus name text / add a suffix, or draw our
      own label (menu.dll overlay, world->screen position); option in F10 to turn it off
- [x] (0.7, needs game test) RMB on a locked chest (nobody's) when the player carries its key: unlock
      (PSLock PropertyStatus = 1, fires OnLockStatusChanged) and loot at once
- [x] (released with 0.7) NEXT SESSION (user's decision 2026-10-12): finish sparkle colours, then release AutoLoot 0.7 WITH them
      (commit + push + release only after colours work; the rest of 0.7 below is user-tested and waits uncommitted)
- [x] (0.7, user-tested: "очень красиво") DONE: deep copy of the sequence per colour; vtable hook of
      gCEffectCommandSpawnEntity::OnStart marks our copy's spawn commands, Game.dll's Engine import
      eCEntity::PatchWithTemplate then recolours the fresh entity's eCParticle_PS colour keys (brightness kept) and
      its children. Palette (user's): quest gold, weapons/ammo red, armor light blue, magic violet, consumables
      green, coins and rest own colour; chests/bodies = most important taken thing, flash x1.25.
      Note: one-shot effects don't keep spawned entities (gCEffect +0x88 only for permanent ones).
      Idea (user 2026-10-12): sparkle colour by item type - user prefers recolouring. Recon so far:
      gCEffect::GetSpawnedEntities (Game.dll) -> spawned entities of a started effect (may appear a bit later than
      StartEffect); Engine eCParticle_PS has Set/AccessFaceInFactor / FaceOutFactor (bCFloatAlphaColor, used by
      CalcColorFactor - likely a tint), UseColorScale; colour-scale keys are not exported. Try: tint FaceIn/Out on
      the spawned particle entities, test in game with EffectTestKey
      Session 10 findings: Engine eCParticle_PS +0x290 UseColorScale, +0x4A8 eCColorScale {+8 items, +0x10 count,
      +0x18 wrap}, item = {float t, r, g, b, a} 0x14 bytes (CalcColor / eCColorScale::CalcValue; FaceIn/OutFactor
      only apply during fades). Particle PS type = 0x49 (eCEntity::GetPropertySet(type)). gCEffect +0x88 = spawned
      entities (bTObjArray<eCEntityProxy>, proxy 16 bytes?). Probe result: spawned=0 at StartEffect and 60 ms
      later; at 300 ms the gCEffect was already freed (garbage, game CRASHED) - never keep gCEffect* across frames.
      Next idea: one deep copy of the sequence per colour (like the quiet copy), hook the spawn command's execute
      (gCEffectCommandSpawnEntity vtable) and, when the command object belongs to one of our copies, tint the
      colour scale of the entity it just spawned (and its children) (gold / weapons / potions / quest...). Ways: a different
      existing eff_* per category (find ones with distinct colours, test visually), or recolour our deep copy of
      the sequence (find the colour fields of the particle commands). F10: on/off, maybe colour per category
- [ ] Test: quest grain with QuickLoot (hold RMB), quest chest skipped -> open by hand, option on/off, delay measured

## Stage 5.14 — "Slash and Craft" mode: attributes grow from combat (user's idea 2026-10-12, big, own sessions)
Inspired by the "New Balance" mod for Gothic 2 Night of the Raven. Part of character growth comes from fighting:
the attribute tied to the weapon / attack type grows with the damage dealt by it. Example: deal 100 damage with
strength weapons -> +1 Strength; the next point needs more (step grows by a percentage, not a flat +100).
Mapping (to confirm in recon): strength weapons (swords, axes, hammers?) -> Strength; dexterity weapons / bows /
crossbows -> Dexterity; magic attacks (spells, scrolls?) -> mana / magic attribute (Risen has no Intelligence -
find what fits: mana, magic circle?).
Depends on 5.12 (own stats on the character screen) - start only after we know how to show our own values in
the game's menus. Needs saving per-save progress too (same question as the theft counter in 5.11).
- [ ] Study New Balance's Slash and Craft rules (formula, step growth, caps, which weapons count, whether it
      replaces or adds to learning points / trainers) - web research, write notes/
- [ ] Recon: damage event in Risen (where hits are calculated, attacker, weapon type, spell vs melee vs ranged),
      Risen's attributes and weapon skills (Strength, Dexterity, mana, sword/axe/staff/bow/crossbow skills -
      maybe weapon skills fit better than attributes), how to raise an attribute from code properly
- [ ] Design with the user: what grows from what, formula and step growth, caps, balance against trainers / LP,
      whether killing weak monsters can be farmed (anti-farm rule?)
- [ ] Progress bars / counters on the character screen (via 5.12), F10 page: on/off, multipliers
- [ ] Per-save storage of progress

## Stage 6 — Release
- [ ] README with install/uninstall steps
- [ ] Test full playthrough segments with saves (back up saves first)
