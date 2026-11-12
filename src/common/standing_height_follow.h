#pragma once
// THE VIEW'S NEUTRAL HEIGHT FOLLOWS THE CONFIRMED STANDING HEIGHT UP.
//
// A recenter captures the head as the view's neutral position. With the physical crouch on it adds how far the
// head is below the player's standing height (VR_RecenterStandingLiftM), so the neutral height is the standing
// one whatever the head was doing. That lift exists only once the standing height is CONFIRMED (calibrated, or a
// minute of standing near it): an unconfirmed one may still fall back, and the lift would outlive it. So every
// recenter in the first minute of a session, the automatic one at a level's start among them, takes the head as
// it is. A player who is crouched, bent over the desk or still sitting down at that moment keeps a neutral height
// that low, and then stands that much above their character, until the next manual recenter.
//
// This is the same rule applied late: once the standing height is confirmed AND the player has stood at it
// (kDwellMs, below), a neutral height more than kStartM below it glides up to it. Up only. A neutral height above the standing one (a recenter on tiptoe, a reference
// that later fell back) is left alone: it is small, and lowering the reference raises the view, which is the
// direction that puts a camera through a ceiling.
//
// Pure: the caller supplies the head height and the reference in ITS frame and the standing-height deficit from
// the crouch's own continuous frame (baseline - height, which is a difference and so carries between frames that
// differ by a vertical translation).
#include <cmath>
#include <cstdint>

namespace standing_follow
{
struct State
{
    bool active = false;
    uint64_t lastMs = 0;
    // THE STANDING HEIGHT HAS TO BE STOOD AT. The height being followed (in the crouch's own frame) and how long
    // the head has been near it since it last changed.
    float baseline = 0.0f;
    uint64_t dwellMs = 0;
};

constexpr float kStartM = 0.05f;       // a neutral height this far below standing starts a glide
constexpr float kDoneM = 0.002f;       // and this close ends it
constexpr float kRateMps = 0.30f;      // 30 cm in a second: seen, not felt as a drop
constexpr float kMaxBelowM = 1.0f;     // |baseline - height| past this is not a standing person's sample
constexpr uint64_t kMaxGapMs = 250;    // calls further apart than this are not one motion
// The follow is upward only, so a standing height that was learned too high would be latched into the view for
// good, while the crouch's own reference can still fall back. "Confirmed" alone does not prevent that: the
// learned height keeps its confirmation while it climbs by less than the crouch's release band (some 15 cm),
// and it climbs after five seconds at a higher posture (a headset pushed up on the forehead, a step, a stretch).
// So a height is followed only once the head has spent kDwellMs within kNearM of it since it last moved by more
// than kSameM: the player stands there, they did not just pass through. A real standing height earns this in
// twenty seconds of ordinary play; it also means the glide is one move to the settled height, not a chase of a
// height that is still climbing (such a chase closes part of the gap, stops, and finishes a minute later; 5 cm
// measured).
constexpr float kNearM = 0.03f;
constexpr float kSameM = 0.01f;
constexpr uint64_t kDwellMs = 20'000;

// How far to raise the reference on this call (metres, >= 0). `eligible`: the switch is on, the physical crouch
// is on, the player is on foot and the standing height is confirmed. `baseline` and `height`: the standing
// height and the head's height in the crouch's own continuous frame (not finite = no fresh sample: nothing is
// learned or moved). `headY` and `referenceY` are in the caller's frame. `startedOut`, when given, is set on
// the call that starts a glide (for one log line).
inline float Step(State& state, bool eligible, float headY, float baseline, float height, float referenceY,
                  uint64_t nowMs, bool* startedOut = nullptr) noexcept
{
    if (startedOut)
        *startedOut = false;
    const uint64_t last = state.lastMs;
    state.lastMs = nowMs;
    const float below = baseline - height;   // positive while the head is under the standing height
    if (!std::isfinite(headY) || !std::isfinite(below) || !std::isfinite(baseline) || !std::isfinite(referenceY) ||
        std::fabs(below) > kMaxBelowM)
    {
        state.active = false;
        return 0.0f;
    }
    // One interval of the camera's own calls, or nothing (the first call, a break in the calls).
    const uint64_t interval = last && nowMs > last && nowMs - last <= kMaxGapMs ? nowMs - last : 0;
    if (!(std::fabs(baseline - state.baseline) <= kSameM))
    {
        state.baseline = baseline;   // the standing height moved: it has to be stood at again
        state.dwellMs = 0;
        state.active = false;
    }
    else if (std::fabs(below) <= kNearM && state.dwellMs < kDwellMs)
        state.dwellMs = state.dwellMs + interval < kDwellMs ? state.dwellMs + interval : kDwellMs;
    if (!eligible || state.dwellMs < kDwellMs)
    {
        state.active = false;
        return 0.0f;
    }
    const float gap = headY + below - referenceY;   // the standing height, in the caller's frame, above the reference
    if (!state.active)
    {
        if (!(gap > kStartM))
            return 0.0f;
        state.active = true;
        if (startedOut)
            *startedOut = true;
    }
    if (!(gap > kDoneM))
    {
        state.active = false;
        return 0.0f;
    }
    if (!interval)
        return 0.0f;   // no interval to scale by
    const float step = kRateMps * static_cast<float>(interval) * 0.001f;
    return step < gap ? step : gap;
}
} // namespace standing_follow
