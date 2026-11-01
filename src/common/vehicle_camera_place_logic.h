#pragma once
// WHERE THE CAMERA IN USE IS, THIS FRAME: the selected camera (vehicle_camera_select.h) laid onto the vehicle a
// game reports (vehicle_camera_frame_logic.h has the geometry and the space). What a game supplies is Inputs;
// what it gets back is the world point its seat view is to be seen from, before the player's own head movement.
//
// Pure: no engine, no clock, no allocation. State is one camera hook's own.

#include <cmath>
#include <cstdint>

#include "vehicle_camera_frame_logic.h"
#include "vehicle_camera_select.h"

namespace vehicle_camera_place
{
struct Inputs
{
    // The vehicle as it is DRAWN this frame: a camera placed from a frame the picture is not drawn from shakes
    // against the vehicle at speed.
    float vehicleOrigin[3]{};
    vehicle_camera_frame::Basis vehicle{};
    // How big it is: its bounding sphere's radius in cm, 0 where the game does not say.
    float vehicleRadiusCm = 0.0f;
    // The seat's own point and the seated character's head, where the game has them.
    bool seatValid = false;
    float seat[3]{};
    bool headValid = false;
    float head[3]{};
    // The frame of the part the seat sits on (the node its occupant is attached to), as drawn this frame: a camera
    // measured from the seat or the head lays its offset out in it, so it turns with whatever the seat turns with --
    // a walker's torso, a turret -- where a seat on the hull turns with the hull as before. False: the vehicle's.
    bool seatFrameValid = false;
    vehicle_camera_frame::Basis seatFrame{};
    // The seated character's head as a frame: the seat's frame above, turned as far as the head has turned since
    // it settled in the seat (vehicle_camera_frame::TurnedSince). A camera measured from the head lays its offset
    // out in it, so it turns and nods with the head. False: the seat's frame, as for a camera measured from the seat.
    bool headFrameValid = false;
    vehicle_camera_frame::Basis headFrame{};
    // Where the game's own third-person camera stands for this seat ("origin": "thirdperson"): the point it
    // turns about, and where it stands from that point -- world units along the carrier's forward, LEFT and up --
    // at the boom's angle (vehicle_boom_logic.h), never at the aim's. A game that cannot say leaves restValid
    // false, and such a camera then stands behind the vehicle's origin at a distance of the mod's own.
    bool restValid = false;
    float rest[3]{};
    float restOffset[3]{};
    // The heading the player's room faces (the stick turns it; his head does not): what an orbiting camera
    // ("locationTracking": "view") rides.
    float viewYaw = 0.0f;
};

// The mod's own stand-in for a third-person rest place the game could not give: behind and above the
// vehicle's origin, in cm -- for a vehicle of a few metres. A larger one stands it off by its own size (its bounding
// sphere's radius), since a fixed 4.5 m behind the origin of a 40 m Longsword or a 340 m Lich is inside its hull.
inline constexpr float kStandInBehindCm = 450.0f;
inline constexpr float kStandInAboveCm = 180.0f;
inline constexpr float kStandInBehindPerRadius = 1.5f;
inline constexpr float kStandInAbovePerRadius = 0.5f;
inline void StandInOffsetCm(float radiusCm, float& behindCm, float& aboveCm) noexcept
{
    const float r = std::isfinite(radiusCm) && radiusCm > 0.0f ? radiusCm : 0.0f;
    behindCm = r * kStandInBehindPerRadius > kStandInBehindCm ? r * kStandInBehindPerRadius : kStandInBehindCm;
    aboveCm = r * kStandInAbovePerRadius > kStandInAboveCm ? r * kStandInAbovePerRadius : kStandInAboveCm;
}

struct State
{
    bool latched = false;
    uint32_t changes = 0;
    // The world heading an offset keeps while its mode does not follow the vehicle's yaw: the vehicle's own at
    // the moment the camera or mode was chosen, so the camera starts where a following one would be.
    float freeYaw = 0.0f;
    // The last placement of a "thirdperson" camera used the stand-in above: for the caller's log.
    bool standIn = false;
};

inline bool Finite3(const float v[3]) noexcept
{
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// What else a placement settled, for a caller that needs it: the point the camera is measured from (its origin,
// as this frame resolved it -- a camera pulled in from a wall moves toward it), the frame its offset was laid out
// in, and the offset as laid out, in cm along that frame's forward, right and up (a "thirdperson" camera's
// includes where the game's own stands from its pivot).
struct Placed
{
    float pivot[3]{};
    vehicle_camera_frame::Basis carrier{};
    float offsetCm[3]{};
};

// The camera's point in the world. False (and `out` untouched) when no chase camera is selected or an input is
// not usable: the caller keeps the view it has without this.
inline bool Place(const vehicle_camera_select::ActiveCamera& camera, const Inputs& in, State& state,
                  float out[3], Placed* placed = nullptr) noexcept
{
    using vehicle_camera_file::Origin;
    if (!camera.valid || !camera.chase || !vehicle_camera_frame::Usable(in.vehicle) || !Finite3(in.vehicleOrigin) ||
        !std::isfinite(in.viewYaw))
        return false;

    const bool seatUsable = in.seatValid && Finite3(in.seat);
    const bool headUsable = in.headValid && Finite3(in.head);
    const float* origin = in.vehicleOrigin;
    // The frame the offset is laid out in: the vehicle's, the seat's own for a camera measured from the seat
    // (Inputs::seatFrame), and the head's for one measured from the head (Inputs::headFrame; the seat's where the
    // head's turn is not known).
    const bool seatFrameUsable = in.seatFrameValid && vehicle_camera_frame::Usable(in.seatFrame);
    const bool headFrameUsable = in.headFrameValid && vehicle_camera_frame::Usable(in.headFrame);
    const vehicle_camera_frame::Basis* frame = &in.vehicle;
    float offsetCm[3]{camera.offsetCm[0], camera.offsetCm[1], camera.offsetCm[2]};
    bool standIn = false;
    switch (static_cast<Origin>(camera.origin))
    {
    case Origin::ThirdPerson:
        // Where the game's camera stands from its own turning point at the boom's angle, laid along whatever
        // carries this camera's offset. So the camera stands where the game's would at that look pitch, and does
        // not swing with the aim as the game's does.
        if (in.restValid && Finite3(in.rest) && Finite3(in.restOffset) &&
            vehicle_camera_frame::Dot(in.restOffset, in.restOffset) > 1.0e-8f)
        {
            origin = in.rest;
            offsetCm[0] += in.restOffset[0] / vehicle_camera_frame::kWorldUnitsPerCm;
            offsetCm[1] -= in.restOffset[1] / vehicle_camera_frame::kWorldUnitsPerCm;
            offsetCm[2] += in.restOffset[2] / vehicle_camera_frame::kWorldUnitsPerCm;
        }
        else
        {
            float behindCm = 0.0f, aboveCm = 0.0f;
            StandInOffsetCm(in.vehicleRadiusCm, behindCm, aboveCm);
            offsetCm[0] -= behindCm;
            offsetCm[2] += aboveCm;
            standIn = true;
        }
        break;
    case Origin::Seat:
        if (seatUsable)
        {
            origin = in.seat;
            if (seatFrameUsable)
                frame = &in.seatFrame;
        }
        break;
    case Origin::Head:
        if (headUsable)
        {
            origin = in.head;
            if (headFrameUsable)
                frame = &in.headFrame;
            else if (seatFrameUsable)
                frame = &in.seatFrame;
        }
        else if (seatUsable)
        {
            origin = in.seat;
            if (seatFrameUsable)
                frame = &in.seatFrame;
        }
        break;
    default: // the vehicle, or a marker the game has not resolved
        break;
    }

    if (!state.latched || state.changes != camera.changes)
    {
        state.freeYaw = vehicle_camera_frame::Heading(*frame);
        state.changes = camera.changes;
        state.latched = true;
    }
    const vehicle_camera_frame::Basis carrier =
        camera.locView ? vehicle_camera_frame::FromAngles(in.viewYaw, 0.0f, 0.0f)
                       : vehicle_camera_frame::Tracked(*frame, camera.locYaw, camera.locPitch, camera.locRoll,
                                                       state.freeYaw);
    float offset[3];
    vehicle_camera_frame::OffsetToWorld(carrier, offsetCm, 1.0f, offset);
    const float point[3]{origin[0] + offset[0], origin[1] + offset[1], origin[2] + offset[2]};
    if (!Finite3(point))
        return false;
    state.standIn = standIn;
    out[0] = point[0];
    out[1] = point[1];
    out[2] = point[2];
    if (placed)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            placed->pivot[axis] = origin[axis];
            placed->offsetCm[axis] = offsetCm[axis];
        }
        placed->carrier = carrier;
    }
    return true;
}

