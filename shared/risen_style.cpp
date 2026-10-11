#include "risen_style.h"

#include <windows.h>
#include <cstring>
#include <vector>

#include "game_assets.h"

ImFont* LoadSystemFont(const char* file, float size)
{
    char path[MAX_PATH];
    GetWindowsDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\Fonts\\");
    strcat_s(path, file);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
        return nullptr;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size);
}

ImFont* LoadGameFont(const char* name, float size, bool merge)
{
    std::vector<unsigned char> data;
    if (!GamePakRead(kFontPak, name, &data) || data.empty())
        return nullptr;
    void* copy = IM_ALLOC(data.size()); // owned and freed by the ImGui font atlas
    memcpy(copy, data.data(), data.size());
    ImFontConfig config;
    config.MergeMode = merge;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(copy, static_cast<int>(data.size()), size, &config);
}

ImFont* LoadGameUiFont(float size)
{
    ImFont* font = LoadGameFont("QUORUMM.TTF", size, false);
    if (font)
        LoadGameFont("FreeSans.ttf", size, true);
    else
        font = LoadSystemFont("segoeui.ttf", size);
    return font ? font : ImGui::GetIO().Fonts->AddFontDefault();
}

void DrawParchmentFrame(ImDrawList* dl, ImTextureID atlas, ImVec2 a, ImVec2 b, float k)
{
    const AtlasRect& r = kAtlasFrame;
    const float m = kFrameBorder, mk = kFrameBorder * k;
    const float sx[4] = {r.x0, r.x0 + m, r.x1 - m, r.x1}, sy[4] = {r.y0, r.y0 + m, r.y1 - m, r.y1};
    const float dx[4] = {a.x, a.x + mk, b.x - mk, b.x}, dy[4] = {a.y, a.y + mk, b.y - mk, b.y};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            dl->AddImage(atlas, ImVec2(dx[x], dy[y]), ImVec2(dx[x + 1], dy[y + 1]),
                         ImVec2(sx[x] / kAtlasSize, sy[y] / kAtlasSize),
                         ImVec2(sx[x + 1] / kAtlasSize, sy[y + 1] / kAtlasSize));
}

int PushParchmentColors()
{
    const struct {
        ImGuiCol id;
        ImVec4 color;
    } colors[] = {
        {ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)},
        {ImGuiCol_Text, ImVec4(0.20f, 0.13f, 0.07f, 1.0f)},
        {ImGuiCol_TextDisabled, ImVec4(0.42f, 0.34f, 0.24f, 1.0f)},
        {ImGuiCol_Border, ImVec4(0.35f, 0.24f, 0.12f, 0.35f)},
        {ImGuiCol_FrameBg, ImVec4(0.35f, 0.24f, 0.12f, 0.16f)},
        {ImGuiCol_FrameBgHovered, ImVec4(0.35f, 0.24f, 0.12f, 0.28f)},
        {ImGuiCol_FrameBgActive, ImVec4(0.35f, 0.24f, 0.12f, 0.40f)},
        {ImGuiCol_CheckMark, ImVec4(0.50f, 0.12f, 0.06f, 1.0f)},
        {ImGuiCol_CheckboxSelectedBg, ImVec4(0.35f, 0.24f, 0.12f, 0.22f)},
        {ImGuiCol_SliderGrab, ImVec4(0.42f, 0.27f, 0.12f, 0.90f)},
        {ImGuiCol_SliderGrabActive, ImVec4(0.55f, 0.15f, 0.07f, 1.0f)},
        {ImGuiCol_Button, ImVec4(0.35f, 0.24f, 0.12f, 0.20f)},
        {ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.24f, 0.12f, 0.35f)},
        {ImGuiCol_ButtonActive, ImVec4(0.35f, 0.24f, 0.12f, 0.50f)},
        {ImGuiCol_Tab, ImVec4(0.35f, 0.24f, 0.12f, 0.15f)},
        {ImGuiCol_TabHovered, ImVec4(0.35f, 0.24f, 0.12f, 0.35f)},
        {ImGuiCol_TabSelected, ImVec4(0.35f, 0.24f, 0.12f, 0.30f)},
        {ImGuiCol_TabSelectedOverline, ImVec4(0.55f, 0.15f, 0.07f, 1.0f)},
        {ImGuiCol_Separator, ImVec4(0.35f, 0.24f, 0.12f, 0.45f)},
        {ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0)},
        {ImGuiCol_ScrollbarGrab, ImVec4(0.35f, 0.24f, 0.12f, 0.40f)},
        {ImGuiCol_ScrollbarGrabHovered, ImVec4(0.35f, 0.24f, 0.12f, 0.60f)},
        {ImGuiCol_ScrollbarGrabActive, ImVec4(0.35f, 0.24f, 0.12f, 0.80f)},
        {ImGuiCol_PopupBg, ImVec4(0.93f, 0.87f, 0.74f, 0.97f)},
        {ImGuiCol_PlotHistogram, ImVec4(0.50f, 0.12f, 0.06f, 0.85f)},
        {ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0)},
    };
    for (const auto& c : colors)
        ImGui::PushStyleColor(c.id, c.color);
    return static_cast<int>(sizeof(colors) / sizeof(colors[0]));
}

bool LanguageButton(bool russian, float rightInset)
{
    const char* code = russian ? "RU" : "EN";
    float h = ImGui::GetFrameHeight();
    float icon = h * 0.62f;
    ImGuiStyle& style = ImGui::GetStyle();
    float w = style.FramePadding.x * 2.0f + icon + style.ItemInnerSpacing.x + ImGui::CalcTextSize(code).x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w - rightInset);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##language", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip("%s: English / Русский", russian ? "Язык" : "Language");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive
                                  : hovered             ? ImGuiCol_ButtonHovered
                                                        : ImGuiCol_Button);
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg, style.FrameRounding);

    // Globe: circle, meridian ellipse, vertical axis and two parallels.
    ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);
    float r = icon * 0.5f, line = h > 18.0f ? h / 18.0f : 1.0f;
    ImVec2 c(pos.x + style.FramePadding.x + r, pos.y + h * 0.5f);
    dl->AddCircle(c, r, fg, 0, line);
    dl->AddEllipse(c, ImVec2(r * 0.45f, r), fg, 0.0f, 0, line);
    dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), fg, line);
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), fg, line);
    float py = r * 0.5f, px = r * 0.866f; // parallels at +-30 degrees
    dl->AddLine(ImVec2(c.x - px, c.y - py), ImVec2(c.x + px, c.y - py), fg, line);
    dl->AddLine(ImVec2(c.x - px, c.y + py), ImVec2(c.x + px, c.y + py), fg, line);

    dl->AddText(ImVec2(c.x + r + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), fg, code);
    return clicked;
}
