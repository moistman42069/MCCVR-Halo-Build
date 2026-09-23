#pragma once
#include "runtime_types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace physical_crouch_cached {
struct Layout {
    GameTitle title;
    unsigned tag,seat,cameraFlags,kind,physics,animation,animationCamera;
    unsigned height,scale,standing,crouched,overrides;
    bool signedComponents;
};
// Each row is independently matched to its own official-kit evaluator,
// animation-camera selector and native height producer; see evidence doc.
inline constexpr Layout reach{GameTitle::HaloReach,0,0x336,0x138,0x8e,0x1b2,
    0x18e,0x1fc,0xa38,0x80,0x524,0x52c,0x54c,true};
inline constexpr Layout halo4{GameTitle::Halo4,8,0x2c,0x16c,0xb2,0x47a,
    0x1e2,0x224,0xf74,0xa0,0x618,0x620,0x640,false};
template<class T> T Read(const uint8_t* pointer,unsigned offset) noexcept
{T value{};std::memcpy(&value,pointer+offset,sizeof(value));return value;}
inline const uint8_t* Decode(const uintptr_t* pages,uint32_t encoded) noexcept
{
    if(!pages||!encoded) return nullptr;
    const uintptr_t base=pages[encoded>>28],offset=uintptr_t(encoded)*4;
    if(!base||base>std::numeric_limits<uintptr_t>::max()-offset) return nullptr;
    return reinterpret_cast<const uint8_t*>(base+offset);
}
inline bool Reduction(const Layout& layout,const uint8_t* unit,const uint8_t* tagTable,
    const uintptr_t* pages,float& reduction,const uint8_t*& definition) noexcept
{
    reduction=0;definition=nullptr;
    if(!unit||!tagTable||!pages||Read<int16_t>(unit,layout.seat)!=-1||
        (Read<uint32_t>(unit,layout.cameraFlags)&4)||unit[layout.kind]!=0) return false;
    const int physics=layout.signedComponents?Read<int16_t>(unit,layout.physics):
        Read<uint16_t>(unit,layout.physics);
    const int animation=layout.signedComponents?Read<int16_t>(unit,layout.animation):
        Read<uint16_t>(unit,layout.animation);
    if(physics<0||physics>0x4000||animation<=0||animation>0x4000) return false;
    const auto mode=Read<uint32_t>(unit,unsigned(physics));
    if(mode==7||mode==8) return false;
    const auto tag=Read<uint32_t>(unit,layout.tag);
    if(tag==UINT32_MAX) return false;
    definition=Decode(pages,Read<uint32_t>(tagTable,(tag&0xffff)*8+4));
    if(!definition) return false;
    float standing=Read<float>(definition,layout.standing);
    float crouched=Read<float>(definition,layout.crouched);
    const int count=Read<int32_t>(definition,layout.overrides);
    if(count<0||count>256) return false;
    if(count)
    {
        const auto* entries=Decode(pages,Read<uint32_t>(definition,layout.overrides+4));
        if(!entries) return false;
        const auto camera=Read<uint32_t>(unit,unsigned(animation)+layout.animationCamera);
        for(int i=0;i<count;++i)
            if(Read<uint32_t>(entries,unsigned(i)*12)==camera)
            {
                standing=Read<float>(entries,unsigned(i)*12+4);
                crouched=Read<float>(entries,unsigned(i)*12+8);
                break;
            }
    }
    const float current=Read<float>(unit,layout.height),scale=Read<float>(unit,layout.scale);
    if(!std::isfinite(standing)||!std::isfinite(crouched)||!std::isfinite(current)||
        !std::isfinite(scale)||standing<=0||standing>10||crouched<0||crouched>standing||
        current<crouched-.0001f||current>standing+.0001f||scale<=0||scale>100) return false;
    // Native producer already smooths this cache; using a desired fraction
    // would jump before the native camera and over-correct its release tail.
    reduction=std::max(0.f,standing-current)*scale;
    return std::isfinite(reduction);
}
}
