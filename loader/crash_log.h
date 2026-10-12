#pragma once

// Crash reports in the shared log (RisenMods.log): exception code, where it happened as module+offset, and the call
// stack. Offsets map to functions with tools\pe_symbol.py (our DLLs: build them with the same sources).

// Installs the handlers. Call once from the loader, after LogStartSession.
void CrashLogInstall();
