#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "../src/common/runtime_types.h"

static unsigned checks{},nativeCalls{},ticks{};
static void Check(bool good,const char* label)
{++checks;if(!good){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
using LegacyCollisionTestVectorFn=uint8_t(__fastcall*)(uint64_t,int32_t,const float*,const float*,int32_t,int32_t,int32_t,void*);
struct LegacyWorldCollisionFeature
{
    void* original{};
    std::atomic<uint32_t> callbacks{};
    std::atomic<uint64_t> engineCalls{},failures{};
    std::atomic<bool> installed{true};
};
static LegacyWorldCollisionFeature g_halo3WorldCollision;
static thread_local bool g_legacyCollisionOwnedQuery{};
static bool option{},nativeFault{},extraFault{},redirect{};
static uint8_t returned{};
static uint64_t flags=0xFEDCBA9876543210;
static int32_t mode=-1234567;
static float start[3]{1,2,3},delta[3]{-4,5,-6};
static uint32_t output{};
static void LegacyWorldCollisionTick(LegacyWorldCollisionFeature&)
{
    ++ticks;
    if(option && extraFault)RaiseException(0xE0123402,0,0,nullptr);
}
static bool Halo3RedirectContactVector(uintptr_t,uint64_t,int32_t,int32_t,int32_t,int32_t,void*,uint8_t& result)
{if(redirect){result=19;return true;}return false;}
// The engine-crosshair check (dev) sees every query the GAME makes, never one the mod made itself.
static unsigned engineNotes{};
static void LegacySightNoteEngineTrace(GameTitle title,uint64_t f,const float* a,const float* b,uintptr_t caller)
{++engineNotes;Check(title==GameTitle::Halo3&&f==flags&&a==start&&b==delta&&caller!=0,"engine-trace note gets the game's own query");}
// The shot re-aim (legacy_sight_trace.inl): when it applies, the native query traces ITS vector instead; the
// shot-lag witness sees whichever vector was traced.
static bool reaim{};
static unsigned reaimAsks{},crosshairNotes{};
static const float* lastNotedQuery{};
static constexpr float kReaimed[3]{7.0f,-8.0f,9.0f};
static bool LegacySightSeatLineFromEye(GameTitle,const float*,const float*,float*,float*){return false;}
static bool LegacySightReaimCrosshairQuery(GameTitle title,uint64_t f,const float* a,const float* b,int32_t ignoredA,
    uintptr_t caller,float out[3])
{
    ++reaimAsks;
    Check(title==GameTitle::Halo3&&f==flags&&a==start&&b==delta&&ignoredA==-1&&caller!=0,
        "the re-aim sees the game's own query");
    if(!reaim)return false;
    out[0]=kReaimed[0];out[1]=kReaimed[1];out[2]=kReaimed[2];
    return true;
}
static void LegacySightNoteCrosshairQuery(GameTitle,uint64_t,const float*,const float* b,int32_t,uintptr_t)
{++crosshairNotes;lastNotedQuery=b;}
// THE SEAT'S SHOT LINE: after the native trace, where the traced query stopped (its own vector and result).
static unsigned resultNotes{};
static const float* lastResultQuery{};
static uint8_t lastResultReturned{};
static void LegacySightNoteCrosshairResult(GameTitle title,const float* a,const float* b,const void* r,uint8_t ret)
{
    ++resultNotes;lastResultQuery=b;lastResultReturned=ret;
    Check(title==GameTitle::Halo3&&a==start&&r==&output,"the shot-line note gets the traced query's start and result");
    Check(nativeCalls==0||output==0xABCD1234,"and reads the result after the native wrote it");
}
#include "../src/dll/halo3_hook_watch_mark.h"
#include "../src/dll/halo3_collision_dispatch.inl"
static uint8_t __fastcall Original(uint64_t f,int32_t m,const float* a,const float* b,int32_t x,int32_t y,int32_t z,void* out)
{
    ++nativeCalls;
    const bool vectorAsExpected=reaim?(b!=delta&&b[0]==kReaimed[0]&&b[1]==kReaimed[1]&&b[2]==kReaimed[2]):b==delta;
    Check(f==flags&&m==mode&&a==start&&vectorAsExpected&&x==-1&&y==INT32_MIN&&z==INT32_MAX&&out==&output,
        "all eight native arguments untouched (the vector only when re-aimed)");
    output=0xABCD1234;
    if(nativeFault)RaiseException(0xE0123401,0,0,nullptr);
    return 0xA7;
}
static DWORD Invoke()
{
    __try{returned=Halo3CollisionResolveDetour(flags,mode,start,delta,-1,INT32_MIN,INT32_MAX,&output);}
    __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
    return 0;
}
static void Reset()
{
    nativeCalls=ticks=engineNotes=reaimAsks=crosshairNotes=resultNotes=0;output=0;returned=0xFF;lastNotedQuery=nullptr;
    lastResultQuery=nullptr;lastResultReturned=0;
    option=nativeFault=extraFault=redirect=reaim=g_legacyCollisionOwnedQuery=false;
    g_halo3WorldCollision.original=reinterpret_cast<void*>(&Original);
    g_halo3WorldCollision.installed=true;g_halo3WorldCollision.failures=0;
    g_halo3WorldCollision.engineCalls=0;
}
int main()
{
    for(int enabled=0;enabled<2;++enabled)
    {
        Reset();option=enabled;
        Check(Invoke()==0&&returned==0xA7&&nativeCalls==1&&output==0xABCD1234,"stock result and output forwarded exactly once");
        Check(engineNotes==1,"the game's own query is noted once for the engine-crosshair check");
        Check(g_halo3WorldCollision.callbacks==0&&ticks==1,"normal callback drained");
        Reset();option=enabled;nativeFault=true;
        Check(Invoke()==0xE0123401&&returned==0xFF&&nativeCalls==1,"native exception propagates without fabricated miss or retry");
        Check(resultNotes==0,"a faulted native query is never read as a stop");
        Check(output==0xABCD1234&&ticks==0&&g_halo3WorldCollision.callbacks==0,"native unwind drains ownership and runs no extra query");
        Check(g_halo3WorldCollision.installed&&g_halo3WorldCollision.failures==0,"native failure is not relabelled as optional-feature failure");
    }
    Reset();option=extraFault=true;
    Check(Invoke()==0&&returned==0xA7&&output==0xABCD1234&&nativeCalls==1,"optional fault retains completed native collision");
    Check(!g_halo3WorldCollision.installed&&g_halo3WorldCollision.failures==1&&g_halo3WorldCollision.callbacks==0,"optional feature alone isolated");
    Reset();g_legacyCollisionOwnedQuery=true;
    Check(Invoke()==0&&returned==0xA7&&nativeCalls==1&&ticks==0,"owned query cannot recursively schedule");
    Check(engineNotes==0,"the mod's own query is never noted as the game's");
    Check(reaimAsks==0,"the mod's own query is never re-aimed");
    Check(crosshairNotes==0,"the mod's own query is never witnessed as a shot");
    Check(resultNotes==0,"nor its stop as a shot's");
    // THE SHOT RE-AIM: the crosshair query traces the re-aimed vector; declined, the game's own vector exactly.
    Reset();
    Check(Invoke()==0&&returned==0xA7&&nativeCalls==1&&reaimAsks==1,"a declined re-aim forwards the game's own vector");
    Check(crosshairNotes==1&&lastNotedQuery==delta,"the witness sees the game's own vector when nothing was re-aimed");
    Check(resultNotes==1&&lastResultQuery==delta&&lastResultReturned==0xA7,
        "the shot line is read once, against the game's own vector and the native's answer");
    Reset();reaim=true;
    Check(Invoke()==0&&returned==0xA7&&nativeCalls==1&&output==0xABCD1234,"a re-aimed query still runs the native once");
    Check(reaimAsks==1,"the re-aim was asked once");
    Check(crosshairNotes==1&&lastNotedQuery&&lastNotedQuery!=delta&&lastNotedQuery[0]==kReaimed[0],
        "the witness sees the re-aimed vector that was traced");
    Check(engineNotes==1,"the engine-crosshair check still sees the game's own query");
    Check(resultNotes==1&&lastResultQuery&&lastResultQuery[0]==kReaimed[0],
        "a re-aimed query's stop is read against the vector that was traced");
    Reset();reaim=true;nativeFault=true;
    Check(Invoke()==0xE0123401&&nativeCalls==1&&g_halo3WorldCollision.callbacks==0,
        "a native fault on a re-aimed query propagates and drains");
    Reset();redirect=true;
    Check(Invoke()==0&&returned==19&&nativeCalls==0&&ticks==0&&g_halo3WorldCollision.callbacks==0,"owned physical contact redirect retained");
    std::printf("PASS: %u production collision dispatch checks\n",checks);
}
