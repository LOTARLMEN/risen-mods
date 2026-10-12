#include "crash_log.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "common.h"

// A vectored handler sees an exception before anyone handles it, so it is the one place that sees every crash
// (the game's own crash reporter, sentry, replaces the unhandled-exception filter later). The price: it also sees
// exceptions that get handled (our overlay's __try, for one), so a report says "if the game closed right after
// this". Only fatal kinds are reported, and only the first few of a launch.

namespace {

constexpr int kMaxReports = 6;
constexpr int kMaxFrames = 24;
volatile LONG g_reports;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter;

const char* CodeName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case 0xC0000374: return "heap corruption";
    case 0xC0000409: return "stack buffer overrun";
    default: return nullptr; // not fatal by itself (C++ exceptions, debug output, ...)
    }
}

// "Engine.dll+0x5e6400" or the raw address when it isn't inside a module.
void Where(DWORD64 address, char* buf, size_t size)
{
    HMODULE mod = nullptr;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &mod) &&
        GetModuleFileNameA(mod, path, MAX_PATH)) {
        const char* name = strrchr(path, '\\');
        snprintf(buf, size, "%s+0x%llx", name ? name + 1 : path,
                 static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(mod)));
    } else {
        snprintf(buf, size, "0x%016llx", static_cast<unsigned long long>(address));
    }
}

// Walks the stack from the faulting context with the x64 unwind data (no dbghelp, nothing allocated).
void LogStack(const CONTEXT& start)
{
    CONTEXT c = start;
    char where[MAX_PATH + 32];
    for (int i = 0; i < kMaxFrames && c.Rip; ++i) {
        Where(c.Rip, where, sizeof(where));
        LogAlways("  #%-2d %s", i, where);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &base, nullptr);
        __try {
            if (fn) {
                void* handlerData = nullptr;
                DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, fn, &c, &handlerData, &frame, nullptr);
            } else { // leaf function: the return address is on top of the stack
                c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);
                c.Rsp += 8;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            LogAlways("  (stack unreadable)");
            return;
        }
    }
}

void Report(EXCEPTION_POINTERS* info, const char* kind)
{
    const EXCEPTION_RECORD& r = *info->ExceptionRecord;
    const char* name = CodeName(r.ExceptionCode);
    char where[MAX_PATH + 32];
    Where(reinterpret_cast<DWORD64>(r.ExceptionAddress), where, sizeof(where));
    LogAlways("!!! %s: exception 0x%08lX (%s) at %s, thread %lu", kind, r.ExceptionCode, name ? name : "?", where,
              GetCurrentThreadId());
    if ((r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || r.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        r.NumberParameters >= 2) {
        const char* op = r.ExceptionInformation[0] == 0 ? "reading" : r.ExceptionInformation[0] == 1 ? "writing"
                                                                                                     : "executing";
        LogAlways("    %s address 0x%llx", op, static_cast<unsigned long long>(r.ExceptionInformation[1]));
    }
    const CONTEXT& c = *info->ContextRecord;
    LogAlways("    rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx", c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi);
    LogAlways("    r8=%llx r9=%llx rsp=%llx rbp=%llx", c.R8, c.R9, c.Rsp, c.Rbp);
    LogStack(c);
}

LONG CALLBACK FirstChance(EXCEPTION_POINTERS* info)
{
    if (!CodeName(info->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_reports) > kMaxReports)
        return EXCEPTION_CONTINUE_SEARCH;
    __try {
        Report(info, "if the game closed right after this, this is the crash");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Reached only while nobody replaced it: then it confirms that the exception above really ended the game.
LONG WINAPI Unhandled(EXCEPTION_POINTERS* info)
{
    LogAlways("!!! CRASH: exception 0x%08lX was not handled, the game closes", info->ExceptionRecord->ExceptionCode);
    return g_previousFilter ? g_previousFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void CrashLogInstall()
{
    if (AddVectoredExceptionHandler(1, FirstChance))
        Log("crash log: on");
    else
        Log("crash log: could not install the handler (error %lu)", GetLastError());
    g_previousFilter = SetUnhandledExceptionFilter(Unhandled);
}
