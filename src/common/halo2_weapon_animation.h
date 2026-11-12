#pragma once
#include "halo2_render_logic.h"
#include "weapon_animation_motion.h"

inline std::array<uint64_t,4> Halo2AnimationIdentity(uint32_t generation,uint32_t unit,
    uint32_t weapon,uint32_t model,uint32_t count,uint32_t partner)
{
    return {generation,(uint64_t(unit)<<32)|weapon,model,(uint64_t(count)<<32)|partner};
}
inline bool Halo2AnimationCameraPose(const Halo2CameraBasis& camera,weapon_animation::Pose& out)
{
    if(!Halo2BuildControllerHandMountBasis(camera,out.rotation))return false;
    std::memcpy(out.translation,camera.position,sizeof(out.translation));
    return weapon_animation::Valid(out);
}
inline bool Halo2ObserveWeaponAnimation(weapon_animation::Reference& reference,
    std::array<uint64_t,4> key,const float* nativeGun,const float* nativeWrist,const Halo2CameraBasis& camera,
    uint64_t now,float unitsPerMetre,weapon_animation::Pose& delta,weapon_animation::ReferenceCache* cache=nullptr)
{
    Halo2FirstPersonTransform gun{},wrist{};weapon_animation::Pose root{},world{};
    if(!Halo2ReadFirstPersonTransform(nativeGun,gun)||!Halo2ReadFirstPersonTransform(nativeWrist,wrist)||!Halo2AnimationCameraPose(camera,root)) {
        reference={};return false;
    }
    std::memcpy(world.rotation,gun.rotation,sizeof(world.rotation));
    // Existing packet ownership preserves the gun's translation relative to
    // its wrist. Restore only the removed wrist translation and gun rotation.
    std::memcpy(world.translation,wrist.translation,sizeof(world.translation));
    if(!weapon_animation::Valid(world)){reference={};return false;}
    return reference.Update(key,weapon_animation::Relative(root,world),now,unitsPerMetre,delta,true,cache);
}
inline bool Halo2ApplyWeaponAnimation(Halo2CameraBasis& carrier,const weapon_animation::Pose& delta,float scale)
{
    weapon_animation::Pose pose{};
    if(!Halo2AnimationCameraPose(carrier,pose)||!weapon_animation::Valid(delta)||!std::isfinite(scale)||scale<=0)return false;
    auto rotationOnly=delta;for(float& v:rotationOnly.translation)v=0;
    const auto result=weapon_animation::Compose(pose,rotationOnly);
    if(!weapon_animation::Valid(result))return false;
    std::memcpy(carrier.forward,result.rotation,sizeof(carrier.forward));
    std::memcpy(carrier.up,result.rotation+6,sizeof(carrier.up));
    std::memcpy(carrier.position,result.translation,sizeof(carrier.position));
    return true;
}
