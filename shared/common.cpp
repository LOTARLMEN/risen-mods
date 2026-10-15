#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static char g_logPath[MAX_PATH];
static char g_logTag[32];
static char g_iniPath[MAX_PATH];
static FILETIME g_iniWriteTime;
static volatile LONG g_logFullNoted;

static const char kSharedLog[] = "RisenMods.log";
static const char kSharedLogPrev[] = "RisenMods.prev.log";

void ModInitAt(const char* basePath)
{
    snprintf(g_logPath, MAX_PATH, "%s.log", basePath);
    snprintf(g_iniPath, MAX_PATH, "%s.ini", basePath);
    g_logTag[0] = 0;
    DeleteFileA(g_logPath); // fresh log every run
}

void ModInit(HINSTANCE instance, const char* name)
{
    ModInitIn(instance, name, nullptr);
}

void ModInitIn(HINSTANCE instance, const char* name, const char* relDir)
{
    char dir[MAX_PATH];
    GetModuleFileNameA(instance, dir, MAX_PATH);
    if (char* slash = strrchr(dir, '\\'))
        *slash = 0;
    if (relDir) {
        size_t len = strlen(dir);
        snprintf(dir + len, MAX_PATH - len, "\\%s", relDir);
    }
    snprintf(g_iniPath, MAX_PATH, "%s\\%s.ini", dir, name);
    snprintf(g_logPath, MAX_PATH, "%s\\%s", dir, kSharedLog);
    snprintf(g_logTag, sizeof(g_logTag), "%s", name);
}

void LogStartSession(const char* modsDir)
{
    char prev[MAX_PATH], path[MAX_PATH];
    snprintf(g_logPath, MAX_PATH, "%s\\%s", modsDir, kSharedLog);
    snprintf(prev, MAX_PATH, "%s\\%s", modsDir, kSharedLogPrev);
    snprintf(g_iniPath, MAX_PATH, "%s\\loader.ini", modsDir);
    snprintf(g_logTag, sizeof(g_logTag), "loader");
    if (!MoveFileExA(g_logPath, prev, MOVEFILE_REPLACE_EXISTING))
        DeleteFileA(g_logPath);
    // Per-mod logs of older versions would only confuse whoever looks for the log.
    WIN32_FIND_DATAA fd;
    snprintf(path, MAX_PATH, "%s\\*.log", modsDir);
    HANDLE find = FindFirstFileA(path, &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (_strnicmp(fd.cFileName, "RisenMods", 9) != 0) {
                snprintf(path, MAX_PATH, "%s\\%s", modsDir, fd.cFileName);
                DeleteFileA(path);
            }
        } while (FindNextFileA(find, &fd));
        FindClose(find);
    }
}

const char* LogPath()
{
    return g_logPath;
}

// One WriteFile per line on a FILE_APPEND_DATA handle: lines from several DLLs and threads don't interleave.
static void LogLine(const char* fmt, va_list args, bool always)
{
    if (!g_logPath[0])
        return;
    char line[1200];
    const int room = static_cast<int>(sizeof(line)) - 2; // CR LF
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = g_logTag[0] ? snprintf(line, room, "%02d:%02d:%02d.%03d [%s] ", t.wHour, t.wMinute, t.wSecond,
                                   t.wMilliseconds, g_logTag)
                        : snprintf(line, room, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    int m = vsnprintf(line + n, room - n, fmt, args);
    n = m < 0 ? n : (n + m < room ? n + m : room - 1); // cut long lines
    line[n++] = '\r';
    line[n++] = '\n';
    HANDLE f = CreateFileA(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    DWORD written;
    if (always || size.QuadPart < kLogMaxBytes) {
        WriteFile(f, line, n, &written, nullptr);
    } else if (InterlockedExchange(&g_logFullNoted, 1) == 0) {
        static const char kFull[] = "... log full: the rest of this launch is not logged (crashes still are)\r\n";
        WriteFile(f, kFull, sizeof(kFull) - 1, &written, nullptr);
    }
    CloseHandle(f);
}

void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogLine(fmt, args, false);
    va_end(args);
}

void LogAlways(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogLine(fmt, args, true);
    va_end(args);
}

// ---------------------------------------------------------------------------------------------
// Ini

static bool IniWriteTime(FILETIME* out)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &data))
        return false;
    *out = data.ftLastWriteTime;
    return true;
}

const char* IniPath(const char* defaultText)
{
    if (defaultText && GetFileAttributesA(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        FILE* f = nullptr;
        if (fopen_s(&f, g_iniPath, "wb") == 0 && f) {
            fwrite(defaultText, 1, strlen(defaultText), f);
            fclose(f);
            Log("config: created default %s", strrchr(g_iniPath, '\\') + 1);
        }
    }
    IniWriteTime(&g_iniWriteTime);
    return g_iniPath;
}

int IniInt(const char* section, const char* key, int def)
{
    return GetPrivateProfileIntA(section, key, def, g_iniPath);
}

float IniFloat(const char* section, const char* key, float def)
{
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), g_iniPath);
    return buf[0] ? static_cast<float>(atof(buf)) : def;
}

void IniString(const char* section, const char* key, const char* def, char* buf, int size)
{
    GetPrivateProfileStringA(section, key, def, buf, size, g_iniPath);
}

