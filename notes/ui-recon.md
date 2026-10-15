# UI recon: game menus and screens (Steam Risen x64, Rev. 2045)

Goal (TODO 5.12): add our own lines and items to the game's own screens, such as the character screen ("Thefts: 12",
progress of 5.14), the main menu and the options ("Mods" item).
This recon is **static only** (exports, disassembly, the layout data). Nothing here has been tried in game yet.
**Guess** marks inferences. RVAs are for this build; resolve exports by mangled name.

Tools:
- `tools/pak_list.py <pak> [regex]` lists a pak. `--extract <name> <out>` extracts one file.
- `tools/gui_windows.py compiled_windows.bin [--raw]` dumps every window of every page as one line each.

## 1. Who does what

| Layer | Contents |
|---|---|
| `GUI2.dll` | A pure widget library. It has one export, `FFGui2_QueryModule` (an `IFFModuleGui2` interface), and internal `CFX*` controls. No game logic and no strings. Don't RE it. |
| `Engine.dll` | The window framework. `eCGuiWindow2` → `eCGuiStatic2`, `eCGuiFrame2`, `eCGuiListCtrl2`, …; the script controls `eCScriptStatic`, `eCScriptListCtrl`, … (their `OnPaint` / `OnInit` / `OnClick` are script names); `eCDocArchive` (layout loading); `eCScriptFunctionMap` (the script name registry). **Many exports.** |
| `Game.dll` | Pages and game controls. `gCHudModule2` / `gCHudSystem2` (the page manager), `gCHudPage2` / `gCHudDialog2` / `gCHudPopUp2` are exported. The game controls are NOT exported (only RTTI/vtables): `gCSkillPicbox`, `gCSkillProgressBar`, `gCMiscLabel`, `gCMiscProgressBar`, `gCLocIDLabel`, `gCHintStatic`, `gCEquipPicbox2`, `gCQuickPicbox2`, `gCInventoryList`, `gCFilterButton`, `gCSpinButton`, `gCPageCloseButton`, `gCAudioVolumeTrackbar2`, … |
| `Script.dll` | `class GUI2` (global `gui2`) is a thin wrapper over `gCHudSystem2`: `Open/Close/Show/IsVisible(gEHudPage)`, `FindPage`, `PrintGameLog*`, `DoModal`, `Modal*`. |
| `Script_Game.dll` | The behaviour: a page's `PageControl` (OnControl_Character, …) and button handlers (OnClick_Menu_Options, OnSpin_*, InvEvent_*). They are exported by name with the signature `__int64 f(gCScriptProcessingUnit*, void* self, void* other, __int64 args)`. |
| Data | **All layouts** live in `data/compiled/library.pak` → `compiled_windows.bin` (GENOMFLE, 84 `.xwnd` documents, 1347 windows, not compressed). The raw path is `#G3:/data/raw/gui2/windows/*.xwnd` (not shipped). The fonts are in `data/common/gui2.pak`. The textures (GUI_TexturePage*, GUI_IconSets01) are in `data/compiled/images.pak`. |

## 2. Pages (gEHudPage) and their PageControl

Taken from `PageID` / `PageControl` in compiled_windows.bin. Every page also has a `_C` variant (controller layout).

| ID | Page (xwnd) | PageControl |
|---|---|---|
| 0 | None | |
| 1 | PAGE_GAME_MAIN (HUD) | OnControl_Game |
| 2 | PAGE_GAME_INVENTORY | OnControl_Inventory |
| **3** | **PAGE_GAME_CHARACTER** | OnControl_Character |
| 4 | PAGE_GAME_LOG | OnControl_Log |
| 5 | PAGE_GAME_MAP | OnControl_Map |
| 6 / 7 | CRAFTSELECT / ITEMSELECT | OnControl_CraftSelect / ItemSelect |
| 8 / 9 / 10 | LOOT / PICKPOCKET / TRADE | OnControl_Loot / Pickpocket / Trade |
| 11 / 12 | DIALOG / TALK | OnControl_Dialog / Talk |
| **13** | **DLG_MENU_MAIN** | OnControl_Menu_Main |
| 14 | DLG_MENU_GAME | OnControl_Menu_Game |
| 15 / 16 / 17 | MENU_GAME_LOAD / SAVE / ACHIEVEMENT | OnControl_Menu_Load / Save / Achievement |
| **18** | **DLG_MENU_OPTIONS** | OnControl_Menu_Options |
| 19 – 23 | OPTIONS_VIDEO / AUDIO / INPUT / SETTINGS / SYSTEM | OnControl_Menu_Video … System |
| 24 / 25 | MENU_CREDITS / MENU_CHEATS | |
| 26 | DLG_MENU_START ("Press START") | OnX360Control_Menu_Start |
| 27 / 28 / 29 | MENU_BACK / OUTRO / LOADING | |

