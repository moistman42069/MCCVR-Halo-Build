#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Halo 3-only vehicle-state snapshot logic. This file contains no Windows,
// MinHook, logging, or engine access, so the packing, staleness, and debounce
// rules can be exhaustively tested before any detour is authorized. The Halo 3
// retail identities (RVAs, AOBs, derived offsets) arrive with the Phase 0
// evidence in docs/HALO3-VEHICLE-EVIDENCE.md; nothing here is engine-derived.
//
// Shape mirrors ReachVehicleInputSnapshot (reach_render_logic.h): the sampler
// on the proven render thread publishes one generation-keyed 64-bit atomic,
// consumers unpack it lock-free, and a stale or zero generation always reads
// as Unknown so no consumer can act on a previous title session's state.

// Halo 3 retail identities, all byte-proven in docs/HALO3-VEHICLE-EVIDENCE.md
// and independently re-derived by an adversarial second pass. The expected
// RVAs are logging/cross-check values only; the runtime binding is always the
// unique AOB match in the loaded module, never these numbers.
inline constexpr uintptr_t kHalo3UnitInVehicleNativeRva = 0x3A36F4;
inline constexpr uintptr_t kHalo3PlayerUnitGetterRva = 0xEE48C;
inline constexpr uintptr_t kHalo3VehicleTypeAccessorRva = 0x396D84;
inline constexpr uintptr_t kHalo3EngineTlsIndexRva = 0xA39F9C;

// Object-table facts, each read from a named instruction in the native
// unit_in_vehicle body (§E3): engine TLS slot +0x38 -> object table; entry
// array at table+0x48; 0x18-byte entries; data pointer at entry+0x10; cached
// object-kind byte at entry+3 (1 = vehicle). The unit's seat word and parent
// handle live in the unit's object data.
inline constexpr uintptr_t kHalo3TlsObjectTableOffset = 0x38;
inline constexpr uintptr_t kHalo3ObjectTableEntriesOffset = 0x48;
inline constexpr size_t kHalo3ObjectEntryStride = 0x18;
inline constexpr uintptr_t kHalo3ObjectEntryDataOffset = 0x10;
inline constexpr uintptr_t kHalo3ObjectEntryKindOffset = 3;
inline constexpr uint8_t kHalo3ObjectKindVehicle = 1;
inline constexpr uint8_t kHalo3ObjectKindGiant = 12;
// What Halo 3 seats a unit on: a biped (0), a vehicle (1) or a giant (12) -- `1 << kind & 0x1003`, the test the
// game's own unit camera (+0x17DB83), seat-state check (+0x39A7A9) and collision filter (+0x1FC518) make. A giant
// (the Scarab, a mod map's walkers) is not a vehicle and the game's unit_in_vehicle (+0x3A36F4) answers false for
// one, but its seats are seats all the same, in the seat block every unit's definition has at the same place.
inline constexpr uint32_t kHalo3SeatParentKindMask = 0x1003u;
inline constexpr bool Halo3IsSeatParentKind(uint8_t kind)
{
    return kind < 32 && ((1u << kind) & kHalo3SeatParentKindMask) != 0;
}
inline constexpr uintptr_t kHalo3ObjectParentOffset = 0x10;
inline constexpr uintptr_t kHalo3UnitSeatWordOffset = 0x24E;

// Observer facts (§E4): the observer array pointer is engine TLS member
// +0x578 (no static RVA exists); records are 0x3D0 bytes; the effect stage
// writes position/forward/up at these record offsets. The C1 probe captures a
// float window around them so the seat-camera focus fields can be measured,
// not assumed.
inline constexpr uintptr_t kHalo3TlsObserverOffset = 0x578;
inline constexpr size_t kHalo3ObserverStride = 0x3D0;
inline constexpr uintptr_t kHalo3ObserverCaptureBase = 0x100;
inline constexpr size_t kHalo3ObserverCaptureFloats = 40; // +0x100..+0x1A0
inline constexpr uintptr_t kHalo3ObserverPosOffset = 0x11C;
inline constexpr uintptr_t kHalo3ObserverFwdOffset = 0x144;
inline constexpr uintptr_t kHalo3ObserverUpOffset = 0x150;
// Measured in the C1 headset session (evidence doc, runtime results): the
// engine-evaluated per-seat camera pivot. On foot it equals the camera
// position exactly (focus distance 0); in a seat it is the authored pivot the
// chase camera hangs behind. The C3 first-person camera anchors on this
// absolute triplet — never on pos+fwd*d, which C1 proved lags during turns.
inline constexpr uintptr_t kHalo3ObserverFocusOffset = 0x18C;

// Vehicle physics-type facts (§E1/§E2/§E4): 10 authored blocks in enum order
// human_tank..guardian; the retail accessor returns the first non-empty block
// index 0..9, or 0xB when a tag authors none (stationary turrets, shade, and
// mounted turret child tags). Flying = {human_plane, alien_fighter, vtol},
// proven from the kit's own _vehicle_mask_flying assert (test al,0x94).
inline constexpr int kHalo3VehicleTypeTurret = 5;
inline constexpr int kHalo3VehicleTypeNoneAuthored = 0xB;
inline constexpr uint32_t kHalo3VehicleFlyingTypeMask =
    (1u << 2) | (1u << 4) | (1u << 7);

enum class Halo3VehicleState : uint32_t
{
    Unknown = 0,
    OnFoot = 1,
    Vehicle = 2,
};

// Low-32 bit layout. The high 32 bits carry the runtime generation.
//   bits 0-1   Halo3VehicleState
//   bits 2-6   vehicle type + 1 (0 = type unproven this sample)
//   bits 7-13  seat index + 1 (0 = seat unproven this sample)
// Type and seat are optional refinements: a sampler that can only prove the
// seated-parent predicate publishes them as unproven, and every consumer must
// treat unproven exactly like "feature stays stock".
inline constexpr int kHalo3VehicleTypeMax = 29;
inline constexpr int kHalo3VehicleSeatMax = 125;

inline constexpr uint64_t Halo3VehicleSnapshot(
    uint32_t generation, Halo3VehicleState state,
    int vehicleType = -1, int seatIndex = -1)
{
    const uint32_t typePlus1 =
        (vehicleType >= 0 && vehicleType <= kHalo3VehicleTypeMax)
            ? static_cast<uint32_t>(vehicleType) + 1u
            : 0u;
    const uint32_t seatPlus1 =
        (seatIndex >= 0 && seatIndex <= kHalo3VehicleSeatMax)
            ? static_cast<uint32_t>(seatIndex) + 1u
            : 0u;
    return (static_cast<uint64_t>(generation) << 32) |
        (static_cast<uint32_t>(state) & 0x3u) |
        (typePlus1 << 2) | (seatPlus1 << 7);
}

inline constexpr Halo3VehicleState Halo3VehicleSnapshotState(
    uint64_t snapshot, uint32_t generation)
{
    if (generation == 0 ||
        static_cast<uint32_t>(snapshot >> 32) != generation)
        return Halo3VehicleState::Unknown;
    const uint32_t state = static_cast<uint32_t>(snapshot) & 0x3u;
    return state <= static_cast<uint32_t>(Halo3VehicleState::Vehicle)
        ? static_cast<Halo3VehicleState>(state)
        : Halo3VehicleState::Unknown;
}

// -1 = unproven or stale; consumers must degrade that refinement to stock.
inline constexpr int Halo3VehicleSnapshotType(
    uint64_t snapshot, uint32_t generation)
{
    if (Halo3VehicleSnapshotState(snapshot, generation) ==
        Halo3VehicleState::Unknown)
        return -1;
    const uint32_t typePlus1 = (static_cast<uint32_t>(snapshot) >> 2) & 0x1Fu;
    return static_cast<int>(typePlus1) - 1;
}

inline constexpr int Halo3VehicleSnapshotSeat(
    uint64_t snapshot, uint32_t generation)
{
    if (Halo3VehicleSnapshotState(snapshot, generation) ==
        Halo3VehicleState::Unknown)
        return -1;
    const uint32_t seatPlus1 = (static_cast<uint32_t>(snapshot) >> 7) & 0x7Fu;
    return static_cast<int>(seatPlus1) - 1;
}

// Vehicle world transform. The C5 drive session (2026-07-31 08:26 log)
// corrected the C4 pick, and ManagedDonkey's s_object_data confirms the
// names (offsets re-verified by member-size arithmetic): +0x50 is
// `position` — the object's authoritative world origin, i.e. the rigid
// tag/authoring frame the Blender seat points live in. R^T*(+0x1C - +0x50)
// reproduced each render model's node-0 default translation to the
// millimetre on warthog, mongoose and banshee. +0x1C is
// `bounding_sphere_center` (+0x3C/+0x2C the attached-bounds variants),
// which rides the ANIMATED model (brute chopper suspension drifts it
// ~0.09 wu) — never anchor authored points to it. +0x5C `forward` /
// +0x68 `up` are the rigid frame's basis, contiguous with `position`.
// left = up x fwd (right-handed, proven by the passenger seat at -Y).
inline constexpr uintptr_t kHalo3ObjectPositionOffset = 0x50;
inline constexpr uintptr_t kHalo3ObjectBoundingCenterOffset = 0x1C;
inline constexpr uintptr_t kHalo3ObjectForwardOffset = 0x5C;
inline constexpr uintptr_t kHalo3ObjectUpOffset = 0x68;

// C7: object frames and parent-chain composition. A ROOT object stores its
// world transform at +0x50/+0x5C/+0x68 (C6 headset-proven on every driver
// seat). An ATTACHED child object (mounted turret) stores the SAME fields
// PARENT-RELATIVE — the C6 log's hog chaingun read position (-0.5, 0, 0.019)
// / forward (1,0,0) / up (0,0,1) while riding at world (9.9, 12.1, -21.4),
// which is why the C6 turret camera composed against the map origin ("deep
// in the sky"). The anchor frame must therefore be composed through the
// parent chain: world = parent ∘ local, with local coordinates expressed in
// the parent's (fwd, left, up) axes.
struct Halo3Frame
{
    float pos[3];
    float fwd[3];
    float up[3];
};

// Halo's real_matrix4x3 layout. The scale precedes four triplets, so this must
// stay byte-identical to the engine value returned for an interpolated model
// node and to the default inverse matrix stored on that node's mode record.
struct Halo3Matrix4x3
{
    float scale;
    float forward[3];
    float left[3];
    float up[3];
    float position[3];
};
static_assert(sizeof(Halo3Matrix4x3) == 0x34);

inline int Halo3MatrixCountFromByteSize(uint16_t byteSize,
                                        int maximumCount = 256)
{
    if (!byteSize || maximumCount <= 0 ||
        byteSize % sizeof(Halo3Matrix4x3) != 0)
        return 0;
    const int count = byteSize / static_cast<int>(sizeof(Halo3Matrix4x3));
    return count <= maximumCount ? count : 0;
}

inline bool Halo3MatrixValid(const Halo3Matrix4x3& m)
{
    // Vehicle render nodes use a positive, finite uniform scale. Reject a
    // singular or implausibly large value before either transform can divide
    // by it or amplify a corrupt point into the world.
    if (!std::isfinite(m.scale) || m.scale <= 1.0e-5f || m.scale > 1.0e3f)
        return false;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(m.forward[i]) || !std::isfinite(m.left[i]) ||
            !std::isfinite(m.up[i]) || !std::isfinite(m.position[i]))
            return false;
    }

    const auto dot = [](const float a[3], const float b[3]) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const float ff = dot(m.forward, m.forward);
    const float ll = dot(m.left, m.left);
    const float uu = dot(m.up, m.up);
    if (std::fabs(ff - 1.0f) > 0.05f ||
        std::fabs(ll - 1.0f) > 0.05f ||
        std::fabs(uu - 1.0f) > 0.05f ||
        std::fabs(dot(m.forward, m.left)) > 0.10f ||
        std::fabs(dot(m.forward, m.up)) > 0.10f ||
        std::fabs(dot(m.left, m.up)) > 0.10f)
        return false;

    // Halo's authored axes are (forward, left, up), with left = up x forward.
    const float expectedLeft[3] = {
        m.up[1] * m.forward[2] - m.up[2] * m.forward[1],
        m.up[2] * m.forward[0] - m.up[0] * m.forward[2],
        m.up[0] * m.forward[1] - m.up[1] * m.forward[0]};
    return dot(expectedLeft, m.left) >= 0.90f;
}

inline bool Halo3MatrixTransformPoint(const Halo3Matrix4x3& m,
                                      const float point[3], float out[3])
{
    if (!point || !out || !Halo3MatrixValid(m) ||
        !std::isfinite(point[0]) || !std::isfinite(point[1]) ||
        !std::isfinite(point[2]))
        return false;
    float transformed[3];
    for (int i = 0; i < 3; ++i)
    {
        transformed[i] = m.position[i] + m.scale *
            (m.forward[i] * point[0] + m.left[i] * point[1] +
             m.up[i] * point[2]);
        if (!std::isfinite(transformed[i]))
            return false;
    }
    for (int i = 0; i < 3; ++i)
        out[i] = transformed[i];
    return true;
}

inline bool Halo3MatrixInverseTransformPoint(const Halo3Matrix4x3& m,
                                             const float point[3],
                                             float out[3])
{
    if (!point || !out || !Halo3MatrixValid(m) ||
        !std::isfinite(point[0]) || !std::isfinite(point[1]) ||
        !std::isfinite(point[2]))
        return false;
    const float relative[3] = {
        point[0] - m.position[0], point[1] - m.position[1],
        point[2] - m.position[2]};
    const float inverseScale = 1.0f / m.scale;
    float transformed[3] = {
        inverseScale * (relative[0] * m.forward[0] +
                        relative[1] * m.forward[1] +
                        relative[2] * m.forward[2]),
        inverseScale * (relative[0] * m.left[0] +
                        relative[1] * m.left[1] +
                        relative[2] * m.left[2]),
        inverseScale * (relative[0] * m.up[0] +
                        relative[1] * m.up[1] +
                        relative[2] * m.up[2])};
    if (!std::isfinite(transformed[0]) || !std::isfinite(transformed[1]) ||
        !std::isfinite(transformed[2]))
        return false;
    for (int i = 0; i < 3; ++i)
        out[i] = transformed[i];
    return true;
}

// The Blender points are in tag space. A mode node's stored default inverse
// maps that point into node-local space; applying the live interpolated node
// then preserves the authored placement at rest and inherits every rendered
// translation and rotation thereafter.
inline bool Halo3ComputeNodeAnchoredPoint(
    const Halo3Matrix4x3& defaultInverse, const Halo3Matrix4x3& liveNode,
    const float authored[3], float out[3])
{
    float nodeLocal[3];
    return Halo3MatrixTransformPoint(defaultInverse, authored, nodeLocal) &&
        Halo3MatrixTransformPoint(liveNode, nodeLocal, out);
}

// How far the occupant's head may carry the camera away from the settled seat
// pose. Beyond this the contribution SATURATES; it is never dropped.
inline constexpr float kHalo3HeadMaximumLocalDelta = 0.08f;

