#include "focus_name.h"

#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "common.h"
#include "iat_patch.h"

// The focus module (Game.dll, Rev. 2045 at +0x83A2C0) builds the name shown over the focused object as
// eCLocString("FO_" + name) converted to bCUnicodeString. That conversion is an Engine.dll import, so Game.dll's
// import of it is replaced; the hook calls the original and appends the suffix only when called from that one site
// (checked by the return address), every other localised text in Game.dll stays untouched.
namespace {

using ToUnicodeFn = void* (*)(const void* locString, void* retUnicodeString);
using AppendFn = void* (*)(void* self, const wchar_t* text);

const char kImport[] = "??BeCLocString@@QEBA?AVbCUnicodeString@@XZ";
const uintptr_t kCallRva = 0x83A3DD; // call qword ptr [IAT] (FF 15 rel32), the focus name conversion

ToUnicodeFn g_original;
AppendFn g_append;
FocusSuffixFn g_suffix;
void* g_site; // return address of the focus name call

void* HookedToUnicode(const void* self, void* ret)
{
    g_original(self, ret);
    if (_ReturnAddress() == g_site)
        if (const wchar_t* suffix = g_suffix())
            g_append(ret, suffix);
    return ret;
}

} // namespace

bool InstallFocusNameHook(FocusSuffixFn suffix)
{
    HMODULE game = GetModuleHandleA("Game.dll"), engine = GetModuleHandleA("Engine.dll");
    HMODULE base = GetModuleHandleA("SharedBase.dll");
    g_append = reinterpret_cast<AppendFn>(
        base ? GetProcAddress(base, "??YbCUnicodeString@@QEAAAEAV0@PEB_W@Z") : nullptr);
    void* convert = engine ? reinterpret_cast<void*>(GetProcAddress(engine, kImport)) : nullptr;
    if (!game || !g_append || !convert) {
        Log("focus name: functions not found, no empty marks");
        return false;
    }
    // The call site must be an indirect call through Game.dll's import slot of the conversion.
    auto* call = reinterpret_cast<unsigned char*>(game) + kCallRva;
    int32_t rel;
    memcpy(&rel, call + 2, 4);
    void** slot = reinterpret_cast<void**>(call + 6 + rel);
    if (call[0] != 0xFF || call[1] != 0x15 || *slot != convert) {
        Log("focus name: unknown game version (focus name call differs), no empty marks");
        return false;
    }
    g_site = call + 6;
    g_suffix = suffix;
    g_original = reinterpret_cast<ToUnicodeFn>(convert);
    if (!PatchImport(game, "Engine.dll", kImport, reinterpret_cast<void*>(&HookedToUnicode))) {
        Log("focus name: import not found, no empty marks");
        return false;
    }
    Log("focus name: hooked");
    return true;
}
