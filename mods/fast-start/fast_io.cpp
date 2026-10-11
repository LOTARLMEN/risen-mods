// Faster game startup: buffered reads of .pak files.
// While loading, FileSystem2.dll reads the .pak archives with millions of 1-64 byte ReadFile calls, each preceded by
// a seek (~8M syscalls, ~17 s of a ~32 s startup). We replace FFCore's g_FFReadFile / g_FFSetFilePointer /
// g_FFGetFilePointer in FileSystem2's import table: read-only .pak handles are read through a 64 KB window and the
// file position is kept in memory. Other files (saves, ini) go straight to the original functions.

#include <windows.h>
#include <cstring>
#include <unordered_map>

#include "fast_io.h"
#include "iat_patch.h"

namespace {

constexpr DWORD kWindow = 64 * 1024;

struct PakFile {
    SRWLOCK lock = SRWLOCK_INIT;
    HANDLE handle;
    long long size = 0;
    long long pos = 0;
    long long windowStart = 0;
    DWORD windowLen = 0;
    unsigned char window[kWindow];
};

SRWLOCK g_filesLock = SRWLOCK_INIT;
std::unordered_map<HANDLE, PakFile*> g_files;

volatile LONG64 g_reads, g_windowFills, g_directReads;

using FFCreateFileFn = void* (*)(const char*, int, int, unsigned, unsigned, void*);
using FFReadFileFn = bool (*)(void*, void*, DWORD, DWORD*);
using FFSetFilePointerFn = bool (*)(void*, long long, long long*, int);
using FFGetFilePointerFn = bool (*)(void*, long long*);
using FFCloseFileFn = bool (*)(void*);
using SetFilePointerExFn = BOOL(WINAPI*)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);

FFCreateFileFn g_origCreate;
FFReadFileFn g_origRead;
FFSetFilePointerFn g_origSeek;
FFGetFilePointerFn g_origTell;
FFCloseFileFn g_origClose;
SetFilePointerExFn g_origSetFilePointerEx;

PakFile* Find(HANDLE h)
{
    AcquireSRWLockShared(&g_filesLock);
    auto it = g_files.find(h);
    PakFile* f = it == g_files.end() ? nullptr : it->second;
    ReleaseSRWLockShared(&g_filesLock);
    return f;
}

bool IsPak(const char* path)
{
    size_t n = path ? strlen(path) : 0;
    return n > 4 && _stricmp(path + n - 4, ".pak") == 0;
}

// Reads at an absolute offset with the real API (moves the OS file pointer, which nobody else relies on).
bool ReadAt(PakFile* f, long long offset, void* dst, DWORD size, DWORD* got)
{
    LARGE_INTEGER li;
    li.QuadPart = offset;
    *got = 0;
    return g_origSetFilePointerEx(f->handle, li, nullptr, FILE_BEGIN) && ReadFile(f->handle, dst, size, got, nullptr);
}

void* HookCreate(const char* path, int create, int access, unsigned share, unsigned flags, void* tmpl)
{
    void* h = g_origCreate(path, create, access, share, flags, tmpl);
    // access 1 = read only (GENERIC_READ); writable files are left alone.
    if (!h || h == INVALID_HANDLE_VALUE || access != 1 || !IsPak(path))
        return h;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size))
        return h;
    auto f = new PakFile;
    f->handle = h;
    f->size = size.QuadPart;
    AcquireSRWLockExclusive(&g_filesLock);
    g_files[h] = f;
    ReleaseSRWLockExclusive(&g_filesLock);
    return h;
}

bool HookClose(void* h)
{
    AcquireSRWLockExclusive(&g_filesLock);
    auto it = g_files.find(h);
    PakFile* f = nullptr;
    if (it != g_files.end()) {
        f = it->second;
        g_files.erase(it);
    }
    ReleaseSRWLockExclusive(&g_filesLock);
    delete f;
    return g_origClose(h);
}

bool HookRead(void* h, void* dst, DWORD size, DWORD* read)
{
    PakFile* f = Find(h);
    if (!f)
        return g_origRead(h, dst, size, read);

    InterlockedIncrement64(&g_reads);
    AcquireSRWLockExclusive(&f->lock);
    bool ok = true;
    DWORD total = 0;
    auto out = static_cast<unsigned char*>(dst);
    while (total < size && f->pos < f->size) {
        long long inWindow = f->pos - f->windowStart;
        if (inWindow >= 0 && inWindow < f->windowLen) {
            DWORD n = static_cast<DWORD>(f->windowLen - inWindow);
            if (n > size - total)
                n = size - total;
            memcpy(out + total, f->window + inWindow, n);
            total += n;
            f->pos += n;
            continue;
        }
        if (size - total >= kWindow) { // big read: straight into the caller's buffer
            DWORD got = 0;
            InterlockedIncrement64(&g_directReads);
            ok = ReadAt(f, f->pos, out + total, size - total, &got);
            total += got;
            f->pos += got;
            break;
        }
        InterlockedIncrement64(&g_windowFills);
        DWORD got = 0;
        ok = ReadAt(f, f->pos, f->window, kWindow, &got);
        f->windowStart = f->pos;
        f->windowLen = ok ? got : 0;
        if (!ok || !got)
            break;
    }
    ReleaseSRWLockExclusive(&f->lock);
    if (read)
        *read = total;
    return ok;
}

