// No Limits: strength and dexterity go past the game's maximum of 200 (MaxValue, 1000 by default) and teachers
// train them up to that maximum instead of their personal limit. Other skills keep the teachers' limits. Learning
// point costs are unchanged (each teach option has a fixed cost).
//
// The maximum: gCSkills_PS::ms_arrSkillInfos (Game.dll, 0x80 bytes per skill: min, max, default, ...) is filled by
// a static initializer (strength / dexterity: 0, 200, 100). Every clamp of the game (GetTotal, GetSkillInfo,
// GetSkillMaxValue, ...) reads max from there, so the mod writes its MaxValue into it and 200 back when switched off.
// A save with a value above 200 loaded without the mod shows 200 (the stored base value isn't touched).
//
// How: gCInfoConditionCanTeachSkill::IsSatisfied (Game.dll) decides whether a teach option is shown. For every
// skill range of the teacher it checks  teacher.min <= player's value + amount <= teacher.max.  We patch the end of
// that loop body in memory to compare against the global maximum (gCSkills_PS::GetSkillMaxValue, already in eax
// at that point) instead of teacher.max for strength and dexterity. Nothing is written to saves: without the mod the game is as before.
// Only for the Steam build Rev. 2045: the patched bytes are checked first, otherwise the mod stays off.
// Settings: mods\no_limits.ini. The patch is switched on the game thread (posted window message).

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "common.h"
#include "hub_client.h"

static const char kDefaultIni[] =
    "; No Limits mod settings. Changes are picked up while the game is running.\r\n"
    "\r\n"
    "[NoLimits]\r\n"
    "; Strength and dexterity go up to MaxValue instead of 200; teachers train them that far instead of\r\n"
    "; their own limit. Other skills are unchanged, so are the learning point costs.\r\n"
    "Enabled=1\r\n"
    "; Maximum of strength and dexterity (the game's own is 200)\r\n"
    "MaxValue=1000\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; Writes the skill table and the player's skills to no_limits.log\r\n"
    "DumpKey=\r\n";

static WNDPROC g_origProc;
static bool g_enabled = true;
static int g_maxValue = 1000;
static int g_dumpKey;
constexpr UINT WM_NOLIMITS_DUMP = WM_APP + 0x5501;
constexpr UINT WM_NOLIMITS_APPLY = WM_APP + 0x5502;

// ---------------------------------------------------------------------------------------------
// The patch

constexpr uintptr_t kIsSatisfiedRva = 0x8a1630; // gCInfoConditionCanTeachSkill::IsSatisfied
constexpr uintptr_t kSiteRva = 0x8a16fa;        // right after the call to GetSkillMaxValue
constexpr uintptr_t kNextRva = 0x8a1736;        // next teacher range
constexpr uintptr_t kSuccessRva = 0x8a1762;     // mov al, 1

// Original bytes from kSiteRva up to kNextRva: value = base + modifier, clamped to [skill min, global max];
// value + amount checked against the teacher's range [min, max].
static const uint8_t kSiteOriginal[] = {
    0x41, 0x8B, 0x8C, 0xAF, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD5, 0x41, 0x03, 0x4C, 0xAF, 0x20, 0x85, 0xC0,
    0x7E, 0x12, 0x3B, 0xC1, 0x0F, 0x4C, 0xC8, 0x48, 0xC1, 0xE2, 0x07, 0x42, 0x39, 0x0C, 0x22, 0x42, 0x0F, 0x4F,
    0x0C, 0x22, 0x48, 0x8B, 0x46, 0x30, 0x41, 0x8B, 0x56, 0x28, 0x03, 0xD1, 0x3B, 0x54, 0x18, 0x18, 0x7C, 0x06,
    0x3B, 0x54, 0x18, 0x1C, 0x7E, 0x2C};