PageControl is called when the page opens or closes. It allows or denies it: OnControl_Character only closes the page when the player is transformed. It does **not** fill in the data.

Other files: `MOD_*.xwnd` (modal dialogs: StackAmount, Wait, ActionKey, …), `POP_*.xwnd` (tooltips: POP_SKILLBASE, POP_SKILLDESC, POP_INVITEM).

## 3. Character screen: how it is built

PAGE_GAME_CHARACTER (1024x768 virtual) is the inventory on the right (380x430, filters, list, quick slots) plus the character on the left. **Every line is data-bound with no code at all.** A row is usually 3 controls with the same `Character="PC_Hero"` and `SkillType=enum N`:
- `gCSkillPicbox` with `ViewMode=3` (icon 45x41, background `skillicon_back`);
- `gCSkillPicbox` with `ViewMode=5` / text (name/value, `HideEmpty`), or a label 135x16;
- `gCSkillProgressBar` (`RangeMax` 1/3/10, `Pos`, `skillprogress_off/on`).

Blocks are `paperpanel` panels: (0,0) 400x193, (400,0) 200x193, (0,195) / (200,195) / (400,195) 200x236 each, the attributes panel (641,0) 328x244, the protections panel (0,0) 331x244 (rows 25x25 + 275x24 `shadowdeko`). The page caption is `gCLocIDLabel LocString="HUD2_Caption_Character"`. **Every** row has `ID = 0xFFFF`, so `GetDlgItem` / `FindDlgCtrl` won't find them; you have to walk the tree and tell them apart by `SkillType` / position.

`gESkill` values (from the enum registration code in Game.dll): 16 Combat_Sword, 17 Axe, 18 Staff, 19 Bow, 20 CrossBow, 21 Magic_Circle, 22 Fireball, 23 Frost, 24 Missile, 25 Misc_Smith, 26 Mining, 27 Lockpick, **28 Pickpocket**, 29 Sneak, 30 Acrobat, 31 Alchemy, 32 Scribe, 33 Trophy.
0..15 are the stats and protections: 15 = Prot_Magic (confirmed). **Guess** from the layout: 2 LV, 3 XP, 4 LP, 7–9 STR/DEX/INT, 10–15 protections (Edge, Blunt, Point, Fire, Ice, Magic).
Other enums: `gEMiscInfo` (Health, Mana, StatusEffect, Guild, …) for gCMiscLabel / gCMiscProgressBar; `gEInfoView` (Header, Description, Learnpoints, NextLevel, GoldCosts, Damage, RequiredSkills, …) for the tooltips (gCHintStatic, `POP_SKILLDESC`, strings `SKILLDESC_%s_%02u`).

**Conclusion:** a vanilla control can only show a vanilla skill. For our own value we need a text control (eCGuiStatic2 / gCLocIDLabel clone) whose text we set ourselves, or a picbox/progress bar whose drawing we take over.

## 4. Main menu and options

- DLG_MENU_MAIN (13) has a bottom row of 5 buttons, each 153x41 at y=653: Continue (x119), Game (277), Options (435), Credits (593), Exit (751). OnClick = `OnClick_Continue` / `OnClick_Menu_Game` / `OnClick_Menu_Options` / `OnClick_Menu_Credits` / `OnClick_ExitGame`. The row is full, so a 6th button means moving the others or putting it somewhere else.
- DLG_MENU_OPTIONS (18) has a tab row at y=710 with 5 buttons, each 164x23 (x 88…776): Video / Audio / Input / Settings / System (`OnClick_Menu_Video` …), plus the same main row at y=653. A "Mods" tab = 6 tabs of ~137 px each, or our own row.
- DLG_MENU_GAME (14) has a row at y=710: NewGame, Load, Save, Achievement.
- Options sub-pages use `gCSpinButton` + `OnSpin_*` (OnSpin_Difficulty, OnSpin_ShowSubtitles, …) and trackbars with control IDs (Audio: 1001–1005, labels 101–105).

