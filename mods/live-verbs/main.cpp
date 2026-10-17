// Verbs of the live bridge (TODO 7.1), hot-reloadable: live_bridge.dll loads a shadow copy of
// bin\mods\live\live_verbs.dll and swaps it on `risen live reload`, so a rebuilt copy can be deployed
// while the game runs. Rules for this DLL: no hooks, threads, subclassing or registrations, and no engine pointer
// kept between calls - everything here may be unloaded after any request. All calls come on the game thread.
// Protocol, client and context rules: mods/live-bridge/main.cpp, notes/live-bridge.md.
#include <windows.h>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "common.h"

namespace {
constexpr char kVersion[]="0.6.1";
constexpr int kReply=32768,kArg=64; // reply line, one argument
// Rev.2045 PE timestamps: the bridge reads fields/layouts verified only for this build.
constexpr DWORD kGameStamp=0x63c9aff6,kScriptStamp=0x63c9b014,kEngineStamp=0x63ca945d;

struct Entity { void* e=nullptr; };
struct Vec3 {float x,y,z;};
struct Api {
    Entity* (*player)(Entity*);
    bool (*dialog)();
    Vec3* (*position)(const Entity*,Vec3*);
    Vec3* (*at)(const Entity*,Vec3*);
    void* (*pose)(const Entity*,float*);
    void (*moveTo)(Entity*,const float*);
    const char* (*stringText)(const void*);
    void (*stringFree)(void*);
    void* (*currentWorld)();
    void* (*worldEntity)(void*);
    void* (*fileBaseName)(const void*,void*);
    bool (*clockValid)(const Entity*);
    void* (*clock)(const Entity*);
    void (*setDay)(void*,const uint32_t*);
    void (*setHour)(void*,const uint32_t*);
    void (*setMinute)(void*,const uint32_t*);
    void (*setSecond)(void*,const uint32_t*);
    void (*enumEntities)(const void*,bool(*)(void*,void*),void*,unsigned long);
    bool (*item)(const Entity*);
    bool (*npc)(const Entity*);
    void* (*name)(const Entity*,void*);
    bool (*processing)(const Entity*);
    bool (*attached)(Entity*);
    Entity* (*owner)(const Entity*,Entity*);
    void* entityGetId;
    void* dynamicGetId;
    void* entityVtable;
    void* dynamicVtable;
} a{};
int g_api=0; // 0 not tried, 1 ready, -1 failed
int g_idSlot=0; // vtable slot of the virtual GetID (eCEntity / eCDynamicEntity), 0 = entity verbs disabled
bool g_clockOk=false,g_versionOk=false;

// One request in flight. The pipe thread owns it until it posts; the game thread claims it

DWORD Stamp(HMODULE m) {
    auto base=reinterpret_cast<const uint8_t*>(m);
    return reinterpret_cast<const IMAGE_NT_HEADERS*>(base+reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew)->FileHeader.TimeDateStamp;
}
template<class T> bool Resolve(HMODULE module,T& fn,const char* name) {
    fn=module?reinterpret_cast<T>(GetProcAddress(module,name)):nullptr;
    if(!fn)Log("missing API: %s",name);
    return fn!=nullptr;
}
// Retail omits the clock getters; the exported setters' field stores prove the layout (as in World Debug).
const uint32_t* ClockField(const void* clock,int offset){return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(clock)+offset);}
bool ApiInit() {
    if(g_api)return g_api>0;
    HMODULE script=GetModuleHandleA("Script.dll"),engine=GetModuleHandleA("Engine.dll"),
        game=GetModuleHandleA("Game.dll"),base=GetModuleHandleA("SharedBase.dll");
    if(!script||!engine||!game||!base)return false;
    g_versionOk=Stamp(game)==kGameStamp&&Stamp(script)==kScriptStamp&&Stamp(engine)==kEngineStamp;
    bool ok=true;
#define API(MEMBER,MODULE,SYMBOL) ok=Resolve(MODULE,a.MEMBER,SYMBOL)&&ok
    API(player,script,"?GetPlayer@Entity@@SA?AV1@XZ");
    API(dialog,script,"?IsDialogCameraActive@Entity@@SA_NXZ");
    API(position,script,"?GetPosition@Entity@@QEBA?AVbCVector@@XZ");
    API(at,script,"?GetAtVector@Entity@@QEBA?AVbCVector@@XZ");
    API(pose,script,"?GetPose@Entity@@QEBA?AVbCMatrix@@XZ");
    API(moveTo,script,"?MoveTo@Entity@@QEAAXAEBVbCMatrix@@@Z");
    API(stringText,base,"?GetText@bCString@@QEBAPEBDXZ");
    API(stringFree,base,"??1bCString@@QEAA@XZ");
    API(currentWorld,game,"?GetCurrentWorld@gCWorld@@SAPEAV1@XZ");
    API(worldEntity,game,"?GetWorldEntity@gCWorld@@QEAAPEAVeCDynamicEntity@@XZ");
    API(fileBaseName,engine,"?GetFileBaseName@eCProcessibleElement@@QEBA?AVbCString@@XZ");
    API(clockValid,script,"?IsValid@PSClock@@QEBA_NXZ");
    API(clock,script,"??CPSClock@@QEBAPEBVgCClock_PS@@XZ");
    API(setDay,game,"?SetDay@gCClock_PS@@QEAAXAEBK@Z");
    API(setHour,game,"?SetHour@gCClock_PS@@QEAAXAEBK@Z");
    API(setMinute,game,"?SetMinute@gCClock_PS@@QEAAXAEBK@Z");
    API(setSecond,game,"?SetSecond@gCClock_PS@@QEAAXAEBK@Z");
    API(enumEntities,game,"?EnumEntities@gCWorld@@QEBAXP6A_NPEAVeCEntity@@PEAX@Z1K@Z");
    API(item,script,"?IsItem@Entity@@QEBA_NXZ");
    API(npc,script,"?IsNPC@Entity@@QEBA_NXZ");
    API(name,script,"?GetName@Entity@@QEBA?AVbCString@@XZ");
    API(processing,script,"?IsProcessingRangeEntered@Entity@@QEBA_NXZ");
    API(attached,script,"?IsAttachedToSlot@PSInteraction@@QEAA_NXZ");
    API(owner,script,"?GetOwner@Entity@@QEBA?AV1@XZ");
    API(entityGetId,engine,"?GetID@eCEntity@@UEBAAEBVbCPropertyID@@XZ");
    API(dynamicGetId,engine,"?GetID@eCDynamicEntity@@UEBAAEBVbCPropertyID@@XZ");
    API(entityVtable,engine,"??_7eCEntity@@6B@");
    API(dynamicVtable,engine,"??_7eCDynamicEntity@@6B@");
#undef API
    // GetID is virtual: find its slot in both vtables (must agree) and call it through the entity's own vtable.
    if(ok) {
        auto ev=static_cast<void**>(a.entityVtable),dv=static_cast<void**>(a.dynamicVtable);
        for(int i=1;i<256&&!g_idSlot;++i)if(ev[i]==a.entityGetId&&dv[i]==a.dynamicGetId)g_idSlot=i;
        Log("GetID vtable slot: %d%s",g_idSlot,g_idSlot?"":" (not found; near/show disabled)");
    }
    // gCWorld's eCProcessibleElement base is at offset 0 (gCWorld::gCWorld calls its ctor with rcx=this);
    // GetFileBaseName copies the bCString at +0x18.
    const uint8_t nameField[]={0x48,0x8d,0x51,0x18};
    if(ok&&memcmp(reinterpret_cast<const uint8_t*>(a.fileBaseName)+9,nameField,4)){Log("unexpected GetFileBaseName code");ok=false;}
    if(ok) {
        const uint8_t day[]={0x89,0x47,0x2c},hour[]={0x89,0x47,0x30},minute[]={0x89,0x47,0x34};
        g_clockOk=!memcmp(reinterpret_cast<const uint8_t*>(a.setDay)+0x2c,day,3)&&
            !memcmp(reinterpret_cast<const uint8_t*>(a.setHour)+0x2c,hour,3)&&
            !memcmp(reinterpret_cast<const uint8_t*>(a.setMinute)+0x2c,minute,3);
    }
    g_api=ok?1:-1;
    Log("API: %s, game build %s, clock %s",ok?"resolved":"disabled",g_versionOk?"Rev.2045":"UNKNOWN",g_clockOk?"ok":"unavailable");
    return ok;
}

