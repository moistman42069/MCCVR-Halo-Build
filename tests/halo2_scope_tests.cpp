#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../src/common/scope_logic.h"
#include "../src/common/halo2_render_logic.h"
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){printf("FAIL: %s\n",why);exit(1);}}
static std::atomic<uint32_t>g_zoomFaultGeneration{0},g_zoomFaults{0};
static bool requested=true,cameraValid=true,ready=true,active=false,scopeFlag=false,image=false,badProjection=false;
static unsigned draws=0,ends=0,step=0,fault=0;
static float aspect=1.5f,zoom=8;
static void Phase(){if(++step==fault)RaiseException(0xe0431111,0,0,nullptr);}
static bool VR_ScopeShouldRenderThisFrame(){return requested;}
static void VR_InvalidateScopeImage(){image=false;}
static bool VR_GetScopeRenderAspect(float& out){out=aspect;return ready;}
static float VR_GetScopeZoom(){return zoom;}
static bool VR_BeginHalo2Scope(uint32_t generation,uint64_t serial)
{Check(generation==7&&serial==99,"current completed-pair identity");active=ready;return active;}
static void VR_EndHalo2Scope(bool complete){++ends;image=complete&&active;active=false;}
static bool Game_SetScopeRendering(bool enabled){const auto prior=scopeFlag;scopeFlag=enabled;return prior;}
static bool Halo2Observer6Dof_BuildScopeCamera(Halo2CameraBasis& camera)
{camera={};camera.position[0]=2;camera.forward[0]=1;camera.up[2]=1;return cameraValid;}
namespace anniversary
{
static unsigned char state[0x200],expected[sizeof(state)],savedContext[kHalo2SaberSceneContextResetBytes],recordBytes[0x800];
static int32_t savedLatch=17,latch=31;
static bool applyOk=true,slotsIntact=true;
static bool ViewSlotsIntact(uintptr_t){return slotsIntact;}
static bool ReadGuarded(uintptr_t at,float& out){memcpy(&out,reinterpret_cast<void*>(at),4);Phase();return true;}
static bool ApplyEyeCamera(uintptr_t record,const Halo2CameraBasis& camera,int,const Halo2SaberEyeCover& cover)
{
    Check(camera.position[0]==2&&scopeFlag&&active,"optional camera owns pass");
    Check(latch==savedLatch&&!memcmp(state+kHalo2SaberSceneContextResetOffset,savedContext,sizeof(savedContext)),"native once/context reset for optional view");
    const float x=1/tanf(cover.halfHorizontalRadians)*(badProjection?2.f:1.f),y=1/tanf(cover.halfVerticalRadians);
    memcpy(reinterpret_cast<void*>(record+kHalo2SaberProjectionScaleXOffset),&x,4);
    memcpy(reinterpret_cast<void*>(record+kHalo2SaberProjectionScaleYOffset),&y,4);
    Phase();return applyOk;
}
static void Native(void* ctx,uintptr_t,int)
{++draws;memset(static_cast<unsigned char*>(ctx)+kHalo2SaberSceneContextResetOffset,0xDA,kHalo2SaberSceneContextResetBytes);latch=91;Phase();}
static void Run(bool ok=true,bool haveLatch=true)
{
    const uint32_t generation=7;const uint64_t serial=99;
    const uintptr_t base=reinterpret_cast<uintptr_t>(&latch)-kHalo2SaberSceneOnceLatchRva;
    void* ctx=state;uintptr_t record=reinterpret_cast<uintptr_t>(recordBytes),rdx=3;int viewIndex=0,constants=0;
    Halo2SaberEyeCover cover{};auto original=&Native;
    // The fixture's faults are RaiseException (kernelbase), so it keeps the
    // contain-everything filter; the GameFault decision has its own tests.
    const uintptr_t faultAnchor=0;(void)faultAnchor;
#define GameFault(anchor) EXCEPTION_EXECUTE_HANDLER
#include "../src/dll/halo2_anniversary_scope_pass.inl"
#undef GameFault
    Check(!memcmp(state,expected,sizeof(state)),"third pass restores exact post-eye context including exterior sentinels");
    Check(!active&&!scopeFlag,"optional flags end even after exception");
}
static void Reset()
{
    for(size_t i=0;i<sizeof(state);++i)state[i]=expected[i]=static_cast<unsigned char>(i*19+7);
    memset(savedContext,0xBC,sizeof(savedContext));latch=31;applyOk=true;slotsIntact=true;
}
}
namespace classic
{
struct StereoScope
{
    uint8_t completedEyeMask=3;bool invalidated=false;bool mono=false;uint32_t generation=7;uint64_t serial=99;
    struct {Halo2CameraBasis render,raster;}eyes[2];
    float renderCoverVerticalFov{},rasterCoverVerticalFov{};
    bool sceneTargetLatchValid=true;uint8_t sceneTargetLatch=17;
    bool engineHalfFovsValid[2]{};Halo2SymmetricHalfFovs engineHalfFovs[2];
};
static uint8_t latch=31;static unsigned char cameraBytes[0x90],savedCamera[0x90];
static bool writeOk=true,restoreOk=true;
static bool ReadByteGuarded(uintptr_t at,uint8_t& out){out=*reinterpret_cast<uint8_t*>(at);return true;}
static bool WriteByteGuarded(uintptr_t at,uint8_t value){*reinterpret_cast<uint8_t*>(at)=value;return true;}
static bool WriteEyeSpans(StereoScope& scope,int eye)
{
    Check(eye==0&&scope.eyes[0].render.position[0]==2&&scope.eyes[0].raster.position[0]==2,"both native scope cameras updated");
    Check(latch==17&&scopeFlag&&active,"classic pass owns native latch");
    memset(cameraBytes,0xCD,sizeof(cameraBytes));Phase();return writeOk;
}
static bool RestoreOwnedSpans(StereoScope&){memcpy(cameraBytes,savedCamera,sizeof(cameraBytes));return restoreOk;}
static bool ReadEngineProjection(StereoScope& scope,int)
{
    const auto p=ComputeScopeProjectionTangents(zoom,aspect);
    scope.engineHalfFovsValid[0]=true;scope.engineHalfFovs[0]={atanf(p.horizontal)*(badProjection?2.f:1.f),atanf(p.vertical)};
    Phase();return true;
}
static void Native(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,
    uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t)
{++draws;memset(cameraBytes,0xDA,sizeof(cameraBytes));latch=91;Phase();}
static void Run(bool eligible=true,bool mono=false,int first=0)
{
    // A mono frame is one pass, as the eye drawn first: that eye alone completes it.
    StereoScope owned{};auto* scope=&owned;scope->mono=mono;
    const uint8_t firstMask=static_cast<uint8_t>(1u<<first);
    if(mono)scope->completedEyeMask=eligible?firstMask:0;else if(!eligible)scope->completedEyeMask=1;
    const int firstEye=first;
    const uintptr_t moduleBase=reinterpret_cast<uintptr_t>(&latch)-kHalo2ClassicSceneTargetLatchRva;
    auto original=&Native;
    uintptr_t argument01=1,argument02=2,argument03=3,argument04=4,argument05=5,argument06=6,argument07=7,
        argument08=8,argument09=9,argument10=10,argument11=11,argument12=12,argument13=13,argument14=14,
        argument15=15,argument16=16,argument17=17,argument18=18,argument19=19;
#include "../src/dll/halo2_classic_scope_pass.inl"
    Check(latch==31&&!memcmp(cameraBytes,savedCamera,sizeof(cameraBytes)),"classic optional camera and prior latch restore exactly");
    Check(!active&&!scopeFlag,"classic optional flags end even after exception");
    Check(scope->completedEyeMask==(mono?(eligible?firstMask:0):(eligible?3:1))&&!scope->invalidated,
        "optional faults preserve completed real eyes");
}
static void Reset(){latch=31;memset(cameraBytes,0xC3,sizeof(cameraBytes));memcpy(savedCamera,cameraBytes,sizeof(cameraBytes));writeOk=restoreOk=true;}
}
static void Reset()
{
    requested=cameraValid=ready=true;active=scopeFlag=image=badProjection=false;
    draws=ends=step=fault=0;g_zoomFaultGeneration=g_zoomFaults=0;anniversary::Reset();classic::Reset();
}
int main()
{
    for(bool anniv:{false,true})
    {
        for(float a:{.75f,1.f,1.8f})for(float z:{6.f,12.f,24.f})
        {Reset();aspect=a;zoom=z;if(anniv)anniversary::Run();else classic::Run();Check(draws==1&&ends==1&&image,"one magnified render per completed pair");}
        for(unsigned f=1;f<=(anniv?4u:3u);++f)
        {Reset();fault=f;if(anniv)anniversary::Run();else classic::Run();Check(!image&&ends==1&&g_zoomFaultGeneration==7,"native exception isolates only optional scope");}
        for(int mode=0;mode<6;++mode)
        {
            Reset();switch(mode){case 0:requested=false;break;case 1:cameraValid=false;break;case 2:ready=false;break;
            case 3:g_zoomFaultGeneration=7;break;case 4:badProjection=true;break;case 5:anniversary::applyOk=false;classic::writeOk=false;break;}
            if(anniv)anniversary::Run();else classic::Run();Check(!image,"no lens without complete camera/resource/projection proof");
        }
    }
    Reset();classic::Run(false);Check(!draws&&!ends,"incomplete real pair cannot start lens");
    Reset();classic::Run(true,true);Check(draws==1&&ends==1&&image,"a mono frame's one completed eye starts the lens");
    Reset();classic::Run(false,true);Check(!draws&&!ends,"a mono frame whose one eye did not complete cannot start lens");
    Reset();classic::Run(true,true,1);Check(draws==1&&ends==1&&image,"a mono frame drawn as the right eye starts the lens");
    Reset();anniversary::Run(false);Check(!draws&&!ends,"failed Anniversary real pair cannot start lens");
    Reset();anniversary::Run(true,false);Check(!draws&&!ends,"missing native latch refuses optional pass");
    Reset();anniversary::slotsIntact=false;anniversary::Run();Check(!draws&&!ends,"an emptied view slot refuses the optional pass");
    printf("Halo 2 scope: %u checks passed\n",checks);
}
