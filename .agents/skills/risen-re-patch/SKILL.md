---
name: risen-re-patch
description: Reverse-engineer Risen 64-bit Rev.2045 binaries and add a safe hook, IAT/vtable patch or native API call to a mod. Use when a feature needs game internals - finding a function or field, calling exported game APIs, hooking a vtable slot or import.
---

# Reverse engineering and safe patches

Game: Steam Risen x64 Rev.2045. Modules in `..\bin\` (Engine.dll, Game.dll, SharedBase.dll, ...),
`..\bin\scripts\Script_Game.dll`. Prior art: old Risen Editor sources `D:\RisenModding\ed_src\RisenEditor-master`
(serialization has known bugs - verify against native code).

## Find things
- Exports with RVA/mangled/demangled names: `notes/exports/*.txt` (grep them first).
- Disassemble: `python -I tools/pe_disasm.py ../bin/<dll> <0xRVA | export substring> [lines]`
  (annotates imports and strings). Nearest export for Module+RVA: `tools/pe_symbol.py`.
- Game scripts (CanOpenLock, InvFilter_Loot, ...) are called through ScriptCall, see `mods/autoloot/game.cpp`.
- Retail builds have stubs: a getter may be `xor al,al; ret` (e.g. GetDebugMode). Disassemble before trusting
  a name. Record findings in the relevant `notes/*.md`.

## Calling and patching rules
- Prefer exported functions via `GetProcAddress`; resolve all, log `missing API: <name>` and disable the
  feature if any is missing.
- Reading a field or patching: verify the exact bytes first (instruction bytes of the setter that stores the
  field, the call site, gain constants, PE TimeDateStamp + SizeOfImage). On mismatch log why and skip - another
  game version must degrade, not crash.
- vtable/IAT hooks: check the slot holds the expected native target, publish the original pointer BEFORE
  making the replacement visible (atomic swap), keep the original signature exactly, forward all args/results.
  Filter by `_ReturnAddress()` when only one caller should be affected.
- Never keep game object pointers (entities, PS, effects) across frames or commands: store copied GUIDs /
  values and re-resolve every time. Pointers are valid only inside the current synchronous call.
- Code in exception handlers / DllMain: no waits on other threads, no heavy work; see loader/crash_log.cpp.
- Game-thread work: post a message to the subclassed window (WM_APP+x) from the worker thread, act in WndProc.

## Verify
Add a check script that compares exports/bytes against the installed DLLs (pattern: `tools/check-world-debug.py`)
and, where logic is testable, fake-API regression checks (`tools/world-debug-controls-check.cpp`).
Then deploy and smoke-test (skill `risen-deploy-test`).
