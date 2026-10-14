# Risen (2023, x64) mods — TODO

Target: Steam Risen, 64-bit THQ Nordic re-release (`D:\SteamLibrary\steamapps\common\Risen`).
Project lives in `Risen\risen-mods` (inside the game folder). Approach: native C++ DLL loaded into `Risen.exe`, calling functions exported by the game DLLs
(`Engine.dll`, `Game.dll`, `bin/scripts/Script_Game.dll` export thousands of named C++ symbols).

Goals:
1. **QuickLoot + AutoLoot** — like Gothic's AutoLoot by Hedin: loot nearby corpses/chests with one key,
   auto-pick up items from the ground, no pickup animation.
2. **Speed Hack** — like Gothic's Z-mod speedup: toggle game speed by hotkey, multiplier configurable in `.ini`.

Layout (since 2026-10-11): `loader/` builds `bin\version.dll` (proxy of the system version.dll) which loads every
`bin\mods\*.dll` in alphabetical order from its DllMain. One folder per mod in `mods/` -> `bin\mods\<mod>.dll`,
settings `bin\mods\<mod>.ini`, log `bin\mods\<mod>.log`. Shared code in `shared/` (log, ini, keys, game window, IAT).
`build.bat [deploy]` builds everything; `tools\package.ps1` makes `dist\Risen<Mod>-<ver>.zip` for every mod with a README.
Mods: fast-start, window (borderless + cursor), speed (Z), autoloot, no-limits, menu (F10 hub), installer;
startup-trace (developer only, not deployed/packed). Public: github.com/LOTARLMEN/risen-mods, releases = zips + installer.

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
- [ ] (user 2026-10-12) Look for more: an even faster startup and faster save loading - the user thinks there are
      more slow spots in the code. Plan: run startup_trace for a fresh profile of the remaining 9.7 s (main-thread
      samples -> top functions, file I/O per pak, sleeps / waits, single-threaded parsing), then the same for loading a
      save (extend startup_trace to sample during the loading screen; compare vanilla vs FastPakReads). Candidates:
      shader cache (above), intro / logo videos, template/strings pak parsing, world sector streaming, save
      decompression. Fix only what's measured; patches check bytes first
      Measured 2026-10-12 (warm cache, Ryzen 5 1600): window after 6.1 s avg (6.46 / 6.08 / 5.64) without the trace.
      Trace of 0..7 s main thread: pak parse 2.7 s (inflated by the trace's own hooks), D3DX shader compile 1.35 s,
      GPU driver 0.65 s, engine code 0.65 s, ReShade update check 0.45 s (WININET), Steam overlay 0.25 s, signature
      checks 0.2 s. FFCore file calls total < 1 s - disk is no longer the bottleneck.
      Pak parse = bCObjectBase::Read / bCAccessorPropertyObject::Read pulling 1-64 bytes per call through
      CFFPackedDirectFileStream::Read (FileSystem2 0x23540: virtual Seek [vt+0x20] to base+pos, virtual Read [vt+0x28]
      -> 0x267e0 -> g_FFReadFile). Stream fields: [this-38h] file, [this-30h] base, [this-28h] pos, [this-20h] size.
      Not parallelizable: single-threaded deserialization into engine globals (bCMemPool, registries) - neither
      threads nor extra processes can hand the parsed objects to the game.
  - [x] Shader cache (Fast Start 1.3, shader_cache.cpp, uncommitted): D3DXCompileShader is never called - Engine
        makes 33 D3DXCreateEffectCompiler and calls ID3DXEffectCompiler::CompileEffect (vtable slot 59) 34 times
        (1.26-1.56 s), then D3DXCreateEffect from the binary (0.09 s). Hook: IAT D3DXCreateEffectCompiler -> source
        key (preprocessed if an include handler), vtable patch Release(2)/SetLiteral(57)/CompileEffect(59), value =
        effect binary (0xFEFF....) in bin\mods\fast_start.shaders (~140 KB, 9 unique effects). Now 33 from cache in
        0.001 s. ReShade.ini CheckForUpdates=0 (user agreed; backup ReShade.ini.bak).
        Result: window after 4.49 s avg (4.44 / 4.28 / 4.75) instead of 6.06 s
  - [x] User 2026-10-12: ran around, all fine, no Press START screen, startup faster
  - [ ] User plays 1-2 h on 2026-10-13 with the cache + skip; reports problems, then release Fast Start 1.3
  - [ ] Installer: CleanupIfEmpty must also delete mods\fast_start.shaders (installer bump)
  - [ ] Release Fast Start 1.3: README RU+EN (shader cache, Press START skip, new ini keys), hub tooltips
- [x] (user 2026-10-12) Skip the "Press START" screen (press_start.cpp, uncommitted): OnX360Paint_Start
      (Script_Game 0x158200) polls the pad and eCInputAdmin::GetRawKeyState for keys 1..0x200; on a key it closes
      pages 0x1A/0x1B, stores keyboard input mode, opens page 0x0D (main menu). We patch Script_Game's IAT entry
      and answer "pressed" once for the call site 0x158335 (bytes checked). Fast Start ini SkipPressStart=1.
      Works in the log; user checks the main menu, keyboard/mouse, and a gamepad if he has one
  - [ ] Fast path in CFFPackedDirectFileStream::Read (vtable patch, serve small reads from our pak window without
        2 virtual calls + 2 FFCore hooks). Expected a few hundred ms; measure the hook cost first
  - [ ] (low, user: "if it doesn't make sense - never mind") Background prefetch of the big paks on a worker
        thread - helps only a cold start / HDD, measure after a reboot
  - [ ] Crash log false positives: the VEH logs first-chance AVs that a __try handles (startup_trace's AfterCall
        produced 6 "crash" reports and used up the per-launch cap). Log as "first-chance", keep the cap for
        unhandled-looking ones, or confirm via a continue handler / sentry
- [x] Separate mods for friends: `dist/RisenFastStart-1.1.zip`, `RisenWindow-1.0.zip`, `RisenSpeed-1.0.zip` (loader + one mod each)
- [x] User test after the split: F4 speed toggle, borderless, Alt+Tab (automated run only checked startup 9.2 s + logs)
- [ ] Fast Start: get feedback from friends (other PCs, GOG version?)

## Stage 0 — Setup
- [x] Create project folder `Risen\risen-mods` (inside the game folder)
- [x] `git init`
- [x] Install universal-modder plugin (`/plugin marketplace add rehan-remade/universal-modder`)
- [x] Install C++ toolchain — VS Build Tools 2022, MSVC 14.44.35207
- [x] Ghidra not needed: dumpbin + tools/pe_disasm.py / pe_symbol.py / pe_exports.py cover it
- [x] Back up original `bin/` (copy to `risen-mods\backup\bin-original`)

## Stage 1 — Recon
- [x] (version.dll, imported by SharedBase.dll) List DLLs imported by `Risen.exe`, pick a proxy-DLL candidate for loading our DLL
      (NOTE: `bin/d3d9.dll` is now ReShade 6.6.2 Addon build — don't use d3d9 as proxy; consider a ReShade addon `.addon64` instead)
- [x] Dump all exports of `Engine.dll`, `Game.dll`, `Script_Game.dll`, `SharedBase.dll` into `notes/exports/`
- [x] (Script.dll Entity::GetPlayer) Find how to get the player entity (e.g. `gCSession`, `eCEntity` getters)
- [x] (WndProc subclass + PostMessage, D3D9 Present for the overlay) Find a per-frame hook point (to poll hotkeys and run logic on the game thread)
- [x] (notes/loot-recon.md) Study loot-related exports in `Script_Game.dll`:
      `Interact_Player_Loot`, `Interact_Player_AutoLoot`, `AutoLoot`, `BtnEvent_OnClick_LootAll`,
      `CanInteract_Player_Loot`, `AssessPlayerLooting`, `CanInteract_Player_TakeItem_*`
- [x] Study time exports in `Engine.dll`: `eCTimer::GetInstance`, `eCTimer::SetTimeScaling(float)`,
      `eCTimer::GetTimeScaling`; console command `timer.fTimeScale`

## Stage 2 — Proof of concept
- [x] Proxy DLL (`version.dll`) that loads with the game and writes a log file — verified in game 2026-10-11
- [x] Call `eCTimer::GetInstance()` and log `GetTimeScaling()` — proves calling game exports works
- [x] `.ini` reader with hot reload

## Stage 2.5 — Borderless window (quick win)
Exclusive fullscreen + ReShade (D3D9) hangs / black screen on Alt+Tab (seen 2026-10-11, hang in `IDirect3DDevice9::Reset`).
Engine has no borderless option (`<Window FullScreen=... Left/Top/Right/Bottom>` only).
- [x] On startup: strip window border (`WS_POPUP`), size to monitor, keep `FullScreen="false"`
- [x] `.ini`: `Borderless=1`
- [x] Hide Windows cursor over the game window (windowed mode shows OS cursor + game cursor), clip cursor to window while focused
- [x] Investigate slow startup: D3D11/D3D12 probing comes from DxDiag run by Engine.dll — stubbed (see top)
- [ ] Bug (user 2026-10-13): game resolution below the monitor's (1600x900 on 1920x1080) + Borderless=1 -> the GUI is
      drawn enlarged and shifted right/down, the bottom (options tab row) is cut off. EnforceBorderless stretches the
      window to the monitor every 250 ms while the game renders 1600x900; the GUI seems to map by the client size.
      Fix idea: remember the size the game itself asks for (WM_WINDOWPOSCHANGING not caused by us) and size the
      borderless window to that (centred), or full monitor only when it matches; or stretch via the backbuffer.
      Workaround: keep the game resolution = monitor resolution.

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
  - [x] Grew into the Stage 5.5 settings window for all mods
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
- [x] (0.7: status 1 = open confirmed by the key unlock) Chests: dump (K) near locked/unlocked/owned chests -> confirm PSLock status values (1 = open?) and owner
      (low priority: sailor chest looted, Sara's chest refused - behaviour already correct in practice)
- [x] (user-tested, released) AutoLoot 0.5: cone (camera pose row 2, fallback player
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
- [x] (0.8, user-tested 2026-10-12: no theft by F, RMB theft noticed as vanilla; released v2026.10.12.7) **Theft (user report 2026-10-12: AutoLoot stole from houses
      unpunished, and looted through walls)**. Script_Game AssessTheft (0xc1740) does NOT look at the owner: theft =
      !PSItem::Dropped && sub_eebd0(item) != 0 = security level of where it lies: first area property found
      (Entity::GetListOfPropertyNames) of SL_Public/Watched/Forbidden/Inquisition/Graveyard/Vulcan/Casern -> the
      script of the same name; if none or 0 -> Entity::GetRoom().GetAIZoneSecurityLevel(). Our SecurityLevel() copies
      it; WouldBeTheft = !dropped && (owner || security). User's decision: AutoLoot (F) never takes theft; RMB QuickLoot
      takes it but first runs the game's broadcast sub_ed3e0(_, Entity item, eCScriptProxyScript "AssessTheft") (by
      value, as Interact_Player_TakeItem +0x3fb and PreInteract_Player_Chest do; bytes + call site checked, else
      refused) -> NPCs who see it react as vanilla. QuickLoot chests may now be owned (theft broadcast on the chest).
      Walls: AutoLoot needs LineOfSight = eCPhysicsScene::TraceRayFirstHit(from chest +120, dir to target +30, flags
      0x1B as Game.dll) with SetQueryIgnoreEntities {player, target}, reset after. Test: dump K shows security= sight=
      y= (check player y vs ground items: +120 assumes feet), steal in a house seen / unseen, items behind a wall,
      bushes/grass must not block
- [x] (0.7, user-tested) Quest items: pick up a quest item via RMB QuickLoot -> journal / quest advances like with the native pickup
- [x] (0.5) **Cone pickup (user's idea 2026-10-12)**: F10 "Area" = Cone (default) / Circle. Cone = items within the radius
      AND within +-N deg (slider, default ~60) of the view direction (camera forward, fallback player facing):
      dot(forward_xz, normalize(item - player)_xz) >= cos(N). Circle = current behaviour. Recon: camera forward
      (Engine eCCameraBase?) or Entity rotation/matrix in Script.dll
- [x] (0.5, one set for radius pickup) **Item category filters (user: great idea, 2026-10-12)**: F10 checkboxes per category (gold, potions, food,
      plants, weapons, armour, ammo, scrolls/runes, keys/lockpicks, misc/trophies...), separate for radius AutoLoot
      and RMB QuickLoot? Recon: item category field on the item template (gCItem_PS?) and its values
- [x] Hand icon right of the hourglass while AutoLoot is on
- [x] README + package (0.7 released v2026.10.12.5 - user: final unless friends find bugs)

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
- [x] (2026-10-14, 0.3 with debug keys: screen 330/310, ring past 200, teacher past 200, damage) User test No Limits 0.2: teacher past 200, character screen shows > 200, potion of strength past 200, save /
      load keeps the value; J (DumpKey) logs base/total/max of the player
- [ ] **Bug (user 2026-10-13, Наблюдения.md #6): a friend (streamer) says No Limits doesn't work, stuck at 200.**
      0.2 was never user-tested. First get facts: his RisenMods.log ("[no-limits]" lines: version, "max 1000
      written", "patch on" or "skip: unexpected bytes"), installed version, which attribute (only strength /
      dexterity are covered!), teacher or potion, does the teach option disappear or does the value stay at 200.
      Suspects: other attribute; the teach command / gCSkillValue clamps again after IsSatisfied; GOG/other build
      More facts (user 2026-10-13, seen on the stream): dexterity never shows above 200, he put on dexterity rings to
      one-shot an enemy - damage didn't grow. Code check: every total in the game (Game.dll GetSkillValue /
      SetSkillModifyValue, Script.dll PSSkills::GetTotal / GetMaximum) = base + modifier clamped to
      ms_arrSkillInfos max, which 0.2 sets to 1000 - so with 0.2 applied, rings must go past 200. The symptom
      matches No Limits 0.1 exactly (released v2026.10.11 .. v2026.10.12.3; 0.1 kept the 200 maximum, only
      opened teachers). Most likely he has 0.1 (installer not re-run) or 0.2 didn't apply (log: "not the expected
      version"). Ask: installer -> No Limits version, or his RisenMods.log
      - [x] (0.3) Log watch: every ~3 s the player's strength / dexterity (trained, total, items +N, maximum) is
        logged when it changes - his log will show it without debug keys
      - User: friend updated everything before the stream -> he had 0.2.1, not 0.1. User's own log: after a load
        the table holds 1000 for 7/8, patch on - the game doesn't reset it.
      - Damage: Script_Game DoSwordDamage (+0xd90c0) = (damage amount + PSSkills::GetTotal(skill)) * multiplier, no
        200 clamp there; SetSkillBaseValue clamps only to the table max. Unchecked: DoPhysicalDamage / AssessDamage
        (protection, caps), character screen text. Note a +5/+10 ring changes damage by only that much.
      - [ ] (0.3 debug, REMOVE after the test) [Debug] BoostKey=END / BoostAmount=100 in the user's ini: END adds 100
        to trained strength + dexterity (PSSkills::SetBase modifier 0 = add). Test: press until > 200, character
        screen, log "player Strength: trained / total", hit the same enemy before/after. Don't save (or press with
        BoostAmount=-100 back)
      - [x] Test done (user 2026-10-13, 19:51): END x3 -> strength 330 / dexterity 310 on the character screen,
        log "maximum 1000", wolves one-shot -> past 200 works (screen + damage). So the friend's case is his setup
        or the TEACHER path (training past 200 at a teacher is still untested!). Next: test a teacher past 200
        ourselves (needs base ~200 + learning points: END to ~195, then train), and ask for his RisenMods.log
        (0.3 logs "player Dexterity: trained / total / maximum" + "patch:" lines)
      - [x] (user 2026-10-14) No max choice: MaxValue slider / ini key removed, fixed kMaxValue 9999 ("no limit"; not
        bigger: 4 digits on screen, PSSkills::SetBase modifier 3 scales with the maximum)
      - [x] (user 2026-10-14: works) Ring test (user 2026-10-14): [Debug] SetKey=HOME / SetValue=200 sets trained str+dex to 200 (SetBase
        modifier 2), then put on a ring -> does the screen show 200 + bonus?
      - [x] (user 2026-10-14: all works) Teacher test at 200 - HOME (str/dex = 200), learning points needed, a strength /
        dexterity teacher must still offer training and it must go to 201+
      - [x] (removed from code and ini 2026-10-14) Remove BoostKey/BoostAmount/SetKey/SetValue from the user's no_limits.ini after the teacher test (code may stay as a
        hidden debug key, it's off without the ini line)
- [x] (No Limits 0.3, built 2026-10-13, needs game test) **Bonuses don't count for teachers (user 2026-10-13, Наблюдения.md #8)**: rings / amulets / potions must not
      use up the teacher's range; trained and bonus values are separate and only add up in the total. Confirmed in
      code: IsSatisfied (+0x8a16fa) checks base [+0x20] + modifier [+0xA8] against teacher.max - so a +10 ring
      makes a "max 100" teacher stop at 90. Fix = in our cave compare only the base (which of +0x20/+0xA8 is the
      base - check with DumpKey wearing a ring). Same patch site as No Limits -> option in this mod ("Teachers
      ignore item bonuses", default on), for every skill, not only 7/8. Check permanent potions (do they raise base
      or modifier?) - the user counts them as bonuses too. Size: small
      Done: +0x20 = base (GetSkillBaseValue), +0xA8 = modifier (GetSkillModifyValue, ApplySkillModifyValues =
      equipment). Cave is rewritten by Apply: IgnoreBonus -> first 13 bytes = mov ecx,[r15+rbp*4+20h] + nop8;
      Enabled off -> ja (cave+65) becomes jmp (all skills keep teacher max). Site jump on while either option is on.
      [ ] Test: ring on, teacher whose max = base + ring bonus still offers training; DumpKey base/total;
      permanent potion -> base or modifier? (if base, potions still count - tell the user)
- [ ] **Character screen: total with a breakdown (user 2026-10-14)**: e.g. "240 (200 + 40)" = trained + item / other
      bonuses, at least for strength / dexterity (maybe every stat with a bonus). Recon (own session, ties into 5.12):
      the value is drawn by gCSkillPicbox (ViewMode=5, Character=PC_Hero, SkillType=N; notes/ui-recon.md section 3) -
      class NOT exported, only RTTI (.?AVgCSkillPicbox@@ -> vftable -> the draw / text update method). Find where it
      formats the number (call to gCSkills_PS::GetSkillValue Game.dll +0x978070 or PSSkills::GetTotal, then a
      "%d" / "%d/%d" format) and hook it to append " (base + bonus)" when bonus != 0 (GetSkillBaseValue +0x977b50,
      GetSkillModifyValue +0x977550). Check the width: the value field is ~135x16 in the 328 px attributes panel -
      maybe shorter "240 (+40)", or the breakdown in the tooltip (gCHintStatic POP_SKILLDESC) instead. Note:
      permanent potions raise the base (they'd be counted as "trained") - check
- [ ] Maybe: intelligence and other attributes too (user said "в целом ограничения прокачки") - ask
- [x] (0.1/0.2) Recon: where the cap lives - teacher dialog scripts in Script_Game.dll (CON_teach & co), template/.pak data
      (per-teacher max level), or a hard max in the attribute code (PSSkills / gCSkillValue?)
- [x] (0.1/0.2) Pick the smallest hook (patch the max-value check or the teacher limit table), learning point costs unchanged
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
- [x] (gone, see below) Discord shows its game overlay over the installer (user, 2026-10-11): check whether it's the exe living in the
      Risen folder (Discord "Registered games") - run a copy from Downloads
- [x] (see below) User test of the window: clicks, drag, folder picker, apply on the real game folder (overwrites dev dlls with release ones)
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
- [ ] **Native "Mods" page (user's decision 2026-10-13: the full native variant, not just a button opening F10).**
      A "Моды" entry in the game's own menu opens a page drawn by the game itself (its buttons / spin buttons /
      trackbars / labels, like OPTIONS_SETTINGS) with the list of our mods and their settings. Data source = the
      menu.dll registry (HubPage / HubItem): no mod changes. F10 ImGui window stays as the fallback. Steps:
  - [x] M1. (2026-10-13, notes/ui-recon.md section 8 + notes/ui-tree-rev2045.txt) Dev mod `mods/ui-lab` (no README -> not packaged): [Debug] DumpKey logs every loaded page (FindPage 0..29)
        as a tree (RTTI class, name, ID, pos, size, text); checks gCHudPage2* == eCGuiWindow2*. SaveKey ->
        SaveWindowAsClass of pages 13/18/22 (XML format of the layout, how spin buttons are described)
        Result: pages are eCGuiWindow2 (no offset), buttons = eCScriptRadioButton (SetOnClick exported), settings rows
        = label + 3 gCSpinButton (exported: SpinType / SpinBuddy / OnSpin). SaveWindowAsClass failed with a relative path
  - [x] M2. (2026-10-13) `scripts/risenmods` -> build\bin\scripts\Script_RisenMods.dll (build.bat builds + deploys it).
        Game.dll loads it, ScriptInit registers RM_Test / RM_OnClick_Mods / RM_OnSpin / RM_OnClick (descriptor:
        4 arrays {data,count,cap} of {bCString name; bCString second; fn}, scripts at +0x30; all counts 0 -> the
        game's message box). Each RM_* forwards to the `RisenScriptEvent` export of menu.dll (ui_lab.dll for now).
        ScriptCall("RM_Test") from ui-lab returned 42. Log via new ModInitIn(instance, name, "..\\mods").
        Installer must put this DLL into bin\scripts (and remove it with the last mod) - later, M8
  - [x] M3. (2026-10-13, user saw it) "Моды" = 6th tab in DLG_MENU_OPTIONS (user's choice: options row, main menu
        untouched; if a main menu button is wanted later, recentre 6 x 153 px from x=211). ui-lab clones the
        "Производительность" tab (eCGuiWindow2::Clone(page) -> eCScriptRadioButton), SetText, SetOnClick(proxy
        "RM_OnClick_Mods"), relayouts the row. Vanilla tab OnClick: OnClick_Menu_Video/Audio/Input/Settings/System
        (Settings = gui2.Open(22)). Click -> RM_OnClick_Mods -> gui2.Open(22) opened the settings sub-page: works.
        143 px tabs cut "Производительность" -> now 164 px tabs on a background widened 896 -> 1072 (built +
        deployed 07:17, NOT seen yet). Open: does the radio highlight follow our tab; game exit at 07:16:52 logged
        Game.dll+0x970cdb + SharedBase+0x7caa9 - the same pair was in the 2026-10-12 22:48 log without ui-lab,
        so probably the known exit-time exception (ask the user whether the game was quit then)
  - [x] M4-M6 (2026-10-13, user-tested in game, mods/menu/native_page.cpp; ui-lab keeps only the dumps). The tab
        lives in menu.dll. Click -> gui2.Open(22) + "mods mode": page 22's own controls hidden (all but the panel and
        textless pictures), our clones shown, restored when page 22 is hidden or via the "Параметры игры" tab (its
        OnClick wrapped: RM_OnClick_Settings -> Leave + OnClick_Menu_Settings). Layout (user's design): a LIST of
        mod buttons (clones of "Apply", 420x44, step 52, "menu" page left out - F10 only) -> a MOD view (title
        "Моды · <mod>", up to 14 rows label + ‹spin›, mouse wheel scrolls long mods by 2 rows, no page spin),
        "Назад" bottom-left (298,606 200x28) or Esc (WM_KEYDOWN swallowed in menu's WndProc) = back to the list.
        Facts: gCSpinButton +0xF0 buddy (arrows point at the value control's ID), +0xF8 OnSpin; the value control
        owns the script; OnSpin args = {int action; bCUnicodeString text}: 0 left arrow, 2 right, 3 text query - the
        script writes the shown text. Clones are created UNDER their siblings (parchment covers them) ->
        FFC.dll CFXWnd::BringToFront([wnd+0xA0]); a text set while hidden isn't drawn until repaint -> set again on
        show. A button hidden inside its own OnClick redraws itself pressed -> view switches are deferred to the next
        tick and hidden buttons are moved off-screen. Back ignored < 500 ms after a switch (suspected radio-group
        echo, watch the log line "Back right after a switch ignored"). Values: shared MenuValues / MenuWriteValue
        with the F10 window (ini + onChanged). Script names: RM_OnSpin_00..15, RM_OnClick_Item_00..13, _Back, _Mods,
        _Settings (34). AutoLoot got its own hand icon switch [Indicator] Show (item 15, Effect parents -> 28).
        Spin action 1 = click on the value itself (starts a rebind on key rows). Rebind: RMB is "back" in the
        game's menus -> InputCaptureSet(on) while waiting (after LMB is up) and until the bound key is released
        (user-tested 2026-10-13: MOUSE2 binds on the first try, page stays open). Capture off on the native page
        uses InputCaptureSet(..., restoreCursor=false): the cursor no longer jumps back to the clicked value.
        Open: texts follow menu.ini Language, not the game language; slider = spin steps (no trackbar); exit-time
        exception once at Engine.dll+0x6b1b04 (2026-10-13 07:53, stack lost to log rotation) besides the usual
        Game.dll+0x970cdb pair - watch it
  - [ ] M7. Robustness: language / resolution change, main menu from a running game, gamepad (_C layouts), save/load
        Resolution: the GUI virtual width depends on the aspect (data/ini/ConfigDefault.xml GUI.Aspect*: 16:9 = 1365,
        4:3 / 5:4 / 10:9 = 1024, vanilla layout 170 px further left). The game has no fullscreen option (ConfigLocal.xml
        FullScreen="false"); a 1600x900 window cuts the bottom even in vanilla. native_page.cpp: g_dx =
        (page width - 1365) / 2 is applied in Place() and template matching; on 1024 the tab row is 4 px gaps + 6 px
        margins (1016 px). Checked at 16:9 only (dx 0, 2026-10-13 08:19); 4:3 untested (try a 1280x960 window). A
        runtime size change only logs "native: screen A -> B" with vanilla/our x - see whether the game moves controls.
        User is now playing with the 08:19 build to catch crashes / bugs (will bring the log).
  - [ ] M8. shared/game_ui.h for 5.11 / 5.14, README Menu, release
- [ ] Native "Mods" tab in the game's own options menu. Was "unlikely"; 5.12 recon (notes/ui-recon.md) found exported
      routes: clone a button in DLG_MENU_OPTIONS (gEHudPage 18, tab row y=710) + own click handler via a
      bin\scripts\Script_*.dll ScriptInit. Depends on the 5.12 experiments

## Stage 5.9 — Quick lockpick (user's idea 2026-10-12; own mod `mods/quick-lockpick`, grows out of QuickLoot)
User's design (2026-10-12): a separate mod. Look at a locked chest, press a key (player picks it in F10): one lockpick
is spent and the lock opens at once, no minigame. F10 option "Loot the chest right after picking" (QuickLoot right
away). User thinks Risen lockpicking works like Gothic 1/2 (left/right combination minigame), not like Gothic 3
(skill level required) - check in recon and tell the user if it's different. Same theft rules as the game (owned
chest seen by NPCs = theft reaction as vanilla).
- [x] Recon (session 11): Risen is a HYBRID. Script_Game CanOpenLock(self=player, other=lock) +0x10ebf0: open -> ok;
      key in inventory (PSLock::GetKey + HasItems) -> ok; Difficulty > 3 (or <= 0) without key -> "Impossible"/"NoKey"
      (key-only locks); skill 0x1B (27, PSSkills::GetTotal / GetLockpick) < 1 -> "NoSkillLockpick"; Difficulty 2 needs
      skill >= 2, Difficulty 3 needs skill >= 3 -> "TooDifficult"; no It_Lockpick -> "NoLockpick"/"NoLockpickOrKey";
      failure shows the game's own message (sub_f0160). So like Gothic 3 a skill level gates the lock, then a Gothic 1/2
      minigame: Unlock +0x10e8b0 -> UseLockPick +0x10e1d0 (GUI2::SendLockpickInfo, lockpick action map) ->
      LockPickLoop +0x10d8d0: PSLock::GetLockPickCombination(i) (gELockCombi left/right) vs pressed key; wrong ->
      GetRandomNumber decides "LockpickBroke" (DeleteItems It_Lockpick, eff_ani_human_interact_lockpick_broke_01,
      "LastLockpick" when none left) or "LockpickAgain" (restart); all right -> Achievement::OnHandleLockpick,
      "OpenLock", eff_ani_human_interact_lockpick_end_01. Quick path plan: ScriptCall("CanOpenLock", player, chest)
      for all vanilla checks + messages, then DeleteItems one It_Lockpick, PSLock status = 1 (as UnlockWithKey in
      AutoLoot 0.7, fires OnLockStatusChanged), Achievement::OnHandleLockpick, end effect; optional QuickLoot.
      Old note: PSLock (status, difficulty / combination), player's lockpicking skill (skill table id), lockpick item
      (It_Lockpick?), the game's own pick path (interaction script / minigame end) - prefer calling the game's
      "lock picked" path so skill checks, XP, witnesses and lockpick breaking work as in vanilla
- [x] User's decisions (2026-10-12): skill gate exactly as the game (CanOpenLock, vanilla messages); the lockpick
      may survive - break chance falls with the lockpick skill (proposal: skill 1 = 50%, 2 = 30%, 3 = 15%, F10
      sliders, user to confirm); chests AND doors; F10 option "Loot right after picking". Later idea (user, maybe):
      quick lockpicking as its own trainable skill once 5.12 (own stats in the character screen) works
- [x] (0.1, session 12, built + deployed, needs game test) mods/quick-lockpick: key (default MOUSE2, same as
      QuickLoot) + break chances 50/30/15 by skill + "loot right after" on F10 page "Взлом". Flow: focus locked
      (status 0) chest/door, not owned, no weapon drawn -> ScriptCall CanOpenLock (vanilla refusal lines) -> key or
      difficulty 0: just open; else roll break (FindStackIndex It_Lockpick + DeleteItems 1), lock opens anyway ->
      status 1, Achievement::OnHandleLockpick, hero line via Script_Game sub_efef0 (+0xefef0, bytes checked;
      bool(void*, bCString by value, variants): "OpenLock" 5 / "LockpickBroke" 3 / "LastLockpick" 1), effects
      lockpick_end_01 / broke_01, optional ScriptCall AutoLoot. Vanilla minigame: a lockpick breaks only on a
      wrong move, 50%, never on the first step; success costs nothing.
      Owned locks are left to the vanilla minigame (theft broadcast sub_ed3e0 isn't exported) - ask the user
- [x] Bug fixed: WM_APP ids 0x5601/0x5602 collided with menu.dll -> F10 opened nothing (our proc swallowed
      WM_MENU_OPEN). Now 0x57xx; the per-mod id blocks are listed in shared/common.h
- [x] User test (2026-10-12): RMB without skill -> hero's vanilla "who could teach me" line. Released 0.1 (README
      RU/EN, zip), release v2026.10.12.6. Installer has no description for it yet (kKnown in installer/mods.cpp, needs installer 1.3)
- [ ] Friend's / user's test with skill + lockpicks: break chance, lines, doors, loot after, save/load

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
- [ ] User's design notes (2026-10-13, Наблюдения.md #4), to merge with the above:
      - chance shown in the focus name like the chest " (Пусто)" mark (same eCLocString hook), e.g. " (Кража 65%)"
      - chance from player's pickpocket skill + dexterity vs the NPC: nameless commoners (Harbor Town slums) easy,
        guards / named NPCs hard; NPC side from its attributes (dexterity?), level, faction - recon what NPCs have
      - only from behind, RMB (conflicts with QuickLoot RMB only on bodies - living NPC = pickpocket)
      - loot (user's decision 2026-10-13): NOT taken from the NPC's real inventory - generated "stealable" things
        that aren't in its pockets: simple plants, food, gold; wealthier NPC -> more gold. Our own loot tables per
        NPC kind (we analyse NPCs and write the tables)
      - XP proportional to difficulty, min 10
      - (user 2026-10-13) ONE theft per NPC, never again (after success; what failure does - design)
      - (user 2026-10-13) knocked-out NPC = 100%, sleeping NPC = 100%, dead NPC = no pickpocket at all.
        Knocked-out: pickpocket (generated loot) is separate from looting its inventory (Stage 5.13 RMB fix) -
        define which key does what on a downed NPC
      - later: "NPCs robbed: N" and own chance on the character screen (5.12)
      - Claude's notes: per-NPC "robbed" state needs per-save storage (biggest open question); check vanilla
        pickpocket first (dialog-based, skill-gated?) so the two don't double-dip
      - Future idea (user 2026-10-13): steal special quest items (like the Golden Goblets) and thereby change
        quests - needs quest recon (how quests check for the items, which NPCs/places hold them); after the base
        pickpocket works
- [ ] User's notes part 2 (2026-10-13, Наблюдения.md #4.1-4.3):
      - 4.1 Pickpocket teachers teach ONE level only (the rest cut from their dialogs); after that the chance grows
        by practice: each successful theft +N% (e.g. +1% per theft, 5 thefts = +5%), cap to design. Needs: recon of
        the teach dialogs (Script_Game teach conditions / info templates per teacher) + per-save counter (same
        storage question). Feasible; cutting dialog options = hook gCInfoConditionCanTeachSkill like No Limits
      - 4.2 Vanilla already lets you steal some items from some NPCs (sometimes quest-important), others never
        (e.g. an artifact you must buy). User wants those stealable too "if it doesn't break quests". Conflicts with
        "generated loot only" above -> design: generated loot + a chance for a real item from the inventory?
        Recon: how vanilla marks stealable items (item flag / NPC flag / quest script), which quests check that
        the item was bought (dialog flag) - each quest item needs a manual check. Medium-hard, do last
      - 4.3 Chance bonuses from equipment: some rings / amulets add +X% (table by item template name, our own
        list; possibly a magic/permanent bonus shown in the item text later). Sneaking (crouch) behind the NPC adds
        a bonus (player's sneak state - recon the movement mode). New items in the game = new templates in .pak:
        hard and not distributable as game assets - instead give existing rare rings/amulets the bonus


## Stage 5.12 — Own stats in the character screen (user's idea 2026-10-12, research first)
Can mods add their own lines to the game's character / attributes screen (e.g. "Thefts: 12", pickpocket chance)?
- [x] (static recon 2026-10-12 -> notes/ui-recon.md, tools/pak_list.py + tools/gui_windows.py) Recon: how the
      character screen is built. GUI2.dll is only a widget lib; layouts = data/compiled/library.pak ->
      compiled_windows.bin (GENOMFLE, 84 xwnd). PAGE_GAME_CHARACTER (gEHudPage 3) is pure data binding: unexported
      Game.dll controls gCSkillPicbox / gCSkillProgressBar with Character="PC_Hero" + SkillType (gESkill, 28 =
      Pickpocket) + ViewMode, all ID 0xFFFF - no code fills it, so vanilla controls can't show our values.
      Exported building blocks: gCHudModule2::GetSystem -> gCHudSystem2::FindPage/IsVisible/HandlePageChange;
      Engine eCGuiWindow2 tree (GetFirstChild/GetNextSibling, Get/SetText, Set/GetPosition, SetVisible,
      Clone(parent), LoadWindowA/SaveWindowAsClass XML). Big find: Game.dll loads every bin\scripts\*script_*.dll and
      calls its ScriptInit -> our own Script_RisenMods.dll can register new GUI handlers (OnClick/OnPaint) by name.
      Page id table, main menu / options button rows and ranked routes in the note
- [ ] Experiments in game (notes/ui-recon.md section 7): FindPage(3) tree dump (check gCHudPage2* == eCGuiWindow2*),
      Clone a label + SetText on the character page (survives close/open, load, resize, language?),
      SaveWindowAsClass dump, minimal Script_RisenMods.dll with ScriptInit, "Mods" button cloned into DLG_MENU_OPTIONS
- [ ] Pick the route with the user after the experiments (runtime clone / own script DLL / layout data mod / ImGui
      fallback), then a shared helper in shared/ (game_ui.h) for 5.11 / 5.14 / the "Mods" menu item

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
- [x] (user 2026-10-12: no delay any more) (0.7: plunder asked up to 5 times while the body is empty + "ms after it was seen dead" in the log - read
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
- [x] Test (user 2026-10-12): quest grain is collected, no AutoLoot delay
- [x] (2026-10-14, built, needs game test: user said held RMB took one item only - it was press-edge only; now repeats every 250 ms while held, silent log on repeats, emptied containers skipped) (low priority, user 2026-10-12) Holding RMB for QuickLoot "doesn't quite work as it should" - ask what exactly
- [ ] Re-check after 0.8 (user asked): arrows / bolts in flight are not picked up - shoot with F on, read the log
- [x] (built 2026-10-13, needs game test) (user 2026-10-13, Наблюдения.md #1) RMB QuickLoot does nothing on an NPC knocked out in a non-lethal fight -
      the player has to LMB and loot by hand. Cause: by design since 0.1 - IsDeadBody() needs isDead, README says
      "never taken: unconscious people" (Sara at the start). Plan: RMB accepts isDown NPCs too, but only when the
      game itself allows looting (CanInteract_Player_Loot / the same script path as LMB), so theft / reactions stay
      vanilla; radius AutoLoot (F) keeps skipping them. Check: does vanilla count it as theft, does the NPC react
      on waking up. Update README RU/EN
      Done: IsDownBody = npc && !dead && isDown && inv stacks && PSInteraction::CanInteractWith(player, npc); QuickLoot
      treats it as a body (game's AutoLoot script + quest stacks), log "quickloot: knocked-out ...". README updated.
      (user 2026-10-14) AutoLoot (F) takes them too: [AutoLoot] KnockedOut=1 (default on, F10 checkbox under
      "bodies", needs Bodies=1), same IsDownBody check. Built, needs game test.
      [ ] Test: knock out in a duel -> RMB takes; F with bodies on takes; no reaction / theft when watched? NPC waking up; CanInteractWith
      false -> log "not lootable"


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

## Stage 5.15 — One log for the whole family + crash reports (user's request 2026-10-12, a friend crashed on stream)
- [x] shared/common.cpp: every mod writes to bin\mods\RisenMods.log, lines "[mod]"-tagged, one WriteFile per line
      on a FILE_APPEND_DATA handle (no interleaving between DLLs), 2 MB cap per launch. Same ModInit/Log API, so new
      mods get it for free; the installer keeps its own %TEMP% log (ModInitAt). Loader 1.1: LogStartSession moves
      the old log to RisenMods.prev.log (a crash survives the restart), deletes old per-mod logs, logs Windows
      build + exe path, "game closed normally" on a clean exit (missing line = crash / killed)
- [x] loader/crash_log.cpp: vectored handler (sentry replaces SetUnhandledExceptionFilter, so the VEH is what
      sees crashes), fatal codes only, max 6 per launch: code, module+offset, AV address, registers, stack via
      RtlLookupFunctionEntry/RtlVirtualUnwind. Tested with a temporary null write (key End): logged
      autoloot.dll+0x856f, writing 0x0, 3 frames; test key removed. Map offsets: tools\pe_symbol.py
- [x] F10: "Log folder" button (explorer /select the log) next to Close, tooltip which file to send. READMEs RU
      updated (EN sections never mentioned logs)
- [x] Released v2026.10.12.8: every mod +0.0.1 (each zip carries loader 1.1; installer updates by version)
- [x] Installer: also delete RisenMods*.log in CleanupIfEmpty (installer 1.3, uncommitted)
- [ ] Friend's crash: ask for RisenMods.prev.log once he runs this version
- [x] READMEs (all mods + repo README.md): "Bugs and crashes: open an issue, attach RisenMods.log" - pushed 455247c;
      the mod zips get it with their next release

## Stage 5.16 — Installer 1.3 + readable text (user 2026-10-12)
Installer 1.3: installer/main.cpp (footer links line: "Bug or crash? Report it on GitHub" -> kIssuesPage with a
tooltip about RisenMods.log, "Mods on GitHub" -> kRepoPage; footer reserve +1 text line), installer/mods.cpp (kKnown:
quick_lockpick description, AutoLoot description updated for theft rules; CleanupIfEmpty deletes RisenMods*.log).
- [x] Text hard to read on parchment: ImGuiCol_TextLink was never set (ImGui's default light blue) - now dark red;
      Text and TextDisabled darker in shared/risen_style.cpp (installer + F10), installer kGood/kBad darker.
      User liked it; no background panel needed
- [x] User checked the installer (links line, links, descriptions)
- [x] Menu 1.1.2 (palette only), deployed, repackaged
- [x] Released v2026.10.12.9 (installer 1.3, menu 1.1.2; other mods unchanged)

## Stage 5.17 — Russian voice-over issues (user 2026-10-13, Наблюдения.md #2-3; not our mods, research first)
- [ ] Some characters are too quiet in the Russian voice-over (user: +50% would fit). Web search found no ready
      fix. Options: (a) check which files / speakers (speech pak, compare peak/LUFS per speaker with a script);
      (b) runtime per-speaker gain in a mod (hook the speech playback, table speaker/file -> gain); (c) normalized
      repack of the speech pak - can't be distributed (game assets), only a local tool. Low priority
- [ ] Subtitles don't match the Russian speech. Steam forum: the Steam release's voice (Sara, the Harbor Town gate
      guard, ...) differs from the Novy Disk text; one player rewrote Sara's lines by hand, no public file found.
      Options: search for a fixed strings pak / old Novy Disk text; or a local tool: speech-to-text (Whisper) over
      the Russian speech files -> list of mismatching lines -> corrected strings (text is distributable only as
      our own patch). Size: several sessions; first measure how many lines differ
- Sources: https://steamcommunity.com/app/40300/discussions/0/3758851615160881458 ,
  https://steamcommunity.com/app/40300/discussions/0/46476690965296705

## Stage 5.20 — Damage numbers (user's idea 2026-10-13)
Floating damage numbers over the target on every hit (like in many RPGs). Feasible: the damage scripts in
Script_Game (DoDamage / DoSwordDamage / AssessDamage ...) know attacker, target and amount; hook one common point
(where the final amount is subtracted from the HP - or watch target HP changes per frame, simpler and safer),
project the target's head position to the screen (camera matrix, already read by AutoLoot) and draw the numbers in
the menu.dll overlay (ImGui, game font), rising and fading. F10: on/off, only player's hits / also incoming,
colours (crit, fire...). Size: 1-2 sessions.
- [ ] Recon: the final damage point (AssessDamage -> HP change), attacker = player check, projection to screen
- [ ] Prototype: HP-delta watcher for NPCs near the player + overlay numbers; then decide whether to hook damage

## Stage 5.18 — Quick mining (user 2026-10-13, Наблюдения.md #5; QuickLoot family, like quick-lockpick)
RMB on an ore vein: if the player has learned mining and carries a pickaxe -> the ore is mined at once (no animation),
otherwise the vanilla behaviour / hero line. Same pattern as quick-lockpick (game script decides, we skip the action).
- [ ] Recon: ore vein entity (focus name, interaction type), Script_Game CanUse / mining script (skill check,
      pickaxe check, how many nuggets, does the vein deplete), what the vanilla animation end gives (XP? item log)
- [ ] mods/quick-mining or part of quick-lockpick (rename to quick-actions?) - ask the user
- [ ] Test: without skill, without pickaxe, depleted vein, save/load. Size: ~1 session

## Stage 5.19 — Draw distance (user 2026-10-13, Наблюдения.md #7)
data\ini\ConfigDefault.xml / ConfigPerformance*.xml already have ViewDistance (level 1-3) with ViewRange per level
(2500 / 4000 / 7500, one 6000) and ScreenObjectDistanceCulling="0.008" (small objects culled by screen size).
- [ ] Check what the game's own options menu changes (is there a view distance slider, which value it writes)
- [ ] Experiment: higher ViewRange / lower culling in ConfigLocal.xml (backup first) - visible difference, FPS, fog
      or LOD pop-in (far terrain / LOD meshes may be the real limit)
- [ ] If it helps: F10 slider in the Window mod (or own small mod) writing the value at runtime (find the engine
      setter) or into ConfigLocal.xml with "restart needed". Size: 1 session, result not guaranteed (LOD limits)

## Stage 5.21 — World editing: old Risen Editor recon -> own Rust tools (user's idea 2026-10-14; next: spawn/copy/delete entities)
Goal: get the community world editor (Risen Editor by Galrath 434, ~2013, place/copy/delete NPCs, monsters, items)
working with the 2023 64-bit re-release; later an AI-friendly CLI over the world/template data and maybe a nicer
world-editing wrapper. Old 32-bit DLL mods (Elusively Patched Unit, Baltram's Script_Extensions) are known broken on
64-bit. Savegames are compatible between the old and new SKU -> data formats probably unchanged (unverified).
Prior art: Baltram's rmtools (github.com/Baltram/rmtools, open source: Rimy3D for ._xmsh/._xmac/._xcom, other converters),
total conversion "Neuland"/DTC (worldofrisen.de/download_89.htm). WoP forum blocks bots - download links may need the user.
- [x] Recon 2026-10-14: Risen Editor = standalone C# (.NET 4, x86, SlimDX/DX11) OFFLINE file editor, never touches
      the game process -> 32/64-bit of the game doesn't matter. Source: github.com/hhergeth/RisenEditor (= galrath434;
      3 forks are identical copies) - does NOT build: author's GameLibrary.dll missing. Working binaries from the WoP
      thread (upload.worldofplayers.de/files9/tA98Wm2x5Risen_Editor.rar = newest 2013-08-01, Yfq8...Risen_Editor.rar
      = 2013-07-30, RisenEditor_master.zip = source) - host stalls behind the user's corporate VPN, fine without it.
      Installed outside the game/repo: D:\RisenModding\ed_new\Risen Editor (ed_old, ed_src next to it).
      Game path: registry HKLM\SOFTWARE\WOW6432Node\Deep Silver\Risen\install_dir (present, wins over Settings.ini).
      Pak format unchanged (G3V0, offsets at +24); world = data\common\projects.pak (410 .lrent).
- [x] Fix: editor threw in RisenPak..ctor on file names present in two paks (64-bit build has 19: 17 .tab in
      common\strings.pak + compiled\strings.pak, 2 fonts in gui2.pak + gui2_back.pak). Patched 1 byte in
      RisenEditor.exe: RisenPak..ctor IL `ldarg.3` (DoOverwrite) -> `ldc.i4.1` (file offset 0x42E7C, 05 -> 17), so the
      later pak wins (compiled strings). Original kept as RisenEditor.exe.orig. Editor starts (user saw the main window).
      Overwriting the exe in place failed (Errno 22, likely AV) - write a copy and rename.
- [x] User test: World.wrl loads (slow, ~700 MB, everything at once), meshes + textures render, Properties work.
      Navigation (from source): double-click an entity in the Scene tab or Ctrl+G = camera jumps to it; opening one
      .lrent jumps to its first object. Camera (GameLibrary.Camera.Update): WASD, E/Q up/down, RMB look, wheel =
      speed; movement is per frame (no delta time) -> slow at low FPS. Open single layers, not World.wrl.
- [x] Patched GameLibrary.dll (orig = GameLibrary.dll.orig): Camera Speed 30 -> 150 (both ctors, ldc.r4 at file
      0x1FE1A / 0x2074F), wheel factor 0.93/1.07 -> 0.77/1.3 (0x1FEB6 / 0x1FEBD). RisenEditor.exe: LargeAddressAware
      flag (4 GB instead of 2). IL tools: pip dnfile + dncil, scripts il_find/il_type/il_dis (session scratchpad;
      move to risen-mods/tools if we keep patching)
- [x] Save test 2026-10-14: FAILED. Save wrote data\common\projects\World\SysDyn_{C3960F11-...}.lrent (same size
      18218, differs from the pak copy from byte 1815), then an unhandled EndOfStreamException ("Попытка чтения после
      конца потока"); the user pressed Continue, nothing more was written. Stack trace not captured (next time:
      "Сведения" button). Not the stream compare (GameLibrary EXT_Stream.StreamsContentsAreEqual checks lengths) -
      likely a BinaryReader re-reading some structure. Rolled back: file moved to
      D:\RisenModding\backup\data-before-save-20261014-162830\sysdyn_written_by_editor.lrent, data\ == snapshot
      (filelist.tsv there). Editor saves = loose files at <game>\data\common\projects\<layer path> (pak path minus .pak).
Decision 2026-10-14 (user): the old editor is a dead end for daily work (slow single-threaded load, hangs, save
crashes, closed GameLibrary.dll). Rewrite in Rust instead: one format library + CLI now, own editor later.
Layout (new repo or risen-mods/rust/, decide at setup):
    risen-formats  (lib crate: pak G3V0, lrent/sec/wrl/wrldatasc, tple, later xmsh/ximg/xmat)  <- shared core
    risen-cli      (bin: list / find / show / edit, --json output for AI agents)
    risen-editor   (later: GUI on egui + wgpu or Bevy, multithreaded loading, on top of risen-formats)
Reference implementation to port from: D:\RisenModding\ed_src\RisenEditor-master (C#): Code\Risen FileSystem\
LRENT_Base.cs + GENOME_Base.cs (property-set serialization), TpleFile.cs, tabFile.cs, Loader\RisenPakFile.cs;
our tools/pak_list.py for the pak directory. Old editor stays usable as a viewer / for cross-checking.
- [x] Rust toolchain on D: 2026-10-14: rustc 1.99 stable, host x86_64-pc-windows-gnu (no MSVC), profile minimal +
      clippy, rustfmt, rust-analyzer; RUSTUP_HOME=D:\Rust\rustup, CARGO_HOME=D:\Rust\cargo (user env vars),
      D:\Rust\cargo\bin on user PATH (already-open shells need the vars set by hand). Workspace risen-mods\rust\
      (risen-formats lib + risen-cli bin `risen`, binrw 0.15 for declarative read+write), rust/target/ gitignored.
      Agent tooling researched: only the official rust-analyzer-lsp Claude Code plugin is worth it (installed, works);
      Orca keeps its old env (main process lives for weeks) -> RUSTUP_HOME/CARGO_HOME also set in ~/.claude/
      settings.json "env", otherwise the rustup proxy has no default toolchain and rust-analyzer exits 1;
      no ready Genome pak/lrent parsers exist, no useful C#->Rust converters.
- [x] risen-formats: pak reader 2026-10-14 (rust/risen-formats/src/pak.rs; real directory tree from the C# reader,
      not the Python heuristic scan; only the directory is read on open, data on demand; names Latin-1 = lossless).
      CLI: `risen pak-list <pak> [filter]`, `risen pak-extract <pak> <name|path> <out>`. Verified: all 25 paks
      (161951 files) == tools/pak_list.py (name, stored size, enc+comp, order); extract == independent Python zlib
      read for plain + zlib entries (strings, projects, animations). Header +16 = default compression (0/1/2).
      Build gotcha: GNU host + windows-sys needs dlltool; rustup's GNU dlltool wants as.exe (missing) ->
      rust/.cargo/config.toml -Cdlltool=D:/Rust/tools/llvm-dlltool.exe (copy of llvm-ar.exe from llvm-tools;
      re-copy after a toolchain update if builds break). Std 1.99 has its own read_le -> don't call binrw's
      read_le on primitives (ambiguity warning), use BinRead::read for structs.
- [ ] Game VFS over all paks + loose data\ files (the 19 duplicate names: later pak wins like the patched editor;
      loose files override paks - check the game's real order when we first need it)
- [x] .lrent read + write 2026-10-14 (risen-formats: archive.rs GENOMFLE + string table, props.rs property
      objects, lrent.rs layer/context/entities, bin_io.rs). Format: "GENOMFLE", u16 1, u32 table offset; body =
      u32 0xD0DEFADE + gCDynamicLayer object; its class data = "GENOMEDL" + u16 + eCEntityDynamicContext object
      whose class data = entities + (parent, child) pairs ending (-1,-1); string table u32 0xDEADBEEF, u8 1, count,
      (u16 len, bytes). Property values + unknown PS class data kept raw (sized), string table kept as read ->
      `risen lrent-check`: ALL 410 .lrent in projects.pak byte-identical after read -> write.
      Risen Editor bugs found (eCGeometryEntity): RenderAlpha+ViewRange exist from v212 (editor: 213),
      CacheInRange from v213 (editor: 214) -> v212/213 entities (OutMain_Nav, Levelmesh_Water_Rivermesh, 2 test
      levels) were misread by it. Its save also rebuilt the string table in first-use order.
      CLI: `risen layers [filter]`, `risen entities <layer|file> [--props] [--json]` (Harbour_Town_NPC_01 -> 80
      entities, Ingvar -2240 493 -20187 with gCNPC_PS etc.), `risen find <name|guid> [--props] [--json]` over all
      410 layers in ~1 s. Position = world matrix translation (m[12..15]); entity 0 "DRE" = layer root;
      creator = template GUID (name lookup needs templates.pak - next).
- [ ] .sec / .wrl round trip (small files, same archive + property objects; gCSector lists its layers)
- [ ] First real edit: `risen move <layer> <guid|index> --to x y z | --by dx dy dz [--out f] [--dry-run]` DONE
      2026-10-14 (lrent.rs DynamicContext::translate + update_root_bounds). Writes loose file
      data\common\projects\<layer path>; an existing loose file is edited further. Moves world+local translation,
      world/geo box, world/geo sphere of the entity and its descendants (only direct children of the root for now).
      Verified on all 410 layers: root DRE matrix = identity (local == world for top-level entities), root geo box =
      union of entity geo boxes, context_box = the same box, root sphere = box center + half diagonal (stored
      spheres differ in float rounding -> only recomputed when the box changes). Hierarchy = (0, i) pairs.
      `risen entities <layer> --geometry` prints matrices/boxes.
      IN-GAME TEST PASSED 2026-10-14: It_Plant_Mushroom #37 {E2121F9E-...} moved from (-38525, 283, 800) to
      (-37450, 120, -5300) appeared next to Sara in a NEW game (loose file
      data\common\projects\World\Outdoor\Game_Start\Game_Start_Item.lrent; rollback = delete data\common\projects).
      First try: the game IGNORED the loose file. Cause: Risen.exe starts the engine with
      `-mnt=mountlist_packed.ini`; bin\mountlist_packed.ini (plain ASCII) has [Root] NoPhysical=true -> only paks,
      loose files under the [Packed] dirs (data/common/projects, data/compiled/..., data/raw/...) are not mounted.
      Fix: NoPhysical=false (original in risen-mods\backup\bin-original\mountlist_packed.ini). No crash, no conflict
      dialog. Note: this also makes the 14 loose .fx/.fxh in data\compiled\effects\shadermaterial (shipped with the
      2023 build) visible - watch for shader differences. Engine strings: eCVirtualFileSystem::ProcessMountList /
      Mount [PAK]/[DIR], NoWatch, NoPacked, NoPhysical, RootPath, DisplayCacheConflicts (ge3dialogs.dll).
      For release: world-edit mods need NoPhysical=false -> the installer must patch mountlist_packed.ini (and undo it)
- [x] Templates (.tple) read + write 2026-10-14 (risen-formats tple.rs): GENOMFLE body = "GENOMETP", u16 version
      (219), u32 count, template entities, hierarchy pairs ending (-1,-1). Entity = u16 version, PropertyId,
      flag bytes (7 + (v<215) + (v>=214)), u8 has-ref + PropertyId, render alpha, view range, u16 insert type,
      name str, position, quaternion, FILETIME, version-dependent bytes (TpleFile.cs), u32 set count, sets =
      (u16 id, str class, u32 size, property object, u32 0xDEADC0DE). Entity with a ref template (instance of
      another template) has NO set count/sets. Entity 0 = file root ("X.tple"), entity 1 = the template, its GUID
      = creator GUID of world entities. 13 multi-templates (Objects\_MultiTemplates, prefabs of ref instances),
      no world entity uses them. `risen tple-check`: all 2679 .tple byte-identical. CLI: `risen templates
      [filter|guid] [--props] [--json]`; `entities` / `find` now print the creator template name (JSON "template").
      Whole world: 50766 entities, 49500 with creator, 49409 resolved; the other 91 (82 GUIDs) point to templates
      that no longer exist (test levels, a few VFX / Goblin / Levelmesh water meshes).
- [x] Spawn / copy 2026-10-14: moving property sets between files needs string index
      remapping, and string indices also sit inside raw class data (would need full class parsers like the
      C# GENOME_Base). The old editor never spawned from templates either (only cloned within a loaded layer).
      So: copy an existing instance INSIDE ITS OWN LAYER (same string table, bytes reused as is).
      lrent.rs: DynamicContext::duplicate (subtree, new v4 GUIDs, hierarchy pairs before the terminator),
      Lrent::translate = context translate + gCNavigation_PS fix: LastUseableNavigationPosition (== entity
      position in 2251/2254 world NPCs) set to the new position, LastUseableNavigationZoneProxy -> none (112
      world NPCs already have none). No entity has its own GUID in its properties. CLI: `risen copy <layer>
      <entity> --to|--by [--count]`, `risen spawn <template> --to x y z [--layer L] [--count N --spread R]`
      (takes the layer with most top-level instances). Scratch check: old entities unchanged, GUIDs unique.
      Test written: 2x Wolf_Gamestart (Harbour_Outdoor_Monster_01) at (-36650, 200, -5300) and 2x Wolf from the
      far Wild_Outdoor_Monster_01 at (-36650, 200, -4300) near Sara -> also tells whether far layers load.
      Rollback: delete data\common\projects\World\Outdoor\{Harbour_Outdoor,Wild_Outdoor}.
      IN-GAME TEST PASSED 2026-10-14 (new game): all 4 wolves at Sara (2 Wolf_Gamestart = "hungry", 2 Wolf),
      no crash -> a far area's layer (Wild_Outdoor) is loaded at the start too: spawning into any instance's
      layer works anywhere. Loose files left in place (Game_Start mushroom, 2 wolf layers).
- [x] Delete 2026-10-14: DynamicContext::remove (subtree, hierarchy renumbered, root
      bounds refreshed). `risen delete <layer|file> <entity> [--out] [--dry-run]` warns when a removed GUID is
      still referenced (byte search of the 16 GUID bytes in this layer + every other layer, loose first).
      References between entities exist: gCNavPath_PS.ZoneA/BEntityProxy (1203), gCNavHelper_PS.LinkedTo (111),
      gCNPC_PS.GuardPoint (9), event groups (trigger + children, BD_Don_Events) reference each other in raw
      class data. NPCs (Ingvar) have no GUID references - scripts use names. Fix found on the way: `copy` of a
      group now replaces the group's old GUIDs inside the copied property values + raw class data
      (PropertyObject::replace_guid), else the copy drove the original. Checks: copy + delete of the copy =
      original bytes (wolf; 16-entity event group), deleting the original then leaves no refs in the layer.
      `entities` / `find` print the parent index (JSON "parent") for nested entities (902 in the world).
      Edit commands also take a .lrent file path. In-game test written: deleted Obj_Dec_Barrel_Broken
      (Game_Start_Dyn #1126), Beach_Seaman_Dead corpse (Game_Start_NPC_01 #7), copied Wolf_Gamestart #108
      (-> 3 wolves remain at Sara). IN-GAME TEST PASSED 2026-10-14 (new game): corpse gone, barrel gone,
      3 wolves, no crash. Loose test files still in data\common\projects (5 layers).
- [ ] Then: spawn into any layer (string remapping / class parsers), strings (.tab)
- [ ] Future (user: "это прорыв"): LIVE world editing through a dll mod - an in-game bridge (local socket / named
      pipe, JSON commands) so any agent or our editor can find / move / spawn / delete entities in the running game
      while the user watches. Same verbs as risen-cli (find, show, move, spawn), resolve entities by GUID every
      command (never keep entity pointers across frames). Needs RE: entity lookup by GUID, set world matrix +
      physics sync, spawn from template. Optional "bake": write the live change into the .lrent via risen-formats
- [ ] Later: risen-editor GUI (viewer first: meshes/textures from xmsh/ximg - Baltram's rmtools has format code)
- [ ] Optional / low priority: editor-trace profiler for the OLD RisenEditor.exe (C# + ClrMD, csc.exe) - only if we
      still use the old editor; symptoms: World.wrl load minutes, double-click picking freeze, exit hangs minutes.
      Untextured white/grey meshes in the old editor = materials it can't parse.

## Stage 6 — Release
- [x] README with install/uninstall steps (every mod, RU + EN)
- [ ] Test full playthrough segments with saves (back up saves first)