// C22 — the occupant-head contribution saturates continuously.
//
// The measured defect: C18 gated head parenting on the delta staying inside
// this limit and dropped the whole contribution when it did not, then demanded
// a fresh 1.5 s + 500 ms settle before it could return. Driving breaches the
// limit constantly, and the settle needs the head to hold still inside a 4.5 cm
// sphere for half a second, which never happens on a moving vehicle. The
// 2026-08-01 06:19 log shows the result directly: consecutive ten-second
// windows at head-parented 0%, 35%, 19%, 27%, 0%, 0%, 0%, 42%, 0%, 16%, 0%
// with zero anchor fallbacks. Every one of those transitions stepped the
// camera by the whole accumulated bounce, which is the "reset" the user
// reported — and with C21 the arms hold the rigid seat point, so each step
// also showed up as the hands jumping against the view.
//
// Clamping the delta to the same limit is continuous at the boundary
// (|d| <= L is identity, |d| > L scales to exactly L), so the camera can
// never step. A large entry or seat-switch animation is still bounded to the
// same distance it was before, but it now arrives and leaves smoothly instead
// of snapping to zero. This is strictly less state and less work per frame
// than the drop-and-re-settle it replaces.
// C23: `gain` scales the whole contribution AFTER the clamp, so it is a plain
// strength control, not a filter. 1 is the full C22 travel (0.08 wu, about
// 24 cm — measured as too much: the user reported it as nauseating once C22
// stopped it dropping out and they felt all of it for the first time). 0 bolts
// the view to the seat exactly. Clamp-then-scale keeps both the saturation and
// the scaling continuous, so no setting can reintroduce a step.
inline bool Halo3ClampedHeadLocalDelta(const float currentLocal[3],
                                       const float reference[3], float gain,
                                       float outDelta[3])
{
    if (!currentLocal || !reference || !outDelta || !std::isfinite(gain))
        return false;
    if (gain < 0.0f)
        gain = 0.0f;
    if (gain > 1.0f)
        gain = 1.0f;
    float delta[3];
    float lengthSquared = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(currentLocal[i]) || !std::isfinite(reference[i]))
            return false;
        delta[i] = currentLocal[i] - reference[i];
        lengthSquared += delta[i] * delta[i];
    }
    if (!std::isfinite(lengthSquared))
        return false;
    if (lengthSquared >
        kHalo3HeadMaximumLocalDelta * kHalo3HeadMaximumLocalDelta)
    {
        const float length = std::sqrt(lengthSquared);
        if (!std::isfinite(length) || length <= 0.0f)
            return false;
        const float scale = kHalo3HeadMaximumLocalDelta / length;
        for (int i = 0; i < 3; ++i)
            delta[i] *= scale;
    }
    for (int i = 0; i < 3; ++i)
    {
        delta[i] *= gain;
        if (!std::isfinite(delta[i]))
            return false;
        outDelta[i] = delta[i];
    }
    return true;
}

// Keep the animated occupant head translation relative to the settled pose.
// At the latch pose this emits the authored node-local point
// exactly; later head-local movement is added around that point while the live
// node remains the sole parent for vehicle/mesh motion.
inline bool Halo3ComputeHeadParentedPoint(
    const Halo3Matrix4x3& liveSeatNode, const float headWorld[3],
    const float headLocalReference[3], const float authoredNodeLocal[3],
    float gain, float out[3])
{
    if (!headLocalReference || !authoredNodeLocal)
        return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(headLocalReference[i]) ||
            !std::isfinite(authoredNodeLocal[i]))
            return false;
    float currentHeadLocal[3];
    if (!Halo3MatrixInverseTransformPoint(
            liveSeatNode, headWorld, currentHeadLocal))
        return false;
    float delta[3];
    if (!Halo3ClampedHeadLocalDelta(currentHeadLocal, headLocalReference,
                                    gain, delta))
        return false;
    const float cameraLocal[3] = {
        authoredNodeLocal[0] + delta[0],
        authoredNodeLocal[1] + delta[1],
        authoredNodeLocal[2] + delta[2]};
    return Halo3MatrixTransformPoint(liveSeatNode, cameraLocal, out);
}

// Entry/seat-switch animation can move the occupant's head by metres relative
// to the destination seat before it reaches its final pose. Never freeze that
// transient as the zero point. The exact authored node anchor remains active
// until one concrete seat identity has existed long enough AND the head has
// remained inside a small node-local sphere for a continuous quiet window.
inline constexpr uint64_t kHalo3HeadSettleMinimumAgeMs = 1500;
inline constexpr uint64_t kHalo3HeadSettleQuietMs = 500;
inline constexpr float kHalo3HeadSettleRadius = 0.015f;

struct Halo3HeadSettleLatch
{
    uint64_t observedSinceMs = 0;
    uint64_t quietSinceMs = 0;
    bool observing = false;
    bool valid = false;
    float quietOrigin[3] = {0.0f, 0.0f, 0.0f};
    float reference[3] = {0.0f, 0.0f, 0.0f};

    bool Update(uint64_t nowMs, const float currentLocal[3])
    {
        if (!currentLocal || !std::isfinite(currentLocal[0]) ||
            !std::isfinite(currentLocal[1]) ||
            !std::isfinite(currentLocal[2]))
            return false;
        if (valid)
            return true;

        if (!observing || nowMs < observedSinceMs || nowMs < quietSinceMs)
        {
            observing = true;
            observedSinceMs = nowMs;
            quietSinceMs = nowMs;
            for (int i = 0; i < 3; ++i)
                quietOrigin[i] = currentLocal[i];
            return false;
        }

        const float dx = currentLocal[0] - quietOrigin[0];
        const float dy = currentLocal[1] - quietOrigin[1];
        const float dz = currentLocal[2] - quietOrigin[2];
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!std::isfinite(distanceSquared))
            return false;
        if (distanceSquared >
            kHalo3HeadSettleRadius * kHalo3HeadSettleRadius)
        {
            quietSinceMs = nowMs;
            for (int i = 0; i < 3; ++i)
                quietOrigin[i] = currentLocal[i];
            return false;
        }

        if (nowMs - observedSinceMs < kHalo3HeadSettleMinimumAgeMs ||
            nowMs - quietSinceMs < kHalo3HeadSettleQuietMs)
            return false;

        for (int i = 0; i < 3; ++i)
            reference[i] = currentLocal[i];
        valid = true;
        return true;
    }
};

// SUPERSEDED by Halo3ClampedHeadLocalDelta (C22). Kept because it states the
// boundary the clamp saturates at, and the clamp's continuity is tested
// against it: inside the limit the two must agree exactly.
inline bool Halo3HeadLocalDeltaWithinLimit(const float currentLocal[3],
                                           const float reference[3])
{
    if (!currentLocal || !reference)
        return false;
    float distanceSquared = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(currentLocal[i]) || !std::isfinite(reference[i]))
            return false;
        const float delta = currentLocal[i] - reference[i];
        distanceSquared += delta * delta;
    }
    return std::isfinite(distanceSquared) &&
        distanceSquared <=
            kHalo3HeadMaximumLocalDelta * kHalo3HeadMaximumLocalDelta;
}

// Position-only OpenXR neutralization is independent of yaw follow. Remember
// the concrete authored seat that received it so a frame-level node/sample
// miss cannot yank the player's leaning origin, while a real seat/vehicle
// switch always gets a fresh exact baseline.
struct Halo3SeatPositionKey
{
    uint32_t generation = 0;
    int parentHandle = -1;
    int seatIndex = -1;
    uint32_t vehicleId = 0xFFFFFFFFu;
    bool mounted = false;
    bool valid = false;

    constexpr bool Matches(uint32_t candidateGeneration,
                           int candidateParentHandle, int candidateSeatIndex,
                           uint32_t candidateVehicleId,
                           bool candidateMounted) const
    {
        return valid && generation == candidateGeneration &&
            parentHandle == candidateParentHandle &&
            seatIndex == candidateSeatIndex &&
            vehicleId == candidateVehicleId && mounted == candidateMounted;
    }

    constexpr void Set(uint32_t candidateGeneration,
                       int candidateParentHandle, int candidateSeatIndex,
                       uint32_t candidateVehicleId, bool candidateMounted)
    {
        generation = candidateGeneration;
        parentHandle = candidateParentHandle;
        seatIndex = candidateSeatIndex;
        vehicleId = candidateVehicleId;
        mounted = candidateMounted;
        valid = true;
    }
};

inline void Halo3FrameLeft(const Halo3Frame& f, float out[3])
{
    out[0] = f.up[1] * f.fwd[2] - f.up[2] * f.fwd[1];
    out[1] = f.up[2] * f.fwd[0] - f.up[0] * f.fwd[2];
    out[2] = f.up[0] * f.fwd[1] - f.up[1] * f.fwd[0];
}

inline bool Halo3FrameOrthonormal(const Halo3Frame& f)
{
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(f.pos[i]) || !std::isfinite(f.fwd[i]) ||
            !std::isfinite(f.up[i]))
            return false;
    const float fLen = f.fwd[0] * f.fwd[0] + f.fwd[1] * f.fwd[1] +
        f.fwd[2] * f.fwd[2];
    const float uLen = f.up[0] * f.up[0] + f.up[1] * f.up[1] +
        f.up[2] * f.up[2];
    const float fu = f.fwd[0] * f.up[0] + f.fwd[1] * f.up[1] +
        f.fwd[2] * f.up[2];
    return std::fabs(fLen - 1.0f) <= 0.05f && std::fabs(uLen - 1.0f) <= 0.05f &&
        std::fabs(fu) <= 0.15f;
}

// Map a point/vector expressed in `frame` axes (x fwd, y left, z up) to world.
inline void Halo3FrameRotate(const Halo3Frame& frame, const float v[3],
                             float out[3])
{
    float left[3];
    Halo3FrameLeft(frame, left);
    for (int i = 0; i < 3; ++i)
        out[i] = frame.fwd[i] * v[0] + left[i] * v[1] + frame.up[i] * v[2];
}

// Express a world vector in the frame's authored axes (x fwd, y left, z up).
inline void Halo3FrameUnrotate(const Halo3Frame& frame, const float v[3],
                               float out[3])
{
    float left[3];
    Halo3FrameLeft(frame, left);
    out[0] = frame.fwd[0] * v[0] + frame.fwd[1] * v[1] +
        frame.fwd[2] * v[2];
    out[1] = left[0] * v[0] + left[1] * v[1] + left[2] * v[2];
    out[2] = frame.up[0] * v[0] + frame.up[1] * v[1] +
        frame.up[2] * v[2];
}

// C17: the native seated camera owns all large/render-rate motion. The object
// frame rotates only the small, fixed correction from the native seat point to
// the user's Blender-authored point; its sampled origin is deliberately absent.
inline void Halo3NativeParentedAnchor(const Halo3Frame& sampledFrame,
                                      const float nativeWorld[3],
                                      const float nativeLocal[3],
                                      const float authoredLocal[3],
                                      float out[3])
{
    const float localCorrection[3] = {
        authoredLocal[0] - nativeLocal[0],
        authoredLocal[1] - nativeLocal[1],
        authoredLocal[2] - nativeLocal[2]};
    Halo3FrameRotate(sampledFrame, localCorrection, out);
    out[0] += nativeWorld[0];
    out[1] += nativeWorld[1];
    out[2] += nativeWorld[2];
}

// ---- C10: render-rate smoothing of the sampled hull frame ----------------
// MEASURED (2026-07-31 Steam session, source b25a414): the camera hook runs at
// 240/sec and the seated parent's +0x50/+0x5C/+0x68 change on exactly 25% of
// those calls — 60 Hz — with position steps up to 0.1238 wu (0.38 m) and
// orientation steps up to 16 deg. Halo 3 simulates at a fixed 60 Hz and MCC
// interpolates the drawn MESH up to the presented rate, so a camera pinned to
// the raw sampled value staircases against a model that glides: the user's
// "the vehicles are shaking and vibrating ... the car gyrates".
//
// The fix is to reconstruct the same thing the renderer does — walk linearly
// from the previous simulation state to the current one across one tick. This
// is continuous by construction: when a new state arrives, the point we have
// just reached (the old current) becomes the new previous, so the output never
// jumps. A t past 1 extrapolates linearly beyond the newest state; the caller
// only ever asks for that by the amount the renderer itself was MEASURED to
// draw ahead (the C12 phase lock below), and only at speed, so a collision
// tick can never fling the camera further than the renderer flings the mesh.
inline Halo3Frame Halo3LerpFrame(const Halo3Frame& a, const Halo3Frame& b,
                                 float t)
{
    Halo3Frame out{};
    for (int i = 0; i < 3; ++i)
    {
        out.pos[i] = a.pos[i] + (b.pos[i] - a.pos[i]) * t;
        out.fwd[i] = a.fwd[i] + (b.fwd[i] - a.fwd[i]) * t;
        out.up[i] = a.up[i] + (b.up[i] - a.up[i]) * t;
    }
    // A component-wise blend of two rotations is not a rotation. Renormalize,
    // then take the up vector perpendicular to forward (Gram-Schmidt). The
    // per-tick angle is small, so this is indistinguishable from a slerp.
    const float fLen = std::sqrt(out.fwd[0] * out.fwd[0] +
                                 out.fwd[1] * out.fwd[1] +
                                 out.fwd[2] * out.fwd[2]);
    if (!(fLen > 1e-3f))
        return b;
    for (int i = 0; i < 3; ++i) out.fwd[i] /= fLen;
    const float fu = out.fwd[0] * out.up[0] + out.fwd[1] * out.up[1] +
        out.fwd[2] * out.up[2];
    for (int i = 0; i < 3; ++i) out.up[i] -= out.fwd[i] * fu;
    const float uLen = std::sqrt(out.up[0] * out.up[0] +
                                 out.up[1] * out.up[1] +
                                 out.up[2] * out.up[2]);
    if (!(uLen > 1e-3f))
        return b;
    for (int i = 0; i < 3; ++i) out.up[i] /= uLen;
    return out;
}

struct Halo3FrameInterp
{
    bool valid = false;
    Halo3Frame prev{};
    Halo3Frame cur{};
    double lastTime = 0.0;      // previous call, for the free-running phase
    double curTime = 0.0;       // when `cur` was first seen, seconds
    double phase = 0.0;         // ticks elapsed since `cur` became current
    double tick = 1.0 / 60.0;   // measured simulation interval
    bool tickKnown = false;
    float correction = 0.0f;    // last phase nudge, diagnostic only
    float lastStepLen = 0.0f;   // |cur - prev| position step, wu
    // Last published blend fraction. Diagnostic: >1 means the seat is being
    // extrapolated past the newest simulation state.
    float lastAlpha = 0.0f;
};

