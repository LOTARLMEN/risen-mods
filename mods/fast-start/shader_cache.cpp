// Faster game startup: disk cache for compiled shaders.
// Engine.dll compiles its ~34 effects on every start: D3DXCreateEffectCompiler + ID3DXEffectCompiler::CompileEffect
// (~1.3 s), then D3DXCreateEffect from the compiled binary. We replace the D3DXCreateEffectCompiler import and patch
// the compiler's vtable (shared by all compilers): the key is a hash of everything that decides the result - the
// effect source (preprocessed when an #include handler is passed), defines, compiler flags and CompileEffect flags -
// the value is the compiled effect binary, returned in a D3DX buffer as the compiler would.
// Compilers that get SetLiteral calls are not cached (literals change the code).
// Cache file: "RSC1", then records "key1 key2 size binary", appended on every miss; a damaged tail is dropped.

#include <windows.h>
#include <unknwn.h>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "iat_patch.h"
#include "shader_cache.h"

namespace {

struct D3DXMacro {
    const char* name;
    const char* definition;
};

struct ID3DXBuffer : IUnknown {
    virtual void* STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual DWORD STDMETHODCALLTYPE GetBufferSize() = 0;
};

using Handle = const char*; // D3DXHANDLE: a name string or an opaque handle

// ID3DXEffectCompiler vtable slots (d3dx9effect.h): IUnknown 0-2, ID3DXBaseEffect 3-56, compiler 57-60.
constexpr int kSlotRelease = 2;
constexpr int kSlotSetLiteral = 57;
constexpr int kSlotCompileEffect = 59;

using CreateEffectCompilerFn = HRESULT(WINAPI*)(const char* src, UINT srcLen, const D3DXMacro* defines, void* include,
                                                DWORD flags, IUnknown** compiler, ID3DXBuffer** errors);
using PreprocessShaderFn = HRESULT(WINAPI*)(const char* src, UINT srcLen, const D3DXMacro* defines, void* include,
                                            ID3DXBuffer** out, ID3DXBuffer** errors);
using CreateBufferFn = HRESULT(WINAPI*)(DWORD size, ID3DXBuffer** buffer);
using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(IUnknown* self);
using SetLiteralFn = HRESULT(STDMETHODCALLTYPE*)(IUnknown* self, Handle parameter, BOOL literal);
using CompileEffectFn = HRESULT(STDMETHODCALLTYPE*)(IUnknown* self, DWORD flags, ID3DXBuffer** effect,
                                                    ID3DXBuffer** errors);

CreateEffectCompilerFn g_origCreateCompiler;
PreprocessShaderFn g_preprocess;
CreateBufferFn g_createBuffer;
ReleaseFn g_origRelease;
SetLiteralFn g_origSetLiteral;
CompileEffectFn g_origCompileEffect;

constexpr DWORD kMagic = 0x31435352; // "RSC1"

struct Key {
    unsigned long long a, b;
    bool operator==(const Key& o) const { return a == o.a && b == o.b; }
};
struct KeyHash {
    size_t operator()(const Key& k) const { return static_cast<size_t>(k.a); }
};

// Two independent 64-bit hashes (FNV-1a and a multiply-xorshift mix) - a collision of both is not a concern.
struct Hasher {
    unsigned long long a = 0xcbf29ce484222325ull, b = 0x9e3779b97f4a7c15ull;
    void Add(const void* data, size_t size)
    {
        auto p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            a = (a ^ p[i]) * 0x100000001b3ull;
            b = (b ^ p[i]) * 0xff51afd7ed558ccdull;
            b ^= b >> 29;
        }
        // Length separator, so ("ab","c") and ("a","bc") differ.
        unsigned long long n = size;
        a = (a ^ n) * 0x100000001b3ull;
        b = (b ^ n) * 0xc4ceb9fe1a85ec53ull;
    }
    void AddStr(const char* s) { Add(s ? s : "", s ? strlen(s) : 0); }
    void AddKey(const Key& k) { Add(&k, sizeof(k)); }
    Key Get() const { return {a, b}; }
};

struct CompilerInfo {
    Key source;
    bool cacheable;
};

std::mutex g_lock;
std::unordered_map<Key, std::vector<unsigned char>, KeyHash> g_cache;
std::unordered_map<IUnknown*, CompilerInfo> g_compilers;
std::string g_path;
HANDLE g_file = INVALID_HANDLE_VALUE;
bool g_loaded, g_vtablePatched;

LARGE_INTEGER g_freq;
LONG g_compilersMade, g_hits, g_misses, g_uncached, g_failed;
double g_createSec, g_hitSec, g_compileSec;

double Seconds(const LARGE_INTEGER& t0)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - t0.QuadPart) / double(g_freq.QuadPart);
}

bool PlausibleEffect(const unsigned char* p, size_t size)
{
    if (size < 8 || size % 4)
        return false;
    DWORD version;
    memcpy(&version, p, 4);
    return HIWORD(version) == 0xFEFF; // effect binary, 0xFEFF0901 = fx_2_0
}

