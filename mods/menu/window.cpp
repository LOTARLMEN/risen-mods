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
    bool child = item.parent >= 0 && item.parent < page->itemCount;
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
    ImGui::SetNextWindowBgAlpha(0.92f);
    // Same size on every tab; a tab with more items scrolls (mouse wheel).
    float fontSize = ImGui::GetFontSize();
    float width = LabelColumn() + FieldWidth() + fontSize * 3.0f + 36.0f * scale;
    float height = fontSize * 34.0f;
    if (height > io.DisplaySize.y * 0.9f)
        height = io.DisplaySize.y * 0.9f;
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale, 14.0f * scale));
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
        // Own header: title on the left, language switch on the right.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Risen Mods");
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
    ImGui::PopStyleVar(7);
    if (!keepOpen)
        MenuSetOpen(FindGameWindow(), false);
}