## 5. Building blocks for our code (all exported)

**Pages:** `gCHudModule2::GetSystem()` (static) → `gCHudSystem2*`. Its methods: `FindPage(gEHudPage)` → `gCHudPage2*`, `IsVisible(gEHudPage)`, `Open`, `Close`, `Show(page, bool)`, `GetCurrentModal`, `CloneFrame(bCString)` / `CloneDialog(bCString)` (a copy of a loaded layout by its name), `PrintGameLog`, `SendGameMessage(int, i64, i64, gEHudPage, bool)`.
Hook points: `gCHudSystem2::HandlePageChange(gEHudPage, bool)` 0x83e290 (private but exported), `Open` 0x83faf0, `Close` 0x83edc0, `gCHudPage2::OnShowWindow(bool)` 0x87f760 (protected, exported).

**The eCGuiWindow2 window tree** (Engine.dll):
- Walking: `GetFirstChild` 0x5650a0, `GetNextSibling` 0x565110, `GetParent`, `GetLastChild`, `GetDlgItem(uint)`, `GetName`, `GetID`.
- Properties: `GetText` / `SetText(bCUnicodeString const&)` 0x569db0, `SetPosition` / `SetSize(bCPoint const&)`, `GetPosition`, `GetSize`, `SetVisible(bool const&)`, `GetWindowAbsPosition`, `SetFont(bCString)`, `SetFontColor(int, bCByteAlphaColor)`, `SetTextStyle`, `SetDisabled`.
- Creation: `Clone(eCGuiWindow2* parent)` 0x5639d0 / `Clone()`, `SetParent`, `eCGuiStatic2::CreateObject` + `Create(bCString const&, bCPoint const&, IFFGuiWnd2* parent)`, `Initialize`, `PerformLayout`.
- XML: `eCGuiWindow2::LoadWindowA(char const*)` / `LoadWindowW` and `SaveWindow` / `SaveWindowAsClass(char const*)`. The windows can be (de)serialised to XML (`Serialize(bCXMLParserNode*)`). That is handy for dumping a live page and for loading our own layout. **Guess:** the path goes through the game's VFS.
- Script events: `eCGuiWindow2::CallScriptEvent(eCScriptProxyScript&, eCScriptEventArgs*, int*)`, `SendGameMessage`. `eCScriptStatic::SetOnPaint` / `SetOnInit` / `SetOnGameMsg` …, `eCScriptListCtrl::SetOn*`.
- Classes in `Create` / `CreateObject`: eCGuiStatic2, eCGuiButton2, eCGuiPushButton2, eCGuiCheckBox2, eCGuiProgressBar2, eCGuiTrackBar2, eCGuiListCtrl2 (`InsertItem(tagUI_LVITEMW&)`, `SetItemCount`), eCGuiFrame2, eCGuiPanel2.

**Drawing directly in the GUI style:** `eCGuiDevice2::DrawTextW(IFFGuiFont2*, wchar_t const*, int, RECT*, color, …)`, `DrawImage(IFFGuiBitmap2*, bCRect const&, …)`, `CalcTextRectW`, `LoadBitmapW`, `CreateFontW`. These could be called from an `OnPaint` script of our control.

