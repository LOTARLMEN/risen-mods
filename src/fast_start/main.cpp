// Risen Fast Start: only the startup speed-ups of risen_mods (buffered .pak reads + DxDiag stub), nothing else.
// Loaded as version.dll from Risen\bin. No settings needed; optional bin\risen_fast_start.ini:
//   [Performance]
//   FastPakReads=0   ; turn off buffered .pak reads
//   SkipDxDiag=0     ; turn off the DxDiag stub
// Log: bin\risen_fast_start.log

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../dxdiag_stub.h"
#include "../fast_io.h"

static char g_logPath[MAX_PATH];
static CRITICAL_SECTION g_logLock;

static void PathNextToExe(char* out, const char* fileName)
{
    GetModuleFileNameA(nullptr, out, MAX_PATH);
    char* slash = strrchr(out, '\\');
    char* dst = slash ? slash + 1 : out;
    strcpy_s(dst, MAX_PATH - (dst - out), fileName);
}

void Log(const char* fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_logLock);
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") == 0 && f) {
        fprintf(f, "%02d:%02d:%02d.%03d  %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
        fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_logLock);
        PathNextToExe(g_logPath, "risen_fast_start.log");
        FILE* f = nullptr;
        if (fopen_s(&f, g_logPath, "w") == 0 && f) // fresh log every launch
            fclose(f);
        char iniPath[MAX_PATH];
        PathNextToExe(iniPath, "risen_fast_start.ini");

        // Must run before the game starts loading, i.e. here (we load as a dependency of SharedBase.dll).
        bool fastIo = GetPrivateProfileIntA("Performance", "FastPakReads", 1, iniPath) && FastIoInstall();
        bool dxDiag = GetPrivateProfileIntA("Performance", "SkipDxDiag", 1, iniPath) && DxDiagStubInstall();
        Log("Risen Fast Start 1.0 loaded: fast pak reads %s, dxdiag stub %s", fastIo ? "on" : "off",
            dxDiag ? "on" : "off");
    }
    return TRUE;
}