// THE LEASH on the player's head (the camera's "leashMin" / "leashMax", vehicle_camera_frame::LeashToBasis):
// `head` is how far, in world units, the head has moved the view from the camera's point; it comes back with
// whatever crosses a limit taken off, measured along the frame the camera's offset is laid out in. `reach` is how
// much of the offset the camera stands at (1, unless a wall holds it in). False (and `head` unchanged) when the
// camera sets no leash.
inline bool LeashHead(const vehicle_camera_select::ActiveCamera& camera, const Placed& placed, float reach,
                      float head[3]) noexcept
{
    if (!vehicle_camera_file::LeashSet(camera.leashMinCm) && !vehicle_camera_file::LeashSet(camera.leashMaxCm))
        return false;
    if (!Finite3(head) || !vehicle_camera_frame::Usable(placed.carrier))
        return false;
    const float at = std::isfinite(reach) ? reach : 1.0f;
    const float cameraCm[3]{placed.offsetCm[0] * at, placed.offsetCm[1] * at, placed.offsetCm[2] * at};
    const vehicle_camera_frame::LeashBox box =
        vehicle_camera_frame::LeashToBasis(camera.leashMinCm, camera.leashMaxCm, cameraCm);
    float shift[3];
    vehicle_camera_frame::LeashShift(head, placed.carrier, box, shift);
    for (int axis = 0; axis < 3; ++axis)
        head[axis] -= shift[axis];
    return true;
}