// --- minimal JSON: flat request objects only -------------------------------------------------------------
// Copies the string value of "key" (escapes \" \\ only) into out; false when absent.
bool JsonString(const char* json,const char* key,char* out,size_t size) {
    char pattern[64];snprintf(pattern,sizeof(pattern),"\"%s\"",key);
    const char* p=strstr(json,pattern);if(!p)return false;
    p+=strlen(pattern);while(*p==' '||*p==':')++p;
    if(*p!='"')return false;++p;
    size_t n=0;
    for(;*p&&*p!='"';++p){if(*p=='\\'&&p[1])++p;if(n+1<size)out[n++]=*p;}
    out[n]=0;return *p=='"';
}
// Appends text to out, escaped as a JSON string body.
void JsonEscape(char* out,size_t size,const char* text) {
    size_t n=strlen(out);
    for(const char* p=text;*p&&n+7<size;++p){
        unsigned char c=*p;
        if(c=='"'||c=='\\'){out[n++]='\\';out[n++]=c;}
        else if(c<0x20)n+=snprintf(out+n,size-n,"\\u%04x",c);
        else out[n++]=c;
    }
    out[n]=0;
}
void Error(char* reply,const char* message) {
    snprintf(reply,kReply,"{\"ok\":false,\"err\":\"");JsonEscape(reply,kReply-2,message);
    size_t n=strlen(reply);snprintf(reply+n,kReply-n,"\"}");
}

