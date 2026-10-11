// Dear ImGui on top of the game's frame (shared/d3d9_hook): the status icons in the top-left corner and the
// settings window (window.cpp). ImGui gets the mouse only while the window is open; keyboard never (the window
// reads keys itself for rebinding).

#include <windows.h>
#include <cmath>
#include <cstring>

#include "common.h"
#include "d3d9_hook.h"
#include "game_assets.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "menu.h"
#include "risen_style.h"

namespace {

bool g_ready;
ImFont* g_font;   // icon text
ImFont* g_uiFont; // window
IDirect3DTexture9* g_atlas; // the game's GUI atlas (cursor, parchment), null = plain ImGui look

void OnInit(IDirect3DDevice9* device, HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // nothing to remember between runs
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    g_font = LoadSystemFont("segoeuib.ttf", 32.0f);
    if (!g_font)
        g_font = io.Fonts->AddFontDefault();
    g_uiFont = LoadGameUiFont(20.0f); // the game's interface fonts

    g_atlas = GameLoadTexture(device, kAtlasPak, kAtlasName);
    g_ready = ImGui_ImplWin32_Init(hwnd) && ImGui_ImplDX9_Init(device);
    Log("overlay: %s, game style %s", g_ready ? "ready" : "init FAILED", g_atlas ? "on" : "off (plain look)");
}

void OnBeforeReset()
{
    if (g_ready)
        ImGui_ImplDX9_InvalidateDeviceObjects();
}

void OnAfterReset()
{
    if (g_ready)
        ImGui_ImplDX9_CreateDeviceObjects();
}

const ImU32 kIconColor = IM_COL32(235, 225, 200, 255);
const ImU32 kAccent = IM_COL32(230, 175, 70, 255);

// Hourglass inside the box (x, y, w, h); sand runs from top to bottom with phase 0..1.
void DrawHourglass(ImDrawList* dl, float x, float y, float w, float h, float phase, float line)
{
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float pad = w * 0.12f; // glass is narrower than the caps
    ImVec2 tl(x + pad, y), tr(x + w - pad, y), bl(x + pad, y + h), br(x + w - pad, y + h), mid(cx, cy);

    // Sand: top triangle shrinks towards the neck, bottom pile grows, a thin stream in between.
    float top = 1.0f - phase, bottom = phase;
    if (top > 0.02f) {
        float sy = cy - (cy - y) * top * 0.85f;
        float half = (w * 0.5f - pad) * (cy - sy) / (cy - y);
        dl->AddTriangleFilled(ImVec2(cx - half, sy), ImVec2(cx + half, sy), mid, kAccent);
    }
    if (bottom > 0.02f) {
        float py = y + h - (h * 0.5f) * bottom * 0.8f;
        dl->AddTriangleFilled(ImVec2(cx, py), ImVec2(x + w - pad, y + h), ImVec2(x + pad, y + h), kAccent);
    }
    dl->AddLine(mid, ImVec2(cx, y + h - 1.0f), kAccent, line * 0.6f);

    dl->AddTriangle(tl, tr, mid, kIconColor, line);
    dl->AddTriangle(mid, br, bl, kIconColor, line);
    dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), kIconColor, line * 1.8f);
    dl->AddLine(ImVec2(x, y + h), ImVec2(x + w, y + h), kIconColor, line * 1.8f);
}

// Open hand, palm towards the viewer, inside the box (x, y, w, h); a gold coin bobs above it with phase 0..1.
void DrawHand(ImDrawList* dl, float x, float y, float w, float h, float phase, float line)
{
    float palmTop = y + h * 0.52f, palmBottom = y + h;
    float palmL = x + w * 0.22f, palmR = x + w * 0.98f;
    float fw = (palmR - palmL) / 4.0f; // finger slot
    const float tips[4] = {0.30f, 0.20f, 0.24f, 0.36f}; // index .. little finger, top as a fraction of h
    for (int i = 0; i < 4; ++i) {
        float fx = palmL + fw * i + fw * 0.12f;
        dl->AddRectFilled(ImVec2(fx, y + h * tips[i]), ImVec2(fx + fw * 0.76f, palmTop + h * 0.08f), kIconColor,
                          fw * 0.38f);
    }
    // Thumb: a slanted rounded bar on the left.
    ImVec2 t0(palmL + fw * 0.3f, palmTop + h * 0.22f), t1(x + w * 0.02f, palmTop - h * 0.04f);
    dl->AddLine(t0, t1, kIconColor, fw * 0.78f);
    dl->AddCircleFilled(t1, fw * 0.39f, kIconColor);
    dl->AddRectFilled(ImVec2(palmL, palmTop), ImVec2(palmR, palmBottom), kIconColor, fw * 0.6f,
                      ImDrawFlags_RoundCornersBottom);
    // Finger gaps.
    ImU32 gap = IM_COL32(20, 16, 10, 200);
    for (int i = 1; i < 4; ++i) {
        float gx = palmL + fw * i;
        dl->AddLine(ImVec2(gx, palmTop - h * 0.02f), ImVec2(gx, palmTop + h * 0.12f), gap, line * 0.7f);
    }
    // Coin above the hand.
    float bob = std::sin(phase * 6.2832f) * h * 0.04f;
    ImVec2 c(x + w * 0.62f, y + h * 0.08f + bob);
    dl->AddCircleFilled(c, h * 0.11f, kAccent);
    dl->AddCircle(c, h * 0.11f, IM_COL32(150, 105, 30, 255), 0, line * 0.6f);
}

