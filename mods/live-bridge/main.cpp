// Developer-only live bridge (TODO 7.1). No README: tools\package.ps1 never ships it.
// Named pipe \\.\pipe\RisenLive (local only, one client at a time): one JSON line in, one JSON line out,
// request/response only - the game never pushes anything. Client: `risen live <verb> [args]` (rust/risen-cli).
// Request {"cmd":"<verb>","args":["..",..]}; reply {"ok":true,...} or {"ok":false,"err":"..."}.
// The pipe thread hands each request to the game window procedure (WM_LIVE_REQUEST) and waits up to kTimeoutMs;
// all engine calls run there. Entities are looked up afresh on every command, no engine pointer is kept.
// Every request and its result go to RisenMods.log in one line.
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "common.h"

namespace {
constexpr char kVersion[]="0.2.0";
constexpr UINT WM_LIVE_REQUEST=WM_APP+0x5A01;
constexpr DWORD kTimeoutMs=2000;
constexpr int kLine=4096;
// Rev.2045 PE timestamps: the bridge reads fields/layouts verified only for this build.
constexpr DWORD kGameStamp=0x63c9aff6,kScriptStamp=0x63c9b014,kEngineStamp=0x63ca945d;
const char kPipe[]="\\\\.\\pipe\\RisenLive";

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
} a{};
int g_api=0; // 0 not tried, 1 ready, -1 failed
bool g_clockOk=false,g_versionOk=false;

// One request in flight. The pipe thread owns it until it posts; the game thread claims it
// Pending -> Running; a timed-out request is Abandoned and the late message is ignored.
enum State {Idle,Pending,Running,Done,Abandoned};
struct Request {std::atomic<int> state{Idle};char line[kLine];char reply[kLine];} g_req;
HANDLE g_done;

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
#undef API
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
    snprintf(reply,kLine,"{\"ok\":false,\"err\":\"");JsonEscape(reply,kLine-2,message);
    size_t n=strlen(reply);snprintf(reply+n,kLine-n,"\"}");
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
    snprintf(reply,kLine,"{\"ok\":true,\"bridge\":\"%s\",\"game\":\"%s\"}",kVersion,g_versionOk?"Rev.2045":"unknown");
}
void Status(char* reply) {
    char world[128]{},name[160]{};
    int n=snprintf(reply,kLine,"{\"ok\":true,\"world\":");
    if(WorldName(world,sizeof(world))){JsonEscape(name,sizeof(name),world);n+=snprintf(reply+n,kLine-n,"\"%s\"",name);}
    else n+=snprintf(reply+n,kLine-n,"null");
    Entity player;a.player(&player);
    if(player.e) {
        Vec3 p{},at{};a.position(&player,&p);a.at(&player,&at);
        n+=snprintf(reply+n,kLine-n,",\"hero\":{\"pos\":[%.0f,%.0f,%.0f],\"yaw\":%.0f},\"dialog\":%s",
            p.x,p.y,p.z,Yaw(at),a.dialog()?"true":"false");
    } else n+=snprintf(reply+n,kLine-n,",\"hero\":null");
    if(void* clock=WorldClock())
        n+=snprintf(reply+n,kLine-n,",\"day\":%u,\"time\":\"%02u:%02u\"",*ClockField(clock,0x2c),*ClockField(clock,0x30),*ClockField(clock,0x34));
    snprintf(reply+n,kLine-n,"}");
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
    snprintf(reply,kLine,"{\"ok\":true,\"pos\":[%.0f,%.0f,%.0f],\"yaw\":%.0f}",p.x,p.y,p.z,Yaw(at));
}
// teleport x y z [yaw]: Entity::MoveTo = world matrix + rigid body pose + navigation reset + stop movement
// (what the vanilla Entity::Teleport does). Without yaw the rotation is kept.
void Teleport(char args[][32],int count,char* reply) {
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
void Look(char args[][32],int count,char* reply) {
    if(count!=1){Error(reply,"usage: look yaw  (degrees, 0 = +Z, 90 = +X; ~90 = turn right)");return;}
    Entity player;alignas(16) float m[16];if(!HeroPose(player,m,reply))return;
    Vec3 at{};a.at(&player,&at);float yaw;
    if(!Coordinate(args[0],Yaw(at),yaw)){Error(reply,"bad yaw");return;}
    SetYaw(m,std::fmod(yaw,360.f));a.moveTo(&player,m);PoseReply(player,reply);
}
// Splits "args":["a","b",..] into fixed slots; returns the count, -1 on a malformed or too long list.
int JsonArgs(const char* json,char out[][32],int max) {
    const char* p=strstr(json,"\"args\"");if(!p)return 0;
    p=strchr(p,'[');if(!p)return -1;++p;
    int count=0;
    for(;;) {
        while(*p==' '||*p==',')++p;
        if(*p==']')return count;
        if(*p!='"'||count==max)return -1;
        size_t n=0;
        for(++p;*p&&*p!='"';++p){if(*p=='\\'&&p[1])++p;if(n+1>=32)return -1;out[count][n++]=*p;}
        if(*p!='"')return -1;
        out[count++][n]=0;++p;
    }
}
void Handle(const char* line,char* reply) {
    char cmd[32],args[8][32];
    if(!JsonString(line,"cmd",cmd,sizeof(cmd))){Error(reply,"request needs {\"cmd\":\"<verb>\"}");return;}
    int count=JsonArgs(line,args,8);
    if(count<0){Error(reply,"args must be a list of at most 8 short strings");return;}
    if(!ApiInit()){Error(reply,"game API unavailable (see RisenMods.log)");return;}
    if(!strcmp(cmd,"ping"))Ping(reply);
    else if(!strcmp(cmd,"status"))Status(reply);
    else if(!strcmp(cmd,"teleport"))Teleport(args,count,reply);
    else if(!strcmp(cmd,"look"))Look(args,count,reply);
    else Error(reply,"unknown cmd; verbs: ping, status, teleport, look");
}

WNDPROC g_original;
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_LIVE_REQUEST) {
        int expected=Pending;
        if(g_req.state.compare_exchange_strong(expected,Running)) {
            Handle(g_req.line,g_req.reply);
            g_req.state=Done;SetEvent(g_done);
        }
        return 0;
    }
    return CallOriginalWndProc(g_original,hwnd,msg,wp,lp);
}

