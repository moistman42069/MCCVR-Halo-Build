#pragma once
#include <cstdint>

// Why a Halo 3 first-person palette was drawn with the game's own arms instead of the controller-posed ones.
// The palette hook poses the arms only when it finds the interpolation context that the same thread built for
// that exact bone bank; each outcome below names the step that was missing. Counted per submission and reported
// from Present, never logged from the hook.
namespace fp_arm_fallback
{
enum class Outcome : uint8_t
{
    Posed,                // the arms followed the controllers
    SolveFailed,          // the context was there; the controller pose or the solve was not
    RepeatSubmission,     // a second palette from a bank this thread already used this frame
    NoInterpolation,      // no first-person interpolation ran on this thread since its last palette
    InterpolationFailed,  // the game's interpolation ran and returned no bones
    ContextRejected,      // bones came back but did not match the skeleton the arm solve knows
    SourceMismatch,       // this thread interpolated a different bank than the one being drawn
    Count
};

// What the palette's own thread saw since its previous palette.
struct ThreadTrace
{
    uint32_t interpolations = 0;
    bool lastResult = false;
    bool lastContextBuilt = false;
    const void* lastConsumed = nullptr;
    uint64_t consumedFrame = 0;  // the frame serial when lastConsumed was used
};

// `frame` is the current frame's serial: a bank used again in the same frame is a repeat; the same bank in a
// later frame with no interpolation in between is a missing interpolation.
inline Outcome Classify(bool contextMatched, bool reconstructed, const ThreadTrace& trace, const void* source,
                        uint64_t frame)
{
    if (contextMatched) return reconstructed ? Outcome::Posed : Outcome::SolveFailed;
    if (source && source == trace.lastConsumed && frame == trace.consumedFrame && trace.interpolations == 0)
        return Outcome::RepeatSubmission;
    if (trace.interpolations == 0) return Outcome::NoInterpolation;
    if (!trace.lastResult) return Outcome::InterpolationFailed;
    if (!trace.lastContextBuilt) return Outcome::ContextRejected;
    return Outcome::SourceMismatch;
}

inline const char* Name(Outcome outcome)
{
    switch (outcome)
    {
    case Outcome::Posed: return "posed";
    case Outcome::SolveFailed: return "solve-failed";
    case Outcome::RepeatSubmission: return "repeat";
    case Outcome::NoInterpolation: return "no-interpolation";
    case Outcome::InterpolationFailed: return "interpolation-failed";
    case Outcome::ContextRejected: return "context-rejected";
    case Outcome::SourceMismatch: return "source-mismatch";
    default: return "?";
    }
}
}
