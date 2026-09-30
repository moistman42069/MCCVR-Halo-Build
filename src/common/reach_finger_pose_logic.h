#pragma once

#include "controller_finger_pose_logic.h"

#include <cstdint>

// Official HREK Reach first-person render-model identities. These are the
// output-palette node indices in the model's own render_model tag, not the
// animation-source indices from its boneMap.
namespace reach_fingers {
inline constexpr uint32_t kSpartanChecksum=0x181E0B17u;
inline constexpr uint32_t kEliteChecksum=0x19021311u;
inline constexpr unsigned kSpartanNodeCount=47;
inline constexpr unsigned kEliteNodeCount=41;
inline constexpr int16_t kSpartanParents[kSpartanNodeCount]{
    -1,0,0,0,1,1,1,6,6,4,4,7,10,7,10,11,11,14,11,11,14,14,14,11,
    14,14,11,16,26,15,25,23,17,24,19,21,20,27,30,29,33,36,32,31,28,34,35};
inline constexpr int16_t kEliteParents[kEliteNodeCount]{
    -1,0,0,0,3,3,3,4,4,6,6,9,7,7,9,14,14,13,14,13,13,14,14,13,
    13,17,19,20,21,22,15,23,18,25,31,28,27,29,26,30,32};
inline constexpr uint16_t kSpartanChains[2][5][4]{
    {{21,35,46,0},{24,33,40,0},{25,30,38,0},{20,36,41,0},{17,32,42,0}},
    {{16,27,37,0},{15,29,39,0},{23,31,43,0},{26,28,44,0},{19,34,45,0}}};
inline constexpr uint8_t kSpartanChainLengths[5]{3,3,3,3,3};
inline constexpr uint16_t kEliteChains[2][5][4]{
    {{19,26,38,0},{20,27,36,0},{0,0,0,0},{17,25,33,0},{23,31,34,0}},
    {{22,29,37,0},{18,32,40,0},{0,0,0,0},{21,28,35,0},{15,30,39,0}}};
inline constexpr uint8_t kEliteChainLengths[5]{3,3,0,3,3};
inline constexpr uint32_t kSecondaryWeaponEvidenceBit=1u;
inline bool Describe(uint32_t checksum,unsigned count,unsigned side,finger_joint::Inventory& out) noexcept {
    if(side>1)return false;
    const finger_joint::RigKey key{GameTitle::HaloReach,checksum,uint16_t(count),finger_joint::Palette::FirstPerson};
    if(checksum==kSpartanChecksum&&count==kSpartanNodeCount)
        return finger_joint::Build(key,side,side?11u:14u,kSpartanParents,kSpartanChains[side],kSpartanChainLengths,finger_joint::NamedDigits,out);
    if(checksum==kEliteChecksum&&count==kEliteNodeCount)
        return finger_joint::Build(key,side,side?14u:13u,kEliteParents,kEliteChains[side],kEliteChainLengths,finger_joint::NamedDigits,out);
    return false;
}

inline bool FreeSupportPoseAdmitted(bool experimental,bool rigIdentityValid,
    bool localWeaponEvidenceValid,bool secondaryWeaponPresent,bool supportAttached,
    bool twoHandAim,bool exactPreparedSample,bool primarySlot) noexcept
{
    return experimental&&rigIdentityValid&&localWeaponEvidenceValid&&
        !secondaryWeaponPresent&&!supportAttached&&!twoHandAim&&
        exactPreparedSample&&primarySlot;
}

template<class Transform,size_t MaxNodes>
inline bool Apply(uint32_t checksum,unsigned nodeCount,unsigned anatomicalSide,
    const Transform* source,const float (&palmDown)[3],
    const ControllerFingerInput& input,Transform (&output)[MaxNodes],
    const Transform* inverseBind=nullptr) noexcept
{
    if(anatomicalSide>1)return false;
    finger_joint::Inventory inventory{};
    if(!Describe(checksum,nodeCount,anatomicalSide,inventory))return false;
    if(checksum==kSpartanChecksum&&nodeCount==kSpartanNodeCount)
        return controller_finger_pose::Apply(source,nodeCount,
            anatomicalSide?11u:14u,kSpartanParents,
            kSpartanChains[anatomicalSide],kSpartanChainLengths,
            palmDown,input,output,inverseBind,&inventory);
    if(checksum==kEliteChecksum&&nodeCount==kEliteNodeCount)
        return controller_finger_pose::Apply(source,nodeCount,
            anatomicalSide?14u:13u,kEliteParents,
            kEliteChains[anatomicalSide],kEliteChainLengths,
            palmDown,input,output,inverseBind,&inventory);
    return false;
}
} // namespace reach_fingers
