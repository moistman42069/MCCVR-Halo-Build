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
// The unit's physics and animation components are object block references, {size, offset} with the size
// just before the offset: each block lives INSIDE the object, after its fixed fields. A reference is
// judged by itself -- present, non-empty, past every fixed field this reader reads, and within the 16-bit
// offset space -- never by a guessed ceiling. The live blocks sit far above one: Reach's physics at
// 0x49B4, Halo 4's physics at 0x6628 (256 bytes) and animation at 0x4B98 (6320 bytes); a 0x4000 cap
// refuses every biped in both titles, so neither crouch camera would ever run.
inline constexpr unsigned UnitFixedEnd(const Layout& l) noexcept
{
    unsigned end=0;
    for(unsigned at:{l.tag,l.seat,l.cameraFlags,l.kind,l.physics,l.animation,l.height,l.scale})
        end=std::max(end,at+4u);
    return end;
}
inline int Component(const Layout& layout,const uint8_t* unit,unsigned at) noexcept
{ return layout.signedComponents?Read<int16_t>(unit,at):Read<uint16_t>(unit,at); }
inline bool BlockPresent(int offset,int size,unsigned fixedEnd,int minimumSize) noexcept
{ return offset>=int(fixedEnd)&&size>=minimumSize&&offset+size<=0x10000; }
inline const uint8_t* Decode(const uintptr_t* pages,uint32_t encoded) noexcept
{
    if(!pages||!encoded) return nullptr;
    const uintptr_t base=pages[encoded>>28],offset=uintptr_t(encoded)*4;
    if(!base||base>std::numeric_limits<uintptr_t>::max()-offset) return nullptr;
    return reinterpret_cast<const uint8_t*>(base+offset);
}
// Which check refused (diagnostics only): 1 no unit/table/pages, 2 seated, 3 camera flag 4,
// 4 kind not 0, 5 physics/animation index out of range, 6 physics mode 7/8, 7 no tag, 8 definition
// not decoded, 9 override count, 10 override entries not decoded, 11 heights/scale implausible.
struct ReductionDiag {int step=0,physics=0,animation=0;float standing=0,crouched=0,current=0,scale=0;};
inline bool Reduction(const Layout& layout,const uint8_t* unit,const uint8_t* tagTable,
    const uintptr_t* pages,float& reduction,const uint8_t*& definition,
    ReductionDiag* diag=nullptr) noexcept
{
    reduction=0;definition=nullptr;
    const auto refuse=[diag](int step){if(diag) diag->step=step;return false;};
    if(diag) *diag=ReductionDiag{};
    if(!unit||!tagTable||!pages) return refuse(1);
    if(Read<int16_t>(unit,layout.seat)!=-1) return refuse(2);
    if(Read<uint32_t>(unit,layout.cameraFlags)&4) return refuse(3);
    if(unit[layout.kind]!=0) return refuse(4);
    const int physics=Component(layout,unit,layout.physics);
    const int animation=Component(layout,unit,layout.animation);
    if(diag) {diag->physics=physics;diag->animation=animation;}
    constexpr int kModeBytes=4;
    if(!BlockPresent(physics,Component(layout,unit,layout.physics-2),UnitFixedEnd(layout),kModeBytes)||
       !BlockPresent(animation,Component(layout,unit,layout.animation-2),UnitFixedEnd(layout),
           int(layout.animationCamera)+4))
        return refuse(5);
    const auto mode=Read<uint32_t>(unit,unsigned(physics));
    if(mode==7||mode==8) return refuse(6);
    const auto tag=Read<uint32_t>(unit,layout.tag);
    if(tag==UINT32_MAX) return refuse(7);
    definition=Decode(pages,Read<uint32_t>(tagTable,(tag&0xffff)*8+4));
    if(!definition) return refuse(8);
    float standing=Read<float>(definition,layout.standing);
    float crouched=Read<float>(definition,layout.crouched);
    const int count=Read<int32_t>(definition,layout.overrides);
    if(count<0||count>256) return refuse(9);
    if(count)
    {
        const auto* entries=Decode(pages,Read<uint32_t>(definition,layout.overrides+4));
        if(!entries) return refuse(10);
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
    if(diag) {diag->standing=standing;diag->crouched=crouched;diag->current=current;diag->scale=scale;}
    if(!std::isfinite(standing)||!std::isfinite(crouched)||!std::isfinite(current)||
        !std::isfinite(scale)||standing<=0||standing>10||crouched<0||crouched>standing||
        current<crouched-.0001f||current>standing+.0001f||scale<=0||scale>100) return refuse(11);
    // Native producer already smooths this cache; using a desired fraction
    // would jump before the native camera and over-correct its release tail.
    reduction=std::max(0.f,standing-current)*scale;
    return std::isfinite(reduction);
}
}
