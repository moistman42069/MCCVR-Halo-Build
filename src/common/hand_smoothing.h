#pragma once
// HAND SMOOTHING: a 1 Euro filter on the tracked controllers, where their poses enter the mod.
//
// Ported from the Halo: Campaign Evolved VR mod's core/HandSmooth (same filter, same units; its defaults except
// the two steadiness cutoffs, below), which
// took it from Casiez, Roussel & Vogel's 1 Euro filter (CHI 2012); VRExpansionPlugin runs the same family on its
// grip controllers. A still hand is held steady (a low cutoff), and the cutoff rises with the hand's speed, so a
// fast move passes through with little lag.
//
// WHERE IT SITS. It is a layer in the pose read, not a change to any consumer: the OpenXR frame thread locates
// both controllers once per frame (vr.cpp, CaptureRightControllerPose), this filter steps once on that sample,
// and everything downstream (aim and shots, the arms and hands, two-handed holds, holsters, reload, the menu
// pointer) reads the same steadied pair. The controllers' VELOCITIES are taken before it and are not touched:
// swing melee and arm-swing running are tuned on raw motion. The headset is never smoothed here.
//
// THE FRAME. It runs on the runtime's own tracking-space pose (metres, OpenXR LOCAL), before handedness, the
// recenter, the turn stick or the world scale are applied downstream, so a snap turn or a recenter never passes
// through the filter.
//
// Position and rotation have separate parameters, and each cutoff is driven by the speed's MAGNITUDE (a
// diagonal move is smoothed like the same move along one axis).
//
// Pure: no game, engine or OpenXR includes. Quaternions are x, y, z, w.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace hand_smoothing
{
// THE DEFAULTS. Campaign Evolved's numbers, except the two steadiness cutoffs: 1.3 Hz here, 1.0 Hz there
// (tuned by play in a headset): a little less steadying of a still hand for a little less lag on a slow
// aim. kCampaignEvolved* keep that mod's pair for reference.
inline constexpr float kCampaignEvolvedPosMinHz = 1.0f, kCampaignEvolvedRotMinHz = 1.0f;
inline constexpr float kDefaultPosMinHz = 1.3f;     // a still hand's position cutoff
inline constexpr float kDefaultPosBeta = 500.0f;    // Hz of cutoff per metre a second
inline constexpr float kDefaultRotMinHz = 1.3f;     // a still hand's rotation cutoff
inline constexpr float kDefaultRotBeta = 5.0f;      // Hz of cutoff per radian a second
inline constexpr float kDefaultSpeedCutoffHz = 10.0f;
// The config's ranges (the same as Campaign Evolved's keys accept).
inline constexpr float kMinHzMax = 30.0f, kPosBetaMax = 100000.0f, kRotBetaMax = 1000.0f;
inline constexpr float kSpeedCutoffMin = 0.1f, kSpeedCutoffMax = 100.0f;
// No step for this long (a loading hitch, tracking lost): restart at the raw pose instead of easing across
// the gap from where the hand used to be.
inline constexpr int64_t kStaleNs = 100'000'000;
// The same raw sample again for no longer than this is a frame that outran the tracker: the filter waits for
// the next real sample. Longer, and the hand is being reported exactly still (a controller at rest whose
// runtime repeats its pose, a simulator): the filter then goes on settling onto that pose. Campaign Evolved
// always waits; there a hand that stops dead stays wherever the filter has got to (measured: 2 degrees
// short of a pose a simulated controller jumped to and then held).
inline constexpr int64_t kRepeatHoldNs = 25'000'000;

struct Params
{
    float posMinHz = kDefaultPosMinHz;   // 0 = position is not smoothed
    float posBeta = kDefaultPosBeta;
    float rotMinHz = kDefaultRotMinHz;   // 0 = rotation is not smoothed
    float rotBeta = kDefaultRotBeta;
    float speedCutoffHz = kDefaultSpeedCutoffHz;
};

struct State
{
    bool have = false;
    int64_t timeNs = 0;
    float rawPos[3]{}, rawRot[4]{0.0f, 0.0f, 0.0f, 1.0f};   // the raw sample last stepped on
    float pos[3]{}, rot[4]{0.0f, 0.0f, 0.0f, 1.0f};         // the filtered pose
    float dpos[3]{}, drot[3]{};                             // filtered velocity: m/s, and rad/s as axis * rate
};

inline float Clamp(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
inline bool Finite3(const float* v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
inline bool FiniteUnit(const float* q)
{
    const float m2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return std::isfinite(m2) && m2 > 0.9f && m2 < 1.1f;
}
inline float Length3(const float* v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// The 1 Euro filter's smoothing factor for cutoff fc (Hz) over dt (s): alpha = 1 / (1 + tau / dt),
// tau = 1 / (2 pi fc).
inline float Alpha(float cutoffHz, float dt)
{
    if (!(cutoffHz > 0.0f) || !(dt > 0.0f))
        return 0.0f;
    const float tau = 1.0f / (6.28318530718f * cutoffHz);
    return 1.0f / (1.0f + tau / dt);
}

inline void QuatMul(const float* a, const float* b, float* out)   // out = a * b
{
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}
inline void QuatConj(const float* q, float* out) { out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3]; }
inline void QuatNormalize(float* q)
{
    const float m = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (m > 1e-12f)
        for (int i = 0; i < 4; ++i)
            q[i] /= m;
    else
    {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
    }
}
// The rotation vector (axis * angle, radians) of a unit quaternion, shortest arc.
inline void QuatLog(const float* quat, float* out)
{
    float q[4]{quat[0], quat[1], quat[2], quat[3]};
    if (q[3] < 0.0f)
        for (float& c : q)
            c = -c;
    const float s = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    const float k = s < 1e-7f ? 2.0f : 2.0f * std::atan2(s, q[3]) / s;
    out[0] = q[0] * k; out[1] = q[1] * k; out[2] = q[2] * k;
}
// From a towards b by t along the shorter arc.
inline void QuatSlerpShort(const float* a, const float* b, float t, float* out)
{
    float to[4]{b[0], b[1], b[2], b[3]};
    float dot = a[0] * to[0] + a[1] * to[1] + a[2] * to[2] + a[3] * to[3];
    if (dot < 0.0f)
    {
        dot = -dot;
        for (float& c : to)
            c = -c;
    }
    float ka = 1.0f - t, kb = t;
    if (dot < 0.9995f)
    {
        const float angle = std::acos(dot), sine = std::sin(angle);
        ka = std::sin((1.0f - t) * angle) / sine;
        kb = std::sin(t * angle) / sine;
    }
    for (int i = 0; i < 4; ++i)
        out[i] = ka * a[i] + kb * to[i];
    QuatNormalize(out);
}

// Advance `state` with one raw sample taken at `timeNs`. True = the filter stepped; false = it was (re)started
// on this sample, or this is not a new sample (the same pose again within kRepeatHoldNs, or no time has passed)
// and the filtered pose stands. `rawRot` must be a unit quaternion and everything finite (the caller checks: FiniteUnit, Finite3).
inline bool Step(State& state, const float* rawPos, const float* rawRot, int64_t timeNs, const Params& params)
{
    if (!state.have || timeNs - state.timeNs > kStaleNs || timeNs < state.timeNs)
    {
        state.have = true;
        state.timeNs = timeNs;
        std::memcpy(state.rawPos, rawPos, sizeof(state.rawPos));
        std::memcpy(state.rawRot, rawRot, sizeof(state.rawRot));
        std::memcpy(state.pos, rawPos, sizeof(state.pos));
        std::memcpy(state.rot, rawRot, sizeof(state.rot));
        state.dpos[0] = state.dpos[1] = state.dpos[2] = 0.0f;
        state.drot[0] = state.drot[1] = state.drot[2] = 0.0f;
        return false;
    }
    // The runtime has not produced a new sample since the last step: wait, so the next step spans the real
    // interval instead of differentiating a zero and then a double. A pose repeated for longer than
    // kRepeatHoldNs is a hand reported still: step on it (its speed is zero), so the filter settles.
    if (std::memcmp(rawPos, state.rawPos, sizeof(state.rawPos)) == 0 &&
        std::memcmp(rawRot, state.rawRot, sizeof(state.rawRot)) == 0 && timeNs - state.timeNs <= kRepeatHoldNs)
        return false;
    const float dt = static_cast<float>(timeNs - state.timeNs) * 1e-9f;
    if (!(dt > 0.0f))
        return false;
    const float speedAlpha = Alpha(Clamp(params.speedCutoffHz, kSpeedCutoffMin, kSpeedCutoffMax), dt);
    const float inv = 1.0f / dt;

    if (params.posMinHz > 0.0f)
    {
        for (int i = 0; i < 3; ++i)
            state.dpos[i] += ((rawPos[i] - state.rawPos[i]) * inv - state.dpos[i]) * speedAlpha;
        const float alpha = Alpha(params.posMinHz + params.posBeta * Length3(state.dpos), dt);
        for (int i = 0; i < 3; ++i)
            state.pos[i] += (rawPos[i] - state.pos[i]) * alpha;
    }
    else
    {
        std::memcpy(state.pos, rawPos, sizeof(state.pos));
        state.dpos[0] = state.dpos[1] = state.dpos[2] = 0.0f;
    }

    if (params.rotMinHz > 0.0f)
    {
        float previousInverse[4], delta[4], turned[3];
        QuatConj(state.rawRot, previousInverse);
        QuatMul(rawRot, previousInverse, delta);   // this step's rotation, in the tracking frame
        QuatLog(delta, turned);
        for (int i = 0; i < 3; ++i)
            state.drot[i] += (turned[i] * inv - state.drot[i]) * speedAlpha;
        const float alpha = Alpha(params.rotMinHz + params.rotBeta * Length3(state.drot), dt);
        float next[4];
        QuatSlerpShort(state.rot, rawRot, alpha, next);
        std::memcpy(state.rot, next, sizeof(state.rot));
    }
    else
    {
        std::memcpy(state.rot, rawRot, sizeof(state.rot));
        state.drot[0] = state.drot[1] = state.drot[2] = 0.0f;
    }

    std::memcpy(state.rawPos, rawPos, sizeof(state.rawPos));
    std::memcpy(state.rawRot, rawRot, sizeof(state.rawRot));
    state.timeNs = timeNs;
    return true;
}

// Which hands are steadied: 0 both, 1 only the hand that aims, 2 only the other hand.
inline bool HandWanted(int hands, bool isAimHand)
{
    return hands == 0 || (hands == 1 && isAimHand) || (hands == 2 && !isAimHand);
}
} // namespace hand_smoothing
