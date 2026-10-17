// Regression checks of the actual command/config/tick code with fake game APIs.
// Never loads the game or changes its files. argv[1] is a scratch base path for INI/log.
#include "../mods/world-debug/main.cpp"
#include <cstdlib>

namespace {
struct CameraState {void* vtable;bool active=false;float speed=0;} cameraState{};
struct ClockState {uint32_t year=12,day=42,hour=4,minute=30,second=10;} clockState;
struct WeatherState {const char* name;float weight;};
struct WeatherAdmin {void* vtable;int mode=0,count=3;WeatherState states[3]={{"Sunny_Full",0},{"Stormy",.25f},{"Foggy",.75f}};} weatherState;
bool hasWorld=true;
void* sceneState[1]{};
bool inDialog=false,hasPlayer=true;
int toggles=0,speedWrites=0;
Entity* TestPlayer(Entity* out){out->e=hasPlayer?reinterpret_cast<void*>(1):nullptr;return out;}
bool TestDialog(){return inDialog;}
void* TestAdmin(){return reinterpret_cast<void*>(1);}
int TestCount(const void*){return 3;}
void* TestModule(const void*,int index){return index==2?static_cast<void*>(&weatherState):index?static_cast<void*>(&cameraState):static_cast<void*>(sceneState);}
void* TestWorld(){return hasWorld?&clockState:nullptr;}
void* TestWorldEntity(void* p){return p;}
bool TestClockValid(const Entity* e){return e->e!=nullptr;}
void* TestClock(const Entity* e){return e->e;}
const uint32_t* TestYear(const void* p){return &static_cast<const ClockState*>(p)->year;}
const uint32_t* TestDay(const void* p){return &static_cast<const ClockState*>(p)->day;}
const uint32_t* TestHour(const void* p){return &static_cast<const ClockState*>(p)->hour;}
const uint32_t* TestMinute(const void* p){return &static_cast<const ClockState*>(p)->minute;}
void TestSetYear(void* p,const uint32_t* v){static_cast<ClockState*>(p)->year=*v;}
void TestSetDay(void* p,const uint32_t* v){static_cast<ClockState*>(p)->day=*v;}
void TestSetHour(void* p,const uint32_t* v){static_cast<ClockState*>(p)->hour=*v;}
void TestSetMinute(void* p,const uint32_t* v){static_cast<ClockState*>(p)->minute=*v;}
void TestSetSecond(void* p,const uint32_t* v){static_cast<ClockState*>(p)->second=*v;}
int TestMode(const void* p){return static_cast<const WeatherAdmin*>(p)->mode;}
void TestSetMode(void* p,int mode){static_cast<WeatherAdmin*>(p)->mode=mode;}
int TestWeatherCount(const void* p){return static_cast<const WeatherAdmin*>(p)->count;}
void* TestWeatherAt(const void*,int i){return &weatherState.states[i];}
const void* TestWeatherName(const void* p){return static_cast<const WeatherState*>(p)->name;}
const char* TestString(const void* p){return static_cast<const char*>(p);}
float TestWeight(const void* p){return static_cast<const WeatherState*>(p)->weight;}
void TestSetWeight(void* p,float value){static_cast<WeatherState*>(p)->weight=value;}
bool TestActive(const void* ptr){return static_cast<const CameraState*>(ptr)->active;}
void TestToggle(void* ptr){auto c=static_cast<CameraState*>(ptr);c->active=!c->active;++toggles;}
void TestSpeed(void* ptr,float value){static_cast<CameraState*>(ptr)->speed=value;++speedWrites;}
int checks=0;
void Check(bool condition,const char* what){++checks;if(!condition){fprintf(stderr,"FAIL: %s\n",what);exit(1);}}
}
int main(int argc,char** argv) {
    if(argc!=2){fprintf(stderr,"Pass a scratch base path under build/tests\n");return 2;}
    ModInitAt(argv[1]);IniPath(kIni);
    IniWriteString("Debug","Enabled","1");IniWriteFloat("Debug","FreecamSpeed",300);
    IniWriteString("Debug","FreecamSlowerKey","-");IniWriteString("Debug","FreecamFasterKey","=");
    a.player=TestPlayer;a.dialog=TestDialog;a.moduleAdmin=TestAdmin;
    a.moduleCount=TestCount;a.moduleAt=TestModule;
    a.sceneVtable=reinterpret_cast<void*>(2);sceneState[0]=a.sceneVtable;
    a.cameraVtable=reinterpret_cast<void*>(3);cameraState.vtable=a.cameraVtable;
    a.cameraActive=TestActive;a.toggleCamera=TestToggle;a.cameraSpeed=TestSpeed;
    g_ready=true;g_rotationReady=true;ConfigLoad();
    Check(g_camSlower==VK_OEM_MINUS&&g_camFaster==VK_OEM_PLUS,"top-row - and = INI bindings");
    Check(ParseKey("_")==VK_OEM_MINUS&&ParseKey("+")==VK_OEM_PLUS,"shifted top-row aliases");
    Command(CameraFaster);Check(speedWrites==0,"speed key ignored before freecam");
    Command(Camera);Check(cameraState.active&&g_ownsCamera&&cameraState.speed==300,"enable and initial speed");
    Tick();Tick();Check(g_ownsCamera&&cameraState.active,"ownership survives ticks (0.1.1 regression)");
    IniWriteFloat("Debug","FreecamSpeed",.1f);ConfigLoad();
    Check(cameraState.speed==.1f,"F10/INI applies immediately after ticks");
    Command(CameraFaster);Check(cameraState.speed==.2f,"= immediately doubles movement speed");
    Check(IniFloat("Debug","FreecamSpeed",0)==.2f,"hotkey speed persisted");
    ConfigLoad();Check(cameraState.speed==.2f,"reload preserves hotkey speed");
    Command(CameraSlower);Command(CameraSlower);Check(cameraState.speed==.1f,"- clamps at minimum");
    IniWriteFloat("Debug","FreecamSpeed",9000);ConfigLoad();Command(CameraFaster);
    Check(cameraState.speed==CAMERA_MAX,"= clamps at maximum");
    int writes=speedWrites;Command(CameraFaster);Check(writes==speedWrites,"holding at limit does not rewrite INI");
    char status[180];Status(status,sizeof(status));Check(strstr(status,"CAM")&&strstr(status,"1e+04"),"status shows active speed");
    Command(Camera);Check(!cameraState.active&&!g_ownsCamera&&toggles==2,"second toggle returns to hero");
    writes=speedWrites;Command(CameraSlower);Check(speedWrites==writes,"speed keys ignored outside freecam");
    Command(Camera);cameraState.active=false;Tick();
    Check(!g_ownsCamera,"engine camera reset clears ownership");
    Command(Camera);IniWriteString("Debug","Enabled","0");ConfigLoad();
    Check(!cameraState.active&&!g_ownsCamera,"disabling tools returns camera");
    IniWriteString("Debug","Enabled","1");ConfigLoad();Command(Camera);inDialog=true;Tick();
    Check(!cameraState.active&&!g_ownsCamera,"dialog returns camera");inDialog=false;
    Command(Camera);hasPlayer=false;Tick();Check(!cameraState.active&&!g_ownsCamera,"leaving world returns camera");
    // Validate gain conversion against native normal camera degrees, independent of speed.
    Check(fabs(.025*CAMERA_ROTATION_MATCH-(.075*acos(-1.)/180.))<1e-9,"freecam radians match normal-camera degrees");
    a.currentWorld=TestWorld;a.worldEntity=TestWorldEntity;a.clockValid=TestClockValid;a.clock=TestClock;
    a.clockYear=TestYear;a.clockDay=TestDay;a.clockHour=TestHour;a.clockMinute=TestMinute;
    a.setYear=TestSetYear;a.setDay=TestSetDay;a.setHour=TestSetHour;a.setMinute=TestSetMinute;a.setSecond=TestSetSecond;
    a.weatherVtable=reinterpret_cast<void*>(4);weatherState.vtable=a.weatherVtable;
    a.weatherMode=TestMode;a.setWeatherMode=TestSetMode;a.weatherCount=TestWeatherCount;a.weatherAt=TestWeatherAt;
    a.weatherName=TestWeatherName;a.stringText=TestString;a.weatherWeight=TestWeight;a.setWeatherWeight=TestSetWeight;
    g_lightingReady=true;hasPlayer=true;inDialog=false;
    IniWriteString("Debug","Enabled","1");IniWriteFloat("Debug","TargetHour",12.5f);ConfigLoad();
    Check(g_timeKey==VK_NUMPAD1&&g_cycleKey==VK_NUMPAD2&&g_weatherKey==VK_NUMPAD3&&g_dayKey==VK_NUMPAD6,"lighting keys default to unused numpad keys");
    Command(SetHour);Check(clockState.hour==12&&clockState.minute==30&&clockState.second==0&&clockState.day==42,"selected hour preserves date and rounds minutes");
    clockState.hour=4;Command(CycleHour);Check(clockState.hour==6,"cycle dawn");
    Command(CycleHour);Check(clockState.hour==12,"cycle noon");Command(CycleHour);Check(clockState.hour==18,"cycle dusk");
    Command(CycleHour);Check(clockState.hour==0&&clockState.day==42,"cycle midnight keeps date");
    Command(NextDay);Check(clockState.day==43&&clockState.hour==0,"next day keeps time");
    clockState.day=364;Command(NextDay);Check(clockState.year==13&&clockState.day==0,"365-day calendar year rollover");
    hasWorld=false;Command(SetHour);Check(clockState.hour==0,"time ignored without loaded world");hasWorld=true;
    inDialog=true;Command(SetHour);Check(clockState.hour==0,"time ignored in dialog");inDialog=false;
    Command(ClearWeather);Check(g_weatherOwned&&weatherState.mode==2&&weatherState.states[0].weight==1&&weatherState.states[1].weight==0,"force clear using native manual blend mode");
    Check(clockState.hour==0,"weather toggle does not change time");
    Command(ClearWeather);Check(!g_weatherOwned&&weatherState.mode==0&&weatherState.states[1].weight==.25f&&weatherState.states[2].weight==.75f,"restore previous weather mode and copied weights");
    weatherState.mode=2;Command(ClearWeather);Command(ClearWeather);
    Check(weatherState.mode==2&&weatherState.states[1].weight==.25f,"preserve pre-existing manual weather");weatherState.mode=0;
    weatherState.states[2].weight=NAN;Command(ClearWeather);
    Check(!g_weatherOwned&&weatherState.mode==0&&weatherState.states[0].weight==0,"invalid weather refuses before mutation");weatherState.states[2].weight=.75f;
    Command(ClearWeather);IniWriteString("Debug","Enabled","0");ConfigLoad();
    Check(!g_weatherOwned&&weatherState.mode==0,"disabling debugging restores weather");
    IniWriteString("Debug","Enabled","1");ConfigLoad();Command(ClearWeather);hasPlayer=false;Tick();
    Check(!g_weatherOwned&&weatherState.mode==0,"leaving world restores weather");hasPlayer=true;
    Command(ClearWeather);weatherState.mode=1;Tick();
    Check(!g_weatherOwned&&weatherState.mode==1,"external weather mode change revokes ownership");
    IniWriteFloat("Debug","TargetHour",24);ConfigLoad();Check(g_targetHour==12,"out-of-range hour falls back to noon");
    printf("%d World Debug control regression checks passed (fake APIs, scratch INI only)\n",checks);
}
