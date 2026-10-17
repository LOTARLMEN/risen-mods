#include "world_trace.h"
#include "common.h"
#include <cstdio>
#include <cstring>
#include <atomic>

namespace {
constexpr unsigned kHistory = 192;
struct Entity {
    ULONG64 address;
    char guid[40], name[96];
    float xyz[3];
    bool valid;
};
struct Event {
    ULONG64 sequence, tick, object, layer, context;
    DWORD thread, touches;
    char phase[32], file[96];
    Entity owner;
};
SRWLOCK g_lock = SRWLOCK_INIT;
Event g_history[kHistory];
ULONG64 g_sequence;
std::atomic<ULONG64> g_engine{0};
thread_local ULONG64 t_collision, t_layer, t_context;
thread_local char t_file[96];
void (*g_collision)(void*);
void (*g_destroy)(void*);
bool (*g_release)(void*);
int (*g_read)(void*, void*);
int (*g_write)(void*, void*);

bool Read(ULONG64 address, void* result, size_t size)
{
    SIZE_T read = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), result, size, &read)
        && read == size;
}
ULONG64 Pointer(ULONG64 address)
{
    ULONG64 value = 0;
    Read(address, &value, sizeof(value));
    return value;
}
void Text(ULONG64 address, char* output, size_t size)
{
    // Usually one kernel read; byte fallback also handles a string at a page boundary.
    if (Read(address, output, size - 1)) {
        output[size - 1] = 0;
        for (size_t i = 0; output[i]; ++i)
            if (static_cast<unsigned char>(output[i]) < 32) output[i] = '?';
        return;
    }
    for (size_t i = 0; i + 1 < size; ++i) {
        char c = 0;
        if (!Read(address + i, &c, 1) || !c) { output[i] = 0; return; }
        output[i] = static_cast<unsigned char>(c) < 32 ? '?' : c;
    }
    output[size - 1] = 0;
}
Entity Identify(ULONG64 owner)
{
    Entity e = {};
    e.address = owner;
    // The inherited GetID slot validates the layout before interpreting native fields.
    if (!owner || !g_engine || Pointer(Pointer(owner) + 0x148) != g_engine + 0x69f090)
        return e;
    GUID id = {};
    if (!Read(Pointer(Pointer(owner + 0x1c8) + 0x10), &id, sizeof(id)) ||
        !Read(owner + 0x90, e.xyz, sizeof(e.xyz))) return e;
    snprintf(e.guid, sizeof(e.guid), "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        id.Data1, id.Data2, id.Data3, id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3],
        id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
    Text(Pointer(Pointer(Pointer(owner + 0x1c0))), e.name, sizeof(e.name));
    e.valid = true;
    return e;
}
void Record(const char* phase, ULONG64 object, bool collision, bool disk)
{
    Event e = {};
    e.tick = GetTickCount64(); e.thread = GetCurrentThreadId();
    e.object = object; e.layer = t_layer; e.context = t_context;
    strcpy_s(e.phase, phase); strcpy_s(e.file, t_file);
    if (collision) {
        BYTE flags = 0;
        Read(object + 0x18, &flags, 1);
        if (!(flags & 0x10)) e.owner = Identify(Pointer(object + 0x10));
        Read(object + 0x60, &e.touches, sizeof(e.touches));
    }
    AcquireSRWLockExclusive(&g_lock);
    e.sequence = ++g_sequence;
    g_history[(e.sequence - 1) % kHistory] = e;
    ReleaseSRWLockExclusive(&g_lock);
    if (disk) Log("world trace #%llu %s thread=%lu object=%llx layer=%llx context=%llx file=%s",
        e.sequence, e.phase, e.thread, e.object, e.layer, e.context, e.file);
}
void Collision(void* self)
{
    const ULONG64 previous = t_collision;
    t_collision = reinterpret_cast<ULONG64>(self);
    Record("collision-exit begin", t_collision, true, false);
    bool complete = false;
    __try { g_collision(self); complete = true; }
    __finally {
        // No native reads after the original function: it may have freed its data.
        Record(complete ? "collision-exit end" : "collision-exit unwind", t_collision, false, false);
        t_collision = previous;
    }
}
void Destroy(void* self)
{
    const ULONG64 previous = t_context;
    t_context = reinterpret_cast<ULONG64>(self);
    Record("context-destroy begin", t_context, false, true);
    bool complete = false;
    __try { g_destroy(self); complete = true; }
    __finally {
        Record(complete ? "context-destroy end" : "context-destroy unwind", t_context, false, true);
        t_context = previous;
    }
}
bool Release(void* self)
{
    const ULONG64 previous = t_layer;
    char previousFile[96]; strcpy_s(previousFile, t_file);
    t_layer = reinterpret_cast<ULONG64>(self);
    Text(Pointer(t_layer + 0x18), t_file, sizeof(t_file));
    Record("layer-release begin", t_layer, false, true);
    bool complete = false, result = false;
    __try { result = g_release(self); complete = true; }
    __finally {
        Record(complete ? "layer-release end" : "layer-release unwind", t_layer, false, true);
        t_layer = previous; strcpy_s(t_file, previousFile);
    }
    return result;
}
int SessionRead(void* self, void* stream)
{
    Record("session-read begin", reinterpret_cast<ULONG64>(self), false, true);
    bool complete = false; int result = 0;
    __try { result = g_read(self, stream); complete = true; }
    __finally { Record(complete ? "session-read end" : "session-read unwind", reinterpret_cast<ULONG64>(self), false, true); }
    return result;
}
int SessionWrite(void* self, void* stream)
{
    Record("session-write begin", reinterpret_cast<ULONG64>(self), false, true);
    bool complete = false; int result = 0;
    __try { result = g_write(self, stream); complete = true; }
    __finally { Record(complete ? "session-write end" : "session-write unwind", reinterpret_cast<ULONG64>(self), false, true); }
    return result;
}
bool Version(HMODULE module, DWORD stamp, DWORD size)
{
    if (!module) return false;
    IMAGE_DOS_HEADER dos = {}; IMAGE_NT_HEADERS64 nt = {};
    ULONG64 base = reinterpret_cast<ULONG64>(module);
    return Read(base, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        Read(base + dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.TimeDateStamp == stamp && nt.OptionalHeader.SizeOfImage == size;
}
bool Hook(HMODULE module, ULONG64 slot, ULONG64 expected, void* replacement, void** original)
{
    auto target = reinterpret_cast<void**>(reinterpret_cast<ULONG64>(module) + slot);
    void* native = reinterpret_cast<void*>(reinterpret_cast<ULONG64>(module) + expected);
    if (Pointer(reinterpret_cast<ULONG64>(target)) != reinterpret_cast<ULONG64>(native)) {
        LogAlways("world trace: skipped slot +%llx: unexpected target", slot); return false;
    }
    DWORD protection;
    if (!VirtualProtect(target, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    // Publish the original before making the replacement visible on another thread.
    *original = native;
    bool ok = InterlockedCompareExchangePointer(target, replacement, native) == native;
    DWORD ignored; VirtualProtect(target, sizeof(void*), protection, &ignored);
    LogAlways("world trace: slot +%llx -> native +%llx %s", slot, expected, ok ? "installed" : "changed; skipped");
    return ok;
}
DWORD WINAPI Install(void*)
{
    HMODULE engine = nullptr, game = nullptr;
    for (unsigned i = 0; i < 300; ++i) {
        engine = GetModuleHandleW(L"Engine.dll"); game = GetModuleHandleW(L"Game.dll");
        if (engine && game) break;
        Sleep(100);
    }
    if (!Version(engine, 0x63ca945d, 0xd3a000) || !Version(game, 0x63c9aff6, 0x1417000)) {
        LogAlways("world trace: unsupported/missing Engine.dll or Game.dll; all layout hooks skipped"); return 0;
    }
    g_engine = reinterpret_cast<ULONG64>(engine);
    Hook(engine, 0x9c2fc8 + 0x1f8, 0x652cc0, reinterpret_cast<void*>(Collision), reinterpret_cast<void**>(&g_collision));
    Hook(engine, 0x9cf840 + 0x60, 0x69c9e0, reinterpret_cast<void*>(Destroy), reinterpret_cast<void**>(&g_destroy));
    Hook(game, 0xf30488 + 0x158, 0x8cf6c0, reinterpret_cast<void*>(Release), reinterpret_cast<void**>(&g_release));
    Hook(game, 0xf4f678 + 0x190, 0x970530, reinterpret_cast<void*>(SessionRead), reinterpret_cast<void**>(&g_read));
    Hook(game, 0xf4f678 + 0x198, 0x970320, reinterpret_cast<void*>(SessionWrite), reinterpret_cast<void**>(&g_write));
    return 0;
}
void EntityLog(const char* label, const Entity& e)
{
    LogAlways("world trace %s entity=%llx valid=%d GUID=%s name=%s position=(%.3f,%.3f,%.3f)",
        label, e.address, e.valid, e.guid, e.name, e.xyz[0], e.xyz[1], e.xyz[2]);
}
}

void WorldTraceStart()
{
    if (!IniInt("Diagnostics", "WorldTrace", 0)) { Log("world trace: off"); return; }
    HANDLE worker = CreateThread(nullptr, 0, Install, nullptr, 0, nullptr);
    if (worker) CloseHandle(worker);
    else LogAlways("world trace: could not start installer (%lu)", GetLastError());
}

unsigned WorldTraceReport(const CONTEXT& c, DiagnosticRange* ranges, unsigned capacity)
{
    if (TryAcquireSRWLockShared(&g_lock)) {
        // A nested fault while logging must not leave the hook-side exclusive lock blocked.
        __try {
            ULONG64 first = g_sequence > kHistory ? g_sequence - kHistory + 1 : 1;
            LogAlways("world trace: copied history %llu..%llu", first, g_sequence);
            for (ULONG64 i = first; i <= g_sequence; ++i) {
                const Event& e = g_history[(i - 1) % kHistory];
                LogAlways("world trace #%llu tick=%llu thread=%lu %s object=%llx layer=%llx context=%llx file=%s touches=%lu",
                    e.sequence, e.tick, e.thread, e.phase, e.object, e.layer, e.context, e.file, e.touches);
                if (e.owner.address) EntityLog("history", e.owner);
            }
        } __finally { ReleaseSRWLockShared(&g_lock); }
    } else LogAlways("world trace: history lock busy; skipped without waiting");
    unsigned count = 0;
    auto add = [&](ULONG64 address, ULONG size) {
        if (address && count < capacity) ranges[count++] = {address, size};
    };
    // Only dereference the synchronous active call, or the verified native crash register.
    ULONG64 ps = t_collision;
    if (!ps && g_engine && c.Rip >= g_engine + 0x652cc0 && c.Rip < g_engine + 0x652f00) ps = c.Rbx;
    if (ps) {
        add(ps, 0x100);
        ULONG64 owner = Pointer(ps + 0x10);
        EntityLog("live collision owner", Identify(owner));
        add(owner, 0x200);
        add(Pointer(owner + 0x1c8), 0x20);
        add(Pointer(Pointer(owner + 0x1c8) + 0x10), 0x20);
        ULONG64 metadata = Pointer(owner + 0x1c0);
        add(metadata, 0x20); add(Pointer(metadata), 0x20); add(Pointer(Pointer(metadata)), 96);
        DWORD touches = 0; Read(ps + 0x60, &touches, sizeof(touches));
        ULONG64 array = Pointer(ps + 0x58);
        LogAlways("world trace: active PS=%llx owner=%llx array=%llx touches=%lu", ps, owner, array, touches);
        unsigned limit = touches < 32 ? touches : 32;
        add(array, limit * 8);
        for (unsigned i = 0; i < limit; ++i) {
            ULONG64 shape = Pointer(array + i * 8);
            ULONG64 wrapper = Pointer(shape + 0x88), peer = Pointer(shape + 0xa8);
            LogAlways("world trace: touch[%u] shape=%llx wrapper=%llx associatedPS=%llx vtable=%llx",
                i, shape, wrapper, peer, Pointer(peer));
            add(shape, 0xb0); add(wrapper, 0x40); add(peer, 0x100);
            if (g_engine && Pointer(peer) == g_engine + 0x9c2fc8) {
                ULONG64 peerOwner = Pointer(peer + 0x10);
                EntityLog("touch owner", Identify(peerOwner)); add(peerOwner, 0x200);
            }
        }
    }
    const ULONG64 registers[] = {c.Rax,c.Rbx,c.Rcx,c.Rdx,c.Rsi,c.Rdi,c.R8,c.R9,c.R10,c.R11,c.R12,c.R13,c.R14,c.R15,c.Rsp};
    for (ULONG64 address : registers) add(address, 0x200);
    add(c.Rip, 32);
    return count;
}
