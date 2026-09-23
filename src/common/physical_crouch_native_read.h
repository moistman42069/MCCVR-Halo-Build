#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Read-only native camera math, independently derived from each official kit
// and matched to the pinned retail evaluator. Caller supplies owned, bounded
// live unit/definition storage under SEH and a current cold-verified contract.
namespace physical_crouch_native
{
template<class T> T Read(const uint8_t* bytes,unsigned at) noexcept
{ T value{};std::memcpy(&value,bytes+at,sizeof(value));return value; }
inline bool Reduction(float standing,float crouched,float fraction,float scale,float& reduction) noexcept
{
    reduction=0;
    if(!std::isfinite(standing)||!std::isfinite(crouched)||!std::isfinite(fraction)||
       !std::isfinite(scale)||standing<=0||standing>10||crouched<0||crouched>standing||
       fraction<0||fraction>1||scale<=0||scale>100)return false;
    reduction=(standing-crouched)*fraction*scale;
    return std::isfinite(reduction);
}
inline bool Odst(const uint8_t* unit,const uint8_t* definition,float& reduction) noexcept
{
    reduction=0;
    if(!unit||!definition||Read<uint32_t>(unit,0x10)!=UINT32_MAX||
       (Read<uint32_t>(unit,0x104)&4)||unit[0x96]!=0)return false;
    // Physics modes 7/8 use a tilted up-vector. Do not cancel world Z using
    // a vertical-only formula in those alternate camera modes.
    const auto physics=Read<int16_t>(unit,0x15A);
    if(physics<0||physics>0x2000)return false;
    const auto mode=Read<int32_t>(unit,unsigned(physics));
    if(mode==7||mode==8)return false;
    const bool forcedStanding=unit[0x516]==6&&(Read<uint32_t>(unit,0x51C)&0x10);
    return Reduction(Read<float>(definition,0x310),Read<float>(definition,0x318),
        forcedStanding?0.f:Read<float>(unit,0x398),Read<float>(unit,0x8C),reduction);
}
inline bool Halo2(const uint8_t* unit,const uint8_t* definition,float& reduction) noexcept
{
    reduction=0;
    if(!unit||!definition||Read<uint32_t>(unit,0x14)!=UINT32_MAX||
       (unit[0x10A]&4)||unit[0xAA]!=0)return false;
    // The native special-state predicate can force standing when this bit
    // is set. Conservatively decline that state rather than execute animation
    // queries from the VR camera or copy another engine's state layout.
    if(Read<uint16_t>(unit,0x390)&0x2000)return false;
    // Retail's compact tag layout is +218/+21C, NOT H2EK's +2B8/+2BC.
    return Reduction(Read<float>(definition,0x218),Read<float>(definition,0x21C),
        Read<float>(unit,0x30C),Read<float>(unit,0xA0),reduction);
}
}