// HELD SHORT OF A WALL ("collide"): how much of the way from the pivot to the camera's place it may go, from a
// trace along that line. `rangeWu` is where the trace met something (meaningful only with `hit`), `lengthWu` the
// whole way, `marginCm` how far short to stop. 1 = all the way.
inline float CollideReach(bool hit, float rangeWu, float lengthWu, float marginCm) noexcept
{
    if (!hit || !std::isfinite(rangeWu) || !std::isfinite(lengthWu) || !(lengthWu > 0.0f) || rangeWu >= lengthWu)
        return 1.0f;
    const float margin = std::isfinite(marginCm) && marginCm > 0.0f ? marginCm * vehicle_camera_frame::kWorldUnitsPerCm
                                                                      : 0.0f;
    const float allowed = rangeWu - margin;
    return allowed > 0.0f ? allowed / lengthWu : 0.0f;
}

// ...eased: drawn in at once, let out at `releasePerSecond` of the whole way a second, so a gap between two walls
// does not throw the camera out and back. `held` is the caller's, 1 to start.
inline float EaseReach(float& held, float wanted, float seconds, float releasePerSecond) noexcept
{
    if (!std::isfinite(wanted))
        wanted = 1.0f;
    wanted = wanted < 0.0f ? 0.0f : (wanted > 1.0f ? 1.0f : wanted);
    if (!std::isfinite(held))
        held = 1.0f;
    if (wanted <= held)
        held = wanted;
    else
    {
        const float step = std::isfinite(seconds) && seconds > 0.0f ? seconds * releasePerSecond : 0.0f;
        held = held + step >= wanted ? wanted : held + step;
    }
    return held;
}