// The same, but for strength (7) and dexterity (8) the upper bound is the global maximum; every other skill
// keeps the teacher's range. Built with ml64 from:
//   mov ecx,[r15+rbp*4+0A8h] / add ecx,[r15+rbp*4+20h] / test eax,eax / jle noclamp
//   cmp eax,ecx / cmovl ecx,eax / mov rdx,rbp / shl rdx,7 / cmp [rdx+r12],ecx / cmovg ecx,[rdx+r12]
//   noclamp: mov r10,[rsi+30h] / mov edx,[r14+28h] / add edx,ecx / cmp edx,[r10+rbx+18h] / jl next
//   test eax,eax / jle teachermax / lea ecx,[rbp-7] / cmp ecx,1 / ja teachermax / cmp edx,eax / jle success
//   jmp next
//   teachermax: cmp edx,[r10+rbx+1Ch] / jle success
//   next: jmp [addrNext]   success: jmp [addrSuccess]   addrNext dq ?   addrSuccess dq ?
static const uint8_t kCave[] = {
    0x41, 0x8B, 0x8C, 0xAF, 0xA8, 0x00, 0x00, 0x00, 0x41, 0x03, 0x4C, 0xAF, 0x20, 0x85, 0xC0, 0x7E, 0x15, 0x3B,
    0xC1, 0x0F, 0x4C, 0xC8, 0x48, 0x8B, 0xD5, 0x48, 0xC1, 0xE2, 0x07, 0x42, 0x39, 0x0C, 0x22, 0x42, 0x0F, 0x4F,
    0x0C, 0x22, 0x4C, 0x8B, 0x56, 0x30, 0x41, 0x8B, 0x56, 0x28, 0x03, 0xD1, 0x41, 0x3B, 0x54, 0x1A, 0x18, 0x7C,
    0x19, 0x85, 0xC0, 0x7E, 0x0E, 0x8D, 0x4D, 0xF9, 0x83, 0xF9, 0x01, 0x77, 0x06, 0x3B, 0xD0, 0x7E, 0x0F, 0xEB,
    0x07, 0x41, 0x3B, 0x54, 0x1A, 0x1C, 0x7E, 0x06,
    0xFF, 0x25, 0x06, 0x00, 0x00, 0x00, // jmp [addrNext]
    0xFF, 0x25, 0x08, 0x00, 0x00, 0x00, // jmp [addrSuccess]
};
constexpr size_t kJumpSize = 14; // jmp [rip+0] + 8-byte address

constexpr int kSkills[] = {7, 8}; // strength, dexterity
constexpr int kGameMax = 200;
static int32_t* g_skillInfos; // gCSkills_PS::ms_arrSkillInfos, 32 ints per skill, [1] = max
static int g_writtenMax;      // what the table holds now (kGameMax = as in the game)

static uint8_t* g_site;
static uint8_t* g_cave;
static bool g_patched;
static int g_state; // 0 not tried, 1 ready, -1 wrong game version

static bool Prepare()
{
    if (g_state)
        return g_state > 0;
    HMODULE game = GetModuleHandleA("Game.dll");
    if (!game)
        return false;
    auto base = reinterpret_cast<uint8_t*>(game);
    auto isSatisfied = reinterpret_cast<uint8_t*>(
        GetProcAddress(game, "?IsSatisfied@gCInfoConditionCanTeachSkill@@UEAA_NAEBVgCInfo@@@Z"));
    g_site = base + kSiteRva;
    g_skillInfos = reinterpret_cast<int32_t*>(GetProcAddress(game, "?ms_arrSkillInfos@gCSkills_PS@@2PAUgSSkillInfo@@A"));
    bool tableOk = g_skillInfos != nullptr;
    for (int id : kSkills)
        tableOk = tableOk && g_skillInfos[id * 32] == 0 && g_skillInfos[id * 32 + 1] == kGameMax;
    if (isSatisfied != base + kIsSatisfiedRva || memcmp(g_site, kSiteOriginal, sizeof(kSiteOriginal)) != 0 || !tableOk) {
        Log("patch: Game.dll is not the expected version (Steam Rev. 2045), mod disabled");
        g_state = -1;
        return false;
    }
    g_cave = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_cave) {
        g_state = -1;
        return false;
    }
    memcpy(g_cave, kCave, sizeof(kCave));
    uintptr_t next = reinterpret_cast<uintptr_t>(base + kNextRva), success = reinterpret_cast<uintptr_t>(base + kSuccessRva);
    memcpy(g_cave + sizeof(kCave), &next, 8);
    memcpy(g_cave + sizeof(kCave) + 8, &success, 8);
    DWORD old;
    VirtualProtect(g_cave, 4096, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), g_cave, 4096);
    g_writtenMax = kGameMax;
    g_state = 1;
    return true;
}