// --- verbs (game thread) ----------------------------------------------------------------------------------
// Y is up; yaw 0 = +Z, 90 = +X (atan2 of the horizontal at-vector; confirmed by walking, 2026-10-17).
float Yaw(const Vec3& at){float yaw=std::atan2(at.x,at.z)*57.2957795f;return yaw<0?yaw+360.f:yaw;}
bool WorldName(char* out,size_t size) {
    void* world=a.currentWorld();if(!world)return false;
    void* text=nullptr;a.fileBaseName(world,&text);
    const char* name=text?a.stringText(&text):nullptr;
    snprintf(out,size,"%s",name?name:"");a.stringFree(&text);
    return out[0]!=0;
}
void* WorldClock() {
    if(!g_clockOk)return nullptr;
    void* world=a.currentWorld();if(!world)return nullptr;
    Entity entity{a.worldEntity(world)};
    return entity.e&&a.clockValid(&entity)?a.clock(&entity):nullptr;
}
void Ping(char* reply) {
    snprintf(reply,kReply,"{\"ok\":true,\"verbs\":\"%s\",\"game\":\"%s\"}",kVersion,g_versionOk?"Rev.2045":"unknown");
}
void Status(char* reply) {
    char world[128]{},name[160]{};
    int n=snprintf(reply,kReply,"{\"ok\":true,\"world\":");
    if(WorldName(world,sizeof(world))){JsonEscape(name,sizeof(name),world);n+=snprintf(reply+n,kReply-n,"\"%s\"",name);}
    else n+=snprintf(reply+n,kReply-n,"null");
    Entity player;a.player(&player);
    if(player.e) {
        Vec3 p{},at{};a.position(&player,&p);a.at(&player,&at);
        n+=snprintf(reply+n,kReply-n,",\"hero\":{\"pos\":[%.0f,%.0f,%.0f],\"yaw\":%.0f},\"dialog\":%s",
            p.x,p.y,p.z,Yaw(at),a.dialog()?"true":"false");
    } else n+=snprintf(reply+n,kReply-n,",\"hero\":null");
    if(void* clock=WorldClock())
        n+=snprintf(reply+n,kReply-n,",\"day\":%u,\"time\":\"%02u:%02u\"",*ClockField(clock,0x2c),*ClockField(clock,0x30),*ClockField(clock,0x34));
    snprintf(reply+n,kReply-n,"}");
}
// "123" absolute, "~-50" relative to base, "~" = base. False when not a finite number.
bool Coordinate(const char* text,float base,float& out) {
    bool relative=*text=='~';if(relative)++text;
    if(!*text&&!relative)return false;
    char* end=nullptr;float v=*text?std::strtof(text,&end):0.f;
    if(*text&&(!end||*end))return false;
    out=relative?base+v:v;return std::isfinite(out)&&std::fabs(out)<1e6f;
}
// Hero pose (row-major bCMatrix: X axis +0x00, Z axis = at +0x20, translation +0x30), cross-checked against
// GetPosition so a different matrix layout refuses instead of teleporting somewhere random.
bool HeroPose(Entity& player,float* m,char* reply) {
    a.player(&player);
    if(!player.e){Error(reply,"no hero: load a game first");return false;}
    if(a.dialog()){Error(reply,"dialog camera active");return false;}
    Vec3 p{};a.position(&player,&p);a.pose(&player,m);
    if(std::fabs(m[12]-p.x)>5.f||std::fabs(m[13]-p.y)>5.f||std::fabs(m[14]-p.z)>5.f) {
        Log("pose/position mismatch: %.1f %.1f %.1f vs %.1f %.1f %.1f",m[12],m[13],m[14],p.x,p.y,p.z);
        Error(reply,"unexpected pose layout; refused (see log)");return false;
    }
    return true;
}
void SetYaw(float* m,float yaw) {
    // Upright hero: Y axis stays up, at = (sin, 0, cos), right = (cos, 0, -sin) (left-handed, Y up).
    float r=yaw*0.0174532925f,s=std::sin(r),c=std::cos(r);
    const float rows[12]={c,0,-s,0, 0,1,0,0, s,0,c,0};
    memcpy(m,rows,sizeof(rows));
}
void PoseReply(Entity& player,char* reply) {
    Vec3 p{},at{};a.position(&player,&p);a.at(&player,&at);
    snprintf(reply,kReply,"{\"ok\":true,\"pos\":[%.0f,%.0f,%.0f],\"yaw\":%.0f}",p.x,p.y,p.z,Yaw(at));
}
// teleport x y z [yaw]: Entity::MoveTo = world matrix + rigid body pose + navigation reset + stop movement
// (what the vanilla Entity::Teleport does). Without yaw the rotation is kept.
void Teleport(char args[][kArg],int count,char* reply) {
    if(count<3||count>4){Error(reply,"usage: teleport x y z [yaw]  (~ = relative, e.g. ~0 ~200 ~0)");return;}
    Entity player;alignas(16) float m[16];if(!HeroPose(player,m,reply))return;
    float x,y,z;
    if(!Coordinate(args[0],m[12],x)||!Coordinate(args[1],m[13],y)||!Coordinate(args[2],m[14],z)){Error(reply,"bad coordinate");return;}
    if(count==4) {
        Vec3 at{};a.at(&player,&at);float yaw;
        if(!Coordinate(args[3],Yaw(at),yaw)){Error(reply,"bad yaw");return;}
        SetYaw(m,yaw);
    }
    m[12]=x;m[13]=y;m[14]=z;a.moveTo(&player,m);PoseReply(player,reply);
}
// look <yaw|~delta>: turn the hero in place.
void Look(char args[][kArg],int count,char* reply) {
    if(count!=1){Error(reply,"usage: look yaw  (degrees, 0 = +Z, 90 = +X; ~90 = turn right)");return;}
    Entity player;alignas(16) float m[16];if(!HeroPose(player,m,reply))return;
    Vec3 at{};a.at(&player,&at);float yaw;
    if(!Coordinate(args[0],Yaw(at),yaw)){Error(reply,"bad yaw");return;}
    SetYaw(m,std::fmod(yaw,360.f));a.moveTo(&player,m);PoseReply(player,reply);
}
// --- entities: gCWorld::EnumEntities walks every layer of the enabled sectors (flag 1), statics included ----
// Pointers from the walk live only for this command.
const uint8_t* EntityId(void* entity){return static_cast<const uint8_t*>(reinterpret_cast<void*(*)(void*)>((*static_cast<void***>(entity))[g_idSlot])(entity));}
void FormatGuid(const uint8_t* b,char* out,size_t size) {
    uint32_t d1;uint16_t d2,d3;memcpy(&d1,b,4);memcpy(&d2,b+4,2);memcpy(&d3,b+6,2);
    snprintf(out,size,"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",d1,d2,d3,b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
}
// Same text form as risen-formats format_guid; braces optional, case ignored.
bool ParseGuid(const char* text,uint8_t* b) {
    if(*text=='{')++text;
    unsigned v[11];
    if(sscanf_s(text,"%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],&v[10])!=11)return false;
    uint32_t d1=v[0];uint16_t d2=static_cast<uint16_t>(v[1]),d3=static_cast<uint16_t>(v[2]);
    memcpy(b,&d1,4);memcpy(b+4,&d2,2);memcpy(b+6,&d3,2);
    for(int i=0;i<8;++i)b[8+i]=static_cast<uint8_t>(v[3+i]);
    return true;
}
// Equipment / inventory: an item with an owner (Entity::GetOwner), or attached to a slot.
bool Carried(Entity& e){if(!a.item(&e))return false;if(a.attached(&e))return true;Entity o;a.owner(&e,&o);return o.e!=nullptr;}
const char* Kind(Entity& e){return a.npc(&e)?"npc":a.item(&e)?"item":"object";}
void EntityName(Entity& e,char* out,size_t size) {
    void* text=nullptr;a.name(&e,&text);
    snprintf(out,size,"%s",text?a.stringText(&text):"");a.stringFree(&text);
}
bool Contains(const char* text,const char* part) {
    for(;*text;++text){size_t i=0;while(part[i]&&tolower(static_cast<unsigned char>(text[i]))==tolower(static_cast<unsigned char>(part[i])))++i;if(!part[i])return true;}
    return !*part;
}
// "--key value" among args; nullptr when absent.
const char* Option(char args[][kArg],int count,const char* key) {
    for(int i=0;i+1<count;++i)if(!strcmp(args[i],key))return args[i+1];
    return nullptr;
}
constexpr int kNearMax=100;
struct NearScan {
    Vec3 center;float radius2;const char* kind;const char* name;void* skip;bool all;
    int scanned=0,matched=0,count=0,limit=20;
    struct Hit{void* e;float d2;} hits[kNearMax];
};
bool NearVisit(void* entity,void* context) {
    auto& s=*static_cast<NearScan*>(context);++s.scanned;
    if(!entity||entity==s.skip)return true;
    Entity e{entity};Vec3 p{};a.position(&e,&p);
    float dx=p.x-s.center.x,dy=p.y-s.center.y,dz=p.z-s.center.z,d2=dx*dx+dy*dy+dz*dz;
    if(!(d2<=s.radius2))return true;
    if(s.kind&&strcmp(Kind(e),s.kind))return true;
    // Default: what is actually in the world around the hero. Loaded but not spawned entities (e.g. of
    // disabled sectors) are outside the processing range; carried items are equipment/inventory.
    if(!s.all&&(!a.processing(&e)||Carried(e)))return true;
    if(s.name){char name[128];EntityName(e,name,sizeof(name));if(!Contains(name,s.name))return true;}
    ++s.matched;
    // Keep the nearest `limit` hits, sorted by distance.
    int i;
    if(s.count<s.limit)i=s.count++;
    else if(s.hits[s.limit-1].d2<=d2)return true;
    else i=s.limit-1;
    while(i>0&&s.hits[i-1].d2>d2){s.hits[i]=s.hits[i-1];--i;}
    s.hits[i]={entity,d2};
    return true;
}
// near [--radius cm] [--limit n] [--kind item|npc|object] [--name part] [--all]: entities around the hero,
// nearest first; --all includes entities outside the processing range and carried items.
void Near(char args[][kArg],int count,char* reply) {
    Entity player;a.player(&player);if(!player.e){Error(reply,"no hero: load a game first");return;}
    void* world=a.currentWorld();if(!world){Error(reply,"no world");return;}
    static NearScan s;s=NearScan{};
    float radius=2000.f;
    if(const char* r=Option(args,count,"--radius"))radius=std::strtof(r,nullptr);
    if(const char* l=Option(args,count,"--limit"))s.limit=atoi(l);
    if(!(radius>0&&radius<=100000.f)||s.limit<1||s.limit>kNearMax){Error(reply,"usage: near [--radius 1..100000 cm] [--limit 1..100] [--kind item|npc|object] [--name part] [--all]");return;}
    s.kind=Option(args,count,"--kind");s.name=Option(args,count,"--name");s.skip=player.e;
    for(int i=0;i<count;++i)if(!strcmp(args[i],"--all"))s.all=true;
    if(s.kind&&strcmp(s.kind,"item")&&strcmp(s.kind,"npc")&&strcmp(s.kind,"object")){Error(reply,"--kind is item, npc or object");return;}
    a.position(&player,&s.center);s.radius2=radius*radius;
    a.enumEntities(world,NearVisit,&s,1);
    int n=snprintf(reply,kReply,"{\"ok\":true,\"scanned\":%d,\"matched\":%d,\"entities\":[",s.scanned,s.matched);
    for(int i=0;i<s.count&&n<kReply-512;++i) {
        Entity e{s.hits[i].e};Vec3 p{};a.position(&e,&p);
        char guid[40],name[128],escaped[256]{};FormatGuid(EntityId(e.e),guid,sizeof(guid));
        EntityName(e,name,sizeof(name));JsonEscape(escaped,sizeof(escaped),name);
        n+=snprintf(reply+n,kReply-n,"%s{\"guid\":\"%s\",\"name\":\"%s\",\"kind\":\"%s\",\"pos\":[%.0f,%.0f,%.0f],\"d\":%.0f}",
            i?",":"",guid,escaped,Kind(e),p.x,p.y,p.z,std::sqrt(s.hits[i].d2));
    }
    snprintf(reply+n,kReply-n,"]}");
}
struct FindScan {uint8_t id[16];void* found=nullptr;};
bool FindVisit(void* entity,void* context) {
    auto& s=*static_cast<FindScan*>(context);
    if(entity&&!memcmp(EntityId(entity),s.id,16)){s.found=entity;return false;}
    return true;
}
// The entity with this GUID in the current world (enabled sectors), or null.
void* FindEntity(const char* guid,char* reply) {
    FindScan s;
    if(!ParseGuid(guid,s.id)){Error(reply,"bad GUID, expected {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}");return nullptr;}
    void* world=a.currentWorld();if(!world){Error(reply,"no world");return nullptr;}
    a.enumEntities(world,FindVisit,&s,1);
    if(!s.found)Error(reply,"no entity with this GUID in the enabled sectors");
    return s.found;
}
// show <guid>: one entity in more detail (rotation rows, dynamic or static, distance to the hero).
void Show(char args[][kArg],int count,char* reply) {
    if(count!=1){Error(reply,"usage: show <guid>");return;}
    void* found=FindEntity(args[0],reply);if(!found)return;
    Entity e{found},player;a.player(&player);
    alignas(16) float m[16];a.pose(&e,m);
    Vec3 p{},at{},h{};a.position(&e,&p);a.at(&e,&at);
    char guid[40],name[128],escaped[256]{};FormatGuid(EntityId(found),guid,sizeof(guid));
    EntityName(e,name,sizeof(name));JsonEscape(escaped,sizeof(escaped),name);
    bool dynamic=*static_cast<void**>(found)==a.dynamicVtable;
    int n=snprintf(reply,kReply,"{\"ok\":true,\"guid\":\"%s\",\"name\":\"%s\",\"kind\":\"%s\",\"dynamic\":%s,\"processing\":%s,\"carried\":%s,\"pos\":[%.1f,%.1f,%.1f],\"yaw\":%.1f,"
        "\"rot\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]",guid,escaped,Kind(e),dynamic?"true":"false",a.processing(&e)?"true":"false",Carried(e)?"true":"false",p.x,p.y,p.z,Yaw(at),
        m[0],m[1],m[2],m[4],m[5],m[6],m[8],m[9],m[10]);
    if(player.e){a.position(&player,&h);float dx=p.x-h.x,dy=p.y-h.y,dz=p.z-h.z;n+=snprintf(reply+n,kReply-n,",\"d\":%.0f",std::sqrt(dx*dx+dy*dy+dz*dz));}
    snprintf(reply+n,kReply-n,"}");
}
// Entity to move/rotate: spawned (processing range), not the hero (teleport), not carried, within 100 m.
bool Editable(const char* guid,Entity& e,float* m,char* reply) {
    Entity player;a.player(&player);if(!player.e){Error(reply,"no hero: load a game first");return false;}
    e.e=FindEntity(guid,reply);if(!e.e)return false;
    if(e.e==player.e){Error(reply,"that is the hero: use teleport / look");return false;}
    if(Carried(e)){Error(reply,"carried item (inventory/equipment); refused");return false;}
    if(!a.processing(&e)){Error(reply,"not spawned (outside the processing range); refused");return false;}
    Vec3 p{},h{};a.position(&e,&p);a.position(&player,&h);
    float dx=p.x-h.x,dy=p.y-h.y,dz=p.z-h.z;
    if(!(dx*dx+dy*dy+dz*dz<=10000.f*10000.f)){Error(reply,"more than 100 m from the hero; refused");return false;}
    a.pose(&e,m);
    if(std::fabs(m[12]-p.x)>5.f||std::fabs(m[13]-p.y)>5.f||std::fabs(m[14]-p.z)>5.f){Error(reply,"unexpected pose layout; refused");return false;}
    return true;
}
void EditReply(Entity& e,char* reply) {
    Vec3 p{},at{};a.position(&e,&p);a.at(&e,&at);
    bool dynamic=*static_cast<void**>(e.e)==a.dynamicVtable;
    snprintf(reply,kReply,"{\"ok\":true,\"pos\":[%.0f,%.0f,%.0f],\"yaw\":%.0f,\"dynamic\":%s}",p.x,p.y,p.z,Yaw(at),dynamic?"true":"false");
}
// move <guid> x y z (~ = relative to the entity): Entity::MoveTo, rotation kept.
void Move(char args[][kArg],int count,char* reply) {
    if(count!=4){Error(reply,"usage: move <guid> x y z  (~ = relative, e.g. ~0 ~50 ~0)");return;}
    Entity e;alignas(16) float m[16];if(!Editable(args[0],e,m,reply))return;
    float x,y,z;
    if(!Coordinate(args[1],m[12],x)||!Coordinate(args[2],m[13],y)||!Coordinate(args[3],m[14],z)){Error(reply,"bad coordinate");return;}
    m[12]=x;m[13]=y;m[14]=z;a.moveTo(&e,m);EditReply(e,reply);
}
// rotate <guid> <yaw|~delta>: turn around the world Y axis; tilt/roll and scale of the axes are kept.
void Rotate(char args[][kArg],int count,char* reply) {
    if(count!=2){Error(reply,"usage: rotate <guid> yaw  (degrees, ~90 = turn right)");return;}
    Entity e;alignas(16) float m[16];if(!Editable(args[0],e,m,reply))return;
    Vec3 at{};a.at(&e,&at);float now=Yaw(at),target;
    if(!Coordinate(args[1],now,target)){Error(reply,"bad yaw");return;}
    // Same convention as SetYaw: (x, z) -> (x cos d + z sin d, -x sin d + z cos d) for every axis row.
    float d=(target-now)*0.0174532925f,s=std::sin(d),c=std::cos(d);
    for(int row=0;row<3;++row){float* v=m+row*4;float x=v[0],z=v[2];v[0]=x*c+z*s;v[2]=-x*s+z*c;}
    a.moveTo(&e,m);EditReply(e,reply);
}
// time HH:MM: world clock through the exported setters (saved with the game; affects NPC schedules).
void Time(char args[][kArg],int count,char* reply) {
    unsigned hour=0,minute=0;
    if(count!=1||sscanf_s(args[0],"%u:%u",&hour,&minute)!=2||hour>23||minute>59){Error(reply,"usage: time HH:MM");return;}
    void* clock=WorldClock();if(!clock){Error(reply,"no world clock: load a game first");return;}
    uint32_t h=hour,m=minute,sec=0;a.setHour(clock,&h);a.setMinute(clock,&m);a.setSecond(clock,&sec);
    snprintf(reply,kReply,"{\"ok\":true,\"day\":%u,\"time\":\"%02u:%02u\"}",*ClockField(clock,0x2c),*ClockField(clock,0x30),*ClockField(clock,0x34));
}
// weather on|off|list: World Debug owns the forced weather (exports RisenDebugClearWeather / RisenDebugWeatherList).
void Weather(char args[][kArg],int count,char* reply) {
    HMODULE debug=GetModuleHandleA("world_debug.dll");
    using ClearFn=int(*)(int,char*,int);using ListFn=bool(*)(char*,int);using ForceFn=int(*)(const char*,char*,int);
    auto clear=debug?reinterpret_cast<ClearFn>(GetProcAddress(debug,"RisenDebugClearWeather")):nullptr;
    auto list=debug?reinterpret_cast<ListFn>(GetProcAddress(debug,"RisenDebugWeatherList")):nullptr;
    auto force=debug?reinterpret_cast<ForceFn>(GetProcAddress(debug,"RisenDebugForceWeather")):nullptr;
    if(!clear||!list||!force){Error(reply,"needs world_debug.dll 0.2.2+");return;}
    if(count!=1){Error(reply,"usage: weather on|off|list|<state>  (on = forced state, default Sunny_Full; off = native weather)");return;}
    if(!strcmp(args[0],"list")) {
        int n=snprintf(reply,kReply,"{\"ok\":true,\"weather\":");
        if(!list(reply+n,kReply-n-2)){Error(reply,"weather unavailable: load a game first");return;}
        n=static_cast<int>(strlen(reply));snprintf(reply+n,kReply-n,"}");return;
    }
    bool on=strcmp(args[0],"off")!=0;
    char message[180]{};
    int state=!strcmp(args[0],"on")||!on?clear(on?1:0,message,sizeof(message)):force(args[0],message,sizeof(message));
    if(state<0){Error(reply,message);return;}
    char escaped[400]{};JsonEscape(escaped,sizeof(escaped),message);
    snprintf(reply,kReply,"{\"ok\":%s,\"forced\":%s,\"msg\":\"%s\"}",(state==1)==on?"true":"false",state==1?"true":"false",escaped);
}
// Splits "args":["a","b",..] into fixed slots; returns the count, -1 on a malformed or too long list.
int JsonArgs(const char* json,char out[][kArg],int max) {
    const char* p=strstr(json,"\"args\"");if(!p)return 0;
    p=strchr(p,'[');if(!p)return -1;++p;
    int count=0;
    for(;;) {
        while(*p==' '||*p==',')++p;
        if(*p==']')return count;
        if(*p!='"'||count==max)return -1;
        size_t n=0;
        for(++p;*p&&*p!='"';++p){if(*p=='\\'&&p[1])++p;if(n+1>=kArg)return -1;out[count][n++]=*p;}
        if(*p!='"')return -1;
        out[count++][n]=0;++p;
    }
}
void Handle(const char* line,char* reply) {
    char cmd[32],args[8][kArg];
    if(!JsonString(line,"cmd",cmd,sizeof(cmd))){Error(reply,"request needs {\"cmd\":\"<verb>\"}");return;}
    int count=JsonArgs(line,args,8);
    if(count<0){Error(reply,"args must be a list of at most 8 short strings");return;}
    if(!ApiInit()){Error(reply,"game API unavailable (see RisenMods.log)");return;}
    if(!strcmp(cmd,"ping"))Ping(reply);
    else if(!strcmp(cmd,"status"))Status(reply);
    else if(!strcmp(cmd,"teleport"))Teleport(args,count,reply);
    else if(!strcmp(cmd,"look"))Look(args,count,reply);
    else if(!strcmp(cmd,"near")||!strcmp(cmd,"show")||!strcmp(cmd,"move")||!strcmp(cmd,"rotate")) {
        if(!g_idSlot){Error(reply,"entity API unavailable (see RisenMods.log)");return;}
        if(!strcmp(cmd,"near"))Near(args,count,reply);
        else if(!strcmp(cmd,"show"))Show(args,count,reply);
        else if(!strcmp(cmd,"move"))Move(args,count,reply);
        else Rotate(args,count,reply);
    }
    else if(!strcmp(cmd,"time"))Time(args,count,reply);
    else if(!strcmp(cmd,"weather"))Weather(args,count,reply);
    else Error(reply,"unknown cmd; verbs: ping, status, teleport, look, near, show, move, rotate, time, weather");
}
}

extern "C" __declspec(dllexport) const char* LiveVersion(){return kVersion;}
// One request line -> one reply line (JSON). size must be at least LiveReplySize().
extern "C" __declspec(dllexport) int LiveReplySize(){return kReply;}
extern "C" __declspec(dllexport) void LiveHandle(const char* line,char* reply,int size){
    if(size<kReply){snprintf(reply,size,"{\"ok\":false,\"err\":\"reply buffer too small\"}");return;}
    Handle(line,reply);
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    // Shadow copies live in bin\mods\live: log to the shared bin\mods\RisenMods.log.
    if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(instance);ModInitIn(instance,"live_verbs","..");}
    return TRUE;
}