// LAG ("lag", "lagDamping"): the camera follows its place on a spring and damper instead of being carried there
// rigidly. A camera on a long boom is swung through a wide arc by every turn and bump of the vehicle; the spring
// lets those swings through slowly. It damps against the place's own velocity, so at ANY steady speed it sits on its
// place and only a change of motion lags. That velocity is SMOOTHED over the lag's seconds (steered by the raw one,
// a bump's jolt would reach the camera within a twentieth of a second), and each frame's sample is the MEDIAN of
// the place's last three frame velocities, so a place that steps in one frame does not throw the camera past: the
// step's one wild velocity is never the median. World units.
//
// No speed is too fast to follow and no distance behind is a reason to start over. An earlier version held the
// velocity to about 60 m/s and started over 9 m behind: a flyer faster than that fell behind its place by more
// every frame, snapped onto it, and fell behind again, over and over. Starting over is kept for what is not motion:
// a place that leaves its own course by metres in a single frame (a respawn, a seat change, a source change), a
// gap of half a second, another camera, and a camera that has somehow ended up far from its place.
struct LagState
{
    bool valid = false;
    uint32_t changes = 0; // the camera the state belongs to (ActiveCamera::changes)
    float pos[3]{};
    float vel[3]{};
    float place[3]{};    // the place at the last step, for its velocity
    float placeVel[3]{}; // the place's velocity, smoothed
    float recent[3][3]{}; // the place's last three frame velocities, newest first
    int recentCount = 0;  // how many of them this run has (the camera stands on its place until it has three)
    // What the follower has done, for its caller's log: how often it started over on a jump and on a runaway, and
    // how far behind its place the camera stood after the last step.
    uint32_t jumps = 0;
    uint32_t runaways = 0;
    float trailWu = 0.0f;
    uint32_t limited = 0; // frames the lag's limit held the camera (ActiveCamera::lagLimitCm)
};
inline constexpr float kLagStiffness = 3.0f;      // the spring's frequency is this over the lag's seconds
inline constexpr float kLagSubstepSeconds = 0.004f;
inline constexpr float kLagMaxStepSeconds = 0.1f; // a longer frame is stepped as this long
inline constexpr float kLagRestartSeconds = 0.5f; // a longer gap starts over on the place
// A JUMP: the place lands this far off the course its smoothed velocity predicts, in one frame, and that fast. Both
// at once: a step of a world unit (a vehicle bounced, a seat point re-chosen) is followed on the spring, and only a
// leap of several metres at hundreds of metres a second -- no vehicle's motion -- starts over.
inline constexpr float kLagJumpWu = 3.0f;         // about 9 m
inline constexpr float kLagJumpSpeedWu = 150.0f;  // about 460 m/s off course
// A camera this far from its place (about 300 m) starts over: a safety net only. A lagged camera trails in a turn by
// design -- a Banshee at 45 m/s turning 90 degrees a second, about 4.6 world units with a half-second lag; a flyer at
// 300 m/s turning 60 a second, about 24 -- and none of that is a reason to start over.
inline constexpr float kLagRunawayWu = 100.0f;

inline float Median3(float a, float b, float c) noexcept
{
    const float lo = a < b ? a : b, hi = a < b ? b : a;
    return c < lo ? lo : (c > hi ? hi : c);
}