void LoadLocked()
{
    g_loaded = true;
    std::vector<unsigned char> data;
    HANDLE h = CreateFileA(g_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size;
        if (GetFileSizeEx(h, &size) && size.QuadPart < (256ll << 20)) {
            data.resize(static_cast<size_t>(size.QuadPart));
            DWORD got = 0;
            if (!data.empty() && (!ReadFile(h, data.data(), DWORD(data.size()), &got, nullptr) || got != data.size()))
                data.clear();
        }
        CloseHandle(h);
    }
    size_t good = 0;
    DWORD magic = 0;
    if (data.size() >= 4)
        memcpy(&magic, data.data(), 4);
    if (magic == kMagic) {
        good = 4;
        for (size_t off = 4; off + 20 <= data.size();) {
            Key k;
            DWORD size;
            memcpy(&k.a, &data[off], 8);
            memcpy(&k.b, &data[off + 8], 8);
            memcpy(&size, &data[off + 16], 4);
            if (size > data.size() - off - 20 || !PlausibleEffect(&data[off + 20], size))
                break;
            g_cache[k].assign(data.begin() + off + 20, data.begin() + off + 20 + size);
            off += 20 + size;
            good = off;
        }
    }
    // Rewrite when missing / foreign / damaged; otherwise append to it.
    bool rewrite = good == 0 || good != data.size();
    g_file = CreateFileA(g_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, rewrite ? CREATE_ALWAYS : OPEN_EXISTING,
                         0, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
        Log("shader cache: can't write %s (error %lu), read-only", g_path.c_str(), GetLastError());
        return;
    }
    DWORD written;
    if (rewrite) {
        WriteFile(g_file, &kMagic, 4, &written, nullptr);
        for (auto& kv : g_cache) {
            DWORD size = DWORD(kv.second.size());
            WriteFile(g_file, &kv.first.a, 8, &written, nullptr);
            WriteFile(g_file, &kv.first.b, 8, &written, nullptr);
            WriteFile(g_file, &size, 4, &written, nullptr);
            WriteFile(g_file, kv.second.data(), size, &written, nullptr);
        }
        if (!data.empty())
            Log("shader cache: file damaged or old at byte %zu of %zu, rewritten with %zu effects", good, data.size(),
                g_cache.size());
    } else {
        SetFilePointer(g_file, 0, nullptr, FILE_END);
    }
    Log("shader cache: %zu effects loaded", g_cache.size());
}

void StoreLocked(const Key& k, const unsigned char* code, DWORD size)
{
    g_cache[k].assign(code, code + size);
    if (g_file == INVALID_HANDLE_VALUE)
        return;
    unsigned char head[20];
    memcpy(head, &k.a, 8);
    memcpy(head + 8, &k.b, 8);
    memcpy(head + 16, &size, 4);
    DWORD written;
    WriteFile(g_file, head, 20, &written, nullptr);
    WriteFile(g_file, code, size, &written, nullptr);
}

// --- ID3DXEffectCompiler methods

ULONG STDMETHODCALLTYPE HookRelease(IUnknown* self)
{
    ULONG left = g_origRelease(self);
    if (left == 0) {
        std::lock_guard<std::mutex> lock(g_lock);
        g_compilers.erase(self); // the address may be reused by the next compiler
    }
    return left;
}

HRESULT STDMETHODCALLTYPE HookSetLiteral(IUnknown* self, Handle parameter, BOOL literal)
{
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_compilers.find(self);
        if (it != g_compilers.end())
            it->second.cacheable = false;
    }
    return g_origSetLiteral(self, parameter, literal);
}

HRESULT STDMETHODCALLTYPE HookCompileEffect(IUnknown* self, DWORD flags, ID3DXBuffer** effect, ID3DXBuffer** errors)
{
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    bool keyed = false;
    Key key{};
    std::vector<unsigned char> code;
    if (effect) {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_compilers.find(self);
        if (it != g_compilers.end() && it->second.cacheable) {
            Hasher h;
            h.AddKey(it->second.source);
            h.Add(&flags, sizeof(flags));
            key = h.Get();
            keyed = true;
            if (!g_loaded)
                LoadLocked();
            auto hit = g_cache.find(key);
            if (hit != g_cache.end())
                code = hit->second;
        }
    }

    ID3DXBuffer* buffer = nullptr;
    if (!code.empty() && SUCCEEDED(g_createBuffer(DWORD(code.size()), &buffer)) && buffer) {
        memcpy(buffer->GetBufferPointer(), code.data(), code.size());
        *effect = buffer;
        if (errors)
            *errors = nullptr;
        std::lock_guard<std::mutex> lock(g_lock);
        ++g_hits;
        g_hitSec += Seconds(t0);
        return S_OK;
    }

    HRESULT hr = g_origCompileEffect(self, flags, effect, errors);
    std::lock_guard<std::mutex> lock(g_lock);
    g_compileSec += Seconds(t0);
    if (FAILED(hr) || !effect || !*effect) {
        ++g_failed;
        return hr;
    }
    if (!keyed) {
        ++g_uncached;
        return hr;
    }
    ++g_misses;
    auto bytes = static_cast<const unsigned char*>((*effect)->GetBufferPointer());
    DWORD size = (*effect)->GetBufferSize();
    if (PlausibleEffect(bytes, size))
        StoreLocked(key, bytes, size);
    else
        Log("shader cache: unexpected effect binary (%lu bytes), not stored", size);
    return hr;
}