**Scripts by name — our own GUI handlers:**
- `Game.dll gCScriptAdmin::LoadAllScriptGame` 0x9633d0 calls `eCVirtualFileSystem::FindFiles("*.dll")`. It looks in the mount `[Bin] scripts` = `bin\scripts`, keeps names that contain `script_`, and calls `LoadScriptDLL` 0x962bb0 → `g_LoadModule` → `GetProcAddress("ScriptInit")` → `ScriptInit()` returns a descriptor.
- The descriptor is 4 arrays `{T* data; int count; int cap;}` at +0x00 AIState, +0x10 AIFunction, +0x20 AICallback, **+0x30 Script**. An element is 0x18 bytes: `{bCString name; bCString ?; void* fn}`. `eCScriptFunctionMap::Register*(name)` → if `fn` is already set (+0x20), registration is **skipped**: the first one wins, so we can't override vanilla names this way, only add new ones.
- **So:** our own `bin\scripts\Script_RisenMods.dll` exporting `ScriptInit` can add GUI handlers, for example `RM_OnClick_Mods` and `RM_OnPaint_Stats`, with the same signature as `OnClick_Menu_Credits`. They can be referenced from a layout or from a cloned control. `args` is an event object; vanilla does `__RTDynamicCast(args, …)` → window → `GetID`.
- A call from C++: `eCScriptProxyScript(bCString name)` + `operator()(eCEntity* self, eCEntity* other, __int64 args)`. That is the same thing `ScriptCall` already does in mods/autoloot/game.cpp.