static const struct { const char* name; int vk; } kKeyNames[] = {
    {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
    {"HOME", VK_HOME}, {"END", VK_END}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
    {"BACKSPACE", VK_BACK}, {"CAPSLOCK", VK_CAPITAL}, {"ENTER", VK_RETURN},
    {"MOUSE2", VK_RBUTTON}, {"RMB", VK_RBUTTON}, {"MOUSE3", VK_MBUTTON}, {"MMB", VK_MBUTTON},
    {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2},
    {"NUMPAD+", VK_ADD}, {"NUMPAD-", VK_SUBTRACT}, {"NUMPAD*", VK_MULTIPLY}, {"NUMPAD/", VK_DIVIDE},
};

int ParseKey(const char* raw)
{
    char name[32];
    size_t n = 0;
    for (const char* p = raw; *p && n < sizeof(name) - 1; ++p)
        if (*p != ' ' && *p != '\t')
            name[n++] = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
    name[n] = 0;
    if (!n)
        return 0;
    if (name[0] == '0' && name[1] == 'X')
        return static_cast<int>(strtol(name + 2, nullptr, 16));
    if (n == 1 && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')))
        return name[0];
    if (name[0] == 'F' && n <= 3) {
        int f = atoi(name + 1);
        if (f >= 1 && f <= 24)
            return VK_F1 + f - 1;
    }
    if (strncmp(name, "NUMPAD", 6) == 0 && n == 7) {
        char c = name[6];
        if (c >= '0' && c <= '9') return VK_NUMPAD0 + (c - '0');
        if (c == '+') return VK_ADD;
        if (c == '-') return VK_SUBTRACT;
        if (c == '*') return VK_MULTIPLY;
        if (c == '/') return VK_DIVIDE;
    }
    for (auto& k : kKeyNames)
        if (strcmp(name, k.name) == 0)
            return k.vk;
    return 0;
}

int IniKey(const char* section, const char* key, int def)
{
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), g_iniPath);
    if (!buf[0])
        return def;
    if (int vk = ParseKey(buf))
        return vk;
    Log("config: unknown key name '%s' for %s, using 0x%02X", buf, key, def);
    return def;
}

const char* KeyName(int vk, char* buf, size_t size)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        snprintf(buf, size, "%c", vk);
    else if (vk >= VK_F1 && vk <= VK_F24)
        snprintf(buf, size, "F%d", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        snprintf(buf, size, "NUMPAD%d", vk - VK_NUMPAD0);
    else {
        snprintf(buf, size, "0x%02X", vk);
        for (auto& k : kKeyNames)
            if (k.vk == vk) {
                strcpy_s(buf, size, k.name);
                break;
            }
    }
    return buf;
}

void IniWriteString(const char* section, const char* key, const char* value)
{
    WritePrivateProfileStringA(section, key, value, g_iniPath);
}

void IniWriteFloat(const char* section, const char* key, float value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    IniWriteString(section, key, buf);
}

void IniWriteKey(const char* section, const char* key, int vk)
{
    char buf[32];
    IniWriteString(section, key, KeyName(vk, buf, sizeof(buf)));
}

bool IniChanged()
{
    FILETIME now;
    if (!IniWriteTime(&now) || CompareFileTime(&now, &g_iniWriteTime) == 0)
        return false;
    g_iniWriteTime = now;
    Sleep(50); // let the editor finish writing
    return true;
}

// ---------------------------------------------------------------------------------------------
// Game window

static BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM result)
{
    // Title is "RISEN, V1.0.0 (Rev. 2045) 1920x1080" (prefix from <Window Name="RISEN"> in ConfigDefault.xml).
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd))
        return TRUE;
    // InternalGetWindowText does not send WM_GETTEXT, so it can't block on the busy game thread.
    wchar_t title[64];
    InternalGetWindowText(hwnd, title, 64);
    if (_wcsnicmp(title, L"RISEN", 5) != 0)
        return TRUE;
    *reinterpret_cast<HWND*>(result) = hwnd;
    return FALSE;
}

HWND FindGameWindow()
{
    HWND hwnd = nullptr;
    EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&hwnd));
    return hwnd;
}

HWND WaitForGameWindow()
{
    HWND hwnd = nullptr;
    for (int attempt = 1; !(hwnd = FindGameWindow()); ++attempt) {
        if (attempt % 150 == 0)
            Log("window: still searching (%d s)", attempt / 5);
        Sleep(200);
    }
    Log("window: found %p", hwnd);
    return hwnd;
}

WNDPROC SubclassWindow(HWND hwnd, WNDPROC proc)
{
    LONG_PTR p = reinterpret_cast<LONG_PTR>(proc);
    return reinterpret_cast<WNDPROC>(IsWindowUnicode(hwnd) ? SetWindowLongPtrW(hwnd, GWLP_WNDPROC, p)
                                                           : SetWindowLongPtrA(hwnd, GWLP_WNDPROC, p));
}

LRESULT CallOriginalWndProc(WNDPROC orig, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return IsWindowUnicode(hwnd) ? CallWindowProcW(orig, hwnd, msg, wp, lp) : CallWindowProcA(orig, hwnd, msg, wp, lp);
}
