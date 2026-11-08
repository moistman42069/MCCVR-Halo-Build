#include "haloce_hud.h"
#include "haloce_hud_layout.h"
#include "haloce_first_person.h"
#include "haloce_unit_control.h"
#include "haloce_native_bindings.h"
#include "haloce_stereo_core.h"
#include "game.h"
#include "hook_quiescence.h"
#include "title_adapter.h"
#include "vr.h"
#include "../common/minhook_lifecycle.h"
#include "../common/haloce_snapshot.h"
#include "../common/log.h"
#include <windows.h>
#include <MinHook.h>
#include <cstring>

namespace
{
using namespace halo_ce;
using CrosshairFn=void(__fastcall*)(int32_t,uint32_t,uint32_t,const void*);
// The static-element draw's six arguments, kept as whole registers and stack slots and handed on unchanged.
using StaticElementFn=void(__fastcall*)(uint64_t,const int16_t*,const uint8_t*,uint64_t,uint64_t,uint64_t);
HMODULE retained{};
uintptr_t moduleBase{};
void* target{};
CrosshairFn original{};
bool hookEnabled{};
void* seatTarget{};
StaticElementFn seatOriginal{};
bool seatHookEnabled{};
std::atomic<bool> installed{},active{},retiring{},prepared{};
std::atomic<uint32_t> generation{},callbacks{};
std::atomic<uint64_t> captures{},fallbacks{},contextRefusals{};
std::atomic<uint64_t> vehicleCaptures{},seatCaptures{};
uint32_t rejectedGeneration{},rejectedCaptureGeneration{};
bool targetBindingsVerified{};
uint64_t lastReport{};
thread_local bool drawing{};
thread_local uint64_t lastSerial{},lastRevision{};
struct CaptureReceipt
{
    RenderContext context;
    uint64_t key{},capturedAtMs{};
};
Snapshot<CaptureReceipt> lastCapture;

bool Current() noexcept
{
    return active.load()&&installed.load()&&!retiring.load()&&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load();
}
bool Context(RenderContext& out,bool& vehicle) noexcept
{
    HaloCELocalPlayerState player{};
    if (!HaloCEFirstPerson_AimArmed()||!HaloCEFirstPerson_GetLocalPlayerState(player)||
        !player.hasControlledUnit||
        player.nativeInputBlocked||player.nativeLookBlocked||player.nativePaused||player.nativeCinematicFlag)
        return false;
    Camera source{};
    ID3D11DeviceContext* nativeContext{};
    __try
    {
        std::memcpy(&source,reinterpret_cast<const void*>(moduleBase+0x29af2c4),sizeof(source));
        std::memcpy(&nativeContext,reinterpret_cast<const void*>(moduleBase+0x2ea2d30),sizeof(nativeContext));
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (!HaloCE_GetRenderContext(source,out)||out.tracking.controllers.controlsPresentationBlocked||
        !out.tracking.controllers.primaryAim.valid||
        !VR_CeAuthoredReticleFrameMatches(nativeContext,out.tracking.serial)) return false;
    if (player.onFoot) return player.nativePreparesFirstPerson;
    // A following vehicle camera deliberately does not prepare a first-person
    // model. Capture its authored reticle independently of that model gate.
    vehicle=HaloCEUnitControl_VehicleAimCurrent(player,out);
    return vehicle;
}
void InvalidateCapture() noexcept
{
    lastCapture.Publish({});
    // A failed native scope cannot lend its queued coverage to a later
    // renderer/reference. Other titles retain their own capture lifecycle.
    if (TitleAdapter_GetActiveTitle()==GameTitle::HaloCE)
        VR_InvalidatePreparedAuthoredReticleCapture();
}
// One native reticle draw: the weapon crosshair, or a seat HUD's centre-anchored background (SeatReticleBody).
struct NativeArt
{
    int32_t user{};uint32_t weapon{},hud{};const void* state{};
    bool seat{};
    uint64_t seatUser{};const int16_t* anchor{};const uint8_t* element{};
    uint64_t flags{},flashStart{},layer{};
    void Draw() const
    {
        if (seat) seatOriginal(seatUser,anchor,element,flags,flashStart,layer);
        else original(user,weapon,hud,state);
    }
};
// How far above the HUD's centre a seat background draws, in the HUD's height: its element offset is (0,-40) and
// centre-anchored elements scale by height/960 (0xB56A58). Its capture window is lowered by that much.
constexpr float kSeatReticleLift=40.0f/960.0f;
void DrawBody(const NativeArt& art,uint64_t key)
{
    RenderContext context{};
    bool vehicle=false;
    if (!Current()||!prepared.load())
    { InvalidateCapture(); art.Draw(); return; }
    // A seat background is a reticle only for a seated player.
    if (!Context(context,vehicle)||(art.seat&&!vehicle))
    {
        contextRefusals.fetch_add(1,std::memory_order_relaxed);
        InvalidateCapture(); art.Draw(); return;
    }
    key^=uint64_t(context.tracking.generation)*0x9e3779b97f4a7c15ull;
    if (!key) key=1;
    CaptureReceipt previous{};
    const bool previousCurrent=lastCapture.Read(previous)&&previous.key&&
        HaloCE_RenderContextCurrent(previous.context);
    if (!previousCurrent) VR_InvalidatePreparedAuthoredReticleCapture();
    const bool first=context.tracking.serial!=lastSerial||context.referenceRevision!=lastRevision||
        !previousCurrent||previous.key!=key;
    // Use the working titles' prepared phase redirect. Native crosshair logic
    // still runs on both eyes. A pending coverage query owns the source pixels;
    // even a new weapon's first phase must use discard until it completes.
    // Zoomed, or just after the frame, the crosshair is the zoom's art: it is hidden, not captured (game.cpp).
    const bool capture=first&&Game_AimFrameCrosshairReadyForCapture(GameTitle::HaloCE)&&
        VR_ShouldCaptureAuthoredReticleThisFrame();
    const uint64_t capturedKey=capture?key:previous.key;
    VR_SetCeAuthoredReticleLift(art.seat?kSeatReticleLift:0.0f);
    const bool began=capture?VR_BeginPreparedAuthoredReticleCapture():
        VR_BeginPreparedAuthoredReticleSuppression();
    VR_SetCeAuthoredReticleLift(0.0f);
    if (!began)
    {
        fallbacks.fetch_add(1,std::memory_order_relaxed);
        InvalidateCapture();
        art.Draw();
        return;
    }
    drawing=true;
    bool returned=false;
    __try { art.Draw(); returned=true; }
    __finally
    {
        const bool ended=capture?VR_EndPreparedAuthoredReticleCapture():
            VR_EndPreparedAuthoredReticleSuppression();
        drawing=false;
        if (returned&&ended&&Current()&&HaloCE_RenderContextCurrent(context))
        {
            lastSerial=context.tracking.serial; lastRevision=context.referenceRevision;
            // A complete phase has an explicit generation/renderer/reference
            // owner. Recenter, switching and a missing native phase cannot
            // inherit an old "captured once" latch.
            // A discard-only weapon change still owns native suppression,
            // but must not relabel the queued old artwork as the new weapon.
            lastCapture.Publish({context,capturedKey,GetTickCount64()});
            captures.fetch_add(1,std::memory_order_relaxed);
            if (vehicle) vehicleCaptures.fetch_add(1,std::memory_order_relaxed);
            if (art.seat) seatCaptures.fetch_add(1,std::memory_order_relaxed);
        }
        else
        {
            InvalidateCapture();
            fallbacks.fetch_add(1,std::memory_order_relaxed);
        }
    }
}
void CrosshairBody(int32_t user,uint32_t weapon,uint32_t hud,const void* state)
{
    const NativeArt art{user,weapon,hud,state};
    if (drawing||user!=0) { art.Draw(); return; }
    // AIM FRAME ZOOM: the frame is the flat game's picture, and the game draws its crosshair and zoom art into it.
    // The art held from before the zoom stays the reticle's.
    if (Game_IsAimFramePass()) { art.Draw(); return; }
    DrawBody(art,(uint64_t(hud)<<32)|weapon);
}
void __fastcall CrosshairHook(int32_t user,uint32_t weapon,uint32_t hud,const void* state)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    HaloCEHudLayout_Suspend();
    __try { CrosshairBody(user,weapon,hud,state); }
    __finally
    {
        HaloCEHudLayout_Resume();
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
// THE DRIVER'S CHEVRON. A seat without the gunner bit never reaches the weapon crosshair (0xB3EE67), so a Warthog
// driver's reticle is its seat HUD's centre-anchored background ("warthog driver"), which the unit HUD 0xC5093C draws
// through the shared static-element draw 0xB5808C from its one background call (0xC50E27). That call, for a
// centre-anchored background, is captured like the crosshair: on the controller's aim, where the Warthog steers.
// Every other static element draws as it is.
bool CentreAnchored(const int16_t* anchor) noexcept
{
    __try { return *anchor==4; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void SeatReticleBody(uintptr_t caller,uint64_t user,const int16_t* anchor,const uint8_t* element,
    uint64_t flags,uint64_t flashStart,uint64_t layer)
{
    namespace seat=contract::hud_seat_reticle;
    static_assert(seat::relatives[0].target==seat::hud_static_element_draw);
    constexpr uint32_t afterBackgroundCall=seat::relatives[0].rva+seat::relatives[0].size;
    NativeArt art{};
    art.seat=true;art.seatUser=user;art.anchor=anchor;art.element=element;
    art.flags=flags;art.flashStart=flashStart;art.layer=layer;
    if (caller!=moduleBase+afterBackgroundCall||!anchor||
        element!=reinterpret_cast<const uint8_t*>(anchor)+0x24||!CentreAnchored(anchor)||
        drawing||static_cast<uint16_t>(user)!=0||Game_IsAimFramePass())
    { art.Draw(); return; }
    HaloCEHudLayout_Suspend();
    __try { DrawBody(art,uint64_t(reinterpret_cast<uintptr_t>(element))^0x5ea7c4e7a0000000ull); }
    __finally { HaloCEHudLayout_Resume(); }
}
void __fastcall SeatReticleHook(uint64_t user,const int16_t* anchor,const uint8_t* element,
    uint64_t flags,uint64_t flashStart,uint64_t layer)
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { SeatReticleBody(reinterpret_cast<uintptr_t>(_ReturnAddress()),user,anchor,element,flags,flashStart,layer); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
}
bool Remove() noexcept
{
    retiring=true; active=false; installed=false; prepared=false; lastCapture.Publish({});
    if (hookEnabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(target);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return false;
        hookEnabled=false;
    }
    if (seatHookEnabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(seatTarget);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return false;
        seatHookEnabled=false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&CrosshairHook),reinterpret_cast<const void*>(&SeatReticleHook)};
    const void* originals[]{reinterpret_cast<const void*>(original),reinterpret_cast<const void*>(seatOriginal)};
    if ((target||seatTarget)&&!WaitForNativeDetourQuiescence(functions,originals,2,callbacks)) return false;
    for (void* hooked:{target,seatTarget}) if (hooked)
    {
        const auto result=MH_RemoveHook(hooked);
        if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return false;
    }
    target=nullptr; original=nullptr; seatTarget=nullptr; seatOriginal=nullptr;
    if (retained) { FreeLibrary(retained); retained=nullptr; }
    moduleBase=0; generation=0; retiring=false; targetBindingsVerified=false;
    return true;
}
// Optional: without it a Warthog driver's chevron stays in the HUD, as before.
void InstallSeatReticle(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    namespace seat=contract::hud_seat_reticle;
    const NativeContractSet set{seat::entries,seat::witnesses,seat::relatives,seat::pointers};
    const char* failure{};
    if (!VerifyNativeFeatureBindings(base,size,gen,set,failure))
    { LOG("CE driver chevron stock fallback: %s",failure?failure:"binding verification"); return; }
    void* candidate=reinterpret_cast<void*>(base+seat::hud_static_element_draw);
    if (MH_CreateHook(candidate,reinterpret_cast<void*>(&SeatReticleHook),
        reinterpret_cast<void**>(&seatOriginal))!=MH_OK)
    { LOG("CE driver chevron stock fallback: hook creation failed"); return; }
    seatTarget=candidate;
    if (MH_EnableHook(seatTarget)!=MH_OK)
    { LOG("CE driver chevron stock fallback: hook enable failed"); return; }
    seatHookEnabled=true;
    LOG("CE driver chevron scope installed: a seat HUD's centre background is captured on the controller's aim");
}
bool Install(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    const NativeContractSet set{contract::hud::entries,contract::hud::witnesses,
        contract::hud::relatives,contract::hud::pointers};
    const char* failure{};
    if (!VerifyNativeFeatureBindings(base,size,gen,set,failure))
    { LOG("CE crosshair stock fallback: %s",failure?failure:"binding verification"); return false; }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(base),&retained)) return false;
    moduleBase=base; generation=gen; retiring=false;
    void* candidate=reinterpret_cast<void*>(base+contract::hud::hud_crosshair_draw);
    if (MH_CreateHook(candidate,reinterpret_cast<void*>(&CrosshairHook),
        reinterpret_cast<void**>(&original))!=MH_OK) { Remove(); return false; }
    target=candidate;
    if (MH_EnableHook(target)!=MH_OK) { Remove(); return false; }
    hookEnabled=true; installed=true; active=true; prepared=false;
    LOG("CE crosshair scope installed: native framing isolated from gameplay HUD; optional authored capture pending");
    InstallSeatReticle(base,size,gen);
    return true;
}
void PrepareCapture(uintptr_t base,size_t size,uint32_t gen) noexcept
{
    if (prepared.load()||gen==rejectedCaptureGeneration) return;
    if (!targetBindingsVerified)
    {
        // The core owns the texture-release hook also witnessed by this
        // optional contract. Its full cold proof precedes installing hooks;
        // rescanning here would reject our own jump at hud_target_release.
        if (!HaloCE_HudTargetBindingsVerified(base,size,gen))
        {
            rejectedCaptureGeneration=gen;
            LOG("CE crosshair native-art fallback: current cold target proof unavailable; native scope and gameplay HUD retained");
            return;
        }
        targetBindingsVerified=true;
    }
    if (!VR_CanPrepareAuthoredReticleResources()) return;
    const auto result=VR_PrepareAuthoredReticleResources();
    if (result==AuthoredReticlePreparationResult::Ready)
    {
        if (VR_PrepareAuthoredReticleSuppressionResources())
        {
            prepared=true;
            LOG("CE crosshair authored capture prepared: native on-foot and verified seated overlays on controller aim; vehicle capture independent of first-person model preparation");
        }
        else
        {
            rejectedCaptureGeneration=gen;
            LOG("CE crosshair native-art fallback: optional suppression resources unavailable; native scope and gameplay HUD retained");
        }
    }
    else if (result==AuthoredReticlePreparationResult::Failed)
    {
        rejectedCaptureGeneration=gen;
        LOG("CE crosshair native-art fallback: optional capture resources unavailable; native scope and gameplay HUD retained");
    }
}
}
bool HaloCEHud_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive) noexcept
{
    active.store(isActive,std::memory_order_release);
    if (retained&&(!isActive||base!=moduleBase||gen!=generation.load()||retiring.load()))
        if (!Remove()) return false;
    if (!isActive||!base||!gen) return false;
    if (!installed.load()&&gen!=rejectedGeneration&&HaloCE_Armed())
    {
        if (!Install(base,size,gen))
        { rejectedGeneration=gen; LOG("CE crosshair stock fallback: optional native scope installation failed; camera retained"); }
    }
    if (Current()) PrepareCapture(base,size,gen);
    const uint64_t now=GetTickCount64();
    if (installed.load()&&now-lastReport>=2000)
    {
        lastReport=now;
        LOG("CE HUD gen=%u nativeScope=1 capturePrepared=%d crosshairCaptures=%llu stockFallbacks=%llu contextRefusals=%llu vehicleCaptures=%llu driverChevron=%d chevronCaptures=%llu",
            gen,prepared.load()?1:0,captures.load(),fallbacks.load(),contextRefusals.load(),vehicleCaptures.load(),
            seatHookEnabled?1:0,seatCaptures.load());
    }
    return Current();
}
bool HaloCEHud_HasCrosshairScope() noexcept { return Current(); }
bool HaloCEHud_CapturedCrosshair() noexcept
{ return HaloCEHud_CrosshairKey()!=0; }
bool HaloCEHud_ReadTarget(ID3D11DeviceContext* context,CeHudTargetSnapshot& snapshot) noexcept
{ return Current()&&prepared.load()&&HaloCEHudTarget_Read(moduleBase,context,snapshot); }
uint64_t HaloCEHud_CrosshairKey() noexcept
{
    CaptureReceipt receipt{};
    const uint64_t now=GetTickCount64();
    if (!Current()||!lastCapture.Read(receipt)||!receipt.capturedAtMs||
        now<receipt.capturedAtMs||now-receipt.capturedAtMs>=250||
        !HaloCE_RenderContextCurrent(receipt.context)) return 0;
    return receipt.key;
}
