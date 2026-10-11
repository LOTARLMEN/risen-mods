#pragma once

// Reads the game's own GUI art at runtime (nothing of the game is shipped with the mods):
//   data\compiled\images.pak  GUI_TexturePage01_REDESIGN._ximg  - atlas with the mouse cursor and parchment frames
//   data\common\gui2.pak      QUORUMM.TTF, FreeSans.ttf          - the fonts of the game's interface
// .pak = Risen's "G3V0" archive: header with the directory offset at +32; directory entries are
// u32 nameLength, name, 0, u64 offset, 3 x u64 file times, u32 attributes, u32 encryption, u32 compression,
// u32 size, u32 packedSize. Only uncompressed entries are supported (all of the above are).

#include <d3d9.h>
#include <vector>

// Game folder (the one with bin\ and data\). Default: the parent of the folder of the running exe (Risen\bin).
void GameAssetsSetRoot(const wchar_t* gameFolder);

// pak is relative to the game folder. False when missing / compressed / not found.
bool GamePakRead(const char* pak, const char* name, std::vector<unsigned char>* out);

// ._ximg with an uncompressed 32-bit DDS inside -> B G R A rows, top to bottom. False on failure.
bool GameLoadImage(const char* pak, const char* name, std::vector<unsigned char>* bgra, int* width, int* height);

// Same as a managed texture (survives device Reset). nullptr on failure.
IDirect3DTexture9* GameLoadTexture(IDirect3DDevice9* device, const char* pak, const char* name);

// Pixel rectangles in GUI_TexturePage01_REDESIGN (1024 x 1024).
struct AtlasRect {
    float x0, y0, x1, y1;
};
constexpr const char* kAtlasPak = "data\\compiled\\images.pak";
constexpr const char* kAtlasName = "GUI_TexturePage01_REDESIGN._ximg";
constexpr const char* kFontPak = "data\\common\\gui2.pak";
constexpr float kAtlasSize = 1024.0f;
constexpr AtlasRect kAtlasCursor = {39, 403, 72, 434};  // the game's arrow, hot spot at the top-left tip
constexpr AtlasRect kAtlasFrame = {661, 0, 1021, 250};  // parchment with a metal bar on top and burnt edges
constexpr float kFrameBorder = 30.0f;                    // 9-slice margin of kAtlasFrame
