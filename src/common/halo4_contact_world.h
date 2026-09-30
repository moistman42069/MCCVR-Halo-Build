#pragma once
#include "contact_melee_motion.h"
#include <cstring>

// H4EK E71F40 / E717F0 / E730F0 and PhysicsRayCast output from 4E8F40.
// H4's Havok result is not Reach's collision_result; offsets are title-owned.
namespace halo4_contact_world
{
template<class T> inline T Read(const void* bytes,unsigned offset) noexcept
{ T value{};std::memcpy(&value,static_cast<const uint8_t*>(bytes)+offset,sizeof(value));return value; }
inline bool Decode(const void* result,contact_melee::Hit& hit) noexcept
{
    hit={};if(!result) return false;
    const auto type=Read<uint32_t>(result,0);
    if(type!=1 && type!=3) return false;
    const auto bsp=Read<uint32_t>(result,0x20);
    if(bsp>=32) return false;
    hit.fraction=Read<float>(result,4);
    hit.position=Read<contact_melee::Point>(result,8);
    hit.normal=Read<contact_melee::Point>(result,0x14);
    if(!contact_melee::Finite(hit.position) || !contact_melee::Finite(hit.normal) ||
        !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1 ||
        contact_melee::Dot(hit.normal,hit.normal)<0.5f) return false;
    hit.kind=contact_melee::HitKind::WorldSurface;hit.world.nativeType=type;
    constexpr unsigned offsets[]{0x20,0x24,0x2C,0x30,0x60,0x64,0x68};
    for(unsigned n=0;n<7;++n) hit.world.nativeData[n]=Read<uint32_t>(result,offsets[n]);
    // +35 is padding and the native stack result need not initialize it.
    hit.world.nativeData[7]=Read<uint8_t>(result,0x34) |
        (uint32_t(Read<uint16_t>(result,0x36))<<8);
    hit.world.material=Read<uint16_t>(result,0x38);hit.world.valid=true;return true;
}
inline bool ParametersMatch(const uint32_t* p,const contact_melee::Hit& hit) noexcept
{
    return p && hit.world.valid && p[0]==UINT32_MAX && p[1]!=UINT32_MAX &&
        p[5]==hit.world.nativeData[0] && static_cast<uint16_t>(p[8])==hit.world.material && !std::memcmp(p+13,&hit.position,12) &&
        !std::memcmp(p+16,&hit.normal,12);
}
inline bool ImpactMatches(const void* impact,const uint32_t* p) noexcept
{
    if(!impact || !p || p[0]!=UINT32_MAX) return false;
    return Read<uint32_t>(impact,0x1C)==UINT32_MAX &&
        !std::memcmp(impact,p+13,12) && !std::memcmp(static_cast<const uint8_t*>(impact)+12,p+16,12) &&
        Read<uint32_t>(impact,0x18)==p[5] &&
        Read<uint16_t>(impact,0x20)==static_cast<uint16_t>(p[6]) &&
        Read<uint16_t>(impact,0x22)==static_cast<uint16_t>(p[7]) &&
        Read<uint32_t>(impact,0x24)==p[10] && Read<uint32_t>(impact,0x28)==p[11] &&
        Read<uint16_t>(impact,0x30)==static_cast<uint16_t>(p[8]);
}
}
