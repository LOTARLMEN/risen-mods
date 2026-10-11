#pragma once

// Draw on top of the game's D3D9 frame.
// Engine.dll imports Direct3DCreate9; we hook that import, then IDirect3D9::CreateDevice, then the device's
// Present and Reset. Works with and without ReShade (bin\d3d9.dll). Call D3D9HookInstall from DllMain: the mod
// loader runs before the game creates its device.

#include <d3d9.h>

struct D3D9Callbacks {
    // Device created (game thread). hwnd is the game window.
    void (*onInit)(IDirect3DDevice9* device, HWND hwnd);
    // Inside BeginScene/EndScene with the back buffer bound, right before the game's Present.
    void (*onDraw)(IDirect3DDevice9* device);
    // Around IDirect3DDevice9::Reset: release / recreate D3DPOOL_DEFAULT resources.
    void (*onBeforeReset)();
    void (*onAfterReset)();
};

bool D3D9HookInstall(const D3D9Callbacks& callbacks);