// ---- C12: measured display-phase lock ------------------------------------
// C11 paced the camera evenly but could not know WHERE inside the tick the
// renderer draws the mesh, so the constant offset between the two was left as
// a hand-tuned config knob — and because the blend was clamped at the newest
// state, any non-zero knob slid for part of each tick and froze for the rest,
// a 60 Hz sawtooth ("bounces repeatedly at rapid small speeds", user,
// 2026-07-31). Both are replaced by measuring the renderer directly.
//
// The witness is the object's bounding-sphere centre (+0x1C), sampled every
// camera call in the same capture window as the hull frame. It is ANIMATED
// data: if MCC render-interpolates the model, this value glides between the
// two simulation states we already hold — so projecting it onto the segment
// between those states reads off the exact sub-tick fraction the renderer is
// displaying right now. Every assumption is checked at runtime:
//  - the centre's hull-frame offset is latched only while PARKED, when sim
//    and render provably show the same pose;
//  - the phase is measured only when the centre was seen moving BETWEEN hull
//    ticks (a 60 Hz value cannot), only at a driving pace, and only against a
//    per-tick travel big enough to project on;
//  - the learned lead is a 2%-per-sample average, applied only at speed, so
//    a parked or crawling vehicle never extrapolates its own physics jitter.
// Anything unproven leaves lead at zero, which is exactly C11.
struct Halo3PhaseLock
{
    // Bounding-centre offset in hull axes, latched at rest.
    bool refValid = false;
    float refLocal[3] = {0.0f, 0.0f, 0.0f};
    double stillSeconds = 0.0;
    // Previous call's raw centre, for change detection.
    bool lastBValid = false;
    float lastB[3] = {0.0f, 0.0f, 0.0f};
    // Render-rate proof. C13: counted over calls on which the HULL did NOT
    // change, accumulated across ticks — see kHalo3PhaseProofCalls.
    int quietCalls = 0;
    int quietMoves = 0;
    bool smooth = false;
    // The learned constant: how far ahead of the free-running phase the
    // renderer actually draws, in ticks.
    bool leadValid = false;
    float lead = 0.0f;
    // True once at least one tick window was long enough to judge `smooth`,
    // so the log can tell "not render-interpolated" from "not measured yet".
    bool smoothEvaluated = false;
    float lastMeasured = 0.0f;  // diagnostic only
};

// C13 — HEADSET REFRESH RATE. Halo 3's 60 Hz is the ENGINE's simulation
// clock, identical on every machine; the camera hook runs at whatever rate
// the headset presents (72, 80, 90, 120, 144...). Nothing here may assume a
// call rate: the tick length is measured, the phase free-runs on the
// performance counter, and the constants below are in SECONDS or in events,
// never in frames. The user asked for a refresh-rate selector; there is
// nothing for one to select, but their question did find the one place that
// had baked a rate in — the render-smoothness proof below, which needed
// three camera calls inside one 16.7 ms tick and so could never complete on
// a 72-90 Hz headset (1.2 to 1.5 calls per tick).
//
// The rate-independent form: judge on calls where the HULL DID NOT CHANGE.
// A 60 Hz witness cannot move on those; a render-interpolated one always
// does. Rare on a slow headset, common on a fast one, correct on both, and
// accumulated across ticks rather than inside one.
inline constexpr int kHalo3PhaseProofCalls = 12;
// Time (not frames) of stillness that counts as parked.
inline constexpr double kHalo3PhaseStillSeconds = 0.15;
// Bounding-centre movement below this is standing still (wu).
inline constexpr float kHalo3PhaseStillEpsilon = 1e-4f;
// Per-tick travel (wu) below which no phase is measured and no lead applied:
// 0.01 wu/tick is walking pace, where a phase error is invisible anyway.
inline constexpr float kHalo3PhaseMinStep = 0.01f;
inline constexpr float kHalo3PhaseLeadTrack = 0.02f;  // EMA per fresh sample
// Time constant for gliding the APPLIED lead in and out. In seconds, so a
// hitch or a stop takes the same ~40 ms to fade at 72 Hz as at 144 Hz; a
// per-call factor would have been twice as abrupt on a fast headset.
inline constexpr double kHalo3PhaseApplySeconds = 0.04;
inline constexpr float kHalo3PhaseLeadMin = -0.5f;
inline constexpr float kHalo3PhaseLeadMax = 1.5f;
// Hard ceiling on the displayed blend fraction while ticks are arriving.
inline constexpr double kHalo3PhaseAlphaMax = 1.75;

// How hard each detected tick pulls the free-running phase back into lock.
// Small on purpose: the whole point is that the phase must NOT be yanked to a
// new value every tick.
inline constexpr double kHalo3PhaseTrack = 0.05;

// Position change (wu) below which two samples count as the same state.
inline constexpr float kHalo3FrameSameEpsilon = 1e-5f;
// A single tick can never move a vehicle this far or turn it this hard; that
// is a teleport, a respawn or a seat change, and it must snap, not slide.
inline constexpr float kHalo3FrameSnapDistance = 2.0f;   // wu
inline constexpr float kHalo3FrameSnapDot = 0.5f;        // ~60 degrees
// Plausible simulation intervals. Anything outside is a hitch, not a tick.
inline constexpr double kHalo3TickMin = 0.004;
inline constexpr double kHalo3TickMax = 0.050;

// Feed the RAW sampled frame every camera call with a monotonic clock in
// seconds (the tick counter is far too coarse — a 60 Hz tick is under two of
// its 15.6 ms steps). Returns the frame to publish.
// C16 — the seat is PARENTED to the car, and that is the whole design.
//
// Everything that tried to place the seat somewhere OTHER than where the
// simulation says the car is has been removed: the C12 measured display
// phase (its witness never armed on real hardware), and the C14 photon-time
// prediction with its headset-rate list (whose 60 Hz entry meant a full tick
// of extrapolation, hit the safety ceiling, and let the car rotate out from
// under the camera — "i was able to drift the camera completely around").
//
// What remains is a blend strictly BETWEEN the two most recent simulation
// states, which is the most the engine actually knows. With smoothing off —
// the default — the seat sits exactly on the newest state: rigidly bolted to
// the hull, suspension, boost and all.
inline Halo3Frame Halo3InterpolateFrame(Halo3FrameInterp& state,
                                        const Halo3Frame& raw, double nowSec,
                                        bool enabled,
                                        Halo3PhaseLock* lock = nullptr,
                                        const float* rawBounds = nullptr)
{
    if (!enabled || !std::isfinite(nowSec))
    {
        state.valid = false;
        state.tickKnown = false; // the log mirror keys off this: OFF is OFF
        if (lock)
            *lock = Halo3PhaseLock{};
        return raw;
    }
    if (!state.valid)
    {
        state.valid = true;
        state.prev = raw;
        state.cur = raw;
        state.lastTime = nowSec;
        state.curTime = nowSec;
        state.phase = 0.0;
        state.tickKnown = false;
        state.correction = 0.0f;
        state.lastStepLen = 0.0f;
        if (lock)
            *lock = Halo3PhaseLock{};
        return raw;
    }
    // The phase FREE-RUNS on the wall clock. Restarting the blend at zero on
    // every detected tick is what the C10 video showed as a residual shake:
    // detection lands 0 to one camera-interval after the real tick and a
    // different amount late each time, so each restart moved the camera by up
    // to a quarter of a tick's worth of travel. Measured on the 2026-07-31
    // 13:16 session: the vehicle still moved on 49% of frames in which the
    // distant world was stationary, mean zero, sign flipping nearly every
    // frame — an oscillation about one sixth of a tick in size.
    const double step = nowSec - state.lastTime;
    state.lastTime = nowSec;
    if (state.tickKnown && step > 0.0 && step < 0.25)
        state.phase += step / state.tick;
    bool changed = false;
    float moved = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = raw.pos[i] - state.cur.pos[i];
        moved += d * d;
        if (std::fabs(d) > kHalo3FrameSameEpsilon ||
            std::fabs(raw.fwd[i] - state.cur.fwd[i]) > kHalo3FrameSameEpsilon ||
            std::fabs(raw.up[i] - state.cur.up[i]) > kHalo3FrameSameEpsilon)
            changed = true;
    }
    // C12 bookkeeping: the raw bounding centre is the render-side witness.
    const bool haveB = lock && rawBounds &&
        std::isfinite(rawBounds[0]) && std::isfinite(rawBounds[1]) &&
        std::isfinite(rawBounds[2]);
    bool bMoved = false;
    if (lock)
    {
        if (haveB)
        {
            if (lock->lastBValid)
                bMoved =
                    std::fabs(rawBounds[0] - lock->lastB[0]) >
                        kHalo3PhaseStillEpsilon ||
                    std::fabs(rawBounds[1] - lock->lastB[1]) >
                        kHalo3PhaseStillEpsilon ||
                    std::fabs(rawBounds[2] - lock->lastB[2]) >
                        kHalo3PhaseStillEpsilon;
            // C13 rate-independent proof: only calls on which the hull held
            // still can tell a render-interpolated centre from a 60 Hz one,
            // and they exist at every headset rate. Counted only while
            // driving, so a parked vehicle's stillness cannot be read as
            // "not interpolated".
            if (!changed && lock->lastBValid &&
                state.lastStepLen >= kHalo3PhaseMinStep)
            {
                ++lock->quietCalls;
                if (bMoved)
                    ++lock->quietMoves;
                if (lock->quietCalls >= kHalo3PhaseProofCalls)
                {
                    // A 60 Hz witness moves on NO quiet call. An interpolated
                    // one moves on the fraction of them that carry a present,
                    // which is the present rate over the camera rate and can
                    // legitimately be well under half (MCC presenting at 120
                    // while the hook runs at 240). A quarter separates "some
                    // of the time" from "never" without demanding a rate.
                    lock->smooth =
                        lock->quietMoves * 4 >= lock->quietCalls;
                    lock->smoothEvaluated = true;
                }
            }
            if (!changed && !bMoved && lock->lastBValid)
            {
                // Parked, the simulation and the renderer provably show the
                // same pose, so the centre's hull-frame offset can be read
                // exactly — the reference every moving measurement projects
                // against.
                if (step > 0.0 && step < 0.25)
                    lock->stillSeconds += step;
                if (lock->stillSeconds >= kHalo3PhaseStillSeconds)
                {
                    float rel[3], left[3];
                    for (int i = 0; i < 3; ++i)
                        rel[i] = rawBounds[i] - state.cur.pos[i];
                    Halo3FrameLeft(state.cur, left);
                    lock->refLocal[0] = rel[0] * state.cur.fwd[0] +
                        rel[1] * state.cur.fwd[1] + rel[2] * state.cur.fwd[2];
                    lock->refLocal[1] =
                        rel[0] * left[0] + rel[1] * left[1] + rel[2] * left[2];
                    lock->refLocal[2] = rel[0] * state.cur.up[0] +
                        rel[1] * state.cur.up[1] + rel[2] * state.cur.up[2];
                    lock->refValid = true;
                }
            }
            else
                lock->stillSeconds = 0.0;
            for (int i = 0; i < 3; ++i)
                lock->lastB[i] = rawBounds[i];
            lock->lastBValid = true;
        }
        else
        {
            lock->lastBValid = false;
            lock->stillSeconds = 0.0;
        }
    }
    if (changed)
    {
        const float dot = raw.fwd[0] * state.cur.fwd[0] +
            raw.fwd[1] * state.cur.fwd[1] + raw.fwd[2] * state.cur.fwd[2];
        if (moved > kHalo3FrameSnapDistance * kHalo3FrameSnapDistance ||
            dot < kHalo3FrameSnapDot)
        {
            state.prev = raw;
            state.cur = raw;
            state.curTime = nowSec;
            state.phase = 0.0;
            state.lastStepLen = 0.0f;
            if (lock)
            {
                // A teleport invalidates the accumulated proof statistics but
                // not the vehicle-shape reference or the learned lead.
                lock->quietCalls = 0;
                lock->quietMoves = 0;
                lock->stillSeconds = 0.0;
                lock->smooth = false;
                lock->smoothEvaluated = false;
            }
            return raw;
        }
        const double interval = nowSec - state.curTime;
        if (interval >= kHalo3TickMin && interval <= kHalo3TickMax)
        {
            // Detection is quantized to the camera's own rate, so smooth the
            // estimate rather than trusting one interval.
            state.tick = state.tickKnown
                ? state.tick * 0.9 + interval * 0.1
                : interval;
            state.tickKnown = true;
        }
        // C12: was the centre seen moving BETWEEN hull ticks? Only a
        // render-interpolated value can be; a 60 Hz value moves only on the
        // tick itself.
        state.prev = state.cur;
        state.cur = raw;
        state.curTime = nowSec;
        {
            float d2 = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                const float d = state.cur.pos[i] - state.prev.pos[i];
                d2 += d * d;
            }
            state.lastStepLen = std::sqrt(d2);
        }
        // Consume the tick we just crossed, then pull the phase gently toward
        // where it should be at this instant: the real tick happened somewhere
        // inside the last camera interval, so on average half of one. A gentle
        // pull keeps the sub-tick offset the phase already has instead of
        // re-randomising it, which is the whole fix.
        const double target = state.tick > 0.0
            ? 0.5 * step / state.tick
            : 0.0;
        state.phase -= 1.0;
        if (state.phase < -0.5 || state.phase > 1.5)
            state.phase = target;                 // hitch: relock immediately
        else
            state.phase += (target - state.phase) * kHalo3PhaseTrack;
        state.correction = static_cast<float>(state.phase - target);
        if (state.phase < 0.0)
            state.phase = 0.0;
    }
    if (!state.tickKnown)
        return state.cur;
    // C12 measurement: project the render-side centre onto the segment
    // between the same centre computed from the two simulation states. The
    // result is the blend fraction the renderer is displaying RIGHT NOW;
    // its constant offset from our free-running phase is the lead. Sampled
    // only on calls where the centre freshly moved (a stale read would bias
    // the phase low), only while ticks are actually arriving, and only when
    // the tick's travel is long enough to project against.
    const bool ticksLive = nowSec - state.curTime <= state.tick * 1.5;
    if (haveB && bMoved && ticksLive && lock->refValid && lock->smooth)
    {
        float bPrev[3], bCur[3], rot[3];
        Halo3FrameRotate(state.prev, lock->refLocal, rot);
        for (int i = 0; i < 3; ++i)
            bPrev[i] = state.prev.pos[i] + rot[i];
        Halo3FrameRotate(state.cur, lock->refLocal, rot);
        for (int i = 0; i < 3; ++i)
            bCur[i] = state.cur.pos[i] + rot[i];
        float dir[3], d2 = 0.0f, num = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            dir[i] = bCur[i] - bPrev[i];
            d2 += dir[i] * dir[i];
            num += (rawBounds[i] - bPrev[i]) * dir[i];
        }
        if (d2 > kHalo3PhaseMinStep * kHalo3PhaseMinStep)
        {
            const float alphaMeas = num / d2;
            if (alphaMeas > -0.5f && alphaMeas < 2.5f)
            {
                const float sample =
                    alphaMeas - static_cast<float>(state.phase);
                lock->lead = lock->leadValid
                    ? lock->lead + (sample - lock->lead) * kHalo3PhaseLeadTrack
                    : sample;
                if (lock->lead < kHalo3PhaseLeadMin)
                    lock->lead = kHalo3PhaseLeadMin;
                if (lock->lead > kHalo3PhaseLeadMax)
                    lock->lead = kHalo3PhaseLeadMax;
                lock->leadValid = true;
                lock->lastMeasured = alphaMeas;
            }
        }
    }
    // C16: the seat is PARENTED to the car. The blend never goes past the
    // newest simulation state, because everything that lived out there —
    // photon-time prediction, a learned lead, extrapolation authority — was
    // machinery the headset kept rejecting. The last of it pinned the blend
    // at a 1.75 ceiling under a pinned 60 Hz "prediction" (a whole tick), and
    // holding at a ceiling while the hull keeps turning LOSES that rotation
    // for good, which let the car rotate out from under the camera.
    double alpha = state.phase;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    state.lastAlpha = static_cast<float>(alpha);
    return Halo3LerpFrame(state.prev, state.cur, static_cast<float>(alpha));
}

