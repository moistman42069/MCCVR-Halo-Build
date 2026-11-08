#pragma once
#include "odst_bringup_logic.h"
#include "fp_source_ownership.h"
#include <array>
#include <span>
#include <bit>

// Own-title evidence: ODST-MODDED-WEAPONS-MAP-EVIDENCE.json and the official
// recon/ONI FP exports. Checksums identify body rigs, never weapon names.
struct OdstMappedFpLayout
{
    OdstFpSkeletonLayout skeleton{};
    uint64_t bodySources = 0;
    uint32_t checksum = 0;
    int bodyCount = 0;
    std::array<int32_t,37> remap{};
    bool operator==(const OdstMappedFpLayout&) const = default;
};

inline bool ResolveOdstMappedFpLayout(uint32_t checksum,
    std::span<const int32_t> remap, int sourceCount, OdstMappedFpLayout& out)
{
    const bool human = remap.size()==37 &&
        (checksum==0x1114091Cu || checksum==0x18080211u || checksum==0x1B120B14u || checksum==0x14020C1Fu);
    const bool elite = remap.size()==31 && checksum==0x10000E0Bu;
    if ((!human && !elite) || sourceCount<=int(remap.size()) || sourceCount>64)
        return false;
    OdstMappedFpLayout result{};
    result.checksum=checksum;
    result.bodyCount=int(remap.size());
    // The body map alone cannot locate camera_control. Mythic Automag places
    // it at source 1. Resolve it only after observing the disjoint gun map.
    result.skeleton.cameraControl=-1;
    for (size_t i=0;i<remap.size();++i)
    {
        const int source=remap[i];
        if (source<0 || source>=sourceCount ||
            (result.bodySources & (uint64_t{1}<<source))) return false;
        result.bodySources|=uint64_t{1}<<source;
        result.remap[i]=source;
    }
    auto& s=result.skeleton;
    s.leftShoulder=remap[1]; s.rightShoulder=remap[2];
    s.leftElbow=remap[3]; s.rightElbow=remap[4];
    s.leftWrist=remap[5]; s.rightWrist=remap[6];
    s.leftHandDescendants=uint64_t{1}<<s.leftWrist;
    s.rightHandAndWeaponDescendants=uint64_t{1}<<s.rightWrist;
    const int digits=human?5:4;
    for (int joint=0;joint<3;++joint)
        for (int digit=0;digit<digits;++digit)
        {
            s.leftHandDescendants|=uint64_t{1}<<remap[7+joint*digits*2+digit];
            s.rightHandAndWeaponDescendants|=uint64_t{1}<<remap[7+joint*digits*2+digits+digit];
        }
    out=result;
    return true;
}

inline bool ResolveOdstHeldSources(const OdstMappedFpLayout& body,
    std::span<const int32_t> remap, int sourceCount, FpHeldSources& out)
{
    if (!body.bodyCount || sourceCount<=0 || sourceCount>64 ||
        (body.skeleton.cameraControl<0 || body.skeleton.cameraControl>=sourceCount) || remap.empty() || remap.size()>64)
        return false;
    FpHeldSources held{};
    held.explicitMap=true;
    for (int source:remap)
    {
        if (source==-1) continue;
        if (source<0 || source>=sourceCount || source==body.skeleton.cameraControl) return false;
        const uint64_t bit=uint64_t{1}<<source;
        if ((body.bodySources | held.bits[0]) & bit) return false;
        held.bits[0]|=bit;
    }
    if (!held.bits[0]) return false;
    out=held;
    return true;
}

struct OdstFpLayoutKey
{
    uint32_t generation=0;
    int view=0,id=0,slot=0,count=0;
    bool operator==(const OdstFpLayoutKey&) const = default;
};
struct OdstFpLayoutEntry
{
    bool valid=false;
    OdstFpLayoutKey key{};
    uint16_t bodyTag=0xFFFF,heldTag=0xFFFF;
    OdstMappedFpLayout layout{};
    uint64_t heldMask=0;
};

inline bool OdstCollectSourceMask(std::span<const int32_t> remap,int count,uint64_t& mask)
{
    mask=0;
    if(count<=0 || count>64 || remap.empty() || remap.size()>64)return false;
    for(int source:remap) {
        if(source==-1)continue;
        if(source<0 || source>=count || (mask&(uint64_t{1}<<source)))return false;
        mask|=uint64_t{1}<<source;
    }
    return mask!=0;
}

// Freeze complete body+gun evidence at the next stereo pair. Either palette
// may arrive first, and one interpolation may feed multiple palettes.
struct OdstFpLayoutCache
{
    std::array<OdstFpLayoutEntry,4> learned{},frozen{};
    uint64_t serial=0,epoch=0;
    uint32_t generation=0;
    bool Begin(uint64_t nextSerial,uint64_t nextEpoch,uint32_t nextGeneration)
    {
        if (!nextSerial || !nextEpoch || !nextGeneration) { *this={}; return false; }
        if (epoch!=nextEpoch || generation!=nextGeneration || nextSerial<serial)
            *this={};
        if (serial!=nextSerial)
        {
            frozen=learned;
            serial=nextSerial; epoch=nextEpoch; generation=nextGeneration;
        }
        return true;
    }
    OdstFpLayoutEntry Find(const OdstFpLayoutKey& key) const
    {
        for (const auto& e:frozen) if (e.valid && e.key==key) return e;
        return {};
    }
    void Reject(const OdstFpLayoutKey& key)
    {
        for (auto& e:learned) if (e.key==key) e={};
    }
    OdstFpLayoutEntry* Entry(const OdstFpLayoutKey& key)
    {
        if(!serial || key.generation!=generation || key.count<=0 || key.count>64)return nullptr;
        for(auto& e:learned)if(e.key==key)return &e;
        for(auto& e:learned)if(!e.key.generation){e.key=key;return &e;}
        learned[0]={};learned[0].key=key;return &learned[0];
    }
    static void Complete(OdstFpLayoutEntry& e)
    {
        e.valid=false;e.layout.skeleton.cameraControl=-1;
        if(!e.layout.bodyCount || !e.heldMask || (e.layout.bodySources&e.heldMask))return;
        const uint64_t all=e.key.count==64?UINT64_MAX:(uint64_t{1}<<e.key.count)-1;
        const uint64_t remaining=all&~(e.layout.bodySources|e.heldMask);
        // The evidenced ODST combined graphs have exactly body, gun and one
        // camera node. Extra unmapped nodes are ambiguous and stay stock.
        if(!std::has_single_bit(remaining))return;
        e.layout.skeleton.cameraControl=int(std::countr_zero(remaining));e.valid=true;
    }
    void Learn(const OdstFpLayoutKey& key,uint16_t tag,const OdstMappedFpLayout& layout)
    {
        auto* e=Entry(key);if(!e || !layout.bodyCount)return;
        e->bodyTag=tag;e->layout=layout;Complete(*e);
    }
    void LearnHeld(const OdstFpLayoutKey& key,uint16_t tag,uint64_t mask)
    {
        auto* e=Entry(key);if(!e)return;
        e->heldTag=tag;e->heldMask=mask;Complete(*e);
    }
};
