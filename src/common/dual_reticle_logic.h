#pragma once
#include "runtime_types.h"
#include "weapon_hand_logic.h"
#include "weapon_muzzle.h"
#include "contact_melee_motion.h"

namespace dual_reticle {
inline bool Supported(GameTitle title) noexcept {
    return title==GameTitle::Halo2 || title==GameTitle::Halo3;
}
inline bool Eligible(GameTitle title,bool secondaryPresented,bool primaryTracked,
    bool secondaryTracked,bool crosshair,bool hideHud,bool theatre) noexcept {
    return Supported(title)&&secondaryPresented&&primaryTracked&&secondaryTracked&&
        crosshair&&!hideHud&&!theatre;
}
inline int PhysicalHand(unsigned weaponSlot,bool leftHanded) noexcept {
    return PhysicalHandForWeaponSlot(static_cast<int>(weaponSlot),leftHanded);
}
struct SecondaryTrim {float yaw,pitch,roll;};
inline SecondaryTrim Mirror(float yaw,float pitch,float roll) noexcept {
    return {-yaw,pitch,-roll};
}
struct ProjectedRay { weapon_muzzle::Receipt source{}; weapon_muzzle::Ray tracking{}; };
inline bool Project(const weapon_muzzle::Receipt& source,
    const contact_melee::TrackingToWorld& transform,ProjectedRay& output) noexcept {
    if(!Supported(source.title)||!source.generation||!source.identity||!source.serial||
        !source.space||source.unit==UINT32_MAX||source.weapon==UINT32_MAX||
        source.slot>1||source.barrel>1||!transform.Valid()) return false;
    ProjectedRay candidate{};candidate.source=source;
    const auto position=transform.Tracking({source.ray.position[0],source.ray.position[1],source.ray.position[2]});
    if(!contact_melee::Finite(position)) return false;
    candidate.tracking.position[0]=position.x;candidate.tracking.position[1]=position.y;
    candidate.tracking.position[2]=position.z;
    const contact_melee::Point direction{source.ray.direction[0],source.ray.direction[1],source.ray.direction[2]};
    const contact_melee::Point up{source.ray.up[0],source.ray.up[1],source.ray.up[2]};
    if(!contact_melee::Finite(direction)||!contact_melee::Finite(up)||
        std::abs(contact_melee::Dot(direction,direction)-1)>.01f||
        std::abs(contact_melee::Dot(up,up)-1)>.01f) return false;
    for(int axis=0;axis<3;++axis) {
        candidate.tracking.direction[axis]=contact_melee::Dot(direction,transform.axis[axis]);
        candidate.tracking.up[axis]=contact_melee::Dot(up,transform.axis[axis]);
    }
    output=candidate;return true;
}
inline bool Current(const ProjectedRay& value,GameTitle title,uint32_t generation,
    uint64_t space,uint64_t serial,int64_t timeNs,uint64_t now,unsigned slot,bool leftHanded) noexcept {
    const auto& source=value.source;
    // Rendering accepts only this prepared tracking sample. Weapon replacement,
    // recentering, handedness changes and a missed palette cannot reuse old rays.
    return Supported(title)&&source.serial==serial&&source.timeNs==timeNs&&
        weapon_muzzle::Fresh(source,title,generation,source.unit,source.weapon,
            space,now,timeNs,static_cast<uint8_t>(slot),source.barrel,leftHanded);
}
}
