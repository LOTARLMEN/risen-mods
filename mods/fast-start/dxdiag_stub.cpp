// Faster game startup: skip DxDiag.
// Engine.dll creates CLSID_DxDiagProvider only to read DxDiag_DisplayDevices.<n>.szDisplayMemoryLocalized (video memory).
// DxDiag's Initialize collects info about the whole system and creates D3D11/D3D12 devices: ~7 s.
// We hook CoCreateInstance in Engine's import table and return a small provider with the same tree, filled from DXGI
// ("<dedicated + shared> MB", the same number DxDiag reports). If DXGI finds no adapter, the real DxDiag is used.

#include <windows.h>
#include <dxdiag.h>
#include <dxgi.h>
#include <cstdio>
#include <string>
#include <vector>

#include "dxdiag_stub.h"
#include "iat_patch.h"

#include "common.h"

namespace {

const GUID kClsidDxDiagProvider = {0xA65B8071, 0x3BFE, 0x4213, {0x9A, 0x5B, 0x49, 0x1D, 0xA4, 0x46, 0x1C, 0xA7}};
const GUID kIidDxDiagProvider = {0x9C6B4CB0, 0x23F8, 0x49CC, {0xA3, 0xED, 0x45, 0xA5, 0x50, 0x00, 0xA6, 0xD2}};
const GUID kIidDxDiagContainer = {0x7D0F462F, 0x4064, 0x4862, {0xBC, 0x7F, 0x93, 0x3E, 0x50, 0x58, 0xC1, 0x0F}};

// Objects live for the whole process, so reference counting is a no-op.
class Container : public IDxDiagContainer {
public:
    std::wstring name;
    std::vector<std::pair<std::wstring, std::wstring>> props;
    std::vector<Container*> children;

    explicit Container(std::wstring n) : name(std::move(n)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** out) override
    {
        if (!out)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == kIidDxDiagContainer) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP GetNumberOfChildContainers(DWORD* count) override
    {
        if (!count)
            return E_INVALIDARG;
        *count = static_cast<DWORD>(children.size());
        return S_OK;
    }
    STDMETHODIMP EnumChildContainerNames(DWORD index, LPWSTR out, DWORD size) override
    {
        return index < children.size() ? CopyName(children[index]->name, out, size) : E_INVALIDARG;
    }
    STDMETHODIMP GetChildContainer(LPCWSTR path, IDxDiagContainer** out) override
    {
        if (!path || !out)
            return E_INVALIDARG;
        // Accepts dotted paths like "DxDiag_DisplayDevices.0", as the real one does.
        const wchar_t* dot = wcschr(path, L'.');
        std::wstring first = dot ? std::wstring(path, dot) : std::wstring(path);
        for (Container* c : children) {
            if (c->name != first)
                continue;
            if (dot)
                return c->GetChildContainer(dot + 1, out);
            *out = c;
            return S_OK;
        }
        *out = nullptr;
        return E_INVALIDARG;
    }
    STDMETHODIMP GetNumberOfProps(DWORD* count) override
    {
        if (!count)
            return E_INVALIDARG;
        *count = static_cast<DWORD>(props.size());
        return S_OK;
    }
    STDMETHODIMP EnumPropNames(DWORD index, WCHAR* out, DWORD size) override
    {
        return index < props.size() ? CopyName(props[index].first, out, size) : E_INVALIDARG;
    }
    STDMETHODIMP GetProp(LPCWSTR prop, VARIANT* out) override
    {
        if (!prop || !out)
            return E_INVALIDARG;
        for (auto& p : props) {
            if (p.first != prop)
                continue;
            VariantInit(out);
            out->vt = VT_BSTR;
            out->bstrVal = SysAllocString(p.second.c_str());
            return out->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        return E_INVALIDARG;
    }

private:
    static HRESULT CopyName(const std::wstring& name, wchar_t* out, DWORD size)
    {
        if (!out || size <= name.size())
            return E_INVALIDARG;
        wcscpy_s(out, size, name.c_str());
        return S_OK;
    }
};

class Provider : public IDxDiagProvider {
public:
    Container root{L""};

