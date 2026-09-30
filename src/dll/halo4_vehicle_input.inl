// Optional, read-only H4 seat admission, independent of contact-melee hooks.
#include "halo4_vehicle_input_bindings.generated.h"
using Halo4OutputUnitFn=uint32_t(__fastcall*)(uint32_t);
struct Halo4VehicleInputFeature
{
    std::atomic<bool> ready{false};
    std::atomic<uint32_t> callbacks{0};
    uint32_t generation{};
    Halo4OutputUnitFn outputUnit{};
    std::atomic<uint64_t> seated{0},unknown{0},faults{0};
} g_halo4VehicleInput;

inline void Halo4VehicleInputSetFailure(const char** reason,const char* text) noexcept
{
    if (reason) *reason=text;
}

// Diagnostics-only variant: identical reads, guards and counters, but every
// rejection exit names itself through `reason` (static literals; never
// formatted or logged here - the reader stays a hot path). The shipping
// wrapper below passes nullptr and is otherwise unchanged.
__declspec(noinline) bool Halo4ReadVehicleInputEx(Halo4VehicleInputState& output,
    const char** reason) noexcept
{
    if (reason) *reason=nullptr;
    auto& feature=g_halo4VehicleInput;
    feature.callbacks.fetch_add(1,std::memory_order_acq_rel);
    bool valid=false;
    __try
    {
        __try
        {
            if (!feature.ready.load(std::memory_order_acquire))
            { Halo4VehicleInputSetFailure(reason,"feature-not-ready"); __leave; }
            if (TitleAdapter_GetActiveTitle()!=GameTitle::Halo4)
            { Halo4VehicleInputSetFailure(reason,"title"); __leave; }
            if (!g_halo4Camera.armed.load(std::memory_order_acquire))
            { Halo4VehicleInputSetFailure(reason,"camera"); __leave; }
            if (g_halo4Camera.teardownRequested.load(std::memory_order_acquire))
            { Halo4VehicleInputSetFailure(reason,"teardown"); __leave; }
            if (feature.generation!=g_halo4Camera.generation.load(std::memory_order_acquire)||
                feature.generation!=TitleAdapter_GetGeneration(GameTitle::Halo4))
            { Halo4VehicleInputSetFailure(reason,"generation"); __leave; }
            if (!g_halo4EngineTlsIndex||*g_halo4EngineTlsIndex>=1088)
            { Halo4VehicleInputSetFailure(reason,"tls"); __leave; }
            if (ReadHalo4CinematicControl()!=CinematicControlState::PlayerControlled)
            { Halo4VehicleInputSetFailure(reason,"cinematic"); __leave; }
            auto** slots=reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
            const auto* tls=slots?slots[*g_halo4EngineTlsIndex]:nullptr;
            if (!tls) { Halo4VehicleInputSetFailure(reason,"tls"); __leave; }
            if (!feature.outputUnit)
            { Halo4VehicleInputSetFailure(reason,"output-unit"); __leave; }
            const auto unit=feature.outputUnit(0);
            Halo4VehicleInputState next{},latest{};
            if (!Halo4ReadVehicleInputMemory(tls,unit,next)||
                feature.outputUnit(0)!=unit||!Halo4ReadVehicleInputMemory(tls,unit,latest))
            { Halo4VehicleInputSetFailure(reason,"memory-or-mapping"); __leave; }
            if (next.parent!=latest.parent||next.seat!=latest.seat||
                !feature.ready.load(std::memory_order_acquire)||
                g_halo4Camera.teardownRequested.load(std::memory_order_acquire)||
                feature.generation!=TitleAdapter_GetGeneration(GameTitle::Halo4))
            { Halo4VehicleInputSetFailure(reason,"unstable"); __leave; }
            output=next;valid=true;
            if (next.seated) feature.seated.fetch_add(1,std::memory_order_relaxed);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            Halo4VehicleInputSetFailure(reason,"fault");
            feature.faults.fetch_add(1,std::memory_order_relaxed);
        }
        if (!valid) feature.unknown.fetch_add(1,std::memory_order_relaxed);
    }
    __finally { feature.callbacks.fetch_sub(1,std::memory_order_acq_rel); }
    return valid;
}

__declspec(noinline) bool Halo4ReadVehicleInput(Halo4VehicleInputState& output) noexcept
{
    const bool valid=Halo4ReadVehicleInputEx(output,nullptr);
    // The post-call store is deliberate, not leftover: RemoveHalo4VehicleInput
    // hands this symbol to WaitForNativeDetourQuiescence, which resolves its
    // code range through RtlLookupFunctionEntry. A bare
    // `return Halo4ReadVehicleInputEx(...)` collapses into an unwind-less leaf
    // tail jump and retirement would then refuse to drain.
    volatile bool retained=valid;
    return retained;
}

bool RemoveHalo4VehicleInput()
{
    auto& feature=g_halo4VehicleInput;
    feature.ready.store(false,std::memory_order_release);
    if (!feature.generation) return true;
    const void* functions[]{reinterpret_cast<const void*>(&Halo4ReadVehicleInput)};
    const void* originals[]{nullptr};
    if (!WaitForNativeDetourQuiescence(functions,originals,1,feature.callbacks)) return false;
    feature.outputUnit=nullptr;feature.generation=0;return true;
}

bool InstallHalo4VehicleInput(uintptr_t base,size_t size,uint32_t generation)
{
    auto& feature=g_halo4VehicleInput;
    if (feature.generation||!generation||!g_halo4EngineTlsIndex) return false;
    for (const auto& binding:kHalo4VehicleInputBindings)
    {
        const auto hit=sig::Find(base,size,binding.pattern);
        if (hit!=base+binding.rva||sig::Find(hit+1,base+size-hit-1,binding.pattern))
        {
            LOG("Halo 4 vehicle input stock fallback: missing/ambiguous +0x%llX; camera and on-foot input retained",
                static_cast<unsigned long long>(binding.rva));return false;
        }
    }
    const auto tlsIndex=base+0x95bfa+*reinterpret_cast<const int32_t*>(base+0x95bf6);
    if (tlsIndex!=reinterpret_cast<uintptr_t>(g_halo4EngineTlsIndex))
    { LOG("Halo 4 vehicle input stock fallback: native TLS identity mismatch");return false; }
    feature.outputUnit=reinterpret_cast<Halo4OutputUnitFn>(base+0x95bb4);
    feature.generation=generation;
    feature.ready.store(true,std::memory_order_release);
    LOG("Halo 4 vehicle input verified: local output/input ownership and native seat identity; seated throttle remains unrotated");
    return true;
}
