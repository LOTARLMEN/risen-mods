---
name: risen-deploy-test
description: Deploy a rebuilt Risen mod DLL/loader into the game and verify it starts - closed-game check, backup, hash check, INI preservation, launch, log markers. Use whenever build output in risen-mods/build/bin must reach ../bin, or the user asks to install/test a build in the game.
---

# Deploy a build and smoke-test it in Risen

Paths: repo `risen-mods\`, game `..\` (`bin\Risen.exe`, `bin\mods\`). PowerShell. The Rust CLI is also
`risen.exe` - always identify the game by the full path `<game>\bin\Risen.exe`.

## 1. Build and check first
- `cmd /c "<repo>\build.bat"` (full path). Require exit 0, no `error`/`warning C`/`LNK` lines.
- Run the component's checks (see repo AGENTS.md), e.g. `tools\check-world-debug.py`,
  `tools\check-world-debug-controls.bat`, `tools\check-world-trace.bat`. Don't deploy on a failure.

## 2. Make sure the game is closed
```powershell
$exe = '<game>\bin\Risen.exe'
Get-Process | ? { $_.Path -eq $exe }
```
If it runs and the user has finished testing (authorized 2026-10-16), close it gracefully:
`(Get-Process | ? Path -eq $exe).CloseMainWindow()`, then wait until the process is gone. Never kill
it while the user is still playing; if unsure, ask.

## 3. Back up, copy only what changed, verify
- Backup folder: `backup\deploy-<yyyyMMdd-HHmmss>\` with the current `bin\` copies of every file you replace
  (DLLs and their INIs). `backup\bin-original` must exist; it is the pristine game, never overwrite it.
- Copy only the changed components: `build\bin\version.dll` -> `bin\version.dll`,
  `build\bin\mods\<mod>.dll` -> `bin\mods\`. Avoid `build.bat deploy` unless everything changed
  (it replaces the loader, the script DLL and all mods).
- Never overwrite an installed user INI (`bin\mods\<mod>.ini`): it holds the user's keys/speeds.
  Edit single keys only when needed and say so.
- Verify `Get-FileHash` of source and target match. Dedicated scripts exist for some mods
  (`tools\deploy-world-debug.ps1`, `tools\deploy-world-trace.ps1`); read their side effects first
  (first install enables World Debug; world-trace enables WorldTrace/DumpFirstChance).

## 4. Launch and read the log
- `Start-Process "steam://rungameid/40300"`. No focus stealing. Frames of the game itself may be captured and inspected (user
  2026-10-17; game window/frame only, never the desktop or other windows).
- Shared log `bin\mods\RisenMods.log` (previous launch: `RisenMods.prev.log`). Poll until the markers appear:
  loader version line, `[<mod>] Risen <Mod> <version>`, `API: resolved`, hook `installed` lines, and no
  `EXCEPTION`/`CRASH`. A version mismatch shows as `disabled`/`skipped` lines - report it.
- Before relaunching after a crash, preserve evidence first (skill `risen-crash-triage`).

## 5. Report
Tell the user exactly which build is in `bin\` (file + LastWriteTime + version), the backup path, what the
log confirmed, and what remains for hands-on testing. Startup success is not a hands-on pass. Record it in
DEVELOPMENT_LOG.md (skill `risen-devlog`).
