#pragma once

// Reads the game's own GUI art at runtime (nothing of the game is shipped with the mod):
//   data\compiled\images.pak  GUI_TexturePage01_REDESIGN._ximg  - atlas with the mouse cursor and parchment frames
//   data\common\gui2.pak      QUORUMM.TTF, FreeSans.ttf          - the fonts of the game's interface
// .pak = Risen's "G3V0" archive: header with the directory offset at +32; directory entries are
// u32 nameLength, name, 0, u64 offset, 3 x u64 file times, u32 attributes, u32 encryption, u32 compression,
// u32 size, u32 packedSize. Only uncompressed entries are supported (all of the above are).

#include <d3d9.h>
#include <vector>

// pak is relative to the game folder (the parent of bin\). False when missing / compressed / not found.
bool GamePakRead(const char* pak, const char* name, std::vector<unsigned char>* out);

// ._ximg with an uncompressed 32-bit DDS inside -> managed texture (survives device Reset). nullptr on failure.
IDirect3DTexture9* GameLoadTexture(IDirect3DDevice9* device, const char* pak, const char* name);

// Pixel rectangles in GUI_TexturePage01_REDESIGN (1024 x 1024).
struct AtlasRect {
    float x0, y0, x1, y1;
};
constexpr float kAtlasSize = 1024.0f;
constexpr AtlasRect kAtlasCursor = {39, 403, 72, 434};  // the game's arrow, hot spot at the top-left tip
constexpr AtlasRect kAtlasFrame = {661, 0, 1021, 250};  // parchment with a metal bar on top and burnt edges
constexpr float kFrameBorder = 30.0f;                    // 9-slice margin of kAtlasFrame
