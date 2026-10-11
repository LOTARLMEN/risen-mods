// The settings window: a tab per registered page, items drawn from their descriptions (hub_api.h). Values are
// read from the mods' ini files when the window opens; every change is written at once and the mod's onChanged
// is called. English and Russian texts; the globe button in the header switches between them.

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "common.h"
#include "imgui.h"
#include "game_assets.h"
#include "menu.h"

namespace {

std::map<const HubPage*, std::vector<float>> g_values; // ini units, one per item
const HubPage* g_waitPage;                             // item being rebound
int g_waitItem = -1;
bool g_keyDown[256]; // keyboard state while waiting, to react to new presses only

const char* Text(const char* en, const char* ru)
{
    return MenuGetConfig().russian && ru ? ru : en;
}

float ReadValue(const HubPage& page, const HubItem& item)
{
    char buf[64];
    GetPrivateProfileStringA(item.section, item.key, "", buf, sizeof(buf), page.iniPath);
    if (!buf[0])
        return item.def;
    switch (item.type) {
    case HubCheckbox:
        return atoi(buf) != 0 ? 1.0f : 0.0f;
    case HubSlider:
        return static_cast<float>(atof(buf));
    case HubKey: {
        int vk = ParseKey(buf);
        return vk ? static_cast<float>(vk) : item.def;
    }
    }
    return 0.0f;
}

std::vector<float>& Values(const HubPage* page)
{
    std::vector<float>& v = g_values[page];
    if (v.size() != static_cast<size_t>(page->itemCount)) {
        v.resize(page->itemCount);
        for (int i = 0; i < page->itemCount; ++i)
            v[i] = page->iniPath ? ReadValue(*page, page->items[i]) : page->items[i].def;
    }
    return v;
}

void WriteValue(const HubPage& page, int index, float value)
{
    const HubItem& item = page.items[index];
    char buf[32];
    if (item.type == HubKey)
        KeyName(static_cast<int>(value), buf, sizeof(buf));
    else if (item.type == HubCheckbox)
        strcpy_s(buf, value != 0.0f ? "1" : "0");
    else {
        snprintf(buf, sizeof(buf), "%.3f", value); // "300.000" -> "300", "2.500" -> "2.5"
        for (char* end = buf + strlen(buf) - 1; *end == '0' || *end == '.'; --end) {
            bool dot = *end == '.';
            *end = 0;
            if (dot)
                break;
        }
    }
    if (page.iniPath)
        WritePrivateProfileStringA(item.section, item.key, buf, page.iniPath);
    Log("set %s [%s] %s=%s", page.id, item.section, item.key, buf);
    if (page.onChanged)
        page.onChanged();
}

void StartWaiting(const HubPage* page, int index)
{
    for (int vk = 0; vk < 256; ++vk)
        g_keyDown[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    g_waitPage = page;
    g_waitItem = index;
    MenuSetWaitingKey(true);
}

void StopWaiting()
{
    g_waitPage = nullptr;
    g_waitItem = -1;
    MenuSetWaitingKey(false);
}

// Next key pressed while waiting, VK_ESCAPE to cancel, 0 while nothing new is pressed.
int PollNewKey()
{
    int found = 0;
    for (int vk = 1; vk < 256; ++vk) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool pressed = down && !g_keyDown[vk];
        g_keyDown[vk] = down;
        // Not bindable: left mouse (clicks the window), generic Shift/Ctrl/Alt (left/right are reported
        // separately), Windows keys.
        bool skip = vk == VK_LBUTTON || (vk >= VK_SHIFT && vk <= VK_MENU) || vk == VK_LWIN || vk == VK_RWIN ||
                    vk == VK_APPS;
        if (pressed && !skip && !found)
            found = vk;
    }
    return found;
}

float LabelColumn()
{
    return ImGui::GetFontSize() * 12.0f;
}

float FieldWidth()
{
    return ImGui::GetFontSize() * 12.0f;
}

void Tooltip(const HubItem& item)
{
    if (item.tipEn && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", Text(item.tipEn, item.tipRu));
}

void DrawItem(const HubPage* page, int index, std::vector<float>& values)
{
    const HubItem& item = page->items[index];
    const char* label = Text(item.labelEn, item.labelRu);
    // A parent is an item above this one: items written without the field have parent 0, which must not make
    // the first item its own parent (it would lock itself once switched off).
    bool child = item.parent >= 0 && item.parent < index;
    bool disabled = child && values[item.parent] == 0.0f;
    char id[64];
    snprintf(id, sizeof(id), "##%s_%d", page->id, index);

    if (child)
        ImGui::Indent();
    ImGui::BeginDisabled(disabled);
    switch (item.type) {
    case HubHeader:
        ImGui::Spacing();
        ImGui::SeparatorText(label);
        break;
    case HubNote:
        ImGui::PushTextWrapPos(LabelColumn() + FieldWidth());
        ImGui::TextDisabled("%s", label);
        ImGui::PopTextWrapPos();
        break;
    case HubCheckbox: {
        bool on = values[index] != 0.0f;
        char text[160];
        snprintf(text, sizeof(text), "%s%s", label, id);
        if (ImGui::Checkbox(text, &on)) {
            values[index] = on ? 1.0f : 0.0f;
            WriteValue(*page, index, values[index]);
        }
        Tooltip(item);
        break;
    }
    case HubSlider: {
        float k = item.displayScale != 0.0f ? item.displayScale : 1.0f;
        float shown = values[index] * k;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        Tooltip(item);
        ImGui::SameLine(LabelColumn());
        ImGui::SetNextItemWidth(FieldWidth());
        if (ImGui::SliderFloat(id, &shown, item.min, item.max, item.format ? item.format : "%.1f")) {
            if (item.step > 0.0f)
                shown = std::round(shown / item.step) * item.step;
            values[index] = shown / k;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) // save once the slider is released
            WriteValue(*page, index, values[index]);
        Tooltip(item);
        break;
    }
    case HubKey: {
        char name[32], text[96];
        bool waiting = g_waitPage == page && g_waitItem == index;
        snprintf(text, sizeof(text), "%s%s",
                 waiting ? Text("Press a key... (Esc: cancel)", "Нажмите клавишу... (Esc - отмена)")
                         : KeyName(static_cast<int>(values[index]), name, sizeof(name)),
                 id);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        Tooltip(item);
        ImGui::SameLine(LabelColumn());
        if (ImGui::Button(text, ImVec2(FieldWidth(), 0)) && !g_waitPage)
            StartWaiting(page, index);
        Tooltip(item);
        break;
    }
    }
    ImGui::EndDisabled();
    if (child)
        ImGui::Unindent();
}

void PollRebind()
{
    if (!g_waitPage)
        return;
    int vk = PollNewKey();
    if (!vk)
        return;
    const HubPage* page = g_waitPage;
    int index = g_waitItem;
    StopWaiting();
    if (vk == VK_ESCAPE)
        return;
    Values(page)[index] = static_cast<float>(vk);
    WriteValue(*page, index, static_cast<float>(vk));
}

// Globe icon + "RU"/"EN", right-aligned on the current line. Returns true when clicked.
bool LanguageButton(bool russian)
{
    const char* code = russian ? "RU" : "EN";
    float h = ImGui::GetFrameHeight();
    float icon = h * 0.62f;
    ImGuiStyle& style = ImGui::GetStyle();
    float w = style.FramePadding.x * 2.0f + icon + style.ItemInnerSpacing.x + ImGui::CalcTextSize(code).x;
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##language", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip("%s: English / Русский", Text("Language", "Язык"));

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

// Stretches the game's parchment frame over the rectangle: corners keep their size (k = pixels per atlas pixel),
// edges stretch along one axis, the middle in both.
void DrawFrame(ImDrawList* dl, ImTextureID atlas, ImVec2 a, ImVec2 b, float k)
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

// Dark ink on parchment, leather-brown controls, dark red accents.
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
    };
    for (const auto& c : colors)
        ImGui::PushStyleColor(c.id, c.color);
    return static_cast<int>(sizeof(colors) / sizeof(colors[0]));
}

} // namespace