void** VtableOf(IUnknown* obj)
{
    return *reinterpret_cast<void***>(obj);
}

bool InModule(void* p, HMODULE mod)
{
    HMODULE owner = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(p), &owner) &&
           owner == mod;
}

// All compilers share one vtable inside d3dx9_39.dll: patch it once, after checking the slots point into d3dx.
void PatchVtableLocked(IUnknown* compiler)
{
    g_vtablePatched = true;
    void** vt = VtableOf(compiler);
    HMODULE d3dx = GetModuleHandleA("d3dx9_39.dll");
    for (int slot : {kSlotRelease, kSlotSetLiteral, kSlotCompileEffect})
        if (!InModule(vt[slot], d3dx)) {
            Log("shader cache: unexpected ID3DXEffectCompiler vtable (slot %d), off", slot);
            return;
        }
    g_origRelease = reinterpret_cast<ReleaseFn>(vt[kSlotRelease]);
    g_origSetLiteral = reinterpret_cast<SetLiteralFn>(vt[kSlotSetLiteral]);
    g_origCompileEffect = reinterpret_cast<CompileEffectFn>(vt[kSlotCompileEffect]);
    DWORD old;
    if (!VirtualProtect(vt, sizeof(void*) * (kSlotCompileEffect + 1), PAGE_READWRITE, &old))
        return;
    vt[kSlotRelease] = reinterpret_cast<void*>(HookRelease);
    vt[kSlotSetLiteral] = reinterpret_cast<void*>(HookSetLiteral);
    vt[kSlotCompileEffect] = reinterpret_cast<void*>(HookCompileEffect);
    VirtualProtect(vt, sizeof(void*) * (kSlotCompileEffect + 1), old, &old);
}

bool SourceKey(const char* src, UINT srcLen, const D3DXMacro* defines, void* include, DWORD flags, Key* key)
{
    Hasher h;
    h.Add("risen effect compiler 1", 23);
    if (include) {
        // The source pulls in other files: hash the preprocessed text (all includes expanded, defines applied).
        ID3DXBuffer* out = nullptr;
        ID3DXBuffer* errors = nullptr;
        HRESULT hr = g_preprocess(src, srcLen, defines, include, &out, &errors);
        if (errors)
            errors->Release();
        if (FAILED(hr) || !out)
            return false;
        h.Add(out->GetBufferPointer(), out->GetBufferSize());
        out->Release();
    } else {
        h.Add(src, src ? srcLen : 0);
    }
    for (const D3DXMacro* d = defines; d && d->name; ++d) {
        h.AddStr(d->name);
        h.AddStr(d->definition);
    }
    h.Add(&flags, sizeof(flags));
    *key = h.Get();
    return true;
}

HRESULT WINAPI HookCreateCompiler(const char* src, UINT srcLen, const D3DXMacro* defines, void* include, DWORD flags,
                                  IUnknown** compiler, ID3DXBuffer** errors)
{
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    Key key;
    bool keyed = SourceKey(src, srcLen, defines, include, flags, &key);
    HRESULT hr = g_origCreateCompiler(src, srcLen, defines, include, flags, compiler, errors);
    std::lock_guard<std::mutex> lock(g_lock);
    ++g_compilersMade;
    g_createSec += Seconds(t0);
    if (SUCCEEDED(hr) && compiler && *compiler) {
        if (!g_vtablePatched)
            PatchVtableLocked(*compiler);
        g_compilers[*compiler] = {key, keyed};
    }
    return hr;
}

} // namespace

bool ShaderCacheInstall(const char* cachePath)
{
    HMODULE engine = GetModuleHandleA("Engine.dll");
    HMODULE d3dx = GetModuleHandleA("d3dx9_39.dll");
    if (!engine || !d3dx)
        return false;
    g_preprocess = reinterpret_cast<PreprocessShaderFn>(GetProcAddress(d3dx, "D3DXPreprocessShader"));
    g_createBuffer = reinterpret_cast<CreateBufferFn>(GetProcAddress(d3dx, "D3DXCreateBuffer"));
    if (!g_preprocess || !g_createBuffer)
        return false;
    QueryPerformanceFrequency(&g_freq);
    g_path = cachePath;
    g_origCreateCompiler = static_cast<CreateEffectCompilerFn>(PatchImport(
        engine, "d3dx9_39.dll", "D3DXCreateEffectCompiler", reinterpret_cast<void*>(HookCreateCompiler)));
    return g_origCreateCompiler != nullptr;
}

void ShaderCacheLogStats()
{
    std::lock_guard<std::mutex> lock(g_lock);
    Log("shader cache: %ld compilers (%.3f s); effects: %ld from cache (%.3f s), %ld compiled (%.3f s), %ld not "
        "cacheable, %ld failed",
        g_compilersMade, g_createSec, g_hits, g_hitSec, g_misses, g_compileSec, g_uncached, g_failed);
}
