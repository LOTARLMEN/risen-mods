// Developer controls. All engine calls run in the game's window procedure; only GUID values
// (never entity/property-set pointers) survive a command or tick.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>
#include <intrin.h>
#include "common.h"
#include "hub_client.h"
#include "iat_patch.h"

namespace {
struct Entity { void* e=nullptr; };
struct Vec3 {float x,y,z;};
// SharedBase copy constructor/operator= copy 16 core bytes and clear the 4-byte hash cache.
struct PropertyId {uint8_t core[16]{};uint32_t hash=0;};
static_assert(sizeof(PropertyId)==20);
constexpr uint32_t FROZEN=0x7e,KINEMATIC=0x80,NO_GRAVITY=1;
constexpr UINT WM_DEBUG_ACTION=WM_APP+0x5901,WM_DEBUG_TICK=WM_APP+0x5902,WM_DEBUG_RELOAD=WM_APP+0x5903;
enum Action {Select=1,Physics,Lift,Camera,CameraSlower,CameraFaster,SetHour,CycleHour,NextDay,ClearWeather};
WNDPROC g_original;
PropertyId g_selected;
bool g_hasSelection=false,g_ready=false;
std::atomic<bool> g_ownsCamera{false};
char g_name[128]{},g_feedback[180]="Select a loose item: NUMPAD7";
bool g_enabled=false;
int g_select=VK_NUMPAD7,g_physics=VK_NUMPAD5,g_lift=VK_NUMPAD9,g_camera=VK_NUMPAD0;
int g_camSlower=VK_OEM_MINUS,g_camFaster=VK_OEM_PLUS;
int g_timeKey=VK_NUMPAD1,g_cycleKey=VK_NUMPAD2,g_dayKey=VK_NUMPAD6,g_weatherKey=VK_NUMPAD3;
float g_targetHour=12.f;
bool g_lightingReady=false,g_weatherOwned=false;
struct WeatherRecord {char name[128];float weight;};
WeatherRecord g_weatherSaved[128]{};
int g_weatherCount=0,g_weatherMode=0;
constexpr float CAMERA_DEFAULT=300.f,CAMERA_MIN=.1f,CAMERA_MAX=10000.f;
float g_liftCm=100.f,g_camSpeed=CAMERA_DEFAULT;
const char kIni[]=
    "; Developer tools. Object changes can be stored by a normal game save.\r\n"
    "[Debug]\r\nEnabled=0\r\nSelectKey=NUMPAD7\r\nPhysicsKey=NUMPAD5\r\nLiftKey=NUMPAD9\r\nFreecamKey=NUMPAD0\r\nLiftCm=100\r\nFreecamSpeed=300\r\nFreecamSlowerKey=-\r\nFreecamFasterKey==\r\nTargetHour=12\r\nSetHourKey=NUMPAD1\r\nCycleHourKey=NUMPAD2\r\nNextDayKey=NUMPAD6\r\nClearWeatherKey=NUMPAD3\r\n";

struct Api {
    Entity* (*player)(Entity*);
    Entity* (*focus)(const void*,Entity*);
    void* world;
    bool (*item)(const Entity*);
    bool (*npc)(const Entity*);
    bool (*processing)(const Entity*);
    bool (*attached)(Entity*);
    Entity* (*owner)(const Entity*,Entity*);
    bool (*dialog)();
    bool (*bodyValid)(const Entity*);
    void* (*body)(const Entity*);
    uint32_t (*flags)(const Entity*);
    void* (*setFlags)(Entity*,const uint32_t*);
    Vec3* (*position)(const Entity*,Vec3*);
    void (*setPosition)(Entity*,const Vec3*); // PSRigidBody::SetPosition(Vec3): x64 indirect by-value ABI
    void* (*name)(const Entity*,void*);
    const char* (*stringText)(const void*);
    void (*stringFree)(void*);
    const PropertyId* (*id)(const void*);
    void* (*moduleAdmin)();
    int (*moduleCount)(const void*);
    void* (*moduleAt)(const void*,int);
    void* (*lookup)(void*,const PropertyId*,int);
    void* sceneVtable;
    void* cameraVtable;
    bool (*cameraActive)(const void*);
    void (*toggleCamera)(void*);
    void (*cameraSpeed)(void*,float);
    void (*stop)(void*);
    void (*enablePhysics)(void*,const bool*);
    void (*wakeCounter)(void*,const float*);
    void (*sleep)(void*,bool);
    void* (*currentWorld)();
    void* (*worldEntity)(void*);
    bool (*clockValid)(const Entity*);
    void* (*clock)(const Entity*);
    const uint32_t* (*clockYear)(const void*);
    const uint32_t* (*clockDay)(const void*);
    const uint32_t* (*clockHour)(const void*);
    const uint32_t* (*clockMinute)(const void*);
    void (*setYear)(void*,const uint32_t*);
    void (*setDay)(void*,const uint32_t*);
    void (*setHour)(void*,const uint32_t*);
    void (*setMinute)(void*,const uint32_t*);
    void (*setSecond)(void*,const uint32_t*);
    void* weatherVtable;
    int (*weatherMode)(const void*);
    void (*setWeatherMode)(void*,int);
    int (*weatherCount)(const void*);
    void* (*weatherAt)(const void*,int);
    void* weatherFind;
    const void* (*weatherName)(const void*);
    float (*weatherWeight)(const void*);
    void (*setWeatherWeight)(void*,float);
} a{};

using RotateFn=void (*)(void*,Vec3*);
RotateFn g_rotate=nullptr;
uintptr_t g_rotateReturn=0;
bool g_rotationReady=false;
// Normal camera input is 0.075 degrees per action unit; freefly uses 0.025 radians.
// Both already receive the player's mouse sensitivity/inversion. Convert only the native
// freefly rotation call, keeping mouse sensitivity fixed independently of translation speed.
constexpr float CAMERA_ROTATION_MATCH=.075f*(3.14159265358979323846f/180.f)/.025f;
void RotateCamera(void* self,Vec3* angle) {
    if(g_enabled&&g_ownsCamera&&reinterpret_cast<uintptr_t>(_ReturnAddress())==g_rotateReturn) {
        Vec3 matched{angle->x*CAMERA_ROTATION_MATCH,angle->y*CAMERA_ROTATION_MATCH,angle->z};
        static bool logged=false;
        if(!logged){logged=true;Log("freecam: rotation matched to player mouse sensitivity");}
        g_rotate(self,&matched);
    } else g_rotate(self,angle);
}
bool CameraRotationInit(HMODULE game) {
    auto code=reinterpret_cast<const uint8_t*>(game);
    // Rev.2045: validate the freefly call, the two input gains, and the normal camera's
    // degrees-to-radians path before changing its IAT. No executable bytes are patched.
    const uint8_t call[]={0xff,0x15,0x44,0xb9,0x30,0x00};
    const uint8_t normalGain[]={0xf3,0x0f,0x59,0x05,0x55,0x71,0x7b,0x00};
    const uint8_t freeGain[]={0xf3,0x0f,0x59,0x05,0x1b,0x71,0x7b,0x00};
    const uint8_t stateGetter[]={0x0f,0xb6,0x41,0x38,0xc3};
    const uint8_t normalRadians[]={0xf3,0x0f,0x59,0x30};
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(code+reinterpret_cast<const IMAGE_DOS_HEADER*>(code)->e_lfanew);
    if(nt->OptionalHeader.SizeOfImage<0xf631a4){Log("freecam: unexpected Game.dll size; camera disabled");return false;}
    float normal=0,free=0;memcpy(&normal,code+0xf631a0,4);memcpy(&free,code+0xf63194,4);
    if(memcmp(code+0x7af5fe,call,sizeof(call))||memcmp(code+0x7ac043,normalGain,sizeof(normalGain))
        ||memcmp(code+0x7ac071,freeGain,sizeof(freeGain))||memcmp(reinterpret_cast<const void*>(a.cameraActive),stateGetter,sizeof(stateGetter))
        ||memcmp(code+0x7bdf4b,normalRadians,sizeof(normalRadians))||normal!=.075f||free!=.025f) {
        Log("freecam: unexpected rotation code (other game version?); camera disabled");return false;
    }
    g_rotateReturn=reinterpret_cast<uintptr_t>(code+0x7af604);
    const char kRotate[]="?RotateAngularRel@eCCameraBase@@QEAAXAEBVbCVector@@@Z";
    // Publish a callable original before the IAT points at RotateCamera.
    HMODULE engine=GetModuleHandleA("Engine.dll");
    g_rotate=engine?reinterpret_cast<RotateFn>(GetProcAddress(engine,kRotate)):nullptr;
    void* previous=g_rotate?PatchImport(game,"Engine.dll",kRotate,reinterpret_cast<void*>(RotateCamera)):nullptr;
    if(!previous){g_rotate=nullptr;Log("freecam: rotation import unavailable; camera disabled");return false;}
    if(previous!=reinterpret_cast<void*>(g_rotate))g_rotate=reinterpret_cast<RotateFn>(previous);
    Log("freecam: rotation call/gains verified, player sensitivity match installed");return true;
}

template<class T> bool Resolve(HMODULE module,T& fn,const char* name) {
    fn=module?reinterpret_cast<T>(GetProcAddress(module,name)):nullptr;
    if (!fn) Log("missing API: %s",name);
    return fn!=nullptr;
}
// Retail omits these trivial getters. Validate the corresponding exported setters'
// exact field stores before reading the properties; all writes use native setters.
const uint32_t* ClockDay(const void* p){return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(p)+0x2c);}
const uint32_t* ClockHour(const void* p){return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(p)+0x30);}
const uint32_t* ClockMinute(const void* p){return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(p)+0x34);}
const void* WeatherName(const void* p){return static_cast<const uint8_t*>(p)+0x78;}
bool ClockLayoutValid(){
    const uint8_t day[]={0x89,0x47,0x2c},hour[]={0x89,0x47,0x30},minute[]={0x89,0x47,0x34};
    return a.setDay&&a.setHour&&a.setMinute&&
        !memcmp(reinterpret_cast<const uint8_t*>(a.setDay)+0x2c,day,3)&&
        !memcmp(reinterpret_cast<const uint8_t*>(a.setHour)+0x2c,hour,3)&&
        !memcmp(reinterpret_cast<const uint8_t*>(a.setMinute)+0x2c,minute,3);
}
// With TestWorld set, gCHUDFileManager::EndProgress (+0x87ab00) must not play
// <voice language>_Intro.bik after a new game; only that one verified call site is skipped.
using PlayVideoFn=void (*)(void*,const void*,bool);
PlayVideoFn g_playVideo=nullptr;
uintptr_t g_introReturn=0;
void PlayVideo(void* app,const void* name,bool wait) {
    if(reinterpret_cast<uintptr_t>(_ReturnAddress())==g_introReturn){Log("test world: new-game intro video skipped");return;}
    g_playVideo(app,name,wait);
}
void SkipIntroVideo(HMODULE game) {
    // call qword ptr [rip+0x241363] -> IAT eCApplication::PlayVideo, preceded by "_Intro.bik" lea.
    auto code=reinterpret_cast<const uint8_t*>(game);
    const uint8_t call[]={0xff,0x15,0x63,0x13,0x24,0x00},introName[]={0x48,0x8d,0x15,0xf6,0x17,0x40,0x00};
    if(memcmp(code+0x87ac0f,call,sizeof(call))||memcmp(code+0x87abd3,introName,sizeof(introName))
        ||strcmp(reinterpret_cast<const char*>(code+0xc7c3d0),"_Intro.bik")){Log("test world: unexpected intro code; video not skipped");return;}
    const char kPlay[]="?PlayVideo@eCApplication@@QEAAXAEBVbCString@@_N@Z";
    HMODULE engine=GetModuleHandleA("Engine.dll");
    g_playVideo=engine?reinterpret_cast<PlayVideoFn>(GetProcAddress(engine,kPlay)):nullptr;
    g_introReturn=reinterpret_cast<uintptr_t>(code+0x87ac15);
    void* previous=g_playVideo?PatchImport(game,"Engine.dll",kPlay,reinterpret_cast<void*>(PlayVideo)):nullptr;
    if(!previous){g_playVideo=nullptr;Log("test world: PlayVideo import unavailable; video not skipped");return;}
    if(previous!=reinterpret_cast<void*>(g_playVideo))g_playVideo=reinterpret_cast<PlayVideoFn>(previous);
    Log("test world: new-game intro video will be skipped");
}
// Rtn_Player (Script_Game +0x1af090) runs PS_Gamestart (lying hero, "where am I", stand up)
// unless game variable "Gamestart" exists. In the test world that one check answers "exists".
using ExistsVarFn=bool (*)(const void*);
ExistsVarFn g_existsVar=nullptr;
uintptr_t g_gamestartReturn=0;
bool ExistsGameVariable(const void* name) {
    if(reinterpret_cast<uintptr_t>(_ReturnAddress())==g_gamestartReturn) {
        static bool logged=false;if(!logged){logged=true;Log("test world: wake-up scene PS_Gamestart skipped");}
        return true;
    }
    return g_existsVar(name);
}
void SkipGamestartScene() {
    HMODULE scriptGame=GetModuleHandleA("Script_Game.dll");
    auto code=reinterpret_cast<const uint8_t*>(scriptGame);
    // call qword ptr [rip+0x53ecd] -> IAT InfoSystem::ExistsGameVariable, argument "Gamestart".
    const uint8_t call[]={0xff,0x15,0xcd,0x3e,0x05,0x00},name[]={0x48,0x8d,0x15,0x61,0xd5,0x0b,0x00};
    if(!code||memcmp(code+0x1af175,call,sizeof(call))||memcmp(code+0x1af160,name,sizeof(name))
        ||strcmp(reinterpret_cast<const char*>(code+0x26c6c8),"Gamestart")){Log("test world: unexpected Rtn_Player code; wake-up scene not skipped");return;}
    const char kExists[]="?ExistsGameVariable@InfoSystem@@SA_NAEBVbCString@@@Z";
    HMODULE script=GetModuleHandleA("Script.dll");
    g_existsVar=script?reinterpret_cast<ExistsVarFn>(GetProcAddress(script,kExists)):nullptr;
    g_gamestartReturn=reinterpret_cast<uintptr_t>(code+0x1af17b);
    void* previous=g_existsVar?PatchImport(scriptGame,"Script.dll",kExists,reinterpret_cast<void*>(ExistsGameVariable)):nullptr;
    if(!previous){g_existsVar=nullptr;Log("test world: ExistsGameVariable import unavailable; wake-up scene not skipped");return;}
    if(previous!=reinterpret_cast<void*>(g_existsVar))g_existsVar=reinterpret_cast<ExistsVarFn>(previous);
    Log("test world: wake-up scene will be skipped");
}
// Test-world probe (TODO 5.23): which command line the engine got and which world New Game
// would load (gCHUDFileManager reads option "worldfile"). Read-only, logged once.
void LogWorldNameProbe(HMODULE engine,HMODULE game){
    void* (*app)();const void* (*line)(void*);void* (*files)();void* (*worldName)(const void*,void*);
    if(!Resolve(engine,app,"?GetInstance@eCApplication@@SAAEAV1@XZ")||
       !Resolve(engine,line,"?GetCommandLineA@eCApplication@@QEBAAEBVbCString@@XZ")||
       !Resolve(game,files,"?GetInstance@gCHUDFileManager@@SAAEAV1@XZ")||
       !Resolve(game,worldName,"?GetDefaultWorldName@gCHUDFileManager@@IEBA?AVbCString@@XZ"))return;
    const void* cmd=line(app());void* text=nullptr;worldName(files(),&text);
    const char* cmdText=a.stringText(cmd);const char* worldText=text?a.stringText(&text):nullptr;
    Log("probe: engine command line '%s', new game world '%s'",cmdText?cmdText:"",worldText?worldText:"");
    a.stringFree(&text);
    // Experiment: Risen.exe hard-codes "-worldfile=World"; [Debug] TestWorld swaps that token in the
    // engine's copy, which GetDefaultWorldName re-parses on every New Game.
    char world[64];IniString("Debug","TestWorld","",world,sizeof(world));
    if(!world[0]||!cmdText)return;
    for(const char* c=world;*c;c++)if(!isalnum(static_cast<unsigned char>(*c))&&*c!='_'){Log("probe: bad TestWorld");return;}
    const char* token=strstr(cmdText,"-worldfile=World");void* (*assign)(void*,const char*);
    if(!token||(token[16]&&token[16]!=' ')||!Resolve(GetModuleHandleA("SharedBase.dll"),assign,"??4bCString@@QEAAAEAV0@PEBD@Z"))
        {Log("probe: worldfile token not found; TestWorld ignored");return;}
    char next[512];snprintf(next,sizeof(next),"%.*s-worldfile=%s%s",static_cast<int>(token-cmdText),cmdText,world,token+16);
    assign(const_cast<void*>(cmd),next);text=nullptr;worldName(files(),&text);
    worldText=text?a.stringText(&text):nullptr;
    Log("probe: TestWorld set, command line '%s', new game world '%s'",a.stringText(cmd),worldText?worldText:"");
    a.stringFree(&text);
    SkipIntroVideo(game);
    SkipGamestartScene();
}
bool ApiInit() {
    if(g_ready)return true;
    HMODULE script=GetModuleHandleA("Script.dll"),engine=GetModuleHandleA("Engine.dll"),
        game=GetModuleHandleA("Game.dll"),base=GetModuleHandleA("SharedBase.dll");
    if(!script||!engine||!game||!base)return false;
    static bool failed=false;if(failed)return false;
    bool ok=true;
#define API(MEMBER,MODULE,SYMBOL) ok=Resolve(MODULE,a.MEMBER,SYMBOL)&&ok
    API(player,script,"?GetPlayer@Entity@@SA?AV1@XZ");
    API(focus,script,"?GetFocus@World@@QEBA?AVEntity@@XZ");
    API(world,script,"world");
    API(item,script,"?IsItem@Entity@@QEBA_NXZ");
    API(npc,script,"?IsNPC@Entity@@QEBA_NXZ");
    API(processing,script,"?IsProcessingRangeEntered@Entity@@QEBA_NXZ");
    API(attached,script,"?IsAttachedToSlot@PSInteraction@@QEAA_NXZ");
    API(owner,script,"?GetOwner@Entity@@QEBA?AV1@XZ");
    API(dialog,script,"?IsDialogCameraActive@Entity@@SA_NXZ");
    API(bodyValid,script,"?IsValid@PSRigidBody@@QEBA_NXZ");
    API(body,script,"??CPSRigidBody@@QEBAPEBVeCRigidBody_PS@@XZ");
    API(flags,script,"??BPropertyBodyFlag@PSRigidBody@@QEBA?BW4eERigidbody_Flag@@XZ");
    API(setFlags,script,"??4PropertyBodyFlag@PSRigidBody@@QEAAAEAV01@AEBW4eERigidbody_Flag@@@Z");
    API(position,script,"?GetPosition@Entity@@QEBA?AVbCVector@@XZ");
    API(setPosition,script,"?SetPosition@PSRigidBody@@QEAAXVbCVector@@@Z");
    API(name,script,"?GetName@Entity@@QEBA?AVbCString@@XZ");
    API(stringText,base,"?GetText@bCString@@QEBAPEBDXZ");
    API(stringFree,base,"??1bCString@@QEAA@XZ");
    API(id,engine,"?GetID@eCDynamicEntity@@UEBAAEBVbCPropertyID@@XZ");
    API(moduleAdmin,engine,"?GetInstance@eCModuleAdmin@@SAAEAV1@XZ");
    API(moduleCount,engine,"?GetModuleCount@eCModuleAdmin@@QEBAHXZ");
    API(moduleAt,engine,"?GetModule@eCModuleAdmin@@QEBAPEAVeCEngineComponentBase@@H@Z");
    API(lookup,engine,"?GetEntity@eCSceneAdmin@@QEAAPEAVeCNode@@AEBVbCPropertyID@@W4eEEntityType@@@Z");
    API(sceneVtable,engine,"??_7eCSceneAdmin@@6B@");
    API(cameraVtable,game,"??_7gCCameraAdmin@@6B@");
    // GetDebugMode is a retail stub (xor al,al; ret). IsExternalCamera reads the
    // same real flag that OnToggleFreeFlyCam toggles; do not lose ownership each tick.
    API(cameraActive,game,"?IsExternalCamera@gCCameraAdmin@@QEAA_NXZ");
    API(toggleCamera,game,"?OnToggleFreeFlyCam@gCCameraAdmin@@QEAAXXZ");
    API(cameraSpeed,game,"?SetFreeCamSpeed@gCCameraAdmin@@QEAAXM@Z");
    API(stop,engine,"?StopMotion@eCRigidBody_PS@@UEAAXXZ");
    API(enablePhysics,engine,"?SetPhysicsEnabled@eCRigidBodyBase_PS@@QEAAXAEB_N@Z");
    API(wakeCounter,engine,"?SetWakeUpCounter@eCRigidBody_PS@@QEAAXAEBM@Z");
    API(sleep,engine,"?SetIsSleeping@eCRigidBody_PS@@UEAAX_N@Z");
#undef API
    bool lightOk=true;
#define LIGHT_API(MEMBER,MODULE,SYMBOL) lightOk=Resolve(MODULE,a.MEMBER,SYMBOL)&&lightOk
    LIGHT_API(currentWorld,game,"?GetCurrentWorld@gCWorld@@SAPEAV1@XZ");
    LIGHT_API(worldEntity,game,"?GetWorldEntity@gCWorld@@QEAAPEAVeCDynamicEntity@@XZ");
    LIGHT_API(clockValid,script,"?IsValid@PSClock@@QEBA_NXZ");
    LIGHT_API(clock,script,"??CPSClock@@QEBAPEBVgCClock_PS@@XZ");
    LIGHT_API(clockYear,game,"?GetYear@gCClock_PS@@QEBAAEBKXZ");
    LIGHT_API(setYear,game,"?SetYear@gCClock_PS@@QEAAXAEBK@Z");
    LIGHT_API(setDay,game,"?SetDay@gCClock_PS@@QEAAXAEBK@Z");
    LIGHT_API(setHour,game,"?SetHour@gCClock_PS@@QEAAXAEBK@Z");
    LIGHT_API(setMinute,game,"?SetMinute@gCClock_PS@@QEAAXAEBK@Z");
    LIGHT_API(setSecond,game,"?SetSecond@gCClock_PS@@QEAAXAEBK@Z");
    LIGHT_API(weatherVtable,engine,"??_7eCWeatherAdmin@@6B@");
    LIGHT_API(weatherMode,engine,"?GetCurrentMode@eCWeatherAdmin@@QEBA?AW4eEWeatherAdminMode@@XZ");
    LIGHT_API(setWeatherMode,engine,"?SetCurrentMode@eCWeatherAdmin@@QEAAXW4eEWeatherAdminMode@@@Z");
    LIGHT_API(weatherCount,engine,"?GetWeatherStatesCount@eCWeatherAdmin@@QEBAHXZ");
    LIGHT_API(weatherAt,engine,"?GetWeatherStatesAt@eCWeatherAdmin@@QEBAPEAVeCWeatherStates@@H@Z");
    LIGHT_API(weatherFind,engine,"?GetWeatherStatesByName@eCWeatherAdmin@@QEBAPEAVeCWeatherStates@@AEBVbCString@@@Z");
    LIGHT_API(weatherWeight,engine,"?GetBlendFactor@eCWeatherStates@@QEBAMXZ");
    LIGHT_API(setWeatherWeight,engine,"?SetBlendFactor@eCWeatherStates@@QEAAXM@Z");
#undef LIGHT_API
    a.clockDay=ClockDay;a.clockHour=ClockHour;a.clockMinute=ClockMinute;
    a.weatherName=WeatherName;
    const uint8_t weatherNameField[]={0x48,0x83,0xc1,0x78};
    lightOk=lightOk&&ClockLayoutValid()&&
        !memcmp(static_cast<const uint8_t*>(a.weatherFind)+0x3e,weatherNameField,4);
    g_lightingReady=lightOk;
    Log("time/weather API: %s",lightOk?"resolved":"disabled");
    if(ok)LogWorldNameProbe(engine,game);
    if(ok)g_rotationReady=CameraRotationInit(game);
    g_ready=ok;failed=!ok;
    Log("API: %s (exports only; no binary patches)",ok?"resolved":"disabled");
    return ok;
}
void* Module(void* vtable) {
    void* admin=a.moduleAdmin();if(!admin)return nullptr;
    int n=a.moduleCount(admin);if(n<0||n>512)return nullptr;
    for(int i=0;i<n;++i){void* m=a.moduleAt(admin,i);if(m&&*static_cast<void**>(m)==vtable)return m;}
    return nullptr;
}
void Feedback(const char* message) {
    snprintf(g_feedback,sizeof(g_feedback),"%s",message);Log("%s",g_feedback);
}
bool Player(Entity& player) {
    a.player(&player);return player.e&&!a.dialog();
}
bool Allowed(Entity& entity,const Entity& player) {
    if(!entity.e||entity.e==player.e||!a.item(&entity)||a.npc(&entity)||!a.bodyValid(&entity)
        ||!a.processing(&entity)||a.attached(&entity))return false;
    Entity owner;a.owner(&entity,&owner);if(owner.e)return false;
    Vec3 p{},q{};a.position(&entity,&p);a.position(&player,&q);
    float d=(p.x-q.x)*(p.x-q.x)+(p.y-q.y)*(p.y-q.y)+(p.z-q.z)*(p.z-q.z);
    return std::isfinite(d)&&d<=2000.f*2000.f;
}
Entity Selected(const Entity& player) {
    Entity e;if(!g_hasSelection)return e;
    void* scene=Module(a.sceneVtable);if(!scene)return e;
    // Rev.2045 GetEntity resolves the GUID map; the type argument is unused in this overload.
    e.e=a.lookup(scene,&g_selected,0);
    if(!Allowed(e,player))e.e=nullptr;
    return e;
}
void Freeze(Entity& e,bool frozen) {
    void* body=a.body(&e);if(!body)return;
    const bool enabled=true;a.enablePhysics(body,&enabled);a.stop(body);
    uint32_t flags=a.flags(&e);
    flags=frozen?((flags&~KINEMATIC)|FROZEN):(flags&~(FROZEN|KINEMATIC|NO_GRAVITY));
    a.setFlags(&e,&flags); // native property notification updates serialized AND buffered actor flags
    const float wake=frozen?0.f:1.f;a.wakeCounter(body,&wake);
    a.sleep(body,frozen);
}
void SelectItem(const Entity& player) {
    Entity e;a.focus(a.world,&e);
    if(!Allowed(e,player)){Feedback("Aim at a loose item with a rigid body (AutoLoot off)");return;}
    const PropertyId* id=a.id(e.e);
    if(!id){Feedback("Target has no entity ID");return;}
    PropertyId copy{};memcpy(copy.core,id->core,sizeof(copy.core));
    void* scene=Module(a.sceneVtable);
    if(!scene||a.lookup(scene,&copy,0)!=e.e){Feedback("GUID lookup did not confirm the focused item; refused");return;}
    g_selected=copy;g_hasSelection=true;
    void* text=nullptr;a.name(&e,&text);snprintf(g_name,sizeof(g_name),"%s",text?a.stringText(&text):"item");a.stringFree(&text);
    char message[180];snprintf(message,sizeof(message),"Selected: %s",g_name);Feedback(message);
    Log("target GUID core: %02X%02X%02X%02X..., flags=0x%X",copy.core[0],copy.core[1],copy.core[2],copy.core[3],a.flags(&e));
}
void ReturnCamera() {
    if(!g_ready||!g_ownsCamera)return;
    void* camera=Module(a.cameraVtable);
    if(camera&&a.cameraActive(camera))a.toggleCamera(camera);
    g_ownsCamera=false;Log("freecam: returned to player");
}
void* WorldClock() {
    if(!g_lightingReady)return nullptr;
    void* world=a.currentWorld();if(!world)return nullptr;
    Entity entity{a.worldEntity(world)};
    return entity.e&&a.clockValid(&entity)?a.clock(&entity):nullptr;
}
void RestoreWeather() {
    if(!g_weatherOwned)return;
    void* weather=Module(a.weatherVtable);
    if(weather&&a.weatherMode(weather)==2) {
        bool same=a.weatherCount(weather)==g_weatherCount;
        for(int i=0;same&&i<g_weatherCount;++i){void* state=a.weatherAt(weather,i);
            const char* name=state?a.stringText(a.weatherName(state)):nullptr;
            same=name&&strcmp(name,g_weatherSaved[i].name)==0;
        }
        if(same){for(int i=0;i<g_weatherCount;++i)a.setWeatherWeight(a.weatherAt(weather,i),g_weatherSaved[i].weight);
            a.setWeatherMode(weather,g_weatherMode);
        }else{a.setWeatherMode(weather,0);Log("weather catalogue changed; returned to native player-position mode");}
    }
    g_weatherOwned=false;g_weatherCount=0;
}
void LightingCommand(int action) {
    void* clock=WorldClock();if(!clock){Feedback("Time/weather unavailable: load a world (see log)");return;}
    if(action==ClearWeather) {
        if(g_weatherOwned){RestoreWeather();Feedback("Weather: previous native mode restored");return;}
        void* weather=Module(a.weatherVtable);if(!weather){Feedback("Weather module unavailable");return;}
        int count=a.weatherCount(weather),mode=a.weatherMode(weather),sunny=-1;
        if(count<1||count>128||mode<0||mode>3){Feedback("Unsupported weather catalogue/mode");return;}
        for(int i=0;i<count;++i){void* state=a.weatherAt(weather,i);
            const char* name=state?a.stringText(a.weatherName(state)):nullptr;
            float weight=state?a.weatherWeight(state):NAN;
            if(!name||strlen(name)>=sizeof(g_weatherSaved[i].name)||!std::isfinite(weight)){
                Feedback("Invalid weather state; no changes applied");return;
            }
            snprintf(g_weatherSaved[i].name,sizeof(g_weatherSaved[i].name),"%s",name);g_weatherSaved[i].weight=weight;
            if(strcmp(name,"Sunny_Full")==0)sunny=i;
        }
        if(sunny<0){Feedback("Sunny_Full weather state unavailable");return;}
        g_weatherMode=mode;g_weatherCount=count;
        // Same mode/weights as native CON_Weather, through exported setters only.
        a.setWeatherMode(weather,2);
        for(int i=0;i<count;++i)a.setWeatherWeight(a.weatherAt(weather,i),i==sunny?1.f:0.f);
        g_weatherOwned=true;Feedback("Weather: Sunny_Full ON (press again to restore)");return;
    }
    if(action==NextDay){
        uint32_t day=*a.clockDay(clock),year=*a.clockYear(clock);
        if(day>=365||year>=10000){Feedback("Invalid world date; day unchanged");return;}
        if(day==364){++year;day=0;a.setYear(clock,&year);}else ++day;
        a.setDay(clock,&day);Feedback("Advanced one day; current hour kept (saved with game)");return;
    }
    uint32_t total=static_cast<uint32_t>(g_targetHour*60.f+.5f);if(total>1439)total=1439;
    if(action==CycleHour){uint32_t now=*a.clockHour(clock)*60+*a.clockMinute(clock);
        total=now<360?360:now<720?720:now<1080?1080:0;
    }
    uint32_t hour=total/60,minute=total%60,second=0;
    a.setHour(clock,&hour);a.setMinute(clock,&minute);a.setSecond(clock,&second);
    char message[180];snprintf(message,sizeof(message),"World time: %02u:%02u (saved with game)",hour,minute);Feedback(message);
}
void Command(int action) {
    if(!g_enabled||HubBusy()||!ApiInit())return;
    Entity player;if(!Player(player)){Feedback("Load a game and leave the dialog first");return;}
    if(action>=SetHour&&action<=ClearWeather){LightingCommand(action);return;}
    if(action==Select){SelectItem(player);return;}
    if(action==Camera) {
        if(!g_rotationReady){Feedback("Freecam unavailable: game version check failed (see log)");return;}
        void* camera=Module(a.cameraVtable);if(!camera){Feedback("Camera module unavailable");return;}
        bool was=a.cameraActive(camera);a.toggleCamera(camera);g_ownsCamera=!was;
        if(!was)a.cameraSpeed(camera,g_camSpeed);
        if(was)Feedback("Freecam OFF");
        else {char slower[32],faster[32],message[180];
            snprintf(message,sizeof(message),"Freecam ON: %.3g cm/s (speed keys: %s / %s)",g_camSpeed,
                KeyName(g_camSlower,slower,sizeof(slower)),KeyName(g_camFaster,faster,sizeof(faster)));Feedback(message);}
        return;
    }
    if(action==CameraSlower||action==CameraFaster) {
        if(!g_ownsCamera)return;
        void* camera=Module(a.cameraVtable);if(!camera||!a.cameraActive(camera))return;
        float next=g_camSpeed*(action==CameraFaster?2.f:.5f);
        if(next<CAMERA_MIN)next=CAMERA_MIN;if(next>CAMERA_MAX)next=CAMERA_MAX;
        if(next==g_camSpeed)return;
        // The engine clamps neither speed nor range. Store first, then re-read the persisted
        // decimal so the subsequent INI reload cannot introduce rounding drift.
        IniWriteFloat("Debug","FreecamSpeed",next);g_camSpeed=IniFloat("Debug","FreecamSpeed",next);
        a.cameraSpeed(camera,g_camSpeed);
        char message[180];snprintf(message,sizeof(message),"Freecam speed: %.3g cm/s",g_camSpeed);Feedback(message);return;
    }
    Entity e=Selected(player);
    if(!e.e){Feedback("Select a nearby loose item first (NUMPAD7)");return;}
    if(action==Physics) {
        bool frozen=(a.flags(&e)&FROZEN)==FROZEN;Freeze(e,!frozen);
        Feedback(frozen?"Physics ON: released and awake":"Physics OFF: item frozen");
    } else if(action==Lift) {
        Vec3 p{};a.position(&e,&p);p.y+=g_liftCm;
        if(!std::isfinite(p.y)){Feedback("Invalid lift position");return;}
        Freeze(e,true);a.setPosition(&e,&p);
        // Keep the frozen body awake for this explicit pose update; it cannot fall while locked.
        if(void* body=a.body(&e))a.sleep(body,false);
        char message[180];snprintf(message,sizeof(message),"Lifted %.0f cm and frozen; release with physics key",g_liftCm);Feedback(message);
        Log("lift target: %.3f %.3f %.3f",p.x,p.y,p.z);
    }
}
void Tick() {
    if(!ApiInit())return;
    static bool checkedModules=false;
    if(!checkedModules) {
        void* scene=Module(a.sceneVtable);void* camera=Module(a.cameraVtable);
        if(scene&&camera){checkedModules=true;Log("modules: scene and camera found by exported vtables");}
    }
    Entity player;
    if(!g_enabled||!Player(player)){ReturnCamera();if(!g_enabled||!player.e)RestoreWeather();if(!player.e)g_hasSelection=false;return;}
    if(g_weatherOwned){void* weather=Module(a.weatherVtable);if(!weather||a.weatherMode(weather)!=2){g_weatherOwned=false;g_weatherCount=0;}}
    if(g_ownsCamera){void* camera=Module(a.cameraVtable);if(!camera||!a.cameraActive(camera))g_ownsCamera=false;}
}
void ConfigLoad() {
    bool enabled=IniInt("Debug","Enabled",0)!=0;
    if(!enabled&&g_enabled){ReturnCamera();RestoreWeather();}g_enabled=enabled;
    g_select=IniKey("Debug","SelectKey",VK_NUMPAD7);g_physics=IniKey("Debug","PhysicsKey",VK_NUMPAD5);
    g_lift=IniKey("Debug","LiftKey",VK_NUMPAD9);g_camera=IniKey("Debug","FreecamKey",VK_NUMPAD0);
    g_camSlower=IniKey("Debug","FreecamSlowerKey",VK_OEM_MINUS);g_camFaster=IniKey("Debug","FreecamFasterKey",VK_OEM_PLUS);
    g_timeKey=IniKey("Debug","SetHourKey",VK_NUMPAD1);g_cycleKey=IniKey("Debug","CycleHourKey",VK_NUMPAD2);
    g_dayKey=IniKey("Debug","NextDayKey",VK_NUMPAD6);g_weatherKey=IniKey("Debug","ClearWeatherKey",VK_NUMPAD3);
    g_targetHour=IniFloat("Debug","TargetHour",12.f);
    if(!std::isfinite(g_targetHour)||g_targetHour<0.f||g_targetHour>=24.f)g_targetHour=12.f;
    g_liftCm=IniFloat("Debug","LiftCm",100.f);if(!std::isfinite(g_liftCm)||g_liftCm<1.f||g_liftCm>500.f)g_liftCm=100.f;
    g_camSpeed=IniFloat("Debug","FreecamSpeed",CAMERA_DEFAULT);
    if(!std::isfinite(g_camSpeed))g_camSpeed=CAMERA_DEFAULT;
    else if(g_camSpeed<CAMERA_MIN)g_camSpeed=CAMERA_MIN;else if(g_camSpeed>CAMERA_MAX)g_camSpeed=CAMERA_MAX;
    // F10 callbacks and INI reloads run on the game thread. Apply immediately to an owned
    // freecam too: previously the slider only took effect after toggling the camera again.
    if(g_ready&&g_ownsCamera) {
        void* camera=Module(a.cameraVtable);
        if(camera&&a.cameraActive(camera))a.cameraSpeed(camera,g_camSpeed);
    }
    Log("config: enabled=%d select=0x%X physics=0x%X lift=0x%X freecam=0x%X speed=%.2f cm/s slower=0x%X faster=0x%X",g_enabled,g_select,g_physics,g_lift,g_camera,g_camSpeed,g_camSlower,g_camFaster);
    Log("time/weather: target=%.2f keys hour=0x%X cycle=0x%X next-day=0x%X clear=0x%X",g_targetHour,g_timeKey,g_cycleKey,g_dayKey,g_weatherKey);
}
const HubItem kItems[]={
    {HubCheckbox,"Debug","Enabled","Enable world debugging","Включить отладку мира",0,0,0,0,0,nullptr,-1},
    {HubNote,nullptr,nullptr,"Turn AutoLoot off. Aim at a loose item, select it, then lift/freeze/release.",
        "Выключите AutoLoot. Наведитесь на предмет, выберите его, затем поднимайте/замораживайте/отпускайте."},
    {HubKey,"Debug","SelectKey","Select focused item","Выбрать предмет под прицелом",VK_NUMPAD7,0,0,0,0,nullptr,0},
    {HubKey,"Debug","PhysicsKey","Freeze / release selected item","Заморозить / отпустить предмет",VK_NUMPAD5,0,0,0,0,nullptr,0},
    {HubKey,"Debug","LiftKey","Lift and freeze selected item","Поднять и заморозить предмет",VK_NUMPAD9,0,0,0,0,nullptr,0},
    {HubSlider,"Debug","LiftCm","Lift height","Высота подъёма",100,1,500,10,1,"%.0f cm",0},
    {HubKey,"Debug","FreecamKey","Native free camera","Свободная камера",VK_NUMPAD0,0,0,0,0,nullptr,0},
    {HubSlider,"Debug","FreecamSpeed","Camera movement speed","Скорость перемещения камеры",CAMERA_DEFAULT,CAMERA_MIN,CAMERA_MAX,.1f,1,"%.2f cm/s",0,
        "Applies immediately. Ctrl+click for precise entry. Mouse look keeps the player's sensitivity.",
        "Применяется сразу. Ctrl+клик — точный ввод. Вращение мышью сохраняет чувствительность героя."},
    {HubKey,"Debug","FreecamSlowerKey","Camera slower (halve)","Камера медленнее (вдвое)",VK_OEM_MINUS,0,0,0,0,nullptr,0},
    {HubKey,"Debug","FreecamFasterKey","Camera faster (double)","Камера быстрее (вдвое)",VK_OEM_PLUS,0,0,0,0,nullptr,0},
    {HubNote,nullptr,nullptr,"In freecam: - / = change movement speed; hold to repeat. Mouse look stays unchanged.",
        "В полёте: - / = меняют скорость движения; можно удерживать. Чувствительность мыши не меняется."},
    {HubNote,nullptr,nullptr,"Existing rigid-body items only. Saving can retain physics/position changes.",
        "Только предметы с физическим телом. Сохранение может запомнить изменения физики и положения."},
    {HubNote,nullptr,nullptr,"Select before flying away. Physics keys keep that item selected (20 m from hero).",
        "Выберите предмет перед полётом. Клавиши физики работают с ним в пределах 20 м от героя."},
    {HubHeader,nullptr,nullptr,"Time and weather","Время и погода"},
    {HubSlider,"Debug","TargetHour","Hour to apply (12.5 = 12:30)","Час для установки (12.5 = 12:30)",12,0,23.99f,.25f,1,"%.2f h",0},
    {HubKey,"Debug","SetHourKey","Apply selected hour","Применить выбранный час",VK_NUMPAD1,0,0,0,0,nullptr,0},
    {HubKey,"Debug","CycleHourKey","Cycle dawn / noon / dusk / night","Утро / полдень / вечер / ночь",VK_NUMPAD2,0,0,0,0,nullptr,0},
    {HubKey,"Debug","NextDayKey","Advance one day (keep hour)","Следующий день (сохранить час)",VK_NUMPAD6,0,0,0,0,nullptr,0},
    {HubKey,"Debug","ClearWeatherKey","Clear weather / restore previous mode","Ясная погода / вернуть прежнюю",VK_NUMPAD3,0,0,0,0,nullptr,0},
    {HubNote,nullptr,nullptr,"For inspection: NUMPAD1 (12:00) + NUMPAD3 (clear). Time/date changes affect NPC schedules and saves. Weather restores when debugging is disabled.",
        "Для осмотра: NUMPAD1 (12:00) + NUMPAD3 (ясно). Время/дата влияют на распорядок NPC и сохраняются. Отключение отладки возвращает погоду."},
};
HubPage g_page={"world_debug",95,"World debug","Отладка мира",nullptr,kItems,sizeof(kItems)/sizeof(kItems[0]),ConfigLoad};
bool Status(char* text,int size) {
    if(!g_enabled)return false;
    Entity player;
    if(g_ownsCamera) {
        snprintf(text,size,"DEBUG CAM %.3g cm/s%s%s%s",g_camSpeed,g_weatherOwned?" CLEAR":"",g_hasSelection?" ":"",g_hasSelection?g_name:"");return true;
    }
    if(g_ready&&Player(player)&&g_hasSelection) {
        Entity e=Selected(player);
        if(e.e){snprintf(text,size,"DEBUG %s [%s]",g_name,(a.flags(&e)&FROZEN)==FROZEN?"frozen":"physics");return true;}
    }
    snprintf(text,size,"DEBUG %s%s",g_ownsCamera?"CAM ":"",g_feedback);return true;
}
const HubStatus g_status={"world_debug",95,HubIconHand,Status};
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_DEBUG_ACTION){Command(static_cast<int>(wp));return 0;}
    if(msg==WM_DEBUG_TICK){Tick();return 0;}
    if(msg==WM_DEBUG_RELOAD){ConfigLoad();return 0;}
    return CallOriginalWndProc(g_original,hwnd,msg,wp,lp);
}
struct Edge {
    int key=0;bool was=false;
    bool Pressed(int vk,bool focused){bool down=vk&&focused&&(GetAsyncKeyState(vk)&0x8000);bool fire=down&&!was&&vk==key;key=vk;was=down;return fire;}
};
struct RepeatEdge {
    Edge edge;ULONGLONG next=0;
    bool Pressed(int vk,bool focused) {
        if(vk!=edge.key)next=0;
        bool fire=edge.Pressed(vk,focused);ULONGLONG now=GetTickCount64();
        if(fire){next=now+400;return true;}
        if(!edge.was){next=0;return false;}
        if(next&&now>=next){next=now+180;return true;}
        return false;
    }
};
DWORD WINAPI Worker(void*) {
    HWND hwnd=WaitForGameWindow();g_original=SubclassWindow(hwnd,WndProc);
    if(const HubApi* hub=HubGet()){hub->registerPage(&g_page);hub->registerStatus(&g_status);}
    Edge keys[4],lightingKeys[4];RepeatEdge speedKeys[2];
    for(unsigned tick=0;IsWindow(hwnd);++tick) {
        bool focused=GetForegroundWindow()==hwnd&&!HubBusy();int bindings[]={g_select,g_physics,g_lift,g_camera};
        for(int i=0;i<4;++i)if(keys[i].Pressed(bindings[i],focused)&&g_enabled)PostMessageW(hwnd,WM_DEBUG_ACTION,i+1,0);
        int lightingBindings[]={g_timeKey,g_cycleKey,g_dayKey,g_weatherKey};
        for(int i=0;i<4;++i)if(lightingKeys[i].Pressed(lightingBindings[i],focused)&&g_enabled)PostMessageW(hwnd,WM_DEBUG_ACTION,SetHour+i,0);
        int speedBindings[]={g_camSlower,g_camFaster};
        for(int i=0;i<2;++i)if(speedKeys[i].Pressed(speedBindings[i],focused&&g_enabled&&g_ownsCamera))
            PostMessageW(hwnd,WM_DEBUG_ACTION,CameraSlower+i,0);
        if(tick%4==0)PostMessageW(hwnd,WM_DEBUG_TICK,0,0);
        if(tick%20==0&&IniChanged())PostMessageW(hwnd,WM_DEBUG_RELOAD,0,0);
        Sleep(30);
    }
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);ModInit(instance,"world_debug");Log("Risen World Debug 0.2.0");
        g_page.iniPath=IniPath(kIni);ConfigLoad();
        if(HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr))CloseHandle(thread);
    }
    return TRUE;
}
