#include "haloce_unit_control.h"
#include "haloce_controls.h"
#include "haloce_first_person.h"
#include "haloce_network_input.h"
#include "haloce_stereo_core.h"
#include "haloce_native_bindings.h"
#include "hook_quiescence.h"
#include "sight_layer.h"
#include "title_adapter.h"
#include "../common/haloce_controls_logic.h"
#include "../common/haloce_unit_control_logic.h"
#include "../common/minhook_lifecycle.h"
#include "../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>

namespace
{
using namespace halo_ce;
using UnitControlFn=void(__fastcall*)(uint32_t,const UnitControlPacket*,int32_t);
using MovementFn=void(__fastcall*)(void*);
using ObjectGetFn=uintptr_t(__fastcall*)(uint32_t,uint32_t);
void* target{};
void* original{};
void* movementTarget{};
void* movementOriginal{};
HMODULE retained{};
uintptr_t moduleBase{};
bool enabled{};
bool movementEnabled{};
std::atomic<bool> active{},retiring{},ready{},vehicleReady{};
std::atomic<uint32_t> generation{},callbacks{};
uint32_t rejectedGeneration{};
uintptr_t rejectedBase{};
enum class PendingCleanup { None,Disable,Quiescence,Remove,Callbacks };
PendingCleanup pendingCleanup{};
bool pinFailureReported{};
std::atomic<uint64_t> bodies{},aims{},stock{},declined{},exceptions{};
std::atomic<uint64_t> movements{},movementStock{},movementDeclined{};
std::atomic<uint64_t> vehicles{},vehicleDeclined{};
uint64_t lastReport{};

bool Current() noexcept
{
    return active.load(std::memory_order_acquire)&&!retiring.load(std::memory_order_acquire)&&
        TitleAdapter_GetActiveTitle()==GameTitle::HaloCE&&
        TitleAdapter_GetGeneration(GameTitle::HaloCE)==generation.load(std::memory_order_acquire);
}
bool Owner(uint32_t unit,HaloCELocalPlayerState& state,RenderContext& context) noexcept
{
    return Current()&&HaloCEControls_GetLocomotionFrame(state,context)&&
        unit!=0xffffffffu&&unit==state.unit&&state.player!=0xffffffffu&&
        state.generation==generation.load(std::memory_order_acquire)&&
        state.generation==context.tracking.generation&&
        OnFootControls({state.hasControlledUnit,state.onFoot,state.nativePerspective==0,
            state.nativeInputBlocked,state.nativeLookBlocked,state.nativePaused,
            state.nativeCinematicFlag,context.tracking.controllers.controlsPresentationBlocked})&&
        Current()&&HaloCE_RenderContextCurrent(context);
}
// Trace-and-reaim (sight_layer.h): the aiming direction through the point the eye-anchored intent ray
// hits, as seen from CE's engine eye -- the record CE replicates and integrates into the unit's aim, so the
// engine's own crosshair trace lands where the controller points. nullptr without a fresh trace result:
// the controller's own forward is written then.
std::atomic<uint64_t> convergedPackets{};
const Vec3* ConvergedAim(const RenderContext& context,Vec3& storage) noexcept
{
    Vec3 head{},dir{},eye{};
    if (!BuildSightGeometry(context.camera,context.tracking,context.reference,
        context.tracking.controllers.primaryAim,context.unitsPerMeter,context.positional,head,dir,eye))
        return nullptr;
    const float h[3]{head.x,head.y,head.z},d[3]{dir.x,dir.y,dir.z},e[3]{eye.x,eye.y,eye.z};
    float bent[3]{};
    if (!Sight_ConvergedDirection(GameTitle::HaloCE,TitleAdapter_GetGeneration(GameTitle::HaloCE),h,d,e,bent))
        return nullptr;
    storage={bent[0],bent[1],bent[2]};
    return &storage;
}
bool CopyPacket(const UnitControlPacket* source,UnitControlPacket& output) noexcept
{
    if (!source) return false;
    __try { output=*source;return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool VehicleOwner(uint32_t unit,HaloCELocalPlayerState& state,RenderContext& context,
    int16_t& seat) noexcept
{
    if (!Current()||!vehicleReady.load(std::memory_order_acquire)||
        !HaloCEControls_GetLocalPlayerState(state)||!HaloCE_GetGameplayContext(context)||
        unit==0xffffffffu||unit!=state.unit||state.player==0xffffffffu||
        !state.hasControlledUnit||state.onFoot||state.parent==0xffffffffu||!(state.parent>>16)||
        (state.nativePerspective!=0&&state.nativePerspective!=1)||state.inputUser<0||state.inputUser>=4||
        state.nativeInputBlocked||state.nativeLookBlocked||state.nativePaused||state.nativeCinematicFlag||
        !context.tracking.controllers.padValid||context.tracking.controllers.controlsPresentationBlocked||
        state.generation!=generation.load(std::memory_order_acquire)||
        state.generation!=context.tracking.generation) return false;
    // E-CE-VEHICLE-CONTROL-1: a following camera alone does not prove a vehicle.
    // Verify the seated local biped's direct parent with CE's native vehicle
    // object lookup. The seat is an identity only; no tag array is indexed.
    __try
    {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const uintptr_t occupant=get(unit,1);
        if (!occupant||*reinterpret_cast<const uint32_t*>(occupant+0xd8)!=state.parent||
            !get(state.parent,2)) return false;
        seat=*reinterpret_cast<const int16_t*>(occupant+0x2d0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    return seat>=0&&Current()&&vehicleReady.load(std::memory_order_acquire)&&
        HaloCE_RenderContextCurrent(context);
}
// SEAT CONVERGENCE (seat_converge_logic.h; docs/H2-CE-SEAT-CONVERGENCE-2026-10-06.md). In a seat CE aims the shot
// in two steps: the fire takes the barrel (a vehicle gun with a gunner) or the unit's aim (a passenger's own
// weapon), then player_aim_projectile (0xB67B00) traces its crosshair from the DETERMINISTIC camera of the
// player's biped -- the seat's camera position along the biped's aiming vector -- and turns the shot at what that
// hits, pinned inside the weapon's deviation cone about the direction it came in with (only in a seat: 0xB69BF8).
// The biped's aiming vector follows the desired aim this packet carries (+0x28 -> unit +0x210 -> +0x21C), so the
// packet's aim is bent to run from that camera position through the point the hand's ray hits: the engine's own
// crosshair test then finds the point and puts the barrel's shot on it. Facing (+0x1C: what steers a driver) and
// looking (+0x34) keep the controller's own forward.
std::atomic<uint64_t> seatConvergedPackets{};
#if HALOMCCVR_DEV_DIAGNOSTICS
// Dev: how often the converged aim lay past the VEHICLE's own aiming stops (unit +0x296 the euler-screen flag,
// +0x298 the yaw / pitch rectangle in the unit's frame: retail 0xAFCEB7..0xAFCFB7 hands them to the aiming update;
// the frame is the root vehicle's forward +0x30 / up +0x3C). Counted only: the engine clamps the gun itself.
std::atomic<uint64_t> seatGunPastStops{},seatGunBoundsRead{};
std::atomic<float> seatGunMaxOverDeg{};
void NoteSeatGunStops(uint32_t rootVehicle,const float aim[3]) noexcept
{
    __try
    {
        const uintptr_t vehicle=reinterpret_cast<ObjectGetFn>(
            moduleBase+contract::player_state::state_object_try_get)(rootVehicle,2);
        if (!vehicle||!*reinterpret_cast<const uint8_t*>(vehicle+0x296)) return;
        seat_converge::Bounds bounds{};
        std::memcpy(&bounds,reinterpret_cast<const void*>(vehicle+0x298),sizeof(bounds));
        // real_rectangle2d: x0, x1 (yaw), y0, y1 (pitch) -- the struct's own order.
        const Vec3 forward=*reinterpret_cast<const Vec3*>(vehicle+0x30),up=*reinterpret_cast<const Vec3*>(vehicle+0x3c);
        const float f[3]{forward.x,forward.y,forward.z},u[3]{up.x,up.y,up.z};
        float held[3]{},over=0;
        if (!bounds.usable()) return;
        seatGunBoundsRead.fetch_add(1,std::memory_order_relaxed);
        if (!seat_converge::ClampToBounds(bounds,f,u,aim,held,over)) return;
        seatGunPastStops.fetch_add(1,std::memory_order_relaxed);
        float prior=seatGunMaxOverDeg.load(std::memory_order_relaxed);
        while (over>prior&&!seatGunMaxOverDeg.compare_exchange_weak(prior,over,std::memory_order_relaxed)) {}
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
#endif
// `carrierDetached`: the caller's own carrier decision for this invocation (the packet path, whose two relationship
// reads bracket the transaction and must stay two), or null to evaluate it here (the trace, the fire, the network
// action: the same policy on their own read).
bool SeatSightBody(uint32_t unit,HaloCESeatSight& out,const bool* carrierDetached=nullptr) noexcept
{
    HaloCELocalPlayerState state{};RenderContext context{};int16_t seat=-1;
    if (!VehicleOwner(unit,state,context,seat)) return false;
    // The carrier the seated packet consumes (the persistent support grip's current-owner policy).
    const bool detach=carrierDetached?*carrierDetached:CeEvaluateSupportInvocation(
        VR_SupportGripWiredForTitle(GameTitle::HaloCE),state,
        context.tracking.controllers.primaryAimSupportDerived).detach;
    if (detach&&!context.tracking.controllers.independentPrimaryAim.valid) return false;
    const ControllerPose& carrier=detach?context.tracking.controllers.independentPrimaryAim:
        context.tracking.controllers.primaryAim;
    Vec3 head{},dir{},renderedEye{};
    if (!BuildSightGeometry(context.camera,context.tracking,context.reference,carrier,
        context.unitsPerMeter,context.positional,head,dir,renderedEye)) return false;
    HaloCESeatSight sight{};
    sight.generation=state.generation;sight.unit=unit;sight.parent=state.parent;sight.seat=seat;
    sight.key=seat_converge::StanceKey(state.parent,seat);
    sight.head[0]=head.x;sight.head[1]=head.y;sight.head[2]=head.z;
    sight.dir[0]=dir.x;sight.dir[1]=dir.y;sight.dir[2]=dir.z;
    sight.unitsPerMeter=context.unitsPerMeter;
    __try
    {
        const auto get=reinterpret_cast<ObjectGetFn>(moduleBase+contract::player_state::state_object_try_get);
        const uintptr_t parent=get(state.parent,2);
        if (!parent) return false;
        // E-CE-VEHICLE-CONTROL-1: the vehicle takes facing and throttle from the occupant at +0x304 and its aim
        // from the occupant at +0x308 (the trigger reads the same handle for the firing player, 0xB7A661).
        sight.role=seat_converge::RoleFromOccupants(true,unit,
            *reinterpret_cast<const uint32_t*>(parent+0x304),*reinterpret_cast<const uint32_t*>(parent+0x308));
        // The ultimate parent, as CE's own crosshair test finds it (0xB38BA8: the parent handle at +0xD8, up to
        // the root). Bounded: a chain that does not end is not a hierarchy to ignore.
        uint32_t root=state.parent;
        bool ended=false;
        for (int depth=0;depth<8&&!ended;++depth)
        {
            const uintptr_t object=get(root,UINT32_MAX);
            if (!object) return false;
            const uint32_t above=*reinterpret_cast<const uint32_t*>(object+0xd8);
            if (above==UINT32_MAX) ended=true;
            else root=above;
        }
        if (!ended) return false;
        sight.root=root;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    { exceptions.fetch_add(1,std::memory_order_relaxed);return false; }
    if (!Current()||!HaloCE_RenderContextCurrent(context)) return false;
    out=sight;
    return true;
}
bool SeatAimBody(uint32_t unit,HaloCESeatAim& out,const bool* carrierDetached=nullptr) noexcept
{
    using seat_converge::Why;
    out=HaloCESeatAim{};
    if (!SeatSightBody(unit,out.sight,carrierDetached)) { out.why=Why::NoHand;return false; }
    if (!Sight_AimEnabled()) { out.why=Why::AimOff;return false; }
    if (!out.sight.role.known) { out.why=Why::NotIdentified;return false; }
    // The seat's engine eye: CE's own deterministic camera for this biped, asked in this same update.
    seat_converge::EyeAnswer answer{};
    const int asked=HaloCEFirstPerson_SeatEye(unit,answer.position,answer.perspective);
    answer.bound=asked!=0;answer.called=asked>0;
    out.why=seat_converge::SeatEye(answer,out.eye);
    if (out.why!=Why::Converged) return false;
    // Only a trace of THIS seat's ray (the biped tick sets the stance before it traces).
    if (Sight_Stance(GameTitle::HaloCE)!=out.sight.key) { out.why=Why::StanceSettling;return false; }
    float range=0;converge::ConvergeOff off=converge::ConvergeOff::None;
    if (!Sight_ConvergedDirection(GameTitle::HaloCE,TitleAdapter_GetGeneration(GameTitle::HaloCE),
        out.sight.head,out.sight.dir,out.eye,out.aim,90.0f,&range,nullptr,&off))
    { out.why=off==converge::ConvergeOff::NoRange?Why::NoRange:Why::NoTrace;return false; }
    for (int axis=0;axis<3;++axis) out.point[axis]=out.sight.head[axis]+out.sight.dir[axis]*range;
    const float length=out.aim[0]*out.aim[0]+out.aim[1]*out.aim[1]+out.aim[2]*out.aim[2];
    if (!std::isfinite(length)||std::fabs(length-1)>0.01f||!std::isfinite(range)||!(range>0))
    { out.why=Why::NoRange;return false; }
    out.why=Why::Converged;
    return true;
}
void UnitControlBody(uint32_t unit,const UnitControlPacket* source,int32_t clientUpdate,uintptr_t caller)
{
    const auto native=reinterpret_cast<UnitControlFn>(original);
    if (!native) return;
    // Network prediction and authority must consume the same outgoing action.
    // Never replace a consumed network packet with a fresh local XR sample.
    if (HaloCENetworkInput_UsesNativeSimulation())
    { stock.fetch_add(1,std::memory_order_relaxed);native(unit,source,clientUpdate);return; }
    UnitControlPacket packet{},candidate{};
    HaloCELocalPlayerState state{},latest{};RenderContext context{},latestContext{};
    bool aim{};
    Vec3 convergedStorage{};const Vec3* converged=nullptr;
    if (caller==moduleBase+0xad0d5b&&ready.load(std::memory_order_acquire)&&
        Owner(unit,state,context))
    {
        // Persistent support grip: primaryAim may carry support steering for a
        // different weapon incarnation. This packet may consume it only while
        // this invocation's current validated owner proves the durable
        // relationship owner and the frozen pose's support provenance is
        // continuous with the relationship across the two reads. A failed
        // relationship read, a released relationship whose frozen publication
        // still carries support-derived geometry, an owner change or a
        // relationship epoch change across the two reads fails closed to the
        // stock packet. With the feature off the decision is the base one.
        const bool supportWired=VR_SupportGripWiredForTitle(GameTitle::HaloCE);
        const CeSupportInvocation before=CeEvaluateSupportInvocation(supportWired,
            state,context.tracking.controllers.primaryAimSupportDerived);
        RenderContext effective=context;
        const bool carrierAvailable=!before.detach||
            context.tracking.controllers.independentPrimaryAim.valid;
        if (before.detach&&carrierAvailable)
            effective.tracking.controllers.primaryAim=
                context.tracking.controllers.independentPrimaryAim;
        // Trace-and-reaim bends the carrier this packet actually consumes (`effective`):
        // the converged override wins over the controller forward in BuildTrackedUnitControl,
        // so bending the undetached primaryAim would write a detached support carrier back in.
        if (carrierAvailable&&CopyPacket(source,packet)&&
            ((converged=ConvergedAim(effective,convergedStorage)),true)&&
            BuildTrackedUnitControl(effective,packet,candidate,aim,converged)&&
            Owner(unit,latest,latestContext))
        {
            const CeSupportInvocation after=CeEvaluateSupportInvocation(supportWired,
                latest,latestContext.tracking.controllers.primaryAimSupportDerived);
            const bool usedSupport=supportWired&&!before.detach&&
                context.tracking.controllers.primaryAimSupportDerived;
            const bool supportContinuityOk=!usedSupport||
                (!after.detach&&after.readable&&before.readable&&
                 after.engaged&&before.engaged&&
                 after.epoch==before.epoch&&
                 after.generation==before.generation&&
                 after.unit==before.unit&&after.weapon==before.weapon);
            const bool decisionStable=before.detach==after.detach;
            if (state.player==latest.player&&state.inputUser==latest.inputUser&&
                state.unit==latest.unit&&supportContinuityOk&&decisionStable&&
                context.referenceRevision==latestContext.referenceRevision&&
                context.rendererEpoch==latestContext.rendererEpoch&&
                context.tracking.spaceEpoch==latestContext.tracking.spaceEpoch&&
                ready.load(std::memory_order_acquire)&&HaloCE_RenderContextCurrent(context))
            {
                // The original packet and native camera/input-angle state remain intact.
                // The engine owns validation, native interpolation and grenade release.
                native(unit,&candidate,clientUpdate);
                bodies.fetch_add(1,std::memory_order_relaxed);
                if (aim) aims.fetch_add(1,std::memory_order_relaxed);
                else declined.fetch_add(1,std::memory_order_relaxed);
                // The aim now in effect is the converged one: the compositor draws the traced reticle.
                if (aim&&converged)
                {
                    convergedPackets.fetch_add(1,std::memory_order_relaxed);
                    Sight_MarkConverged(GameTitle::HaloCE,context.unitsPerMeter);
                    Sight_MarkAimWritten(GameTitle::HaloCE);   // the packet the native just consumed carries it
                }
                return;
            }
        }
    }
    int16_t seat=-1,latestSeat=-1;
    if (caller==moduleBase+0xad0d5b&&ready.load(std::memory_order_acquire)&&
        VehicleOwner(unit,state,context,seat))
    {
        // Persistent support grip: BuildTrackedVehicleControl injects the same
        // primaryAim carrier, so the seat path applies the identical
        // current-owner policy: a failed read, a release discontinuity or an
        // owner/epoch change between the two reads fails closed to stock.
        const bool supportWired=VR_SupportGripWiredForTitle(GameTitle::HaloCE);
        const CeSupportInvocation before=CeEvaluateSupportInvocation(supportWired,
            state,context.tracking.controllers.primaryAimSupportDerived);
        RenderContext effective=context;
        const bool carrierAvailable=!before.detach||
            context.tracking.controllers.independentPrimaryAim.valid;
        if (before.detach&&carrierAvailable)
            effective.tracking.controllers.primaryAim=
                context.tracking.controllers.independentPrimaryAim;
        // Trace-and-reaim in the seat: the aim this packet carries, bent to run from CE's own seat camera
        // through the point the hand's ray hits (SeatAimBody). Solved before the packet is built so its own
        // ownership reads sit inside this invocation's two; a refusal leaves the controller's forward below.
        HaloCESeatAim seatAim{};
        const bool seatSolved=carrierAvailable&&SeatAimBody(unit,seatAim,&before.detach);
        if (carrierAvailable&&CopyPacket(source,packet)&&
            BuildTrackedVehicleControl(effective,packet,candidate)&&
            VehicleOwner(unit,latest,latestContext,latestSeat))
        {
            const CeSupportInvocation after=CeEvaluateSupportInvocation(supportWired,
                latest,latestContext.tracking.controllers.primaryAimSupportDerived);
            const bool usedSupport=supportWired&&!before.detach&&
                context.tracking.controllers.primaryAimSupportDerived;
            const bool supportContinuityOk=!usedSupport||
                (!after.detach&&after.readable&&before.readable&&
                 after.engaged&&before.engaged&&
                 after.epoch==before.epoch&&
                 after.generation==before.generation&&
                 after.unit==before.unit&&after.weapon==before.weapon);
            const bool decisionStable=before.detach==after.detach;
            if (state.player==latest.player&&state.inputUser==latest.inputUser&&state.unit==latest.unit&&
                state.parent==latest.parent&&seat==latestSeat&&
                state.nativePerspective==latest.nativePerspective&&
                supportContinuityOk&&decisionStable&&
                context.referenceRevision==latestContext.referenceRevision&&
                context.rendererEpoch==latestContext.rendererEpoch&&
                context.tracking.spaceEpoch==latestContext.tracking.spaceEpoch&&
                ready.load(std::memory_order_acquire)&&vehicleReady.load(std::memory_order_acquire)&&
                HaloCE_RenderContextCurrent(context))
            {
                // The converged aim goes into desired aiming only (+0x28): the vehicle takes its gunner's
                // aim from there and the biped's own aiming vector -- the line CE's crosshair test runs
                // along -- follows it. Only for the very seat this invocation proved twice.
                seat_converge::Why seatWhy=seatAim.why;
                bool seatWritten=false;
                if (seatSolved&&seatAim.sight.parent==state.parent&&seatAim.sight.seat==seat&&
                    seatAim.sight.generation==state.generation)
                {
                    const Vec3 bent{seatAim.aim[0],seatAim.aim[1],seatAim.aim[2]};
                    if (Finite(bent)&&std::fabs(Dot(bent,bent)-1)<=0.01f)
                    { WriteUnitControl(candidate,0x28,bent);seatWritten=true; }
                    else seatWhy=seat_converge::Why::NoRange;
                }
                else if (seatSolved) seatWhy=seat_converge::Why::StanceSettling;
                // Native driver/gunner forwarding selects which occupant controls
                // the parent. Physics, seat limits, throttle and actions stay native.
                native(unit,&candidate,clientUpdate);
                vehicles.fetch_add(1,std::memory_order_relaxed);
                if (seatWritten)
                {
                    // The aim now in effect is the converged one: the compositor draws the traced reticle.
                    seatConvergedPackets.fetch_add(1,std::memory_order_relaxed);
                    Sight_MarkConverged(GameTitle::HaloCE,context.unitsPerMeter);
                    Sight_MarkAimWritten(GameTitle::HaloCE);   // the packet the native just consumed carries it
#if HALOMCCVR_DEV_DIAGNOSTICS
                    if (seatAim.sight.role.gunner&&seatAim.sight.root==state.parent)
                        NoteSeatGunStops(state.parent,seatAim.aim);
#endif
                }
                Sight_NoteSeatPoll(GameTitle::HaloCE,seat_converge::StanceKey(state.parent,seat),
                    seatAim.sight.role,seatWritten?seat_converge::Why::Converged:seatWhy);
                return;
            }
        }
        vehicleDeclined.fetch_add(1,std::memory_order_relaxed);
    }
    stock.fetch_add(1,std::memory_order_relaxed);
    native(unit,source,clientUpdate);
}
__declspec(noinline) void __fastcall UnitControlHook(uint32_t unit,const UnitControlPacket* packet,int32_t clientUpdate)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    // A unit's control runs once per simulation tick: the measured tick a flat-parity muzzle shot carries the eye by.
    Sight_NoteTitleTick(GameTitle::HaloCE);
    __try { UnitControlBody(unit,packet,clientUpdate,caller); }
    __finally
    {
        if (AbnormalTermination()) exceptions.fetch_add(1,std::memory_order_relaxed);
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
bool ReadMovement(void* data,uint32_t& unit,UnitMovementBasis& basis) noexcept
{
    if (!data) return false;
    __try
    {
        std::memcpy(&unit,data,sizeof(unit));
        std::memcpy(&basis,static_cast<uint8_t*>(data)+0x14,sizeof(basis));
        return Finite(basis.forward)&&Finite(basis.aim);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteMovement(void* data,const UnitMovementBasis& basis) noexcept
{
    if (!data) return false;
    __try { std::memcpy(static_cast<uint8_t*>(data)+0x14,&basis,sizeof(basis));return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void MovementBody(void* data,uintptr_t caller)
{
    const auto native=reinterpret_cast<MovementFn>(movementOriginal);
    if (!native) return;
    if (HaloCENetworkInput_UsesNativeSimulation())
    { movementStock.fetch_add(1,std::memory_order_relaxed);native(data);return; }
    uint32_t unit{};UnitMovementBasis before{},candidate{};
    HaloCELocalPlayerState state{},latest{};RenderContext context{},latestContext{};
    if ((caller!=moduleBase+contract::unit_control::movement_consumer_return&&
         caller!=moduleBase+contract::unit_control::movement_consumer_return_secondary)||
        !ready.load(std::memory_order_acquire)||!ReadMovement(data,unit,before)||
        !Owner(unit,state,context)||!BuildUnitMovementBasis(context,candidate)||
        !Owner(unit,latest,latestContext)||state.player!=latest.player||
        state.inputUser!=latest.inputUser||state.unit!=latest.unit||
        context.referenceRevision!=latestContext.referenceRevision||
        context.rendererEpoch!=latestContext.rendererEpoch||
        context.tracking.spaceEpoch!=latestContext.tracking.spaceEpoch||
        !ready.load(std::memory_order_acquire)||!HaloCE_RenderContextCurrent(context))
    { movementStock.fetch_add(1,std::memory_order_relaxed);native(data);return; }
    if (!WriteMovement(data,candidate))
    {
        (void)WriteMovement(data,before);
        movementDeclined.fetch_add(1,std::memory_order_relaxed);native(data);return;
    }
    // Only private movement-input vectors change. Native collision, slope,
    // acceleration, output velocity and the live unit's aiming remain native.
    __try { native(data);movements.fetch_add(1,std::memory_order_relaxed); }
    __finally
    {
        if (!WriteMovement(data,before)) movementDeclined.fetch_add(1,std::memory_order_relaxed);
    }
}
__declspec(noinline) void __fastcall MovementHook(void* data)
{
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    __try { MovementBody(data,caller); }
    __finally
    {
        if (AbnormalTermination()) exceptions.fetch_add(1,std::memory_order_relaxed);
        callbacks.fetch_sub(1,std::memory_order_release);
    }
}
bool Remove() noexcept
{
    active=false;retiring=true;ready=false;vehicleReady=false;
    const auto pending=[](PendingCleanup stage,const char* reason)
    {
        if (pendingCleanup!=stage)
            LOG("CE body/controller grenade aim cleanup pending: %s; optional feature stock, module/trampoline retained",reason);
        pendingCleanup=stage;
        return false;
    };
    if (enabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(target);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return pending(PendingCleanup::Disable,"hook disable failed");
        enabled=false;
    }
    if (movementEnabled)
    {
        const auto result=MCCVR_DisableHookForRetirement(movementTarget);
        if (result!=MH_OK&&result!=MH_ERROR_DISABLED) return pending(PendingCleanup::Disable,"movement hook disable failed");
        movementEnabled=false;
    }
    const void* functions[]{reinterpret_cast<const void*>(&UnitControlHook),reinterpret_cast<const void*>(&MovementHook)};
    const void* originals[]{original,movementOriginal};
    if ((target||movementTarget)&&!WaitForNativeDetourQuiescence(functions,originals,2,callbacks))
        return pending(PendingCleanup::Quiescence,"callback quiescence not proved");
    if (target)
    {
        const auto result=MH_RemoveHook(target);
        if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return pending(PendingCleanup::Remove,"hook removal failed");
        target=original=nullptr;
    }
    if (movementTarget)
    {
        const auto result=MH_RemoveHook(movementTarget);
        if (result!=MH_OK&&result!=MH_ERROR_NOT_CREATED) return pending(PendingCleanup::Remove,"movement hook removal failed");
        movementTarget=movementOriginal=nullptr;
    }
    if (callbacks.load(std::memory_order_acquire)) return pending(PendingCleanup::Callbacks,"callback still active");
    target=original=movementTarget=movementOriginal=nullptr;
    if (retained) { FreeLibrary(retained);retained=nullptr; }
    moduleBase=0;generation=0;retiring=false;pendingCleanup=PendingCleanup::None;return true;
}
bool Install(size_t size) noexcept
{
    const char* failure{};
    const NativeContractSet contracts{contract::unit_control::entries,contract::unit_control::witnesses,
        contract::unit_control::relatives,contract::unit_control::pointers};
    if (!VerifyNativeFeatureBindings(moduleBase,size,generation.load(),contracts,failure))
    { LOG("CE body/controller grenade aim stock fallback: %s",failure?failure:"binding failure");return false; }
    // Check the independent vehicle evidence before any detour can alter a
    // native entry signature. Failure affects only this new optional path.
    const NativeContractSet vehicleContracts{contract::vehicle::entries,contract::vehicle::witnesses,
        contract::vehicle::relatives,contract::vehicle::pointers};
    const bool vehicleVerified=VerifyNativeFeatureBindings(moduleBase,size,generation.load(),vehicleContracts,failure);
    if (!vehicleVerified)
        LOG("CE vehicle controls stock fallback: %s; on-foot controls and stereo remain active",failure?failure:"binding failure");
    void* location=reinterpret_cast<void*>(moduleBase+contract::unit_control::unit_control_set);
    auto result=MH_CreateHook(location,reinterpret_cast<void*>(&UnitControlHook),&original);
    if (result!=MH_OK)
    { LOG("CE body/controller grenade aim stock fallback: create status %d",result);return false; }
    target=location;
    location=reinterpret_cast<void*>(moduleBase+contract::unit_control::movement_consumer);
    result=MH_CreateHook(location,reinterpret_cast<void*>(&MovementHook),&movementOriginal);
    if (result!=MH_OK)
    { LOG("CE body/controller grenade aim stock fallback: movement create status %d",result);return false; }
    movementTarget=location;
    result=MH_EnableHook(target);
    if (result!=MH_OK)
    { LOG("CE body/controller grenade aim stock fallback: enable status %d",result);return false; }
    enabled=true;
    result=MH_EnableHook(movementTarget);
    if (result!=MH_OK)
    { LOG("CE body/controller grenade aim stock fallback: movement enable status %d",result);return false; }
    movementEnabled=true;ready=true;
    vehicleReady=vehicleVerified;
    if (vehicleReady.load())
        LOG("CE vehicle controls installed: tracked aiming-hand facing/aim/looking in local seated native packets; native following camera, driver/gunner forwarding and throttle retained; Original and Anniversary");
    LOG("CE unit control installed: local on-foot head facing/looking, controller aiming; private native-camera movement basis, native grenade release retained");
    return true;
}
}

bool HaloCEUnitControl_Poll(uintptr_t base,size_t size,uint32_t gen,bool isActive) noexcept
{
    if (retained&&(!isActive||base!=moduleBase||gen!=generation.load()||retiring.load()))
        if (!Remove()) return false;
    active.store(isActive,std::memory_order_release);
    if (!isActive||!base||!gen) return false;
    if (rejectedBase==base&&rejectedGeneration==gen) return false;
    if (!retained)
    {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(base),&retained))
        {
            if (!pinFailureReported)
                LOG("CE body/controller grenade aim stock fallback: module retention failed; retry pending");
            pinFailureReported=true;
            return false;
        }
        pinFailureReported=false;
        moduleBase=base;generation=gen;retiring=false;
    }
    if (!ready.load()&&!Install(size))
    {
        rejectedBase=base;rejectedGeneration=gen;
        // A partial optional hook owns its trampoline and module until removal
        // is proved. Retry cleanup on later polls without retrying this binding.
        (void)Remove();
        return false;
    }
    const uint64_t now=GetTickCount64();
    if (now-lastReport>=2000)
    {
        lastReport=now;
        LOG("CE unit control gen=%u ready=%d bodies=%llu aims=%llu (converged on the traced point %llu) stock=%llu aimDeclined=%llu movement=%llu movementStock=%llu movementDeclined=%llu exceptions=%llu vehicleReady=%d vehicles=%llu (seat aim converged on the traced point %llu) vehicleDeclined=%llu",
            gen,ready.load(),bodies.load(),aims.load(),convergedPackets.load(),stock.load(),declined.load(),movements.load(),movementStock.load(),movementDeclined.load(),exceptions.load(),vehicleReady.load(),vehicles.load(),seatConvergedPackets.load(),vehicleDeclined.load());
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (const uint64_t read=seatGunBoundsRead.exchange(0,std::memory_order_relaxed))
            LOG("CE seat gun stops (dev, natural -- nothing switches on it): the vehicle's own aiming rectangle read on %llu converged gunner updates; the converged aim lay past it on %llu (max %.1f deg): there the engine holds the gun at its stop and the shot inside its cone",
                read,seatGunPastStops.exchange(0,std::memory_order_relaxed),
                double(seatGunMaxOverDeg.exchange(0.0f,std::memory_order_relaxed)));
#endif
    }
    return Current()&&ready.load();
}

bool HaloCEUnitControl_VehicleAimCurrent(const HaloCELocalPlayerState& player,
    const halo_ce::RenderContext& context) noexcept
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    bool admitted=false;
    __try
    {
        HaloCELocalPlayerState current{},latest{};RenderContext frame{},latestFrame{};
        int16_t seat=-1,latestSeat=-1;
        admitted=ready.load(std::memory_order_acquire)&&
            VehicleOwner(player.unit,current,frame,seat)&&
            VehicleOwner(player.unit,latest,latestFrame,latestSeat)&&
            player.generation==current.generation&&player.player==current.player&&
            player.inputUser==current.inputUser&&player.parent==current.parent&&
            !player.onFoot&&(player.nativePerspective==0||player.nativePerspective==1)&&
            current.player==latest.player&&current.inputUser==latest.inputUser&&
            current.parent==latest.parent&&seat==latestSeat&&
            player.nativePerspective==current.nativePerspective&&
            current.nativePerspective==latest.nativePerspective&&
            context.tracking.generation==frame.tracking.generation&&
            context.referenceRevision==frame.referenceRevision&&
            context.rendererEpoch==frame.rendererEpoch&&
            context.tracking.spaceEpoch==frame.tracking.spaceEpoch&&
            frame.referenceRevision==latestFrame.referenceRevision&&
            frame.rendererEpoch==latestFrame.rendererEpoch&&
            frame.tracking.spaceEpoch==latestFrame.tracking.spaceEpoch&&
            frame.tracking.controllers.primaryAim.valid&&
            latestFrame.tracking.controllers.primaryAim.valid&&
            ready.load(std::memory_order_acquire)&&Current()&&
            HaloCE_RenderContextCurrent(context);
    }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
    return admitted;
}

bool HaloCEUnitControl_SeatSight(uint32_t unit,HaloCESeatSight& out) noexcept
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    bool admitted=false;
    __try { admitted=ready.load(std::memory_order_acquire)&&SeatSightBody(unit,out); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
    return admitted;
}

bool HaloCEUnitControl_SeatAim(uint32_t unit,HaloCESeatAim& out) noexcept
{
    callbacks.fetch_add(1,std::memory_order_acq_rel);
    bool solved=false;
    __try { solved=ready.load(std::memory_order_acquire)&&SeatAimBody(unit,out); }
    __finally { callbacks.fetch_sub(1,std::memory_order_release); }
    return solved;
}

void HaloCEUnitControl_SeatAimApplied(const HaloCESeatAim& aim,uint32_t parent,int16_t seat,bool converged) noexcept
{
    if (converged)
    {
        seatConvergedPackets.fetch_add(1,std::memory_order_relaxed);
        Sight_MarkConverged(GameTitle::HaloCE,aim.sight.unitsPerMeter);
        Sight_MarkAimWritten(GameTitle::HaloCE);   // the action the simulation consumes carries it
    }
    Sight_NoteSeatPoll(GameTitle::HaloCE,seat_converge::StanceKey(parent,seat),aim.sight.role,
        converged?seat_converge::Why::Converged:
        aim.why==seat_converge::Why::Converged?seat_converge::Why::NotWritten:aim.why);
}
