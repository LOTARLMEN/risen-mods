#include "game_assets.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "common.h"

namespace {

std::wstring g_root; // empty = derive from the exe

std::wstring GamePath(const char* relative)
{
    if (g_root.empty()) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH); // Risen\bin\Risen.exe
        std::wstring dir = exe;
        for (int i = 0; i < 2; ++i)
            dir.resize(dir.find_last_of(L'\\') == std::wstring::npos ? 0 : dir.find_last_of(L'\\'));
        g_root = dir;
    }
    std::wstring path = g_root + L'\\';
    for (const char* p = relative; *p; ++p)
        path += static_cast<wchar_t>(*p); // relative paths are ASCII
    return path;
}

} // namespace

void GameAssetsSetRoot(const wchar_t* gameFolder)
{
    g_root = gameFolder ? gameFolder : L"";
}

bool GamePakRead(const char* pak, const char* name, std::vector<unsigned char>* out)
{
    std::wstring path = GamePath(pak);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        Log("assets: can't open %ls", path.c_str());
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

bool GameLoadImage(const char* pak, const char* name, std::vector<unsigned char>* bgra, int* width, int* height)
{
    std::vector<unsigned char> data;
    if (!GamePakRead(pak, name, &data))
        return false;
    // ._ximg: own header, then a complete DDS file.
    size_t dds = 0;
    while (dds + 128 <= data.size() && memcmp(&data[dds], "DDS ", 4) != 0)
        ++dds;
    if (dds + 128 > data.size())
        return false;
    const unsigned char* h = &data[dds];
    uint32_t w, ht, bits, rMask, aMask;
    memcpy(&ht, h + 12, 4);
    memcpy(&w, h + 16, 4);
    memcpy(&bits, h + 88, 4);
    memcpy(&rMask, h + 92, 4);
    memcpy(&aMask, h + 104, 4);
    size_t pixels = dds + 128;
    if (bits != 32 || rMask != 0x00FF0000 || aMask != 0xFF000000 || !w || !ht ||
        pixels + static_cast<size_t>(w) * ht * 4 > data.size()) {
        Log("assets: %s is not a 32-bit ARGB DDS (bits %u)", name, bits);
        return false;
    }
    bgra->assign(data.begin() + pixels, data.begin() + pixels + static_cast<size_t>(w) * ht * 4);
    *width = static_cast<int>(w);
    *height = static_cast<int>(ht);
    return true;
}

IDirect3DTexture9* GameLoadTexture(IDirect3DDevice9* device, const char* pak, const char* name)
{
    std::vector<unsigned char> pixels;
    int width, height;
    if (!GameLoadImage(pak, name, &pixels, &width, &height))
        return nullptr;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lock;
    if (FAILED(tex->LockRect(0, &lock, nullptr, 0))) {
        tex->Release();
        return nullptr;
    }
    for (int y = 0; y < height; ++y) // same byte order: B G R A
        memcpy(static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch, &pixels[static_cast<size_t>(y) * width * 4],
               static_cast<size_t>(width) * 4);
    tex->UnlockRect(0);
    return tex;
}
