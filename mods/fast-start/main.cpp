// Fast Start: game window in ~10 s instead of ~30 s. Buffered .pak reads (fast_io.cpp) + DxDiag stub (dxdiag_stub.cpp).
// No settings needed; optional mods\fast_start.ini:
//   [Performance]
//   FastPakReads=0   ; turn off buffered .pak reads
//   SkipDxDiag=0     ; turn off the DxDiag stub

#include <windows.h>

#include "common.h"
#include "dxdiag_stub.h"
#include "fast_io.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "fast_start");
        IniPath();
        // The loader runs us before the game starts loading its data, so the hooks are in place in time.
        bool fastIo = IniInt("Performance", "FastPakReads", 1) && FastIoInstall();
        bool dxDiag = IniInt("Performance", "SkipDxDiag", 1) && DxDiagStubInstall();
        Log("Fast Start 1.1: fast pak reads %s, dxdiag stub %s", fastIo ? "on" : "off", dxDiag ? "on" : "off");
    }
    return TRUE;
}
