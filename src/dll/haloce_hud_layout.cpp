#include "../common/hud_visibility.h"
#include "haloce_hud_layout.h"
#include "haloce_hud.h"
#include "haloce_native_bindings.h"
#include "haloce_stereo_core.h"
#include "hook_quiescence.h"
#include "title_adapter.h"
#include "../common/haloce_hud_layout.h"
#include "../common/minhook_lifecycle.h"
#include "../common/log.h"
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <intrin.h>

namespace
{
using namespace halo_ce;
constexpr UINT capacity=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
using MainFn=void(__fastcall*)();
HMODULE retained{};
uintptr_t moduleBase{};
void* target{};
MainFn original{};
bool enabled{};
std::atomic<bool> installed{},active{},retiring{},observationAvailable{};
std::atomic<uint32_t> generation{},callbacks{};
std::atomic<uint64_t> observationEpoch{1},draws{},fallbacks{};
std::atomic<uint64_t> nativeResourceRefusals{};
uint32_t rejectedGeneration{};
uint64_t lastReport{};
struct MutationSlot
{
    std::atomic<ID3D11DeviceContext*> context{};
    std::atomic<uint64_t> revision{1};
};
// Stable slots are never recycled while callbacks can reference them. Reusing
// a native pointer advances its existing revision; an exhausted bounded table
// leaves only that context unobserved. No render-thread lock, wait or eviction.
MutationSlot mutationSlots[64];
struct RasterState
{
    ID3D11DeviceContext* context{};
    uint64_t epoch{};
    bool viewportsKnown{},scissorsKnown{};
    UINT viewportCount{},scissorCount{};
    D3D11_VIEWPORT viewports[capacity]{};
    D3D11_RECT scissors[capacity]{};
    MutationSlot* mutation{};
    uint64_t revision{};
};
thread_local RasterState states[4];
thread_local UINT replaceSlot{},suspensions{},privateRasterDepth{};
thread_local bool writing{};
struct LayoutScope
{
    RasterState* state{};
    HudLayoutAffine affine;
    RenderContext owner;
    bool live{};
};
thread_local LayoutScope* scope{};
struct EyeReplayRaster
{
    RasterState* state{};
    RasterState saved;
    bool live{},valid{};
    HudLayoutAffine affine{1.0f,0.5f,0.0f,0.0f};
    bool clipToEye{};
    D3D11_RECT bounds{};
};
thread_local EyeReplayRaster eyeReplay;
void ClipEyeScissors(D3D11_RECT* values,UINT count) noexcept
{
    if (!eyeReplay.clipToEye) return;
    const auto& b=eyeReplay.bounds;
    for (UINT i=0;i<count;++i)
    {
        auto& r=values[i];
        r.left=std::clamp(r.left,b.left,b.right);r.right=std::clamp(r.right,b.left,b.right);
        r.top=std::clamp(r.top,b.top,b.bottom);r.bottom=std::clamp(r.bottom,b.top,b.bottom);
    }
}

bool Current() noexcept
{
    return active.load()&&installed.load()&&!retiring.load()&&observationAvailable.load()&&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load();
}
MutationSlot* FindMutation(ID3D11DeviceContext* context,bool create) noexcept
{
    for (auto& slot:mutationSlots)
        if (slot.context.load(std::memory_order_acquire)==context) return &slot;
    if (!create) return nullptr;
    for (auto& slot:mutationSlots)
    {
        ID3D11DeviceContext* empty{};
        if (slot.context.compare_exchange_strong(empty,context,std::memory_order_acq_rel)||empty==context)
            return &slot;
    }
    return nullptr;
}
bool StateCurrent(RasterState& state) noexcept
{
    if (state.epoch!=observationEpoch.load(std::memory_order_acquire)||!state.mutation||
        state.revision!=state.mutation->revision.load(std::memory_order_acquire))
    { state.viewportsKnown=state.scissorsKnown=false; return false; }
    return true;
}
RasterState* Find(ID3D11DeviceContext* context,bool create) noexcept
{
    if (!context) return nullptr;
    const uint64_t epoch=observationEpoch.load(std::memory_order_acquire);
    for (auto& state:states)
        if (state.context==context)
        {
            MutationSlot* mutation=state.mutation;
            if (state.epoch!=epoch) state={context,epoch};
            if (!mutation) mutation=FindMutation(context,create);
            if (!mutation) return nullptr;
            state.mutation=mutation;
            const uint64_t revision=mutation->revision.load(std::memory_order_acquire);
            if (state.revision!=revision) state.viewportsKnown=state.scissorsKnown=false;
            state.revision=revision;
            return &state;
        }
    if (!create) return nullptr;
    auto* mutation=FindMutation(context,true);
    if (!mutation) return nullptr;
    // Never evict an in-flight HUD's known native state.
    for (UINT n=0;n<4;++n)
    {
        auto& state=states[(replaceSlot++)%4];
        if ((scope&&scope->state==&state)||(eyeReplay.live&&eyeReplay.state==&state)) continue;
        state={context,epoch};
        state.mutation=mutation;
        state.revision=mutation->revision.load(std::memory_order_acquire);
        return &state;
    }
    return nullptr;
}
HudLayoutViewport View(const D3D11_VIEWPORT& v) noexcept
{ return {v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth}; }
bool Transform(const HudLayoutAffine& a,const D3D11_VIEWPORT* in,UINT count,D3D11_VIEWPORT* out) noexcept
{
    for (UINT i=0;i<count;++i)
    {
        HudLayoutViewport v{};
        if (!ApplyHudLayoutViewport(a,View(in[i]),v)) return false;
        out[i]={v.x,v.y,v.width,v.height,v.minDepth,v.maxDepth};
    }
    return true;
}
bool Transform(const HudLayoutAffine& a,const D3D11_RECT* in,UINT count,D3D11_RECT* out) noexcept
{
    for (UINT i=0;i<count;++i)
    {
        HudLayoutRect r{};
        if (!ApplyHudLayoutScissor(a,{in[i].left,in[i].top,in[i].right,in[i].bottom},r)) return false;
        out[i]={r.left,r.top,r.right,r.bottom};
    }
    return true;
}
bool WriteState(RasterState& state,const HudLayoutAffine* affine) noexcept
{
    if (!StateCurrent(state)||!state.context||!state.viewportsKnown||!state.scissorsKnown) return false;
    D3D11_VIEWPORT v[capacity]{};
    D3D11_RECT r[capacity]{};
    if (affine&&(!Transform(*affine,state.viewports,state.viewportCount,v)||
        !Transform(*affine,state.scissors,state.scissorCount,r))) return false;
    const D3D11_VIEWPORT* views=affine?v:state.viewports;
    const D3D11_RECT* rects=affine?r:state.scissors;
    if (eyeReplay.live&&eyeReplay.state==&state&&!privateRasterDepth)
    {
        if (!Transform(eyeReplay.affine,views,state.viewportCount,v)||
            !Transform(eyeReplay.affine,rects,state.scissorCount,r))
        { eyeReplay.valid=false; return false; }
        ClipEyeScissors(r,state.scissorCount);
        views=v; rects=r;
    }
    writing=true;
    bool returned=false;
    __try
    {
        state.context->RSSetViewports(state.viewportCount,views);
        state.context->RSSetScissorRects(state.scissorCount,rects);
        returned=true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    writing=false;
    return returned;
}
void DropScope() noexcept
{
    if (!scope||!scope->live) return;
    scope->live=false;
    if (scope->state) WriteState(*scope->state,nullptr);
    fallbacks.fetch_add(1,std::memory_order_relaxed);
}
bool Transforming(RasterState* state) noexcept
{
    if (!scope||!scope->live||scope->state!=state||suspensions) return false;
    if (!StateCurrent(*state)||!state->viewportsKnown||!state->scissorsKnown||
        !Current()||!HaloCE_RenderContextCurrent(scope->owner))
    { DropScope(); return false; }
    return true;
}
bool NativeOwner(RenderContext& out,ID3D11DeviceContext*& context) noexcept
{
    Camera source{};
    int16_t player=-1;
    __try
    {
        std::memcpy(&source,reinterpret_cast<const void*>(moduleBase+0x29af2c4),sizeof(source));
        std::memcpy(&context,reinterpret_cast<const void*>(moduleBase+0x2ea2d30),sizeof(context));
        std::memcpy(&player,reinterpret_cast<const void*>(moduleBase+0x29af2b8),sizeof(player));
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return player==0&&context&&HaloCE_GetRenderContext(source,out)&&
        HaloCE_RenderContextCurrent(out);
}
void MainLayoutBody()
{
    LayoutScope local{};
    ID3D11DeviceContext* context{};
    if (scope||suspensions||!Current()||!HaloCEHud_HasCrosshairScope()||
        !NativeOwner(local.owner,context))
    { original(); return; }
    auto* state=Find(context,false);
    const auto& h=local.owner.tracking.hud;
    const auto& rect=local.owner.camera.viewport;
    const float gameAspect=float(rect.right-rect.left)/float(rect.bottom-rect.top);
    if (!state||!state->viewportsKnown||!state->scissorsKnown||state->viewportCount!=1||
        state->viewports[0].TopLeftX!=float(rect.left)||
        state->viewports[0].TopLeftY!=float(rect.top)||
        state->viewports[0].Width!=float(rect.right-rect.left)||
        state->viewports[0].Height!=float(rect.bottom-rect.top)||
        !ComputeHudLayoutAffine(h.size,h.aspect,h.verticalOffset,gameAspect,
            local.owner.tracking.eyes[0].fov,View(state->viewports[0]),local.affine))
    { fallbacks.fetch_add(1,std::memory_order_relaxed); original(); return; }
    // Height is configured in output pixels. Preserve that distance for both
    // native half-height eyes and full-height VR eyes; only the former needs
    // its authored translation doubled before the final eye mapping.
    if (eyeReplay.live&&eyeReplay.state==state)
        local.affine.offsetY-=h.verticalOffset*(1.0f/eyeReplay.affine.vertical-1.0f);
    local.state=state;
    local.live=true;
    scope=&local;
    if (!WriteState(*state,&local.affine)) DropScope();
    __try { original(); }
    __finally
    {
        if (local.live)
        {
            if (WriteState(*state,nullptr)) draws.fetch_add(1,std::memory_order_relaxed);
            else fallbacks.fetch_add(1,std::memory_order_relaxed);
        }
        scope=nullptr;
    }
}
void MainBody()
{
    // The real switch dump reached the ordinary original() fallback, before
    // target preparation. A disposed native effect table must not be consumed
    // by that fallback either. The outer native callback still owns cleanup;
    // only this gameplay HUD draw waits for native resource reconstruction.
    if (!HaloCE_NativeHudResourcesReady(moduleBase,generation.load(std::memory_order_acquire)))
    { nativeResourceRefusals.fetch_add(1,std::memory_order_relaxed);return; }
    RenderContext owner{};ID3D11DeviceContext* context{};UINT width{},height{};
    if (scope||suspensions||!Current()||!NativeOwner(owner,context)||
        !HaloCE_BeginAnniversaryHudGameplay(context,width,height))
    { MainLayoutBody();return; }
    bool complete=false,drew=false;
    // The native HUD canvas keeps desktop dimensions when VR eye allocation
    // grows. The packed target contains two eyes, not the authored HUD canvas.
    const auto& native=owner.camera.viewport;
    const UINT nativeWidth=static_cast<UINT>(native.right-native.left);
    const UINT nativeHeight=static_cast<UINT>(native.bottom-native.top);
    __try
    {
        complete=true;
        for (UINT eye=0;eye<2;++eye)
        {
            bool restored=true;
            if (!HaloCEHudLayout_BeginEyeReplay(context,nativeWidth,nativeHeight,width,height/2,&restored,eye*(height/2),true))
            {
                complete=false;
                if (!drew&&restored) MainLayoutBody();
                break;
            }
            bool returned=false;
            __try { drew=true;MainLayoutBody();returned=true; }
            __finally { restored=HaloCEHudLayout_EndEyeReplay();if (!returned) complete=false; }
            if (!returned||!restored) { complete=false;break; }
        }
    }
    __finally { HaloCE_EndAnniversaryHudGameplay(complete); }
}
void __fastcall MainHook()
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    HaloCE_CaptureClassicDlssDepth(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    RenderContext owner{};ID3D11DeviceContext* context{};
    const bool hideHud=Current()&&NativeOwner(owner,context)&&owner.tracking.hud.hidden;
    if(hideHud) ++hud_visibility::depth;
    __try { MainBody(); }
    __finally { if(hideHud) --hud_visibility::depth;callbacks.fetch_sub(1,std::memory_order_release); }
}
bool Remove() noexcept
{
    retiring=true; active=false; installed=false;
    if (enabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(target);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return false;
        enabled=false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&MainHook)};
    const void* originals[]{reinterpret_cast<const void*>(original)};
    if (target&&!WaitForNativeDetourQuiescence(functions,originals,1,callbacks)) return false;
    if (target)
    {
        const auto result=MH_RemoveHook(target);
        if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return false;
    }
    target=nullptr; original=nullptr;
    observationEpoch.fetch_add(1,std::memory_order_acq_rel);
    if (retained) { FreeLibrary(retained); retained=nullptr; }
    moduleBase=0; generation=0; retiring=false;
    return true;
}
bool Install(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    const NativeContractSet set{contract::hud_layout::entries,contract::hud_layout::witnesses,
        contract::hud_layout::relatives,contract::hud_layout::pointers};
    const char* failure{};
    if (!VerifyNativeFeatureBindings(base,size,gen,set,failure))
    { LOG("CE HUD layout stock fallback: %s",failure?failure:"binding verification"); return false; }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(base),&retained)) return false;
    moduleBase=base; generation=gen; retiring=false;
    void* candidate=reinterpret_cast<void*>(base+contract::hud_layout::hud_gameplay_draw);
    if (MH_CreateHook(candidate,reinterpret_cast<void*>(&MainHook),
        reinterpret_cast<void**>(&original))!=MH_OK) { Remove(); return false; }
    target=candidate;
    if (MH_EnableHook(target)!=MH_OK) { Remove(); return false; }
    enabled=true; installed=true; active=true;
    LOG("CE HUD layout installed: native gameplay scope, observed raster size/aspect/height; curvature remains native flat, visible results unverified");
    return true;
}
}

