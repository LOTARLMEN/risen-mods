// Skips the "Press START" screen shown before the main menu.
// The screen's paint script OnX360Paint_Start (Script_Game.dll) polls the gamepad and every key with
// eCInputAdmin::GetRawKeyState; on a key it closes the screen, switches the input to keyboard and opens the main
// menu. We replace that import of Script_Game and answer "pressed" once, for that one call site only, so the game
// goes the same way as after a real key press.

#include <windows.h>
#include <intrin.h>
#include <cstring>

#include "common.h"
#include "iat_patch.h"
#include "press_start.h"

namespace {

constexpr const char* kGetRawKeyState = "?GetRawKeyState@eCInputAdmin@@QEAA_NW4eEScanCode@@@Z";
constexpr DWORD kCallSite = 0x158335;  // call [GetRawKeyState] in OnX360Paint_Start (Rev. 2045)
constexpr DWORD kReturnRva = 0x15833b; // return address of that call
constexpr DWORD kIatRva = 0x202450;    // Script_Game's import slot for GetRawKeyState

using GetRawKeyStateFn = bool (*)(void* self, int scanCode);
GetRawKeyStateFn g_orig;
ULONG_PTR g_returnAddress;
volatile LONG g_done;

bool HookGetRawKeyState(void* self, int scanCode)
{
    if (!g_done && reinterpret_cast<ULONG_PTR>(_ReturnAddress()) == g_returnAddress &&
        InterlockedExchange(&g_done, 1) == 0) {
        Log("press start: screen skipped");
        return true;
    }
    return g_orig(self, scanCode);
}

} // namespace

bool PressStartSkipInstall()
{
    HMODULE script = nullptr;
    for (int i = 0; i < 200 && !script; ++i) { // Script_Game.dll is loaded with the scripts, around the game window
        script = GetModuleHandleA("Script_Game.dll");
        if (!script)
            Sleep(50);
    }
    if (!script) {
        Log("press start: Script_Game.dll not loaded, off");
        return false;
    }
    auto base = reinterpret_cast<const unsigned char*>(script);
    const unsigned char* call = base + kCallSite;
    LONG rel;
    memcpy(&rel, call + 2, 4);
    if (call[0] != 0xFF || call[1] != 0x15 || kReturnRva + rel != kIatRva || call[6] != 0x84 || call[7] != 0xC0) {
        Log("press start: unexpected code in OnX360Paint_Start (other game version?), off");
        return false;
    }
    g_returnAddress = reinterpret_cast<ULONG_PTR>(base + kReturnRva);
    g_orig = static_cast<GetRawKeyStateFn>(
        PatchImport(script, "Engine.dll", kGetRawKeyState, reinterpret_cast<void*>(HookGetRawKeyState)));
    return g_orig != nullptr;
}
