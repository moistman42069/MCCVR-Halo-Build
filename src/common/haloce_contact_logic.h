#pragma once
#include "haloce_first_person_logic.h"
#include "haloce_frame_context.h"
#include "contact_melee_motion.h"
#include "haloce_weapon_bounds.h"

namespace halo_ce
{
inline contact_melee::Point ContactPoint(Vec3 value) noexcept
{ return {value.x,value.y,value.z}; }

// CE's native button melee searches a broad forward fan, while physical melee
// still requires a real speed-qualified tracked swing. Give that swing a small
// directional reach allowance so the player need not put the controller into
// a short enemy's collision surface. This is VR policy, not an engine radius.
inline constexpr float kPhysicalMeleeReachMetres=0.20f;
inline bool BuildMeleeContactVector(const contact_melee::Sweep& sweep,float unitsPerMetre,
    contact_melee::Point& out) noexcept
{
    if (!contact_melee::Finite(sweep.start)||!contact_melee::Finite(sweep.end)||
        !std::isfinite(unitsPerMetre)||unitsPerMetre<=0||unitsPerMetre>10) return false;
    const auto delta=contact_melee::Subtract(sweep.end,sweep.start);
    const float length=std::sqrt(contact_melee::Dot(delta,delta));
    if (!std::isfinite(length)||length<=0.000001f) return false;
    const float extension=1+kPhysicalMeleeReachMetres*unitsPerMetre/length;
    const contact_melee::Point extended{delta.x*extension,delta.y*extension,delta.z*extension};
    if (!contact_melee::Finite(extended)) return false;
    out=extended;return true;
}

struct ContactHandBinding
{
    uint64_t mask{};
    int16_t wrist{-1};
    unsigned anatomy{}; // Native left=0/right=1 arm chain, distinct from physical hand.
};
inline bool BuildContactHandBindings(const FirstPersonBinding& binding,const ControllerRig& rig,
    ContactHandBinding (&out)[2]) noexcept
{
    if (!binding.generation||binding.graph==UINT32_MAX||!binding.count||binding.count>64||
        binding.leftWrist<0||binding.rightWrist<0||binding.gun<0||
        binding.leftWrist>=binding.count||binding.rightWrist>=binding.count||binding.gun>=binding.count)
        return false;
    const uint64_t range=binding.count==64?~uint64_t{}:(uint64_t{1}<<binding.count)-1;
    const uint64_t masks=binding.leftMask|binding.rightMask|binding.gunMask;
    if ((masks&~range)||(binding.leftMask&binding.rightMask)||
        !binding.gunMask||(binding.gunMask&~binding.rightMask)||
        !(binding.gunMask&(uint64_t{1}<<binding.gun))||
        !(binding.leftMask&(uint64_t{1}<<binding.leftWrist))||
        !(binding.rightMask&(uint64_t{1}<<binding.rightWrist))||
        (binding.gunMask&((uint64_t{1}<<binding.leftWrist)|(uint64_t{1}<<binding.rightWrist)))||
        (masks&1)) return false;
    const unsigned primary=rig.leftHanded?0u:1u,support=1-primary;
    const bool anatomical=rig.leftHanded&&rig.handAlignment;
    out[primary]={((anatomical?binding.leftMask:binding.rightMask)&~binding.gunMask)|binding.gunMask,
        anatomical?binding.leftWrist:binding.rightWrist,anatomical?0u:1u};
    out[support]={(anatomical?binding.rightMask:binding.leftMask)&~binding.gunMask,
        anatomical?binding.rightWrist:binding.leftWrist,anatomical?1u:0u};
    return out[0].mask&&out[1].mask&&!(out[0].mask&out[1].mask);
}

inline uint64_t ContactHash(uint64_t hash,uint64_t value) noexcept
{ return (hash^value)*1099511628211ull; }

inline uint64_t ContactReferenceEpoch(const RenderContext& context) noexcept
{
    if (!context.tracking.generation||!context.tracking.spaceEpoch||!context.referenceRevision||
        !context.rendererEpoch||context.reference.generation!=context.tracking.generation||
        context.reference.spaceEpoch!=context.tracking.spaceEpoch) return 0;
    uint64_t epoch=14695981039346656037ull;
    for (uint64_t value:{uint64_t(context.tracking.generation),context.tracking.spaceEpoch,
        context.referenceRevision,context.rendererEpoch}) epoch=ContactHash(epoch,value);
    return epoch?epoch:1;
}

inline bool BuildContactTransform(const RenderContext& context,contact_melee::TrackingToWorld& out) noexcept
{
    const auto& tracking=context.tracking;const auto& reference=context.reference;
    if (!tracking.generation||!tracking.serial||!tracking.spaceEpoch||!context.referenceRevision||
        !context.rendererEpoch||tracking.predictedDisplayTimeNs<=0||
        tracking.generation!=reference.generation||tracking.spaceEpoch!=reference.spaceEpoch||
        tracking.controllers.controlsPresentationBlocked||!Finite(reference.position)||
        !Finite(tracking.headPosition)||!std::isfinite(context.unitsPerMeter)||
        context.unitsPerMeter<=0||context.unitsPerMeter>10) return false;
    Camera frame{};Quat inverse{};
    if (!BuildTrackingFrame(context.camera,reference,frame,inverse)) return false;
    contact_melee::TrackingToWorld candidate{};
    candidate.unitsPerMetre=context.unitsPerMeter;
    candidate.axis[0]=ContactPoint(ToNative(frame,Rotate(inverse,{1,0,0})));
    candidate.axis[1]=ContactPoint(ToNative(frame,Rotate(inverse,{0,1,0})));
    candidate.axis[2]=ContactPoint(ToNative(frame,Rotate(inverse,{0,0,1})));
    const auto origin=candidate.World(ContactPoint(context.positional?reference.position:tracking.headPosition));
    candidate.origin={context.camera.position.x-origin.x,context.camera.position.y-origin.y,context.camera.position.z-origin.z};
    if (!candidate.Valid()) return false;
    out=candidate;return true;
}

// Preserve native descendant identity/order, with each physical wrist first.
// Recognized stock graphs append the entire authored weapon mesh envelope.
inline bool BuildContactFrames(const RenderContext& context,const FirstPersonBinding& binding,
    const NodeMatrix* palette,uint32_t unit,contact_melee::Frame (&out)[2],
    uint8_t* weaponBoundsSamples=nullptr) noexcept
{
    ContactHandBinding hands[2]{};contact_melee::TrackingToWorld transform{};
    const auto& rig=context.tracking.controllers;
    if (!palette||unit==UINT32_MAX||binding.generation!=context.tracking.generation||
        !BuildContactHandBindings(binding,rig,hands)||!BuildContactTransform(context,transform)) return false;
    for (size_t i=0;i<binding.count;++i) if (!Valid(palette[i])) return false;
    const uint64_t epoch=ContactReferenceEpoch(context);
    if (!epoch) return false;
    Vec3 weaponSamples[kCeWeaponBoundsSamples]{};
    const bool haveWeaponBounds=BuildWeaponMeshBoundsSamples(binding,palette,weaponSamples);
    uint64_t settings=ContactHash(14695981039346656037ull,uint64_t(context.positional));
    for (bool value:{rig.leftHanded,rig.handAlignment,rig.twoHandAimActive,rig.armIk,rig.floatingHands})
        settings=ContactHash(settings,uint64_t(value));
    for (float value:{context.unitsPerMeter,rig.gunScale,rig.supportScale,rig.gunForwardM,
        rig.gunRightM,rig.gunUpM,rig.leftGunForwardM,rig.leftGunRightM,rig.leftGunUpM,rig.supportForwardM,rig.visualPitchDeg,rig.visualYawDeg,
        rig.visualRollDeg,rig.supportMountPitchDeg,rig.supportMountYawDeg,rig.supportMountRollDeg,
        rig.shoulderBackM,rig.primaryShoulderDrop})
    {
        if (!std::isfinite(value)) return false;
        uint32_t bits{};std::memcpy(&bits,&value,sizeof(bits));settings=ContactHash(settings,bits);
    }
    contact_melee::Frame staged[2]{};
    for (unsigned side=0;side<2;++side)
    {
        auto& next=staged[side];const auto& controller=rig.physical[side];
        if (!controller.valid||!Finite(controller.position)||!Valid(controller.orientation)||
            !next.controllerPose.SetPose(&controller.orientation.x,&controller.position.x)) return false;
        next.serial=context.tracking.serial;next.timeNs=context.tracking.predictedDisplayTimeNs;
        next.referenceEpoch=epoch;next.unit=unit;next.transform=transform;next.rigidMotion=true;
        uint64_t shape=ContactHash(settings,binding.graph);
        for (uint64_t value:{uint64_t(binding.count),hands[side].mask,uint64_t(hands[side].wrist),uint64_t(side)})
            shape=ContactHash(shape,value);
        shape=ContactHash(shape,binding.nodeIdentity);
        shape=ContactHash(shape,uint64_t(haveWeaponBounds));
        next.shape=shape?shape:1;
        next.points[next.count++]=transform.Tracking(ContactPoint(palette[hands[side].wrist].position));
        for (size_t i=0;i<binding.count;++i)
            if (i!=size_t(hands[side].wrist)&&(hands[side].mask&(uint64_t{1}<<i)))
                next.points[next.count++]=transform.Tracking(ContactPoint(palette[i].position));
        if (haveWeaponBounds&&(hands[side].mask&binding.gunMask)==binding.gunMask)
        {
            static_assert(kFirstPersonMaxNodes+kCeWeaponBoundsSamples<=contact_melee::kMaxPoints);
            for (auto sample:weaponSamples) next.points[next.count++]=transform.Tracking(ContactPoint(sample));
        }
        // Match the controller solver's broad eight-metre sanity bound. This
        // is a rejection limit for corrupt/implausible palette data, not an
        // assumed hand or weapon size used to generate geometry.
        for (unsigned i=0;i<next.count;++i)
        {
            const auto offset=contact_melee::Subtract(next.points[i],next.controllerPose.origin);
            if (!contact_melee::Finite(offset)||contact_melee::Dot(offset,offset)>64) return false;
        }
        if (!next.Valid()) return false;
    }
    out[0]=staged[0];out[1]=staged[1];
    if (weaponBoundsSamples)
        for (unsigned side=0;side<2;++side)
            weaponBoundsSamples[side]=haveWeaponBounds&&
                (hands[side].mask&binding.gunMask)==binding.gunMask?kCeWeaponBoundsSamples:0;
    return true;
}

// Apply both physical-hand responses to one private palette. Rebuild the named
// arm chains from their native authored source, then recollapse hidden arms at
// the corrected wrists. No camera/root movement or double weapon translation.
inline bool ApplyContactCorrections(const RenderContext& context,const FirstPersonBinding& binding,
    const NodeMatrix* authored,const Vec3 (&delta)[2],NodeMatrix* palette) noexcept
{
    ContactHandBinding hands[2]{};const auto& rig=context.tracking.controllers;
    if (!authored||!palette||binding.generation!=context.tracking.generation||
        !BuildContactHandBindings(binding,rig,hands)||!std::isfinite(context.unitsPerMeter)||
        context.unitsPerMeter<=0||context.unitsPerMeter>10) return false;
    const float maximum=0.75f*context.unitsPerMeter;
    for (auto correction:delta)
        if (!Finite(correction)||Dot(correction,correction)>maximum*maximum) return false;
    std::array<NodeMatrix,kFirstPersonMaxNodes> staged{};
    for (size_t i=0;i<binding.count;++i)
    {
        if (!Valid(palette[i])||!Valid(authored[i])) return false;
        staged[i]=palette[i];
    }
    bool changed=false;
    for (unsigned side=0;side<2;++side)
    {
        if (Dot(delta[side],delta[side])==0) continue;
        changed=true;
        for (size_t i=0;i<binding.count;++i)
            if (hands[side].mask&(uint64_t{1}<<i)) staged[i].position=staged[i].position+delta[side];
        const unsigned anatomy=hands[side].anatomy;
        const auto target=staged[hands[side].wrist];
        if (ShouldApplyArmIk(rig.armIk,rig.twoHandAimActive))
        {
            if (!SolveFirstPersonArm(binding,anatomy,authored,context.camera,rig,target,staged)) return false;
        }
        else
        {
            const auto& carrier=authored[hands[side].wrist];
            for (int joint:{binding.shoulder[anatomy],binding.elbow[anatomy]})
                if (joint>=0&&joint<binding.count&&!MoveNode(carrier,target,authored[joint],staged[joint])) return false;
        }
    }
    if (!changed) return true;
    if (rig.floatingHands&&!CollapseFirstPersonArmsAtWrists(binding,staged)) return false;
    for (size_t i=0;i<binding.count;++i) if (!Valid(staged[i])) return false;
    std::memcpy(palette,staged.data(),binding.count*sizeof(NodeMatrix));return true;
}
}