inline Halo3Frame Halo3ComposeFrame(const Halo3Frame& parent,
                                    const Halo3Frame& local)
{
    Halo3Frame world{};
    Halo3FrameRotate(parent, local.pos, world.pos);
    for (int i = 0; i < 3; ++i)
        world.pos[i] += parent.pos[i];
    Halo3FrameRotate(parent, local.fwd, world.fwd);
    Halo3FrameRotate(parent, local.up, world.up);
    return world;
}

// C8 identity — the bounding-center fingerprint. Physics type + seat index is
// NOT a vehicle identity: (1,0) is both warthog and mongoose, (3,0) is ghost,
// wraith and mauler, and every mounted turret is (5,0,native=1). Expressing
// (bounding_sphere_center - position) in the object's OWN axes recovers that
// tag's render_model node-0 default translation — a per-tag constant, free
// from data the sampler already captures, and proven in a live session (it
// identified a Mongoose in a drive logged as a "warthog"). R^T * v, with R's
// columns the object's (fwd, left, up).
inline bool Halo3ComputeFingerprint(const Halo3Frame& frame,
                                    const float center[3], float out[3])
{
    if (!Halo3FrameOrthonormal(frame))
        return false;
    float left[3];
    Halo3FrameLeft(frame, left);
    float v[3];
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(center[i]))
            return false;
        v[i] = center[i] - frame.pos[i];
    }
    out[0] = v[0] * frame.fwd[0] + v[1] * frame.fwd[1] + v[2] * frame.fwd[2];
    out[1] = v[0] * left[0] + v[1] * left[1] + v[2] * left[2];
    out[2] = v[0] * frame.up[0] + v[1] * frame.up[1] + v[2] * frame.up[2];
    return true;
}

inline float Halo3FingerprintDistance(const float a[3], const float b[3])
{
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]))
            return 1e9f;
        const float d = a[i] - b[i];
        d2 += d * d;
    }
    return std::sqrt(d2);
}

// A fingerprint identifies a cousin only when ONE candidate is close and
// every other is comfortably far. Ambiguity must never pick a winner: the
// caller then publishes no point and the stock chase camera stands, which is
// the whole safety property of this feature (a mislabel puts the player's
// head inside another vehicle's geometry; no label merely costs the effect).
inline constexpr float kHalo3FingerprintAccept = 0.15f;   // wu
inline constexpr float kHalo3FingerprintSeparate = 0.30f; // wu

inline bool Halo3FingerprintIsUnique(float bestDistance, float runnerUpDistance,
                                     float accept = kHalo3FingerprintAccept,
                                     float separate = kHalo3FingerprintSeparate)
{
    if (!std::isfinite(bestDistance))
        return false;
    if (bestDistance > accept)
        return false;
    if (!std::isfinite(runnerUpDistance))
        return true;   // only one candidate at all
    return runnerUpDistance >= separate;
}

// The largest bounding sphere radius read as a real object's, in world units: a guard against a wrong or garbage
// read, not a size limit. A mod map's giant flyer is far past the stock vehicles' few units: HugeGrass's Lich has a
// live sphere of 112.25 wu (342 m), which the old 100 wu limit refused, so its seat never had a frame.
inline constexpr float kHalo3MaxBoundsRadiusWu = 1000.0f;

// Anchor-frame sanity: the frame origin must sit inside the seated parent's
// own bounding sphere (+0x1C center / +0x28 radius) plus a margin generous
// enough for every authored point yet far below any wrong-frame miss (the C6
// sky camera was tens of wu off). Insane -> the stock chase view stands.
inline bool Halo3AnchorWithinBounds(const float pos[3], const float center[3],
                                    float radius, float margin)
{
    if (!std::isfinite(radius) || radius < 0.0f || radius > kHalo3MaxBoundsRadiusWu)
        return false;
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(pos[i]) || !std::isfinite(center[i]))
            return false;
        const float d = pos[i] - center[i];
        d2 += d * d;
    }
    const float limit = radius + margin;
    return d2 <= limit * limit;
}

// THE DRAWN ROOT NODE AGAINST THE SIMULATION'S ORIGIN, AT SPEED. A vehicle is drawn between two simulation ticks (a
// networked one where it was smoothed to), so its drawn root node stands off the simulation's origin by up to what
// it covers in a tick or two. A world unit holds at rest and at MCC's ground speeds; a flyer boosting in Project
// Reclaimer stood further off, and the vehicle cameras flipped frame by frame between the drawn and the simulated
// frame, which is a shake. The reach is a world unit and what 0.1 s covers at the speed the origin moves, along
// the way it moves only: off that line the root node must still stand within a world unit, so a model whose root
// sits elsewhere is not taken once it moves fast.
inline constexpr float kHalo3RootNodeRestReachWu = 1.0f;
inline constexpr float kHalo3RootNodeLagSeconds = 0.1f;
inline constexpr float kHalo3RootNodeMaxSpeedWu = 200.0f;
inline float Halo3RootNodeReachWu(float speedWuPerSecond) noexcept
{
    const float speed = std::isfinite(speedWuPerSecond)
        ? std::clamp(speedWuPerSecond, 0.0f, kHalo3RootNodeMaxSpeedWu) : 0.0f;
    return kHalo3RootNodeRestReachWu + speed * kHalo3RootNodeLagSeconds;
}

// The speed of a point that moves in steps, as the simulation's origin does once a tick: each step over the time
// since the step before. The first step, one after more than half a second, and a point that has not moved for
// that long are no speed.
struct Halo3StepSpeed
{
    float pos[3]{};
    float dir[3]{};      // the way the last step went (unit), 0 with no speed
    double changedAt = 0.0;
    float speed = 0.0f;
    bool valid = false;
};
inline float Halo3StepSpeedUpdate(Halo3StepSpeed& track, const float pos[3], double nowSeconds) noexcept
{
    if (!std::isfinite(nowSeconds) || !std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]))
        return track.speed = 0.0f;
    if (!track.valid)
    {
        for (int i = 0; i < 3; ++i)
            track.pos[i] = pos[i];
        track.changedAt = nowSeconds;
        track.speed = 0.0f;
        track.valid = true;
        return 0.0f;
    }
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i)
        d2 += (pos[i] - track.pos[i]) * (pos[i] - track.pos[i]);
    const double since = nowSeconds - track.changedAt;
    if (d2 > 0.0f)
    {
        const float step = std::sqrt(d2);
        track.speed = since > 0.0 && since <= 0.5 ? static_cast<float>(step / since) : 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            track.dir[i] = track.speed > 0.0f ? (pos[i] - track.pos[i]) / step : 0.0f;
            track.pos[i] = pos[i];
        }
        track.changedAt = nowSeconds;
    }
    else if (since > 0.5)
    {
        track.speed = 0.0f;
        track.dir[0] = track.dir[1] = track.dir[2] = 0.0f;
    }
    return track.speed;
}

// Whether a drawn root node `away` (world units from the simulation's origin) stands in for that origin: within a
// world unit off the line of motion, and along it within the reach the speed allows (Halo3RootNodeReachWu).
inline bool Halo3RootNodeWithinReach(const float away[3], const Halo3StepSpeed& track) noexcept
{
    float d2 = 0.0f, along = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(away[i]))
            return false;
        d2 += away[i] * away[i];
        along += away[i] * track.dir[i];
    }
    const float rest = kHalo3RootNodeRestReachWu;
    const float side2 = d2 - along * along;
    const float reach = Halo3RootNodeReachWu(track.speed);
    return side2 <= rest * rest && std::fabs(along) <= reach;
}

// Frame provenance, published for the probe log so a headset report can be
// matched to how its anchor was built.
enum class Halo3FrameStatus : uint32_t
{
    None = 0,
    Direct = 1,     // root object: +0x50 frame used as-is
    Composed = 2,   // attached child: parent ∘ local
    Insane = 3,     // sanity gate rejected the frame -> stock
    TooDeep = 4,    // more than one attachment level -> stock
    Carrier = 5,    // C8: mounted gunner anchored in the carrier's own frame
};

// C9 seat yaw — "the camera does not rotate with the car" (user, 2026-07-31).
// The FP seat camera moves the eye onto the authored point but leaves facing
// entirely HMD-owned in WORLD space (ApplyHeadLook builds its yaw from
// g_gameYawRef, which only a recenter or a cutscene boundary ever rebases).
// Drive a circle and the hull turns underneath a view pinned to the map, so
// "straight ahead" ends up out of the side of the vehicle. The fix is to add
// the hull's rotation since you sat down to that yaw reference.
//
// The rotation is applied by REBASING g_gameYawRef itself, one frame step at a
// time, rather than by publishing an offset consumers add. Everything the
// player sees is already expressed against that one reference — the view, the
// hand-aim ray (Game_ComputeAimStick), the rendered hands and gun
// (BuildTrackedGameBasis), the crosshair and the move stick — so rebasing it
// turns them all together and none can be forgotten. It also leaves the player
// facing the hull's final heading when they climb out, with no snap.
inline float Halo3WrapPi(float a)
{
    constexpr float kPi = 3.14159265f;
    constexpr float kTwoPi = 6.28318531f;
    if (!std::isfinite(a))
        return 0.0f;
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

struct Halo3SeatYaw
{
    bool armed = false;
    float lastYaw = 0.0f;   // previous hull heading
    float total = 0.0f;     // rotation folded into the reference (diagnostic)
};

// A hull heading needs a horizontal forward to exist at all. A Banshee pulled
// through the vertical has none, and atan2 of a near-zero pair swings wildly,
// so the follow holds the last valid heading until a usable sample returns.
inline constexpr float kHalo3SeatYawMinHorizontal = 0.25f;
// fwd = the seated parent's measured world forward (the CARRIER's for a mounted
// gunner). active means the same concrete FP seat owns the view. The caller
// keys that identity and skips torn/missing samples, so the next valid heading
// catches up from the last valid one instead of losing rotation.
//
// Returns this frame's yaw step to fold into the shared view/aim reference —
// a delta, not a total. Rebasing the one reference every consumer already
// shares (view, hand aim, rendered hands, crosshair, move stick) is what keeps
// them together; publishing a separate offset would have to be added at each
// of those sites and any one that was missed would slide by exactly the hull
// angle. That same reference is the vehicle's steering channel, which is why
// this cannot ship without the driver-seat steering author below: once the
// reference follows the hull, the closed loop's error stops depending on the
// hull's heading and a fixed hand offset becomes a constant turn RATE.
inline float Halo3SeatYawDelta(Halo3SeatYaw& state, const float fwd[3],
                               bool active)
{
    if (!active)
    {
        state.armed = false;
        state.total = 0.0f;
        return 0.0f;
    }
    // Preserve the last valid heading across a bad/vertical sample. Resetting
    // here permanently lost whatever turn occurred before the next good frame.
    if (!std::isfinite(fwd[0]) || !std::isfinite(fwd[1]))
        return 0.0f;
    const float horizontal =
        std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    if (horizontal < kHalo3SeatYawMinHorizontal)
        return 0.0f;
    const float yaw = std::atan2(fwd[1], fwd[0]);
    if (!state.armed)
    {
        // First seated frame: this heading is "straight ahead" from now on.
        state.armed = true;
        state.lastYaw = yaw;
        return 0.0f;
    }
    const float step = Halo3WrapPi(yaw - state.lastYaw);
    state.lastYaw = yaw;
    // A hard collision or spin can exceed the old 0.60-radian cap in one
    // sample. Concrete seat identity now rejects seat swaps/teleports, so every
    // finite wrapped step within that identity is real vehicle motion.
    state.total = Halo3WrapPi(state.total + step);
    return step;
}

// Pitch for the roll-stable vehicle-follow frame. The live rendered seat node
// supplies a complete basis, but vehicle roll is deliberately not consumed:
// suspension bank, a collision, or a flip must not roll the player's horizon.
// Forward gives the ground-vehicle elevation without consuming the node's up
// vector. Aircraft are excluded below because normal flight crosses the Euler
// vertical where a forward-only roll-free frame is not uniquely defined.
inline bool Halo3RollStablePitchFromForward(const float fwd[3],
                                             float& outPitch)
{
    if (!fwd || !std::isfinite(fwd[0]) || !std::isfinite(fwd[1]) ||
        !std::isfinite(fwd[2]))
        return false;
    const float horizontal =
        std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1]);
    const float length = std::sqrt(horizontal * horizontal + fwd[2] * fwd[2]);
    if (!std::isfinite(length) || length < 1.0e-4f)
        return false;
    outPitch = std::atan2(fwd[2], horizontal);
    return std::isfinite(outPitch);
}

// Compose a tracked yaw/pitch/roll basis inside the vehicle's yaw+pitch frame.
// `gameYawRef - hullYaw` is the player's artificial snap/smooth-turn offset.
// Multiplication order is load-bearing: on a hill, looking 90 degrees sideways
// must look along the vehicle's side rather than keep pitching every compass
// direction uphill. At hullPitch == 0 this reduces to the pre-feature mapping
// exactly. Columns are forward, left, up in Halo's game axes.
inline bool Halo3ComposeRollStableFollowBasis(
    float hullYaw, float hullPitch, float gameYawRef,
    float trackedYawDelta, float trackedPitch, float trackedRoll,
    float outBasis[9])
{
    if (!outBasis || !std::isfinite(hullYaw) ||
        !std::isfinite(hullPitch) || !std::isfinite(gameYawRef) ||
        !std::isfinite(trackedYawDelta) || !std::isfinite(trackedPitch) ||
        !std::isfinite(trackedRoll))
        return false;

    auto basisFromAngles = [](float yaw, float pitch, float roll,
                              float basis[9]) {
        const float cp = std::cos(pitch), sp = std::sin(pitch);
        const float cy = std::cos(yaw), sy = std::sin(yaw);
        const float cr = std::cos(roll), sr = std::sin(roll);
        basis[0] = cp * cy;
        basis[1] = cp * sy;
        basis[2] = sp;
        basis[6] = (-sp * cy) * cr + sy * sr;
        basis[7] = (-sp * sy) * cr - cy * sr;
        basis[8] = cp * cr;
        basis[3] = basis[7] * basis[2] - basis[8] * basis[1];
        basis[4] = basis[8] * basis[0] - basis[6] * basis[2];
        basis[5] = basis[6] * basis[1] - basis[7] * basis[0];
    };

    float hull[9], local[9];
    basisFromAngles(hullYaw, hullPitch, 0.0f, hull);
    const float localYaw = Halo3WrapPi(
        Halo3WrapPi(gameYawRef - hullYaw) + trackedYawDelta);
    basisFromAngles(localYaw, trackedPitch, trackedRoll, local);

    for (int column = 0; column < 3; ++column)
    {
        const int c = column * 3;
        for (int row = 0; row < 3; ++row)
        {
            outBasis[c + row] =
                hull[row] * local[c] +
                hull[3 + row] * local[c + 1] +
                hull[6 + row] * local[c + 2];
            if (!std::isfinite(outBasis[c + row]))
                return false;
        }
    }
    return true;
}

