#include "game_assets.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.h"

namespace {

bool GamePath(const char* relative, char* out, size_t size)
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH); // Risen\bin\Risen.exe
    char* slash = strrchr(exe, '\\');
    if (!slash)
        return false;
    *slash = 0;
    if ((slash = strrchr(exe, '\\')) != nullptr)
        *slash = 0;
    return snprintf(out, size, "%s\\%s", exe, relative) < static_cast<int>(size);
}

} // namespace

bool GamePakRead(const char* pak, const char* name, std::vector<unsigned char>* out)
{
    char path[MAX_PATH];
    if (!GamePath(pak, path, sizeof(path)))
        return false;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) {
        Log("assets: can't open %s", path);
        return false;
    }
    bool ok = false;
    uint8_t header[48];
    uint64_t dirOffset = 0;
    if (fread(header, 1, sizeof(header), f) == sizeof(header) && memcmp(header + 4, "G3V0", 4) == 0) {
        memcpy(&dirOffset, header + 32, 8);
        _fseeki64(f, 0, SEEK_END);
        int64_t fileSize = _ftelli64(f);
        std::vector<uint8_t> dir;
        if (dirOffset > 0 && static_cast<int64_t>(dirOffset) < fileSize) {
            dir.resize(static_cast<size_t>(fileSize - dirOffset));
            _fseeki64(f, static_cast<int64_t>(dirOffset), SEEK_SET);
            dir.resize(fread(dir.data(), 1, dir.size(), f));
        }
        // Find "<u32 length><name>\0" and read the fields after it.
        uint32_t len = static_cast<uint32_t>(strlen(name));
        const size_t fields = 8 + 24 + 20;
        for (size_t i = 0; i + 4 + len + 1 + fields <= dir.size(); ++i) {
            if (memcmp(&dir[i], &len, 4) != 0 || memcmp(&dir[i + 4], name, len) != 0 || dir[i + 4 + len] != 0)
                continue;
            const uint8_t* p = &dir[i + 4 + len + 1];
            uint64_t offset;
            uint32_t encryption, compression, size;
            memcpy(&offset, p, 8);
            memcpy(&encryption, p + 36, 4);
            memcpy(&compression, p + 40, 4);
            memcpy(&size, p + 44, 4);
            if (encryption || compression) {
                Log("assets: %s is packed (%u/%u), not supported", name, encryption, compression);
                break;
            }
            out->resize(size);
            _fseeki64(f, static_cast<int64_t>(offset), SEEK_SET);
            ok = fread(out->data(), 1, size, f) == size;
            break;
        }
    }
    fclose(f);
    Log("assets: %s from %s: %s", name, pak, ok ? "ok" : "NOT FOUND");
    return ok;
}

IDirect3DTexture9* GameLoadTexture(IDirect3DDevice9* device, const char* pak, const char* name)
{
    std::vector<unsigned char> data;
    if (!GamePakRead(pak, name, &data))
        return nullptr;
    // ._ximg: own header, then a complete DDS file.
    size_t dds = 0;
    while (dds + 128 <= data.size() && memcmp(&data[dds], "DDS ", 4) != 0)
        ++dds;
    if (dds + 128 > data.size())
        return nullptr;
    const unsigned char* h = &data[dds];
    uint32_t height, width, flags, bits, rMask, aMask;
    memcpy(&height, h + 12, 4);
    memcpy(&width, h + 16, 4);
    memcpy(&flags, h + 80, 4);
    memcpy(&bits, h + 88, 4);
    memcpy(&rMask, h + 92, 4);
    memcpy(&aMask, h + 104, 4);
    size_t pixels = dds + 128;
    if (bits != 32 || rMask != 0x00FF0000 || aMask != 0xFF000000 || !width || !height ||
        pixels + static_cast<size_t>(width) * height * 4 > data.size()) {
        Log("assets: %s is not a 32-bit ARGB DDS (bits %u)", name, bits);
        return nullptr;
    }
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lock;
    if (FAILED(tex->LockRect(0, &lock, nullptr, 0))) {
        tex->Release();
        return nullptr;
    }
    for (uint32_t y = 0; y < height; ++y) // same byte order: B G R A
        memcpy(static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch, &data[pixels + y * width * 4], width * 4);
    tex->UnlockRect(0);
    return tex;
}
