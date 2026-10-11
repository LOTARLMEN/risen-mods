// Startup diagnostics, enabled by [Debug] StartupTrace=1 in risen_mods.ini. Writes bin\risen_startup.log:
//  - every DLL load with time since process start and the call stack that loaded it;
//  - samples of the main thread's stack every 50 ms until the game window shows up (or 120 s).
// The main thread is only suspended long enough to copy its registers and stack; everything that may take
// loader locks (module lookup) happens after it is resumed, so the sampler can't deadlock against it.

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "fast_io.h"
#include "iat_patch.h"
#include "startup_trace.h"

namespace {

char g_tracePath[MAX_PATH];
CRITICAL_SECTION g_bufLock;
char* g_buf;
size_t g_bufLen;
constexpr size_t kBufSize = 8 << 20;

LARGE_INTEGER g_qpcFreq, g_qpcAtLoad;
double g_secAtLoad; // seconds from process creation to our DllMain
HANDLE g_mainThread;
ULONG_PTR g_mainStackBase;
volatile LONG g_stop;

double Now()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return g_secAtLoad + double(t.QuadPart - g_qpcAtLoad.QuadPart) / double(g_qpcFreq.QuadPart);
}

void Append(const char* fmt, ...)
{
    char line[2048];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n <= 0)
        return;
    if (n >= int(sizeof(line)))
        n = int(sizeof(line)) - 1;
    EnterCriticalSection(&g_bufLock);
    if (g_bufLen + n + 1 < kBufSize) {
        memcpy(g_buf + g_bufLen, line, n);
        g_bufLen += n;
        g_buf[g_bufLen++] = '\n';
    }
    LeaveCriticalSection(&g_bufLock);
}

void Flush()
{
    EnterCriticalSection(&g_bufLock);
    FILE* f = nullptr;
    if (g_bufLen && fopen_s(&f, g_tracePath, "ab") == 0 && f) {
        fwrite(g_buf, 1, g_bufLen, f);
        fclose(f);
        g_bufLen = 0;
    }
    LeaveCriticalSection(&g_bufLock);
}

// "Engine.dll+0x1a2b3c", or "?0x7ff..." for addresses outside any module.
void FormatAddress(ULONG_PTR addr, char* out, size_t size)
{
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &mod) || !mod) {
        snprintf(out, size, "?0x%llx", static_cast<unsigned long long>(addr));
        return;
    }
    char path[MAX_PATH];
    GetModuleFileNameA(mod, path, MAX_PATH);
    const char* name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    snprintf(out, size, "%s+0x%llx", name, static_cast<unsigned long long>(addr - reinterpret_cast<ULONG_PTR>(mod)));
}

bool InModule(ULONG_PTR addr)
{
    HMODULE mod = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(addr), &mod) && mod;
}

// True if the bytes before addr end with a call instruction, i.e. addr looks like a return address.
bool AfterCall(ULONG_PTR addr)
{
    const auto* p = reinterpret_cast<const unsigned char*>(addr);
    __try {
        if (p[-5] == 0xE8)                                  // call rel32
            return true;
        if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10)        // call [rip+disp32] / [reg+disp32]
            return true;
        if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10)        // call reg / [reg]
            return true;
        if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10)        // call [reg+disp8] / with rex
            return true;
        if (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10)        // call [reg+disp32] with sib
            return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

// -------------------------------------------------------------------------------------------------
// DLL load notifications (ntdll LdrRegisterDllNotification)

struct UnicodeStr { USHORT Length, MaximumLength; PWSTR Buffer; };
struct DllNotificationData { ULONG Flags; const UnicodeStr* FullDllName; const UnicodeStr* BaseDllName; PVOID DllBase; ULONG SizeOfImage; };
using DllNotificationFn = VOID(CALLBACK*)(ULONG reason, const DllNotificationData* data, PVOID context);
using LdrRegisterDllNotificationFn = LONG(NTAPI*)(ULONG flags, DllNotificationFn fn, PVOID context, PVOID* cookie);

VOID CALLBACK OnDllNotification(ULONG reason, const DllNotificationData* data, PVOID)
{
    if (reason != 1) // 1 = loaded, 2 = unloaded
        return;
    char name[MAX_PATH];
    int len = WideCharToMultiByte(CP_UTF8, 0, data->BaseDllName->Buffer, data->BaseDllName->Length / 2,
                                  name, sizeof(name) - 1, nullptr, nullptr);
    name[len > 0 ? len : 0] = 0;

    void* frames[48];
    USHORT count = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    char stack[1600] = "";
    size_t used = 0;
    int shown = 0;
    for (USHORT i = 0; i < count && shown < 10; ++i) {
        char frame[160];
        FormatAddress(reinterpret_cast<ULONG_PTR>(frames[i]), frame, sizeof(frame));
        // Skip our own frames and the loader internals, keep the interesting callers.
        if (strncmp(frame, "version.dll", 11) == 0 || strncmp(frame, "ntdll.dll", 9) == 0)
            continue;
        int n = snprintf(stack + used, sizeof(stack) - used, " < %s", frame);
        if (n <= 0 || used + n >= sizeof(stack))
            break;
        used += n;
        ++shown;
    }
    Append("%8.3f  LOAD tid=%lu %s%s", Now(), GetCurrentThreadId(), name, stack);
}


