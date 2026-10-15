// Developer controls. All engine calls run in the game's window procedure; only GUID values
// (never entity/property-set pointers) survive a command or tick.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "common.h"
#include "hub_client.h"

namespace {
struct Entity { void* e=nullptr; };
struct Vec3 {float x,y,z;};
// SharedBase copy constructor/operator= copy 16 core bytes and clear the 4-byte hash cache.
struct PropertyId {uint8_t core[16]{};uint32_t hash=0;};
static_assert(sizeof(PropertyId)==20);
constexpr uint32_t FROZEN=0x7e,KINEMATIC=0x80,NO_GRAVITY=1;
constexpr UINT WM_DEBUG_ACTION=WM_APP+0x5901,WM_DEBUG_TICK=WM_APP+0x5902,WM_DEBUG_RELOAD=WM_APP+0x5903;
enum Action {Select=1,Physics,Lift,Camera};
WNDPROC g_original;
PropertyId g_selected;
bool g_hasSelection=false,g_ownsCamera=false,g_ready=false;
char g_name[128]{},g_feedback[180]="Select a loose item: NUMPAD7";
bool g_enabled=false;
int g_select=VK_NUMPAD7,g_physics=VK_NUMPAD5,g_lift=VK_NUMPAD9,g_camera=VK_NUMPAD0;
constexpr float CAMERA_DEFAULT=30.f,CAMERA_MIN=.1f,CAMERA_MAX=1000.f;
float g_liftCm=100.f,g_camSpeed=CAMERA_DEFAULT;
const char kIni[]=
    "; Developer tools. Object changes can be stored by a normal game save.\r\n"
    "[Debug]\r\nEnabled=0\r\nSelectKey=NUMPAD7\r\nPhysicsKey=NUMPAD5\r\nLiftKey=NUMPAD9\r\nFreecamKey=NUMPAD0\r\nLiftCm=100\r\nFreecamSpeed=30\r\n";

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
} a{};