// Algebraic inverse of the follow basis.
// Reach's reticle deliberately does not use it: it projects world aim
// through the final completed compact-camera basis, which already includes
// pitch trim, View Follow, roll and headset smoothing. The Halo 3 / ODST
// dev assessment reticle does (Halo3SeatAimToVrReticle, below).
inline bool Halo3InverseRollStableFollowForward(
    const float worldForward[3], float hullYaw, float hullPitch,
    float gameYawRef, float& outTrackedYawDelta, float& outTrackedPitch)
{
    if (!worldForward || !std::isfinite(hullYaw) ||
        !std::isfinite(hullPitch) || !std::isfinite(gameYawRef) ||
        !std::isfinite(worldForward[0]) ||
        !std::isfinite(worldForward[1]) ||
        !std::isfinite(worldForward[2]))
    {
        return false;
    }

    const float worldLengthSquared =
        worldForward[0] * worldForward[0] +
        worldForward[1] * worldForward[1] +
        worldForward[2] * worldForward[2];
    if (!std::isfinite(worldLengthSquared) || worldLengthSquared < 1.0e-8f)
        return false;
    const float inverseWorldLength = 1.0f / std::sqrt(worldLengthSquared);
    const float normalizedWorld[3] = {
        worldForward[0] * inverseWorldLength,
        worldForward[1] * inverseWorldLength,
        worldForward[2] * inverseWorldLength};

    float hullBasis[9];
    if (!Halo3ComposeRollStableFollowBasis(
            hullYaw, hullPitch, hullYaw, 0.0f, 0.0f, 0.0f, hullBasis))
    {
        return false;
    }

    // Basis columns are forward, left, up, so H^-1 is H transpose.
    float local[3] = {
        normalizedWorld[0] * hullBasis[0] +
            normalizedWorld[1] * hullBasis[1] +
            normalizedWorld[2] * hullBasis[2],
        normalizedWorld[0] * hullBasis[3] +
            normalizedWorld[1] * hullBasis[4] +
            normalizedWorld[2] * hullBasis[5],
        normalizedWorld[0] * hullBasis[6] +
            normalizedWorld[1] * hullBasis[7] +
            normalizedWorld[2] * hullBasis[8]};
    const float localLengthSquared =
        local[0] * local[0] + local[1] * local[1] + local[2] * local[2];
    if (!std::isfinite(localLengthSquared) || localLengthSquared < 1.0e-8f)
        return false;
    const float inverseLocalLength = 1.0f / std::sqrt(localLengthSquared);
    local[0] *= inverseLocalLength;
    local[1] *= inverseLocalLength;
    local[2] *= inverseLocalLength;

    const float localYaw = std::atan2(local[1], local[0]);
    const float clampedZ = local[2] < -1.0f
        ? -1.0f
        : (local[2] > 1.0f ? 1.0f : local[2]);
    const float trackedYawDelta = Halo3WrapPi(
        localYaw - Halo3WrapPi(gameYawRef - hullYaw));
    const float trackedPitch = std::asin(clampedZ);
    if (!std::isfinite(trackedYawDelta) || !std::isfinite(trackedPitch))
        return false;
    outTrackedYawDelta = trackedYawDelta;
    outTrackedPitch = trackedPitch;
    return true;
}

// The DEV ASSESSMENT reticle in a seat (honest_reticle_assess = 2): the
// game's aim (world forward, game axes) expressed back in VR space through the
// mapping the seat's target came through, as the yaw/pitch the reticle is
// drawn along. The forward mapping is
//   delta = yawSign * wrap(vrYaw - headYawRef),  pitch = pitchSign * vrPitch,
// then the game angles are gameYawRef + delta and pitch, or, in the roll-stable
// vehicle-follow frame (`followed`), that pair composed into the hull's yaw and
// PITCH (Halo3ComposeRollStableFollowBasis). Undoing only the flat form in a
// followed seat leaves the hull pitch on the reticle: on a 20-degree slope the
// reticle sits ~20 degrees off the gun when it aims over the hood or the tail.
inline bool Halo3SeatAimToVrReticle(
    const float aimForward[3], bool followed, float hullYaw, float hullPitch,
    float gameYawRef, float headYawRef, float yawSign, float pitchSign,
    float& outVrYaw, float& outVrPitch)
{
    if (!aimForward || !std::isfinite(aimForward[0]) ||
        !std::isfinite(aimForward[1]) || !std::isfinite(aimForward[2]) ||
        !std::isfinite(gameYawRef) || !std::isfinite(headYawRef) ||
        !std::isfinite(yawSign) || !std::isfinite(pitchSign))
        return false;
    float trackedYawDelta = 0.0f, trackedPitch = 0.0f;
    if (followed)
    {
        if (!Halo3InverseRollStableFollowForward(
                aimForward, hullYaw, hullPitch, gameYawRef,
                trackedYawDelta, trackedPitch))
            return false;
    }
    else
    {
        const float z = aimForward[2] < -1.0f
            ? -1.0f
            : (aimForward[2] > 1.0f ? 1.0f : aimForward[2]);
        trackedYawDelta = Halo3WrapPi(
            std::atan2(aimForward[1], aimForward[0]) - gameYawRef);
        trackedPitch = std::asin(z);
    }
    const float vrYaw = headYawRef + yawSign * trackedYawDelta;
    const float vrPitch = pitchSign * trackedPitch;
    if (!std::isfinite(vrYaw) || !std::isfinite(vrPitch))
        return false;
    outVrYaw = vrYaw;
    outVrPitch = vrPitch;
    return true;
}

inline bool Halo3TransformBasisVector(const float basis[9],
                                      const float local[3], float out[3])
{
    if (!basis || !local || !out)
        return false;
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(local[i]))
            return false;
        for (int j = 0; j < 3; ++j)
            if (!std::isfinite(basis[i * 3 + j]))
                return false;
    }
    for (int row = 0; row < 3; ++row)
    {
        out[row] = basis[row] * local[0] + basis[3 + row] * local[1] +
            basis[6 + row] * local[2];
        if (!std::isfinite(out[row]))
            return false;
    }
    return true;
}


// ---- Virtual steering wheel --------------------------------------------
// Two gripped hands define a wheel by the line between them: its tilt out of
// horizontal, measured in the head's own heading plane, is the wheel angle. No
// fixed pivot, so the wheel is wherever the player's hands are and a grab
// never snaps the steering.
struct Halo3Wheel
{
    bool bothHeld = false;      // both grips are down right now
    bool engaged = false;       // the wheel is in your hands
    int clicks = 0;             // clicks banked toward a double-click
    uint64_t lastReleaseMs = 0;
    float steer = 0.0f;         // last authored steer, [-1, 1], + = right
};

inline constexpr float kHalo3WheelPressGrip = 0.6f;
inline constexpr float kHalo3WheelReleaseGrip = 0.4f;
inline constexpr uint64_t kHalo3WheelDoubleClickMs = 450;
inline constexpr float kHalo3WheelMinSpan = 0.15f;  // metres between hands

// Taking the wheel is a DOUBLE-CLICK of both grips, and letting go is another;
// it is not a hold. Two reasons, both from the user (2026-07-31):
//   * the right grip is already the dismount, so a sustained two-hand squeeze
//     would either eject the player or have to disable dismounting outright;
//   * nothing should have to be squeezed for the length of a drive.
// The grip BUTTONS are withheld from the game only while BOTH grips are down
// at once (Halo3WheelSwallowsGrips) — which is exactly this gesture and
// nothing else, so a lone right grip always dismounts, wheel on or off.
inline bool Halo3UpdateWheelToggle(Halo3Wheel& state, bool available,
                                   float gripLeft, float gripRight,
                                   uint64_t nowMs)
{
    if (!available || !std::isfinite(gripLeft) || !std::isfinite(gripRight))
    {
        state.bothHeld = false;
        state.engaged = false;
        state.clicks = 0;
        state.steer = 0.0f;
        return false;
    }
    // Hysteresis on the pair: down past 0.6, up below 0.4, so a hand resting
    // near the threshold cannot rattle out a click it never meant.
    const bool both = state.bothHeld
        ? (gripLeft >= kHalo3WheelReleaseGrip &&
           gripRight >= kHalo3WheelReleaseGrip)
        : (gripLeft > kHalo3WheelPressGrip &&
           gripRight > kHalo3WheelPressGrip);
    bool toggled = false;
    if (state.bothHeld && !both)
    {
        // A click completes on release, so the second release is the toggle.
        if (state.clicks == 1 &&
            nowMs - state.lastReleaseMs <= kHalo3WheelDoubleClickMs)
        {
            state.engaged = !state.engaged;
            state.clicks = 0;
            if (!state.engaged)
                state.steer = 0.0f;
            toggled = true;
        }
        else
        {
            state.clicks = 1;
            state.lastReleaseMs = nowMs;
        }
    }
    state.bothHeld = both;
    return toggled;
}

// The one condition under which the input hook withholds the grip buttons.
inline constexpr bool Halo3WheelSwallowsGrips(const Halo3Wheel& state)
{
    return state.bothHeld;
}

// left/right = tracked hand positions in OpenXR room space (Y up, -Z forward).
// headYaw = the room-space head heading (atan2(fwd.x, -fwd.z)), which is the
// plane the wheel is read in, so it works whichever way the player is facing.
// Returns true while the wheel owns steering; state.steer holds the value.
inline bool Halo3ComputeWheelSteer(Halo3Wheel& state, const float left[3],
                                   const float right[3], float headYaw,
                                   float maxDeg, float deadzoneDeg)
{
    const bool finite = std::isfinite(headYaw) && std::isfinite(left[0]) &&
        std::isfinite(left[1]) && std::isfinite(left[2]) &&
        std::isfinite(right[0]) && std::isfinite(right[1]) &&
        std::isfinite(right[2]);
    if (!state.engaged || !finite)
    {
        state.steer = 0.0f;
        return false;
    }

    const float dx = right[0] - left[0];
    const float dy = right[1] - left[1];
    const float dz = right[2] - left[2];
    const float span = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (span < kHalo3WheelMinSpan)
    {
        // Hands together is not a wheel. Because this is a toggle rather than
        // a hold, hands dropped into a lap must read as straight ahead — a
        // held-over steer would keep turning with nobody driving.
        state.steer = 0.0f;
        return true;
    }
    // Room right at the head's heading: forward is (sin, 0, -cos), so right is
    // (cos, 0, sin).
    const float lateral = dx * std::cos(headYaw) + dz * std::sin(headYaw);
    constexpr float kHalfPi = 1.57079633f;
    const float angle = lateral > 0.05f * span
        ? std::atan2(dy, lateral)
        : (dy >= 0.0f ? kHalfPi : -kHalfPi); // past 87 deg / hands crossed
    const float degrees = angle * 57.2957795f;
    const float dead = deadzoneDeg < 0.0f ? 0.0f : deadzoneDeg;
    const float full = maxDeg > dead + 1.0f ? maxDeg : dead + 1.0f;
    float magnitude = std::fabs(degrees) - dead;
    if (magnitude < 0.0f) magnitude = 0.0f;
    magnitude /= (full - dead);
    if (magnitude > 1.0f) magnitude = 1.0f;
    // Right hand UP tilts the wheel counter-clockwise, which is a left turn.
    state.steer = degrees > 0.0f ? -magnitude : magnitude;
    return true;
}

// C7 identity-probe field map (log-only; §E5, offline-verified, nothing keys
// a camera off these until a headset log confirms the in-process reads).
// Definition-relative tag_block records: +0x2C0 + type*0xC {count, address};
// element = tagDataBase + address*4. Discriminating fields:
inline constexpr uintptr_t kHalo3VehiclePhysicsBlocksOffset = 0x2C0;
inline constexpr size_t kHalo3VehiclePhysicsBlockStride = 0xC;
inline constexpr uintptr_t kHalo3JeepEngineMomentOffset = 0x14;   // element
inline constexpr uintptr_t kHalo3JeepTurnRateOffset = 0x10;       // element
inline constexpr uintptr_t kHalo3ScoutSpecificTypeOffset = 0x28;  // int8
inline constexpr uintptr_t kHalo3ScoutAccelerationOffset = 0x10;  // element
inline constexpr uintptr_t kHalo3DefMinFlipVelocityOffset = 0x39C;
inline constexpr uintptr_t kHalo3DefMaxFlipVelocityOffset = 0x3A0;
inline constexpr uintptr_t kHalo3DefBlurSpeedOffset = 0x3B4;

// Halo 3 cache-layout seat fields, independently described by the official
// H3EK seat exports and Assembly's Halo3MCC vehi plugin, and confirmed in the
// pinned retail module: `halo3+0x35579D` computes the occupied seat record as
// `seats_block + seat_index * 0xD4` and reads its flags dword at seat `+0x00`.
// The runtime patches only the currently occupied loaded tag, saving and
// restoring the original value; no map file is ever changed.
inline constexpr uintptr_t kHalo3VehicleSeatsBlockOffset = 0x258;
inline constexpr size_t kHalo3VehicleSeatStride = 0xD4;
inline constexpr uint32_t kHalo3SeatThirdPersonCameraBit = 1u << 4;
// Seat flags bit 2, `driver`: the seat that drives its vehicle (bit 3 is `gunner`; ODST's kOdstSeatDriverBit is the
// same bit). Every driver's seat logged in play carries it -- Warthog 0x48014, Elephant 0x10801C, Banshee
// 0x4080801C, Chopper 0x801C -- and neither of the Elephant's mounted guns does (0x101198, 0x5500018).
inline constexpr uint32_t kHalo3SeatDriverBit = 1u << 2;
// Seat flags bit 5, `allows weapons`: the occupant fires their OWN weapon
// rather than a vehicle barrel. Read straight off the loaded seat record, and
// confirmed against real Halo 3 values recorded during the ODST O5 work - the
// Warthog passenger's 0x1070 carries it, the driver's 0x40014 does not, which
// is exactly why only the passenger shows the parallax miss.
inline constexpr uint32_t kHalo3SeatAllowsWeaponsBit = 1u << 5;

inline constexpr bool Halo3SeatFiresPersonalWeapon(uint32_t seatFlags)
{
    return (seatFlags & kHalo3SeatAllowsWeaponsBit) != 0;
}
inline constexpr uintptr_t kHalo3SeatCameraTracksBlockOffset = 0x80;

