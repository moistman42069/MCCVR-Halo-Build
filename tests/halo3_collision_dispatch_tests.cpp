#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
#include "../src/dll/halo3_collision_dispatch.inl"
static uint8_t __fastcall Original(uint64_t f,int32_t m,const float* a,const float* b,int32_t x,int32_t y,int32_t z,void* out)
{
    ++nativeCalls;
    Check(f==flags&&m==mode&&a==start&&b==delta&&x==-1&&y==INT32_MIN&&z==INT32_MAX&&out==&output,"all eight native arguments untouched");
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
    nativeCalls=ticks=0;output=0;returned=0xFF;
    option=nativeFault=extraFault=redirect=g_legacyCollisionOwnedQuery=false;
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
        Check(g_halo3WorldCollision.callbacks==0&&ticks==1,"normal callback drained");
        Reset();option=enabled;nativeFault=true;
        Check(Invoke()==0xE0123401&&returned==0xFF&&nativeCalls==1,"native exception propagates without fabricated miss or retry");
        Check(output==0xABCD1234&&ticks==0&&g_halo3WorldCollision.callbacks==0,"native unwind drains ownership and runs no extra query");
        Check(g_halo3WorldCollision.installed&&g_halo3WorldCollision.failures==0,"native failure is not relabelled as optional-feature failure");
    }
    Reset();option=extraFault=true;
    Check(Invoke()==0&&returned==0xA7&&output==0xABCD1234&&nativeCalls==1,"optional fault retains completed native collision");
    Check(!g_halo3WorldCollision.installed&&g_halo3WorldCollision.failures==1&&g_halo3WorldCollision.callbacks==0,"optional feature alone isolated");
    Reset();g_legacyCollisionOwnedQuery=true;
    Check(Invoke()==0&&returned==0xA7&&nativeCalls==1&&ticks==0,"owned query cannot recursively schedule");
    Reset();redirect=true;
    Check(Invoke()==0&&returned==19&&nativeCalls==0&&ticks==0&&g_halo3WorldCollision.callbacks==0,"owned physical contact redirect retained");
    std::printf("PASS: %u production collision dispatch checks\n",checks);
}