// Pipe thread: runs one request on the game thread and fills reply.
// Only this thread starts requests, and it waits for a running one, so the state is Idle or Abandoned here.
// The late message of an abandoned request is ignored, or runs the next Pending request (same result).
void Run(HWND hwnd,const char* line,char* reply) {
    snprintf(g_req.line,sizeof(g_req.line),"%s",line);ResetEvent(g_done);
    g_req.state=Pending;
    if(!PostMessageW(hwnd,WM_LIVE_REQUEST,0,0)){g_req.state=Idle;Error(reply,"game window gone");return;}
    if(WaitForSingleObject(g_done,kTimeoutMs)!=WAIT_OBJECT_0) {
        int pending=Pending;
        if(g_req.state.compare_exchange_strong(pending,Abandoned)){Error(reply,"timeout: game thread busy (loading?)");return;}
        WaitForSingleObject(g_done,INFINITE); // already running: let it finish
    }
    snprintf(reply,kLine,"%s",g_req.reply);g_req.state=Idle;
}

DWORD WINAPI Worker(void*) {
    HWND hwnd=WaitForGameWindow();g_original=SubclassWindow(hwnd,WndProc);
    g_done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Log("pipe %s ready",kPipe);
    static char line[kLine],reply[kLine];
    while(IsWindow(hwnd)) {
        HANDLE pipe=CreateNamedPipeA(kPipe,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
            1,kLine,kLine,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE){Log("CreateNamedPipe failed: %lu; bridge stopped",GetLastError());return 0;}
        if(ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED) {
            // A client may send several lines; each gets exactly one reply line.
            size_t used=0;char chunk[512];DWORD got=0;
            while(ReadFile(pipe,chunk,sizeof(chunk),&got,nullptr)&&got) {
                for(DWORD i=0;i<got;++i) {
                    char c=chunk[i];
                    if(c=='\r')continue;
                    if(c!='\n'){if(used+1<sizeof(line))line[used++]=c;continue;}
                    line[used]=0;used=0;if(!line[0])continue;
                    ULONGLONG start=GetTickCount64();
                    Run(hwnd,line,reply);
                    Log("%s -> %.300s (%llu ms)",line,reply,GetTickCount64()-start);
                    DWORD written=0;
                    WriteFile(pipe,reply,static_cast<DWORD>(strlen(reply)),&written,nullptr);
                    WriteFile(pipe,"\n",1,&written,nullptr);
                }
            }
            FlushFileBuffers(pipe);DisconnectNamedPipe(pipe);
        }
        CloseHandle(pipe);
    }
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);ModInit(instance,"live_bridge");Log("Risen Live Bridge %s",kVersion);
        if(HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr))CloseHandle(thread);
    }
    return TRUE;
}