// C20 — the one field that decides whether a seated player is first person.
//
// Halo 3 re-reads this bit from the LOADED TAG every camera frame; nothing is
// latched at seat entry. The verified retail chain in the pinned module is:
//   halo3+0x1326E8  reads the occupied seat's flags and returns bit 4 as
//                   "this seat is third person" (it also folds in bit 6,
//                   `third person on enter`, for the entry window);
//   halo3+0x212198  calls it and dispatches on the result — first person goes
//                   to the FP camera update at +0x17DAEC, third person to the
//                   following/chase camera at +0x2144C0;
//   halo3+0x3555EC  the seated camera evaluator, whose bit-4-clear branch at
//                   +0x3557F5 resolves the occupant's own `head` marker.
// Every stock Halo 3 player seat sets bit 4 (E1 exports), which is why the
// title has no first-person vehicle view of its own, and why the community
// first-person-vehicle map mod analysed in this document clears exactly this
// bit on the driver, passenger and gunner seats. Requiring the bit to be SET
// before patching is therefore also the live proof that the seat walk landed
// on a real player seat.
//
// C17 additionally zeroed the seat's camera-track count. The reference mod
// keeps those tracks, and C17 was headset-rejected; the track count is read
// here as a bounds check only and is never written.
inline constexpr bool Halo3SeatFlagsLookLikePlayerSeat(uint32_t seatFlags)
{
    return (seatFlags & kHalo3SeatThirdPersonCameraBit) != 0;
}

inline constexpr uint32_t Halo3FirstPersonSeatFlags(uint32_t seatFlags)
{
    return seatFlags & ~kHalo3SeatThirdPersonCameraBit;
}

// C21 — which origin the first-person arms and gun hang off.
//
// Halo's seated camera resolves the OCCUPANT'S `head` marker, so using the
// engine camera source as the hand origin in a vehicle seat parents the arms
// to the character's face: turning the view animates the head and drags the
// gun with it. The seat's own rigid placement is the body-side origin.
//
// Selection is deliberately conservative. The body origin is taken only when
// the feature is enabled AND the seat published a finite placement for this
// frame; every other case leaves the caller's existing origin untouched, so a
// missing, torn or unseated sample can only fall back to the exact behavior
// this replaces — never to an invented position.
inline bool Halo3SelectHandOrigin(bool followBody, bool bodyValid,
                                  const float body[3], float origin[3])
{
    if (!followBody || !bodyValid || !body || !origin)
        return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(body[i]))
            return false;
    for (int i = 0; i < 3; ++i)
        origin[i] = body[i];
    return true;
}

// C8 vehicle identity. Physics type is a CLASS, not a vehicle: (1,seat 0) is
// warthog AND mongoose, (3,seat 0) is ghost, wraith AND mauler. The C7
// session log (2026-07-31 09:32, Steam, cand ebd0e14) confirmed IN PROCESS
// that the §E5 definition reads work — the block-count array indexed exactly
// by physics type (jeep [0100000000], turret [0000010000], chopper
// [0000000010], tank [1000000000]) and `engine_moment` came back 2000.0 on
// the warthog and 650.0 on the mongoose, the two values E5 predicted offline.
// `turn_rate` resolved as radians (9.4 = 540 deg hog, 6.3 = 360 deg
// mongoose), which is the same fact in the field's own units.
enum class Halo3VehicleId : uint8_t
{
    Unknown = 0,   // never keys a camera: no point -> stock chase view
    Scorpion,
    Warthog,
    Mongoose,
    Ghost,
    Wraith,
    Mauler,        // the Prowler
    Banshee,
    Hornet,
    Chopper,
    // A turret the player walks up to and uses (physics type 5 with no
    // carrier under it). Not resolved from definition content: the machinegun
    // turret, plasma cannon and missile pod are not separable offline, and
    // their authored eye points sit within 0.09 wu (27 cm) of each other, so
    // one shared point is within authoring noise for all three.
    StationaryTurret,
    // The Elephant: tank physics like the Scorpion, told apart by its definition's bounding radius
    // (kHalo3ElephantBoundingRadiusWu). After every other entry so their numbers stay as they are; it has no
    // per-seat trim slot (config.h kVehicleTrimNames mirrors the ones before it) and no tuned eye point, so its
    // seats see from the occupant's head.
    Elephant,
};

// Definition-resident fields the sampler resolves ONCE per seat session (on a
// definition-index change), never per frame.
struct Halo3DefinitionFields
{
    int physicsType = -1;
    bool jeepValid = false;
    float engineMoment = 0.0f;
    bool scoutValid = false;
    int specificType = -1;
    // The definition's own bounding radius, world units, once the definition has proven it reads as a vehicle's
    // (Halo3DefinitionRadiusUsable); < 0 = not known.
    float boundingRadius = -1.0f;
};

// A PHYSICS TYPE IS A CLASS, NOT A VEHICLE: the Elephant has the Scorpion's tank physics, and a mod map's own
// vehicles take a stock one's (HugeGrass has flyers that read as Banshees and Hornets). The definition's bounding
// radius tells them apart -- a value of the vehicle's tag, which damage, riders and attachments never change. The
// live vehicle's own sphere is NOT that value in Halo 3: measured in play it stands 1.0 to 1.82 times the
// definition's (Warthog 1.0 against 1.52 wu, Banshee 1.4 against 1.5-1.8), so it is not used.
//
// The stock radii, read in play (Halo 3, 2026-10-19, "H3 vehicle definition" log lines): a vehicle its physics reads
// as one of these whose radius is another is not that vehicle. 0 = not measured: the physics alone decides.
// The Hornet's is read offline from the stock maps' own vehicle tags (objects\vehicles\hornet\hornet and Avalanche's
// hornet_lite alike, on every map that has one), the same definition field at +4 that play read for the three above
// -- and offline those three read exactly what play measured.
inline constexpr float kHalo3ScorpionBoundingRadiusWu = 2.200f; // 671 cm
inline constexpr float kHalo3ElephantBoundingRadiusWu = 5.129f; // 1563 cm
inline constexpr float kHalo3BansheeBoundingRadiusWu = 1.400f;  // 427 cm, the same in two sessions
inline constexpr float kHalo3HornetBoundingRadiusWu = 2.268f;   // 691 cm
// The rest of the stock vehicles, read the same way offline from their base tags (and play agrees: Warthog 1.000,
// Mongoose 0.800, Chopper 1.307). Every stock variant (snow, gauss, troop, anti-air, ...) has its base vehicle's
// radius. These six serve ONLY Halo3SameSizeStockVehicle: the physics rules above do not compare them, as before.
inline constexpr float kHalo3WarthogBoundingRadiusWu = 1.000f;  // 305 cm
inline constexpr float kHalo3MongooseBoundingRadiusWu = 0.800f; // 244 cm
inline constexpr float kHalo3GhostBoundingRadiusWu = 0.850f;    // 259 cm
inline constexpr float kHalo3WraithBoundingRadiusWu = 1.800f;   // 549 cm
inline constexpr float kHalo3MaulerBoundingRadiusWu = 1.435f;   // 437 cm (1.4348 in the tag)
inline constexpr float kHalo3ChopperBoundingRadiusWu = 1.307f;  // 398 cm (1.30717 in the tag)
// How far a radius may stand from a measured one, as a fraction of it, and still be that vehicle.
inline constexpr float kHalo3BoundingRadiusTolerance = 0.05f;

// A stock vehicle's definition radius; 0 for anything else.
inline float Halo3StockBoundingRadiusWu(Halo3VehicleId id) noexcept
{
    switch (id)
    {
    case Halo3VehicleId::Scorpion: return kHalo3ScorpionBoundingRadiusWu;
    case Halo3VehicleId::Elephant: return kHalo3ElephantBoundingRadiusWu;
    case Halo3VehicleId::Banshee: return kHalo3BansheeBoundingRadiusWu;
    case Halo3VehicleId::Hornet: return kHalo3HornetBoundingRadiusWu;
    case Halo3VehicleId::Warthog: return kHalo3WarthogBoundingRadiusWu;
    case Halo3VehicleId::Mongoose: return kHalo3MongooseBoundingRadiusWu;
    case Halo3VehicleId::Ghost: return kHalo3GhostBoundingRadiusWu;
    case Halo3VehicleId::Wraith: return kHalo3WraithBoundingRadiusWu;
    case Halo3VehicleId::Mauler: return kHalo3MaulerBoundingRadiusWu;
    case Halo3VehicleId::Chopper: return kHalo3ChopperBoundingRadiusWu;
    default: return 0.0f;
    }
}

inline bool Halo3RadiusMatches(float radiusWu, float measuredWu) noexcept
{
    return measuredWu > 0.0f && radiusWu >= 0.0f && std::isfinite(radiusWu) &&
           std::fabs(radiusWu - measuredWu) <= kHalo3BoundingRadiusTolerance * measuredWu;
}

// The definition's radius is only taken when the definition opens as a vehicle's does: its object type (the int16
// two fields before the radius) is the vehicle type, and the radius is a size a vehicle can have (measured: 0.8 wu
// for a turret to 1.4 for the Banshee). That is the proof that the field read is the bounding radius on this build.
inline bool Halo3DefinitionRadiusUsable(int objectType, float radiusWu) noexcept
{
    return objectType == kHalo3ObjectKindVehicle && std::isfinite(radiusWu) && radiusWu >= 0.1f && radiusWu <= 30.0f;
}

// Is a vehicle the stock one its physics reads as? Yes when its radius is the stock one's, and when either radius is
// unknown (not proven, or not measured): with nothing to compare, the physics decides as it always has.
inline bool Halo3StockRadiusFits(float boundingRadiusWu, float stockRadiusWu) noexcept
{
    if (!(stockRadiusWu > 0.0f) || !(boundingRadiusWu >= 0.0f))
        return true;
    return Halo3RadiusMatches(boundingRadiusWu, stockRadiusWu);
}

// A tank: the Scorpion or the Elephant by its radius; another tank is one the mod does not know. Unproven: the
// Scorpion, as before.
inline Halo3VehicleId Halo3ResolveTank(float boundingRadiusWu) noexcept
{
    if (!(boundingRadiusWu >= 0.0f) || Halo3RadiusMatches(boundingRadiusWu, kHalo3ScorpionBoundingRadiusWu))
        return Halo3VehicleId::Scorpion;
    if (Halo3RadiusMatches(boundingRadiusWu, kHalo3ElephantBoundingRadiusWu))
        return Halo3VehicleId::Elephant;
    return Halo3VehicleId::Unknown;
}

// Tolerances are wide enough for any float representation of the authored
// constant and far narrower than the gap between the two authored values
// (650 vs 2000). Anything unrecognised stays Unknown — a wrong identity puts
// the player's head inside another vehicle, a missing one costs the effect.
inline constexpr float kHalo3WarthogEngineMoment = 2000.0f;
inline constexpr float kHalo3MongooseEngineMoment = 650.0f;
inline constexpr float kHalo3EngineMomentTolerance = 200.0f;

inline Halo3VehicleId Halo3ResolveVehicleId(const Halo3DefinitionFields& def)
{
    switch (def.physicsType)
    {
    case 0: return Halo3ResolveTank(def.boundingRadius); // human_tank: the Scorpion, or the Elephant
    case 4:                                        // alien_fighter: the Banshee, or a mod's own flyer
        return Halo3StockRadiusFits(def.boundingRadius, kHalo3BansheeBoundingRadiusWu) ? Halo3VehicleId::Banshee
                                                                                         : Halo3VehicleId::Unknown;
    case 7:                                        // vtol: the Hornet, or a mod's own flyer
        return Halo3StockRadiusFits(def.boundingRadius, kHalo3HornetBoundingRadiusWu) ? Halo3VehicleId::Hornet
                                                                                        : Halo3VehicleId::Unknown;
    case 8: return Halo3VehicleId::Chopper;       // chopper
    case 1:                                        // human_jeep
        if (!def.jeepValid || !std::isfinite(def.engineMoment))
            return Halo3VehicleId::Unknown;
        if (std::fabs(def.engineMoment - kHalo3WarthogEngineMoment) <=
            kHalo3EngineMomentTolerance)
            return Halo3VehicleId::Warthog;
        if (std::fabs(def.engineMoment - kHalo3MongooseEngineMoment) <=
            kHalo3EngineMomentTolerance)
            return Halo3VehicleId::Mongoose;
        return Halo3VehicleId::Unknown;
    case 3:                                        // alien_scout
        if (!def.scoutValid)
            return Halo3VehicleId::Unknown;
        if (def.specificType == 1) return Halo3VehicleId::Ghost;
        if (def.specificType == 3) return Halo3VehicleId::Wraith;
        if (def.specificType == 4) return Halo3VehicleId::Mauler;
        return Halo3VehicleId::Unknown;
    default:
        return Halo3VehicleId::Unknown;            // incl. 5 (turret)
    }
}

// THE STOCK VEHICLES BY TAG PATH. A physics type says what CLASS a vehicle is; its tag path (halo3_tag_names.h) says
// which vehicle: HugeGrass's Sparrowhawk and Falcon have the Hornet's physics, its Mammoth the Warthog's, its
// Spectre the Ghost's. This is every vehicle tag of the 38 stock Halo 3 maps whose physics reads as one of the
// vehicles above, read offline from the maps' tag indices with the definition fields Halo3ResolveVehicleId reads.
// The stock tags that read as none (the turret guns, which take their carrier's identity, the stationary turrets,
// the Pelican, the Phantom, the Shade and the props) need no entry: they are Unknown either way.
struct Halo3StockVehiclePath
{
    const char* path;
    Halo3VehicleId id;
};
inline constexpr Halo3StockVehiclePath kHalo3StockVehiclePaths[] = {
    {"objects\\vehicles\\scorpion\\scorpion", Halo3VehicleId::Scorpion},
    {"objects\\vehicles\\scorpion\\scorpion_snow", Halo3VehicleId::Scorpion},
    {"objects\\levels\\multi\\shrine\\behemoth\\behemoth", Halo3VehicleId::Elephant},
    {"objects\\vehicles\\warthog\\warthog", Halo3VehicleId::Warthog},
    {"objects\\vehicles\\warthog\\warthog_snow", Halo3VehicleId::Warthog},
    {"objects\\vehicles\\warthog\\warthog_gauss", Halo3VehicleId::Warthog},
    {"objects\\vehicles\\warthog\\warthog_gauss_snow", Halo3VehicleId::Warthog},
    {"objects\\vehicles\\warthog\\warthog_troop", Halo3VehicleId::Warthog},
    {"objects\\vehicles\\warthog\\warthog_wrecked_turret", Halo3VehicleId::Warthog},
    {"objects\\vehicles\\mongoose\\mongoose", Halo3VehicleId::Mongoose},
    {"objects\\vehicles\\mongoose\\mongoose_snow", Halo3VehicleId::Mongoose},
    {"objects\\vehicles\\ghost\\ghost", Halo3VehicleId::Ghost},
    {"objects\\vehicles\\ghost\\ghost_snow", Halo3VehicleId::Ghost},
    {"objects\\vehicles\\wraith\\wraith", Halo3VehicleId::Wraith},
    {"objects\\vehicles\\wraith\\wraith_anti_air", Halo3VehicleId::Wraith},
    {"objects\\vehicles\\mauler\\mauler", Halo3VehicleId::Mauler},
    {"objects\\vehicles\\banshee\\banshee", Halo3VehicleId::Banshee},
    {"objects\\vehicles\\banshee\\banshee_ambient", Halo3VehicleId::Banshee},
    {"objects\\vehicles\\hornet\\hornet", Halo3VehicleId::Hornet},
    {"objects\\levels\\dlc\\sidewinder\\hornet_lite\\hornet_lite", Halo3VehicleId::Hornet},
    {"objects\\vehicles\\brute_chopper\\brute_chopper", Halo3VehicleId::Chopper},
};

