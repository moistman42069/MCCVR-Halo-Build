#pragma once
#include "runtime_types.h"
#include "haloce_snapshot.h"
#include <cmath>
#include <cstdint>

namespace weapon_muzzle
{
struct Marker
{
    GameTitle title{};
    uint64_t identity{};
    uint16_t nodeCount{},node{};
    uint8_t barrel{};
    float position[3]{},forward[3]{},up[3]{0,0,1};
};
struct Ray {float position[3]{},direction[3]{},up[3]{0,0,1};};

// The adapter must resolve this exact authored node in its committed visible
// palette. A controller/grip pose is not a substitute for that node matrix.
inline bool Transform(const Marker& marker,float scale,const float basis[9],
    const float position[3],Ray& output) noexcept
{
    if(!marker.identity || !marker.nodeCount || marker.node>=marker.nodeCount ||
        !basis || !position || !std::isfinite(scale) || scale<=.000001f || scale>=100.f)return false;
    for(int column=0;column<3;++column)
    {
        float length=0;
        for(int row=0;row<3;++row)
        {
            const auto component=basis[column*3+row];
            if(!std::isfinite(component))return false;
            length+=component*component;
        }
        if(std::abs(length-1.f)>.05f)return false;
        for(int other=0;other<column;++other)
        {
            float dot=0;
            for(int row=0;row<3;++row)dot+=basis[column*3+row]*basis[other*3+row];
            if(std::abs(dot)>.025f)return false;
        }
    }
    Ray staged{};float length=0,upLength=0,dot=0;
    for(float& value:staged.up)value=0;
    for(int row=0;row<3;++row)
    {
        staged.position[row]=position[row];
        for(int column=0;column<3;++column)
        {
            staged.position[row]+=scale*basis[column*3+row]*marker.position[column];
            staged.direction[row]+=basis[column*3+row]*marker.forward[column];
            staged.up[row]+=basis[column*3+row]*marker.up[column];
        }
        if(!std::isfinite(staged.position[row]) || std::abs(staged.position[row])>1000000.f ||
            !std::isfinite(staged.direction[row]) || !std::isfinite(staged.up[row]))return false;
        length+=staged.direction[row]*staged.direction[row];
        upLength+=staged.up[row]*staged.up[row];
        dot+=staged.direction[row]*staged.up[row];
    }
    if(!std::isfinite(length) || length<.95f || length>1.05f ||
        !std::isfinite(upLength) || upLength<.95f || upLength>1.05f || std::abs(dot)>.025f)return false;
    const float inverse=1.f/std::sqrt(length);
    for(float& value:staged.direction)value*=inverse;
    const float upInverse=1.f/std::sqrt(upLength);
    for(float& value:staged.up)value*=upInverse;
    output=staged;return true;
}

struct Receipt
{
    GameTitle title{};
    uint32_t generation{},unit=UINT32_MAX,weapon=UINT32_MAX;
    uint64_t identity{},space{},serial{},at{};
    int64_t timeNs{};
    uint8_t slot{},barrel{};
    bool leftHanded{};
    Ray ray{};
};
inline bool Fresh(const Receipt& sample,GameTitle title,uint32_t generation,
    uint32_t unit,uint32_t weapon,uint64_t space,uint64_t now,int64_t timeNs,
    uint8_t slot,uint8_t barrel,bool leftHanded) noexcept
{
    return title!=GameTitle::None && sample.title==title && generation && sample.generation==generation &&
        unit!=UINT32_MAX && sample.unit==unit && weapon!=UINT32_MAX && sample.weapon==weapon &&
        sample.identity && space && sample.space==space && sample.serial &&
        sample.at && now>=sample.at && now-sample.at<=100 && sample.timeNs>0 &&
        timeNs>=sample.timeNs && timeNs-sample.timeNs<=100000000 &&
        slot<2 && sample.slot==slot && barrel<2 && sample.barrel==barrel && sample.leftHanded==leftHanded;
}
using Publication=halo_ce::Snapshot<Receipt>;

// Both barrels belong to one committed palette. Publish the whole sample so a
// rejected/hidden node immediately invalidates its previous successful receipt.
struct Palette { Receipt barrels[2]{}; };
class Store
{
    halo_ce::Snapshot<Palette> slots_[7][2];
    static bool Supported(GameTitle title) noexcept
    { return title>=GameTitle::Halo3 && title<=GameTitle::Halo2; }
public:
    bool Publish(GameTitle title,uint8_t slot,const Palette& palette) noexcept
    {
        if(!Supported(title) || slot>=2)return false;
        return slots_[static_cast<unsigned>(title)][slot].Publish(palette);
    }
    bool Read(GameTitle title,uint32_t generation,uint32_t unit,uint32_t weapon,
        uint64_t space,uint64_t now,int64_t timeNs,uint8_t slot,uint8_t barrel,
        bool leftHanded,Receipt& output) const noexcept
    {
        if(!Supported(title) || slot>=2 || barrel>=2)return false;
        Palette palette{};
        if(!slots_[static_cast<unsigned>(title)][slot].Read(palette))return false;
        const auto& sample=palette.barrels[barrel];
        if(!Fresh(sample,title,generation,unit,weapon,space,now,timeNs,slot,barrel,leftHanded))return false;
        output=sample;return true;
    }
    // The committed palette as published, unjudged: for a diagnostic that says why Read refused it.
    bool Peek(GameTitle title,uint8_t slot,Palette& output) const noexcept
    { return Supported(title) && slot<2 && slots_[static_cast<unsigned>(title)][slot].Read(output); }
};
}

#include "weapon_muzzles.generated.h"
namespace weapon_muzzle
{
inline const Marker* Find(GameTitle title,uint64_t identity,uint8_t barrel) noexcept
{
    if(!identity || barrel>=2)return nullptr;
    for(const auto& marker:kMarkers)
        if(marker.title==title && marker.identity==identity && marker.barrel==barrel)return &marker;
    return nullptr;
}
}
