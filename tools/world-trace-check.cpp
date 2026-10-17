// Exercise the actual loader implementation without loading or running the game.
#include "../loader/world_trace.cpp"
#include "../loader/crash_log.cpp"
#include <cstdlib>

namespace {
unsigned checks;
void Check(bool value, const char* message)
{
    if (!value) { fprintf(stderr, "FAIL: %s\n", message); ExitProcess(1); }
    ++checks;
}
void Put(void* base, unsigned offset, ULONG64 value)
{ memcpy(static_cast<char*>(base) + offset, &value, sizeof(value)); }
void* expectedSelf;
void* expectedStream;
void NativeCollision(void* self) { Check(self == expectedSelf, "collision argument forwarded"); }
void NativeDestroy(void* self) { Check(self == expectedSelf, "destroy argument forwarded"); }
bool NativeRelease(void* self) { Check(self == expectedSelf, "release argument forwarded"); return true; }
int NativeRead(void* self, void* stream) {
    Check(self == expectedSelf && stream == expectedStream, "read arguments forwarded"); return -7;
}
int NativeWrite(void* self, void* stream) {
    Check(self == expectedSelf && stream == expectedStream, "write arguments forwarded"); return 23;
}
void NativeFault(void*) { RaiseException(0xe0123456, 0, 0, nullptr); }
BOOLEAN NTAPI FakeShutdown() { return TRUE; }
void TestUnwind(void* ps)
{
    g_collision = NativeFault;
    ULONG64 before = g_sequence;
    bool propagated = false;
    __try { Collision(ps); }
    __except (GetExceptionCode() == 0xe0123456 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { propagated = true; }
    Check(propagated, "native exception propagated");
    Check(t_collision == 0 && g_sequence == before + 2, "TLS restored during exception unwind");
    Check(strcmp(g_history[(g_sequence - 1) % kHistory].phase, "collision-exit unwind") == 0, "unwind phase recorded");
}
void TestHandledException()
{
    bool handled = false;
    __try { RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { handled = true; }
    Check(handled && !t_reporting, "first-chance handler preserves game exception handling");
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    CreateDirectoryA(argv[1], nullptr);
    LogStartSession(argv[1]);
    WritePrivateProfileStringA("Diagnostics", "DumpFirstChance", "1", IniPath());
    Check(!Identify(0xffffffffffffffff).valid, "invalid object pointer is safely rejected");
    char text[8]; Text(0xffffffffffffffff, text, sizeof(text));
    Check(!text[0], "invalid string is safely rejected");

    // Fake native objects: independent expected GUID, name and position values.
    BYTE entity[0x200] = {}, vtable[0x160] = {}, ps[0x100] = {}, metadata[0x20] = {}, idMetadata[0x20] = {};
    GUID id = {0x12345678,0x9abc,0xdef0,{1,2,3,4,5,6,7,8}};
    const char* name = "DIAGNOSTIC_HEAP_MARKER_64F45D6C";
    const char** stringObject = &name;
    g_engine = 0x10000000;
    Put(entity, 0, reinterpret_cast<ULONG64>(vtable));
    Put(vtable, 0x148, g_engine + 0x69f090);
    Put(entity, 0x1c8, reinterpret_cast<ULONG64>(idMetadata));
    Put(idMetadata, 0x10, reinterpret_cast<ULONG64>(&id));
    Put(entity, 0x1c0, reinterpret_cast<ULONG64>(metadata));
    Put(metadata, 0, reinterpret_cast<ULONG64>(stringObject));
    const float xyz[] = {-37950,182.19078f,-5800}; memcpy(entity + 0x90, xyz, sizeof(xyz));
    Put(ps, 0x10, reinterpret_cast<ULONG64>(entity));
    Entity e = Identify(reinterpret_cast<ULONG64>(entity));
    Check(e.valid && strcmp(e.guid, "12345678-9ABC-DEF0-0102-030405060708") == 0, "native GUID layout");
    Check(strcmp(e.name, name) == 0 && e.xyz[0] == xyz[0] && e.xyz[1] == xyz[1], "native name and position layout");
    expectedSelf = ps; expectedStream = entity;
    g_collision = NativeCollision; Collision(ps);
    Check(t_collision == 0, "collision scope restored");
    g_destroy = NativeDestroy; Destroy(ps);
    Check(t_context == 0, "context scope restored");
    g_release = NativeRelease; Check(Release(ps), "release result forwarded");
    Check(t_layer == 0 && !t_file[0], "layer scope restored");
    g_read = NativeRead; Check(SessionRead(ps, entity) == -7, "read result forwarded");
    g_write = NativeWrite; Check(SessionWrite(ps, entity) == 23, "write result forwarded");
    TestUnwind(ps);
    for (unsigned i = 0; i < kHistory + 15; ++i) Record("bounded", i, false, false);
    Check(g_history[(g_sequence - 1) % kHistory].sequence == g_sequence, "history wraps to newest record");
    Check(!Version(nullptr, 0, 0), "missing module rejected");
    Check(!Version(GetModuleHandleW(nullptr), 0, 0), "different PE version rejected");
    BYTE slot[16] = {};
    void* original = nullptr;
    Check(!Hook(reinterpret_cast<HMODULE>(slot), 0, 8, reinterpret_cast<void*>(Collision), &original) && !original,
        "unexpected vtable target rejected without mutation");
    void* page = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(page != nullptr, "test vtable allocated");
    Put(page, 0, reinterpret_cast<ULONG64>(page) + 8);
    Check(Hook(reinterpret_cast<HMODULE>(page), 0, 8, reinterpret_cast<void*>(Collision), &original), "verified vtable patched");
    Check(original == static_cast<BYTE*>(page) + 8 && Pointer(reinterpret_cast<ULONG64>(page)) == reinterpret_cast<ULONG64>(Collision),
        "original published and replacement stored");
    VirtualFree(page, 0, MEM_RELEASE);
    CONTEXT live = {}; DiagnosticRange bounded[1];
    t_collision = reinterpret_cast<ULONG64>(ps);
    Check(WorldTraceReport(live, bounded, 1) == 1, "active object capture respects caller capacity");
    t_collision = 0;

    CrashLogInstall(); CrashLogStartWorker();
    for (unsigned i = 0; i < 100 && !InterlockedCompareExchange(&g_dumpReady, 0, 0); ++i) Sleep(50);
    Check(g_dumpReady != 0, "dump worker ready");
    TestHandledException();
    // A heap marker must survive in the explicit memory ranges of the resulting dump.
    void* marker = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Check(marker != nullptr, "heap marker allocated");
    strcpy_s(static_cast<char*>(marker), 4096, name);
    CONTEXT c = {}; RtlCaptureContext(&c); c.Rbx = reinterpret_cast<ULONG64>(marker);
    EXCEPTION_RECORD exception = {}; exception.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    exception.ExceptionAddress = reinterpret_cast<void*>(c.Rip);
    EXCEPTION_POINTERS info = {&exception, &c};
    DiagnosticRange ranges[160];
    unsigned count = WorldTraceReport(c, ranges, 160);
    Check(count > 0 && count <= 160, "selective memory list bounded");
    Dump(&info, false, false, ranges, count);
    Check(GetFileAttributesA(g_job.path) != INVALID_FILE_ATTRIBUTES, "dump file created");
    char log[MAX_PATH]; snprintf(log, sizeof(log), "%s.log", g_job.path);
    Check(GetFileAttributesA(log) != INVALID_FILE_ATTRIBUTES, "independent log snapshot created");
    // Generic report limits cannot consume the separate physics budget.
    g_reports = kMaxReports + 1; g_physicsReports = 0;
    g_dumpReports = 3; g_physicsDumps = 0;
    Dump(&info, true, false, ranges, count);
    Check(g_physicsDumps == 1, "physics dump reserved after ordinary quota exhausted");
    g_dumpReports = 3;
    Dump(&info, false, true, ranges, count);
    Check(g_dumpReports == 3 && !g_dumpBusy, "unhandled dump bypasses first-chance quota");
    Check(GetFileAttributesA(g_job.path) != INVALID_FILE_ATTRIBUTES, "unhandled artifact exists");
    AcquireSRWLockExclusive(&g_lock);
    count = WorldTraceReport(c, ranges, 160);
    ReleaseSRWLockExclusive(&g_lock);
    Check(count > 0, "crash report does not wait on interrupted history writer");
    ShutdownQueryFn previousQuery = g_shutdownQuery;
    g_shutdownQuery = FakeShutdown;
    ULONG64 beforeShutdown = GetTickCount64();
    Dump(&info, false, true, ranges, count);
    Check(!g_dumpBusy && GetTickCount64() - beforeShutdown < 1000, "shutdown never waits for a terminated worker");
    LONG snapshot = g_snapshotSerial;
    SnapshotLog();
    Check(g_snapshotSerial == snapshot + 1, "shutdown text snapshot runs on faulting thread");
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*-p%lu-t%lu-e%ld.exception.log", g_directory,
        GetCurrentProcessId(), GetCurrentThreadId(), g_snapshotSerial);
    WIN32_FIND_DATAA found = {}; HANDLE match = FindFirstFileA(pattern, &found);
    Check(match != INVALID_HANDLE_VALUE && found.nFileSizeLow > 0, "shutdown report exists independently of worker");
    FindClose(match);
    g_shutdownQuery = previousQuery;
    char manifest[MAX_PATH]; snprintf(manifest, sizeof(manifest), "%s\\run-pid.txt", argv[1]);
    FILE* file = nullptr; fopen_s(&file, manifest, "w");
    Check(file != nullptr, "fresh test run manifest created");
    fprintf(file, "%lu", GetCurrentProcessId()); fclose(file);
    printf("PASS: %u world trace / crash dump checks\n", checks);
    return 0;
}
