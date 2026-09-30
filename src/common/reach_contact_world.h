#pragma once
#include "contact_melee_motion.h"
#include <cstring>

// HREK D6C1B0's collision result and D6CD60's damage target. These are
// Reach layouts, independently matched to the pinned retail module.
namespace reach_contact_world
{
template<class T> inline T Read(const void* bytes,unsigned offset) noexcept
{ T value{};std::memcpy(&value,static_cast<const uint8_t*>(bytes)+offset,sizeof(value));return value; }

inline bool Decode(const void* result,contact_melee::Hit& hit) noexcept
{
    hit={};
    if(!result) return false;
    const auto type=Read<uint32_t>(result,0);
    if(type!=1 && type!=3) return false;
    hit.fraction=Read<float>(result,4);
    hit.position=Read<contact_melee::Point>(result,8);
    hit.normal=Read<contact_melee::Point>(result,0x2C);
    if(!contact_melee::Finite(hit.position) || !contact_melee::Finite(hit.normal) ||
        !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1 ||
        contact_melee::Dot(hit.normal,hit.normal)<0.5f) return false;
    hit.kind=contact_melee::HitKind::WorldSurface;
    hit.world.nativeType=type;
    // Preserve the complete native surface location and breakable tuple.
    // No object handle is manufactured for a structure/instanced hit.
    constexpr unsigned offsets[]{0x18,0x1C,0x20,0x24,0x3C,0x4C,0x50,0x54,0x58};
    for(unsigned n=0;n<9;++n) hit.world.nativeData[n]=Read<uint32_t>(result,offsets[n]);
    hit.world.material=Read<uint16_t>(result,0x28);
    hit.world.valid=true;
    return true;
}

inline bool ParametersMatch(const uint32_t* parameters,const void* collision,
    const contact_melee::Hit& hit) noexcept
{
    if(!parameters || !collision || !hit.world.valid || parameters[0]!=UINT32_MAX ||
        parameters[1]==UINT32_MAX || static_cast<uint16_t>(parameters[8])!=hit.world.material || parameters[5]!=Read<uint32_t>(collision,0x4C) ||
        std::memcmp(parameters+13,&hit.position,12) ||
        std::memcmp(parameters+16,&hit.normal,12)) return false;
    // The native builder fills these only when its collision result says
    // breakable. Ordinary solid material effects keep the -1 sentinels.
    if(Read<uint8_t>(collision,0x5D)&8)
        return parameters[6]==Read<uint8_t>(collision,0x62) &&
            parameters[7]==Read<uint8_t>(collision,0x5E) &&
            parameters[10]==Read<uint32_t>(collision,0x3C) &&
            parameters[11]==Read<uint32_t>(collision,0x54);
    return parameters[6]==UINT32_MAX && parameters[7]==UINT32_MAX &&
        parameters[10]==UINT32_MAX && parameters[11]==UINT32_MAX;
}

inline bool ImpactMatches(const void* impact,const uint32_t* parameters) noexcept
{
    if(!impact || !parameters || parameters[0]!=UINT32_MAX) return false;
    return Read<uint32_t>(impact,0x1C)==UINT32_MAX &&
        !std::memcmp(impact,parameters+13,12) &&
        !std::memcmp(static_cast<const uint8_t*>(impact)+12,parameters+16,12) &&
        Read<uint32_t>(impact,0x18)==parameters[5] &&
        Read<uint16_t>(impact,0x20)==static_cast<uint16_t>(parameters[6]) &&
        Read<uint16_t>(impact,0x22)==static_cast<uint16_t>(parameters[7]) &&
        Read<uint32_t>(impact,0x24)==parameters[10] &&
        Read<uint32_t>(impact,0x28)==parameters[11] &&
        Read<uint16_t>(impact,0x30)==static_cast<uint16_t>(parameters[8]);
}
}
