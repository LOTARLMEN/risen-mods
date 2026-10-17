#include "crash_log.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <dbghelp.h>

#include "common.h"
#include "world_trace.h"

// First-chance records can describe handled exceptions. A separate, reserved budget
// keeps the known physics fault visible even after other handled exceptions.

namespace {

constexpr int kMaxReports = 16;
constexpr int kMaxFrames = 48;
volatile LONG g_reports;
volatile LONG g_physicsReports, g_dumpReports, g_physicsDumps, g_dumpBusy, g_dumpReady;
volatile LONG g_snapshotSerial;
using ShutdownQueryFn = BOOLEAN(NTAPI*)();
ShutdownQueryFn g_shutdownQuery;
thread_local bool t_reporting;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter;
HANDLE g_request, g_done;
char g_directory[MAX_PATH];
using WriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    const MINIDUMP_EXCEPTION_INFORMATION*, const MINIDUMP_USER_STREAM_INFORMATION*,
    const MINIDUMP_CALLBACK_INFORMATION*);
struct DumpJob {
    CONTEXT context;
    EXCEPTION_RECORD exception;
    DWORD thread;
    DiagnosticRange ranges[160];
    unsigned count, next;
    char path[MAX_PATH];
};
DumpJob g_job;

BOOL CALLBACK DumpCallback(void* parameter, PMINIDUMP_CALLBACK_INPUT input,
    MINIDUMP_CALLBACK_OUTPUT* output)
{
    auto job = static_cast<DumpJob*>(parameter);
    if (input->CallbackType == MemoryCallback) {
        while (job->next < job->count) {
            const DiagnosticRange range = job->ranges[job->next++];
            MEMORY_BASIC_INFORMATION memory = {};
            if (!VirtualQuery(reinterpret_cast<void*>(range.address), &memory, sizeof(memory)) ||
                memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) continue;
            ULONG64 end = reinterpret_cast<ULONG64>(memory.BaseAddress) + memory.RegionSize;
            output->MemoryBase = range.address;
            output->MemorySize = static_cast<ULONG>(end - range.address < range.size ? end - range.address : range.size);
            return TRUE;
        }
        return FALSE;
    }
    if (input->CallbackType == ReadMemoryFailureCallback) { output->Status = S_OK; return TRUE; }
    return TRUE;
}

DWORD WINAPI DumpWorker(void*)
{
    // Load DbgHelp outside DllMain and before any exception. Only this worker calls it.
    HMODULE library = LoadLibraryW(L"dbghelp.dll");
    auto writeDump = library ? reinterpret_cast<WriteDumpFn>(GetProcAddress(library, "MiniDumpWriteDump")) : nullptr;
    if (!writeDump) { LogAlways("crash dump: DbgHelp unavailable (%lu)", GetLastError()); return 0; }
    InterlockedExchange(&g_dumpReady, 1);
    Log("crash dump: worker ready, directory=%s", g_directory);
    for (;;) {
        if (WaitForSingleObject(g_request, INFINITE) != WAIT_OBJECT_0) return 0;
        HANDLE file = CreateFileA(g_job.path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            EXCEPTION_POINTERS pointers = {&g_job.exception, &g_job.context};
            MINIDUMP_EXCEPTION_INFORMATION exception = {g_job.thread, &pointers, FALSE};
            MINIDUMP_CALLBACK_INFORMATION callback = {DumpCallback, &g_job};
            auto flags = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpWithUnloadedModules | MiniDumpWithThreadInfo);
            BOOL ok = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, flags, &exception, nullptr, &callback);
            DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            CloseHandle(file);
            LogAlways("crash dump: %s path=%s error=%lu explicit-ranges=%u", ok ? "saved" : "FAILED", g_job.path, error, g_job.count);
        } else LogAlways("crash dump: create FAILED (%lu) path=%s", GetLastError(), g_job.path);
        char log[MAX_PATH]; snprintf(log, sizeof(log), "%s.log", g_job.path);
        if (!CopyFileA(LogPath(), log, TRUE)) LogAlways("crash dump: log snapshot FAILED (%lu)", GetLastError());
        // The handler owns busy until it observes completion. On timeout it leaves busy
        // set forever, avoiding reuse of an in-flight context or stale completion event.
        SetEvent(g_done);
    }
}

bool PhysicsFault(const EXCEPTION_POINTERS* info)
{
    HMODULE engine = GetModuleHandleW(L"Engine.dll");
    ULONG64 address = reinterpret_cast<ULONG64>(info->ExceptionRecord->ExceptionAddress);
    ULONG64 base = reinterpret_cast<ULONG64>(engine);
    return engine && address >= base + 0x652cc0 && address < base + 0x652f00;
}

