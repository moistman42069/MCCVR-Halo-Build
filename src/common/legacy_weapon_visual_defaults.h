#pragma once
#include "runtime_types.h"
#include <cstdint>
#include "visual_aim.h"
#include "weapon_model_catalog.h"
namespace weapon_alignment {
// Both H2 plasma rifles share compression bounds. Only their classic visual
// defaults are interchangeable; retain the ambiguous native identity elsewhere.
inline constexpr uint64_t kHalo2SharedPlasmaRifleVisual = 0x7FFFFFFFFFFFFFFFull;
inline uint64_t Halo2VisualIdentity(const float low[3],const float high[3],unsigned nodes) noexcept {
    if(const auto* model=weapon_model::FindHalo2(low,high,nodes))return model->identity;
    unsigned matches=0;
    for(const auto& model:weapon_model::kModels) {
        if(model.title!=GameTitle::Halo2||model.nodeCount!=nodes)continue;
        bool match=true;
        for(unsigned a=0;a<3;++a)
            match=match&&std::isfinite(low[a])&&std::isfinite(high[a])&&
                std::fabs(low[a]-model.minimum[a])<=.000002f&&
                std::fabs(high[a]-model.maximum[a])<=.000002f;
        if(!match)continue;
        if(model.identity!=0x1B55D3C632C37F00ull&&model.identity!=0xED4D20677ED1FEE9ull)return 0;
        ++matches;
    }
    return matches==2?kHalo2SharedPlasmaRifleVisual:0;
}
struct LegacyVisualDefaults { float yaw{},pitch{},roll{}; };
inline LegacyVisualDefaults LegacyWeaponVisualDefaults(GameTitle title,uint64_t model,bool classic=false) noexcept {
    if(title==GameTitle::Halo2) {
        LegacyVisualDefaults d{};
        switch(model) {
        case kHalo2SharedPlasmaRifleVisual: if(classic)d.yaw=1.f;break;
        case 0xA2DBA1A2B3208E57ull: if(!classic)d.pitch=1.f;break; // Magnum
        case 0x67EC65EB1DE2A459ull: if(classic)d.yaw=-.7f;break; // SMG
        case 0x1B55D3C632C37F00ull: d.yaw=classic?1.f:.5f;break; // Plasma Rifle
        case 0xED4D20677ED1FEE9ull: if(classic)d.yaw=1.f;break; // Brute Plasma Rifle
        case 0x6D99DEAEE0C79FD5ull: d.yaw=classic?1.5f:-.5f;break; // Plasma Pistol
        case 0x1295A15AB1C5709Cull: if(classic)d.yaw=1.5f;break; // Battle Rifle
        case 0xACBAC7877B648512ull: d.pitch=classic?10.f:0.f;d.roll=90.f;break; // Brute Shot
        case 0xBEA57366DD3981D5ull: if(classic)d.yaw=1.1f;break; // Shotgun
        default: break;
        }
        return d;
    }
    if(title!=GameTitle::Halo3&&title!=GameTitle::Halo3ODST)return {};
    LegacyVisualDefaults d{0.f,1.f,0.f};
    switch(model) {
    case 0x15130F16ull: // H3 Spiker
    case 0x11140D13ull: d.roll=10.f;break; // ODST Spiker
    case 0x10190A19ull: d.pitch=8.f;break; // Needler
    case 0x1C080C11ull: d.pitch=-25.f;break; // Energy Sword
    case 0x16120C11ull: // H3 Plasma Rifle / ODST Brute Plasma Rifle
    case 0x191D0A13ull: d.yaw=1.f;break; // ODST Plasma Rifle
    case 0x1A100F1Eull: d.pitch=0.f;break; // Automag
    case 0x150A000Cull: d.yaw=title==GameTitle::Halo3ODST?1.f:-1.f;break; // Plasma Pistol
    default: break;
    }
    return d;
}
// Explicit ODST barrel corrections, separate from the visual pitch defaults.
inline float OdstBarrelPitch(uint64_t model) noexcept {
    switch(model) {
    case 0x10190A19ull: return -10.f; // Needler
    case 0x17141007ull: return -8.f; // Brute Shot
    default: return 0.f;
    }
}
inline bool ApplyOdstBarrelDefaults(uint64_t model,const float basis[9],weapon_muzzle::Ray& ray) noexcept {
    const float pitch=OdstBarrelPitch(model)*.0174533f;
    if(pitch==0.f)return true;
    const float c=std::cos(pitch),s=std::sin(pitch);
    float corrected[9]{};
    for(int r=0;r<3;++r) {
        corrected[r]=basis[r]*c+basis[6+r]*s;
        corrected[3+r]=basis[3+r];
        corrected[6+r]=-basis[r]*s+basis[6+r]*c;
    }
    const float pivot[3]{ray.position[0],ray.position[1],ray.position[2]};
    return Reframe(basis,pivot,corrected,pivot,ray);
}
}
