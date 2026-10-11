#include "self_update.h"

#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <thread>

#include "common.h"
#include "net.h"

namespace {

bool ReadAll(const std::wstring& path, std::vector<unsigned char>* out)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        return false;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out->insert(out->end(), buf, buf + n);
    fclose(f);
    return true;
}

} // namespace

std::wstring OwnExePath()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

std::string Sha256Hex(const std::vector<unsigned char>& data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    bool ok = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
              BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) &&
              BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0)) &&
              BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (hash)
        BCryptDestroyHash(hash);
    if (alg)
        BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok)
        return std::string();
    char hex[65];
    for (int i = 0; i < 32; ++i)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return hex;
}

std::string ExeVersion(const std::vector<unsigned char>& exe)
{
    // VS_FIXEDFILEINFO: signature 0xFEEF04BD, struct version 0x00010000, then FileVersionMS / LS.
    for (size_t i = 0; i + 16 <= exe.size(); i += 4) {
        DWORD sig, ver, ms, ls;
        memcpy(&sig, &exe[i], 4);
        if (sig != 0xFEEF04BD)
            continue;
        memcpy(&ver, &exe[i + 4], 4);
        if (ver != 0x00010000)
            continue;
        memcpy(&ms, &exe[i + 8], 4);
        memcpy(&ls, &exe[i + 12], 4);
        char text[48];
        if (HIWORD(ls))
            snprintf(text, sizeof(text), "%u.%u.%u", HIWORD(ms), LOWORD(ms), HIWORD(ls));
        else
            snprintf(text, sizeof(text), "%u.%u", HIWORD(ms), LOWORD(ms));
        return text;
    }
    return std::string();
}

bool FindInstallerUpdate(const ReleaseInstaller& release, std::vector<unsigned char>* exe, std::string* version,
                         std::string* error, FetchProgress* progress)
{
    if (release.source.empty())
        return false;
    std::vector<unsigned char> own;
    if (!ReadAll(OwnExePath(), &own))
        return false;
    if (!release.sha256.empty() && Sha256Hex(own) == release.sha256)
        return false; // the same exe
    bool url = release.source.compare(0, 8, L"https://") == 0;
    if (url ? !HttpGet(release.source, exe, error, &progress->done, &progress->total) : !ReadAll(release.source, exe))
        return false;
    if (!release.sha256.empty() && Sha256Hex(*exe) != release.sha256) {
        *error = "the downloaded installer is damaged (checksum)";
        return false;
    }
    std::string mine = ExeVersion(own);
    *version = ExeVersion(*exe);
    Log("self-update: running %s, release has %s", mine.c_str(), version->c_str());
    return !version->empty() && !mine.empty() && CompareVersions(*version, mine) > 0;
}

bool ApplyInstallerUpdate(const std::vector<unsigned char>& exe, std::string* error)
{
    std::wstring self = OwnExePath(), fresh = self + L".new", old = self + L".old";
    FILE* f = nullptr;
    if (_wfopen_s(&f, fresh.c_str(), L"wb") != 0 || !f) {
        *error = "can't write next to the installer (folder is read-only?)";
        return false;
    }
    bool written = fwrite(exe.data(), 1, exe.size(), f) == exe.size();
    written = fclose(f) == 0 && written;
    if (!written) {
        DeleteFileW(fresh.c_str());
        *error = "can't write the new installer";
        return false;
    }
    DeleteFileW(old.c_str());
    if (!MoveFileExW(self.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(fresh.c_str());
        *error = "can't rename the running installer";
        return false;
    }
    if (!MoveFileExW(fresh.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MoveFileExW(old.c_str(), self.c_str(), MOVEFILE_REPLACE_EXISTING); // put the old one back
        DeleteFileW(fresh.c_str());
        *error = "can't put the new installer in place";
        return false;
    }
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + self + L"\"";
    if (!CreateProcessW(self.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        *error = "updated, but couldn't start the new installer - start it again";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log("self-update: replaced and restarted");
    return true;
}

void DeleteOldInstaller()
{
    std::wstring old = OwnExePath() + L".old";
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    std::thread([old] {
        for (int i = 0; i < 50 && !DeleteFileW(old.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND; ++i)
            Sleep(100); // the previous process is still exiting
    }).detach();
}
