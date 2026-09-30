#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Controller-driven finger posing inputs, not optical hand-joint tracking.
// Capture before action remapping/consumption so Unbound and menu actions do
// not rewrite the physical trigger/grip shape used by an avatar.
struct ControllerFingerInput
{
    bool valid=false;
    float trigger=0,grip=0;
};
// Input handedness alone does not exchange anatomical mesh ownership. The
// native/VR palette's completed hand-alignment route is the authority.
inline unsigned FingerAnatomicalSupportSide(bool handAlignment) noexcept
{
    return handAlignment?1u:0u;
}
inline ControllerFingerInput MakeControllerFingerInput(bool triggerActive,float trigger,
    bool gripActive,float grip) noexcept
{
    if(!triggerActive||!gripActive||!std::isfinite(trigger)||!std::isfinite(grip)||
        trigger<-.01f||trigger>1.01f||grip<-.01f||grip>1.01f)return {};
    return {true,std::clamp(trigger,0.f,1.f),std::clamp(grip,0.f,1.f)};
}
struct ControllerFingerSample
{
    uint64_t preparedSerial=0;
    int64_t displayTime=0;
    uint64_t referenceEpoch=0;
    bool leftHanded=false;
    ControllerFingerInput physicalHands[2]{}; // left, right
};
inline bool SelectControllerFingerInputs(const ControllerFingerSample& sample,
    uint64_t preparedSerial,int64_t displayTime,uint64_t referenceEpoch,
    bool leftHanded,bool trackingFresh,
    ControllerFingerInput (&semanticHands)[2]) noexcept
{
    semanticHands[0]=semanticHands[1]={};
    if(!trackingFresh||!preparedSerial||sample.preparedSerial!=preparedSerial||
        !displayTime||!referenceEpoch||sample.displayTime!=displayTime||
        sample.referenceEpoch!=referenceEpoch||sample.leftHanded!=leftHanded)return false;
    semanticHands[0]=sample.physicalHands[leftHanded?1:0]; // support
    semanticHands[1]=sample.physicalHands[leftHanded?0:1]; // primary
    return semanticHands[0].valid||semanticHands[1].valid;
}