void WindowOpened()
{
    g_values.clear(); // values are re-read: the ini may have been edited by hand
    StopWaiting();
}

void WindowDraw(float scale)
{
    PollRebind();
    bool russian = MenuGetConfig().russian;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImTextureID atlas = reinterpret_cast<ImTextureID>(OverlayAtlas());
    if (!atlas)
        ImGui::SetNextWindowBgAlpha(0.92f);
    // With the parchment: keep clear of the burnt edges and the metal bar on top.
    ImVec2 padding = atlas ? ImVec2(34.0f * scale, 36.0f * scale) : ImVec2(18.0f * scale, 14.0f * scale);
    // Same size on every tab; a tab with more items scrolls (mouse wheel).
    float fontSize = ImGui::GetFontSize();
    float width = LabelColumn() + FieldWidth() + fontSize * 3.0f + padding.x * 2.0f;
    float height = fontSize * 34.0f;
    if (height > io.DisplaySize.y * 0.9f)
        height = io.DisplaySize.y * 0.9f;
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    int colors = atlas ? PushParchmentColors() : 0;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, atlas ? 0.0f : 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * scale, 10.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * scale, 5.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 26.0f * scale);
    bool keepOpen = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##risen_mods_menu", nullptr, flags)) {
        if (atlas) {
            ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
            DrawFrame(ImGui::GetWindowDrawList(), atlas, pos, ImVec2(pos.x + size.x, pos.y + size.y), scale * 1.2f);
        }
        // Own header: title on the left, language switch on the right.
        ImGui::AlignTextToFramePadding();
        if (atlas)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.10f, 0.05f, 1.0f));
        ImGui::TextUnformatted("Risen Mods");
        if (atlas)
            ImGui::PopStyleColor();
        if (LanguageButton(russian))
            MenuSetLanguage(!russian);

        // Room left for the tab contents above the separator and the Close button.
        float footer = ImGui::GetStyle().ItemSpacing.y * 2.0f + ImGui::GetFrameHeight() + 1.0f;
        if (ImGui::BeginTabBar("##pages", ImGuiTabBarFlags_FittingPolicyScroll)) {
            RegistryLock();
            for (const HubPage* page : Pages()) {
                char title[96];
                snprintf(title, sizeof(title), "%s###%s", Text(page->titleEn, page->titleRu), page->id);
                if (ImGui::BeginTabItem(title)) {
                    ImGui::BeginChild("##content", ImVec2(0, -footer));
                    std::vector<float>& values = Values(page);
                    for (int i = 0; i < page->itemCount; ++i)
                        DrawItem(page, i, values);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            RegistryUnlock();
            ImGui::EndTabBar();
        }
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - footer);
        ImGui::Separator();
        if (ImGui::Button(Text("Close", "Закрыть"), ImVec2(-FLT_MIN, 0)))
            keepOpen = false;
    }
    ImGui::End();
    ImGui::PopStyleVar(8);
    ImGui::PopStyleColor(colors);
    if (!keepOpen)
        MenuSetOpen(FindGameWindow(), false);
}
