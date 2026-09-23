#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../src/common/halo2_render_logic.h"
#include "../src/common/halo4_restoration_logic.h"

static unsigned checks;
static void Check(bool value, const char* name)
{ ++checks; if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); } }

namespace H2
{
using Halo2ParticleRendererFn = void(__fastcall*)(uint32_t,uint32_t,uint32_t,uint32_t);
static struct { bool hide_muzzle_flash[6]{}; } g_config;
static std::atomic<uint32_t> g_particleActiveCallbacks{};
static std::atomic<uintptr_t> g_particleOriginal{},g_moduleBase{};
static std::atomic<bool> g_armed{true},g_levelLive{true},g_teardownRequested{},g_particleHitPending{};
static std::atomic<uint64_t> g_particleReadFaults{},g_particleSuppressed{};
static constexpr uintptr_t kHalo2ClassicRenderDisabledByteRva=16;
static unsigned originalCalls;
static bool throwOriginal;
static void __fastcall Original(uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{
    Check(a==11 && c==22 && d==33, "H2 original arguments preserved");
    ++originalCalls;
    if (throwOriginal) RaiseException(0xe0424242,0,0,nullptr);
}
#include "../src/dll/halo2_particle_suppression.inl"
static bool CallFault()
{
    __try { Halo2ParticleRendererDetour(11,0,22,33); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
static void Run()
{
    uint8_t module[32]{};
    g_moduleBase=reinterpret_cast<uintptr_t>(module);
    g_particleOriginal=reinterpret_cast<uintptr_t>(&Original);
    for (unsigned state=0;state<32;++state)
    for (unsigned gate=0;gate<3;++gate)
    for (unsigned firstPerson : {0u,1u,255u})
    {
        g_config.hide_muzzle_flash[5]=(state&1)!=0;
        g_armed=(state&2)!=0; g_levelLive=(state&4)!=0;
        g_teardownRequested=(state&8)!=0;
        g_particleOriginal=(state&16)?reinterpret_cast<uintptr_t>(&Original):0;
        module[16]=static_cast<uint8_t>(gate);
        const unsigned before=originalCalls;
        const auto hidden=g_particleSuppressed.load();
        Halo2ParticleRendererDetour(11,firstPerson,22,33);
        const bool suppress=state==23 && gate==0 && firstPerson!=0;
        Check(g_particleSuppressed==hidden+(suppress?1:0),"H2 setting, owner, live renderer and lifecycle gates");
        Check(originalCalls==before+((state&16)&&!suppress?1:0),"H2 stock dispatch exactly once when available");
        Check(g_particleActiveCallbacks==0,"H2 callback retired after every route");
    }
    g_config.hide_muzzle_flash[5]=true;g_armed=true;g_levelLive=true;g_teardownRequested=false;
    g_particleOriginal=reinterpret_cast<uintptr_t>(&Original);
    g_moduleBase=1;
    const auto before=originalCalls;
    Halo2ParticleRendererDetour(11,1,22,33);
    Check(originalCalls==before+1 && g_particleReadFaults==1,"H2 unreadable live renderer leaves original active");
    throwOriginal=true;
    Check(CallFault() && g_particleActiveCallbacks==0,"H2 native exception propagates once with balanced callback");
}
}

namespace H4
{
static volatile uint8_t g_halo4EffectsEnabled=1;
static volatile LONG64 g_halo4EffectsHidden;
static struct {
    std::atomic<bool> effectsInstalled{true};
    bool effectsModeOnePatched=true;
} g_halo4Restoration;
static constexpr uintptr_t kHalo4EffectModeOneRva=16;
static constexpr std::array<uint8_t,3> kHalo4EffectModeOneStock{0x41,0x8a,0xda};
static constexpr std::array<uint8_t,3> kHalo4EffectModeOneHidden{0x30,0xdb,0x90};
static bool freezeOk=true,contextOk=true,writeOk=true,mutateDuringCapture=false;
static bool frozen;
static DWORD64 frozenRip;
static unsigned freezes,writes;
static uint8_t* module;
struct ReachFrozenThread { HANDLE handle; bool suspended; };
class ReachThreadFreeze
{
    std::vector<ReachFrozenThread> threads{{reinterpret_cast<HANDLE>(1),true}};
public:
    bool Capture() {
        ++freezes;
        if(mutateDuringCapture)module[16]=0xcc;
        frozen=freezeOk;return freezeOk;
    }
    bool Release() { frozen=false;return true; }
    const auto& Threads() const {return threads;}
};
static BOOL FixtureGetThreadContext(HANDLE,CONTEXT* context)
{Check(frozen,"H4 thread context checked only while frozen");context->Rip=frozenRip;return contextOk;}
static DWORD FixtureWait(HANDLE,DWORD) {return WAIT_TIMEOUT;}
static int Halo4SafeRead(const void* from,void* to,size_t size)
{__try {std::memcpy(to,from,size);return 1;} __except(EXCEPTION_EXECUTE_HANDLER){return 0;}}
static int Halo4SafeWrite(void* to,const void* from,size_t size)
{
    Check(frozen,"H4 executable setting writes happen only under freeze");
    ++writes;
    if(!writeOk)return 0;
    __try {std::memcpy(to,from,size);return 1;} __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
template<size_t N> static bool Halo4PatchMatches(uintptr_t site,const std::array<uint8_t,N>& expected)
{std::array<uint8_t,N> actual{};return Halo4SafeRead(reinterpret_cast<void*>(site),actual.data(),N)&&actual==expected;}
#define GetThreadContext FixtureGetThreadContext
#define WaitForSingleObject FixtureWait
#include "../src/dll/halo4_effect_suppression.inl"
#undef GetThreadContext
#undef WaitForSingleObject
#include "../src/dll/halo4_effect_hide_bridge.inl"
static void Reset()
{
    std::memset(module,0,4096);
    std::memcpy(module+16,kHalo4EffectModeOneHidden.data(),3);
    g_halo4Restoration.effectsInstalled=true;g_halo4Restoration.effectsModeOnePatched=true;
    g_halo4EffectsEnabled=1;freezeOk=true;contextOk=true;writeOk=true;mutateDuringCapture=false;
    frozenRip=0;freezes=0;writes=0;
}
static void Run()
{
    module=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(module!=nullptr,"H4 fixture executable storage allocated");
    const auto base=reinterpret_cast<uintptr_t>(module);
    Reset();
    Check(Halo4SetEffectsSuppressed(base,true)&&freezes==0&&writes==0,"H4 unchanged setting does not freeze or write");
    for(unsigned i=0;i<10;++i)
    {
        const bool hide=(i&1)!=0;
        Check(Halo4SetEffectsSuppressed(base,hide),"H4 live toggle completed");
        Check(g_halo4Restoration.effectsModeOnePatched==hide && (g_halo4EffectsEnabled!=0)==hide,
            "H4 bridge and independent particle gate agree");
        Check(Halo4PatchMatches(base+16,hide?kHalo4EffectModeOneHidden:kHalo4EffectModeOneStock),
            "H4 exact three-byte stock or hide sequence selected");
        Check(module[15]==0&&module[19]==0,"H4 adjoining instructions preserved");
    }
    for(unsigned refusal=0;refusal<8;++refusal)
    {
        Reset();
        switch(refusal){
        case 0:g_halo4Restoration.effectsInstalled=false;break;
        case 1:module[16]=0xcc;break;
        case 2:freezeOk=false;break;
        case 3:contextOk=false;break;
        case 4:frozenRip=base+17;break;
        case 5:frozenRip=base+18;break;
        case 6:writeOk=false;break;
        case 7:mutateDuringCapture=true;break;
        }
        Check(!Halo4SetEffectsSuppressed(base,false),"H4 unknown ownership or unsafe thread leaves setting pending");
        Check(g_halo4Restoration.effectsModeOnePatched&&g_halo4EffectsEnabled==1&&!frozen,
            "H4 refused transition retains prior bridge policy and resumes threads");
        Check(module[16]==((refusal==1||refusal==7)?0xcc:0x30),"H4 foreign or untouched code never overwritten");
    }
    for(auto rip:{base+16,base+19})
    {Reset();frozenRip=rip;Check(Halo4SetEffectsSuppressed(base,false),"H4 exact sequence boundaries can resume safely");}
    Reset();
    for(unsigned enabled=0;enabled<2;++enabled)
    for(unsigned flags=0;flags<256;++flags)
    {
        uint8_t descriptor[0x50]{};descriptor[0x4a]=static_cast<uint8_t>(flags);
        std::array<uint8_t,0x40> before{};before.fill(0x42);auto after=before;
        g_halo4EffectsEnabled=static_cast<uint8_t>(enabled);
        Halo4EffectHideBridge(descriptor,after.data());
        const bool admitted=enabled && Halo4EffectDescriptorIsLocalFirstPerson(static_cast<uint8_t>(flags));
        Check((before!=after)==admitted,"H4 disabled bridge and foreign descriptors remain stock");
        if(admitted){
            uint32_t xyz[3]{};std::memcpy(xyz,after.data()+0x28,sizeof(xyz));
            Check(xyz[0]==0x461c4000 && xyz[1]==xyz[0] && xyz[2]==xyz[0],"H4 selected FP effect moves finite-far");
            std::memcpy(after.data()+0x28,before.data()+0x28,12);
            Check(after==before,"H4 effect orientation and other fields stay native");
        }
    }
    Halo4EffectHideBridge(nullptr,nullptr);
    Halo4EffectHideBridge(reinterpret_cast<void*>(1),reinterpret_cast<void*>(1));
    Check(true,"H4 missing or torn descriptor remains feature-local");
    VirtualFree(module,0,MEM_RELEASE);
}
}
int main(){H2::Run();H4::Run();std::printf("%u production muzzle suppression checks passed\n",checks);}
