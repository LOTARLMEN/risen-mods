// Speed indicator: hourglass + "x2" in the top-left corner while the speed-up is on. Drawn with Dear ImGui
// on top of the game's frame (shared/d3d9_hook). No input: the indicator never takes mouse or keyboard.

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "common.h"
#include "d3d9_hook.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "overlay.h"

namespace {

bool g_ready;
ImFont* g_font;

void OnInit(IDirect3DDevice9* device, HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // nothing to remember between runs
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    char font[MAX_PATH];
    GetWindowsDirectoryA(font, MAX_PATH);
    strcat_s(font, "\\Fonts\\segoeuib.ttf");
    if (GetFileAttributesA(font) != INVALID_FILE_ATTRIBUTES)
        g_font = io.Fonts->AddFontFromFileTTF(font, 32.0f);
    if (!g_font)
        g_font = io.Fonts->AddFontDefault();

    g_ready = ImGui_ImplWin32_Init(hwnd) && ImGui_ImplDX9_Init(device);
    Log("overlay: %s", g_ready ? "ready" : "init FAILED");
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

// Hourglass inside the box (x, y, w, h); sand runs from top to bottom with phase 0..1.
void DrawHourglass(ImDrawList* dl, float x, float y, float w, float h, float phase, float line)
{
    const ImU32 glass = IM_COL32(235, 225, 200, 255);
    const ImU32 sand = IM_COL32(230, 175, 70, 255);
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float pad = w * 0.12f; // glass is narrower than the caps
    ImVec2 tl(x + pad, y), tr(x + w - pad, y), bl(x + pad, y + h), br(x + w - pad, y + h), mid(cx, cy);

    // Sand: top triangle shrinks towards the neck, bottom pile grows, a thin stream in between.
    float top = 1.0f - phase, bottom = phase;
    if (top > 0.02f) {
        float sy = cy - (cy - y) * top * 0.85f;
        float half = (w * 0.5f - pad) * (cy - sy) / (cy - y);
        dl->AddTriangleFilled(ImVec2(cx - half, sy), ImVec2(cx + half, sy), mid, sand);
    }
    if (bottom > 0.02f) {
        float py = y + h - (h * 0.5f) * bottom * 0.8f;
        dl->AddTriangleFilled(ImVec2(cx, py), ImVec2(x + w - pad, y + h), ImVec2(x + pad, y + h), sand);
    }
    dl->AddLine(mid, ImVec2(cx, y + h - 1.0f), sand, line * 0.6f);

    dl->AddTriangle(tl, tr, mid, glass, line);
    dl->AddTriangle(mid, br, bl, glass, line);
    dl->AddLine(ImVec2(x, y), ImVec2(x + w, y), glass, line * 1.8f);
    dl->AddLine(ImVec2(x, y + h), ImVec2(x + w, y + h), glass, line * 1.8f);
}

void OnDraw(IDirect3DDevice9*)
{
    if (!g_ready || !SpeedIsActive() || !OverlayEnabled())
        return; // nothing on screen: skip ImGui entirely

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    float scale = io.DisplaySize.y / 1080.0f * OverlayScale();
    float pad = 10.0f * scale, iconH = 34.0f * scale, iconW = 22.0f * scale;
    float fontSize = 32.0f * scale;

    char text[16];
    snprintf(text, sizeof(text), "x%.2f", SpeedMultiplier());
    for (char* end = text + strlen(text) - 1; *end == '0' || *end == '.'; --end) { // "x2.00" -> "x2"
        bool dot = *end == '.';
        *end = 0;
        if (dot)
            break;
    }
    ImVec2 textSize = g_font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);

    float x = 24.0f * scale, y = 24.0f * scale;
    float boxW = pad * 3 + iconW + textSize.x, boxH = pad * 2 + (iconH > textSize.y ? iconH : textSize.y);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + boxW, y + boxH), IM_COL32(20, 16, 10, 170), 8.0f * scale);
    dl->AddRect(ImVec2(x, y), ImVec2(x + boxW, y + boxH), IM_COL32(200, 170, 110, 120), 8.0f * scale, 0, 1.5f * scale);

    float phase = std::fmod(static_cast<float>(ImGui::GetTime()) / 1.6f, 1.0f);
    DrawHourglass(dl, x + pad, y + (boxH - iconH) * 0.5f, iconW, iconH, phase, 2.0f * scale);
    dl->AddText(g_font, fontSize, ImVec2(x + pad * 2 + iconW, y + (boxH - textSize.y) * 0.5f),
                IM_COL32(245, 235, 210, 255), text);

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

} // namespace

bool OverlayInstall()
{
    D3D9Callbacks callbacks = {OnInit, OnDraw, OnBeforeReset, OnAfterReset};
    return D3D9HookInstall(callbacks);
}
