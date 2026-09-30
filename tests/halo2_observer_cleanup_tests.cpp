#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace {
enum class CoreState { Installed,CleanupRequired,StockFallback };
CoreState g_coreState=CoreState::Installed;
constexpr bool kHalo2DebugGlobalAimAssistOverrideEnabled=false;
void Game_Halo2RestoreAimAssist(){}
bool optionalReady=true;
bool hapticReady=true;
bool RemoveHalo2WeaponHaptics(){return hapticReady;}
bool RemoveHalo2ContactMelee(){return optionalReady;}
bool RemoveHalo2DualAim(){return optionalReady;}
bool RemoveHalo2WorldCollision(){return optionalReady;}
std::atomic<bool> g_armed{},g_teardownRequested{},g_finalPaletteReady{},g_vehicleSeatVerified{},
    g_vehicleFrameVerified{},g_passCamerasSet{},g_installed{},g_referenceValid{},g_recenterRequested{};
std::atomic<uintptr_t> g_objectDatumAccessor{},g_interpolatorResetAddress{},g_observerResult{},g_moduleBase{};
std::atomic<uint32_t> g_weaponTickIndex{},g_weaponTickPreviousIndex{},g_weaponTickGeneration{},g_generation{};
std::atomic<uint64_t> g_packetBuilderLastAppliedMs{},g_vehicleSeatSample{};
struct Context { bool valid{}; };
Context g_visibleConsumerContext{},g_finalPaletteContext{},g_aimAssistControllerRayScope{};
std::array<Context,4> g_slotCache{};
HMODULE g_moduleReference{};
#define DECLARE(target,original,counter,detour) void* target{}; std::atomic<uintptr_t> original{}; std::atomic<uint32_t> counter{}; void detour(){}
DECLARE(g_target,g_originalAddress,g_activeCallbacks,Halo2ObserverFinalTransformDetour)
DECLARE(g_interpFrameTarget,g_interpFrameOriginal,g_interpFrameActiveCallbacks,Halo2InterpolatedFrameDetour)
DECLARE(g_packetBuilderTarget,g_packetBuilderOriginal,g_packetBuilderActiveCallbacks,Halo2FirstPersonPacketBuilderDetour)
DECLARE(g_visibleConsumerTarget,g_visibleConsumerOriginal,g_visibleConsumerActiveCallbacks,Halo2VisibleFirstPersonConsumerDetour)
DECLARE(g_finalPaletteTarget,g_finalPaletteOriginal,g_finalPaletteActiveCallbacks,Halo2FinalPaletteComposeDetour)
DECLARE(g_weaponAimTarget,g_weaponAimOriginal,g_weaponAimActiveCallbacks,Halo2WeaponAimHelperDetour)
DECLARE(g_aimAssistTarget,g_aimAssistOriginal,g_aimAssistActiveCallbacks,Halo2AimAssistCalculateDetour)
DECLARE(g_aimAssistViewDirectionTarget,g_aimAssistViewDirectionOriginal,g_aimAssistViewDirectionActiveCallbacks,Halo2AimAssistViewDirectionDetour)
DECLARE(g_nativeAimTarget,g_nativeAimOriginal,g_nativeAimActiveCallbacks,Halo2NativeAimUpdateDetour)
DECLARE(g_reanchorTarget,g_reanchorOriginal,g_reanchorActiveCallbacks,Halo2InterpolatorReadDetour)
DECLARE(g_weaponsTarget,g_weaponsOriginal,g_weaponsActiveCallbacks,Halo2FirstPersonWeaponsDetour)
DECLARE(g_particleTarget,g_particleOriginal,g_particleActiveCallbacks,Halo2ParticleRendererDetour)
#undef DECLARE
void** targets[]{&g_target,&g_interpFrameTarget,&g_packetBuilderTarget,&g_visibleConsumerTarget,
    &g_finalPaletteTarget,&g_weaponAimTarget,&g_aimAssistTarget,&g_aimAssistViewDirectionTarget,
    &g_nativeAimTarget,&g_reanchorTarget,&g_weaponsTarget,&g_particleTarget};
std::atomic<uintptr_t>* originals[]{&g_originalAddress,&g_interpFrameOriginal,&g_packetBuilderOriginal,
    &g_visibleConsumerOriginal,&g_finalPaletteOriginal,&g_weaponAimOriginal,&g_aimAssistOriginal,
    &g_aimAssistViewDirectionOriginal,&g_nativeAimOriginal,&g_reanchorOriginal,&g_weaponsOriginal,&g_particleOriginal};
std::atomic<uint32_t>* counters[]{&g_activeCallbacks,&g_interpFrameActiveCallbacks,&g_packetBuilderActiveCallbacks,
    &g_visibleConsumerActiveCallbacks,&g_finalPaletteActiveCallbacks,&g_weaponAimActiveCallbacks,&g_aimAssistActiveCallbacks,
    &g_aimAssistViewDirectionActiveCallbacks,&g_nativeAimActiveCallbacks,&g_reanchorActiveCallbacks,&g_weaponsActiveCallbacks,&g_particleActiveCallbacks};
const void* detours[]{reinterpret_cast<void*>(&Halo2ObserverFinalTransformDetour),reinterpret_cast<void*>(&Halo2InterpolatedFrameDetour),
    reinterpret_cast<void*>(&Halo2FirstPersonPacketBuilderDetour),reinterpret_cast<void*>(&Halo2VisibleFirstPersonConsumerDetour),
    reinterpret_cast<void*>(&Halo2FinalPaletteComposeDetour),reinterpret_cast<void*>(&Halo2WeaponAimHelperDetour),
    reinterpret_cast<void*>(&Halo2AimAssistCalculateDetour),reinterpret_cast<void*>(&Halo2AimAssistViewDirectionDetour),
    reinterpret_cast<void*>(&Halo2NativeAimUpdateDetour),reinterpret_cast<void*>(&Halo2InterpolatorReadDetour),
    reinterpret_cast<void*>(&Halo2FirstPersonWeaponsDetour),reinterpret_cast<void*>(&Halo2ParticleRendererDetour)};