static void WriteCode(uint8_t* at, const uint8_t* bytes, size_t size)
{
    DWORD old;
    VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at, bytes, size);
    VirtualProtect(at, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, size);
}

// Game thread only: IsSatisfied runs there too, so it is never half-way through the patched bytes.
static void Apply()
{
    if (!Prepare())
        return;
    int max = g_enabled ? g_maxValue : kGameMax;
    if (max != g_writtenMax) {
        for (int id : kSkills)
            g_skillInfos[id * 32 + 1] = max;
        g_writtenMax = max;
        Log("maximum of strength / dexterity: %d", max);
    }
    if (g_enabled == g_patched)
        return;
    if (g_enabled) {
        uint8_t jump[kJumpSize] = {0xFF, 0x25, 0, 0, 0, 0};
        uintptr_t cave = reinterpret_cast<uintptr_t>(g_cave);
        memcpy(jump + 6, &cave, 8);
        WriteCode(g_site, jump, sizeof(jump));
    } else {
        WriteCode(g_site, kSiteOriginal, kJumpSize);
    }
    g_patched = g_enabled;
    Log("patch: strength / dexterity teacher limits %s", g_patched ? "OFF (up to the maximum)" : "back ON (as in the game)");
}

// ---------------------------------------------------------------------------------------------
// Debug dump

struct Entity {
    void* e = nullptr;
};

static const char* SkillName(int id)
{
    static const char* const kNames[34] = {
        "HitPoints", "ManaPoints", "Level?", "Experience?", "LearnPoints?", "HitPointsMax", "ManaPointsMax",
        "Strength", "Dexterity", "Intelligence", "ProtEdge", "ProtBlunt", "ProtPoint", "ProtFire", "ProtIce",
        "ProtMagic", "CombatSword", "CombatAxe", "CombatStaff", "CombatBow", "CombatCrossBow", "Circle", "Fireball",
        "Frost", "Missile", "Smith", "Mining", "Lockpick", "Pickpocket", "Sneak", "Acrobat", "Alchemy", "Scribe",
        "Trophy"};
    return id >= 0 && id < 34 ? kNames[id] : "?";
}

static void Dump()
{
    HMODULE game = GetModuleHandleA("Game.dll"), script = GetModuleHandleA("Script.dll");
    auto infos = reinterpret_cast<const int32_t*>(
        game ? GetProcAddress(game, "?ms_arrSkillInfos@gCSkills_PS@@2PAUgSSkillInfo@@A") : nullptr);
    if (!infos || !script) {
        Log("dump: no skill table");
        return;
    }
    using GetPlayerFn = Entity* (*)(Entity*);
    using SkillFn = int (*)(const Entity*, int);
    using ValidFn = bool (*)(const Entity*);
    auto getPlayer = reinterpret_cast<GetPlayerFn>(GetProcAddress(script, "?GetPlayer@Entity@@SA?AV1@XZ"));
    auto valid = reinterpret_cast<ValidFn>(GetProcAddress(script, "?IsValid@PSSkills@@QEBA_NXZ"));
    auto base = reinterpret_cast<SkillFn>(GetProcAddress(script, "?GetBase@PSSkills@@QEBAHW4gESkill@@@Z"));
    auto total = reinterpret_cast<SkillFn>(GetProcAddress(script, "?GetTotal@PSSkills@@QEBAHW4gESkill@@@Z"));
    auto maximum = reinterpret_cast<SkillFn>(GetProcAddress(script, "?GetMaximum@PSSkills@@QEBAHW4gESkill@@@Z"));
    Entity player;
    if (getPlayer)
        getPlayer(&player);
    bool havePlayer = player.e && valid && base && total && maximum && valid(&player);
    Log("dump: skill table (min, max, default), player %s, patch %s", havePlayer ? "loaded" : "none",
        g_patched ? "on" : "off");
    for (int id = 0; id < 34; ++id) {
        const int32_t* s = infos + id * 32; // gSSkillInfo, 0x80 bytes
        char line[96] = "";
        if (havePlayer)
            wsprintfA(line, " | player base=%d total=%d max=%d", base(&player, id), total(&player, id),
                      maximum(&player, id));
        Log("  %2d %-15s %5d %5d %5d%s", id, SkillName(id), s[0], s[1], s[2], line);
    }
}