void Dump(EXCEPTION_POINTERS* info, bool physics, bool unhandled, DiagnosticRange* ranges, unsigned count)
{
    // ExitProcess terminates other threads before DLL_PROCESS_DETACH. Waiting for
    // the dump worker in an exception from those destructors can never complete.
    if (g_shutdownQuery && g_shutdownQuery()) {
        LogAlways("crash dump: process shutdown in progress; worker unavailable, preserving text report"); return;
    }
    if (!InterlockedCompareExchange(&g_dumpReady, 0, 0)) { LogAlways("crash dump: worker not ready"); return; }
    if (!unhandled && !IniInt("Diagnostics", "DumpFirstChance", 0)) return;
    if (!unhandled && InterlockedIncrement(physics ? &g_physicsDumps : &g_dumpReports) > 3) {
        LogAlways("crash dump: first-chance dump quota reached (text report retained)"); return;
    }
    if (InterlockedCompareExchange(&g_dumpBusy, 1, 0)) { LogAlways("crash dump: worker busy; skipped"); return; }
    ResetEvent(g_done);
    g_job.context = *info->ContextRecord; g_job.exception = *info->ExceptionRecord;
    g_job.exception.ExceptionRecord = nullptr;
    g_job.thread = GetCurrentThreadId(); g_job.count = count; g_job.next = 0;
    memcpy(g_job.ranges, ranges, count * sizeof(DiagnosticRange));
    SYSTEMTIME now; GetLocalTime(&now);
    snprintf(g_job.path, sizeof(g_job.path), "%s\\%04u%02u%02u-%02u%02u%02u-%03u-p%lu-t%lu-%s.dmp",
        g_directory, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentProcessId(), g_job.thread, unhandled ? "unhandled" : "first-chance");
    LogAlways("crash dump: requested %s", g_job.path);
    SetEvent(g_request);
    if (WaitForSingleObject(g_done, 5000) == WAIT_OBJECT_0) InterlockedExchange(&g_dumpBusy, 0);
    else LogAlways("crash dump: worker timeout; further dumps disabled this launch");
}

void SnapshotLog()
{
    if (!g_directory[0]) return;
    char path[MAX_PATH]; SYSTEMTIME now; GetLocalTime(&now);
    snprintf(path, sizeof(path), "%s\\%04u%02u%02u-%02u%02u%02u-p%lu-t%lu-e%ld.exception.log",
        g_directory, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        GetCurrentProcessId(), GetCurrentThreadId(), InterlockedIncrement(&g_snapshotSerial));
    LogAlways("crash report: preserving %s", path);
    if (!CopyFileA(LogPath(), path, TRUE)) LogAlways("crash report: snapshot FAILED (%lu)", GetLastError());
}

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
    LogAlways("    r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx eflags=%lx",
        c.R10, c.R11, c.R12, c.R13, c.R14, c.R15, c.EFlags);
    LogStack(c);
    DiagnosticRange ranges[160];
    unsigned count = WorldTraceReport(c, ranges, 160);
    Dump(info, PhysicsFault(info), strcmp(kind, "UNHANDLED") == 0, ranges, count);
    SnapshotLog();
}

LONG CALLBACK FirstChance(EXCEPTION_POINTERS* info)
{
    if (t_reporting || !CodeName(info->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;
    bool physics = PhysicsFault(info);
    if (InterlockedIncrement(physics ? &g_physicsReports : &g_reports) > (physics ? 6 : kMaxReports))
        return EXCEPTION_CONTINUE_SEARCH;
    t_reporting = true;
    __try {
        __try { Report(info, "FIRST-CHANCE (may be handled)"); }
        __except (EXCEPTION_EXECUTE_HANDLER) { }
    } __finally { t_reporting = false; }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Reached only while nobody replaced it: then it confirms that the exception above really ended the game.
LONG WINAPI Unhandled(EXCEPTION_POINTERS* info)
{
    if (!t_reporting) {
        t_reporting = true;
        __try {
            __try { Report(info, "UNHANDLED"); }
            __except (EXCEPTION_EXECUTE_HANDLER) { }
        } __finally { t_reporting = false; }
    }
    return g_previousFilter ? g_previousFilter(info) : EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void CrashLogStartWorker()
{
    strcpy_s(g_directory, LogPath());
    char* slash = strrchr(g_directory, '\\');
    if (!slash) return;
    strcpy_s(slash, sizeof(g_directory) - (slash - g_directory), "\\crashes");
    if (!CreateDirectoryA(g_directory, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        LogAlways("crash dump: cannot create directory (%lu)", GetLastError()); return;
    }
    g_request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_request || !g_done) { LogAlways("crash dump: cannot create worker events (%lu)", GetLastError()); return; }
    HANDLE worker = CreateThread(nullptr, 0, DumpWorker, nullptr, 0, nullptr);
    if (worker) CloseHandle(worker);
    else LogAlways("crash dump: cannot create worker (%lu)", GetLastError());
}

void CrashLogInstall()
{
    g_shutdownQuery = reinterpret_cast<ShutdownQueryFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlDllShutdownInProgress"));
    if (AddVectoredExceptionHandler(1, FirstChance))
        Log("crash log: on");
    else
        Log("crash log: could not install the handler (error %lu)", GetLastError());
    g_previousFilter = SetUnhandledExceptionFilter(Unhandled);
}
