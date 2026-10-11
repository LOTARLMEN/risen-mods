#include "net.h"

#include <windows.h>
#include <winhttp.h>

namespace {

struct Handle {
    HINTERNET h = nullptr;
    ~Handle()
    {
        if (h)
            WinHttpCloseHandle(h);
    }
};

std::string LastError(const char* what)
{
    char buf[96];
    wsprintfA(buf, "%s failed (error %lu)", what, GetLastError());
    return buf;
}

} // namespace

bool HttpGet(const std::wstring& url, std::vector<unsigned char>* out, std::string* error,
             std::atomic<long long>* done, std::atomic<long long>* total)
{
    URL_COMPONENTS parts = {sizeof(parts)};
    wchar_t host[256], path[2048];
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        *error = "bad URL";
        return false;
    }
    Handle session, connect, request;
    session.h = WinHttpOpen(L"RisenModsInstaller", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) // AUTOMATIC_PROXY needs Windows 8.1+
        session.h = WinHttpOpen(L"RisenModsInstaller", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        *error = LastError("WinHttpOpen");
        return false;
    }
    WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000);
    connect.h = WinHttpConnect(session.h, host, parts.nPort, 0);
    if (!connect.h) {
        *error = LastError("connect");
        return false;
    }
    request.h = WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request.h) {
        *error = LastError("request");
        return false;
    }
    const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
    if (!WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        *error = LastError("download");
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        char buf[64];
        wsprintfA(buf, "server answered HTTP %lu", status);
        *error = buf;
        return false;
    }
    DWORD length = 0;
    size = sizeof(length);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX)) {
        out->reserve(length);
        if (total)
            *total += length;
    }
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) {
            *error = LastError("download");
            return false;
        }
        if (available == 0)
            return true;
        size_t at = out->size();
        out->resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, out->data() + at, available, &read)) {
            *error = LastError("download");
            return false;
        }
        out->resize(at + read);
        if (done)
            *done += read;
    }
}
