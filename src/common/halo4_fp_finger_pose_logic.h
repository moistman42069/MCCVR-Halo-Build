#pragma once
#include "anatomical_palette_logic.h"
#include "controller_finger_pose_logic.h"
#include "halo4_fp_finger_bind.generated.h"

// Same experience as H3: only the free anatomical support hand opens/points/
// curls. Runtime establishes exact local ownership and keeps authored grips.
inline constexpr uint16_t kHalo4FpFingerChains[2][5][4]{
    {{46,61,70,0},{48,62,75,0},{43,59,69,0},{57,64,73,0},{39,60,74,79}},
    {{44,63,71,0},{41,67,76,0},{49,66,72,0},{56,58,77,0},{40,65,68,78}}};
inline constexpr uint8_t kHalo4FpFingerLengths[5]{3,3,3,3,4};
inline bool Halo4DescribeFpFingers(uint32_t checksum,unsigned count,unsigned side,finger_joint::Inventory& out) noexcept {
    if(checksum!=kHalo4StormFpRuntimeImportChecksum||count!=80||side>1)return false;
    return finger_joint::Build({GameTitle::Halo4,checksum,80,finger_joint::Palette::FirstPerson},side,
        side?29u:37u,kHalo4FpFingerParents,kHalo4FpFingerChains[side],kHalo4FpFingerLengths,finger_joint::NamedDigits,out);
}
inline bool Halo4PoseFreeFpFingers(const Halo4FloatingTransform* source,
    uint32_t checksum,unsigned count,unsigned side,const ControllerFingerInput& input,
    Halo4FloatingTransform (&output)[80]) noexcept
{
    if(!source||checksum!=kHalo4StormFpRuntimeImportChecksum||count!=80||side>1)return false;
    Halo4FloatingTransform bind[80]{};
    std::memcpy(bind,source,sizeof(bind));
    if(!input.valid){std::memcpy(output,source,sizeof(bind));return true;}
    const unsigned wrist=side?29u:37u;
    for(const auto& local:kHalo4FpFingerBind) {
        if(!controller_finger_pose::IsDescendant(kHalo4FpFingerParents,80,local.node,wrist))continue;
        Halo4FloatingTransform transform{},world{};
        if(!BuildAnatomicalPalmMarker(local.position,local.quaternion,transform)||
           !Halo4ComposeFloatingTransforms(bind[local.parent],transform,world))return false;
        bind[local.node]=world;
    }
    AnatomicalPalmMarkers markers{};Halo4FloatingTransform palm{};
    if(!Halo4AnatomicalPalmMarkers(markers)||
       !Halo4ComposeFloatingTransforms(bind[wrist],side?markers.right:markers.left,palm))return false;
    const float down[3]{-palm.rotation[6],-palm.rotation[7],-palm.rotation[8]};
    finger_joint::Inventory inventory{};if(!Halo4DescribeFpFingers(checksum,count,side,inventory))return false;
    return controller_finger_pose::Apply(bind,80,wrist,kHalo4FpFingerParents,
        kHalo4FpFingerChains[side],kHalo4FpFingerLengths,down,input,output,static_cast<const Halo4FloatingTransform*>(nullptr),&inventory);
}