    STDMETHODIMP QueryInterface(REFIID riid, void** out) override
    {
        if (!out)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == kIidDxDiagProvider) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP Initialize(DXDIAG_INIT_PARAMS* params) override
    {
        return params && params->dwSize == sizeof(DXDIAG_INIT_PARAMS) ? S_OK : E_INVALIDARG;
    }
    STDMETHODIMP GetRootContainer(IDxDiagContainer** out) override
    {
        if (!out)
            return E_INVALIDARG;
        *out = &root;
        return S_OK;
    }
};

Provider* g_provider;
bool g_providerBuilt;

using CoCreateInstanceFn = HRESULT(WINAPI*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
CoCreateInstanceFn g_origCoCreateInstance;

std::wstring Megabytes(unsigned long long bytes)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%llu MB", bytes >> 20);
    return buf;
}

// Display devices like DxDiag lists them: hardware adapters that drive at least one output.
Provider* BuildProvider()
{
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    auto create = dxgi ? reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi, "CreateDXGIFactory1"))
                       : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (!create || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
        return nullptr;

    auto provider = new Provider;
    auto devices = new Container(L"DxDiag_DisplayDevices");
    provider->root.children.push_back(devices);
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        IDXGIOutput* output = nullptr;
        bool use = SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                   SUCCEEDED(adapter->EnumOutputs(0, &output));
        if (output)
            output->Release();
        adapter->Release();
        if (!use)
            continue;
        auto dev = new Container(std::to_wstring(devices->children.size()));
        std::wstring total = Megabytes(desc.DedicatedVideoMemory + desc.SharedSystemMemory);
        std::wstring dedicated = Megabytes(desc.DedicatedVideoMemory);
        std::wstring shared = Megabytes(desc.SharedSystemMemory);
        dev->props = {
            {L"szDescription", desc.Description},
            {L"szDisplayMemoryLocalized", total},
            {L"szDisplayMemoryEnglish", total},
            {L"szDedicatedMemoryLocalized", dedicated},
            {L"szDedicatedMemoryEnglish", dedicated},
            {L"szSharedMemoryLocalized", shared},
            {L"szSharedMemoryEnglish", shared},
        };
        devices->children.push_back(dev);
        char name[128];
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
        Log("dxdiag: display device %s: %llu MB (dedicated %llu + shared %llu)", name,
            (desc.DedicatedVideoMemory + desc.SharedSystemMemory) >> 20, desc.DedicatedVideoMemory >> 20,
            desc.SharedSystemMemory >> 20);
    }
    factory->Release();
    if (devices->children.empty()) {
        Log("dxdiag: no display adapter found via DXGI, using the real DxDiag");
        return nullptr; // leaked on purpose, happens at most once
    }
    return provider;
}

HRESULT WINAPI HookCoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID riid, LPVOID* out)
{
    if (clsid == kClsidDxDiagProvider && !outer) {
        if (!g_providerBuilt) { // Engine calls this on its main thread
            g_provider = BuildProvider();
            g_providerBuilt = true;
        }
        if (g_provider) {
            Log("dxdiag: served by the stub");
            return g_provider->QueryInterface(riid, out);
        }
    }
    return g_origCoCreateInstance(clsid, outer, context, riid, out);
}

} // namespace

bool DxDiagStubInstall()
{
    HMODULE engine = GetModuleHandleA("Engine.dll");
    HMODULE ole32 = GetModuleHandleA("ole32.dll");
    if (!engine || !ole32)
        return false;
    g_origCoCreateInstance = reinterpret_cast<CoCreateInstanceFn>(GetProcAddress(ole32, "CoCreateInstance"));
    return g_origCoCreateInstance &&
           PatchImport(engine, "ole32.dll", "CoCreateInstance", reinterpret_cast<void*>(HookCoCreateInstance));
}
