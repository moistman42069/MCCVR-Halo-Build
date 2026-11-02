#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../src/common/scope_logic.h"
#include "../src/common/odst_bringup_logic.h"
#include "../src/common/runtime_types.h"
static unsigned checks{};
static void Check(bool ok,const char* reason){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
// ODST's independently documented layout; deliberately not H3's 0x90 derived block.
static struct {struct {
    uintptr_t compactSize=0x90,derivedSize=0xC0,rootCurrentCompact=8,rootCurrentDerived=0x98,
        rootSecondaryCompact=0x158,rootSecondaryDerived=0x1E8,nestedCurrentCompact=0x6D0,
        nestedCurrentDerived=0x760,nestedSecondaryCompact=0x820,nestedSecondaryDerived=0x8B0,
        compactPosition=0,compactForward=0xC,compactUp=0x18,verticalFov=0x28,referenceFov=0x2C,
        projectionMatrix=0x78;
} layout;} kOdstCameraProfile;
static std::atomic<uint32_t> g_odstRuntimeGeneration{7};
static std::atomic<float> g_aimFwdX{0},g_aimFwdY{1},g_aimFwdZ{0};
static std::atomic<bool> g_scopeRenderActive{false};
struct FpInterpolationContext{uint64_t tag=12,serial=23;};
static FpInterpolationContext g_fpInterpolationContexts[2];
static struct {
    std::atomic<bool> armed{true},teardownRequested{false};
    void(*buildViewport)(void*,void*)=nullptr;
    void(*buildMatrices)(void*,void*,void*,float)=nullptr;
    void(*prepareView)(void*,int)=nullptr;
    void(*originalRenderView)(void*)=nullptr;
    void(*fpCameraUpload)(void*,void*)=nullptr;
} g_odstCamera;
static bool cinematic=false,controllerValid=true,requested=true,targetReady=true,raster=false;
// Scope convergence (sight_layer.h): off unless a case publishes a converged direction.
static bool converge=false;
static float convergedForward[3]{},expectForward[3]{0,1,0};
static bool Sight_ScopeConvergedDirection(GameTitle title,float out[3])
{Check(title==GameTitle::Halo3ODST,"ODST asks for its own converged direction");if(!converge)return false;std::memcpy(out,convergedForward,12);return true;}
static float zoom=6,aspect=4.f/3.f;
static unsigned step=0,faultStep=0,draws=0,copies=0,ends=0,logs=0;
static alignas(16) unsigned char view[0xB00],saved[sizeof(view)];
static CinematicControlState ReadOdstCinematicControl(int32_t&,int32_t&)
{return cinematic?CinematicControlState::AuthoredLocked:CinematicControlState::PlayerControlled;}
static bool VR_ScopeShouldRenderThisFrame(){return requested;}
static bool ControllerWorldPoseEx(bool left,float* basis,float*,float&)
{Check(!left,"primary role follows current handedness routing");basis[0]=basis[4]=basis[8]=1;return controllerValid;}
static bool VR_GetScopeRenderAspect(float& out){out=aspect;return targetReady;}
static float VR_GetScopeZoom(){return zoom;}
static bool VR_BeginScopeRaster(){raster=targetReady;return raster;}
static void Phase(){++step;if(step==faultStep)RaiseException(0xE0431111,0,0,nullptr);}
static void VR_CaptureScope(){Phase();Check(raster&&g_scopeRenderActive,"scope capture owns private raster");++copies;}
static void VR_EndScopeRaster(){Check(raster,"raster ends once");raster=false;++ends;}
static void Viewport(void*,void*){Phase();}
static void Matrices(void*,void*,void* derived,float){std::memset(derived,0xBD,0xC0);Phase();}
static void Upload(void*,void*){Phase();}
static void Prepare(void*,int){Phase();}
static void Draw(void* input)
{
    ++draws;Check(input==view&&raster&&g_scopeRenderActive,"one private ODST native render");
    const auto& l=kOdstCameraProfile.layout;
    const auto* camera=reinterpret_cast<const float*>(view+l.rootCurrentCompact);
    Check(camera[0]==2&&camera[1]==3&&camera[2]==4&&camera[3]==expectForward[0]&&camera[4]==expectForward[1]&&
        camera[5]==expectForward[2],
        "scope stays at native gameplay origin, along the aim or along the converged intent ray");
    const auto lens=ComputeScopeProjectionTangents(zoom,aspect);
    const auto* matrix=reinterpret_cast<const float*>(view+l.rootCurrentDerived+l.projectionMatrix);
    Check(std::abs(matrix[0]-1.f/lens.horizontal)<.001f&&std::abs(matrix[5]-1.f/lens.vertical)<.001f,
        "scope projection preserves requested optical zoom and source aspect");
    const uintptr_t compact[]{l.rootSecondaryCompact,l.nestedCurrentCompact,l.nestedSecondaryCompact};
    const uintptr_t derived[]{l.rootSecondaryDerived,l.nestedCurrentDerived,l.nestedSecondaryDerived};
    for(int i=0;i<3;++i)
    {
        Check(std::memcmp(view+compact[i],camera,l.compactSize)==0,"every nested compact owns scope pose");
        Check(std::memcmp(view+derived[i],view+l.rootCurrentDerived,l.derivedSize)==0,"every nested derived owns scope projection");
    }
    g_fpInterpolationContexts[0]={99,100};g_fpInterpolationContexts[1]={101,102};
    // Native preparation may mutate any camera block. All eight must restore.
    for(auto offset:compact)std::memset(view+offset,0xAC,l.compactSize);
    for(auto offset:derived)std::memset(view+offset,0xDA,l.derivedSize);
    Phase();
}
#define LOG(...) (++logs)
#include "../src/dll/odst_scope.inl"
#undef LOG
static void Reset()
{
    for(size_t i=0;i<sizeof(view);++i)view[i]=uint8_t(i*13+7);
    const float origin[]{2,3,4};std::memcpy(view+8,origin,12);std::memcpy(saved,view,sizeof(view));
    g_odstRuntimeGeneration=7;g_odstScopeFaultGeneration=0;g_scopeRenderActive=false;
    g_odstCamera.armed=true;g_odstCamera.teardownRequested=false;
    g_odstCamera.buildViewport=Viewport;g_odstCamera.buildMatrices=Matrices;g_odstCamera.fpCameraUpload=Upload;
    g_odstCamera.prepareView=Prepare;g_odstCamera.originalRenderView=Draw;
    cinematic=false;controllerValid=requested=targetReady=true;raster=false;
    converge=false;expectForward[0]=0;expectForward[1]=1;expectForward[2]=0;
    step=faultStep=draws=copies=ends=0;g_fpInterpolationContexts[0]={12,23};g_fpInterpolationContexts[1]={34,45};
}
static void Restored()
{
    Check(std::memcmp(view,saved,sizeof(view))==0,"all camera bytes and surrounding canaries restored");
    Check(!raster&&!g_scopeRenderActive,"optional scope ownership released");
    Check(g_fpInterpolationContexts[0].tag==12&&g_fpInterpolationContexts[1].serial==45,"primary/secondary palette receipts restored");
    Check(g_odstCamera.armed&&!g_odstCamera.teardownRequested,"scope never disarms stereo camera");
}
int main()
{
    for(float z:{6.f,12.f,24.f})for(float a:{1.f,4.f/3.f,16.f/9.f})
    {Reset();zoom=z;aspect=a;RenderOdstScope(view);Check(draws==1&&copies==1&&ends==1,"one completed zoom view");Restored();}
    for(unsigned fault=1;fault<=6;++fault)
    {
        Reset();faultStep=fault;RenderOdstScope(view);Restored();
        Check(g_odstScopeFaultGeneration==7&&ends==1&&copies==0,"each native failure isolates optional generation");
        const auto oldDraws=draws;RenderOdstScope(view);Check(draws==oldDraws,"failed generation never retries native drawing");
    }
    for(int refusal=0;refusal<7;++refusal)
    {
        Reset();switch(refusal){case 0:cinematic=true;break;case 1:controllerValid=false;break;
        case 2:requested=false;break;case 3:targetReady=false;break;case 4:g_odstCamera.buildMatrices=nullptr;break;
        case 5:g_odstScopeFaultGeneration=7;break;case 6:g_odstRuntimeGeneration=0;break;}
        RenderOdstScope(view);Check(draws==0&&ends==0,"unproven scope never opens native transaction");Restored();
    }
    // Converged: the lens looks along the intent ray from its own origin (2,3,4), not along the aim (0,1,0).
    Reset();converge=true;convergedForward[0]=1;convergedForward[1]=0;convergedForward[2]=0;
    expectForward[0]=1;expectForward[1]=0;expectForward[2]=0;
    RenderOdstScope(view);Check(draws==1&&copies==1,"a converged lens renders once");Restored();
    ReportOdstScope();Check(logs==2,"fault details and the pass's account emitted only by worker report");
    ReportOdstScope();Check(logs==3,"a pass that went quiet is reported once");
    ReportOdstScope();Check(logs==3,"an unchanged pass is not reported again");
    std::printf("PASS: %u production ODST scope checks (native renderer stubbed)\n",checks);
}
