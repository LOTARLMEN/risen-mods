// Settings window opened in game with the panel key (F10). Game time is frozen (SpeedFreeze) and the game gets
// no input while it is open (shared/input_capture). Every change is applied at once and saved to speed.ini.
// English and Russian texts; the globe button in the header switches between them.

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "common.h"
#include "imgui.h"
#include "input_capture.h"
#include "speed.h"

namespace {

std::atomic<bool> g_open;
std::atomic<int> g_waitingFor; // field being rebound: 0 none, 1 speed toggle key, 2 panel key
bool g_keyDown[256];           // keyboard state while waiting, to react to new presses only
HWND g_hwnd;

struct Texts {
    const char* title;
    const char* enabled;
    const char* speed;
    const char* toggleKey;
    const char* pressKey;
    const char* showIndicator;
    const char* indicatorSize;
    const char* panelKey;
    const char* frozenNote;
    const char* close;
    const char* languageTip;
};

const Texts kEnglish = {
    "Risen Mods - Speed",
    "Speed-up enabled",
    "Speed",
    "Toggle key",
    "Press a key... (Esc: cancel)",
    "Show hourglass while sped up",
    "Hourglass size",
    "This window",
    "The game is paused while this window is open.",
    "Close",
    "Language",
};

const Texts kRussian = {
    "Risen Mods - Ускорение",
    "Ускорение включено",
    "Скорость",
    "Клавиша",
    "Нажмите клавишу... (Esc - отмена)",
    "Песочные часы при ускорении",
    "Размер часов",
    "Это окно",
    "Пока окно открыто, игра стоит на паузе.",
    "Закрыть",
    "Язык",
};

void StartWaiting(int field)
{
    for (int vk = 0; vk < 256; ++vk)
        g_keyDown[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    g_waitingFor = field;
}

// Next key pressed while waiting, VK_ESCAPE to cancel, 0 while nothing new is pressed.
int PollNewKey()
{
    int found = 0;
    for (int vk = 1; vk < 256; ++vk) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool pressed = down && !g_keyDown[vk];
        g_keyDown[vk] = down;
        // Not bindable: left/right mouse (used to click the panel), generic Shift/Ctrl/Alt (left/right are
        // reported separately), Windows keys.
        bool skip = vk == VK_LBUTTON || vk == VK_RBUTTON || (vk >= VK_SHIFT && vk <= VK_MENU) || vk == VK_LWIN ||
                    vk == VK_RWIN || vk == VK_APPS;
        if (pressed && !skip && !found)
            found = vk;
    }
    return found;
}

float LabelColumn()
{
    return ImGui::GetFontSize() * 10.0f;
}

float FieldWidth()
{
    return ImGui::GetFontSize() * 13.0f;
}

void KeyButton(const Texts& t, const char* label, const char* id, int field, int vk)
{
    char name[32], text[96];
    snprintf(text, sizeof(text), "%s###%s", g_waitingFor == field ? t.pressKey : KeyName(vk, name, sizeof(name)), id);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(LabelColumn());
    if (ImGui::Button(text, ImVec2(FieldWidth(), 0)) && !g_waitingFor)
        StartWaiting(field);
}

// Globe icon + "RU"/"EN", right-aligned on the current line. Returns true when clicked.
bool LanguageButton(const Texts& t, bool russian)
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
        ImGui::SetTooltip("%s: English / Русский", t.languageTip);

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

bool PanelIsOpen() { return g_open; }
bool PanelIsWaitingKey() { return g_waitingFor != 0; }

void PanelSetOpen(HWND hwnd, bool open)
{
    if (open == g_open || (open && !OverlayReady()))
        return;
    g_waitingFor = 0;
    g_hwnd = hwnd;
    g_open = open;
    InputCaptureSet(hwnd, open);
    SpeedFreeze(open);
    Log("panel: %s", open ? "open" : "closed");
}

void PanelDraw(float scale)
{
    SpeedConfig c = SpeedGetConfig();
    const Texts& t = c.russian ? kRussian : kEnglish;
    bool changed = false;

    if (int field = g_waitingFor) {
        if (int vk = PollNewKey()) {
            g_waitingFor = 0;
            if (vk != VK_ESCAPE) {
                (field == 1 ? c.toggleKey : c.panelKey) = vk;
                changed = true;
            }
        }
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    // Wide enough for label + field rows, so the language button sits at the right edge.
    float minWidth = LabelColumn() + FieldWidth() + 36.0f * scale;
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, 0), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scale, 14.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * scale, 10.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * scale, 5.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14.0f * scale);
    bool keepOpen = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##speed_panel", nullptr, flags)) {
        // Own header: title on the left, language switch on the right.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(t.title);
        if (LanguageButton(t, c.russian)) {
            c.russian = !c.russian;
            changed = true;
        }
        ImGui::Separator();

        changed |= ImGui::Checkbox(t.enabled, &c.enabled);

        ImGui::TextUnformatted(t.speed);
        ImGui::SameLine(LabelColumn());
        ImGui::SetNextItemWidth(FieldWidth());
        ImGui::SliderFloat("##multiplier", &c.multiplier, 1.5f, 10.0f, "x%.1f");
        if (ImGui::IsItemEdited())
            c.multiplier = static_cast<int>(c.multiplier * 10.0f + 0.5f) / 10.0f; // 0.1 steps
        changed |= ImGui::IsItemDeactivatedAfterEdit(); // save once the slider is released

        KeyButton(t, t.toggleKey, "toggle", 1, c.toggleKey);

        ImGui::Separator();
        changed |= ImGui::Checkbox(t.showIndicator, &c.indicator);
        ImGui::TextUnformatted(t.indicatorSize);
        ImGui::SameLine(LabelColumn());
        ImGui::SetNextItemWidth(FieldWidth());
        ImGui::SliderFloat("##scale", &c.indicatorScale, 0.5f, 2.5f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::Separator();
        KeyButton(t, t.panelKey, "panel", 2, c.panelKey);
        ImGui::TextDisabled("%s", t.frozenNote);
        if (ImGui::Button(t.close, ImVec2(-FLT_MIN, 0)))
            keepOpen = false;
    }
    ImGui::End();
    ImGui::PopStyleVar(6);

    // Slider drags apply live without writing the ini every frame.
    const SpeedConfig& old = SpeedGetConfig();
    if (changed)
        SpeedSetConfig(c);
    else if (c.multiplier != old.multiplier || c.indicatorScale != old.indicatorScale) {
        SpeedConfig live = old;
        live.multiplier = c.multiplier;
        live.indicatorScale = c.indicatorScale;
        SpeedApplyLive(live);
    }
    if (!keepOpen)
        PanelSetOpen(g_hwnd, false);
}