// ---------------------------------------------------------------------------------------------
// Settings, menu, window

static void ConfigLoad()
{
    g_enabled = IniInt("NoLimits", "Enabled", 1) != 0;
    g_maxValue = IniInt("NoLimits", "MaxValue", 1000);
    if (g_maxValue < kGameMax)
        g_maxValue = kGameMax;
    if (g_maxValue > 9999)
        g_maxValue = 9999;
    g_dumpKey = IniKey("Debug", "DumpKey", 0);
    Log("config: enabled=%d max=%d dump=0x%02X", g_enabled, g_maxValue, g_dumpKey);
}

static const HubItem kItems[] = {
    {HubCheckbox, "NoLimits", "Enabled", "Strength and dexterity without limits", "Сила и ловкость без лимитов", 1,
     0, 0, 0, 0, nullptr, -1,
     "Past the game's maximum of 200, and teachers train them that far instead of their own limit. Other skills "
     "and learning point costs are unchanged.",
     "Выше максимума игры (200), и учителя учат до него, а не до своего предела. Остальные навыки и стоимость в "
     "очках обучения не меняются."},
    {HubSlider, "NoLimits", "MaxValue", "Maximum", "Максимум", 1000, 200, 2000, 10, 0, "%.0f", 0},
    {HubNote, nullptr, nullptr, "Without the mod a save shows at most 200 again, the trained points stay in it.",
     "Без мода в сохранении снова будет не больше 200, вкачанное в нём остаётся."},
};

static void OnMenuChanged()
{
    ConfigLoad();
    Apply();
}

static HubPage g_page = {"no-limits", 40, "No Limits", "Без лимитов", nullptr, kItems,
                         sizeof(kItems) / sizeof(kItems[0]), OnMenuChanged};

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NOLIMITS_DUMP:
        Dump();
        return 0;
    case WM_NOLIMITS_APPLY:
        ConfigLoad();
        Apply();
        return 0;
    }
    return CallOriginalWndProc(g_origProc, hwnd, msg, wp, lp);
}

static DWORD WINAPI Worker(LPVOID)
{
    HWND hwnd = WaitForGameWindow();
    g_origProc = SubclassWindow(hwnd, HookedWndProc);
    if (const HubApi* hub = HubGet())
        hub->registerPage(&g_page);
    PostMessageW(hwnd, WM_NOLIMITS_APPLY, 0, 0);
    bool wasDown = false;
    for (unsigned tick = 0; IsWindow(hwnd); ++tick) {
        bool down = g_dumpKey && GetForegroundWindow() == hwnd && (GetAsyncKeyState(g_dumpKey) & 0x8000);
        if (down && !wasDown && !HubBusy())
            PostMessageW(hwnd, WM_NOLIMITS_DUMP, 0, 0);
        wasDown = down;
        if (tick % 30 == 0 && IniChanged())
            PostMessageW(hwnd, WM_NOLIMITS_APPLY, 0, 0);
        Sleep(30);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        ModInit(instance, "no_limits");
        Log("No Limits 0.2.1");
        g_page.iniPath = IniPath(kDefaultIni);
        ConfigLoad();
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr))
            CloseHandle(t);
    }
    return TRUE;
}