**How layouts load:** `gCHudSystem2::LoadWindows` 0x83d3a0 → `eCDocArchive` {raw = `#G3:/data/raw/gui2/windows/` + `eCGuiWindow2::GetClassRawExt` (.xwnd), compiled = `#G3:/data/compiled/library/compiled_windows.bin`} → `Load()`:
`IsBinaryValid` (if the raw path doesn't exist in the VFS → "Forcing binary version usage", the normal case) else `IsUpToDate` (headers / timestamps) → `ImportRaw` + `WriteBinary`.
`mountlist_packed.ini` already mounts `data/raw/gui2` as [Packed] (that pak doesn't exist). **Guess:** a `data/raw/gui2.pak` with `windows/*.xwnd` would make the game rebuild the layouts from raw files. We would need all 84 files and the xwnd format (probably one GENOMFLE document). Check whether ExportRaw / WriteBinary can write into the packed VFS. In FileSystem2, when two paks hold the same name, the newer one wins ("Replacing outdated packed file"). That is a second **guess** route: our own pak with a newer `compiled_windows.bin` via an extra line in the mountlist (a game file change, needs a backup).

## 6. Routes for adding our own content (ranked)

1. **Runtime, in our code (recommended first).** On the page's open event (hook `HandlePageChange` / `Open`, or poll `IsVisible(3)` once per frame from our d3d9 Present hook), get `FindPage(3)` → walk the tree → `Clone(parent)` a suitable label (for example the "Lockpick" row) → `SetPosition` / `SetText(L"Кражи: 12")`. Update the text on every open. Create the clones once per page instance (the page lives from `LoadWindows` until `ReloadLocalePage` / `ReSize` (**guess**), so check that our clone is still there, by name via `SetName`).
   Pros: no game files touched, works on this version only (the exports are checked by name). Risks: pointer subobjects (gCHudPage2 → eCGuiWindow2: see check 1 below), the bCUnicodeString layout, lifetime of the clones on resize or language change.
2. **Our own Script_*.dll** (`ScriptInit`) for click / paint handlers: the "Mods" button in options gets `OnClick = "RM_OnClick_Mods"` (set it through the button's script proxy, or clone and repoint) → opens our F10 panel or a native page. It can also feed `OnPaint` for our own statics. The engine supports it out of the box; the installer has to put the DLL into `bin\scripts`.
3. **Data mod of the layout** (xwnd / compiled_windows.bin through raw/gui2.pak or a newer pak): a "real" row in the layout. It needs writing GENOMFLE, is fragile, and conflicts with other mods. Only if 1–2 don't work.
4. **Fallback:** our ImGui panel (menu.dll overlay) drawn next to the game screen while `GUI2::IsVisible(3)`. Simple and robust, but it looks foreign. The GUI coordinates are a 1024x768 virtual space; map them onto the window resolution.

## 7. First experiments (next session, in game)

1. A test key in a debug ini: `gCHudModule2::GetSystem()->FindPage(3)` → log `GetName()` (should be "PAGE_GAME_CHARACTER") and the tree (name, ID, position, size, RTTI class) through `GetFirstChild` / `GetNextSibling`. That checks that `gCHudPage2*` can be passed to `eCGuiWindow2` methods without an offset (vbtable / virtual bases!).
2. `Clone` a label onto the character page + `SetText` → does our line show up, does it survive close/open, saving/loading, a resolution change, a language change.
3. `SaveWindowAsClass` of the live page → we see the XML format of a layout (for route 3 and for understanding the classes).
4. A minimal `bin\scripts\Script_RisenMods.dll` with `ScriptInit` and one `RM_Test` function → `ScriptCall("RM_Test")` from our mod. Does the engine load it, and is the "script_" check case-insensitive (MakeLower → yes).
5. Clone the Credits button in DLG_MENU_OPTIONS, put it into the tab row, give it the text "Моды", and make the click open our panel.

## 8. In game: live tree (mods/ui-lab, 2026-10-13)

Full dump: `notes/ui-tree-rev2045.txt` (1355 windows, RU, 16:9). Confirmed and new:
- `gCHudSystem2::FindPage` returns a `gCHudPage2*` that **is** an `eCGuiWindow2*` (no offset). Tree walk, `GetID`,
  `GetPosition`/`GetSize`/`GetText` (hidden return pointer) and the name (bCString at +0x48) all work. The window
  wraps an FFC.dll `CFXWnd` at `[+0xA0]+0x10`. RTTI class names read from `vtable[-1]`.
- All loaded pages (59 gCHudPage2 incl. `_C` controller twins, dialogs, popups) are **siblings under one root**, all
  loaded from the start; FindPage(n) just points into that list.
- The virtual space is 1365x768 on a 16:9 screen: the 1024-wide layout is centred (+170 px vs the xwnd numbers).
- Main menu buttons are `eCScriptRadioButton` 153x41 at y=653 (x 289/447/605/763/921), ID 0xFFFF, text = localised
  caption. Exported: `Get/SetOnClick(eCScriptProxyScript const&)`, OnPaint/OnInit/OnGameMsg, `CreateObject`.
- DLG_MENU_OPTIONS: tab row = 5 `eCScriptRadioButton` 164x23 at y=710, x 258/430/602/774/946 (step 172) on a
  background `eCGuiStatic2` 896x32 at (234,706). 6 tabs fit as ~143 px + 6 px gaps. The sub-pages
  (DLG_MENU_OPTIONS_VIDEO/AUDIO/INPUT/SETTINGS/SYSTEM) are separate pages drawn over it.
- DLG_MENU_OPTIONS_SETTINGS = the template for our page: title `eCGuiStatic2` 310x24 at (522,139); `eCGuiPanel2`
  800x500 at (282,140); section headers `eCGuiStatic2` 250x25 at x=315; a row = label `eCGuiStatic2` 307x24 at
  x=353 + **three `gCSpinButton`**: left arrow 16x22 at x=662, value 253x22 at x=678 with the row's ID (1001..1009,
  placeholder text "On/Off" / "Quality" replaced at runtime), right arrow 16x22 at x=931. Row step 29-30 px from
  y=218. "Apply" = `eCScriptRadioButton` id 1010 285x28.
- **gCSpinButton is exported after all** (Game.dll): `Get/SetSpinType` (gESpinButtonType: arrow / value?),
  `Get/SetSpinBuddy` (u16 - the arrows point at the value control's ID), `Get/AccessOnSpin` (script name).
- Audio page: trackbars are `gCAudioVolumeScrollBar2` 284x22 + value label `eCGuiStatic2` 50x24 id 1001..1005.
- Own script DLL works (TODO M2): see `scripts/risenmods/main.cpp` for the ScriptInit descriptor. `eCScriptRadioButton`
  OnClick proxy is at +0x150 (`GetOnClick`), `eCScriptProxy::GetScriptName` gives its name. `Clone(parent)` makes a
  working, visible copy; `SetText` / `SetPosition` / `SetSize` / `SetOnClick` on it take effect at once (M3).
  `gui2` is a data export of Script.dll (GetProcAddress gives the object itself).
- `SaveWindowAsClass("ui_page_NN.xml")` returned 0 (relative path not accepted; try an absolute or `#G3:/` path).
