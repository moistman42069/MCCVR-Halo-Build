#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#include "../src/common/halo4_render_logic.h"
#include "../src/common/runtime_types.h"
#include "../src/common/scope_logic.h"
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){printf("FAIL: %s\n",why);exit(1);}}
struct Halo4SetupArgs{bool valid=true;uintptr_t view=1;uint32_t window=0,count=1,mode=0,user=0;uintptr_t observer=0;};
struct Halo4FloatingTargetFrame{Halo4ControllerWorldPoseInput common;};
static struct{bool rightAimValid=true;float rightAimOrientation[4]{0,0,0,1},rightAimPosition[3]{};}g_halo4RigTracking;
static struct{std::atomic<uint32_t>generation{7};}g_halo4Camera;
static std::atomic<uint32_t>g_halo4ScopeFaultGeneration{0},g_halo4ScopeFaults{0};
static std::atomic<bool>g_scopeRenderActive{false};
static bool g_halo4ScopeRendering=false,g_halo4ScopePixelsValid=true;
static bool g_halo4OrigCuiGameplayRender=true,g_halo4OrigModelSkinning=true;
static bool request=true,ready=true,freeze=true,writeOk=true,readOk=true,restoreOk=true,badProjection=false;
static bool raster=false,image=false,pixels=true;
static float aspect=1.5f,zoom=4,gain=.5f;
static unsigned step=0,fault=0,draws=0,copies=0,restores=0,ends=0;
static unsigned char observer[kHalo4ObserverSnapshotBytes],saved[sizeof(observer)],element[0x800];
static Halo4CameraBasis root{{2,3,4},{1,0,0},{0,0,1}};
// The rendered centre eye, 0.8 to the character's left (roomscale off), and the intent ray it looks along.
static Halo4CameraBasis center{{2,3.8f,4},{1,0,0},{0,0,1}};
static bool converge=false;
static float convergedForward[3]{1,0,0};
static const float* expectPosition=root.position;
static float expectForward[3]{1,0,0};
static bool Sight_ScopeConvergedDirection(GameTitle title,float out[3])
{Check(title==GameTitle::Halo4,"Halo 4 asks for its own converged direction");if(!converge)return false;memcpy(out,convergedForward,12);return true;}
static void Phase(){if(++step==fault)RaiseException(0xe0431111,0,0,nullptr);}
static bool VR_ScopeShouldRenderThisFrame(){return request;}
static void VR_InvalidateScopeImage(){image=false;}
static bool Halo4FreezeFloatingTargetFrame(Halo4FloatingTargetFrame&){return freeze;}
static bool VR_GetScopeRenderAspect(float& out){out=aspect;return ready;}
static float VR_GetScopeZoom(){return zoom;}
static bool VR_BeginScopeRaster(){raster=ready;return raster;}
static void VR_EndScopeRaster(){raster=false;++ends;}
static void VR_CaptureScope(){Phase();Check(raster&&g_halo4ScopeRendering,"scope owns capture");++copies;image=true;}
static bool Halo4SafeWrite(void* dst,const void* src,size_t n){if(!writeOk)return false;memcpy(dst,src,n);Phase();return true;}
static bool Halo4SafeRead(const void* src,void* dst,size_t n){if(!readOk)return false;memcpy(dst,src,n);Phase();return true;}
static void Setup(uintptr_t,uint32_t,uint32_t,uint32_t,uint32_t,uintptr_t at)
{
    Check(at==reinterpret_cast<uintptr_t>(observer),"native observer identity");
    Check(!memcmp(observer+kHalo4ObserverPositionOffset,expectPosition,12),
        "the lens sits on Chief's eye, or converged on the rendered eye");
    Check(!memcmp(observer+kHalo4ObserverForwardOffset,expectForward,12),
        "the lens looks along Chief's aim, or converged along the intent ray");
    for(size_t i=0;i<sizeof(observer);++i)
        if(!(i<12||(i>=0x28&&i<0x40)||(i>=0x78&&i<0x7C)))
            Check(observer[i]==saved[i],"opaque observer and FOV ratio remain native");
    float fov=0;memcpy(&fov,observer+kHalo4ObserverVerticalFovOffset,4);
    auto* projection=reinterpret_cast<float*>(element+kHalo4ElementProjectionMatrixOffset);
    memset(projection,0,64);projection[0]=1/(tanf(fov*gain)*aspect);projection[5]=1/tanf(fov*gain);
    projection[10]=1;projection[11]=-1;
    if(badProjection)projection[0]*=2;
    Phase();
}
static void Draw(uintptr_t,uintptr_t,uint32_t)
{++draws;Check(raster&&g_halo4ScopeRendering&&g_scopeRenderActive,"native render has isolated scope flags");g_halo4ScopePixelsValid=pixels;Phase();}
static bool Halo4RestoreMonoCamera(const Halo4SetupArgs&,const unsigned char* original)
{++restores;memcpy(observer,original,sizeof(observer));Phase();return restoreOk;}
static auto g_halo4OrigSetup=&Setup;
static auto g_halo4OrigWrapper=&Draw;
#include "../src/dll/halo4_scope.inl"
static void Reset()
{
    for(size_t i=0;i<sizeof(observer);++i)observer[i]=saved[i]=static_cast<unsigned char>(i*13+7);
    request=ready=freeze=writeOk=readOk=restoreOk=pixels=true;badProjection=raster=image=false;
    g_halo4RigTracking.rightAimValid=true;g_halo4RigTracking.rightAimOrientation[3]=1;
    g_halo4OrigCuiGameplayRender=g_halo4OrigModelSkinning=true;
    g_halo4ScopeRendering=false;g_scopeRenderActive=false;g_halo4ScopeFaultGeneration=0;g_halo4ScopeFaults=0;
    step=fault=draws=copies=restores=ends=0;
    converge=false;expectPosition=root.position;memcpy(expectForward,root.forward,12);
}
static void Run(bool learned=true)
{
    Halo4SetupArgs args{};args.observer=reinterpret_cast<uintptr_t>(observer);
    Halo4FovCalibration calibration{};calibration.gain=gain;calibration.learned=learned;
    Halo4RenderScope(reinterpret_cast<uintptr_t>(element),1,0,args,saved,root,center,calibration);
    Check(!raster&&!g_halo4ScopeRendering&&!g_scopeRenderActive,"all optional raster flags retired");
    Check(!memcmp(observer,saved,sizeof(observer)),"observer restored byte for byte");
}
int main()
{
    for(float a:{.75f,1.f,1.8f})for(float z:{1.f,4.f,10.f})for(float g:{.4f,.5f,.7f})
    {Reset();aspect=a;zoom=z;gain=g;Run();Check(draws==1&&copies==1&&image&&restores==1&&ends==1,"valid optical pass renders and restores once");}
    // Every external step can fault. Core state and callback ownership recover.
    for(unsigned f=1;f<=6;++f)
    {Reset();fault=f;Run();Check(g_halo4ScopeFaultGeneration==7&&!image,"exception isolates zoom for generation");Check(ends==1,"exception ends raster once");}
    for(int mode=0;mode<11;++mode)
    {
        Reset();bool learned=true;
        switch(mode){case 0:request=false;break;case 1:ready=false;break;case 2:freeze=false;break;
        case 3:writeOk=false;break;case 4:readOk=false;break;case 5:badProjection=true;break;
        case 6:pixels=false;break;case 7:learned=false;break;case 8:g_halo4RigTracking.rightAimValid=false;break;
        case 9:g_halo4OrigModelSkinning=false;break;case 10:g_halo4OrigCuiGameplayRender=false;break;}
        Run(learned);Check(!image&&copies==0,"missing proof never publishes scope pixels");
    }
    Reset();restoreOk=false;Run();Check(!image&&g_halo4ScopeFaultGeneration==7,"failed restore invalidates an already captured image");
    // Converged: the lens moves to the rendered eye and looks along that eye's intent ray, which passes through
    // the converged point. Chief's own aim passes through the point too, but from 0.8 away; the lens no longer
    // shows the view from where he stands.
    Reset();converge=true;expectPosition=center.position;expectForward[0]=1;expectForward[1]=0;expectForward[2]=0;
    Run();Check(draws==1&&copies==1&&image,"a converged lens renders from the rendered eye");
    // Converged along a ray off that eye's straight ahead: the lens turns with it.
    Reset();converge=true;expectPosition=center.position;
    convergedForward[0]=0;convergedForward[1]=1;convergedForward[2]=0;
    expectForward[0]=0;expectForward[1]=1;expectForward[2]=0;
    Run();Check(draws==1,"a converged lens turns with the ray");
    convergedForward[0]=1;convergedForward[1]=0;convergedForward[2]=0;
    printf("Halo 4 scope: %u checks passed\n",checks);
}
