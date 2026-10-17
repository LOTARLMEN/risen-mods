---
name: risen-crash-triage
description: Investigate a Risen crash, hang or exception report - preserve evidence before relaunching, map Module+offset to functions, tell first-chance noise from real crashes, read minidumps and world-trace history. Use when the user says the game crashed/froze, or RisenMods.log shows EXCEPTION/CRASH lines.
---

# Crash triage for Risen mods

Background: `notes/crash-diagnostics.md` (loader 1.2 diagnostics, world trace, dump worker).

## 1. Preserve evidence BEFORE any relaunch
The log rotates on every launch (`RisenMods.log` -> `RisenMods.prev.log`, older is lost).
Copy into `build\tests\crash-<yyyyMMdd-HHmmss>\`:
- `bin\mods\RisenMods.log`, `RisenMods.prev.log`, everything new in `bin\mods\crashes\`
  (`*.dmp`, `*.dmp.log`, `*.exception.log`; only the 40 newest are kept),
- the save the user loaded, if relevant (ask which one; locate with `es -sort dm -n 10 .save`),
- Windows evidence: `Get-WinEvent -FilterHashtable @{LogName='Application'; Id=1000,1001} -MaxEvents 5`
  and `%LOCALAPPDATA%\CrashDumps\Risen.exe.*.dmp` if present.
Write down which installed builds were in `bin\` (timestamps) and what scene/loose data was installed.

## 2. Is it a real crash?
- `FIRST-CHANCE (may be handled)` alone proves nothing: the game and our overlay handle exceptions.
- Real: `UNHANDLED` report, Windows event 1000 for Risen.exe, the user saw the game close/freeze.
- An exception during a voluntary exit (after the user quit) is a shutdown fault - separate from a gameplay crash.
- Ask the user what they did right before (load, save, freecam, open menu) instead of guessing.

## 3. Locate the code
- Stack lines are `Module+0xOFFSET`. Nearest export: `python -I tools/pe_symbol.py ../bin ../bin/scripts -- Engine.dll+0x652e0c`.
- Disassemble: `python -I tools/pe_disasm.py ../bin/<dll> <0xRVA | export substring> [lines]`
  (Script_Game.dll lives in `bin\scripts\`). Exports: `notes/exports/*.txt`.
- Our DLLs in the stack: check that build's source at that timestamp (git log), and rules: no game pointers
  across frames, patches verify bytes first.
- World-trace history (WorldTrace=1) lists recent layer release / session read-write / collision exit events
  with GUID/name/position of owners - correlate with installed test layers.

## 4. Conclusions
- Separate "cause found" from "not reproduced". A successful relaunch or quieter log is not a fix.
- If unknown: record in TODO.md as an open item with evidence path, exact reproduction steps needed,
  and what diagnostics are installed. Don't erase or downplay earlier crash records.
- Diagnostics toggles: `bin\mods\loader.ini [Diagnostics] WorldTrace / DumpFirstChance`. DumpFirstChance=1
  stalls the faulting thread while a dump is written - enable only for a targeted repro, then turn it off.
