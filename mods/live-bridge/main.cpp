// Developer-only live bridge host (TODO 7.1). No README: tools\package.ps1 never ships it.
// Named pipe \\.\pipe\RisenLive (local only, one client at a time): one JSON line in, one JSON line out,
// request/response only - the game never pushes anything. Client: `risen live <verb> [args]` (rust/risen-cli).
// Request {"cmd":"<verb>","args":["..",..]}; reply {"ok":true,...} or {"ok":false,"err":"..."}.
// The pipe thread hands each request to the game window procedure (WM_LIVE_REQUEST) and waits up to kTimeoutMs;
// all engine calls run there. Every request and its result go to RisenMods.log in one line.
// The verbs live in bin\mods\live\live_verbs.dll (mods/live-verbs). This host loads a shadow copy of it, so the
// file can be replaced while the game runs; `reload` swaps the copy on the game thread between two requests.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "common.h"

namespace {
constexpr char kVersion[]="0.6.0";
constexpr UINT WM_LIVE_REQUEST=WM_APP+0x5A01;
constexpr DWORD kTimeoutMs=2000;
constexpr int kLine=4096,kReply=32768; // request line, reply line (verbs: LiveReplySize)
const char kPipe[]="\\\\.\\pipe\\RisenLive";

// --- verbs DLL (game thread only) ----------------------------------------------------------------------------
using HandleFn=void (*)(const char*,char*,int);
using VersionFn=const char* (*)();
using SizeFn=int (*)();
HMODULE g_verbs;
HandleFn g_handle;
char g_verbsDir[MAX_PATH],g_loadedPath[MAX_PATH];
unsigned g_generation;

void Reply(char* reply,bool ok,const char* key,const char* text) {
    int n=snprintf(reply,kReply,"{\"ok\":%s,\"%s\":\"",ok?"true":"false",key);
    for(const char* p=text;*p&&n<kReply-8;++p){
        if(*p=='"'||*p=='\\')reply[n++]='\\';
        reply[n++]=static_cast<unsigned char>(*p)<0x20?' ':*p;
    }
    snprintf(reply+n,kReply-n,"\"}");
}
void Unload() {
    if(!g_verbs)return;
    FreeLibrary(g_verbs);g_verbs=nullptr;g_handle=nullptr;
    if(g_loadedPath[0]&&!DeleteFileA(g_loadedPath))Log("old copy %s not deleted (%lu)",g_loadedPath,GetLastError());
    g_loadedPath[0]=0;
}
// Copies live_verbs.dll to live_verbs.loaded-<n>.dll and loads that copy. On failure the old verbs stay.
bool Load(char* error,size_t size) {
    char source[MAX_PATH],copy[MAX_PATH];
    snprintf(source,sizeof(source),"%s\\live_verbs.dll",g_verbsDir);
    snprintf(copy,sizeof(copy),"%s\\live_verbs.loaded-%lu-%u.dll",g_verbsDir,GetCurrentProcessId(),++g_generation);
    if(!CopyFileA(source,copy,FALSE)){snprintf(error,size,"cannot copy %s (%lu)",source,GetLastError());return false;}
    HMODULE module=LoadLibraryA(copy);
    auto handle=module?reinterpret_cast<HandleFn>(GetProcAddress(module,"LiveHandle")):nullptr;
    auto version=module?reinterpret_cast<VersionFn>(GetProcAddress(module,"LiveVersion")):nullptr;
    auto replySize=module?reinterpret_cast<SizeFn>(GetProcAddress(module,"LiveReplySize")):nullptr;
    if(!handle||!version||!replySize||replySize()>kReply) {
        snprintf(error,size,"%s is not a compatible live_verbs.dll (%lu)",source,module?0ul:GetLastError());
        if(module)FreeLibrary(module);
        DeleteFileA(copy);return false;
    }
    Unload();
    g_verbs=module;g_handle=handle;snprintf(g_loadedPath,sizeof(g_loadedPath),"%s",copy);
    Log("verbs %s loaded from %s",version(),copy);
    return true;
}
// Left-over copies of earlier game sessions (the loaded one of a crashed run cannot be deleted then; later can).
void CleanCopies() {
    char pattern[MAX_PATH],path[MAX_PATH];snprintf(pattern,sizeof(pattern),"%s\\live_verbs.loaded-*.dll",g_verbsDir);
    WIN32_FIND_DATAA found;HANDLE find=FindFirstFileA(pattern,&found);if(find==INVALID_HANDLE_VALUE)return;
    do{snprintf(path,sizeof(path),"%s\\%s",g_verbsDir,found.cFileName);DeleteFileA(path);}while(FindNextFileA(find,&found));
    FindClose(find);
}
void Handle(const char* line,char* reply) {
    char error[400];
    // Only the host knows `reload`; everything else goes to the verbs DLL.
    if(strstr(line,"\"cmd\":\"reload\"")) {
        if(!Load(error,sizeof(error))){Reply(reply,false,"err",error);return;}
        auto version=reinterpret_cast<VersionFn>(GetProcAddress(g_verbs,"LiveVersion"));
        Reply(reply,true,"verbs",version());return;
    }
    if(!g_handle&&!Load(error,sizeof(error))){Reply(reply,false,"err",error);return;}
    g_handle(line,reply,kReply);
}

// --- request hand-over (pipe thread <-> game thread) ---------------------------------------------------------
// One request in flight. The pipe thread owns it until it posts; the game thread claims it
// Pending -> Running; a timed-out request is Abandoned and the late message is ignored.
enum State {Idle,Pending,Running,Done,Abandoned};
struct Request {std::atomic<int> state{Idle};char line[kLine];char reply[kReply];} g_req;
HANDLE g_done;

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
    if(!PostMessageW(hwnd,WM_LIVE_REQUEST,0,0)){g_req.state=Idle;Reply(reply,false,"err","game window gone");return;}
    if(WaitForSingleObject(g_done,kTimeoutMs)!=WAIT_OBJECT_0) {
        int pending=Pending;
        if(g_req.state.compare_exchange_strong(pending,Abandoned)){Reply(reply,false,"err","timeout: game thread busy (loading?)");return;}
        WaitForSingleObject(g_done,INFINITE); // already running: let it finish
    }
    snprintf(reply,kReply,"%s",g_req.reply);g_req.state=Idle;
}

DWORD WINAPI Worker(void*) {
    HWND hwnd=WaitForGameWindow();g_original=SubclassWindow(hwnd,WndProc);
    g_done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Log("pipe %s ready",kPipe);
    static char line[kLine],reply[kReply];
    while(IsWindow(hwnd)) {
        HANDLE pipe=CreateNamedPipeA(kPipe,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
            1,kReply,kLine,0,nullptr);
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
        char dir[MAX_PATH];DWORD n=GetModuleFileNameA(instance,dir,MAX_PATH);
        while(n&&dir[n-1]!='\\')--n;dir[n?n-1:0]=0;
        snprintf(g_verbsDir,sizeof(g_verbsDir),"%s\\live",dir);CleanCopies();
        if(HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr))CloseHandle(thread);
    }
    return TRUE;
}
