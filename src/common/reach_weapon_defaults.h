#pragma once
#include <cstdint>
#include "visual_aim.h"
namespace weapon_alignment {
struct ReachDefaults { float yaw{},pitch{},roll{},barrelYaw{},barrelPitch{}; };
// Visual slider baselines and separate, explicitly requested barrel-ray trims.
inline ReachDefaults ReachWeaponDefaults(uint64_t model) noexcept {
    switch(model) {
    case 0x0000000015011515ull: return {-0.8f,0.0f,0.0f,0.0f,0.0f}; // dmr
    case 0x00000000120C1715ull: return {1.2f,0.0f,0.0f,0.0f,0.0f}; // grenade_launcher
    case 0x0000000015150C0Cull: return {-0.5f,0.0f,0.0f,0.0f,0.0f}; // magnum
    case 0x0000000019110C0Cull: return {1.0f,0.0f,0.0f,0.0f,0.0f}; // rocket_launcher
    case 0x0000000013140A0Dull: return {0.5f,0.0f,10.0f,0.0f,0.0f}; // spike_rifle
    case 0x000000001409161Eull: return {3.0f,0.0f,0.0f,0.0f,0.0f}; // plasma_rifle
    case 0x000000001208171Eull: return {-0.5f,0.0f,0.0f,0.0f,0.0f}; // plasma_repeater
    case 0x00000000100B1711ull: return {1.6f,0.0f,0.0f,1.6f,0.0f}; // plasma_pistol
    case 0x00000000100A141Eull: return {-0.5f,0.0f,0.0f,0.0f,0.0f}; // plasma_launcher
    case 0x0000000011091511ull: return {-1.5f,0.0f,0.0f,0.0f,-10.0f}; // needler
    case 0x00000000110D171Eull: return {-0.5f,0.0f,0.0f,0.0f,0.0f}; // focus_rifle
    case 0x000000001615110Eull: return {0.5f,0.0f,0.0f,0.0f,0.0f}; // needle_rifle
    case 0x00000000140C1111ull: return {0.0f,-25.0f,0.0f,0.0f,0.0f}; // energy_sword
    default: return {};
    }
}
// Yaw/pitch use the same forward/left/up convention as the visual slider.
inline bool ApplyReachBarrelDefaults(uint64_t model,const float basis[9],weapon_muzzle::Ray& ray) noexcept {
    const auto d=ReachWeaponDefaults(model);
    if(d.barrelYaw==0.f&&d.barrelPitch==0.f)return true;
    const float yaw=-d.barrelYaw*.0174533f,pitch=d.barrelPitch*.0174533f;
    const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    const float trim[9]{cp*cy,cp*sy,sp,-sy,cy,0,-sp*cy,-sp*sy,cp};
    float corrected[9]{};
    for(int c=0;c<3;++c)for(int r=0;r<3;++r)for(int k=0;k<3;++k)
        corrected[c*3+r]+=basis[k*3+r]*trim[c*3+k];
    const float pivot[3]{ray.position[0],ray.position[1],ray.position[2]};
    return Reframe(basis,pivot,corrected,pivot,ray);
}
}
