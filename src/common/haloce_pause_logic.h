#pragma once
#include <cstdint>

namespace halo_ce
{
enum class PauseRequest { None, Enter, Exit };

// Match Halo 3's native-state correction delay. Observe the requested target,
// not the fading presentation: repeatedly requesting the same target would
// restart the comfort fade indefinitely. Unknown samples never imply resume.
struct NativePausePresentation
{
    uint32_t generation{};
    uint64_t mismatchSince{};
    bool mismatchPending{},mismatchValue{};

    PauseRequest ObserveOwned(uint32_t currentGeneration,bool presentationOwned,
        bool known,bool paused,bool targetPaused,uint64_t now) noexcept
    {
        // Ownership participates in reconciliation even when the native
        // clock is no longer readable. The CE module can stay resident in
        // MCC's shell after its camera heartbeat and controls have retired.
        // A retained pause bit belongs to that old level, not the shell.
        if (!presentationOwned)
        {
            *this={};generation=currentGeneration;
            return targetPaused?PauseRequest::Exit:PauseRequest::None;
        }
        return Observe(currentGeneration,known,paused,targetPaused,now);
    }

    PauseRequest Observe(uint32_t currentGeneration,bool known,bool paused,
        bool targetPaused,uint64_t now) noexcept
    {
        if (generation!=currentGeneration)
        { *this={};generation=currentGeneration; }
        if (!currentGeneration||!known||paused==targetPaused)
        { mismatchPending=false;return PauseRequest::None; }
        if (!mismatchPending||mismatchValue!=paused||now<mismatchSince)
        {
            mismatchPending=true;mismatchValue=paused;mismatchSince=now;
            return PauseRequest::None;
        }
        if (now-mismatchSince<50) return PauseRequest::None;
        mismatchPending=false;
        return paused?PauseRequest::Enter:PauseRequest::Exit;
    }
};

// A local menu does not pause a network simulation. Only an explicit menu
// presentation request can latch input suppression here: suppression by itself
// also occurs in cinematics and is not evidence that a menu opened.
struct RequestedMenuPresentation
{
    NativePausePresentation native;
    uint64_t requestedAt{};
    bool previousTarget{},sawSuppression{};
    PauseRequest Observe(uint32_t generation,bool owned,bool clockKnown,bool paused,
        bool inputKnown,bool suppressed,bool target,uint64_t now) noexcept
    {
        if (!owned||native.generation!=generation)
        { *this={};native.generation=generation; }
        if (target&&!previousTarget) { requestedAt=now;sawSuppression=false; }
        if (!target) sawSuppression=false;
        previousTarget=target;
        if (owned&&target&&inputKnown&&suppressed) sawSuppression=true;
        const bool waiting=target&&!sawSuppression&&now>=requestedAt&&now-requestedAt<500;
        const bool requested=target&&(waiting||(inputKnown&&suppressed));
        // Unknown input after a menu request cannot manufacture a resume.
        const bool known=clockKnown&&(paused||!target||waiting||inputKnown);
        return native.ObserveOwned(generation,owned,known,paused||requested,target,now);
    }
};

// Recent CE eye ownership protects ordinary missed frames from flashing a
// flat image. A completed pause transition explicitly needs the native screen
// and must not wait for that gameplay ownership timeout.
inline bool AllowStockScreen(bool sharedAllows,bool ceOwned,bool paused) noexcept
{ return sharedAllows&&(!ceOwned||paused); }
}