// One box per visible status, left to right.
void DrawStatusRow(float screenScale)
{
    const MenuConfig& config = MenuGetConfig();
    float scale = screenScale * config.iconScale;
    float pad = 10.0f * scale, iconH = 34.0f * scale, fontSize = 32.0f * scale;
    float x = 24.0f * scale, y = 24.0f * scale;
    float phase = std::fmod(static_cast<float>(ImGui::GetTime()) / 1.6f, 1.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();

    RegistryLock();
    for (const HubStatus* s : Statuses()) {
        char text[32] = "";
        if (!s->query(text, sizeof(text)))
            continue;
        float iconW = (s->icon == HubIconHand ? 32.0f : 22.0f) * scale;
        ImVec2 textSize = text[0] ? g_font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text) : ImVec2(0, 0);
        float boxW = pad * 2 + iconW + (text[0] ? pad + textSize.x : 0.0f);
        float boxH = pad * 2 + (iconH > textSize.y ? iconH : textSize.y);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + boxW, y + boxH), IM_COL32(20, 16, 10, 170), 8.0f * scale);
        dl->AddRect(ImVec2(x, y), ImVec2(x + boxW, y + boxH), IM_COL32(200, 170, 110, 120), 8.0f * scale, 0,
                    1.5f * scale);
        float iy = y + (boxH - iconH) * 0.5f;
        if (s->icon == HubIconHand)
            DrawHand(dl, x + pad, iy, iconW, iconH, phase, 2.0f * scale);
        else
            DrawHourglass(dl, x + pad, iy, iconW, iconH, phase, 2.0f * scale);
        if (text[0])
            dl->AddText(g_font, fontSize, ImVec2(x + pad * 2 + iconW, y + (boxH - textSize.y) * 0.5f),
                        IM_COL32(245, 235, 210, 255), text);
        x += boxW + 8.0f * scale;
    }
    RegistryUnlock();
}

bool AnyStatusVisible()
{
    char text[32];
    bool any = false;
    RegistryLock();
    for (const HubStatus* s : Statuses())
        if (s->query(text, sizeof(text))) {
            any = true;
            break;
        }
    RegistryUnlock();
    return any;
}

void OnDraw(IDirect3DDevice9*)
{
    if (!g_ready)
        return;
    bool open = MenuIsOpen();
    bool icons = MenuGetConfig().icons && AnyStatusVisible();
    if (!open && !icons)
        return; // nothing on screen: skip ImGui entirely

    ImGuiIO& io = ImGui::GetIO();
    if (open) {
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        io.MouseDrawCursor = !g_atlas; // the game hides the system cursor; with the atlas we draw the game's arrow
        // The game's raw input takes the buttons away from window messages: read them directly.
        bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
        io.AddMouseButtonEvent(0, (GetAsyncKeyState(swapped ? VK_RBUTTON : VK_LBUTTON) & 0x8000) != 0);
        io.AddMouseButtonEvent(1, (GetAsyncKeyState(swapped ? VK_LBUTTON : VK_RBUTTON) & 0x8000) != 0);
        if (float wheel = MenuTakeWheel())
            io.AddMouseWheelEvent(0.0f, wheel);
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        io.MouseDrawCursor = false;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame(); // also reads the cursor position (GetCursorPos)
    ImGui::NewFrame();

    float screenScale = io.DisplaySize.y / 1080.0f;
    if (icons)
        DrawStatusRow(screenScale);
    if (open) {
        ImGui::PushFont(g_uiFont, 20.0f * screenScale);
        WindowDraw(screenScale);
        if (g_atlas) {
            const AtlasRect& r = kAtlasCursor;
            float s = screenScale * 0.9f;
            ImVec2 p = io.MousePos;
            ImGui::GetForegroundDrawList()->AddImage(
                reinterpret_cast<ImTextureID>(g_atlas), p,
                ImVec2(p.x + (r.x1 - r.x0) * s, p.y + (r.y1 - r.y0) * s),
                ImVec2(r.x0 / kAtlasSize, r.y0 / kAtlasSize), ImVec2(r.x1 / kAtlasSize, r.y1 / kAtlasSize));
        }
        ImGui::PopFont();
    }

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

} // namespace

void* OverlayAtlas()
{
    return g_atlas;
}

bool OverlayReady()
{
    return g_ready;
}

bool OverlayInstall()
{
    D3D9Callbacks callbacks = {OnInit, OnDraw, OnBeforeReset, OnAfterReset};
    return D3D9HookInstall(callbacks);
}
