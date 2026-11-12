#pragma once
#include <cstdint>
namespace weapon_alignment {
// Shipped visual yaw baseline, in slider degrees (positive turns right).
// Unknown/custom models retain zero; this never changes ballistic aim.
inline float Halo4DefaultVisualYaw(uint64_t model) noexcept {
    switch(model) {
    case 0x0000000015111A1Dull: return 1.8f; // storm_magnum
    case 0x00000000171C0B05ull: return -0.7f; // storm_shotgun
    case 0x00000000111B0416ull: return -1.5f; // storm_dmr
    case 0x0000000010030014ull: return 3.0f; // storm_rail_gun
    case 0x000000001A0D0314ull: return -0.3f; // storm_sniper_rifle
    case 0x0000000011120517ull: return 2.0f; // storm_br
    case 0x000000001814181Cull: return -0.5f; // storm_assault_rifle
    case 0x0000000013020014ull: return 2.5f; // storm_lmg
    case 0x0000000011080014ull: return 0.5f; // storm_rocket_launcher
    case 0x000000001402121Bull: return 3.5f; // storm_sticky_detonator
    case 0x0000000012140501ull: return 1.8f; // storm_plasma_pistol
    case 0x000000001B080715ull: return -0.5f; // storm_concussion_rifle
    case 0x0000000017141708ull: return -0.5f; // storm_fuel_rod_cannon
    case 0x000000001314081Cull: return 1.5f; // storm_needler
    case 0x00000000110F0115ull: return -0.6f; // storm_beam_rifle
    case 0x0000000017161F1Cull: return 1.7f; // storm_assault_carbine
    case 0x000000001108171Full: return -0.3f; // storm_spread_gun
    case 0x000000001305160Bull: return 3.8f; // storm_stasis_pistol (Boltshot)
    case 0x000000001402141Cull: return -0.3f; // storm_forerunner_sniper_rifle
    case 0x0000000017180016ull: return 2.5f; // storm_forerunner_smg
    case 0x0000000014031004ull: return 1.7f; // storm_forerunner_rifle
    case 0x00000000111D0D05ull: return 2.5f; // storm_forerunner_incineration_launcher
    case 0x151C1A1Bull: return -0.5f; // storm_covenant_carbine
    default: return 0.f;
    }
}
}
