#pragma once

// The Risen look of the F10 menu and the installer: the game's parchment frame (game_assets.h atlas), dark ink
// colours, the game's interface fonts, the globe language button. Needs a current ImGui context.

#include "imgui.h"

// Font from the game's gui2.pak; merge = add the glyphs missing in the previous font. nullptr when not found.
ImFont* LoadGameFont(const char* name, float size, bool merge);
// The game's interface font: Quorum (Latin) + FreeSans merged in (Cyrillic and the rest), as in the game.
// Falls back to Segoe UI, then to ImGui's default font.
ImFont* LoadGameUiFont(float size);
// A font from C:\Windows\Fonts, nullptr when missing.
ImFont* LoadSystemFont(const char* file, float size);

// Stretches the game's parchment frame over the rectangle: corners keep their size (k = pixels per atlas pixel),
// edges stretch along one axis, the middle in both.
void DrawParchmentFrame(ImDrawList* dl, ImTextureID atlas, ImVec2 a, ImVec2 b, float k);

// Dark ink on parchment, leather-brown controls, dark red accents. Returns the count for PopStyleColor.
int PushParchmentColors();
// Dark red of titles on the parchment.
constexpr ImVec4 kParchmentTitle = ImVec4(0.45f, 0.10f, 0.05f, 1.0f);

// Globe icon + "RU"/"EN", right-aligned on the current line (rightInset = room left free to its right).
// Returns true when clicked.
bool LanguageButton(bool russian, float rightInset = 0.0f);
