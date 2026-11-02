static bool muzzleHookExists=false,muzzleHookEnabled=false,muzzleIngress=false;
static int muzzleLifecycleFailure=0,muzzleScan=0;
static uintptr_t muzzleFixtureBase=0;
static std::atomic<bool> g_halo3MuzzleBindingsReady{false};
static std::atomic<bool> g_halo3GripOwnerReady{false};
namespace MuzzleSig
{
static uintptr_t Find(uintptr_t base,size_t,const char*)
{
    const uint32_t rvas[]{0x343D74,0x2C24AC,0x2C250F};
    const int index=muzzleScan++/2;
    Check(index<3,"bounded H3 marker and FP ownership proof inventory");
    if(muzzleLifecycleFailure==1)return 0;
    if(base==muzzleFixtureBase)return base+rvas[index];
    return muzzleLifecycleFailure==2?base+0x100:0;
}
}
static MH_STATUS MuzzleCreate(void* target,void* detour,void** original)
{
    Check(target==reinterpret_cast<void*>(muzzleFixtureBase+0x343D74)&&
        detour==reinterpret_cast<void*>(&Halo3MuzzleMarkersDetour),"only exact marker wrapper hooked");
    if(muzzleLifecycleFailure==3)return MH_ERROR_MEMORY_ALLOC;
    muzzleHookExists=true;*original=reinterpret_cast<void*>(0x9000);return MH_OK;
}
static MH_STATUS MuzzleEnable(void*)
{if(muzzleLifecycleFailure==4)return MH_ERROR_MEMORY_PROTECT;muzzleHookEnabled=true;return MH_OK;}
static MH_STATUS MuzzleDisable(void*)
{if(muzzleLifecycleFailure==5)return MH_ERROR_MEMORY_PROTECT;muzzleHookEnabled=false;return MH_OK;}
static MH_STATUS MuzzleRemove(void*)
{
    Check(!muzzleHookEnabled&&!muzzleIngress&&!g_halo3Muzzle.callbacks,"marker trampoline only freed after complete drain");
    if(muzzleLifecycleFailure==6)return MH_ERROR_MEMORY_PROTECT;
    muzzleHookExists=false;return MH_OK;
}
static bool MuzzleQuiescence(const void* const* functions,const void* const* originals,size_t count,
    const std::atomic<uint32_t>& callbacks)
{
    Check(count==1&&functions[0]==reinterpret_cast<void*>(&Halo3MuzzleMarkersDetour)&&
        originals[0]==reinterpret_cast<void*>(0x9000),"marker retirement protects exact detour and trampoline");
    return !callbacks.load()&&!muzzleIngress;
}
#define sig MuzzleSig
#define MCCVR_DisableHookForRetirement MuzzleDisable
#define WaitForNativeDetourQuiescence MuzzleQuiescence
#define MH_CreateHook MuzzleCreate
#define MH_EnableHook MuzzleEnable
#define MCCVR_EnableHook MuzzleEnable
#define MH_RemoveHook MuzzleRemove
#define InstallHalo3Muzzle FixtureInstallMuzzle
#define RemoveHalo3Muzzle FixtureRemoveMuzzle
#define LOG(...) (++logged)
#include "../src/dll/halo3_muzzle_lifecycle.inl"
#undef sig
#undef MCCVR_DisableHookForRetirement
#undef WaitForNativeDetourQuiescence
#undef MH_CreateHook
#undef MH_EnableHook
#undef MCCVR_EnableHook
#undef MH_RemoveHook
#undef InstallHalo3Muzzle
#undef RemoveHalo3Muzzle
#undef LOG
static void MuzzleLifecycleTests()
{
    constexpr size_t size=0x900000;
    auto* bytes=static_cast<uint8_t*>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Check(bytes!=nullptr,"bounded native muzzle image fixture");
    muzzleFixtureBase=reinterpret_cast<uintptr_t>(bytes);
    for(const auto [call,target]:{std::pair{0x36864C,0x343D74}})
    {bytes[call]=0xE8;const int32_t delta=target-call-5;std::memcpy(bytes+call+1,&delta,4);}
    g_halo3Dual.enabled=true;g_halo3Dual.generation=7;g_halo3Muzzle.target=nullptr;g_halo3Muzzle.original=nullptr;
    for(int failure=0;failure<=8;++failure)
    {
        muzzleLifecycleFailure=failure<=4?failure:0;muzzleScan=0;
        const bool installed=FixtureInstallMuzzle(muzzleFixtureBase,size);
        Check(installed==g_halo3MuzzleBindingsReady.load()&&installed==(failure==0||failure>4),"optional marker install admission and creation failures");
        Check(g_halo3Dual.enabled.load(),"marker failure never disables independent firing");
        if(failure>=5)
        {
            muzzleLifecycleFailure=failure;
            if(failure==7)g_halo3Muzzle.callbacks=1;
            if(failure==8)muzzleIngress=true;
            Check(!FixtureRemoveMuzzle()&&g_halo3Muzzle.target&&g_halo3Muzzle.original&&muzzleHookExists,
                "failed marker retirement retains target and native original");
        }
        muzzleLifecycleFailure=0;muzzleIngress=false;g_halo3Muzzle.callbacks=0;
        Check(FixtureRemoveMuzzle()&&!g_halo3MuzzleBindingsReady.load()&&!g_halo3Muzzle.target&&!g_halo3Muzzle.original&&!muzzleHookExists,
            "marker cleanup succeeds on retry");
    }
    muzzleScan=0;bytes[0x36864C]=0x90;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase,size)&&g_halo3Dual.enabled.load(),"changed native edge refuses only muzzle feature");
    Check(VirtualFree(bytes,0,MEM_RELEASE)!=0,"muzzle fixture allocation released");
}