template<class T> bool Resolve(HMODULE module,T& fn,const char* name) {
    fn=module?reinterpret_cast<T>(GetProcAddress(module,name)):nullptr;
    if (!fn) Log("missing API: %s",name);
    return fn!=nullptr;
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
    API(cameraActive,game,"?GetDebugMode@gCCameraAdmin@@QEBA_NXZ");
    API(toggleCamera,game,"?OnToggleFreeFlyCam@gCCameraAdmin@@QEAAXXZ");
    API(cameraSpeed,game,"?SetFreeCamSpeed@gCCameraAdmin@@QEAAXM@Z");
    API(stop,engine,"?StopMotion@eCRigidBody_PS@@UEAAXXZ");
    API(enablePhysics,engine,"?SetPhysicsEnabled@eCRigidBodyBase_PS@@QEAAXAEB_N@Z");
    API(wakeCounter,engine,"?SetWakeUpCounter@eCRigidBody_PS@@QEAAXAEBM@Z");
    API(sleep,engine,"?SetIsSleeping@eCRigidBody_PS@@UEAAX_N@Z");
#undef API
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
void Command(int action) {
    if(!g_enabled||HubBusy()||!ApiInit())return;
    Entity player;if(!Player(player)){Feedback("Load a game and leave the dialog first");return;}
    if(action==Select){SelectItem(player);return;}
    if(action==Camera) {
        void* camera=Module(a.cameraVtable);if(!camera){Feedback("Camera module unavailable");return;}
        bool was=a.cameraActive(camera);a.toggleCamera(camera);g_ownsCamera=!was;
        if(!was)a.cameraSpeed(camera,g_camSpeed);
        Feedback(was?"Freecam OFF":"Freecam ON (native camera controls)");return;
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
    if(!g_enabled||!Player(player)){ReturnCamera();if(!player.e)g_hasSelection=false;return;}
    if(g_ownsCamera){void* camera=Module(a.cameraVtable);if(!camera||!a.cameraActive(camera))g_ownsCamera=false;}
}
void ConfigLoad() {
    bool enabled=IniInt("Debug","Enabled",0)!=0;
    if(!enabled&&g_enabled)ReturnCamera();g_enabled=enabled;
    g_select=IniKey("Debug","SelectKey",VK_NUMPAD7);g_physics=IniKey("Debug","PhysicsKey",VK_NUMPAD5);
    g_lift=IniKey("Debug","LiftKey",VK_NUMPAD9);g_camera=IniKey("Debug","FreecamKey",VK_NUMPAD0);
    g_liftCm=IniFloat("Debug","LiftCm",100.f);if(!std::isfinite(g_liftCm)||g_liftCm<1.f||g_liftCm>500.f)g_liftCm=100.f;
    g_camSpeed=IniFloat("Debug","FreecamSpeed",CAMERA_DEFAULT);
    if(!std::isfinite(g_camSpeed)||g_camSpeed<CAMERA_MIN||g_camSpeed>CAMERA_MAX)g_camSpeed=CAMERA_DEFAULT;
    // F10 callbacks and INI reloads run on the game thread. Apply immediately to an owned
    // freecam too: previously the slider only took effect after toggling the camera again.
    if(g_ready&&g_ownsCamera) {
        void* camera=Module(a.cameraVtable);
        if(camera&&a.cameraActive(camera))a.cameraSpeed(camera,g_camSpeed);
    }
    Log("config: enabled=%d select=0x%X physics=0x%X lift=0x%X freecam=0x%X speed=%.1f cm/s",g_enabled,g_select,g_physics,g_lift,g_camera,g_camSpeed);
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
    {HubSlider,"Debug","FreecamSpeed","Camera speed","Скорость камеры",CAMERA_DEFAULT,CAMERA_MIN,CAMERA_MAX,.1f,1,"%.1f cm/s",0,
        "Applies immediately. Ctrl+click the number to enter a precise speed.",
        "Применяется сразу. Ctrl+клик по числу — ввести точную скорость."},
    {HubNote,nullptr,nullptr,"Existing rigid-body items only. Saving can retain physics/position changes.",
        "Только предметы с физическим телом. Сохранение может запомнить изменения физики и положения."},
    {HubNote,nullptr,nullptr,"Select before flying away. Physics keys keep that item selected (20 m from hero).",
        "Выберите предмет перед полётом. Клавиши физики работают с ним в пределах 20 м от героя."},
};
HubPage g_page={"world_debug",95,"World debug","Отладка мира",nullptr,kItems,sizeof(kItems)/sizeof(kItems[0]),ConfigLoad};
bool Status(char* text,int size) {
    if(!g_enabled)return false;
    Entity player;
    if(g_ready&&Player(player)&&g_hasSelection) {
        Entity e=Selected(player);
        if(e.e){snprintf(text,size,"DEBUG %s%s [%s]",g_ownsCamera?"CAM ":"",g_name,(a.flags(&e)&FROZEN)==FROZEN?"frozen":"physics");return true;}
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
DWORD WINAPI Worker(void*) {
    HWND hwnd=WaitForGameWindow();g_original=SubclassWindow(hwnd,WndProc);
    if(const HubApi* hub=HubGet()){hub->registerPage(&g_page);hub->registerStatus(&g_status);}
    Edge keys[4];
    for(unsigned tick=0;IsWindow(hwnd);++tick) {
        bool focused=GetForegroundWindow()==hwnd&&!HubBusy();int bindings[]={g_select,g_physics,g_lift,g_camera};
        for(int i=0;i<4;++i)if(keys[i].Pressed(bindings[i],focused)&&g_enabled)PostMessageW(hwnd,WM_DEBUG_ACTION,i+1,0);
        if(tick%4==0)PostMessageW(hwnd,WM_DEBUG_TICK,0,0);
        if(tick%20==0&&IniChanged())PostMessageW(hwnd,WM_DEBUG_RELOAD,0,0);
        Sleep(30);
    }
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);ModInit(instance,"world_debug");Log("Risen World Debug 0.1.1");
        g_page.iniPath=IniPath(kIni);ConfigLoad();
        if(HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr))CloseHandle(thread);
    }
    return TRUE;
}
