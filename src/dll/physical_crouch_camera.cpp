#include "physical_crouch_camera.h"
#include <windows.h>
#include <atomic>

namespace {
std::atomic<uint64_t> sequence{},identity{},epoch{},sampleAt{};
std::atomic<uint64_t> invalidation{},publishedInvalidation{};
std::atomic<bool> available{},requested{};
}

void PhysicalCrouchCamera_Publish(GameTitle title,uint32_t generation,
    uint64_t trackingEpoch,uint64_t nowMs,bool allowed,bool crouched) noexcept
{
    const auto admitted=invalidation.load(std::memory_order_acquire);
    uint64_t previous=sequence.load(std::memory_order_acquire);
    if((previous&1)||!sequence.compare_exchange_strong(previous,previous+1,
        std::memory_order_acq_rel)) return;
    identity.store((uint64_t(generation)<<8)|uint8_t(title),std::memory_order_relaxed);
    epoch.store(trackingEpoch,std::memory_order_relaxed);
    sampleAt.store(nowMs,std::memory_order_relaxed);
    requested.store(crouched,std::memory_order_relaxed);
    available.store(allowed,std::memory_order_relaxed);
    publishedInvalidation.store(admitted,std::memory_order_relaxed);
    sequence.store(previous+2,std::memory_order_release);
}

void PhysicalCrouchCamera_Invalidate() noexcept
{
    // Unlike a competing publication, invalidation must never be dropped.
    // Its independent epoch prevents a concurrent writer from reviving it.
    invalidation.fetch_add(1,std::memory_order_acq_rel);
}

bool PhysicalCrouchCamera_Read(PhysicalCrouchCameraRequest& result) noexcept
{
    result={};
    const auto admission=invalidation.load(std::memory_order_acquire);
    const auto before=sequence.load(std::memory_order_acquire);
    if(before&1) return false;
    const auto key=identity.load(std::memory_order_relaxed);
    PhysicalCrouchCameraRequest candidate{static_cast<GameTitle>(key&255),
        static_cast<uint32_t>(key>>8),epoch.load(std::memory_order_relaxed),
        sampleAt.load(std::memory_order_relaxed),available.load(std::memory_order_acquire),
        requested.load(std::memory_order_relaxed)};
    if(sequence.load(std::memory_order_acquire)!=before||!candidate.available||
        publishedInvalidation.load(std::memory_order_relaxed)!=admission||
        invalidation.load(std::memory_order_acquire)!=admission) return false;
    result=candidate;return true;
}

#if HALOMCCVR_DEV_DIAGNOSTICS
namespace {
constexpr size_t kWitnessSlots=8;
std::atomic<float> witnessDrop[kWitnessSlots]{};
std::atomic<uint64_t> witnessAt[kWitnessSlots]{};
std::atomic<uint32_t> missWhy[kWitnessSlots]{};
std::atomic<uint64_t> missAt[kWitnessSlots]{};
std::atomic<float> appliedWu[kWitnessSlots]{};
std::atomic<uint64_t> appliedAt[kWitnessSlots]{};
}
void PhysicalCrouchCamera_NoteApplied(GameTitle title,float correctionWu) noexcept
{
    const size_t slot=static_cast<size_t>(title)%kWitnessSlots;
    appliedWu[slot].store(correctionWu,std::memory_order_relaxed);
    appliedAt[slot].store(GetTickCount64(),std::memory_order_release);
}
bool PhysicalCrouchCamera_Applied(GameTitle title,uint64_t maxAgeMs,float& correctionWu) noexcept
{
    const size_t slot=static_cast<size_t>(title)%kWitnessSlots;
    const uint64_t at=appliedAt[slot].load(std::memory_order_acquire),now=GetTickCount64();
    correctionWu=appliedWu[slot].load(std::memory_order_relaxed);
    return at&&now>=at&&now-at<=maxAgeMs;
}
void PhysicalCrouchCamera_NoteNativeMiss(GameTitle title,uint32_t why) noexcept
{
    const size_t slot=static_cast<size_t>(title)%kWitnessSlots;
    missWhy[slot].store(why,std::memory_order_relaxed);
    missAt[slot].store(GetTickCount64(),std::memory_order_release);
}
uint32_t PhysicalCrouchCamera_NativeMiss(GameTitle title,uint64_t maxAgeMs) noexcept
{
    const size_t slot=static_cast<size_t>(title)%kWitnessSlots;
    const uint64_t at=missAt[slot].load(std::memory_order_acquire),now=GetTickCount64();
    return at&&now>=at&&now-at<=maxAgeMs ? missWhy[slot].load(std::memory_order_relaxed) : 0u;
}
void PhysicalCrouchCamera_NoteNativeDrop(GameTitle title,float dropWu) noexcept
{
    const size_t slot=static_cast<size_t>(title)%kWitnessSlots;
    witnessDrop[slot].store(dropWu,std::memory_order_relaxed);
    witnessAt[slot].store(GetTickCount64(),std::memory_order_release);
}
bool PhysicalCrouchCamera_NativeDrop(GameTitle title,uint64_t maxAgeMs,float& dropWu) noexcept
{
    const size_t slot=static_cast<size_t>(title)%kWitnessSlots;
    const uint64_t at=witnessAt[slot].load(std::memory_order_acquire),now=GetTickCount64();
    dropWu=witnessDrop[slot].load(std::memory_order_relaxed);
    return at&&now>=at&&now-at<=maxAgeMs;
}
#endif