std::array<bool,12> enabled{},exists{},quiesced{};
int failDisable=-1,failRemove=-1,ingress=-1;
unsigned checks{},removes{};
void Check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"H2 cleanup: %s\n",message);std::exit(1);}}
int Index(void* target){return int((reinterpret_cast<uintptr_t>(target)-0x1000)/0x100);}
MH_STATUS MCCVR_DisableHookForRetirement(void* target)
{
    const int i=Index(target);Check(i>=0&&i<12,"known target");
    if(i==failDisable)return MH_ERROR_MEMORY_PROTECT;
    if(!exists[i])return MH_ERROR_NOT_CREATED;
    const bool wasEnabled=enabled[i];enabled[i]=false;
    return wasEnabled?MH_OK:MH_ERROR_DISABLED;
}
MH_STATUS FixtureRemove(void* target)
{
    const int i=Index(target);++removes;
    Check(!enabled[i]&&quiesced[i]&&!counters[i]->load(),"never free live callback or ingress");
    if(i==failRemove)return MH_ERROR_MEMORY_PROTECT;
    const bool wasPresent=exists[i];exists[i]=false;
    return wasPresent?MH_OK:MH_ERROR_NOT_CREATED;
}
bool WaitForNativeDetourQuiescence(const void* const* functions,const void* const* saved,size_t count,const std::atomic<uint32_t>& counter)
{
    Check(count==1,"one exact callback range");
    int found=-1;
    for(int i=0;i<12;++i)if(counters[i]==&counter)found=i;
    Check(found>=0,"known counter");const int i=found;
    Check(functions[0]==detours[i]&&reinterpret_cast<uintptr_t>(saved[0])==originals[i]->load(),"matching entry and trampoline checked");
    Check(!enabled[i],"disabled before checking ingress");
    if(i!=11)for(int j=0;j<11;++j)Check(!enabled[j],"all core entries disabled before freeing any");
    if(counter.load()||i==ingress)return false;
    quiesced[i]=true;return true;
}
#define MH_RemoveHook FixtureRemove
#define LOG(...) ((void)0)
#include "../src/dll/halo2_observer_cleanup.inl"
#undef LOG
#undef MH_RemoveHook
void Reset()
{
    for(int i=0;i<12;++i){*targets[i]=reinterpret_cast<void*>(0x1000+size_t(i)*0x100);*originals[i]=0x8000+i*0x100;
        *counters[i]=0;enabled[i]=exists[i]=true;quiesced[i]=false;}
    g_coreState=CoreState::Installed;g_armed=true;g_teardownRequested=false;g_finalPaletteReady=true;
    g_generation=7;g_moduleBase=0x123000;g_installed=true;g_moduleReference=reinterpret_cast<HMODULE>(0x123000);
    g_objectDatumAccessor=0x456000;g_referenceValid=true;optionalReady=true;hapticReady=true;
    failDisable=failRemove=ingress=-1;removes=0;
}
}
int main()
{
    for(int mode=0;mode<4;++mode)for(int i=0;i<12;++i)
    {
        Reset();
        if(mode==0)failDisable=i;
        if(mode==1)*counters[i]=1;
        if(mode==2)ingress=i;
        if(mode==3)failRemove=i;
        Check(!RemoveCore("fixture level change"),"failure keeps cleanup pending");
        Check(!g_armed&&g_teardownRequested&&!g_finalPaletteReady&&g_coreState==CoreState::CleanupRequired,"feature disarmed on all failures");
        Check(g_generation==7&&g_moduleBase==0x123000&&g_objectDatumAccessor==0x456000&&g_installed&&g_moduleReference,
            "dependencies retained until all hooks retire");
        Check(*targets[i]&&originals[i]->load(),"failed target retains its original");
        if(mode!=3)for(int j=0;j<11;++j)Check(exists[j],"no core trampoline removed before complete disable/drain");
        failDisable=failRemove=ingress=-1;*counters[i]=0;
        Check(RemoveCore("fixture retry"),"partial cleanup retries successfully");
        for(int j=0;j<12;++j)Check(!*targets[j]&&!originals[j]->load()&&!exists[j],"successful cleanup clears all hook pairs");
        Check(g_generation==0&&g_moduleBase==0&&!g_installed&&!g_moduleReference&&!g_objectDatumAccessor,
            "dependencies cleared after success");
    }
    Reset();optionalReady=false;
    Check(!RemoveCore("optional feature still busy")&&g_target&&g_originalAddress,"optional dependency refusal retains core");
    optionalReady=true;Check(RemoveCore("optional retry"),"optional dependency retry");
    Reset();hapticReady=false;
    Check(!RemoveCore("haptic ingress busy")&&g_generation==7&&g_objectDatumAccessor==0x456000,
        "haptic retirement preserves weapon-reader and module dependencies until drained");
    hapticReady=true;Check(RemoveCore("haptic retry"),"haptic retirement retry completes before core teardown");
    Reset();for(int i=0;i<12;++i){enabled[i]=false;exists[i]=false;}
    Check(RemoveCore("already removed")&&g_generation==0,"absent entries converge safely");
    std::printf("PASS: %u Halo 2 production cleanup checks\n",checks);
}
