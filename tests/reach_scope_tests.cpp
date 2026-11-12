#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "../src/common/scope_logic.h"
#include "../src/common/reach_wind_replay.h"
#include "../src/common/reach_render_logic.h"
static unsigned checks{};
static void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
constexpr uintptr_t kReachPvSnapshotBegin=kReachPlayerViewCameraStateOffset;
constexpr size_t kReachCompactCameraBytes=kReachCompactCameraSize;
constexpr size_t kReachPvSnapshotBytes=kReachLastWindowFlagOffset+1-kReachPvSnapshotBegin;
struct ReachVrRenderAccess{bool active=true;};
struct ReachScopeFrame{bool ready=false;alignas(16) unsigned char compact[kReachCompactCameraBytes]{},derived[kReachDerivedBlockSize]{};};
struct ReachOwnerScope{uintptr_t workspace=0;bool cutsceneTheater=false;unsigned char headCenter[kReachCompactCameraBytes]{};ReachScopeFrame scope{};};
static ReachOwnerScope g_reachOwnerScope;
static bool g_reachScopeRendering=false;
static std::atomic<bool> g_scopeRenderActive{false},g_reachCinematicLocked{false};
static std::atomic<float> g_aimFwdX{1},g_aimFwdY{0},g_aimFwdZ{0};
static std::atomic<uint32_t> g_reachScopeFaultGeneration{0},g_reachScopeFaults{0},g_reachWindReplayFaults{0};
static std::atomic<uintptr_t> g_reachFirstPersonRenderGateReturn{1};
static struct{bool active=false;uint64_t serial=42;}g_reachFpCameraEyeScope;
static unsigned char wind[48]{};
static struct{std::atomic<uint32_t> generation{7};uintptr_t base=0;void* hudDrawWidgetTarget=wind;void* decoratorWindState=wind;}g_reachCamera;
static unsigned phase{},faultAt{},draws{},copies{},logs{};
static bool requested=true,resources=true,image=false;
// Scope convergence (game.h Game_ReachScopeConvergedDirection): off unless a case publishes a converged direction.
static bool converge=false;
static float convergedForward[3]{};
static bool Game_ReachScopeConvergedDirection(float out[3]){if(!converge)return false;std::memcpy(out,convergedForward,12);return true;}
static float zoom=6,aspect=16.f/9.f;
static alignas(16) unsigned char workspace[kReachRenderScopeSnapshotSize],view[0xA40],savedWorkspace[sizeof(workspace)],savedView[sizeof(view)];
static uintptr_t ownerGlobal{};
static void Phase(){if(++phase==faultAt)RaiseException(0xE0431111,0,0,nullptr);}
static bool Frustum(void*,float* b){Phase();for(int i=0;i<4;++i)b[i]=1;return true;}
static void Projection(void* camera,float*,void* derived,float)
{Phase();std::memset(derived,0xAD,kReachDerivedBlockSize);auto* p=reinterpret_cast<float*>(static_cast<unsigned char*>(derived)+kReachDerivedProjectionOffset);
const float y=std::tan(*reinterpret_cast<float*>(static_cast<unsigned char*>(camera)+0x28)/2);p[0]=1/(y*aspect);p[5]=1/y;}
static void State(void* p,void*){Phase();std::memset(p,0xCE,32);}
static void Matrix(void* p,void*,void*,void*,void*){Phase();std::memset(p,0xAB,64);}
static void Commit(){Phase();}
static struct {decltype(&Frustum) frustum=Frustum;decltype(&Projection) projection=Projection;
decltype(&State) cameraState=State;decltype(&Matrix) matrix=Matrix;decltype(&Commit) commitOuterCamera=Commit;}g_reachHelpers;
static bool VR_ScopeShouldRenderThisFrame(){return requested;}
static void VR_InvalidateScopeImage(){image=false;}
static bool VR_ReachScopeReady(const ReachVrRenderAccess& a){return a.active&&resources;}
static bool VR_GetScopeRenderAspect(float& a){a=aspect;return resources;}
static float VR_GetScopeZoom(){return zoom;}
static bool ControllerWorldPoseEx(bool left,float* b,float*,float&)
{Check(!left,"scope uses primary role");b[0]=b[4]=b[8]=1;return true;}
static bool ReachWindCopy(void* d,const void* s,size_t n){std::memcpy(d,s,n);return true;}
static bool ReachCallPlayerViewWithEyeScopedSuppressions(uintptr_t pv)
{
    ++draws;Check(g_reachScopeRendering&&g_scopeRenderActive,"scope suppression owns only optional pass");
    Check(pv==reinterpret_cast<uintptr_t>(view),"same proven player view");
    Check(view[kReachLastWindowFlagOffset]==0,"scope cannot run final-window cleanup");
    Check(ownerGlobal==pv+kReachPlayerViewCameraStateOffset,"native owner rearmed");
    Check(std::memcmp(workspace,workspace+kReachSecondaryCompactOffset,kReachCompactCameraBytes)==0,"scope primary/secondary coherent");
    const auto* p=reinterpret_cast<float*>(workspace+kReachPrimaryDerivedOffset+kReachDerivedProjectionOffset);
    const auto lens=ComputeScopeProjectionTangents(zoom,aspect);
    Check(std::fabs(p[0]*lens.horizontal-1)<.005f&&std::fabs(p[5]*lens.vertical-1)<.005f,"native lens magnification matches source");
    std::memset(wind,0xCC,sizeof(wind));ownerGlobal=0;Phase();return true;
}
static bool VR_ReachCopyScope(ReachVrRenderAccess&){Phase();++copies;image=true;return true;}
#define LOG(...) (++logs)
#include "../src/dll/reach_scope.inl"
#undef LOG
static void Reset()
{
    g_reachOwnerScope={};g_reachOwnerScope.workspace=reinterpret_cast<uintptr_t>(workspace);
    float* h=reinterpret_cast<float*>(g_reachOwnerScope.headCenter);h[0]=2;h[1]=3;h[2]=4;h[3]=1;h[8]=1;h[10]=1.8f;
    for(size_t i=0;i<sizeof(workspace);++i)workspace[i]=static_cast<unsigned char>(i*13);
    for(size_t i=0;i<sizeof(view);++i)view[i]=static_cast<unsigned char>(i*7);
    std::memcpy(savedWorkspace,workspace,sizeof(workspace));std::memcpy(savedView,view,sizeof(view));
    ownerGlobal=reinterpret_cast<uintptr_t>(view)+kReachPlayerViewCameraStateOffset;
    g_reachCamera.base=reinterpret_cast<uintptr_t>(&ownerGlobal)-kReachRenderCameraOwnerRva;
    std::memset(wind,0x34,sizeof(wind));g_reachScopeRendering=false;g_scopeRenderActive=false;
    g_reachScopeFaultGeneration=0;g_reachScopeFaults=0;g_reachCinematicLocked=false;g_reachFpCameraEyeScope={true,42};
    phase=faultAt=draws=copies=0;requested=resources=true;image=false;g_aimFwdX=1;g_aimFwdY=0;
    converge=false;
}
static bool Prepare(ReachVrRenderAccess& access)
{unsigned char derived[kReachDerivedBlockSize]{};float bounds[4]{};Projection(g_reachOwnerScope.headCenter,bounds,derived,0);
ReachPrepareScope(g_reachOwnerScope,derived,access);return g_reachOwnerScope.scope.ready;}
static void Restored()
{
    Check(std::memcmp(workspace,savedWorkspace,sizeof(workspace))==0,"all camera and callback bytes restored");
    Check(std::memcmp(view,savedView,sizeof(view))==0,"all player-view bytes/last-window restored");
    Check(ownerGlobal==reinterpret_cast<uintptr_t>(view)+kReachPlayerViewCameraStateOffset,"owner restored for first real eye");
    Check(!g_scopeRenderActive&&!g_reachScopeRendering&&g_reachFpCameraEyeScope.active&&g_reachFpCameraEyeScope.serial==42,"FP receipts and scope ownership restored");
    for(auto b:wind)Check(b==0x34,"scope wind advance removed before real eye pair");
}
int main()
{
    ReachVrRenderAccess access{};
    for(float z:{6.f,12.f,24.f})for(float a:{1.f,4.f/3.f,16.f/9.f})
    {Reset();zoom=z;aspect=a;Check(Prepare(access),"scope camera prepared");ReachRenderScope(reinterpret_cast<uintptr_t>(view),access);
    Check(draws==1&&copies==1&&image,"one separate optical draw/copy");Restored();}
    for(unsigned fail=1;fail<=5;++fail)
    {Reset();Check(Prepare(access),"fault fixture prepared");phase=0;faultAt=fail;ReachRenderScope(reinterpret_cast<uintptr_t>(view),access);
    Check(draws<=1&&copies==0&&!image&&g_reachScopeFaultGeneration==7,"fault isolates scope without native draw replay");Restored();}
    Reset();g_aimFwdX=-1;Check(!Prepare(access),"back-facing lens cannot corrupt head perspective culling");
    Reset();resources=false;Check(!Prepare(access),"missing GPU cache refuses scope only");
    Reset();g_reachCinematicLocked=true;Check(!Prepare(access),"authored cinematics refuse scope");
    Reset();requested=false;Check(!Prepare(access),"disabled scope changes nothing");
    // Converged: the lens looks from the head along the intent ray, 30 deg off the game's aim (which the
    // converged aim bends for the engine eye, so from the head it runs parallel to the shot and misses the point).
    {
        Reset();converge=true;
        const float c=std::cos(30.f*.0174532925f),s=std::sin(30.f*.0174532925f);
        convergedForward[0]=c;convergedForward[1]=s;convergedForward[2]=0;
        Check(Prepare(access),"a converged lens is prepared");
        const auto* forward=reinterpret_cast<const float*>(g_reachOwnerScope.scope.compact+0xC);
        const auto* position=reinterpret_cast<const float*>(g_reachOwnerScope.scope.compact);
        Check(std::fabs(forward[0]-c)<1e-5f&&std::fabs(forward[1]-s)<1e-5f&&std::fabs(forward[2])<1e-5f,
            "the converged lens looks along the intent ray");
        Check(position[0]==2&&position[1]==3&&position[2]==4,"the converged lens stays at the head");
        ReachRenderScope(reinterpret_cast<uintptr_t>(view),access);
        Check(draws==1&&copies==1&&image,"the converged lens draws once");Restored();
    }
    // Not converged (the converged aim not written, scope_converge off): the lens keeps the game's aim.
    {
        const float c=std::cos(30.f*.0174532925f),s=std::sin(30.f*.0174532925f);
        Reset();converge=false;g_aimFwdX=c;g_aimFwdY=s;
        Check(Prepare(access),"an unconverged lens is prepared");
        const auto* forward=reinterpret_cast<const float*>(g_reachOwnerScope.scope.compact+0xC);
        Check(std::fabs(forward[0]-c)<1e-5f&&std::fabs(forward[1]-s)<1e-5f&&std::fabs(forward[2])<1e-5f,
            "the lens keeps the game's aim");
    }
    const float f[]{1,0,0},u[]{0,0,1};
    for(int degrees=-70;degrees<=70;degrees+=5)
    {const float radians=degrees*.0174532925f;ScopeCameraPose pose{};pose.forward[0]=cosf(radians);pose.forward[1]=sinf(radians);pose.up[2]=1;
    ScopeProjectionTangents head{1,.7f};Check(ExpandScopeCullTangents(f,u,pose,{.1f,.1f},head),"visible scope fits shared hemisphere");
    Check(head.horizontal>=1&&head.vertical>=.7f,"scope union never narrows head visibility");}
    std::printf("Reach scope: %u checks passed\n",checks);
}