// -------------------------------------------------------------------------------------------------
// File I/O statistics: FileSystem2.dll imports FFCore's g_FF* file functions, we patch its IAT.

struct FileStats {
    std::string path;
    unsigned long long reads = 0, seeks = 0, bytes = 0, smallReads = 0;
    double seconds = 0;
    unsigned createArgs[3] = {};
};

std::mutex g_ioLock;
std::unordered_map<void*, FileStats*> g_openFiles;
std::unordered_map<std::string, FileStats*> g_fileStats;
unsigned long long g_readSizeHist[8]; // <=64, <=512, <=4K, <=64K, <=1M, more
double g_ioSeconds;

using FFCreateFileFn = void* (*)(const char*, int, int, unsigned, unsigned, void*);
using FFReadFileFn = bool (*)(void*, void*, DWORD, DWORD*);
using FFSetFilePointerFn = bool (*)(void*, long long, long long*, int);
using FFCloseFileFn = bool (*)(void*);
FFCreateFileFn g_origCreate;
FFReadFileFn g_origRead;
FFSetFilePointerFn g_origSeek;
FFCloseFileFn g_origClose;

double Elapsed(const LARGE_INTEGER& start)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - start.QuadPart) / double(g_qpcFreq.QuadPart);
}

FileStats* StatsFor(void* h)
{
    auto it = g_openFiles.find(h);
    return it == g_openFiles.end() ? nullptr : it->second;
}

void* HookCreate(const char* path, int create, int access, unsigned a4, unsigned a5, void* a6)
{
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    void* h = g_origCreate(path, create, access, a4, a5, a6);
    double dt = Elapsed(t0);
    std::lock_guard<std::mutex> lock(g_ioLock);
    g_ioSeconds += dt;
    if (h && h != INVALID_HANDLE_VALUE && path) {
        FileStats*& st = g_fileStats[path];
        if (!st) {
            st = new FileStats;
            st->path = path;
            st->createArgs[0] = unsigned(access);
            st->createArgs[1] = a4;
            st->createArgs[2] = a5;
        }
        st->seconds += dt;
        g_openFiles[h] = st;
    }
    return h;
}

bool HookRead(void* h, void* buf, DWORD size, DWORD* read)
{
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    bool ok = g_origRead(h, buf, size, read);
    double dt = Elapsed(t0);
    std::lock_guard<std::mutex> lock(g_ioLock);
    g_ioSeconds += dt;
    int bucket = size <= 64 ? 0 : size <= 512 ? 1 : size <= 4096 ? 2 : size <= 65536 ? 3 : size <= (1u << 20) ? 4 : 5;
    ++g_readSizeHist[bucket];
    if (FileStats* st = StatsFor(h)) {
        ++st->reads;
        st->bytes += size;
        st->smallReads += size <= 4096;
        st->seconds += dt;
    }
    return ok;
}

bool HookSeek(void* h, long long dist, long long* newPos, int whence)
{
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    bool ok = g_origSeek(h, dist, newPos, whence);
    double dt = Elapsed(t0);
    std::lock_guard<std::mutex> lock(g_ioLock);
    g_ioSeconds += dt;
    if (FileStats* st = StatsFor(h)) {
        ++st->seeks;
        st->seconds += dt;
    }
    return ok;
}

bool HookClose(void* h)
{
    {
        std::lock_guard<std::mutex> lock(g_ioLock);
        g_openFiles.erase(h);
    }
    return g_origClose(h);
}

void InstallIoHooks()
{
    HMODULE fs = GetModuleHandleA("FileSystem2.dll");
    if (!fs) {
        Append("io: FileSystem2.dll not loaded yet, no I/O stats");
        return;
    }
    g_origCreate = static_cast<FFCreateFileFn>(PatchImport(fs, "FFCore.dll",
        "?g_FFCreateFile@@YAPEAXPEBDW4EFFFileCreate@@W4EFFFileAccess@@KKPEAX@Z", reinterpret_cast<void*>(HookCreate)));
    g_origRead = static_cast<FFReadFileFn>(PatchImport(fs, "FFCore.dll",
        "?g_FFReadFile@@YA_NPEAX0KPEAK@Z", reinterpret_cast<void*>(HookRead)));
    g_origSeek = static_cast<FFSetFilePointerFn>(PatchImport(fs, "FFCore.dll",
        "?g_FFSetFilePointer@@YA_NPEAX_JPEA_JW4EFFFileSeek@@@Z", reinterpret_cast<void*>(HookSeek)));
    g_origClose = static_cast<FFCloseFileFn>(PatchImport(fs, "FFCore.dll",
        "?g_FFCloseFile@@YA_NPEAX@Z", reinterpret_cast<void*>(HookClose)));
    Append("io: hooks create=%d read=%d seek=%d close=%d", !!g_origCreate, !!g_origRead, !!g_origSeek, !!g_origClose);
}

