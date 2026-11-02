#pragma once
#include "odst_modded_fp_logic.h"

// Confirmed by the Mythic cache and the 552124dd native palette probe.
// Only this complete human body map proves the early-camera graph family.
inline bool ResolveOdstMythicEarlyCamera(uint32_t checksum,
    std::span<const int32_t> map,int count,OdstMappedFpLayout& result)
{
    if(checksum!=0x14020C1Fu || map.size()!=37 || count<39 || count>64 || map[0]!=0)
        return false;
    for(int i=1;i<37;++i)if(map[i]!=i+1)return false;
    OdstMappedFpLayout candidate{};
    if(!ResolveOdstMappedFpLayout(checksum,map,count,candidate))return false;
    candidate.skeleton.cameraControl=1;
    // The evidenced family appends the entire held model after the 37 body nodes.
    for(int i=38;i<count;++i)
        candidate.skeleton.rightHandAndWeaponDescendants|=uint64_t{1}<<i;
    result=candidate;
    return true;
}
