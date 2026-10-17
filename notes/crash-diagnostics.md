# Load/unload crash diagnostics

Loader 1.2 diagnostics adds `[Diagnostics] WorldTrace=1` and `DumpFirstChance=1`
in `bin/mods/loader.ini`. The trace is off by default; the current investigation
enables both. `tools/deploy-world-trace.ps1` installs only the loader/config while
the game is closed, backs them up with hashes, and preserves the exact test layer
in that backup. Other DLLs, INIs and loose layers are checked for unchanged hashes.

The Rev. 2045 Engine/Game PE stamps, image sizes and five original vtable targets
are verified before hooking. No executable instructions or physics behavior are
patched. Hooks forward all arguments/results and preserve exception propagation.
Session read/write, dynamic-layer release and context destruction are logged.
Collision exit records live in a fixed ring of 192 copied events; crash reporting
prints them beyond the normal log size cap. Entity GUID/name/position are copied
with bounded ReadProcessMemory calls, after checking the inherited GetID slot.
Only synchronous active calls hold native pointers; history addresses are printed
as numbers and never dereferenced in later frames. No native getters are called
by crash reporting.

Reports distinguish FIRST-CHANCE (may be handled) from UNHANDLED. The game's own
reporter can replace the latter filter, so first-chance evidence remains essential.
Ordinary first-chance reports have a 16-report/3-dump budget; the known collision
range Engine+652cc0..652f00 has an independent 6-report/3-dump budget. Unhandled
reports bypass those quotas. A thread-local guard rejects recursive reporting.

A prestarted dedicated worker loads DbgHelp and writes minidumps to
`bin/mods/crashes/<time>-p<PID>-t<TID>-<kind>.dmp`. Each has a `.dmp.log` snapshot
that survives subsequent launch-log rotation. Besides thread/module data, the
dump callback requests the active collision PS, owner metadata, up to 32 touching
shapes/associated PS objects, and bounded memory at register values. Only readable
committed regions are added; unreadable memory is skipped. These are selective
dumps, not complete heap captures. If the worker is busy or exceeds five seconds,
the text records the omission; a timed-out job is never reused. Early exceptions
before worker initialization can only produce text. The report history may be
skipped if a writer's lock is interrupted, rather than deadlocking the handler.
Every completed report also copies the log directly to a unique `.exception.log`,
including when dump creation is skipped. RtlDllShutdownInProgress detects final
DLL destruction after ExitProcess terminated worker threads; those exceptions
preserve text without waiting for an unavailable worker. A heap dump at that late
stage still requires the external Windows crash reporter.

`tools/check-world-trace.bat` executes actual wrappers against fake native memory,
checks identity/layout, forwarding, exception unwind, invalid-pointer rejection,
version/slot refusal, ring wrap, and nonblocking history access. It generates real
minidumps, independently parsed by `check-world-trace-dumps.py`, to verify the
exception register's heap marker and saved logs, including reserved physics and
unhandled quotas. These tests do not establish in-game stability or fix the original
08:04:38 crash. The original cause remains open.

API references: [MiniDumpWriteDump](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump),
[callback types](https://learn.microsoft.com/en-us/windows/win32/api/minidumpapiset/ne-minidumpapiset-minidump_callback_type).