// What a definition's tag path says, as one byte (the camera thread reads it per tag index): a stock vehicle's
// Halo3VehicleId, or one of these two.
inline constexpr uint8_t kHalo3PathNoName = 0xFF;   // no name to go by: the physics and the radius decide alone
inline constexpr uint8_t kHalo3PathNotStock = 0xFE; // a name that is no stock vehicle's

inline uint8_t Halo3PathVerdict(const char* path, size_t length) noexcept
{
    if (!path || !length)
        return kHalo3PathNoName;
    const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    for (const Halo3StockVehiclePath& stock : kHalo3StockVehiclePaths)
    {
        size_t i = 0;
        while (i < length && stock.path[i] && lower(path[i]) == stock.path[i])
            ++i;
        if (i == length && !stock.path[i])
            return static_cast<uint8_t>(stock.id);
    }
    return kHalo3PathNotStock;
}

// Does a definition pass stock vehicle `id`'s physics and radius checks? Where there is nothing to compare (a radius
// not proven on this build, or not measured) the class decides, as in Halo3ResolveVehicleId -- so the Elephant's
// path with tank physics is the Elephant even when its radius could not be read.
inline bool Halo3FitsStockVehicle(const Halo3DefinitionFields& def, Halo3VehicleId id) noexcept
{
    switch (id)
    {
    case Halo3VehicleId::Scorpion:
        return def.physicsType == 0 && Halo3StockRadiusFits(def.boundingRadius, kHalo3ScorpionBoundingRadiusWu);
    case Halo3VehicleId::Elephant:
        return def.physicsType == 0 && Halo3StockRadiusFits(def.boundingRadius, kHalo3ElephantBoundingRadiusWu);
    case Halo3VehicleId::Banshee:
        return def.physicsType == 4 && Halo3StockRadiusFits(def.boundingRadius, kHalo3BansheeBoundingRadiusWu);
    case Halo3VehicleId::Hornet:
        return def.physicsType == 7 && Halo3StockRadiusFits(def.boundingRadius, kHalo3HornetBoundingRadiusWu);
    case Halo3VehicleId::Chopper:
        return def.physicsType == 8;
    case Halo3VehicleId::Warthog:
    case Halo3VehicleId::Mongoose:
    case Halo3VehicleId::Ghost:
    case Halo3VehicleId::Wraith:
    case Halo3VehicleId::Mauler:
        return Halo3ResolveVehicleId(def) == id;
    default:
        return false;
    }
}

// THE SAME SIZE AS A STOCK VEHICLE. A tag at a path of its own (a mod's warthog_fast) whose physics read as a stock
// vehicle AND whose PROVEN definition radius is that vehicle's (Halo3RadiusMatches) is taken for it: its tuned seat
// points, cameras and steering. Anything else is one the mod does not know: another size (a Spectre has the Ghost's
// physics at 1.3 wu against 0.85), a radius not proven on this build (it never promotes), or no stock radius.
inline Halo3VehicleId Halo3SameSizeStockVehicle(const Halo3DefinitionFields& def) noexcept
{
    const Halo3VehicleId candidate = Halo3ResolveVehicleId(def);
    if (candidate == Halo3VehicleId::Unknown || candidate == Halo3VehicleId::StationaryTurret ||
        !(def.boundingRadius >= 0.0f))
        return Halo3VehicleId::Unknown;
    return Halo3RadiusMatches(def.boundingRadius, Halo3StockBoundingRadiusWu(candidate)) ? candidate
                                                                                          : Halo3VehicleId::Unknown;
}

// THE RULE WITH NAMES. No name: the physics and the radius, exactly as without names. A stock vehicle's path: that
// vehicle when its physics and radius agree, else one the mod does not know (a mod map can put another vehicle at a
// stock path). Any other path: the stock vehicle it is the same size as (Halo3SameSizeStockVehicle), else one the mod
// does not know -- the Sparrowhawk is not a Hornet, whatever its physics.
inline Halo3VehicleId Halo3ResolveVehicleIdByPath(const Halo3DefinitionFields& def, uint8_t pathVerdict) noexcept
{
    if (pathVerdict == kHalo3PathNoName)
        return Halo3ResolveVehicleId(def);
    if (pathVerdict == kHalo3PathNotStock)
        return Halo3SameSizeStockVehicle(def);
    if (pathVerdict == static_cast<uint8_t>(Halo3VehicleId::Unknown))
        return Halo3VehicleId::Unknown;
    const auto id = static_cast<Halo3VehicleId>(pathVerdict);
    return Halo3FitsStockVehicle(def, id) ? id : Halo3VehicleId::Unknown;
}

// What a seat's vehicle is called (Halo3SeatVehicleName): the name its camera file entry, seat memory and note go by.
enum class Halo3VehicleNameSource : uint8_t
{
    Identity,    // the identity's own name ("warthog")
    Tag,         // its tag's short name ("warthog_fast", "sparrowhawk")
    Fingerprint, // "custom_" and its tags' fingerprint
};
// A stock vehicle (a stock path, or no names to go by) and a stationary turret: the identity's name. A stand-in at a
// path of its own (a known identity whose path is no stock vehicle's: Halo3SameSizeStockVehicle) keeps its own name,
// so a variant has its own seat memory and note. One the mod does not know: its tag's name, else its fingerprint.
inline Halo3VehicleNameSource Halo3SeatNameSource(Halo3VehicleId id, uint8_t pathVerdict, bool haveTagName) noexcept
{
    if (id == Halo3VehicleId::StationaryTurret)
        return Halo3VehicleNameSource::Identity;
    if (id != Halo3VehicleId::Unknown)
        return pathVerdict == kHalo3PathNotStock && haveTagName ? Halo3VehicleNameSource::Tag
                                                                : Halo3VehicleNameSource::Identity;
    return haveTagName ? Halo3VehicleNameSource::Tag : Halo3VehicleNameSource::Fingerprint;
}

// User-authored first-person seat points (Blender kit, vehicle/carrier tag
// space in world units).
// Keyed by RESOLVED VEHICLE IDENTITY, not physics type, so a mongoose can
// never wear the warthog's eye point. `carrierFrame` marks the mounted-turret
// gunner seats: those points are authored in the CARRIER's frame (and
// `vehicle` is then the CARRIER's identity), because an attached child stores
// its transform relative to the parent's ATTACHMENT NODE. C18 maps every point
// through the selected node's stored default inverse and live rendered matrix;
// mounted points select the carrier plus the gun child's attachment node.
//
// Values are the user's own Blender placements (kit v3, every vehicle at the
// world origin, metres / 3.048), read back 2026-07-31 with the parse guard
// that refuses any camera whose evaluated and stored transforms disagree.
struct Halo3SeatPoint
{
    Halo3VehicleId vehicle;
    int8_t seatIndex;
    bool carrierFrame;
    float x, y, z;
};

inline constexpr Halo3SeatPoint kHalo3SeatPoints[] = {
    // Root vehicles — the seated parent owns the frame.
    {Halo3VehicleId::Warthog,  0, false,  0.0299f,  0.1683f, 0.6663f},
    {Halo3VehicleId::Warthog,  1, false, -0.0139f, -0.1903f, 0.6601f},
    {Halo3VehicleId::Mongoose, 0, false, -0.0476f,  0.0000f, 0.5207f},
    {Halo3VehicleId::Mongoose, 1, false, -0.4368f,  0.0000f, 0.5967f},
    {Halo3VehicleId::Ghost,    0, false, -0.1078f,  0.0000f, 0.4491f},
    {Halo3VehicleId::Wraith,   0, false,  0.3060f,  0.0000f, 1.0573f},
    {Halo3VehicleId::Mauler,   0, false, -1.0738f,  0.0000f, 0.6381f},
    {Halo3VehicleId::Mauler,   1, false, -0.2252f,  0.4659f, 0.7430f},
    {Halo3VehicleId::Mauler,   2, false, -0.2389f, -0.4790f, 0.6764f},
    {Halo3VehicleId::Chopper,  0, false, -0.4396f,  0.0000f, 0.7348f},
    {Halo3VehicleId::Banshee,  0, false,  0.4190f,  0.0000f, 0.4119f},
    {Halo3VehicleId::Hornet,   0, false,  1.0059f, -0.0098f, 0.6517f},
    {Halo3VehicleId::Hornet,   1, false,  0.5215f,  0.3566f, 0.5583f},
    {Halo3VehicleId::Hornet,   2, false,  0.5208f, -0.3379f, 0.5830f},
    // Scorpion driver: the user's forward/height, with the lateral the tag
    // itself authors (+0.189, the hatch is left of the turret ring). Their
    // saved file still had this one camera untouched on the centre line.
    {Halo3VehicleId::Scorpion, 0, false,  0.0326f,  0.1893f, 1.0226f},

    // Mounted turret gunners — identified and anchored by the CARRIER.
    {Halo3VehicleId::Warthog,  0, true,  -0.5832f,  0.0000f, 1.0624f},
    {Halo3VehicleId::Scorpion, 0, true,   0.4673f, -0.1903f, 0.8659f},
    {Halo3VehicleId::Wraith,   0, true,  -0.1886f,  0.0000f, 1.1956f},
    {Halo3VehicleId::Mauler,   0, true,   0.0054f,  0.0000f, 0.9307f},

    // Walk-up turrets — the machinegun turret's point, shared (see the enum).
    {Halo3VehicleId::StationaryTurret, 0, false, -0.2368f, 0.0000f, 0.5743f},
};

// `vehicle` is the seated parent's identity for a normal seat, or the
// CARRIER's identity when the player is a mounted-turret gunner. Unknown
// never matches, so an unidentified vehicle always falls back to stock.
inline constexpr const Halo3SeatPoint* Halo3FindSeatPoint(
    Halo3VehicleId vehicle, int seatIndex, bool mountedTurret)
{
    if (vehicle == Halo3VehicleId::Unknown)
        return nullptr;
    for (const Halo3SeatPoint& p : kHalo3SeatPoints)
    {
        if (p.vehicle == vehicle && p.carrierFrame == mountedTurret &&
            static_cast<int>(p.seatIndex) == seatIndex)
            return &p;
    }
    return nullptr;
}

// The seat that steers. Every vehicle in the authored table puts its driver at
// seat 0 of the vehicle itself; a mounted turret gunner is seat 0 of the GUN,
// and a walk-up turret has no vehicle under it at all. Unknown never steers —
// an unidentified vehicle keeps the stock closed-loop aim it has today.
inline constexpr bool Halo3SeatIsDriver(Halo3VehicleId id, int seatIndex,
                                        bool mountedTurret)
{
    return !mountedTurret && seatIndex == 0 &&
        id != Halo3VehicleId::Unknown &&
        id != Halo3VehicleId::StationaryTurret;
}

// Halo 3 drives its vehicles two different ways. Most are LOOK-STEERED: the
// right stick turns the vehicle, and the engine's "aim" for that seat is the
// hull's own heading. The Scorpion and the Wraith are not — their hull turns
// from the left stick and the right stick aims a weapon that swings
// independently of the hull.
//
// Only the first family may have its steering authored, and it is exactly the
// family the hull-follow view breaks: when the aim IS the hull, rebasing the
// reference by the hull's rotation cancels the closed loop's feedback. In the
// second family the aim is a separate heading, so the loop keeps real feedback
// and must be left alone — the driver is aiming a turret, not steering.
// Getting this wrong for a vehicle costs that vehicle its motion steering,
// never its controls.
inline constexpr bool Halo3VehicleIsLookSteered(Halo3VehicleId id)
{
    switch (id)
    {
    case Halo3VehicleId::Warthog:
    case Halo3VehicleId::Mongoose:
    case Halo3VehicleId::Ghost:
    case Halo3VehicleId::Mauler:
    case Halo3VehicleId::Chopper:
    case Halo3VehicleId::Banshee:
    case Halo3VehicleId::Hornet:
        return true;
    default: // Scorpion, Wraith, turrets, Unknown
        return false;
    }
}

// A wheel is a ground control. Aircraft are look-steered too, so they also need
// their steering authored, but a wheel is the wrong shape for one: their stick
// is a flight control and its vertical axis is climb. They take the plain stick
// here and a two-hand yoke later.
inline constexpr bool Halo3VehicleUsesWheel(Halo3VehicleId id)
{
    return Halo3VehicleIsLookSteered(id) &&
        id != Halo3VehicleId::Banshee && id != Halo3VehicleId::Hornet;
}

// THE CONTROL CLASS: the stock vehicle a vehicle is DRIVEN like, from its physics alone. A vehicle the mod cannot name
// (a mod map's own) still says in its physics whether it is a tank, a jeep, a flyer or a turret, and its seat's tag
// says whether a seat drives it; that is all the seat aim policy (seat_drive_policy.h) needs, so such a seat is aimed
// like the stock seat it is driven like instead of not at all. The class never keys a camera, an eye point or a
// name: those stay by identity. halo3.dll names the physics blocks in this order: human_tank, human_jeep,
// human_plane, alien_scout, alien_fighter, turret, mantis, vtol, chopper, guardian; in play the Pelican and the
// Phantom read as human_plane, the Sparrowhawk as vtol, and the Seraph, the Longsword and HugeGrass's other fighters
// as alien_fighter.
inline Halo3VehicleId Halo3ControlClass(const Halo3DefinitionFields& def)
{
    switch (def.physicsType)
    {
    case 0: return Halo3VehicleId::Scorpion;          // human_tank: the look aims a turret on the hull
    case 1: return Halo3VehicleId::Warthog;           // human_jeep
    case 2: return Halo3VehicleId::Hornet;            // human_plane: flown like the vtol
    case 3:                                           // alien_scout: the Wraith's look aims its mortar
        if (!def.scoutValid)
            return Halo3VehicleId::Unknown;
        return def.specificType == 3 ? Halo3VehicleId::Wraith : Halo3VehicleId::Ghost;
    case 4: return Halo3VehicleId::Banshee;           // alien_fighter
    case 5: return Halo3VehicleId::StationaryTurret;  // turret
    case 7: return Halo3VehicleId::Hornet;            // vtol
    case 8: return Halo3VehicleId::Chopper;           // chopper
    default: return Halo3VehicleId::Unknown;          // mantis, guardian, a giant or a biped (no physics block)
    }
}