void DumpIoStats()
{
    std::lock_guard<std::mutex> lock(g_ioLock);
    Append("io: total %.3f s inside FFCore file calls; read sizes <=64:%llu <=512:%llu <=4K:%llu <=64K:%llu <=1M:%llu >1M:%llu",
           g_ioSeconds, g_readSizeHist[0], g_readSizeHist[1], g_readSizeHist[2], g_readSizeHist[3], g_readSizeHist[4],
           g_readSizeHist[5]);
    for (auto& kv : g_fileStats) {
        const FileStats* st = kv.second;
        Append("io: %7.3f s  reads=%llu (small %llu) seeks=%llu bytes=%llu args=%x/%x/%x  %s", st->seconds, st->reads,
               st->smallReads, st->seeks, st->bytes, st->createArgs[0], st->createArgs[1], st->createArgs[2],
               st->path.c_str());
    }
}

// -------------------------------------------------------------------------------------------------
// Main thread sampler

constexpr size_t kStackCopy = 64 * 1024;
unsigned char g_stackCopy[kStackCopy];

void SampleOnce(int index)
{
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    size_t copied = 0;
    if (SuspendThread(g_mainThread) == DWORD(-1))
        return;
    if (GetThreadContext(g_mainThread, &ctx) && ctx.Rsp && ctx.Rsp < g_mainStackBase) {
        copied = g_mainStackBase - ctx.Rsp;
        if (copied > kStackCopy)
            copied = kStackCopy;
        memcpy(g_stackCopy, reinterpret_cast<const void*>(ctx.Rsp), copied);
    }
    ResumeThread(g_mainThread);
    if (!copied)
        return;

    char line[1900];
    char frame[160];
    FormatAddress(ctx.Rip, frame, sizeof(frame));
    int used = snprintf(line, sizeof(line), "%8.3f  SAMPLE %d  %s", Now(), index, frame);
    int shown = 0;
    ULONG_PTR last = 0;
    for (size_t off = 0; off + 8 <= copied && shown < 14; off += 8) {
        ULONG_PTR value;
        memcpy(&value, g_stackCopy + off, 8);
        if (value < 0x10000 || value == last || !InModule(value) || !AfterCall(value))
            continue;
        last = value;
        FormatAddress(value, frame, sizeof(frame));
        int n = snprintf(line + used, sizeof(line) - used, " < %s", frame);
        if (n <= 0 || used + n >= int(sizeof(line)))
            break;
        used += n;
        ++shown;
    }
    Append("%s", line);
}

DWORD WINAPI Sampler(LPVOID)
{
    for (int i = 0; !g_stop && Now() < 120.0; ++i) {
        SampleOnce(i);
        if (i % 20 == 0)
            Flush();
        Sleep(50);
    }
    Append("%8.3f  sampler stopped", Now());
    DumpIoStats();
    long long reads, fills, direct;
    FastIoStats(&reads, &fills, &direct);
    Append("fast io: %lld pak reads served with %lld window fills + %lld direct reads", reads, fills, direct);
    Flush();
    return 0;
}

} // namespace

void StartupTraceInit(const char* iniPath, const char* tracePath)
{
    if (!GetPrivateProfileIntA("Debug", "StartupTrace", 0, iniPath))
        return;
    strcpy_s(g_tracePath, tracePath);
    InitializeCriticalSection(&g_bufLock);
    g_buf = static_cast<char*>(VirtualAlloc(nullptr, kBufSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_buf)
        return;

    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_qpcAtLoad);
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimePreciseAsFileTime(&now);
    ULARGE_INTEGER c{created.dwLowDateTime, created.dwHighDateTime}, n{now.dwLowDateTime, now.dwHighDateTime};
    g_secAtLoad = double(n.QuadPart - c.QuadPart) / 1e7;

    FILE* f = nullptr;
    if (fopen_s(&f, g_tracePath, "w") == 0 && f)
        fclose(f);
    Append("startup trace: times are seconds since process creation; risen_mods loaded at %.3f", g_secAtLoad);

    // DllMain runs on the thread that loads the static imports, which is the game's main thread.
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    g_mainStackBase = reinterpret_cast<ULONG_PTR>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);

    if (auto reg = reinterpret_cast<LdrRegisterDllNotificationFn>(
            GetProcAddress(GetModuleHandleA("ntdll.dll"), "LdrRegisterDllNotification"))) {
        PVOID cookie = nullptr;
        reg(0, OnDllNotification, nullptr, &cookie);
    }
    InstallIoHooks();
    if (g_mainThread)
        if (HANDLE t = CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr))
            CloseHandle(t);
}

void StartupTraceMark(const char* what)
{
    if (!g_buf)
        return;
    Append("%8.3f  MARK %s", Now(), what);
}

void StartupTraceStopSoon()
{
    if (!g_buf)
        return;
    // Keep sampling a few seconds after the window appears to cover the way to the main menu.
    if (HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(15000);
            InterlockedExchange(&g_stop, 1);
            return 0;
        }, nullptr, 0, nullptr))
        CloseHandle(t);
}
