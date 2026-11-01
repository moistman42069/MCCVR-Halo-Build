#pragma once
#include "haloce_frame_context.h"
#include "haloce_first_person_logic.h"
#include <array>
#include <cstring>

namespace halo_ce
{
// E-CE-UNIT-CONTROL-1: HCEEK control_data is 0x50 bytes. Preserve native
// actions, weapon/grenade choices, speed and aim-assist data byte for byte.
using UnitControlPacket=std::array<uint8_t,0x50>;
template<class T> inline T ReadUnitControl(const UnitControlPacket& packet,size_t offset) noexcept
{ T value{};std::memcpy(&value,packet.data()+offset,sizeof(value));return value; }
template<class T> inline void WriteUnitControl(UnitControlPacket& packet,size_t offset,const T& value) noexcept
{ std::memcpy(packet.data()+offset,&value,sizeof(value)); }

// `aimOverride` (optional): the aiming direction to write instead of the controller's own forward --
// trace-and-reaim's converged aim (src/dll/sight_layer.h), which passes through the point the eye-anchored
// intent ray hits as seen from CE's engine eye. It is written only if finite and unit length.
inline bool BuildTrackedUnitControl(const RenderContext& context,const UnitControlPacket& source,
    UnitControlPacket& output,bool& controllerAim,const Vec3* aimOverride=nullptr) noexcept
{
    controllerAim=false;
    if (!context.tracking.serial||!context.tracking.generation||!context.referenceRevision||
        context.tracking.generation!=context.reference.generation||
        context.tracking.spaceEpoch!=context.reference.spaceEpoch||
        !context.tracking.spaceEpoch||!context.rendererEpoch||
        !Valid(context.tracking.headOrientation)||!Valid(context.reference.orientation)||
        context.tracking.controllers.controlsPresentationBlocked||
        (ReadUnitControl<uint16_t>(source,2)&0x100)) return false;
    Camera frame{};Quat inverse{};
    if (!BuildTrackingFrame(context.camera,context.reference,frame,inverse)) return false;
    Vec3 head=ToNative(frame,Rotate(Multiply(inverse,context.tracking.headOrientation),{0,0,-1}));
    const float headLength=std::sqrt(Dot(head,head));
    if (!Finite(head)||!std::isfinite(headLength)||headLength<0.001f) return false;
    head=head*(1/headLength);
    Vec3 heading{head.x,head.y,0};
    const float newLength=std::sqrt(Dot(heading,heading));
    if (!std::isfinite(newLength)||newLength<0.001f) return false;
    heading=heading*(1/newLength);
    auto candidate=source;
    // Keep native throttle byte-identical. Its movement consumer uses current
    // body/aim vectors, not these desired vectors; the separate private motion
    // basis adapter keeps the existing XInput head-relative mapping coherent.
    WriteUnitControl(candidate,0x1c,heading);
    WriteUnitControl(candidate,0x34,head);
    NodeMatrix aim{};
    if (aimOverride&&Finite(*aimOverride)&&std::fabs(Dot(*aimOverride,*aimOverride)-1)<=0.01f)
    { WriteUnitControl(candidate,0x28,*aimOverride);controllerAim=true; }
    else if (BuildControllerMatrix(context.camera,context.tracking,context.reference,
        context.tracking.controllers.primaryAim,context.unitsPerMeter,context.positional,aim))
    { WriteUnitControl(candidate,0x28,aim.forward);controllerAim=true; }
    output=candidate;
    return true;
}

inline bool BuildTrackedVehicleControl(const RenderContext& context,const UnitControlPacket& source,
    UnitControlPacket& output) noexcept
{
    if (!context.tracking.serial||!context.tracking.generation||!context.referenceRevision||
        !context.tracking.spaceEpoch||!context.rendererEpoch||
        context.tracking.generation!=context.reference.generation||
        context.tracking.spaceEpoch!=context.reference.spaceEpoch||
        !context.tracking.controllers.padValid||
        context.tracking.controllers.controlsPresentationBlocked||
        (ReadUnitControl<uint16_t>(source,2)&0x100)) return false;
    NodeMatrix aim{};
    if (!BuildControllerMatrix(context.camera,context.tracking,context.reference,
        context.tracking.controllers.primaryAim,context.unitsPerMeter,context.positional,aim)) return false;
    auto candidate=source;
    // CE's ordinary player packet supplies one world direction to these three
    // inputs. Keep that native relationship while replacing only its source
    // with the tracked aiming hand. The following camera reads separate
    // player-control angles; neither it nor the native throttle is rewritten.
    WriteUnitControl(candidate,0x1c,aim.forward);
    WriteUnitControl(candidate,0x28,aim.forward);
    WriteUnitControl(candidate,0x34,aim.forward);
    output=candidate;
    return true;
}

struct UnitMovementBasis { Vec3 forward,aim; };
static_assert(sizeof(UnitMovementBasis)==24);
inline bool BuildUnitMovementBasis(const RenderContext& context,UnitMovementBasis& output) noexcept
{
    if (!context.tracking.serial||!context.referenceRevision||!context.rendererEpoch||
        !context.tracking.generation||!context.tracking.spaceEpoch||
        context.tracking.generation!=context.reference.generation||
        context.tracking.spaceEpoch!=context.reference.spaceEpoch||
        context.tracking.controllers.controlsPresentationBlocked) return false;
    Camera frame{};Quat inverse{};
    if (!BuildTrackingFrame(context.camera,context.reference,frame,inverse)) return false;
    output={frame.forward,context.camera.forward};return true;
}
}