// The camera's point this frame, in `out`. `seconds` since the last call: 0 (the same frame again) moves nothing.
// A camera without lag, a different camera, a long gap, a jump or a runaway puts the point on its place. A camera with
// a LIMIT (lagLimitCm) never stands further than that from its place: there it is held on the limit, and whatever of
// its speed would carry it further out is taken off, so it rides the limit instead of springing back from it.
inline void FollowLag(const vehicle_camera_select::ActiveCamera& camera, const float place[3], float seconds,
                      LagState& state, float out[3]) noexcept
{
    const auto restart = [&]() {
        state.valid = Finite3(place);
        state.changes = camera.changes;
        state.recentCount = 0;
        state.trailWu = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            state.pos[axis] = place[axis];
            state.vel[axis] = 0.0f;
            state.place[axis] = place[axis];
            state.placeVel[axis] = 0.0f;
            out[axis] = place[axis];
        }
    };
    const float lag = camera.lagSeconds;
    if (!(lag > 0.0f) || !std::isfinite(lag) || !Finite3(place) || !state.valid || state.changes != camera.changes ||
        !std::isfinite(seconds) || seconds < 0.0f || seconds > kLagRestartSeconds)
    {
        restart();
        if (!(lag > 0.0f))
            state.valid = false;
        return;
    }
    if (seconds > 0.0f)
    {
        float raw[3];
        float offCourse = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            raw[axis] = (place[axis] - state.place[axis]) / seconds;
            const float miss = place[axis] - (state.place[axis] + state.placeVel[axis] * seconds);
            offCourse += miss * miss;
        }
        offCourse = std::sqrt(offCourse);
        if (state.recentCount >= 3 && offCourse > kLagJumpWu && offCourse > kLagJumpSpeedWu * seconds)
        {
            ++state.jumps;
            restart();
            return;
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            state.recent[2][axis] = state.recent[1][axis];
            state.recent[1][axis] = state.recent[0][axis];
            state.recent[0][axis] = raw[axis];
        }
        if (state.recentCount < 3)
        {
            // A fresh run stands on its place while it learns the place's motion, three frames; then it starts
            // moving WITH it, so a camera chosen in flight does not fall back from a moving place and catch up.
            ++state.recentCount;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float moving = state.recentCount == 3
                    ? Median3(state.recent[0][axis], state.recent[1][axis], state.recent[2][axis])
                    : 0.0f;
                state.pos[axis] = place[axis];
                state.vel[axis] = moving;
                state.placeVel[axis] = moving;
                state.place[axis] = place[axis];
                out[axis] = place[axis];
            }
            return;
        }
        const float step = seconds > kLagMaxStepSeconds ? kLagMaxStepSeconds : seconds;
        const float w = kLagStiffness / lag;
        float zeta = camera.lagDamping;
        if (!std::isfinite(zeta))
            zeta = 1.0f;
        zeta = zeta < vehicle_camera_file::kMinLagDamping ? vehicle_camera_file::kMinLagDamping
             : zeta > vehicle_camera_file::kMaxLagDamping ? vehicle_camera_file::kMaxLagDamping
                                                          : zeta;
        int count = static_cast<int>(step / kLagSubstepSeconds) + 1;
        const float h = step / static_cast<float>(count);
        const float limit = std::isfinite(camera.lagLimitCm) && camera.lagLimitCm > 0.0f
            ? camera.lagLimitCm * vehicle_camera_frame::kWorldUnitsPerCm : 0.0f;
        bool limited = false;
        const float blend = 1.0f - std::exp(-seconds / lag);
        for (int axis = 0; axis < 3; ++axis)
        {
            const float sample = Median3(state.recent[0][axis], state.recent[1][axis], state.recent[2][axis]);
            state.placeVel[axis] += (sample - state.placeVel[axis]) * blend;
        }
        // The place moves in a straight line across the frame; each substep pulls toward where it is at its start.
        for (int i = 0; i < count; ++i)
        {
            const float t = h * static_cast<float>(i) / step; // 0..1 across the stepped part of the frame
            float at[3];
            for (int axis = 0; axis < 3; ++axis)
            {
                at[axis] = state.place[axis] + (place[axis] - state.place[axis]) * t;
                const float accel = w * w * (at[axis] - state.pos[axis]) +
                                    2.0f * zeta * w * (state.placeVel[axis] - state.vel[axis]);
                state.vel[axis] += accel * h;
                state.pos[axis] += state.vel[axis] * h;
            }
            if (limit > 0.0f)
            {
                float d[3], apart2 = 0.0f;
                for (int axis = 0; axis < 3; ++axis)
                {
                    d[axis] = state.pos[axis] - at[axis];
                    apart2 += d[axis] * d[axis];
                }
                if (apart2 > limit * limit)
                {
                    // Onto the limit, and the speed away from the place (as it moves across this frame) taken off.
                    const float apart = std::sqrt(apart2);
                    float outward = 0.0f;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        d[axis] /= apart;
                        state.pos[axis] = at[axis] + d[axis] * limit;
                        outward += (state.vel[axis] - (place[axis] - state.place[axis]) / step) * d[axis];
                    }
                    if (outward > 0.0f)
                        for (int axis = 0; axis < 3; ++axis)
                            state.vel[axis] -= outward * d[axis];
                    limited = true;
                }
            }
        }
        // The place has moved on across the frame's last substep: the limit is kept against where it ends.
        if (limit > 0.0f)
        {
            float d[3], apart2 = 0.0f;
            for (int axis = 0; axis < 3; ++axis)
            {
                d[axis] = state.pos[axis] - place[axis];
                apart2 += d[axis] * d[axis];
            }
            if (apart2 > limit * limit)
            {
                const float scale = limit / std::sqrt(apart2);
                for (int axis = 0; axis < 3; ++axis)
                    state.pos[axis] = place[axis] + d[axis] * scale;
                limited = true;
            }
        }
        if (limited)
            ++state.limited;
        float apart = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            apart += (place[axis] - state.pos[axis]) * (place[axis] - state.pos[axis]);
        if (!Finite3(state.pos) || !Finite3(state.vel) || !(apart <= kLagRunawayWu * kLagRunawayWu))
        {
            ++state.runaways;
            restart();
            return;
        }
        state.trailWu = std::sqrt(apart);
        for (int axis = 0; axis < 3; ++axis)
            state.place[axis] = place[axis];
    }
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = state.pos[axis];
}
// LAG ON THE BOOM: FollowLag applied to the camera's OFFSET from its pivot, not to its place in the world. The pivot
// rides the vehicle exactly, so the vehicle's travel never reaches the spring -- at any speed, speeding up or stopping
// short, the camera stays with it -- and only the boom's swing (a turn, a pitch, a bump that tilts the vehicle) comes
// through slowly. Lagging the place in the world instead let every change of the vehicle's speed through: the spring
// steers by the place's velocity smoothed over the lag's seconds, so after a change it goes on at the old velocity
// for a while, and the camera runs ahead or falls behind by about a quarter of a second of the change -- a fast flyer
// stopping from 240 m/s threw it some 50 m past the vehicle and back, and a flyer's constant small changes of speed
// were a constant surge and pull-back. On the boom the velocities are the boom's (its length times how fast it turns),
// and the lagged offset keeps the boom's length, so the camera never stands further from its pivot than the camera
// file puts it.
inline void FollowLagOnBoom(const vehicle_camera_select::ActiveCamera& camera, const float pivot[3],
                            const float place[3], float seconds, LagState& state, float out[3]) noexcept
{
    if (!Finite3(pivot) || !Finite3(place))
    {
        FollowLag(camera, place, seconds, state, out);
        return;
    }
    const float offset[3]{place[0] - pivot[0], place[1] - pivot[1], place[2] - pivot[2]};
    float lagged[3];
    FollowLag(camera, offset, seconds, state, lagged);
    const float want = std::sqrt(vehicle_camera_frame::Dot(offset, offset));
    const float have = std::sqrt(vehicle_camera_frame::Dot(lagged, lagged));
    const float scale = want > 1.0e-4f && have > 1.0e-4f && std::isfinite(have) ? want / have : 1.0f;
    for (int axis = 0; axis < 3; ++axis)
        out[axis] = pivot[axis] + lagged[axis] * scale;
}
} // namespace vehicle_camera_place
