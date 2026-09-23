#pragma once
#include "runtime_types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

struct PhysicalCrouchCameraRequest
{
    GameTitle title=GameTitle::None;
    uint32_t generation{};
    uint64_t epoch{},at{};
    bool available{},requested{};
};

// Retain ownership while the native crouch blend returns to standing, but
// never retain it through tracking loss, a new unit, or a stale input receipt.
class PhysicalCrouchCameraLease
{
public:
    float Update(const PhysicalCrouchCameraRequest& request,GameTitle title,
        uint32_t generation,uint64_t epoch,uint64_t now,uint32_t owner,
        uintptr_t storage,float nativeReduction,float physicalDown) noexcept
    {
        if(!request.available||request.title!=title||!generation||request.generation!=generation||
            !epoch||request.epoch!=epoch||!request.at||now<request.at||now-request.at>100||
            owner==UINT32_MAX||!(owner>>16)||!storage||
            !std::isfinite(nativeReduction)||nativeReduction<0.f||
            !std::isfinite(physicalDown))
        {Reset();return 0.f;}
        if(title!=title_||generation!=generation_||epoch!=epoch_||owner!=owner_||storage!=storage_)
        {
            Reset();title_=title;generation_=generation;epoch_=epoch;owner_=owner;storage_=storage;
        }
        if(request.requested) engaged_=true;
        else if(nativeReduction<=0.f) engaged_=false;
        if(!engaged_) return 0.f;
        // The existing tracked Y displacement is downward by this amount.
        // Never raise the final view above the native camera/ceiling result.
        return std::min(nativeReduction,std::max(0.f,physicalDown));
    }
    void Reset() noexcept {*this=PhysicalCrouchCameraLease{};}
private:
    GameTitle title_=GameTitle::None;
    uint32_t generation_{},owner_=UINT32_MAX;
    uint64_t epoch_{};
    uintptr_t storage_{};
    bool engaged_{};
};
