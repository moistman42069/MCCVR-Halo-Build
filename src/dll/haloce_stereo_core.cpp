#include "haloce_stereo_core.h"
#include "haloce_controls.h"
#include "physical_crouch_camera.h"
uint64_t VR_PhysicalCrouchEpoch() noexcept;
#include "haloce_native_bindings.h"
#include "haloce_hud_layout.h"
#include "haloce_hud_target.h"
#include "../common/haloce_contracts.generated.h"
#include "../common/haloce_resource_registry.h"
#include "../common/haloce_surface_transfer.h"
#include "../common/haloce_view_construction.h"
#include "../common/haloce_classic_view_pair.h"
#include "../common/haloce_dlss_logic.h"
#include "../common/haloce_dlss_contract.h"
#include "../common/log.h"
#include "game.h"
#include "title_adapter.h"
#include "hook_quiescence.h"
#include "roomscale.h"
#include "../common/minhook_lifecycle.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include <array>

namespace
{
using namespace halo_ce;
// e17a664 was rejected in the September 14 headset test: 734 prepared
// frames, zero captured pairs, black VR and mismatched stacked desktop views.
// Keep the failed implementation intact for diagnosis, but do not install it.
constexpr bool kRejectedCeInitialStereoEnabled=false;
// 6e31b25 captured pairs but failed stereo/6DoF headset testing.
constexpr bool kCeSourceRasterStereoEnabled=false;
// b9662cd retained the same displaced right/flat left headset failure.
constexpr bool kCeConstructTrackedViewsEnabled=false;
// September 15 integrated candidate: Classic rendering, independently owned
// native consumers/depth, HUD replay, FP shader projection and motion-blur
// handling replace the camera-only experiments above. Headset test pending.
// be2140f: hands/aim improved, but Classic produced no pair and Anniversary
// retained the split/displaced world. Disable before developing a correction.
constexpr bool kCeIntegratedBaseVrEnabled=false;
// Correct the native scene-cache transition skipped by manufactured stereo,
// with Classic source/bootstrap and isolated HUD/mirror corrections retained.
// Pinned native refresh/admission and production fixtures pass; headset pending.
constexpr bool kCeSceneVisibilityBaseVrEnabled=true;
// Preserve the unfinished body-following adapter, but keep experimental CE
// locomotion out of the core VR candidate (September 15 user priority).
constexpr bool kCeExperimentalRoomscaleEnabled=false;
using PrepareFn=void(__fastcall*)(uintptr_t);
using BuilderFn=uintptr_t(__fastcall*)(uintptr_t,SaberViewPair*,uint8_t,float*);
// The first four append arguments are integer/pointer registers. Preserve all
// twelve remaining 8-byte ABI stack slots verbatim (native reads mixed widths).
using AppendFn=uintptr_t(__fastcall*)(uintptr_t,const SaberCamera*,uint32_t,int32_t,
    uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,
    uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
using FrameFn=void(__fastcall*)(uintptr_t,uint32_t);
using CameraUploadFn=void(__fastcall*)(uintptr_t,uintptr_t,const SaberCamera*);
using SceneCameraFn=void(__fastcall*)(uintptr_t,int32_t,const float*,const float*);
using DepthMeshFn=void(__fastcall*)(uintptr_t,uintptr_t,uintptr_t,int32_t);
using OutputFn=void(__fastcall*)(int);
using TransferFn=uintptr_t(__fastcall*)(uintptr_t,SurfaceTransfer*);
using CreateFn=uintptr_t(__fastcall*)(uintptr_t,uintptr_t);
using ReleaseFn=void(__fastcall*)(uintptr_t);
using CopyFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,
    UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
struct Hook { void* target{}; void* original{}; bool enabled{}; };
enum HookIndex { Prepare,Builder,Frame,Output,Transfer,Create,Import,Release,ResetList,Copy,Append,CameraUpload,DepthMesh,SceneCamera,Count };
std::array<Hook,Count> hooks;
NativeBindings bindings;
HMODULE moduleReference{};
std::atomic<bool> installed{},active{},armed{},retiring{},trackingEnabled{};
std::atomic<bool> gameplayBridgeVerified{};
std::atomic<bool> hudTargetBindingsVerified{};
std::atomic<uint32_t> callbacks{},generation{};
std::atomic<uint64_t> firstCameraMs{},lastCameraMs{},lastOwnedMs{},trackingAtMs{};
std::atomic<bool> recenter{true};
std::atomic<uint64_t> referenceRevision{1};
std::atomic<uintptr_t> copyTarget{};
std::atomic<uintptr_t> activeListAddress{};
std::atomic<uint64_t> built{},captured{},dropped{},stock{},descriptorMiss{},previewFolded{};
std::atomic<uint64_t> sceneVisibilityRefreshes{};
std::atomic<uint32_t> lastPairStage{0xffffffffu};
enum class FrameFailure : uint32_t { None,NoReceipt,InvalidReceipt,CameraChanged,CacheBegin,CopyShape,RasterChanged,EyeCopy,Destination,IncompletePair,RenderConsumer,DepthResource };
struct FrameDiagnostic
{
    FrameFailure failure{};
    uint32_t nativeCount{},nativeFlags{},cameraDifference{0xffffffffu},eyeMask{};
    float cameraWidth{},cameraHeight{};
    uint32_t sourceWidth{},sourceHeight{};
    float position[2][3]{};
    float origin[2][3]{},nearPlane[2]{},farPlane[2]{};
    uint32_t originMask{};
    uint32_t consumedDepth{},consumedScene{},consumedShading{},consumerFailure{};
    int32_t consumedPlayer[2]{-1,-1};
    uintptr_t consumedCamera[2]{},sourceWrapper[2]{},sourceResource[2]{},copyContext[2]{};
    uint32_t sourceSelectorFlags[2]{};
    uint32_t copyProof[2]{},copySourceSize[2][2]{},copyRequestSize[2][2]{};
    uintptr_t depthResource[2]{},depthView[2]{};
    uint32_t depthMask{},depthFailure{};
};
Snapshot<FrameDiagnostic> frameDiagnostic;
uint32_t rejectedGeneration{};
uint64_t lastReport{},resourceEpoch{};
Snapshot<Tracking> trackingSnapshot;
PreparedHandoff handoff;
ResourceRegistry resources;
std::atomic<uintptr_t> presentationTexture{};
EyeCache cache;
struct CompletedFrame { EyeCache::Key key; uint64_t referenceRevision{},capturedAtMs{}; };
Snapshot<CompletedFrame> completedFrame;
struct Wanted { D3D11_TEXTURE2D_DESC descriptor{}; uint32_t generation{}; uintptr_t context{}; };
Snapshot<Wanted> wanted,allocated;
Snapshot<Wanted> wantedDlssDepth;
std::atomic<bool> dlssDepthRequested{};
std::atomic<bool> dlssCameraBindingsVerified{};
Reference reference;
struct ReferenceSample { Reference value; uint64_t revision{}; };
Snapshot<ReferenceSample> publishedReference;
bool ClassicGetRenderContext(RenderContext& context) noexcept;
struct Prepared
{
    uintptr_t sourceList{};
    uint32_t generation{};
    bool synthetic{},valid{};
    PreparedReceipt receipt;
    uint64_t referenceRevision{};
};
Snapshot<Prepared> preparedLists[2],renderReady;
struct CopiedWorkerList { Prepared source;uintptr_t destination{};uint64_t rendererEpoch{},revision{}; };
Snapshot<CopiedWorkerList> copiedWorkerList;
std::atomic<uint64_t> copiedWorkerRevision{1};
void RevokeCopiedWorkerList() noexcept
{
    // Revocation must succeed even if a bounded snapshot publication is busy.
    copiedWorkerRevision.fetch_add(1,std::memory_order_acq_rel);
}
std::atomic_flag preparationBusy=ATOMIC_FLAG_INIT;
std::atomic_flag jobPreparationBusy=ATOMIC_FLAG_INIT;
struct JobScope { uintptr_t job{},activeList{}; bool owned{},cameraOwned{}; };
thread_local JobScope jobScope;
struct BuildScope { uintptr_t list{}; ViewConstruction views; bool enabled{},failed{}; };
thread_local BuildScope* buildScope{};
struct FrameScope
{
    bool synthetic{},capture{};
    uint32_t renderFlags{},hudAttempted{};
    int eye{-1};
    EyeCache::Key key;
    Prepared prepared;
    const SurfaceTransfer* transfer{};
    uintptr_t selectedSource{},selectedDestination{};
    uintptr_t renderer{};
    int lastSceneEye{-1};
    int primaryDrawEye{-1};
    unsigned primaryDrawStage{};
    FrameDiagnostic diagnostic;
    uintptr_t packedResource{};
    uint64_t packedRevision{};
    unsigned packedEyeMask{};
    D3D11_TEXTURE2D_DESC packedDescriptor{};
    struct DepthReceipt
    {
        uintptr_t root{},surface{},resource{},view{},backend{},context{};
        uint64_t revision{};
        D3D11_TEXTURE2D_DESC descriptor{};
    } depth[2];
    dlss::CameraSample dlssCameras[2]{};
};
thread_local FrameScope* frameScope{};
thread_local bool anniversaryMaterialMasked{};
struct Callback
{
    Callback() { callbacks.fetch_add(1,std::memory_order_acq_rel); }
    ~Callback() { callbacks.fetch_sub(1,std::memory_order_release); }
};
template<class T> bool Read(uintptr_t address,T& out) noexcept
{
    if (!address) return false;
    __try { std::memcpy(&out,reinterpret_cast<void*>(address),sizeof(T)); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Commit(SaberViewPair* destination,const StagedViewPair& pair) noexcept
{
    __try
    {
        // Private staging preserves opaque native resource fields. Never run
        // a native camera/list destructor on the private borrowed bytes.
        destination->views[0].camera=pair.cameras[0];
        destination->views[1].camera=pair.cameras[1];
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Current() noexcept
{
    return installed.load(std::memory_order_acquire)&&active.load(std::memory_order_acquire)&&
        !retiring.load(std::memory_order_acquire)&&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load(std::memory_order_acquire);
}
bool Anniversary() noexcept
{
    int mode{};
    return Read(bindings.base+0x1b7aa84,mode)&&mode!=0;
}
bool TrackingNow(Tracking& value) noexcept
{
    const uint64_t stamp=trackingAtMs.load(std::memory_order_acquire),now=GetTickCount64();
    return trackingEnabled.load(std::memory_order_acquire)&&stamp&&now>=stamp&&now-stamp<250&&
        trackingSnapshot.Read(value)&&value.serial&&value.generation==generation.load();
}
bool HasSyntheticLists() noexcept
{
    Prepared value{};
    for (const auto& slot:preparedLists)
        if (!slot.Read(value)||value.synthetic) return true;
    return !renderReady.Read(value)||value.synthetic;
}
bool SingleCamera() noexcept
{
    int count{}; uintptr_t backend{},array{},camera{}; int stereo{}; SaberCamera source{}; Camera checked{};
    return Read(bindings.base+0x2b17b98,count)&&count==1&&
        Read(bindings.base+0x2e3bdd8,backend)&&Read(backend+0x238,stereo)&&stereo==0&&
        Read(bindings.base+0x2b17b90,array)&&Read(array,camera)&&Read(camera,source)&&
        NativeCameraFromSaber(source,checked);
}
bool LiveGame() noexcept
{
    uintptr_t clock{}; uint8_t initialized{}; int32_t tick{};
    return Read(bindings.base+0x2e9fd68,clock)&&Read(clock,initialized)&&initialized==1&&
        Read(clock+0xc,tick)&&tick>0;
}
struct GameplayCameraSample
{
    RenderContext context;
    uint64_t capturedAtMs{};
};
Snapshot<GameplayCameraSample> gameplayCamera;
void PublishGameplayContext(const RenderContext& context) noexcept
{
    if (Valid(context.camera)&&HaloCE_RenderContextCurrent(context))
        gameplayCamera.Publish({context,GetTickCount64()});
}
void FollowRoomscale(const Camera& source,const Tracking& tracking,Reference& frozen,
    float scale,bool positional,uint64_t revision) noexcept
{
    if (!kCeExperimentalRoomscaleEnabled||!tracking.controllers.roomscaleEnabled||!Valid(source)||!Valid(frozen.orientation)||
        !Valid(tracking.headOrientation)||!Finite(tracking.headPosition)||!Finite(frozen.position)||
        !std::isfinite(scale)||scale<=0||tracking.generation!=frozen.generation||
        tracking.spaceEpoch!=frozen.spaceEpoch||revision!=referenceRevision.load()||recenter.load()) return;
    // Match H3/H2: measure the unmodified native center camera, ask ordinary
    // native walking to follow the physical step, and consume only observed
    // native motion from the tracking reference. No unit-position write.
    const Vec3 heading=ToNative(source,Rotate(Multiply(Conjugate(frozen.orientation),
        tracking.headOrientation),{0,0,-1}));
    if (!Finite(heading)) return;
    const float body[]{source.position.x,source.position.y,source.position.z};
    const float head[]{tracking.headPosition.x,tracking.headPosition.y,tracking.headPosition.z};
    const float orientation[]{tracking.headOrientation.x,tracking.headOrientation.y,
        tracking.headOrientation.z,tracking.headOrientation.w};
    const float forward[]{heading.x,heading.y,heading.z};
    float position[]{frozen.position.x,frozen.position.y,frozen.position.z};
    Roomscale_Camera(GameTitle::HaloCE,positional&&!tracking.controllers.controlsPresentationBlocked&&
        Game_RoomscaleCameraAllowed(GameTitle::HaloCE),body,head,orientation,forward,position,scale);
    const Vec3 next{position[0],position[1],position[2]};
    if (!Finite(next)||!Current()||revision!=referenceRevision.load()||recenter.load()) return;
    frozen.position=next;
    reference=frozen;
    publishedReference.Publish({frozen,revision});
}
// A per-frame effective reference applies one vertical camera correction to
// both eyes, controllers and native aim. The user's calibration stays intact.
void ApplyPhysicalCrouchReference(const Tracking& tracking,Reference& frozen,
    float scale,bool positional) noexcept
{
    if(!std::isfinite(scale)||scale<=0||scale>10||!Finite(tracking.headPosition)||
       !Finite(frozen.position))return;
    const float down=-std::clamp(tracking.headPosition.y-frozen.position.y,-4.f,4.f)*scale;
    const float correction=HaloCEControls_PhysicalCrouchCorrection(tracking.generation,
        VR_PhysicalCrouchEpoch(),down,positional&&!tracking.controllers.controlsPresentationBlocked);
    if(std::isfinite(correction)&&correction>0&&correction<=std::max(0.f,down))
        frozen.position.y-=correction/scale;
}
#include "haloce_classic_runtime.inl"
void PublishAnniversaryGameplayContext(const SaberCamera& nativeCenter,
    const Tracking& tracking,const Reference& frozen,uint64_t revision,
    float scale,bool positional) noexcept
{
    if (!Current()||CeObserveRendererMode()!=1) return;
    gameplayCamera.Publish({});
    if (!gameplayBridgeVerified.load(std::memory_order_acquire)||
        !std::isfinite(scale)||scale<=0||scale>10) return;
    Camera mapped{},center{}; Vec3 worldOffset{}; float forwardBias{};
    uint8_t freeCamera{}; uintptr_t scene{},externalCamera{};
    if (!Read(bindings.base+0x2e3b826,freeCamera)||freeCamera||
        !Read(bindings.base+0x2e3c418,scene)||!scene||scene>UINTPTR_MAX-0x138||
        !Read(scene+0x130,externalCamera)||externalCamera||
        !Read(bindings.base+0x2b05118,worldOffset)||!Read(bindings.base+0x2e3b838,forwardBias)||
        !NativeCameraFromSaber(nativeCenter,mapped)||
        !RecoverNativeCameraFromSaberBridge(mapped,worldOffset,forwardBias,center)) return;
    // The original primary append input is the engine's center camera. The
    // rendered eyes already contain HMD rotation/translation and must never
    // become the control/shot reference (that would apply tracking twice).
    PublishGameplayContext({tracking,frozen,center,scale,positional,revision,
        ceRendererEpoch.load(std::memory_order_acquire)});
}
uintptr_t AppendBody(uintptr_t list,const SaberCamera* source,uint32_t flags,int32_t index,
    uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9,uint64_t a10,
    uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a14,uint64_t a15,uint64_t a16,uintptr_t caller)
{
    auto* scope=buildScope;
    const int eye=caller==bindings.base+0x454a6e?0:caller==bindings.base+0x454c98?1:-1;
    const SaberCamera* selected=source;
    if (scope&&scope->enabled&&scope->list==list&&eye>=0)
    {
        SaberCamera native{}; uint32_t count{};
        const bool valid=!scope->failed&&(flags&~0x1000u)==(eye?0x20bu:0x10bu)&&
            Read(list+8,count)&&count==static_cast<uint32_t>(eye)&&
            Read(reinterpret_cast<uintptr_t>(source),native)&&
            scope->views.PrepareEye(eye,native,
                [](SaberCamera& camera) { return RebuildNativeCamera(bindings,camera); });
        if (valid) selected=&scope->views.staged.cameras[eye];
        else scope->failed=true;
    }
    return reinterpret_cast<AppendFn>(hooks[Append].original)(list,selected,flags,index,
        a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16);
}
uintptr_t __fastcall AppendHook(uintptr_t list,const SaberCamera* source,uint32_t flags,int32_t index,
    uint64_t a5,uint64_t a6,uint64_t a7,uint64_t a8,uint64_t a9,uint64_t a10,
    uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a14,uint64_t a15,uint64_t a16)
{
    Callback callback;
    return AppendBody(list,source,flags,index,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,a16,
        reinterpret_cast<uintptr_t>(_ReturnAddress()));
}
uintptr_t ConstructViews(BuilderFn original,uintptr_t arg,SaberViewPair* list,
    uint8_t secondary,float* settings,BuildScope* scope)
{
    const auto previous=buildScope;
    buildScope=scope;
    __try { return original(arg,list,secondary,settings); }
    __finally { buildScope=previous; }
}
uintptr_t __fastcall BuilderHook(uintptr_t arg,SaberViewPair* list,uint8_t secondary,float* settings)
{
    Callback callback;
    const auto original=reinterpret_cast<BuilderFn>(hooks[Builder].original);
    const uintptr_t address=reinterpret_cast<uintptr_t>(list);
    const bool scoped=jobScope.owned&&(address==jobScope.activeList||address==jobScope.job+0x70);
    const auto origin=address==jobScope.activeList?PreparationOrigin::ActiveList:PreparationOrigin::CopiedList;
    const size_t slot=static_cast<size_t>(origin);
    const uint32_t gen=generation.load();
    const auto ticket=scoped?handoff.Begin(origin,address,gen):PreparationTicket{};
    if (scoped) preparedLists[slot].Publish({});
    Tracking tracking{};
    // Observe a graphics transition before freezing the preparation revision.
    // Publication must not discover it after the new pair has been built.
    const bool current=scoped&&Current();
    if (current) (void)CeObserveRendererMode();
    const bool eligible=current&&Anniversary()&&LiveGame()&&SingleCamera()&&!secondary;
    if (eligible)
    {
        const uint64_t now=GetTickCount64();
        const uint64_t last=lastCameraMs.exchange(now,std::memory_order_acq_rel);
        if (!last||now<last||now-last>=500) firstCameraMs.store(now,std::memory_order_release);
        uint64_t zero=0; firstCameraMs.compare_exchange_strong(zero,now);
    }
    const bool force=eligible&&jobScope.cameraOwned&&armed.load()&&TrackingNow(tracking);
    constexpr bool constructTrackedViews=kCeConstructTrackedViewsEnabled||
        kCeIntegratedBaseVrEnabled||kCeSceneVisibilityBaseVrEnabled;
    const uint64_t revisionBeforeBuild=referenceRevision.load(std::memory_order_acquire);
    BuildScope construction{};
    if (force&&constructTrackedViews)
    {
        if (recenter.exchange(false)||reference.generation!=gen||reference.spaceEpoch!=tracking.spaceEpoch)
            reference={tracking.headPosition,tracking.headOrientation,tracking.spaceEpoch,gen};
        Wanted raster{};
        construction.list=address;
        construction.enabled=allocated.Read(raster)&&raster.generation==gen;
        construction.views.tracking=tracking; construction.views.reference=reference;
        construction.views.unitsPerMeter=Game_GetWorldScale();
        construction.views.positional=Game_IsPositionalTracking();
        ApplyPhysicalCrouchReference(tracking,construction.views.reference,
            construction.views.unitsPerMeter,construction.views.positional);
        construction.views.width=raster.descriptor.Width; construction.views.height=raster.descriptor.Height;
    }
    const auto nativeResult=ConstructViews(original,arg,list,force?1:secondary,settings,
        force&&constructTrackedViews?&construction:nullptr);
    if (!scoped) return nativeResult;
    Prepared result{address,gen,force,false,{}};
    result.referenceRevision=revisionBeforeBuild;
    if (force)
    {
        SaberViewPair source{},rasterSource{},committed{}; StagedViewPair staged{};
        if (!constructTrackedViews&&
            (recenter.exchange(false)||reference.generation!=gen||reference.spaceEpoch!=tracking.spaceEpoch))
            reference={tracking.headPosition,tracking.headOrientation,tracking.spaceEpoch,gen};
        if (!recenter.load(std::memory_order_acquire)&&
            revisionBeforeBuild==referenceRevision.load(std::memory_order_acquire))
            publishedReference.Publish({constructTrackedViews?construction.views.reference:reference,revisionBeforeBuild});
        Wanted raster{};
        const bool rasterReady=allocated.Read(raster)&&raster.generation==gen;
        const auto stage=constructTrackedViews?
            (!construction.enabled?PairStageResult::AwaitingRaster:
                !construction.failed&&Read(address,source)&&construction.views.Finish(source,
                    [](SaberCamera& camera) { return RebuildNativeCamera(bindings,camera); },staged)
                ?PairStageResult::Staged:PairStageResult::InvalidRebuiltCamera):
            !rasterReady?PairStageResult::AwaitingRaster:
            Read(address,source)&&SelectNativeEyeRaster(source,raster.descriptor.Width,
                raster.descriptor.Height,rasterSource)
            ? StageBoundNativePair(bindings,rasterSource,tracking,reference,
                Game_GetWorldScale(),Game_IsPositionalTracking(),staged)
            : PairStageResult::InvalidNativePair;
        lastPairStage.store(static_cast<uint32_t>(stage),std::memory_order_relaxed);
        if (stage==PairStageResult::Staged&&
            Commit(list,staged)&&Read(address,committed)&&
            handoff.Publish(ticket,tracking,staged,committed)&&
            handoff.Read(origin,address,committed,gen,tracking.spaceEpoch,result.receipt)&&
            result.referenceRevision==referenceRevision.load(std::memory_order_acquire))
        {
            result.valid=true; built.fetch_add(1,std::memory_order_relaxed);
            PublishAnniversaryGameplayContext(constructTrackedViews?construction.views.stock[0]:
                source.views[0].camera,tracking,constructTrackedViews?construction.views.reference:reference,revisionBeforeBuild,
                constructTrackedViews?construction.views.unitsPerMeter:Game_GetWorldScale(),
                constructTrackedViews?construction.views.positional:Game_IsPositionalTracking());
        }
        else dropped.fetch_add(1,std::memory_order_relaxed);
    }
    preparedLists[slot].Publish(result);
    return nativeResult;
}
void PrepareBody(uintptr_t job)
{
    const auto original=reinterpret_cast<PrepareFn>(hooks[Prepare].original);
    // Native stock Saber preparation also runs while Original is selected.
    // Its list bookkeeping does not read/write the tracking reference. Holding
    // the camera guard for that whole job made overlapping Original frames
    // fall back to the fixed native lens. Keep the job ledger exclusive, but
    // claim camera ownership only for Anniversary. Builder rechecks this
    // receipt before VR mutation,
    // so a mode change inside a stock job cannot acquire an unguarded reference.
    const bool jobClaimed=!jobPreparationBusy.test_and_set(std::memory_order_acquire);
    const bool needsCamera=Current()&&Anniversary();
    const bool cameraClaimed=jobClaimed&&needsCamera&&
        !preparationBusy.test_and_set(std::memory_order_acquire);
    const bool claimed=jobClaimed&&(!needsCamera||cameraClaimed);
    const auto previous=jobScope;
    uintptr_t renderer{}; int renderJob{},copyPrepared{};
    const bool scoped=claimed&&Read(bindings.base+0x1bea9e0,renderer)&&renderer&&
        Read(job+0xbe58,renderJob)&&Read(job+0xbe5c,copyPrepared);
    jobScope={job,renderer?renderer+0xb0:0,scoped,cameraClaimed};
    if (scoped) activeListAddress.store(renderer+0xb0,std::memory_order_release);
    if (scoped&&renderJob)
    {
        renderReady.Publish({});RevokeCopiedWorkerList();
        // This native copy overwrites the active list without calling its
        // builder. Retire that old source ticket before identical stationary
        // cameras can accidentally grant it the preceding frame's identity.
        if (copyPrepared) handoff.Invalidate(PreparationOrigin::ActiveList);
    }
    __try { original(job); }
    __finally
    {
        if (scoped&&renderJob)
        {
            Prepared result{}; SaberViewPair rendered{};
            const auto origin=copyPrepared?PreparationOrigin::CopiedList:PreparationOrigin::ActiveList;
            if (preparedLists[static_cast<size_t>(origin)].Read(result)&&
                result.sourceList==(copyPrepared?job+0x70:renderer+0xb0)&&
                result.generation==generation.load())
            {
                if (result.valid)
                    result.valid=Read(renderer+0xb0,rendered)&&
                        handoff.Read(origin,result.sourceList,rendered,result.generation,
                            result.receipt.tracking.spaceEpoch,result.receipt);
                renderReady.Publish(result);
            }
        }
        jobScope=previous;
        if (cameraClaimed) preparationBusy.clear(std::memory_order_release);
        if (jobClaimed) jobPreparationBusy.clear(std::memory_order_release);
    }
}
void __fastcall PrepareHook(uintptr_t job) { Callback callback; PrepareBody(job); }
void __fastcall SceneCameraHook(uintptr_t scene,int32_t refresh,
    const float* primary,const float* secondary)
{
    Callback callback;
    // Native 543550 records two-position membership in scene+110 bit1000,
    // but only a native refresh request rebuilds static object +8E eye masks.
    // The ordinary split setup requests that refresh; manufactured views do
    // not pass through that setup. Compare native previous/current membership
    // so entering AND leaving two eyes refresh once, including stock fallback.
    // Both preparation branches reach this callback before geometry culling.
    const uintptr_t first=reinterpret_cast<uintptr_t>(primary);
    const uintptr_t second=reinterpret_cast<uintptr_t>(secondary);
    const auto ownsPositions=[&](uintptr_t list) noexcept {
        return list&&list<=UINTPTR_MAX-0x438&&first==list+0x70&&
            (!second||second==list+0x438);
    };
    uintptr_t vtable{}; uint32_t flags{};
    if (!refresh&&jobScope.owned&&
        (ownsPositions(jobScope.activeList)||
            (jobScope.job<=UINTPTR_MAX-0x70&&ownsPositions(jobScope.job+0x70)))&&
        scene&&scene<=UINTPTR_MAX-0x110&&Read(scene,vtable)&&
        vtable==bindings.base+0x1819658&&Read(scene+0x110,flags)&&
        static_cast<bool>(flags&0x1000u)!=static_cast<bool>(secondary))
    {
        refresh=1;
        sceneVisibilityRefreshes.fetch_add(1,std::memory_order_relaxed);
    }
    reinterpret_cast<SceneCameraFn>(hooks[SceneCamera].original)(scene,refresh,primary,secondary);
}
// Native455A10 completes both depth views before later depth-derived passes
// and shading. Distinct final color copies cannot prove independent depth.
// Read only existing native ownership and creation-time descriptor receipts.
bool ReadDepthReceipt(FrameScope::DepthReceipt& out,bool requireBound) noexcept
{
    using SelectFn=uintptr_t(__fastcall*)(uintptr_t);
    FrameScope::DepthReceipt result{};uintptr_t config{},vtable{};uint32_t flags{};
    if (!bindings.surfaceSelector||!Read(bindings.base+0x2e3bdd8,config)||!config||
        !Read(config+0x318,result.root)||!result.root||
        !Read(result.root,vtable)||vtable!=bindings.base+0x17fb608||
        !Read(result.root+0x88,flags)||!(flags&(1u<<9))) return false;
    __try { result.surface=reinterpret_cast<SelectFn>(bindings.surfaceSelector)(result.root); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!result.surface||!Read(result.surface,vtable)||vtable!=bindings.base+0x17fb608||
        !Read(result.surface+0xe0,result.resource)||!result.resource||
        !Read(result.surface+0x108,result.view)||!result.view||
        !Read(bindings.base+0x2e3bde0,result.backend)||!result.backend||
        !Read(result.backend,vtable)||vtable!=bindings.base+0x17f9d10||
        !Read(result.backend+0xce0,result.context)||!result.context) return false;
    uintptr_t immediate{};
    ResourceRegistry::Record record{};
    if (!Read(bindings.base+0x2ea2d30,immediate)||immediate!=result.context||
        !resources.Read(result.resource,result.resource,record)) return false;
    const auto& d=record.descriptor;
    if (!(d.BindFlags&D3D11_BIND_DEPTH_STENCIL)||!d.Width||!d.Height||
        d.Width>16384||d.Height>16384||d.MipLevels!=1||d.ArraySize!=1||
        d.SampleDesc.Count!=1||d.SampleDesc.Quality||d.Usage!=D3D11_USAGE_DEFAULT||
        d.CPUAccessFlags||d.MiscFlags) return false;
    if (requireBound)
    {
        uintptr_t bound{},described{};uint8_t readOnly{};
        // The depth pass uses the ordinary writable DSV and native descriptor.
        if (!Read(result.backend+0xd20,bound)||bound!=result.view||
            !Read(result.backend+0x18+0x30,described)||described!=result.root||
            !Read(result.backend+0x18+0x44,readOnly)||readOnly) return false;
    }
    result.revision=record.revision;result.descriptor=d;
    out=result;return true;
}
bool SameDepthReceipt(const FrameScope::DepthReceipt& a,const FrameScope::DepthReceipt& b) noexcept
{
    return a.root==b.root&&a.surface==b.surface&&a.resource==b.resource&&a.view==b.view&&
        a.backend==b.backend&&a.context==b.context&&a.revision==b.revision&&
        std::memcmp(&a.descriptor,&b.descriptor,sizeof(a.descriptor))==0;
}
void RejectDepth(FrameScope& scope,uint32_t failure) noexcept
{
    scope.diagnostic.depthFailure=failure;scope.diagnostic.failure=FrameFailure::DepthResource;
    scope.capture=false;
}
void DepthMeshBody(uintptr_t a,uintptr_t b,uintptr_t c,int32_t eye,uintptr_t caller)
{
    if (auto* scope=frameScope;scope&&(caller!=bindings.base+0x456329||eye<0||eye>1||
        scope->primaryDrawEye!=eye||scope->primaryDrawStage!=1))
    { scope->primaryDrawEye=-1;scope->primaryDrawStage=0; }
    reinterpret_cast<DepthMeshFn>(hooks[DepthMesh].original)(a,b,c,eye);
    auto* scope=frameScope;
    if (!scope||!scope->capture||caller!=bindings.base+0x456329) return;
    FrameScope::DepthReceipt receipt{};
    if (!ReadDepthReceipt(receipt,true)) { RejectDepth(*scope,1);return; }
    if (eye<0||eye>1)
    {
        // Auxiliary depth may not overwrite either completed primary depth.
        for (int primary=0;primary<2;++primary)
            if ((scope->diagnostic.depthMask&(1u<<primary))&&
                scope->depth[primary].resource==receipt.resource) RejectDepth(*scope,6);
        return;
    }
    auto& diagnostic=scope->diagnostic;
    diagnostic.depthResource[eye]=receipt.resource;diagnostic.depthView[eye]=receipt.view;
    const auto& camera=scope->prepared.receipt.pair.cameras[eye];
    if (diagnostic.depthMask!=(eye?1u:0u)||!(diagnostic.consumedDepth&(1u<<eye)))
    { RejectDepth(*scope,2);return; }
    if (receipt.descriptor.Width!=camera.viewportWidth||receipt.descriptor.Height!=camera.viewportHeight)
    { RejectDepth(*scope,3);return; }
    if (eye&&(receipt.resource==scope->depth[0].resource||receipt.view==scope->depth[0].view))
    { RejectDepth(*scope,4);return; }
    scope->depth[eye]=receipt;diagnostic.depthMask|=1u<<eye;
    if(dlssDepthRequested.load(std::memory_order_relaxed)) {
        wantedDlssDepth.Publish({receipt.descriptor,generation.load(),receipt.context});
        (void)cache.CaptureDepth(scope->key,eye,reinterpret_cast<ID3D11DeviceContext*>(receipt.context),
            reinterpret_cast<ID3D11Resource*>(receipt.resource),receipt.descriptor,scope->dlssCameras[eye],
            scope->prepared.referenceRevision);
    }
}
__declspec(noinline) void __fastcall DepthMeshHook(uintptr_t a,uintptr_t b,uintptr_t c,int32_t eye)
{
    Callback callback;DepthMeshBody(a,b,c,eye,reinterpret_cast<uintptr_t>(_ReturnAddress()));
}
// The frame-entry list is preparation evidence only. Verify that the native
// depth, scene and shading camera upload calls actually consume those same
// primary records before lending their later GPU output. No native mutation.
dlss::CameraSample ReadSaberDlssCamera(uintptr_t backend,const SaberCamera& camera) noexcept
{
    uintptr_t buffer{},data{};
    float origin[4]{},projection[16]{};
    if(!dlssCameraBindingsVerified.load(std::memory_order_acquire)||
        !Read(backend+0xd8,buffer)||!buffer||!Read(buffer+8,data)||!data||
        !Read(data+0x240,origin)||!Read(data+0x270,projection)) return {};
    return halo_ce::DlssSaberCamera(camera,origin,projection);
}
void CameraUploadBody(uintptr_t arg,uintptr_t backend,const SaberCamera* camera,uintptr_t caller)
{
    auto* scope=frameScope;
    // No material may borrow the previous camera while native upload changes
    // its matrices. Unknown/auxiliary uploads remain unowned after returning.
    if (scope) { scope->primaryDrawEye=-1;scope->primaryDrawStage=0; }
    const unsigned stage=caller==bindings.base+0x4562bf?1u:
        caller==bindings.base+0x456a86?2u:caller==bindings.base+0x457c07?3u:0u;
    const auto address=reinterpret_cast<uintptr_t>(camera);
    int eye=-1;
    SaberCamera before{};
    uint32_t failure=0;
    if (stage&&scope&&scope->capture)
    {
        const uintptr_t first=scope->renderer+0xf0;
        if (address>=first&&(address-first)%sizeof(SaberView)==0&&
            (address-first)/sizeof(SaberView)<scope->diagnostic.nativeCount)
        {
            const auto index=(address-first)/sizeof(SaberView);
            if (index<2) eye=static_cast<int>(index);
            // Auxiliary views occupy the same native list. They cannot grant
            // primary-eye identity, but their normal uploads are not failures.
        }
        else failure=1; // supplied address is outside the proven native list
        if (eye>=0)
        {
            const auto& expected=scope->prepared.receipt.pair.cameras[eye];
            int32_t player{},expectedPlayer{},primaryPlayer{};
            std::memcpy(&expectedPlayer,reinterpret_cast<const uint8_t*>(&expected)+0x220,4);
            std::memcpy(&primaryPlayer,reinterpret_cast<const uint8_t*>(&scope->prepared.receipt.pair.cameras[0])+0x220,4);
            if (!Read(address,before)||!SamePreparedCamera(before,expected)) failure=2;
            else
            {
                std::memcpy(&player,reinterpret_cast<const uint8_t*>(&before)+0x220,4);
                if (player!=expectedPlayer||player!=primaryPlayer) failure=3;
                scope->diagnostic.consumedPlayer[eye]=player;
            }
        }
    }
    reinterpret_cast<CameraUploadFn>(hooks[CameraUpload].original)(arg,backend,camera);
    if (stage&&scope&&scope->capture)
    {
        if (eye>=0&&!failure)
        {
            SaberCamera after{}; uintptr_t selected{};
            if (!Read(address,after)||!SamePreparedCamera(before,after)||
                std::memcmp(reinterpret_cast<const uint8_t*>(&before)+0x220,
                    reinterpret_cast<const uint8_t*>(&after)+0x220,4)) failure=4;
            else if (!Read(scope->renderer+0xbe98,selected)||selected!=address) failure=6;
            else
            {
                scope->diagnostic.consumedCamera[eye]=address;
                scope->primaryDrawEye=eye;scope->primaryDrawStage=stage;
                if (stage==1) {
                    scope->diagnostic.consumedDepth|=1u<<eye;
                    if(dlssDepthRequested.load(std::memory_order_relaxed))
                        scope->dlssCameras[eye]=ReadSaberDlssCamera(backend,after);
                }
                if (stage==2)
                { scope->diagnostic.consumedScene|=1u<<eye; scope->lastSceneEye=eye; }
                if (stage==3) scope->diagnostic.consumedShading|=1u<<eye;
                if (stage==2||stage==3)
                {
                    FrameScope::DepthReceipt depth{};
                    if (scope->diagnostic.depthMask!=3||!ReadDepthReceipt(depth,false)||
                        !SameDepthReceipt(scope->depth[eye],depth)) RejectDepth(*scope,5);
                }
            }
        }
        if (failure)
        {
            scope->diagnostic.consumerFailure=failure;
            scope->diagnostic.failure=FrameFailure::RenderConsumer;
            scope->capture=false;
        }
    }
}
void __fastcall CameraUploadHook(uintptr_t arg,uintptr_t backend,const SaberCamera* camera)
{
    Callback callback;
    CameraUploadBody(arg,backend,camera,reinterpret_cast<uintptr_t>(_ReturnAddress()));
}
void FrameBody(uintptr_t arg,uint32_t flags)
{
    const auto original=reinterpret_cast<FrameFn>(hooks[Frame].original);
    FrameScope scope{}; uintptr_t renderer{}; SaberViewPair rendered{};
    // Preserve the actual native HUD-enable bit for per-eye output replay.
    // be2140f left this zero and rejected every otherwise eligible HUD pass.
    scope.renderFlags=flags;
    // The native Anniversary worker can still run in Original mode. Its
    // stock work owns no Classic eye receipt, even when old synthetic lists
    // still need their packed-copy bounds protection during a switch.
    const bool anniversaryFrame=Current()&&Anniversary();
    // Keep the last coherent pair while its successor renders or is rejected.
    // AcquirePair still rejects stale tracking, recenter and renderer changes.
    // Preparation can signal native completion before its wrapper returns.
    // The builder's already-published marker protects the packed GPU copy in
    // that window. Only the fully frozen receipt below permits VR submission.
    if (!frameScope&&HasSyntheticLists()) scope.synthetic=true;
    scope.diagnostic.failure=FrameFailure::NoReceipt;
    if (!frameScope&&renderReady.Read(scope.prepared)&&scope.prepared.synthetic&&
        scope.prepared.generation==generation.load()&&
        Read(bindings.base+0x1bea9e0,renderer)&&Read(renderer+0xb0,rendered)&&
        rendered.flags==1&&rendered.count>=2)
    {
        scope.renderer=renderer;
        scope.synthetic=true;
        if (anniversaryFrame) lastOwnedMs.store(GetTickCount64(),std::memory_order_release);
        auto& diagnostic=scope.diagnostic;
        diagnostic.nativeCount=rendered.count; diagnostic.nativeFlags=rendered.flags;
        for (int eye=0;eye<2;++eye)
        {
            for (int axis=0;axis<3;++axis)
                diagnostic.position[eye][axis]=rendered.views[eye].camera.pose.matrix[12+axis];
            diagnostic.nearPlane[eye]=rendered.views[eye].camera.nearPlane;
            diagnostic.farPlane[eye]=rendered.views[eye].camera.farPlane;
            // Append stores its origin index at view+0x1e and its separate
            // 12-byte position table at list+0xbd24, bounded by list+0xbd20.
            uint32_t originCount{};
            const uint32_t originIndex=rendered.views[eye].native10[0x0e];
            if (Read(renderer+0xb0+0xbd20,originCount)&&originCount<=16&&originIndex<originCount&&
                Read(renderer+0xb0+0xbd24+12*originIndex,diagnostic.origin[eye]))
                diagnostic.originMask|=1u<<eye;
        }
        diagnostic.cameraWidth=scope.prepared.receipt.pair.cameras[0].viewportWidth;
        diagnostic.cameraHeight=scope.prepared.receipt.pair.cameras[0].viewportHeight;
        const bool receiptCurrent=anniversaryFrame&&Current()&&armed.load()&&trackingEnabled.load()&&scope.prepared.valid&&
            scope.prepared.referenceRevision==referenceRevision.load(std::memory_order_acquire);
        const bool sameCameras=MatchesPreparedViews(rendered,scope.prepared.receipt);
        if (scope.prepared.valid&&!sameCameras)
        {
            for (uint32_t eye=0;eye<2&&diagnostic.cameraDifference==0xffffffffu;++eye)
                for (uint32_t offset=0;offset<offsetof(SaberCamera,derived160);++offset)
                    if (reinterpret_cast<const uint8_t*>(&rendered.views[eye].camera)[offset]!=
                        reinterpret_cast<const uint8_t*>(&scope.prepared.receipt.pair.cameras[eye])[offset])
                    { diagnostic.cameraDifference=eye*sizeof(SaberCamera)+offset; break; }
        }
        scope.capture=receiptCurrent&&sameCameras&&cache.Begin(scope.prepared.receipt,scope.key);
        diagnostic.failure=!receiptCurrent?FrameFailure::InvalidReceipt:!sameCameras?FrameFailure::CameraChanged:
            !scope.capture?FrameFailure::CacheBegin:FrameFailure::None;
    }
    const auto previous=frameScope;
    const bool previousMaterialMask=anniversaryMaterialMasked;
    if (previous) anniversaryMaterialMasked=true;
    if (!previous) frameScope=&scope;
    bool returned=false;
    __try { original(arg,flags); returned=true; }
    __finally
    {
        if (!previous)
        {
            if (returned&&scope.capture&&Current()&&Anniversary()&&
                scope.prepared.referenceRevision==referenceRevision.load(std::memory_order_acquire)&&cache.Finish(scope.key))
            { completedFrame.Publish({scope.key,scope.prepared.referenceRevision,GetTickCount64()}); captured.fetch_add(1,std::memory_order_relaxed); }
            else if (scope.synthetic)
            {
                if (scope.diagnostic.failure==FrameFailure::None) scope.diagnostic.failure=FrameFailure::IncompletePair;
                cache.Drop(scope.key); dropped.fetch_add(1,std::memory_order_relaxed);
            }
            else stock.fetch_add(1,std::memory_order_relaxed);
            if (scope.synthetic) frameDiagnostic.Publish(scope.diagnostic);
            frameScope=previous;
        }
        anniversaryMaterialMasked=previousMaterialMask;
    }
}
void __fastcall FrameHook(uintptr_t arg,uint32_t flags) { Callback callback; FrameBody(arg,flags); }
uint32_t AnniversaryEyeTracking(const SaberCamera* camera,Tracking& tracking) noexcept;
#include "haloce_anniversary_hud.inl"
void OutputBody(int eye)
{
    auto* scope=frameScope; const int previous=scope?scope->eye:-1;
    if (scope) { scope->primaryDrawEye=-1;scope->primaryDrawStage=0; }
    if (scope&&scope->synthetic&&eye>=0&&eye<2) scope->eye=eye;
    __try
    {
        if (scope) AnniversaryHud_ReplayEye(*scope,eye);
        reinterpret_cast<OutputFn>(hooks[Output].original)(eye);
    }
    __finally { if (scope) scope->eye=previous; }
}
void __fastcall OutputHook(int eye) { Callback callback; OutputBody(eye); }
uintptr_t TransferBody(uintptr_t backend,SurfaceTransfer* request,uintptr_t caller)
{
    auto* scope=frameScope; SurfaceTransfer copy{};
    const bool scoped=scope&&scope->synthetic&&scope->eye>=0&&caller==bindings.base+0x45e376&&
        Read(reinterpret_cast<uintptr_t>(request),copy);
    if (scoped) { scope->transfer=&copy; scope->selectedSource=scope->selectedDestination=0; }
    uintptr_t result{};
    __try { result=reinterpret_cast<TransferFn>(hooks[Transfer].original)(backend,request); }
    __finally { if (scoped) scope->transfer=nullptr; }
    return result;
}
uintptr_t __fastcall TransferHook(uintptr_t backend,SurfaceTransfer* request)
{ Callback callback; return TransferBody(backend,request,reinterpret_cast<uintptr_t>(_ReturnAddress())); }
void CopyBody(ID3D11DeviceContext* context,ID3D11Resource* destination,
    UINT destinationSub,UINT x,UINT y,UINT z,ID3D11Resource* source,UINT sourceSub,const D3D11_BOX* box,
    uintptr_t caller)
{
    const auto original=reinterpret_cast<CopyFn>(hooks[Copy].original);
    auto* scope=frameScope;
    // Actual native source, after variant resolution, inside CE's copy lock.
    if (scope&&scope->transfer&&caller==bindings.base+0x204da0)
    {
        const auto& transfer=*scope->transfer;
        ResourceRegistry::Record src{},dst{};
        const auto sourceId=reinterpret_cast<uintptr_t>(source),destinationId=reinterpret_cast<uintptr_t>(destination);
        const bool sourceKnown=resources.Read(sourceId,sourceId,src);
        const bool destinationKnown=resources.Read(destinationId,destinationId,dst);
        const int copyEye=scope->eye;
        if (copyEye>=0&&copyEye<2)
        {
            // Record rejected copies too: previously copy-shape left a null
            // right source, hiding descriptor misses behind a generic failure.
            auto& diagnostic=scope->diagnostic;
            diagnostic.sourceWrapper[copyEye]=static_cast<uintptr_t>(transfer.sourceSurface);
            diagnostic.sourceResource[copyEye]=sourceId;
            diagnostic.copyContext[copyEye]=reinterpret_cast<uintptr_t>(context);
            diagnostic.copyProof[copyEye]=(sourceKnown?1u:0u)|(destinationKnown?2u:0u)|
                (box?4u:0u)|(sourceSub==0&&destinationSub==0?8u:0u)|
                (x==0&&z==0?16u:0u);
            diagnostic.copySourceSize[copyEye][0]=src.descriptor.Width;
            diagnostic.copySourceSize[copyEye][1]=src.descriptor.Height;
            diagnostic.copyRequestSize[copyEye][0]=transfer.width;
            diagnostic.copyRequestSize[copyEye][1]=transfer.height;
        }
        const bool shape=sourceKnown&&box&&sourceSub==0&&destinationSub==0&&x==0&&z==0&&
            IsPrimaryEyeTransfer(transfer,scope->eye,src.descriptor.Width,src.descriptor.Height)&&
            box->left==0&&box->top==0&&box->front==0&&box->back==1&&
            box->right==src.descriptor.Width&&box->bottom==src.descriptor.Height&&
            y==static_cast<UINT>(transfer.destinationY);
        if (shape)
        {
            const int eye=scope->eye;
            // Both actual native copies must share the same live packed
            // destination before the later ordinary HUD may augment them.
            if (destinationKnown&&dst.descriptor.Width==src.descriptor.Width&&
                dst.descriptor.Height==2*src.descriptor.Height&&dst.descriptor.MipLevels==1&&
                dst.descriptor.ArraySize==1&&dst.descriptor.SampleDesc.Count==1)
            {
                if (eye==0)
                {
                    scope->packedResource=destinationId;scope->packedRevision=dst.revision;
                    scope->packedDescriptor=dst.descriptor;scope->packedEyeMask=1;
                }
                else if (scope->packedEyeMask==1&&scope->packedResource==destinationId&&
                    scope->packedRevision==dst.revision&&
                    !std::memcmp(&scope->packedDescriptor,&dst.descriptor,sizeof(dst.descriptor)))
                    scope->packedEyeMask=3;
                else scope->packedEyeMask=0;
            }
            else scope->packedEyeMask=0;
            scope->diagnostic.sourceWrapper[eye]=static_cast<uintptr_t>(transfer.sourceSurface);
            scope->diagnostic.sourceResource[eye]=sourceId;
            scope->diagnostic.copyContext[eye]=reinterpret_cast<uintptr_t>(context);
            uintptr_t selector{};
            if (Read(bindings.base+0x1bea6b8,selector)&&selector)
                Read(selector+0x1d0,scope->diagnostic.sourceSelectorFlags[eye]);
            scope->diagnostic.sourceWidth=src.descriptor.Width;
            scope->diagnostic.sourceHeight=src.descriptor.Height;
            wanted.Publish({src.descriptor,generation.load(),reinterpret_cast<uintptr_t>(context)});
            if (scope->capture)
            {
                const uint32_t mask=1u<<eye;
                const auto& seen=scope->diagnostic;
                if ((seen.consumedDepth&mask)==0||(seen.consumedScene&mask)==0||
                    (seen.consumedShading&mask)==0||scope->lastSceneEye!=eye)
                {
                    scope->capture=false;
                    scope->diagnostic.consumerFailure=5; // missing/out-of-order camera consumption
                    scope->diagnostic.failure=FrameFailure::RenderConsumer;
                }
            }
            if (scope->capture)
            {
                const auto& camera=scope->prepared.receipt.pair.cameras[scope->eye];
                if (camera.viewportWidth!=src.descriptor.Width||camera.viewportHeight!=src.descriptor.Height)
                { scope->capture=false; scope->diagnostic.failure=FrameFailure::RasterChanged; }
            }
            if (scope->capture)
            {
                if (!cache.Capture(scope->key,scope->eye,context,source,src.descriptor))
                { scope->capture=false; scope->diagnostic.failure=FrameFailure::EyeCopy; }
                else scope->diagnostic.eyeMask|=1u<<scope->eye;
            }
        }
        else { scope->capture=false; scope->diagnostic.failure=FrameFailure::CopyShape; descriptorMiss.fetch_add(1,std::memory_order_relaxed); }
        // A forced second full-size view can exceed the stock packed target.
        // Preserve native bookkeeping and a bounded desktop preview. Unknown
        // descriptors never authorize this potentially out-of-bounds GPU call.
        if (!shape||!destinationKnown||dst.descriptor.Width<src.descriptor.Width||
            dst.descriptor.Height<src.descriptor.Height||dst.descriptor.ArraySize!=1||
            dst.descriptor.MipLevels!=1||dst.descriptor.SampleDesc.Count!=1||
            src.descriptor.SampleDesc.Count!=1||src.descriptor.SampleDesc.Quality!=0||
            src.descriptor.MipLevels!=1||src.descriptor.ArraySize!=1||
            dst.descriptor.SampleDesc.Quality!=0||dst.descriptor.Format!=src.descriptor.Format)
        { scope->capture=false; if (shape) scope->diagnostic.failure=FrameFailure::Destination; return; }
        if (y>dst.descriptor.Height-src.descriptor.Height)
        { y=0; previewFolded.fetch_add(1,std::memory_order_relaxed); }
    }
    original(context,destination,destinationSub,x,y,z,source,sourceSub,box);
}
void STDMETHODCALLTYPE CopyHook(ID3D11DeviceContext* context,ID3D11Resource* destination,
    UINT destinationSub,UINT x,UINT y,UINT z,ID3D11Resource* source,UINT sourceSub,const D3D11_BOX* box)
{
    Callback callback;
    CopyBody(context,destination,destinationSub,x,y,z,source,sourceSub,box,
        reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

// Resource construction/import are cold native management scopes. Render
// hooks use these immutable CPU descriptors and issue only the GPU copy.
void RevokeWrappedResource(uintptr_t wrapper) noexcept
{
    uintptr_t resource{};
    if (Read(wrapper+0xe0,resource)&&resource) resources.Forget(resource);
}
void RecordResource(uintptr_t wrapper) noexcept
{
    ID3D11Resource* resource{};
    if (!Read(wrapper+0xe0,resource)||!resource) return;
    ID3D11Texture2D* texture{};
    __try
    {
        if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)))&&texture)
        {
            D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
            HaloCE_RecordTextureCreated(texture,desc);
            texture->Release();
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { }
}
uintptr_t __fastcall CreateHook(uintptr_t wrapper,uintptr_t data)
{
    Callback callback; RevokeWrappedResource(wrapper);
    const auto result=reinterpret_cast<CreateFn>(hooks[Create].original)(wrapper,data);
    RecordResource(wrapper); return result;
}
uintptr_t __fastcall ImportHook(uintptr_t wrapper,uintptr_t resource)
{
    Callback callback; RevokeWrappedResource(wrapper);
    const auto result=reinterpret_cast<CreateFn>(hooks[Import].original)(wrapper,resource);
    RecordResource(wrapper); return result;
}
void __fastcall ReleaseHook(uintptr_t wrapper)
{
    Callback callback; RevokeWrappedResource(wrapper);
    reinterpret_cast<ReleaseFn>(hooks[Release].original)(wrapper);
}
uintptr_t __fastcall ResetListHook(uintptr_t list)
{
    Callback callback;
    // Native reset owns the list and destroys its native resources. This is
    // also the retirement barrier for a manufactured secondary view.
    CopiedWorkerList copied{};
    if (!copiedWorkerList.Read(copied)||copied.destination==list||copied.source.sourceList==list)
        RevokeCopiedWorkerList();
    for (size_t index=0;index<2;++index)
    {
        Prepared previous{};
        if (preparedLists[index].Read(previous)&&previous.sourceList==list)
        {
            handoff.Invalidate(static_cast<PreparationOrigin>(index));
            preparedLists[index].Publish({});
        }
    }
    if (activeListAddress.load(std::memory_order_acquire)==list) renderReady.Publish({});
    return reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(hooks[ResetList].original)(list);
}
#include "haloce_resolution.inl"

bool Remove() noexcept
{
    retiring.store(true,std::memory_order_release);
    RevokeCopiedWorkerList();
    gameplayBridgeVerified.store(false,std::memory_order_release);
    hudTargetBindingsVerified.store(false,std::memory_order_release);
    dlssCameraBindingsVerified.store(false,std::memory_order_release);
    wantedDlssDepth.Publish({});
    if (!ce_resolution::Remove()||!AnniversaryHud_Remove()||!Classic_Remove()) return false;
    // Keep copy protection installed until native reset/stock preparation has
    // retired every manufactured list. An inactive title may retain these
    // dormant hooks until its next reset; never remove them under a queued eye.
    if (installed.load()&&(callbacks.load()||HasSyntheticLists())) return false;
    for (auto& hook:hooks) if (hook.enabled)
    {
        const auto status=MCCVR_DisableHookForRetirement(hook.target);
        if (status!=MH_OK&&status!=MH_ERROR_DISABLED) return false;
        hook.enabled=false;
    }
    if (callbacks.load(std::memory_order_acquire)) return false;
    const void* functions[Count]={reinterpret_cast<void*>(&PrepareHook),reinterpret_cast<void*>(&BuilderHook),
        reinterpret_cast<void*>(&FrameHook),reinterpret_cast<void*>(&OutputHook),reinterpret_cast<void*>(&TransferHook),
        reinterpret_cast<void*>(&CreateHook),reinterpret_cast<void*>(&ImportHook),reinterpret_cast<void*>(&ReleaseHook),
        reinterpret_cast<void*>(&ResetListHook),reinterpret_cast<void*>(&CopyHook),reinterpret_cast<void*>(&AppendHook),
        reinterpret_cast<void*>(&CameraUploadHook),reinterpret_cast<void*>(&DepthMeshHook),
        reinterpret_cast<void*>(&SceneCameraHook)};
    const void* originals[Count]{};
    for (size_t i=0;i<Count;++i) originals[i]=hooks[i].original;
    // The shared verifier accepts at most eight ranges. Entries are disabled,
    // so independently checking the remaining roots cannot admit new callers.
    if (!WaitForNativeDetourQuiescence(functions,originals,8,callbacks)||
        !WaitForNativeDetourQuiescence(functions+8,originals+8,Count-8,callbacks)) return false;
    for (auto& hook:hooks) if (hook.target)
    {
        const auto status=MH_RemoveHook(hook.target);
        if (status!=MH_OK&&status!=MH_ERROR_NOT_CREATED) return false;
        hook={};
    }
    if (!cache.Reset()) return false;
    resources.InvalidateAll();
    if (moduleReference) { FreeLibrary(moduleReference); moduleReference=nullptr; }
    bindings={}; installed=false; generation=0; allocated.Publish({});
    frameDiagnostic.Publish({});
    completedFrame.Publish({}); renderReady.Publish({}); wanted.Publish({});
    preparedLists[0].Publish({}); preparedLists[1].Publish({});
    handoff.Invalidate(PreparationOrigin::ActiveList); handoff.Invalidate(PreparationOrigin::CopiedList);
    firstCameraMs=0; lastCameraMs=0; lastOwnedMs=0; activeListAddress=0; recenter=true;
    return true;
}
bool Install(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    const char* failure="module retention";
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(base),&moduleReference)) return false;
    if (!ResolveNativeBindings(base,size,gen,bindings,failure))
    { LOG("CE core stock fallback: %s",failure); Remove(); return false; }
    const NativeContractSet gameplayContracts{contract::gameplay_bridge::entries,
        contract::gameplay_bridge::witnesses,contract::gameplay_bridge::relatives,
        contract::gameplay_bridge::pointers};
    gameplayBridgeVerified=VerifyNativeFeatureBindings(base,size,gen,gameplayContracts,failure);
    if (!gameplayBridgeVerified.load())
        LOG("CE Anniversary control-camera stock fallback: %s; camera core retained",failure?failure:"bridge verification");
    // The HUD target contract includes native texture release, which this
    // camera transaction detours below. Verify its untouched body now and
    // lend only a module/generation-bound result to the optional HUD feature.
    const NativeContractSet hudTargetContracts{contract::hud_target::entries,
        contract::hud_target::witnesses,contract::hud_target::relatives,
        contract::hud_target::pointers};
    hudTargetBindingsVerified=VerifyNativeFeatureBindings(base,size,gen,hudTargetContracts,failure);
    if (!hudTargetBindingsVerified.load())
        LOG("CE HUD target stock fallback: %s; camera core retained",failure?failure:"target verification");
    const NativeContractSet dlssContracts{dlss_contract::entries,dlss_contract::witnesses,
        dlss_contract::relatives,dlss_contract::pointers};
    dlssCameraBindingsVerified=VerifyNativeFeatureBindings(base,size,gen,dlssContracts,failure);
    if(!dlssCameraBindingsVerified.load())
        LOG("CE DLSS Anniversary constant reader unavailable: %s; ordinary eye rendering retained",
            failure?failure:"constant verification");
    generation=gen; retiring=false;
    preparedLists[0].Publish({}); preparedLists[1].Publish({}); renderReady.Publish({});
    const uintptr_t addresses[Count]={bindings.prepare,bindings.pairBuilder,bindings.frame,
        bindings.output,bindings.transfer,
        base+contract::anniversary_texture_create,base+contract::anniversary_texture_import_2d,
        base+contract::anniversary_texture_release_resources,base+contract::anniversary_view_list_reset,copyTarget.load(),
        base+contract::anniversary_view_append,base+contract::anniversary_camera_upload,
        base+contract::anniversary_depth_mesh_pass,base+contract::anniversary_scene_camera_update};
    void* detours[Count]={reinterpret_cast<void*>(&PrepareHook),reinterpret_cast<void*>(&BuilderHook),
        reinterpret_cast<void*>(&FrameHook),reinterpret_cast<void*>(&OutputHook),reinterpret_cast<void*>(&TransferHook),
        reinterpret_cast<void*>(&CreateHook),reinterpret_cast<void*>(&ImportHook),
        reinterpret_cast<void*>(&ReleaseHook),reinterpret_cast<void*>(&ResetListHook),reinterpret_cast<void*>(&CopyHook),
        reinterpret_cast<void*>(&AppendHook),reinterpret_cast<void*>(&CameraUploadHook),reinterpret_cast<void*>(&DepthMeshHook),
        reinterpret_cast<void*>(&SceneCameraHook)};
    for (size_t i=0;i<Count;++i)
    {
        if (!addresses[i]) { Remove(); return false; }
        auto& hook=hooks[i]; void* target=reinterpret_cast<void*>(addresses[i]);
        const auto status=MH_CreateHook(target,detours[i],&hook.original);
        if (status!=MH_OK)
        { LOG("CE core stock fallback: create hook %zu status %d",i,status); Remove(); return false; }
        hook.target=target;
    }
    for (auto& hook:hooks)
    {
        const auto status=MH_EnableHook(hook.target);
        if (status!=MH_OK) { LOG("CE core stock fallback: enable status %d",status); Remove(); return false; }
        hook.enabled=true;
    }
    installed=true;
    if (!ce_resolution::Install()) LOG("CE full resolution stock fallback: optional allocation hooks unavailable; camera retained");
    (void)Classic_Install();
    if (!AnniversaryHud_InstallNatural()) LOG("CE Anniversary HUD stock fallback: optional installation failed; camera retained");
    LOG("CE refinement candidate installed: both native renderers, graphics-toggle camera continuity and independent weapon/HUD features; waiting for fresh camera; new headset result pending");
    return true;
}
}

bool HaloCE_HudTargetBindingsVerified(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    return Current()&&hudTargetBindingsVerified.load(std::memory_order_acquire)&&
        bindings.base==base&&bindings.size==size&&bindings.generation==gen&&
        generation.load(std::memory_order_acquire)==gen;
}
bool HaloCE_NativeHudResourcesReady(uintptr_t expectedBase,uint32_t expectedGeneration) noexcept
{ return ce_resolution::NativeHudResourcesReady(expectedBase,expectedGeneration); }

void HaloCE_RequestRecovery(uint32_t gen) noexcept
{
    if (gen && gen == TitleAdapter_GetGeneration(GameTitle::HaloCE) &&
        TitleAdapter_GetActiveTitle() == GameTitle::HaloCE && !installed.load())
        rejectedGeneration = 0;
}

bool HaloCE_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive,bool allowInitialInstall) noexcept
{
    active.store(isActive,std::memory_order_release);
    if (moduleReference&&(!isActive||gen!=generation.load()||base!=bindings.base||retiring.load()))
    {
        if (armed.exchange(false)) LOG("CE core disarmed by HaloCE_Poll: title/generation retirement");
        TitleAdapter_PublishLifecycle(GameTitle::HaloCE,generation.load(),{installed.load(),false,true,0});
        if (!Remove()) return false;
    }
    if (!isActive) rejectedGeneration=0;
    if (!isActive||!base||!gen) return false;
    if (!kRejectedCeInitialStereoEnabled&&!kCeSourceRasterStereoEnabled&&!kCeConstructTrackedViewsEnabled&&
        !kCeIntegratedBaseVrEnabled&&!kCeSceneVisibilityBaseVrEnabled)
    {
        if (gen!=rejectedGeneration)
        {
            LOG("CE core disabled by HaloCE_Poll: be2140f failed Classic capture and Anniversary world rendering; input and graphics gesture retained");
            rejectedGeneration=gen;
        }
        TitleAdapter_PublishLifecycle(GameTitle::HaloCE,gen,{false,false,false,0});
        return false;
    }
    if (!installed.load()&&allowInitialInstall&&gen!=rejectedGeneration&&copyTarget.load())
        if (!Install(base,size,gen)) rejectedGeneration=gen;
    if (installed.load()) (void)CeObserveRendererMode();
    const uint64_t now=GetTickCount64(),first=firstCameraMs.load(),last=lastCameraMs.load();
    const bool fresh=last&&now>=last&&now-last<500;
    if (installed.load()&&fresh&&first&&now-first>=1000&&!armed.exchange(true))
        LOG("CE camera heartbeat ready; stereo outputs and optional hands/aim/HUD/contact report their own validation; physical body following deferred");
    if (!fresh&&armed.exchange(false))
    { recenter=true; LOG("CE core disarmed by HaloCE_Poll: camera heartbeat expired; hooks retained for re-entry"); firstCameraMs=0; }
    constexpr uint32_t capabilities=TitleCapability_Stereo|TitleCapability_RoomScale|
        TitleCapability_ControllerInput|TitleCapability_RuntimeModes|TitleCapability_Haptics;
    TitleAdapter_PublishLifecycle(GameTitle::HaloCE,gen,{installed.load(),armed.load(),retiring.load(),armed.load()?capabilities:0});
    if (fresh) TitleAdapter_PublishHeartbeat(GameTitle::HaloCE,gen,last);
    if (now-lastReport>=2000)
    {
        lastReport=now;
        if (!installed.load())
            LOG("CE cold install waiting: levelClockReady=%d rejectedGeneration=%u; no camera hooks or module pin before live simulation",
                allowInitialInstall?1:0,rejectedGeneration);
        LOG("CE RESOLUTION installed=%d requestedEye=%ux%u packedHeight=%u nativeChildren=%llu packedAllocations=%llu managedRebuilds=%llu failures=%llu reason=%u",
            ce_resolution::enabled.load(),ce_resolution::requestedWidth.load(),ce_resolution::requestedHeight.load(),
            2*ce_resolution::requestedHeight.load(),ce_resolution::children.load(),ce_resolution::packedAllocations.load(),
            ce_resolution::rebuilds.load(),ce_resolution::failures.load(),ce_resolution::lastFailure.load());
        LOG("CE DIAG gen=%u installed=%d armed=%d built=%llu pairs=%llu dropped=%llu stock=%llu descriptorMiss=%llu previewFolded=%llu stage=%u sceneRefresh=%llu",
            gen,installed.load(),armed.load(),built.load(),captured.load(),dropped.load(),stock.load(),descriptorMiss.load(),previewFolded.load(),lastPairStage.load(),sceneVisibilityRefreshes.load());
        LOG("CE CLASSIC gen=%u installed=%d pairs=%llu drops=%llu stock=%llu outputs=%llu sourceMiss=%llu failure=%u sourceFailure=%u",
            gen,classicInstalled.load(),classicPairs.load(),classicDrops.load(),classicStock.load(),
            classicOutputs.load(),classicSourceMiss.load(),static_cast<unsigned>(classicLastFailure.load()),
            static_cast<unsigned>(classicSourceFailure.load()));
        LOG("CE CLASSIC PREP stage=%u cache=%d raster=%ux%u output=%ux%u",
            classicPairStage.load(),classicCacheBegan.load(),classicRasterWidth.load(),
            classicRasterHeight.load(),classicOutputWidth.load(),classicOutputHeight.load());
        const uint32_t hudFailure=anniversaryHudFailure.load();
        LOG("CE Anniversary HUD gen=%u installed=%d draws=%llu fallback=%llu failure=%u reason=%s targetsPrepared=%llu incompatibleDepth=%llu",
            gen,anniversaryHudNaturalInstalled.load(),anniversaryHudDraws.load(),
            anniversaryHudFallbacks.load(),hudFailure,AnniversaryHudFailureName(hudFailure),
            anniversaryHudTargetPreparations.load(),anniversaryHudDepthDetachments.load());
        FrameDiagnostic diagnostic{};
        if (frameDiagnostic.Read(diagnostic))
        {
            constexpr const char* reasons[]={"none","no-receipt","invalid-receipt","camera-changed","cache-begin",
                "copy-shape","raster-changed","eye-copy","destination","incomplete-pair","render-consumer","depth-resource"};
            const auto reason=static_cast<uint32_t>(diagnostic.failure);
            LOG("CE FRAME failure=%s(%u) nativeViews=%u flags=0x%X cameraDiff=0x%X eyeMask=%u raster=%.0fx%.0f source=%ux%u eye0=(%.3f,%.3f,%.3f) eye1=(%.3f,%.3f,%.3f)",
                reason<std::size(reasons)?reasons[reason]:"unknown",reason,diagnostic.nativeCount,diagnostic.nativeFlags,
                diagnostic.cameraDifference,diagnostic.eyeMask,diagnostic.cameraWidth,diagnostic.cameraHeight,
                diagnostic.sourceWidth,diagnostic.sourceHeight,
                diagnostic.position[0][0],diagnostic.position[0][1],diagnostic.position[0][2],
                diagnostic.position[1][0],diagnostic.position[1][1],diagnostic.position[1][2]);
            LOG("CE ORIGINS mask=%u origin0=(%.3f,%.3f,%.3f) origin1=(%.3f,%.3f,%.3f) clip0=%.4f/%.1f clip1=%.4f/%.1f construction=before-append",
                diagnostic.originMask,diagnostic.origin[0][0],diagnostic.origin[0][1],diagnostic.origin[0][2],
                diagnostic.origin[1][0],diagnostic.origin[1][1],diagnostic.origin[1][2],
                diagnostic.nearPlane[0],diagnostic.farPlane[0],diagnostic.nearPlane[1],diagnostic.farPlane[1]);
            LOG("CE DEPTH mask=%u reject=%u resources=%p/%p views=%p/%p",
                diagnostic.depthMask,diagnostic.depthFailure,
                reinterpret_cast<void*>(diagnostic.depthResource[0]),reinterpret_cast<void*>(diagnostic.depthResource[1]),
                reinterpret_cast<void*>(diagnostic.depthView[0]),reinterpret_cast<void*>(diagnostic.depthView[1]));
            LOG("CE COPY proof=0x%X/0x%X source=%ux%u/%ux%u request=%ux%u/%ux%u",
                diagnostic.copyProof[0],diagnostic.copyProof[1],
                diagnostic.copySourceSize[0][0],diagnostic.copySourceSize[0][1],
                diagnostic.copySourceSize[1][0],diagnostic.copySourceSize[1][1],
                diagnostic.copyRequestSize[0][0],diagnostic.copyRequestSize[0][1],
                diagnostic.copyRequestSize[1][0],diagnostic.copyRequestSize[1][1]);
            LOG("CE CONSUMERS depth=%u scene=%u shading=%u reject=%u players=%d/%d cameras=%p/%p sourceWrappers=%p/%p sourceResources=%p/%p contexts=%p/%p selectors=0x%X/0x%X",
                diagnostic.consumedDepth,diagnostic.consumedScene,diagnostic.consumedShading,diagnostic.consumerFailure,
                diagnostic.consumedPlayer[0],diagnostic.consumedPlayer[1],
                reinterpret_cast<void*>(diagnostic.consumedCamera[0]),reinterpret_cast<void*>(diagnostic.consumedCamera[1]),
                reinterpret_cast<void*>(diagnostic.sourceWrapper[0]),reinterpret_cast<void*>(diagnostic.sourceWrapper[1]),
                reinterpret_cast<void*>(diagnostic.sourceResource[0]),reinterpret_cast<void*>(diagnostic.sourceResource[1]),
                reinterpret_cast<void*>(diagnostic.copyContext[0]),reinterpret_cast<void*>(diagnostic.copyContext[1]),
                diagnostic.sourceSelectorFlags[0],diagnostic.sourceSelectorFlags[1]);
        }
    }
    return armed.load();
}
bool HaloCE_Armed() noexcept { return Current()&&armed.load(std::memory_order_acquire); }
void HaloCE_Recenter() noexcept
{
    referenceRevision.fetch_add(1,std::memory_order_acq_rel);
    recenter=true;
    completedFrame.Publish({});
}
void HaloCE_PublishTracking(const halo_ce::Tracking& tracking,bool enabled) noexcept
{
    // A valid replacement does not revoke the previous coherent sample while
    // workers read it. Its original timestamp still expires normally if a
    // busy snapshot prevents publication. Only an actual disable revokes it.
    if (!enabled) { trackingEnabled.store(false,std::memory_order_release);return; }
    if (trackingSnapshot.Publish(tracking))
    { trackingAtMs.store(GetTickCount64(),std::memory_order_release); trackingEnabled.store(true,std::memory_order_release); }
}
void HaloCE_SetDlssRequested(bool enabled) noexcept
{ dlssDepthRequested.store(enabled,std::memory_order_release); }
void HaloCE_CaptureClassicDlssDepth(uintptr_t caller) noexcept
{ ClassicCaptureDlssDepth(caller); }
void HaloCE_PresentResources(ID3D11Device* device,ID3D11DeviceContext* context) noexcept
{
    if (!device||!context) return;
    copyTarget.store(reinterpret_cast<uintptr_t>((*reinterpret_cast<void***>(context))[46]),std::memory_order_release);
    if (!HaloCE_Armed()) return;
    Wanted next{};
    if (!wanted.Read(next)||!next.generation||next.generation!=generation.load()||
        next.context!=reinterpret_cast<uintptr_t>(context)) return;
    Wanted previous{};
    const bool same=allocated.Read(previous)&&previous.generation==next.generation&&previous.context==next.context&&
        std::memcmp(&previous.descriptor,&next.descriptor,sizeof(next.descriptor))==0;
    if(!same) {
        if (cache.Prepare(device,context,next.descriptor,next.generation,++resourceEpoch)) {
            allocated.Publish(next);
            LOG("CE eye caches prepared: %ux%u format=%u generation=%u resource=%llu",
                next.descriptor.Width,next.descriptor.Height,next.descriptor.Format,next.generation,resourceEpoch);
        }
        else allocated.Publish({}); // Prepare may have retired the old GPU banks.
    }
    Wanted depth{};
    if(dlssDepthRequested.load(std::memory_order_acquire)&&wantedDlssDepth.Read(depth)&&
        depth.generation==next.generation&&depth.context==next.context)
        (void)cache.PrepareDepth(device,context,depth.descriptor,depth.generation);
}
bool HaloCE_AcquirePair(ID3D11DeviceContext* context,uint64_t currentSerial,uint64_t spaceEpoch,
    halo_ce::EyeCache::Completed& pair) noexcept
{
    if (Current()) (void)CeObserveRendererMode();
    CompletedFrame frame{};
    const uint64_t now=GetTickCount64();
    if (!HaloCE_Armed()||recenter.load()||!completedFrame.Read(frame)||
        frame.referenceRevision!=referenceRevision.load(std::memory_order_acquire)||
        !frame.capturedAtMs||now<frame.capturedAtMs||now-frame.capturedAtMs>=250) return false;
    const auto key=frame.key;
    if (!key.serial||
        key.generation!=generation.load()||key.spaceEpoch!=spaceEpoch||
        key.serial>currentSerial||currentSerial-key.serial>8) return false;
    return cache.AcquireCompleted(key,context,pair);
}
void HaloCE_ReleasePair(uint64_t borrowId) noexcept { cache.ReleaseCompleted(borrowId); }
bool HaloCE_RenderContextCurrent(const halo_ce::RenderContext& context) noexcept
{
    Tracking tracking{};
    return HaloCE_Armed()&&!recenter.load(std::memory_order_acquire)&&
        context.referenceRevision==referenceRevision.load(std::memory_order_acquire)&&
        context.rendererEpoch==ceRendererEpoch.load(std::memory_order_acquire)&&
        context.tracking.generation==generation.load(std::memory_order_acquire)&&
        context.reference.generation==context.tracking.generation&&
        context.reference.spaceEpoch==context.tracking.spaceEpoch&&
        TrackingNow(tracking)&&tracking.spaceEpoch==context.tracking.spaceEpoch&&
        context.tracking.serial&&context.tracking.serial<=tracking.serial&&
        tracking.serial-context.tracking.serial<=8;
}
bool HaloCE_GetGameplayContext(halo_ce::RenderContext& context) noexcept
{
    if (Current()) (void)CeObserveRendererMode();
    GameplayCameraSample sample{};
    const uint64_t now=GetTickCount64();
    if (!gameplayCamera.Read(sample)||!sample.capturedAtMs||now<sample.capturedAtMs||
        now-sample.capturedAtMs>=250||!HaloCE_RenderContextCurrent(sample.context)) return false;
    RenderContext next=sample.context;
    if (!TrackingNow(next.tracking)||!HaloCE_RenderContextCurrent(next)) return false;
    context=next;
    return true;
}
bool HaloCE_OwnsPresentation() noexcept
{
    const uint64_t last=lastOwnedMs.load(std::memory_order_acquire),now=GetTickCount64();
    return Current()&&last&&now>=last&&now-last<500;
}
namespace
{
uint32_t AnniversaryEyeTrackingForCamera(const SaberCamera* camera,Tracking& tracking,
    int eye,unsigned requiredStages) noexcept
{
    tracking={};
    const auto* scope=frameScope;
    if (!camera||!scope||!scope->synthetic||!scope->capture||!scope->prepared.valid) return 30;
    if (!HaloCE_Armed()||!Anniversary()||recenter.load(std::memory_order_acquire)) return 31;
    if (eye<0||eye>1) return 32;
    const uintptr_t address=reinterpret_cast<uintptr_t>(camera);
    const uint32_t mask=1u<<eye;
    const auto& receipt=scope->prepared.receipt;
    const auto& frozen=receipt.tracking;
    const uint64_t revision=referenceRevision.load(std::memory_order_acquire);
    if (address!=scope->renderer+0xf0+eye*sizeof(SaberView)||
        scope->diagnostic.consumedCamera[eye]!=address) return 33;
    if (((requiredStages&1)&&!(scope->diagnostic.consumedDepth&mask))||
        ((requiredStages&2)&&!(scope->diagnostic.consumedScene&mask))||
        ((requiredStages&4)&&!(scope->diagnostic.consumedShading&mask))) return 34;
    if (scope->prepared.referenceRevision!=revision) return 35;
    if (scope->prepared.generation!=generation.load(std::memory_order_acquire)||
        !receipt.ticket.revision||!receipt.ticket.sourceList||
        receipt.ticket.generation!=scope->prepared.generation) return 36;
    // PrepareBody freezes this receipt after the native source-list copy.
    // The next worker may now recycle that private source and advance its
    // ledger ticket while this renderer still draws the copied cameras. A
    // source-ticket query here incorrectly revokes a valid in-flight frame.
    // Keep the live renderer/consumer, lifetime and tracking checks below;
    // never replace the frozen receipt with the next worker's preparation.
    Tracking current{}; SaberCamera consumed{}; uintptr_t selected{};
    if (!TrackingNow(current)||frozen.generation!=current.generation||
        frozen.spaceEpoch!=current.spaceEpoch||!frozen.serial||frozen.serial>current.serial||
        current.serial-frozen.serial>8) return 37;
    if (!Read(address,consumed)||!SamePreparedCamera(consumed,receipt.pair.cameras[eye])||
        std::memcmp(reinterpret_cast<const uint8_t*>(&consumed)+0x220,
            reinterpret_cast<const uint8_t*>(&receipt.pair.cameras[eye])+0x220,4)) return 38;
    if (!Read(scope->renderer+0xbe98,selected)||selected!=address) return 39;
    if (!Current()||!Anniversary()||recenter.load(std::memory_order_acquire)||
        referenceRevision.load(std::memory_order_acquire)!=revision) return 40;
    tracking=frozen;
    return 0;
}
uint32_t AnniversaryEyeTracking(const SaberCamera* camera,Tracking& tracking) noexcept
{
    return AnniversaryEyeTrackingForCamera(camera,tracking,frameScope?frameScope->lastSceneEye:-1,7);
}
}
bool HaloCE_GetAnniversaryEyeTracking(const halo_ce::SaberCamera* camera,
    halo_ce::Tracking& tracking) noexcept
{
    return AnniversaryEyeTracking(camera,tracking)==0;
}
bool HaloCE_GetAnniversaryPrimaryEyeTracking(halo_ce::Tracking& tracking) noexcept
{
    tracking={};
    const auto* scope=frameScope;
    if (anniversaryMaterialMasked||!scope||scope->primaryDrawEye<0||scope->primaryDrawEye>1||
        scope->primaryDrawStage<1||scope->primaryDrawStage>3) return false;
    const int eye=scope->primaryDrawEye;
    const auto* camera=reinterpret_cast<const SaberCamera*>(scope->renderer+0xf0+eye*sizeof(SaberView));
    return AnniversaryEyeTrackingForCamera(camera,tracking,eye,1u<<(scope->primaryDrawStage-1))==0;
}
bool HaloCE_GetAnniversaryPreparedListTracking(uintptr_t list,halo_ce::Tracking& tracking) noexcept
{
    tracking={};
    Tracking now{};SaberViewPair live{};
    if (!list||!HaloCE_Armed()||!Anniversary()||recenter.load(std::memory_order_acquire)||
        !TrackingNow(now)||now.controllers.controlsPresentationBlocked||!Read(list,live)) return false;
    const uint64_t revision=referenceRevision.load(std::memory_order_acquire);
    const uint64_t epoch=ceRendererEpoch.load(std::memory_order_acquire);
    const uint64_t workerRevision=copiedWorkerRevision.load(std::memory_order_acquire);
    for (const auto& slot:preparedLists)
    {
        Prepared prepared{};
        if (!slot.Read(prepared)||!prepared.valid||!prepared.synthetic||
            prepared.generation!=generation.load()||prepared.referenceRevision!=revision) continue;
        const auto& receipt=prepared.receipt;
        const auto& t=receipt.tracking;
        if (prepared.sourceList!=receipt.ticket.sourceList) continue;
        const bool exactSource=list==prepared.sourceList&&list==receipt.ticket.sourceList;
        CopiedWorkerList copied{};
        const bool copiedTarget=receipt.ticket.origin==PreparationOrigin::CopiedList&&
            copiedWorkerList.Read(copied)&&copied.destination==list&&copied.rendererEpoch==epoch&&copied.revision==workerRevision&&
            copied.source.sourceList==prepared.sourceList&&copied.source.referenceRevision==revision&&
            copied.source.receipt.ticket.revision==receipt.ticket.revision&&
            copied.source.receipt.ticket.generation==receipt.ticket.generation&&
            list==activeListAddress.load(std::memory_order_acquire);
        if ((!exactSource&&!copiedTarget)||!handoff.Current(receipt.ticket)||
            !MatchesPreparedViews(live,receipt)||t.generation!=now.generation||
            t.spaceEpoch!=now.spaceEpoch||!t.serial||t.serial>now.serial||now.serial-t.serial>8) continue;
        int32_t players[2]{};
        std::memcpy(&players[0],reinterpret_cast<const uint8_t*>(&live.views[0].camera)+0x220,4);
        std::memcpy(&players[1],reinterpret_cast<const uint8_t*>(&live.views[1].camera)+0x220,4);
        if (players[0]!=0||players[1]!=0) continue;
        SaberViewPair after{};Tracking finalNow{};
        if (!Read(list,after)||!MatchesPreparedViews(after,receipt)||
            std::memcmp(reinterpret_cast<const uint8_t*>(&after.views[0].camera)+0x220,&players[0],4)||
            std::memcmp(reinterpret_cast<const uint8_t*>(&after.views[1].camera)+0x220,&players[1],4)||
            !handoff.Current(receipt.ticket)||referenceRevision.load()!=revision||ceRendererEpoch.load()!=epoch||
            !HaloCE_Armed()||!Anniversary()||recenter.load()||!TrackingNow(finalNow)||
            finalNow.controllers.controlsPresentationBlocked||finalNow.generation!=t.generation||
            finalNow.spaceEpoch!=t.spaceEpoch||finalNow.serial<t.serial||finalNow.serial-t.serial>8) return false;
        if (copiedTarget&&!exactSource)
        {
            CopiedWorkerList final{};
            if (!copiedWorkerList.Read(final)||final.destination!=list||final.rendererEpoch!=epoch||final.revision!=workerRevision||
                final.source.sourceList!=prepared.sourceList||final.source.referenceRevision!=revision||
                final.source.receipt.ticket.revision!=receipt.ticket.revision||
                list!=activeListAddress.load()||copiedWorkerRevision.load()!=workerRevision) return false;
        }
        tracking=t;return true;
    }
    return false;
}
void HaloCE_RecordAnniversaryVisibilitySubmission(uintptr_t list,int32_t phase,uintptr_t caller) noexcept
{
    // This native phase-1 call follows the exact job+70 -> renderer+B0 copy.
    // A matching stationary camera at the renderer alone is never copy proof.
    if (caller!=bindings.base+0x45550b||phase!=1||!jobScope.owned||!jobScope.cameraOwned||
        list!=jobScope.activeList||!HaloCE_Armed()||!Anniversary()||recenter.load()) return;
    RevokeCopiedWorkerList();
    int32_t rendering{},copied{};uintptr_t renderer{};
    Prepared p{};SaberViewPair live{};Tracking tracking{};
    const uint64_t revision=referenceRevision.load(),epoch=ceRendererEpoch.load(),workerRevision=copiedWorkerRevision.load();
    if (!Read(jobScope.job+0xbe58,rendering)||!rendering||
        !Read(jobScope.job+0xbe5c,copied)||!copied||
        !Read(bindings.base+0x1bea9e0,renderer)||renderer+0xb0!=list||
        !preparedLists[1].Read(p)||!p.valid||!p.synthetic||p.generation!=generation.load()||
        p.sourceList!=jobScope.job+0x70||p.sourceList!=p.receipt.ticket.sourceList||
        p.receipt.ticket.origin!=PreparationOrigin::CopiedList||p.referenceRevision!=revision||
        !handoff.Current(p.receipt.ticket)||!TrackingNow(tracking)||tracking.controllers.controlsPresentationBlocked||
        p.receipt.tracking.generation!=tracking.generation||p.receipt.tracking.spaceEpoch!=tracking.spaceEpoch||
        !p.receipt.tracking.serial||p.receipt.tracking.serial>tracking.serial||tracking.serial-p.receipt.tracking.serial>8||
        !Read(list,live)||!MatchesPreparedViews(live,p.receipt)||!handoff.Current(p.receipt.ticket)||
        referenceRevision.load()!=revision||ceRendererEpoch.load()!=epoch) return;
    if (copiedWorkerRevision.load()==workerRevision)
        copiedWorkerList.Publish({p,list,epoch,workerRevision});
}
bool HaloCE_GetClassicPrimaryEyeContext(halo_ce::RenderContext& context) noexcept
{
    const auto* primary=classicPrimaryViewScope;
    return primary&&primary->frame==classicFrameScope&&
        primary->eye==primary->frame->eye&&ClassicScopeCurrent(*primary->frame)&&
        HaloCE_Armed()&&!recenter.load(std::memory_order_acquire)&&
        ClassicGetRenderContext(context)&&HaloCE_RenderContextCurrent(context);
}
bool HaloCE_GetRenderContext(const halo_ce::Camera& stockCamera,
    halo_ce::RenderContext& context) noexcept
{
    if (Current()) (void)CeObserveRendererMode();
    if (!HaloCE_Armed()||recenter.load(std::memory_order_acquire)) return false;
    if (ClassicGetRenderContext(context)) return true;
    if (anniversaryNaturalHud)
    {
        if (!Valid(stockCamera)||!AnniversaryHud_NaturalCurrent(*anniversaryNaturalHud)) return false;
        context=anniversaryNaturalHud->owner;context.camera=stockCamera;
        return true;
    }
    if (anniversaryHudReplay)
    {
        if (!Valid(stockCamera)||!HaloCE_RenderContextCurrent(anniversaryHudReplay->owner)) return false;
        context=anniversaryHudReplay->owner; context.camera=stockCamera;
        return true;
    }
    // A native FP callback supplies the center BEFORE its temporary first-
    // person camera changes. Never derive a center by averaging tracked eyes.
    if (!Anniversary()||!Valid(stockCamera)) return false;
    const uint64_t revision=referenceRevision.load(std::memory_order_acquire);
    RenderContext next{}; ReferenceSample sample{};
    if (!TrackingNow(next.tracking)||!publishedReference.Read(sample)||sample.revision!=revision||
        sample.value.generation!=next.tracking.generation||sample.value.spaceEpoch!=next.tracking.spaceEpoch)
        return false;
    next.reference=sample.value; next.referenceRevision=revision;
    next.rendererEpoch=ceRendererEpoch.load(std::memory_order_acquire);
    next.camera=stockCamera;
    next.unitsPerMeter=Game_GetWorldScale(); next.positional=Game_IsPositionalTracking();
    if (!std::isfinite(next.unitsPerMeter)||next.unitsPerMeter<=0||next.unitsPerMeter>10||
        !Current()||recenter.load(std::memory_order_acquire)||
        revision!=referenceRevision.load(std::memory_order_acquire)) return false;
    context=next;
    return true;
}
bool HaloCE_BeginAnniversaryHudGameplay(ID3D11DeviceContext* context,UINT& width,UINT& height) noexcept
{ return AnniversaryHud_BeginGameplay(context,width,height); }
void HaloCE_EndAnniversaryHudGameplay(bool complete) noexcept
{ AnniversaryHud_EndGameplay(complete); }
void HaloCE_RecordTextureCreated(ID3D11Texture2D* texture,
    const D3D11_TEXTURE2D_DESC& descriptor) noexcept
{
    const auto identity=reinterpret_cast<uintptr_t>(texture);
    resources.Forget(identity);
    if (!identity||!(descriptor.BindFlags&(D3D11_BIND_RENDER_TARGET|D3D11_BIND_DEPTH_STENCIL))) return;
    const auto revision=resources.Revoke(identity);
    resources.Publish(identity,identity,revision,descriptor);
}
void HaloCE_RecordPresentationTexture(ID3D11Texture2D* texture,
    const D3D11_TEXTURE2D_DESC& descriptor) noexcept
{
    const auto identity=reinterpret_cast<uintptr_t>(texture);
    if (!identity) return;
    const auto previous=presentationTexture.exchange(identity,std::memory_order_acq_rel);
    if (previous&&previous!=identity) resources.Forget(previous);
    ResourceRegistry::Record existing{};
    if (resources.Read(identity,identity,existing)&&
        std::memcmp(&existing.descriptor,&descriptor,sizeof(descriptor))==0) return;
    HaloCE_RecordTextureCreated(texture,descriptor);
}
void HaloCE_ForgetPresentationTexture() noexcept
{
    const auto previous=presentationTexture.exchange(0,std::memory_order_acq_rel);
    if (previous) resources.Forget(previous);
    completedFrame.Publish({});
}