// Shared by g_FFSetFilePointer and SetFilePointerEx: move = FILE_BEGIN / FILE_CURRENT / FILE_END.
bool Seek(PakFile* f, long long distance, long long* newPos, DWORD move)
{
    AcquireSRWLockExclusive(&f->lock);
    long long base = move == FILE_BEGIN ? 0 : move == FILE_CURRENT ? f->pos : f->size;
    long long target = base + distance;
    bool ok = move <= FILE_END && target >= 0;
    if (ok)
        f->pos = target;
    if (ok && newPos)
        *newPos = target;
    ReleaseSRWLockExclusive(&f->lock);
    if (!ok)
        SetLastError(ERROR_NEGATIVE_SEEK);
    return ok;
}

bool HookSeek(void* h, long long distance, long long* newPos, int whence)
{
    PakFile* f = Find(h);
    if (!f || whence < 0 || whence > 2) // g_FFSetFilePointer: 0 begin, 1 current, 2 end
        return g_origSeek(h, distance, newPos, whence);
    return Seek(f, distance, newPos, static_cast<DWORD>(whence));
}

bool HookTell(void* h, long long* pos)
{
    PakFile* f = Find(h);
    if (!f)
        return g_origTell(h, pos);
    AcquireSRWLockShared(&f->lock);
    *pos = f->pos;
    ReleaseSRWLockShared(&f->lock);
    return true;
}

BOOL WINAPI HookSetFilePointerEx(HANDLE h, LARGE_INTEGER distance, PLARGE_INTEGER newPos, DWORD move)
{
    PakFile* f = Find(h);
    if (!f)
        return g_origSetFilePointerEx(h, distance, newPos, move);
    return Seek(f, distance.QuadPart, newPos ? &newPos->QuadPart : nullptr, move);
}

} // namespace

bool FastIoInstall()
{
    HMODULE fs = GetModuleHandleA("FileSystem2.dll");
    HMODULE ffcore = GetModuleHandleA("FFCore.dll");
    if (!fs || !ffcore)
        return false;
    // Resolve originals from the exports first, so we either patch everything or nothing.
    g_origCreate = reinterpret_cast<FFCreateFileFn>(
        GetProcAddress(ffcore, "?g_FFCreateFile@@YAPEAXPEBDW4EFFFileCreate@@W4EFFFileAccess@@KKPEAX@Z"));
    g_origRead = reinterpret_cast<FFReadFileFn>(GetProcAddress(ffcore, "?g_FFReadFile@@YA_NPEAX0KPEAK@Z"));
    g_origSeek = reinterpret_cast<FFSetFilePointerFn>(
        GetProcAddress(ffcore, "?g_FFSetFilePointer@@YA_NPEAX_JPEA_JW4EFFFileSeek@@@Z"));
    g_origTell = reinterpret_cast<FFGetFilePointerFn>(GetProcAddress(ffcore, "?g_FFGetFilePointer@@YA_NPEAXAEA_J@Z"));
    g_origClose = reinterpret_cast<FFCloseFileFn>(GetProcAddress(ffcore, "?g_FFCloseFile@@YA_NPEAX@Z"));
    g_origSetFilePointerEx = reinterpret_cast<SetFilePointerExFn>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetFilePointerEx"));
    if (!g_origCreate || !g_origRead || !g_origSeek || !g_origTell || !g_origClose || !g_origSetFilePointerEx)
        return false;

    PatchImport(fs, "FFCore.dll", "?g_FFCreateFile@@YAPEAXPEBDW4EFFFileCreate@@W4EFFFileAccess@@KKPEAX@Z",
                reinterpret_cast<void*>(HookCreate));
    PatchImport(fs, "FFCore.dll", "?g_FFReadFile@@YA_NPEAX0KPEAK@Z", reinterpret_cast<void*>(HookRead));
    PatchImport(fs, "FFCore.dll", "?g_FFSetFilePointer@@YA_NPEAX_JPEA_JW4EFFFileSeek@@@Z",
                reinterpret_cast<void*>(HookSeek));
    PatchImport(fs, "FFCore.dll", "?g_FFGetFilePointer@@YA_NPEAXAEA_J@Z", reinterpret_cast<void*>(HookTell));
    PatchImport(fs, "FFCore.dll", "?g_FFCloseFile@@YA_NPEAX@Z", reinterpret_cast<void*>(HookClose));
    PatchImport(fs, "KERNEL32.dll", "SetFilePointerEx", reinterpret_cast<void*>(HookSetFilePointerEx));
    return true;
}

void FastIoStats(long long* reads, long long* windowFills, long long* directReads)
{
    *reads = g_reads;
    *windowFills = g_windowFills;
    *directReads = g_directReads;
}
