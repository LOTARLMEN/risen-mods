#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static char g_logPath[MAX_PATH];
static char g_iniPath[MAX_PATH];
static CRITICAL_SECTION g_logLock;
static FILETIME g_iniWriteTime;

void ModInitAt(const char* basePath)
{
    InitializeCriticalSection(&g_logLock);
    snprintf(g_logPath, MAX_PATH, "%s.log", basePath);
    snprintf(g_iniPath, MAX_PATH, "%s.ini", basePath);
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "w") == 0 && f) // fresh log every launch
        fclose(f);
}

void ModInit(HINSTANCE instance, const char* name)
{
    char base[MAX_PATH];
    GetModuleFileNameA(instance, base, MAX_PATH);
    char* slash = strrchr(base, '\\');
    char* dst = slash ? slash + 1 : base;
    strcpy_s(dst, MAX_PATH - (dst - base), name);
    ModInitAt(base);
}

void Log(const char* fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_logLock);
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") == 0 && f) {
        fprintf(f, "%02d:%02d:%02d.%03d  %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
        fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
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

static int ParseKeyName(const char* raw)
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
    static const struct { const char* name; int vk; } table[] = {
        {"SPACE", VK_SPACE}, {"TAB", VK_TAB}, {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
        {"HOME", VK_HOME}, {"END", VK_END}, {"PAGEUP", VK_PRIOR}, {"PAGEDOWN", VK_NEXT},
        {"BACKSPACE", VK_BACK}, {"CAPSLOCK", VK_CAPITAL}, {"ENTER", VK_RETURN},
        {"MOUSE3", VK_MBUTTON}, {"MMB", VK_MBUTTON}, {"MOUSE4", VK_XBUTTON1}, {"MOUSE5", VK_XBUTTON2},
    };
    for (auto& k : table)
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
    if (int vk = ParseKeyName(buf))
        return vk;
    Log("config: unknown key name '%s' for %s, using 0x%02X", buf, key, def);
    return def;
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