void HaloCEHudLayout_SetObservationAvailable(bool available) noexcept
{
    if (observationAvailable.exchange(available)!=available)
        observationEpoch.fetch_add(1,std::memory_order_acq_rel);
}
bool HaloCEHudLayout_PrepareViewports(ID3D11DeviceContext* context,UINT count,
    const D3D11_VIEWPORT* values,D3D11_VIEWPORT* out) noexcept
{
    if (!active.load(std::memory_order_acquire)||writing||privateRasterDepth||!observationAvailable.load()) return false;
    auto* state=Find(context,true);
    if (!state) return false;
    if (count>capacity||(count&&!values)||!out)
    { DropScope(); state->viewportsKnown=false; return false; }
    state->viewportCount=count;
    if (count) std::memcpy(state->viewports,values,sizeof(*values)*count);
    state->viewportsKnown=true;
    const uint64_t previous=state->mutation->revision.fetch_add(1,std::memory_order_acq_rel);
    if (state->revision!=previous) state->scissorsKnown=false;
    state->revision=previous+1;
    bool changed=false;
    if (Transforming(state))
    {
        if (count!=1||!Transform(scope->affine,values,count,out)) DropScope();
        else changed=true;
    }
    if (eyeReplay.live&&eyeReplay.state==state)
    {
        if (!Transform(eyeReplay.affine,changed?out:values,count,out))
        { eyeReplay.valid=false; return changed; }
        changed=true;
    }
    return changed;
}
bool HaloCEHudLayout_PrepareScissors(ID3D11DeviceContext* context,UINT count,
    const D3D11_RECT* values,D3D11_RECT* out) noexcept
{
    if (!active.load(std::memory_order_acquire)||writing||privateRasterDepth||!observationAvailable.load()) return false;
    auto* state=Find(context,true);
    if (!state) return false;
    if (count>capacity||(count&&!values)||!out)
    { DropScope(); state->scissorsKnown=false; return false; }
    state->scissorCount=count;
    if (count) std::memcpy(state->scissors,values,sizeof(*values)*count);
    state->scissorsKnown=true;
    const uint64_t previous=state->mutation->revision.fetch_add(1,std::memory_order_acq_rel);
    if (state->revision!=previous) state->viewportsKnown=false;
    state->revision=previous+1;
    bool changed=false;
    if (Transforming(state))
    {
        if (!Transform(scope->affine,values,count,out)) DropScope();
        else changed=true;
    }
    if (eyeReplay.live&&eyeReplay.state==state)
    {
        if (!Transform(eyeReplay.affine,changed?out:values,count,out))
        { eyeReplay.valid=false; return changed; }
        ClipEyeScissors(out,count);
        changed=true;
    }
    return changed;
}
void HaloCEHudLayout_InvalidateState(ID3D11DeviceContext* context) noexcept
{
    if (!active.load(std::memory_order_acquire)||writing) return;
    auto* state=Find(context,true);
    if (!state) return;
    if (eyeReplay.live&&eyeReplay.state==state) eyeReplay.valid=false;
    if (scope&&scope->state==state) DropScope();
    state->revision=state->mutation->revision.fetch_add(1,std::memory_order_acq_rel)+1;
    state->viewportsKnown=state->scissorsKnown=false;
}
void HaloCEHudLayout_Suspend() noexcept
{
    if (suspensions++==0&&scope&&scope->live) WriteState(*scope->state,nullptr);
}
void HaloCEHudLayout_Resume() noexcept
{
    if (!suspensions||--suspensions||!scope||!scope->live) return;
    if (!Transforming(scope->state)||!WriteState(*scope->state,&scope->affine)) DropScope();
}
bool HaloCEHudLayout_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive) noexcept
{
    if (active.exchange(isActive,std::memory_order_acq_rel)!=isActive)
        observationEpoch.fetch_add(1,std::memory_order_acq_rel);
    if (retained&&(!isActive||base!=moduleBase||gen!=generation.load()||retiring.load()))
        if (!Remove()) return false;
    if (!isActive||!base||!gen||!observationAvailable.load()) return false;
    if (!installed.load()&&gen!=rejectedGeneration&&HaloCE_Armed())
        if (!Install(base,size,gen))
        { rejectedGeneration=gen; LOG("CE HUD layout stock fallback: optional installation failed; camera retained"); }
    const uint64_t now=GetTickCount64();
    if (installed.load()&&now-lastReport>=2000)
    {
        lastReport=now;
        LOG("CE HUD layout gen=%u draws=%llu stockFallbacks=%llu nativeResourcesUnavailable=%llu curvature=native-flat",
            gen,draws.load(),fallbacks.load(),nativeResourceRefusals.load());
    }
    return Current();
}
bool HaloCEHudLayout_CopyState(ID3D11DeviceContext* context,UINT* viewportCount,
    D3D11_VIEWPORT* viewports,UINT* scissorCount,D3D11_RECT* scissors) noexcept
{
    if (!active.load(std::memory_order_acquire)||!observationAvailable.load()||!viewportCount||!viewports||!scissorCount||!scissors) return false;
    auto* state=Find(context,false);
    if (!state||!state->viewportsKnown||!state->scissorsKnown) return false;
    if (Transforming(state))
    {
        if (!Transform(scope->affine,state->viewports,state->viewportCount,viewports)||
            !Transform(scope->affine,state->scissors,state->scissorCount,scissors)) return false;
    }
    else
    {
        std::memcpy(viewports,state->viewports,sizeof(*viewports)*state->viewportCount);
        std::memcpy(scissors,state->scissors,sizeof(*scissors)*state->scissorCount);
    }
    *viewportCount=state->viewportCount; *scissorCount=state->scissorCount;
    return true;
}
void HaloCEHudLayout_BeginPrivateRaster() noexcept { ++privateRasterDepth; }
void HaloCEHudLayout_EndPrivateRaster() noexcept { if (privateRasterDepth) --privateRasterDepth; }
bool HaloCEHudLayout_BeginEyeReplay(ID3D11DeviceContext* context,UINT nativeWidth,UINT nativeHeight,
    UINT eyeWidth,UINT eyeHeight,bool* cleanupVerified,UINT outputTop,bool clipToEye) noexcept
{
    if (cleanupVerified) *cleanupVerified=true;
    if (!Current()||scope||suspensions||writing||privateRasterDepth||eyeReplay.live||!context||
        !eyeWidth||!eyeHeight||eyeWidth>16384||eyeHeight>8192||
        nativeWidth!=eyeWidth||(nativeHeight!=eyeHeight*2&&nativeHeight!=eyeHeight)||
        (outputTop!=0&&outputTop!=eyeHeight)) return false;
    auto* state=Find(context,false);
    if (!state||!StateCurrent(*state)||!state->viewportsKnown||!state->scissorsKnown) return false;
    if (cleanupVerified) *cleanupVerified=false;
    eyeReplay={state,*state,true,true};
    eyeReplay.affine.vertical=float(eyeHeight)/float(nativeHeight);
    eyeReplay.affine.offsetY=float(outputTop);
    eyeReplay.clipToEye=clipToEye;
    eyeReplay.bounds={0,LONG(outputTop),LONG(eyeWidth),LONG(outputTop+eyeHeight)};
    state->viewportCount=state->scissorCount=1;
    state->viewports[0]={0,0,float(nativeWidth),float(nativeHeight),0,1};
    state->scissors[0]={0,0,LONG(nativeWidth),LONG(nativeHeight)};
    // Seed the complete known HUD raster even if native state caching skips a
    // redundant viewport set. Numeric observations stay in authored pixels.
    if (WriteState(*state,nullptr)) return true;
    const bool restored=HaloCEHudLayout_EndEyeReplay();
    if (cleanupVerified) *cleanupVerified=restored;
    return false;
}
bool HaloCEHudLayout_EndEyeReplay() noexcept
{
    if (!eyeReplay.live) return false;
    auto* state=eyeReplay.state;
    const bool valid=eyeReplay.valid&&state&&StateCurrent(*state)&&!scope&&!privateRasterDepth;
    eyeReplay.live=false;
    bool restored=false;
    if (state&&StateCurrent(*state))
    {
        const auto revision=state->revision;
        *state=eyeReplay.saved; state->revision=revision;
        restored=WriteState(*state,nullptr);
    }
    eyeReplay={};
    return valid&&restored;
}
