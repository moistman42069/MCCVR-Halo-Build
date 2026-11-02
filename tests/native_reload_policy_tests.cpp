// Execute the production detours with a bounded fake engine. Only the caller
// intrinsic and native boundary functions are replaced; no game is loaded.
#include <intrin.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
static uintptr_t testCaller;
#define _ReturnAddress() reinterpret_cast<void*>(testCaller)
#include "../src/dll/native_reload_policy.cpp"
#undef _ReturnAddress

static GameTitle activeTitle=GameTitle::None;
static uint32_t activeGeneration=3;
static bool own=true,advanceGeneration=false;
static uint8_t object[0x700];
static int stockCalls,initializations,played,checks;
static unsigned selected;
static constexpr uint32_t localWeapon=0x12340002,otherWeapon=0x56780003;
static void Check(bool value,const char* what)
{ ++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);} }
void Logf(const char*,...) {}
const wchar_t* LogDirectory() {return L"";}
GameTitle TitleAdapter_GetActiveTitle() {return activeTitle;}
uint32_t TitleAdapter_GetGeneration(GameTitle) {return activeGeneration;}
const TitleDescriptor* TitleRegistry_Find(GameTitle) {return nullptr;}
void* Game_ReloadPolicyWeapon(GameTitle title,uint32_t weapon)
{return own && title==activeTitle && weapon==localWeapon ? object : nullptr;}
namespace sig { uintptr_t Find(uintptr_t,size_t,const char*) {return 0;} }
bool WaitForNativeDetourQuiescence(const void* const*,const void* const*,size_t,const std::atomic<uint32_t>& callbacks)
{return callbacks.load()==0;}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_ERROR_UNSUPPORTED_FUNCTION;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID) {return MH_OK;}
extern "C" MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID) {return MH_OK;}
static void Put(size_t at,int16_t value) {std::memcpy(object+at,&value,2);}
static int16_t Read(size_t at) {int16_t value;std::memcpy(&value,object+at,2);return value;}
static uint8_t __fastcall StockAuto(uint32_t,int16_t,uint8_t) {++stockCalls;return 1;}
static uint8_t __fastcall StockAuto2(uint32_t,int16_t) {++stockCalls;return 1;}
static void __fastcall StockState(uint32_t,int16_t magazine,int32_t state)
{
    ++stockCalls;
    if(magazine>=0 && magazine<2) Put(native_reload::layouts[selected].base+magazine*native_reload::layouts[selected].stride,int16_t(state));
    if(advanceGeneration) ++activeGeneration;
}
static void __fastcall StockCeStart(uint32_t weapon,int16_t magazine,uint8_t)
{StockState(weapon,magazine,1);}
static int16_t __fastcall StockDuration(uint32_t,uint32_t,int32_t) {return 37;}
static int16_t __fastcall StockCeDuration(uint32_t,int16_t,int16_t,int16_t) {return 37;}
static void __fastcall StockPlay(uint32_t,uint32_t,uint32_t,uint8_t) {++played;}
static uint8_t __fastcall StockBoolPlay(uint32_t,uint32_t,uint32_t,uint8_t) {++played;return 7;}
static void __fastcall StockCePlay(int16_t,int16_t,uint8_t) {++played;}
static uint8_t __fastcall StockH4Check(uint32_t,uint32_t,uint32_t,uint8_t) {++played;return 0;}
template<unsigned I> static void __fastcall StockAction(uint32_t unit,uint32_t weapon,uint32_t action,int32_t slot,uint8_t flag)
{
    ++initializations;
    (void)unit;(void)weapon;(void)action;(void)slot;(void)flag;
    PlayHook<I>(0,0,123,0);
}
static void __fastcall StockCeAction(uint32_t,uint32_t)
{++initializations;PlayHook<0>(0,13,1,0);}
static uint8_t tailUsersBytes[0x18000],tailDescriptor[0x100],ceWeaponTag[0x500],ceGraph[0x100];
static std::vector<uint8_t> ceGlobals(0x110000);
static int eventFrame=30,seekCalls,primaryTicks=30,animationLength=50,totalTicks=50;static bool clampSeek=false,duplicatePlay=false,foreignChannel=false;
static uint8_t* __fastcall FakeUsers(unsigned) {return tailUsersBytes;}
static float __fastcall FakeRemaining(void*) {return float(animationLength);}
static int __fastcall FakeLength(void*) {return animationLength;}
static uint8_t* __fastcall FakeAnimation(void*) {return tailDescriptor;}
static int __fastcall FakeEvent(void*,uint16_t event)
{Check(event==(selected>=native_reload::Reach?1:0),"own title insertion event number");return eventFrame;}
static void __fastcall FakeSeek(void* p,float frame)
{
    ++seekCalls;if(clampSeek&&frame>20) frame=20;
    std::memcpy(static_cast<uint8_t*>(p)+kTailLayout[selected].frame,&frame,4);
}
static void __fastcall FakeH4Seek(void* p,float frame,void* manager)
{Check(manager==p,"H4 passes the real first-person manager");FakeSeek(p,frame);}
static uint8_t* __fastcall FakeCeTag(uint32_t tag) {return tag==9?ceGraph:ceWeaponTag;}
template<class T> static void Store(uint8_t* p,size_t at,T value) {std::memcpy(p+at,&value,sizeof value);}
template<unsigned I> static void __fastcall TailState(uint32_t weapon,int16_t magazine,int32_t state)
{
    ++stockCalls;const auto& m=native_reload::layouts[I];const auto& l=kTailLayout[I];
    Put(m.base+magazine*m.stride,int16_t(state));Put(m.base+magazine*m.stride+2,int16_t(totalTicks));
    if constexpr(I!=0) Put(m.base+magazine*m.stride+(I==1?12:16),int16_t(primaryTicks));
    Store(tailUsersBytes,4,uint32_t(42));Store(tailUsersBytes,l.weapon,foreignChannel?otherWeapon:weapon);
    auto* channel=tailUsersBytes+l.channel;
    if constexpr(I==0) {Store(channel,l.identity,int16_t(13));Store(channel,l.frame,int16_t(0));}
    else {
        Store(channel,l.graph,uint32_t(8));Store(channel,l.index,int16_t(0));
        Store(channel,l.identity,uint32_t(123));Store(channel,l.frame,0.0f);
        if(l.initialized) Store(channel,l.initialized,uint8_t(1));
    }
    ActionHook<I>(I==0?weapon:42,I==0?9:weapon,7,0,1);
    if(duplicatePlay) ActionHook<I>(I==0?weapon:42,I==0?9:weapon,7,0,1);
    if(advanceGeneration) ++activeGeneration;
}
static void __fastcall TailCeStart(uint32_t weapon,int16_t magazine,uint8_t) {TailState<0>(weapon,magazine,1);}
template<unsigned I> static void TailRun()
{
    selected=I;activeTitle=kBindings[I].title;activeGeneration=3;own=true;
    auto& r=runtime[I];r.generation=3;r.options=4;r.tailReady=true;r.tailFaulted=false;r.faulted=false;
    r.tailUsers=&FakeUsers;r.tailFunctions[0]=I==0?reinterpret_cast<void*>(&FakeCeTag):reinterpret_cast<void*>(&FakeAnimation);
    r.tailFunctions[1]=reinterpret_cast<void*>(&FakeEvent);
    r.tailFunctions[2]=I==5?reinterpret_cast<void*>(&FakeH4Seek):reinterpret_cast<void*>(&FakeSeek);
    r.tailFunctions[3]=I==4?reinterpret_cast<void*>(&FakeRemaining):reinterpret_cast<void*>(&FakeLength);
    Store(tailDescriptor,0x10,int16_t(animationLength));Store(tailDescriptor,0x14,int16_t(animationLength));
    r.original[Auto]=reinterpret_cast<void*>(&TailCeStart);r.original[State]=reinterpret_cast<void*>(&TailState<I>);
    r.original[Action]=I==0?reinterpret_cast<void*>(&StockCeAction):reinterpret_cast<void*>(&StockAction<I>);
    r.original[Play]=I==0?reinterpret_cast<void*>(&StockCePlay):I==1?reinterpret_cast<void*>(&StockPlay):I==5?reinterpret_cast<void*>(&StockH4Check):reinterpret_cast<void*>(&StockBoolPlay);
    r.original[Duration]=I==0?reinterpret_cast<void*>(&StockCeDuration):reinterpret_cast<void*>(&StockDuration);
    std::fill(std::begin(tailUsersBytes),std::end(tailUsersBytes),uint8_t(0));
    std::fill(std::begin(object),std::end(object),uint8_t(0));
    const auto& m=native_reload::layouts[I];const auto& l=kTailLayout[I];
    if constexpr(I==0) {
        r.base=reinterpret_cast<uintptr_t>(ceGlobals.data())-kTailBindings[0].globals[1];
        Store(ceGlobals.data(),0,reinterpret_cast<uintptr_t>(tailDescriptor));
        Store(ceGlobals.data(),kTailBindings[0].globals[2]-kTailBindings[0].globals[1],uintptr_t(0x1000));
        Store(ceWeaponTag,0x478,uint32_t(9));Store(ceGraph,0x74,int32_t(1));Store(ceGraph,0x78,uint32_t(0x1000));
        Store(tailDescriptor,0x22,int16_t(50));Store(tailDescriptor,0x34,int16_t(30));
    }
    auto run=[] {if constexpr(I==0) {testCaller=0;AutoHook<0>(localWeapon,0,0);} else StateHook<I>(localWeapon,0,1);};
    eventFrame=30;seekCalls=0;played=0;stockCalls=0;initializations=0;run();
    Check(Read(m.base+2)==20,"shortened mode retains twenty native tail ticks");
    if constexpr(I!=0) Check(Read(m.base+(I==1?12:16))==20,"transfer remains native and blocked until tail finishes");
    Check(played==1 && initializations==1 && stockCalls==1,"shortened mode executes native reload initialization and play once");
    Check((I==0?float(TailRead<int16_t>(tailUsersBytes+l.channel,l.frame)):TailRead<float>(tailUsersBytes+l.channel,l.frame))==30,"visible native animation starts at insertion frame");
    Check(Read(m.base+(I==1?8:10))==0,"tail seek never manufactures ammunition");
    Check(animationScope==-1&&timingScope==-1&&tailScope==nullptr&&!tailAction&&r.callbacks==0,"all tail scopes drain");
    constexpr uint32_t ready[]{10,0x5000024,0x26,0x26,0x27,0x71};
    Check(DurationHook<I>(localWeapon,I==0?0:ready[I],I==0?10:1,0)==37,"shortened mode does not disable ready animation");
    foreignChannel=true;run();foreignChannel=false;Check(Read(m.base+2)==50,"foreign first-person weapon retains full reload");
    duplicatePlay=true;run();duplicatePlay=false;Check(Read(m.base+2)==50,"ambiguous first-person animation retains full reload");
    advanceGeneration=true;run();advanceGeneration=false;activeGeneration=3;Check(Read(m.base+2)==50,"generation change rejects pending seek");
    if constexpr(I!=0) {
        eventFrame=-1;run();Check(Read(m.base+2)==13,"missing authored event retains final quarter");
        eventFrame=0;run();Check(Read(m.base+2)==13,"zero authored event retains final quarter");
        eventFrame=50;run();Check(Read(m.base+2)==13,"endpoint marker retains a visible final section");
        eventFrame=30;primaryTicks=50;run();Check(Read(m.base+2)==13,"native full-duration insertion fallback still shortens");primaryTicks=30;
        eventFrame=30;clampSeek=true;seekCalls=0;run();clampSeek=false;
        Check(seekCalls==2&&Read(m.base+2)==50&&TailRead<float>(tailUsersBytes+l.channel,l.frame)==0,"clamped event restores stock animation and timing");
    } else {
        Store(tailDescriptor,0x34,int16_t(60));run();Check(Read(m.base+2)==13,"CE absent interior event retains final quarter");Store(tailDescriptor,0x34,int16_t(30));
    }
    if constexpr(I==0) {
        Store(tailDescriptor,0x34,int16_t(0));run();Check(Read(m.base+2)==13,"CE zero marker shortens the visible reload");
        Store(tailDescriptor,0x34,int16_t(30));
    }
    // Engine ticks need not equal authored 30-Hz animation frames.
    totalTicks=100;primaryTicks=100;eventFrame=-1;
    if constexpr(I==0) Store(tailDescriptor,0x34,int16_t(0));
    run();Check(Read(m.base+2)==26,"final section scales native ticks independently from frames");
    Check((I==0?float(TailRead<int16_t>(tailUsersBytes+l.channel,l.frame)):TailRead<float>(tailUsersBytes+l.channel,l.frame))==37,"final-section visual seek uses animation frames");
    totalTicks=50;primaryTicks=30;eventFrame=30;
    if constexpr(I==0) Store(tailDescriptor,0x34,int16_t(30));
    totalTicks=1;run();Check(Read(m.base+2)==1,"one-tick reload remains native");totalTicks=50;
    if constexpr(I==0) {
        const auto offset=kTailBindings[0].globals[2]-kTailBindings[0].globals[1];
        Store(ceGlobals.data(),offset,intptr_t(-4096));Store(ceGraph,0x78,uint32_t(-4096));
        run();Check(Read(m.base+2)==20,"CE signed virtual tag base resolves the same selected animation");
        Store(ceGlobals.data(),offset,intptr_t(4096));Store(ceGraph,0x78,uint32_t(4096));
    }
    r.options=6;run();Check(Read(m.base+2)==0,"full skip retains precedence even for an unfiltered option mask");
    r.options=7;r.tailFaulted=true;Check(Current(I,1)&&Current(I,2)&&!Current(I,4),"tail fault preserves automatic/full-skip options");
    r.tailFaulted=false;r.faulted=true;Check(Current(I,1)&&!Current(I,2)&&Current(I,4),"full-skip fault does not disable independent tail");
    r.faulted=false;r.options=0;r.tailReady=false;
}
template<unsigned I> static void Run()
{
    selected=I;activeTitle=kBindings[I].title;activeGeneration=3;own=true;
    auto& r=runtime[I];r.generation=3;r.base=0x180000000;r.options=3;r.faulted=false;
    r.original[Auto]=I==0 ? reinterpret_cast<void*>(&StockCeStart) : I==1 ? reinterpret_cast<void*>(&StockAuto2) : reinterpret_cast<void*>(&StockAuto);
    r.original[State]=reinterpret_cast<void*>(&StockState);
    r.original[Action]=I==0 ? reinterpret_cast<void*>(&StockCeAction) : reinterpret_cast<void*>(&StockAction<I>);
    r.original[Play]=I==0 ? reinterpret_cast<void*>(&StockCePlay) : I==5 ? reinterpret_cast<void*>(&StockH4Check) : (I==2 || I==3) ? reinterpret_cast<void*>(&StockBoolPlay) : reinterpret_cast<void*>(&StockPlay);
    r.original[Duration]=I==0 ? reinterpret_cast<void*>(&StockCeDuration) : reinterpret_cast<void*>(&StockDuration);
    testCaller=r.base+kBindings[I].autoReturn;stockCalls=0;
    AutoHook<I>(localWeapon,0,0);Check(stockCalls==0,"automatic empty-trigger request suppressed");
    testCaller+=1;AutoHook<I>(localWeapon,0,0);Check(stockCalls==1,"manual/continuation request preserved");
    testCaller-=1;AutoHook<I>(otherWeapon,0,0);Check(stockCalls==2,"NPC/remote weapon preserved");
    r.options=2;AutoHook<I>(localWeapon,0,0);Check(stockCalls==3,"auto option independent from animation option");
    r.options=3;
    const auto& layout=native_reload::layouts[I];
    for(int magazine=-1;magazine<=2;++magazine) for(int state=-1;state<=7;++state) {
        std::fill(std::begin(object),std::end(object),uint8_t(0x5a));
        if(magazine>=0&&magazine<2) Put(layout.base+magazine*layout.stride,int16_t(state));
        uint8_t before[sizeof object];std::memcpy(before,object,sizeof object);
        const bool changed=native_reload::ShortenMagazine(I,object,magazine);
        Check(changed==(magazine>=0&&magazine<2&&state>=1&&state<=(I==0?1:3)),"native state admission");
        for(size_t at=0;at<sizeof object;++at) {
            bool timer=false;
            if(changed) for(unsigned n=0;n<layout.timerCount;++n) {
                size_t pos=layout.base+magazine*layout.stride+layout.timers[n];
                timer|=at==pos||at==pos+1;
            }
            Check(object[at]==(timer?0:before[at]),"only proven countdown bytes may change");
        }
        Check(!native_reload::ShortenMagazine(I,{object,layout.base},magazine),"truncated datum rejected");
    }
    std::fill(std::begin(object),std::end(object),uint8_t(0x22));
    Put(layout.base,1);stockCalls=0;
    if constexpr(I==0) {testCaller=0;AutoHook<I>(localWeapon,0,1);}
    else StateHook<I>(localWeapon,0,1);
    Check(stockCalls==1 && Read(layout.base+2)==0,"production state hook shortens after one original call");
    Check(Read(layout.base+(I==0?10:I==1?8:10))==0x2222,"native ammo remains untouched");
    Put(layout.base+2,50);own=false;
    if constexpr(I==0) AutoHook<I>(localWeapon,0,1);else StateHook<I>(localWeapon,0,1);
    Check(Read(layout.base+2)==50,"unowned state stays stock");own=true;
    if constexpr(I!=0) {advanceGeneration=true;StateHook<I>(localWeapon,0,1);advanceGeneration=false;Check(Read(layout.base+2)==50,"title generation changed during original stays stock");activeGeneration=3;}
    const uint32_t reload=I==0?9:7;
    played=initializations=0;
    ActionHook<I>(I==0?localWeapon:42,I==0?reload:localWeapon,reload,0,1);
    Check(initializations==1 && played==0,"equip/action initialization preserved while play suppressed");
    ActionHook<I>(I==0?localWeapon:42,I==0?1:localWeapon,1,0,1);
    Check(initializations==2 && played==1,"firing animation remains stock");
    Check(animationScope==-1 && timingScope==-1 && r.callbacks==0,"scope and callback drain restored");
    r.options=1;ActionHook<I>(I==0?localWeapon:42,I==0?reload:localWeapon,reload,0,1);
    Check(played==2,"animation option independent from disable-auto");r.options=3;
    constexpr uint32_t ready[]{10,0x5000024,0x26,0x26,0x27,0x71};
    Check(DurationHook<I>(localWeapon,I==0?0:ready[I],I==0?10:1,0)==0,"ready animation wait removed");
    Check(DurationHook<I>(otherWeapon,I==0?0:ready[I],I==0?10:1,0)==37,"remote ready time preserved");
    Check(DurationHook<I>(localWeapon,123,1,0)==37,"unrelated native duration preserved");
    timingScope=I;
    Check(DurationHook<I>(localWeapon,123,1,0)==0,"admitted reload duration shortened");
    if constexpr(I!=0) Check(DurationHook<I>(localWeapon,ready[I],2,0)==37,"keyframe query preserved even in reload scope");
    timingScope=-1;
    if constexpr(I==2 || I==3) Check(PlayHook<I>(0,0,123,0)==7,"unscoped native animation return preserved");
    if constexpr(I==4) Check(!native_reload::SuppressAction(I,27)&&!native_reload::SuppressAction(I,28),"unproven Reach variants remain stock");
    if constexpr(I==5) Check(!native_reload::SuppressAction(I,29)&&!native_reload::SuppressAction(I,30),"unproven Halo 4 variants remain stock");
    r.faulted=true;Check(!Current(I,2)&&Current(I,1),"animation fault leaves disable-auto enabled");r.faulted=false;
    activeTitle=GameTitle::None;Check(!Current(I,1)&&!Current(I,2),"title exit gates both optional features");
    r.options=0;Check(Retire(I),"idle detours retire independently");
}
int main()
{
    Check(!g_config.manual_reload_disable_auto&&!g_config.manual_reload_skip_animations&&!g_config.manual_reload_shortened_animation,"options default off");
    wchar_t directory[MAX_PATH]{},path[MAX_PATH]{};
    GetTempPathW(MAX_PATH,directory);
    swprintf_s(path,L"%smccvr-reload-policy-%lu-%llu.cfg",directory,GetCurrentProcessId(),GetTickCount64());
    ConfigLoad(path);
    g_config.manual_reload=true;g_config.manual_reload_disable_auto=true;g_config.manual_reload_skip_animations=true;
    ConfigSave();g_config.manual_reload_disable_auto=false;g_config.manual_reload_skip_animations=false;
    ConfigLoad(path);
    Check(g_config.manual_reload_disable_auto&&g_config.manual_reload_skip_animations,"both settings survive config roundtrip");
    for(unsigned bits=0;bits<8;++bits) {
        g_config.manual_reload_disable_auto=(bits&1)!=0;g_config.manual_reload_skip_animations=(bits&2)!=0;g_config.manual_reload_shortened_animation=(bits&4)!=0;
        ConfigSave();g_config.manual_reload_disable_auto=false;g_config.manual_reload_skip_animations=false;g_config.manual_reload_shortened_animation=false;ConfigLoad(path);
        Check(g_config.manual_reload_disable_auto==bool(bits&1)&&g_config.manual_reload_skip_animations==bool(bits&2)&&g_config.manual_reload_shortened_animation==bool(bits&4),"three reload options persist independently");
        Check(native_reload::Options(true,bits&1,bits&2,bits&4)==((bits&2)?(bits&3):bits),"full skip has deterministic precedence");
        Check(native_reload::Options(false,bits&1,bits&2,bits&4)==0,"manual mode gates all reload options");
    }
    // Dual-wield shake reload is a button press now; only its setting lives here.
    g_config.dual_wield_shake_reload=true;ConfigSave();g_config.dual_wield_shake_reload=false;ConfigLoad(path);
    Check(g_config.dual_wield_shake_reload,"dual shake option persists independently");
    g_config.dual_wield_shake_reload=false;
    DeleteFileW(path);
    Run<0>();Run<1>();Run<2>();Run<3>();Run<4>();Run<5>();
    TailRun<0>();TailRun<1>();TailRun<2>();TailRun<3>();TailRun<4>();TailRun<5>();
    Check(!native_reload::ShortenMagazine(6,object,0),"unsupported title rejected");
    std::printf("%d native reload policy checks passed\n",checks);
}
