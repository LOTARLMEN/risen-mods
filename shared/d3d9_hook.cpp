#include "d3d9_hook.h"

#include "common.h"
#include "iat_patch.h"

namespace {

D3D9Callbacks g_callbacks;
IDirect3DDevice9* g_device; // the game's device; we only draw on this one
bool g_drawFailed;

using Direct3DCreate9Fn = IDirect3D9*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                                   IDirect3DDevice9**);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);

Direct3DCreate9Fn g_origCreate9;
CreateDeviceFn g_origCreateDevice;
ResetFn g_origReset;
PresentFn g_origPresent;

constexpr int kCreateDeviceIndex = 16; // IDirect3D9
constexpr int kResetIndex = 16;        // IDirect3DDevice9
constexpr int kPresentIndex = 17;

// Swaps a vtable slot (vtables are shared by all objects of the class) and returns the previous entry.
void* PatchVtable(void* object, int index, void* hook)
{
    void** slot = *reinterpret_cast<void***>(object) + index;
    if (*slot == hook)
        return nullptr;
    DWORD old;
    VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    void* orig = *slot;
    *slot = hook;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return orig;
}

void Draw(IDirect3DDevice9* dev)
{
    IDirect3DSurface9* backBuffer = nullptr;
    IDirect3DSurface9* oldTarget = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)))
        return;
    dev->GetRenderTarget(0, &oldTarget);
    D3DVIEWPORT9 viewport;
    dev->GetViewport(&viewport);
    if (oldTarget != backBuffer)
        dev->SetRenderTarget(0, backBuffer);
    if (SUCCEEDED(dev->BeginScene())) {
        g_callbacks.onDraw(dev);
        dev->EndScene();
    }
    if (oldTarget != backBuffer)
        dev->SetRenderTarget(0, oldTarget);
    dev->SetViewport(&viewport);
    if (oldTarget)
        oldTarget->Release();
    backBuffer->Release();
}

HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd,
                                      const RGNDATA* dirty)
{
    if (dev == g_device && g_callbacks.onDraw && !g_drawFailed) {
        __try {
            Draw(dev);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_drawFailed = true; // never take the game down because of an overlay
            Log("d3d9: overlay crashed (0x%08lX), overlay disabled", GetExceptionCode());
        }
    }
    return g_origPresent(dev, src, dst, wnd, dirty);
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* params)
{
    bool ours = dev == g_device;
    if (ours && g_callbacks.onBeforeReset)
        g_callbacks.onBeforeReset();
    HRESULT hr = g_origReset(dev, params);
    if (ours && SUCCEEDED(hr) && g_callbacks.onAfterReset)
        g_callbacks.onAfterReset();
    return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
                                           D3DPRESENT_PARAMETERS* params, IDirect3DDevice9** out)
{
    HRESULT hr = g_origCreateDevice(d3d, adapter, type, focus, flags, params, out);
    if (FAILED(hr) || !out || !*out || g_device)
        return hr;
    g_device = *out;
    if (void* p = PatchVtable(g_device, kPresentIndex, reinterpret_cast<void*>(HookPresent)))
        g_origPresent = reinterpret_cast<PresentFn>(p);
    if (void* p = PatchVtable(g_device, kResetIndex, reinterpret_cast<void*>(HookReset)))
        g_origReset = reinterpret_cast<ResetFn>(p);
    HWND hwnd = params && params->hDeviceWindow ? params->hDeviceWindow : focus;
    Log("d3d9: device %p created for window %p (%ux%u)", g_device, hwnd, params ? params->BackBufferWidth : 0,
        params ? params->BackBufferHeight : 0);
    if (g_callbacks.onInit)
        g_callbacks.onInit(g_device, hwnd);
    return hr;
}

IDirect3D9* WINAPI HookDirect3DCreate9(UINT sdk)
{
    IDirect3D9* d3d = g_origCreate9(sdk);
    if (d3d)
        if (void* p = PatchVtable(d3d, kCreateDeviceIndex, reinterpret_cast<void*>(HookCreateDevice)))
            g_origCreateDevice = reinterpret_cast<CreateDeviceFn>(p);
    return d3d;
}

} // namespace

bool D3D9HookInstall(const D3D9Callbacks& callbacks)
{
    g_callbacks = callbacks;
    HMODULE engine = GetModuleHandleA("Engine.dll");
    if (!engine)
        return false;
    g_origCreate9 = reinterpret_cast<Direct3DCreate9Fn>(
        PatchImport(engine, "d3d9.dll", "Direct3DCreate9", reinterpret_cast<void*>(HookDirect3DCreate9)));
    return g_origCreate9 != nullptr;
}
