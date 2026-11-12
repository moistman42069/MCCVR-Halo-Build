#pragma once
#include "odst_modded_fp_logic.h"

// New Covenant's ODST cache and native probe agree: 31 body nodes, followed
// by the held model and one final camera node. Never infer this from count alone.
inline bool ResolveOdstEliteLayout(uint32_t checksum,std::span<const int32_t> map,
    int count,OdstMappedFpLayout& result)
{
    if(checksum!=0x10000E0Bu || map.size()!=31 || count<33 || count>64)return false;
    for(int i=0;i<31;++i)if(map[i]!=i)return false;
    OdstMappedFpLayout candidate{};
    if(!ResolveOdstMappedFpLayout(checksum,map,count,candidate))return false;
    candidate.skeleton.cameraControl=count-1;
    for(int i=31;i<count-1;++i)
        candidate.skeleton.rightHandAndWeaponDescendants|=uint64_t{1}<<i;
    result=candidate;
    return true;
}

// The native secondary Elite palette emits only the left arm and its four
// fingers. Omitted outputs are -1; the underlying source graph keeps its indices.
inline bool ResolveOdstEliteSecondaryLayout(uint32_t checksum,std::span<const int32_t> map,
    int count,OdstMappedFpLayout& result)
{
    if(checksum!=0x10000E0Bu || map.size()!=31)return false;
    std::array<int32_t,31> complete{};
    for(int i=0;i<31;++i) {
        const bool left=i==1 || i==3 || i==5 || (i>=7 && i<=10) ||
            (i>=15 && i<=18) || (i>=23 && i<=26);
        if(map[i]!=(left?i:-1))return false;
        complete[i]=i;
    }
    // Resolve graph roles from the proven full topology, not from missing
    // palette outputs. Hidden right-side outputs are never made visible.
    return ResolveOdstEliteLayout(checksum,complete,count,result);
}