// The seat as the aim policy sees it: whether it drives the vehicle, whether the vehicle turns toward its look, and
// whether it flies (an aircraft's seat has no turning stop). A named vehicle answers by its identity, as before. A
// vehicle the mod cannot name answers from its seat's own tag flags (the occupied seat's, as loaded) and its control
// class; with neither it is a seat that does not drive, whose aim the hand then owns -- every write is still held
// until the seat proves its record holds the game's aim (seat_drive::StintCheck).
struct Halo3SeatRole
{
    bool driver = false;
    bool lookSteered = false;
    bool aircraft = false;
};
inline Halo3SeatRole Halo3SeatRoleOf(Halo3VehicleId id, Halo3VehicleId controlClass, int seatIndex, bool mounted,
                                     uint32_t seatFlags)
{
    Halo3SeatRole role;
    if (id != Halo3VehicleId::Unknown)
    {
        role.driver = Halo3SeatIsDriver(id, seatIndex, mounted);
        role.lookSteered = Halo3VehicleIsLookSteered(id);
        role.aircraft = id == Halo3VehicleId::Banshee || id == Halo3VehicleId::Hornet;
        return role;
    }
    role.driver = !mounted && (seatFlags & kHalo3SeatDriverBit) != 0;
    role.lookSteered = Halo3VehicleIsLookSteered(controlClass);
    role.aircraft = controlClass == Halo3VehicleId::Banshee || controlClass == Halo3VehicleId::Hornet;
    return role;
}

// Which seats may take the hull-follow view at all. The hazard the follow
// creates is specific: if the frame it follows is the same heading the engine's
// aim channel drives, the closed-loop hand aim loses its feedback and a fixed
// hand offset becomes a runaway turn. Seat by seat:
//   * driver of a look-steered vehicle — the aim IS the hull, so it is only
//     allowed because the steering author below replaces that loop;
//   * every other seat in the table — the frame is the hull (or, for a mounted
//     gunner, the CARRIER) while the aim is a turret or the occupant's own
//     weapon, so the loop keeps real feedback;
//   * a walk-up turret has no vehicle under it, so the frame published for that
//     seat is the turret's own object, which may well be what its aim turns.
//     Nothing authors its steering, so it is left exactly as it is;
//   * Unknown has no authored point, so its camera is the stock chase view and
//     there is no "the camera does not turn" to fix.
inline constexpr bool Halo3SeatFollowsHull(Halo3VehicleId id, int seatIndex,
                                           bool mountedTurret)
{
    if (id == Halo3VehicleId::Unknown ||
        id == Halo3VehicleId::StationaryTurret)
        return false;
    return Halo3FindSeatPoint(id, seatIndex, mountedTurret) != nullptr;
}

// Banshees/Hornets cross vertical in normal flight, where forward alone cannot
// choose a continuous roll-free up vector. Their driver pitch is also flight
// feedback. Keep every aircraft seat on the proven yaw-only path until a full
// transported flight frame/yoke author exists. Ground/racing-wheel drivers,
// passengers, gunners, and tanks consume the roll-stable pitch frame.
inline constexpr bool Halo3SeatFollowsPitch(Halo3VehicleId id, int seatIndex,
                                            bool mountedTurret)
{
    if (!Halo3SeatFollowsHull(id, seatIndex, mountedTurret))
        return false;
    return id != Halo3VehicleId::Banshee && id != Halo3VehicleId::Hornet;
}

// THE SAME TWO QUESTIONS FOR ANY SEAT WITH A VIEW, a seat with no tuned point included: a vehicle the mod cannot
// name (a mod map's own) or one it names without one (the Elephant). A tuned seat answers exactly as above. A seat
// with no tuned point has its view at the occupant's head (`seatEye`), and its view may follow its hull wherever that
// is safe for the reason above: never on a turret's own frame, and in a driver's seat (`driver`, Halo3SeatRoleOf)
// only where it is known how the vehicle steers -- by its name, or by its control class (Halo3ControlClass) for one
// the mod cannot name: a look-steered one has its steering authored as the stock vehicle's is
// (Halo3SeatRoleAuthorsSteering), a tank's look aims its gun. The driver of a vehicle of no known class (a walker, a
// giant) keeps the view as it is. An aircraft, by name or by class, keeps the yaw-only follow.
inline constexpr bool Halo3SeatViewFollowsHull(Halo3VehicleId id, Halo3VehicleId controlClass, bool driver,
                                               int seatIndex, bool mountedTurret, bool seatEye)
{
    if (id == Halo3VehicleId::StationaryTurret)
        return false;
    if (Halo3SeatFollowsHull(id, seatIndex, mountedTurret))
        return true;
    if (!seatEye || controlClass == Halo3VehicleId::StationaryTurret)
        return false;
    return !driver || id != Halo3VehicleId::Unknown || controlClass != Halo3VehicleId::Unknown;
}
inline constexpr bool Halo3SeatViewFollowsPitch(Halo3VehicleId id, Halo3VehicleId controlClass, bool driver,
                                                int seatIndex, bool mountedTurret, bool seatEye)
{
    const Halo3VehicleId flown = id != Halo3VehicleId::Unknown ? id : controlClass;
    return Halo3SeatViewFollowsHull(id, controlClass, driver, seatIndex, mountedTurret, seatEye) &&
        flown != Halo3VehicleId::Banshee && flown != Halo3VehicleId::Hornet;
}

// A seat whose weapon is a physically slewing gun rather than the occupant's
// own: a walk-up emplacement or any mounted gunner. Their aim carries an
// authored rate cap the closed loop cannot exceed. From H3EK seat blocks,
// `shade_d` is 15 deg/s on both axes and `warthog_g` is 60 deg/s yaw with NO
// pitch cap at all - which is why pitch is the axis that visibly oscillates.
// These seats take the servo's rest hysteresis and the barrel-riding reticle;
// every other seat keeps exactly the loop it shipped with.
inline constexpr bool Halo3SeatIsTurret(Halo3VehicleId id, bool mountedTurret)
{
    return mountedTurret || id == Halo3VehicleId::StationaryTurret;
}

// The AUTOMATIC rule for handing a seat's steering to the wheel/stick author
// instead of the closed-loop hand aim: it switches on the same condition that
// makes the view follow the hull, which is why the follow toggle is an
// argument. With the toggle off nothing rotates, the closed loop still has its
// feedback, and every control stays exactly as Alpha 0.3.1 shipped it.
// vehicle_control_logic.h holds the player's own choice, of which this is the
// default; with the argument true it answers "is this a look-steered driver?".
inline constexpr bool Halo3SeatAuthorsSteering(Halo3VehicleId id, int seatIndex,
                                               bool mountedTurret,
                                               bool followEnabled)
{
    return followEnabled && Halo3SeatIsDriver(id, seatIndex, mountedTurret) &&
        Halo3VehicleIsLookSteered(id);
}

// Halo3SeatAuthorsSteering and Halo3VehicleUsesWheel for any seat, by its role (Halo3SeatRoleOf): a vehicle the mod
// cannot name is a look-steered driver's by its seat's driver flag and its control class. A named vehicle's role is
// its identity's, so for it these answer exactly as those do.
inline constexpr bool Halo3SeatRoleAuthorsSteering(const Halo3SeatRole& role, bool followEnabled)
{
    return followEnabled && role.driver && role.lookSteered;
}
inline constexpr bool Halo3SeatRoleUsesWheel(const Halo3SeatRole& role)
{
    return role.lookSteered && !role.aircraft;
}

// The right stick is normally the steering fallback in an authored driver
// seat, so VR turn must not consume it too. Once the two-hand wheel is actively
// authoring steering, the stick is free and resumes the configured snap/smooth
// view turn. Everywhere else VR turn retains its established ownership.
inline constexpr bool Halo3VrTurnOwnsStick(bool seatAuthorsSteering,
                                           bool wheelActive)
{
    return !seatAuthorsSteering || wheelActive;
}

// Keep snap-turn edge state coherent while the stick temporarily belongs to
// steering. A deflected takeover must not snap immediately; after the player
// recentres, the next deliberate deflection produces exactly one snap.
inline bool Halo3ConsumeSnapTurn(bool turnOwnsStick, float stickX,
                                 bool& latched)
{
    if (!std::isfinite(stickX))
        return false;
    const float magnitude = std::fabs(stickX);
    if (!turnOwnsStick)
    {
        if (magnitude > 0.6f)
            latched = true;
        else if (magnitude < 0.3f)
            latched = false;
        return false;
    }
    if (!latched && magnitude > 0.6f)
    {
        latched = true;
        return true;
    }
    if (magnitude < 0.3f)
        latched = false;
    return false;
}

// C4 transform probe: bounded float window of the seated DIRECT parent's
// object data, captured so a drive session can measure where the vehicle's
// world position and orientation vectors live (the Blender-authored seat
// points are in the direct parent's model space and need that transform).
// Nothing downstream consumes offsets until the log proves them.
inline constexpr uintptr_t kHalo3ParentCaptureBase = 0x14;
inline constexpr size_t kHalo3ParentCaptureFloats = 56; // +0x14..+0xF4

// Pure analysis for the C4 worker: find float triplets of ~unit length (the
// signature of orientation basis vectors) and the triplet nearest a reference
// point (the signature of the object position, referenced against the
// C1-proven observer focus).
struct Halo3UnitTripletScan
{
    int count = 0;
    int indices[12] = {};
};

inline Halo3UnitTripletScan Halo3FindUnitTriplets(
    const float* window, size_t floatCount, float tolerance)
{
    Halo3UnitTripletScan scan{};
    if (!window)
        return scan;
    for (int i = 0; i + 2 < static_cast<int>(floatCount); ++i)
    {
        const float a = window[i], b = window[i + 1], c = window[i + 2];
        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c))
            continue;
        const float len = std::sqrt(a * a + b * b + c * c);
        if (std::fabs(len - 1.0f) <= tolerance)
        {
            if (scan.count < 12)
                scan.indices[scan.count] = i;
            ++scan.count;
        }
    }
    return scan;
}

struct Halo3NearestTriplet
{
    int index = -1;
    float distance = 0.0f;
};

inline Halo3NearestTriplet Halo3FindNearestTriplet(
    const float* window, size_t floatCount, const float reference[3])
{
    Halo3NearestTriplet best{};
    if (!window || !reference)
        return best;
    for (int i = 0; i + 2 < static_cast<int>(floatCount); ++i)
    {
        const float dx = window[i] - reference[0];
        const float dy = window[i + 1] - reference[1];
        const float dz = window[i + 2] - reference[2];
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
            continue;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (best.index < 0 || d < best.distance)
        {
            best.index = i;
            best.distance = d;
        }
    }
    return best;
}

// C2 mode refinement: how the Present-tick publisher upgrades Gameplay when
// the sampler proves a seated state. 0 = leave Gameplay (not seated, unknown,
// or stale), 1 = Vehicle, 2 = Turret. Turret requires the measured physics
// type 5 (C1-confirmed live for mounted AND stationary turrets); an unproven
// type upgrades only to Vehicle so a missing refinement stays conservative.
inline constexpr int Halo3ClassifyGameplayUpgrade(
    Halo3VehicleState state, int vehicleType)
{
    if (state != Halo3VehicleState::Vehicle)
        return 0;
    return vehicleType == kHalo3VehicleTypeTurret ? 2 : 1;
}

// Focus-candidate scan for the C1 observer capture. The chase camera is
// modeled as position = focus - forward * focus_distance, where focus is the
// engine-evaluated per-seat camera pivot. Given the captured float window and
// the proven position/forward offsets, find the (triplet, scalar) pair that
// best satisfies |position + forward * d - triplet|. Pure and worker-only;
// the result names the focus_position and focus_distance offsets by
// measurement instead of assumption.
struct Halo3FocusCandidate
{
    int tripletIndex = -1;   // float index of the candidate focus triplet
    int distanceIndex = -1;  // float index of the candidate distance scalar
    float residual = 0.0f;
    float distance = 0.0f;
};

inline Halo3FocusCandidate Halo3FindFocusCandidate(
    const float* window, size_t count, size_t posIndex, size_t fwdIndex)
{
    Halo3FocusCandidate best{};
    if (!window || count < 3 || posIndex + 2 >= count || fwdIndex + 2 >= count)
        return best;
    const float* pos = window + posIndex;
    const float* fwd = window + fwdIndex;
    for (int i = 0; i + 2 < static_cast<int>(count); ++i)
    {
        // A focus triplet must be distinct from the position and forward
        // fields themselves (overlap would trivially "solve" with d == 0).
        if (i + 2 >= static_cast<int>(posIndex) &&
            i <= static_cast<int>(posIndex) + 2)
            continue;
        if (i + 2 >= static_cast<int>(fwdIndex) &&
            i <= static_cast<int>(fwdIndex) + 2)
            continue;
        const float* cand = window + i;
        if (!std::isfinite(cand[0]) || !std::isfinite(cand[1]) ||
            !std::isfinite(cand[2]))
            continue;
        for (int j = 0; j < static_cast<int>(count); ++j)
        {
            const float d = window[j];
            if (!std::isfinite(d) || d < 0.0f || d > 100.0f)
                continue;
            const float ex = pos[0] + fwd[0] * d - cand[0];
            const float ey = pos[1] + fwd[1] * d - cand[1];
            const float ez = pos[2] + fwd[2] * d - cand[2];
            const float residual = std::sqrt(ex * ex + ey * ey + ez * ez);
            if (best.tripletIndex < 0 || residual < best.residual)
            {
                best.tripletIndex = i;
                best.distanceIndex = j;
                best.residual = residual;
                best.distance = d;
            }
        }
    }
    return best;
}

// Entry/exit debounce. Halo 3 swings its chase camera for a moment when a seat
// is entered or exited; the first-person substitution and its one-shot
// recenter must trigger on a settled seat, not mid-swing. The raw per-frame
// freshness gate is separate and immediate: a stale snapshot always fails open
// to stock the same frame, regardless of what this debounce last reported.
inline constexpr uint32_t kHalo3VehicleDebounceFrames = 15;

struct Halo3VehicleDebounce
{
    Halo3VehicleState stable = Halo3VehicleState::Unknown;
    Halo3VehicleState candidate = Halo3VehicleState::Unknown;
    uint32_t count = 0;

    // Feed one sampled state per frame. Returns true exactly when `stable`
    // changes, i.e. the sampled state held for `threshold` consecutive frames;
    // that edge is the recenter trigger. A flapping sample restarts the count.
    constexpr bool Update(Halo3VehicleState sampled, uint32_t threshold)
    {
        if (sampled == stable)
        {
            candidate = stable;
            count = 0;
            return false;
        }
        if (sampled != candidate)
        {
            candidate = sampled;
            count = 1;
        }
        else
        {
            ++count;
        }
        if (count >= threshold)
        {
            stable = candidate;
            count = 0;
            return true;
        }
        return false;
    }
};
