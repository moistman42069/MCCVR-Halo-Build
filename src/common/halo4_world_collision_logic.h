#pragma once

#include <cmath>
#include <cstdint>

// Pure, engine-independent policy for the first Halo 4 world-contact
// experiment. The runtime query is a point sweep from the last accepted wrist
// position to the latest tracked target. This helper validates and resolves a
// reported hit while leaving the engine ABI and threading outside test code.
struct Halo4WorldCollisionResolution
{
    bool valid = false;
    bool contact = false;
    float accepted[3]{};
    float correction[3]{};
};

// One root plus fixed min/max semantic slots selected from an authored hand.
// The right-hand publication combines that set with the exact Halo 4 held
// model's H4EK-authored bounds; no title-independent dimensions are invented.
inline constexpr int kHalo4WorldCollisionExtremaCount = 7;
inline constexpr int kHalo4WeaponCollisionBoundsSampleCount = 14;
inline constexpr int kHalo4WorldCollisionMaxSamples =
    kHalo4WorldCollisionExtremaCount +
    kHalo4WeaponCollisionBoundsSampleCount;

struct Halo4WeaponCollisionBounds
{
    uint32_t runtimeImportChecksum;
    float minimum[3];
    float maximum[3];
};

// H4EK 1.890 ManagedBlam render_model compression-position bounds for every
// existing first-person render model referenced by the 77 official .weapon
// tags. The runtime import checksum is already proven by the live render-model
// descriptor and avoids guessing a weapon type or copying another engine's
// geometry. Unknown checksums deliberately receive hand-only collision.
inline constexpr Halo4WeaponCollisionBounds kHalo4WeaponCollisionBounds[]{
    {0x10030014u, {-0.0988832f, -0.0121f, -0.0124852005f}, {0.264650017f, 0.0121f, 0.0915045f}},
    {0x10050216u, {-0.0254121833f, -0.0201971028f, -0.0532677844f}, {0.191055641f, 0.0201971028f, 0.0732690245f}},
    {0x11070A16u, {-0.0385448635f, -0.0156979524f, -0.08223673f}, {0.383093178f, 0.0156979524f, 0.08223673f}},
    {0x11080014u, {-0.269691825f, -0.0405867323f, -0.03488739f}, {0.191290349f, 0.0445399471f, 0.110577367f}},
    {0x1108171Fu, {-0.09948748f, -0.0175814945f, -0.0241994746f}, {0.279507935f, 0.0175814927f, 0.08677069f}},
    {0x110F0115u, {-0.07586707f, -0.042297408f, -0.058178328f}, {0.50159806f, 0.042297408f, 0.0932276845f}},
    {0x11120517u, {-0.11482f, -0.00805f, -0.0119863f}, {0.21078f, 0.0121363f, 0.08608f}},
    {0x111B0416u, {-0.1255f, -0.01282f, -0.0215f}, {0.265672f, 0.0114198f, 0.0834739953f}},
    {0x111B0A00u, {-0.0299846269f, -0.014535673f, -0.0373922028f}, {0.151489168f, 0.0191084426f, 0.0481085852f}},
    {0x111D0D05u, {-0.2325192f, -0.0328173f, -0.0191207f}, {0.2099376f, 0.0328173f, 0.113721296f}},
    {0x111E0F1Cu, {-0.0444768369f, -0.06804858f, -0.0456474945f}, {0.18849726f, 0.06804858f, 0.101898991f}},
    {0x12140501u, {-0.0192445f, -0.008581693f, -0.0412483029f}, {0.101777934f, 0.0132886069f, 0.0568027f}},
    {0x13020014u, {-0.0644032f, -0.0374721f, -0.027175f}, {0.31773f, 0.0374721f, 0.07558f}},
    {0x1305160Bu, {-0.0169282053f, -0.0154789491f, -0.03747928f}, {0.1605304f, 0.01547104f, 0.06674192f}},
    {0x13081E16u, {-0.0192445274f, -0.008820772f, -0.0412482731f}, {0.101929232f, 0.0132387634f, 0.0568027f}},
    {0x130C1C16u, {-0.05358559f, -0.0162019487f, -0.08452677f}, {0.3623625f, 0.016281724f, 0.08385826f}},
    {0x1314081Cu, {-0.123702705f, -0.0361403f, -0.0576259f}, {0.1159724f, 0.0349579975f, 0.116330191f}},
    {0x1402121Bu, {-0.0642338f, -0.0205f, -0.02293087f}, {0.109456085f, 0.0205f, 0.0596091338f}},
    {0x1402141Cu, {-0.118816696f, -0.0206853021f, -0.0218272135f}, {0.506342053f, 0.0206853021f, 0.10795977f}},
    {0x14031004u, {-0.0856358856f, -0.0165035f, -0.0143127395f}, {0.3268325f, 0.0165035f, 0.09084353f}},
    {0x14040E08u, {-0.009711542f, -0.043816302f, -0.0363407657f}, {0.121007167f, 0.0468524136f, 0.05106438f}},
    {0x14191210u, {-0.00257470272f, -1.1433977e-18f, -0.00257470272f}, {0.007425297f, 0.01f, 0.007425297f}},
    {0x15091B16u, {-0.035497f, -0.0396850221f, -0.0174026974f}, {0.06963639f, 0.02335031f, 0.0696735f}},
    {0x15111A1Du, {-0.0383199975f, -0.00892473f, -0.0293590985f}, {0.075715f, 0.00892473f, 0.0558000021f}},
    {0x151C1A1Bu, {-0.105280541f, -0.02495401f, -0.0624283329f}, {0.32850948f, 0.02495401f, 0.0848532f}},
    {0x160E0512u, {-0.08563654f, -0.0162987374f, -0.0143127395f}, {0.326109827f, 0.0162987243f, 0.0809431449f}},
    {0x160F1B01u, {-0.0219149813f, -0.0116482805f, -0.0316562019f}, {0.1461231f, 0.0161748f, 0.07126853f}},
    {0x1615080Du, {-0.30007568f, -0.04981772f, -0.534983456f}, {0.07952363f, 0.0241183564f, 0.522285461f}},
    {0x16170807u, {-0.0549394861f, -0.03419309f, -0.02003104f}, {0.044337146f, 0.0254806373f, 0.1369933f}},
    {0x17141708u, {-0.228255391f, -0.0294936f, -0.046647f}, {0.2007807f, 0.0311604f, 0.132953942f}},
    {0x17161F1Cu, {-0.09792f, -0.0235393029f, -0.0270641353f}, {0.29444f, 0.0269603133f, 0.07439147f}},
    {0x17180016u, {-0.0487442f, -0.0245576f, -0.0388763137f}, {0.2602094f, 0.0245576f, 0.07937829f}},
    {0x171C0B05u, {-0.102605f, -0.0133150993f, -0.0118296f}, {0.267829984f, 0.0235137f, 0.06835f}},
    {0x1814181Cu, {-0.0935352f, -0.0122993f, -0.0122084022f}, {0.23352f, 0.0129804034f, 0.0776163f}},
    {0x1A0B0214u, {-0.254746884f, -0.0248101987f, -0.0172689f}, {0.136763f, 0.039303802f, 0.1114975f}},
    {0x1A0D0314u, {-0.0849358f, -0.0370414f, -0.0191539f}, {0.444099f, 0.0351f, 0.0956756f}},
    {0x1A110E12u, {-0.04397051f, -0.0388348028f, -0.0413072668f}, {0.0383695f, 0.0393228f, 0.0448977f}},
    {0x1A140816u, {-0.0938183f, -0.0541521f, -0.287851f}, {0.108661994f, 0.0541104f, 0.3805714f}},
    {0x1B080715u, {-0.1080806f, -0.0286612958f, -0.0484079979f}, {0.229776189f, 0.0303805266f, 0.0748976f}},
};

inline constexpr int kHalo4WeaponCollisionBoundsCount =
    static_cast<int>(sizeof(kHalo4WeaponCollisionBounds) /
                     sizeof(kHalo4WeaponCollisionBounds[0]));
static_assert(kHalo4WeaponCollisionBoundsCount == 39);

inline const Halo4WeaponCollisionBounds* Halo4FindWeaponCollisionBounds(
    uint32_t runtimeImportChecksum) noexcept
{
    for (const Halo4WeaponCollisionBounds& bounds :
         kHalo4WeaponCollisionBounds)
        if (bounds.runtimeImportChecksum == runtimeImportChecksum)
            return &bounds;
    return nullptr;
}

inline bool Halo4WorldCollisionFiniteVector(const float value[3]) noexcept
{
    return value && std::isfinite(value[0]) && std::isfinite(value[1]) &&
        std::isfinite(value[2]);
}

inline float Halo4WorldCollisionDistanceSquared(
    const float first[3], const float second[3]) noexcept
{
    if (!Halo4WorldCollisionFiniteVector(first) ||
        !Halo4WorldCollisionFiniteVector(second))
        return -1.0f;
    const float x = second[0] - first[0];
    const float y = second[1] - first[1];
    const float z = second[2] - first[2];
    const float value = x * x + y * y + z * z;
    return std::isfinite(value) ? value : -1.0f;
}

inline int Halo4SelectWorldCollisionExtrema(
    const float root[3], const float* authoredPoints, int authoredPointCount,
    float output[][3], int outputCapacity) noexcept
{
    if (!Halo4WorldCollisionFiniteVector(root) || !authoredPoints ||
        authoredPointCount <= 0 || !output ||
        outputCapacity < kHalo4WorldCollisionExtremaCount)
        return 0;

    for (int axis = 0; axis < 3; ++axis)
        output[0][axis] = root[axis];
    int extrema[6]{-1, -1, -1, -1, -1, -1};
    float values[6]{INFINITY, -INFINITY, INFINITY, -INFINITY,
                    INFINITY, -INFINITY};
    for (int point = 0; point < authoredPointCount; ++point)
    {
        const float* candidate = authoredPoints + point * 3;
        if (!Halo4WorldCollisionFiniteVector(candidate)) continue;
        for (int axis = 0; axis < 3; ++axis)
        {
            const int minimum = axis * 2;
            const int maximum = minimum + 1;
            if (candidate[axis] < values[minimum])
            {
                values[minimum] = candidate[axis];
                extrema[minimum] = point;
            }
            if (candidate[axis] > values[maximum])
            {
                values[maximum] = candidate[axis];
                extrema[maximum] = point;
            }
        }
    }

    for (int slot = 0; slot < 6; ++slot)
    {
        const int extremum = extrema[slot];
        if (extremum < 0) return 0;
        const float* candidate = authoredPoints + extremum * 3;
        for (int axis = 0; axis < 3; ++axis)
            output[slot + 1][axis] = candidate[axis];
    }
    // Fixed semantic slots (root, min/max X/Y/Z) preserve sweep identity even
    // when several extrema happen to name the same authored point.
    return kHalo4WorldCollisionExtremaCount;
}

inline int Halo4BuildWeaponCollisionBoundsSamples(
    const Halo4WeaponCollisionBounds& bounds, float scale,
    const float rotation[9], const float translation[3],
    const float collisionRoot[3], const float wrist[3],
    float output[][3], int outputCapacity) noexcept
{
    if (!rotation || !Halo4WorldCollisionFiniteVector(translation) ||
        !Halo4WorldCollisionFiniteVector(collisionRoot) ||
        !Halo4WorldCollisionFiniteVector(wrist) || !output ||
        outputCapacity < kHalo4WeaponCollisionBoundsSampleCount ||
        !std::isfinite(scale) || std::fabs(scale) < 0.001f)
        return 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(bounds.minimum[axis]) ||
            !std::isfinite(bounds.maximum[axis]) ||
            bounds.minimum[axis] > bounds.maximum[axis])
            return 0;
    }
    for (int element = 0; element < 9; ++element)
        if (!std::isfinite(rotation[element])) return 0;

    float local[kHalo4WeaponCollisionBoundsSampleCount][3]{};
    int sample = 0;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
            {
                local[sample][0] = x ? bounds.maximum[0] : bounds.minimum[0];
                local[sample][1] = y ? bounds.maximum[1] : bounds.minimum[1];
                local[sample][2] = z ? bounds.maximum[2] : bounds.minimum[2];
                ++sample;
            }
    const float centre[3]{
        (bounds.minimum[0] + bounds.maximum[0]) * 0.5f,
        (bounds.minimum[1] + bounds.maximum[1]) * 0.5f,
        (bounds.minimum[2] + bounds.maximum[2]) * 0.5f};
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side)
        {
            for (int component = 0; component < 3; ++component)
                local[sample][component] = centre[component];
            local[sample][axis] = side ? bounds.maximum[axis]
                                       : bounds.minimum[axis];
            ++sample;
        }

    for (sample = 0; sample < kHalo4WeaponCollisionBoundsSampleCount;
         ++sample)
    {
        for (int row = 0; row < 3; ++row)
        {
            float rotated = 0.0f;
            for (int column = 0; column < 3; ++column)
                rotated += rotation[column * 3 + row] *
                    local[sample][column];
            output[sample][row] = collisionRoot[row] + translation[row] -
                wrist[row] + scale * rotated;
        }
        if (!Halo4WorldCollisionFiniteVector(output[sample])) return 0;
    }
    return kHalo4WeaponCollisionBoundsSampleCount;
}

inline bool Halo4BuildWorldCollisionPushVelocity(
    const float previous[3], const float desired[3], uint64_t elapsedMs,
    float worldScale, float output[3], float strength = 0.65f,
    float maximumMetresPerSecond = 2.0f) noexcept
{
    if (!Halo4WorldCollisionFiniteVector(previous) ||
        !Halo4WorldCollisionFiniteVector(desired) || !output ||
        elapsedMs == 0 || elapsedMs > 250 || !std::isfinite(worldScale) ||
        worldScale <= 0.0f || !std::isfinite(strength) || strength <= 0.0f ||
        !std::isfinite(maximumMetresPerSecond) ||
        maximumMetresPerSecond <= 0.0f)
        return false;
    const float inverseSeconds = 1000.0f / static_cast<float>(elapsedMs);
    float lengthSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        output[axis] = (desired[axis] - previous[axis]) *
            inverseSeconds * strength;
        lengthSquared += output[axis] * output[axis];
    }
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f)
        return false;
    const float maximum = maximumMetresPerSecond * worldScale;
    if (lengthSquared > maximum * maximum)
    {
        const float scale = maximum / std::sqrt(lengthSquared);
        for (int axis = 0; axis < 3; ++axis)
            output[axis] *= scale;
    }
    return Halo4WorldCollisionFiniteVector(output);
}

inline float Halo4PhysicalMeleeSwingSpeedMetresPerSecond(
    const float previousDesired[3], const float desired[3],
    uint64_t elapsedMs, float worldScale) noexcept
{
    if (!Halo4WorldCollisionFiniteVector(previousDesired) ||
        !Halo4WorldCollisionFiniteVector(desired) || elapsedMs == 0 ||
        elapsedMs > 250 || !std::isfinite(worldScale) || worldScale <= 0.0f)
        return -1.0f;
    const float distanceSquared = Halo4WorldCollisionDistanceSquared(
        previousDesired, desired);
    if (distanceSquared < 0.0f) return -1.0f;
    const float speed = std::sqrt(distanceSquared) * 1000.0f /
        (static_cast<float>(elapsedMs) * worldScale);
    return std::isfinite(speed) ? speed : -1.0f;
}

inline bool Halo4PhysicalMeleeContactQualifies(
    int32_t objectIndex, int32_t ignoredObjectIndex,
    float swingSpeedMetresPerSecond, float requiredSpeedMetresPerSecond) noexcept
{
    return objectIndex != -1 && objectIndex != ignoredObjectIndex &&
        std::isfinite(swingSpeedMetresPerSecond) &&
        std::isfinite(requiredSpeedMetresPerSecond) &&
        requiredSpeedMetresPerSecond >= 0.3f &&
        requiredSpeedMetresPerSecond <= 3.0f &&
        swingSpeedMetresPerSecond >= requiredSpeedMetresPerSecond;
}

inline float Halo4PhysicalMeleeVelocityMagnitude(
    const float velocityMetresPerSecond[3]) noexcept
{
    if (!Halo4WorldCollisionFiniteVector(velocityMetresPerSecond))
        return -1.0f;
    const float squared = velocityMetresPerSecond[0] *
            velocityMetresPerSecond[0] +
        velocityMetresPerSecond[1] * velocityMetresPerSecond[1] +
        velocityMetresPerSecond[2] * velocityMetresPerSecond[2];
    if (!std::isfinite(squared) || squared < 0.0f) return -1.0f;
    const float speed = std::sqrt(squared);
    return std::isfinite(speed) ? speed : -1.0f;
}

// Some SteamVR WMR drivers advertise XR_SPACE_VELOCITY_LINEAR_VALID_BIT but
// return a permanent zero vector while the tracked pose is visibly moving.
// Derive a bounded fallback from two predicted-display-time pose samples. The
// caller still prefers a meaningful native velocity, preserving runtimes whose
// OpenXR velocity path is correct.
inline bool Halo4DeriveControllerPoseVelocity(
    const float previousPosition[3], const float currentPosition[3],
    int64_t elapsedNanoseconds, float outputMetresPerSecond[3],
    float maximumMetresPerSecond = 20.0f) noexcept
{
    if (!previousPosition || !currentPosition || !outputMetresPerSecond ||
        !Halo4WorldCollisionFiniteVector(previousPosition) ||
        !Halo4WorldCollisionFiniteVector(currentPosition) ||
        elapsedNanoseconds < 1000000ll || elapsedNanoseconds > 100000000ll ||
        !std::isfinite(maximumMetresPerSecond) ||
        maximumMetresPerSecond <= 0.0f)
        return false;
    const float inverseSeconds =
        1000000000.0f / static_cast<float>(elapsedNanoseconds);
    float squared = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        outputMetresPerSecond[axis] =
            (currentPosition[axis] - previousPosition[axis]) *
            inverseSeconds;
        squared += outputMetresPerSecond[axis] *
            outputMetresPerSecond[axis];
    }
    return std::isfinite(squared) &&
        squared <= maximumMetresPerSecond * maximumMetresPerSecond &&
        Halo4WorldCollisionFiniteVector(outputMetresPerSecond);
}

inline bool Halo4SelectControllerLinearVelocity(
    bool nativeValid, const float nativeVelocity[3],
    bool poseDerivedValid, const float poseDerivedVelocity[3],
    float outputMetresPerSecond[3], bool& usedPoseDerived,
    float nativeMotionFloorMetresPerSecond = 0.01f) noexcept
{
    usedPoseDerived = false;
    if (!outputMetresPerSecond ||
        !std::isfinite(nativeMotionFloorMetresPerSecond) ||
        nativeMotionFloorMetresPerSecond < 0.0f)
        return false;
    const float nativeSpeed = nativeValid && nativeVelocity
        ? Halo4PhysicalMeleeVelocityMagnitude(nativeVelocity) : -1.0f;
    const float derivedSpeed = poseDerivedValid && poseDerivedVelocity
        ? Halo4PhysicalMeleeVelocityMagnitude(poseDerivedVelocity) : -1.0f;
    if (nativeSpeed >= nativeMotionFloorMetresPerSecond)
    {
        for (int axis = 0; axis < 3; ++axis)
            outputMetresPerSecond[axis] = nativeVelocity[axis];
        return true;
    }
    if (derivedSpeed >= 0.0f)
    {
        for (int axis = 0; axis < 3; ++axis)
            outputMetresPerSecond[axis] = poseDerivedVelocity[axis];
        usedPoseDerived = true;
        return true;
    }
    if (nativeSpeed >= 0.0f)
    {
        for (int axis = 0; axis < 3; ++axis)
            outputMetresPerSecond[axis] = nativeVelocity[axis];
        return true;
    }
    return false;
}

struct Halo4PhysicalMeleeVelocityDecision
{
    bool trigger = false;
    bool latched = false;
};

inline Halo4PhysicalMeleeVelocityDecision
Halo4UpdatePhysicalMeleeVelocityLatch(
    bool velocityValid, float speedMetresPerSecond,
    float requiredSpeedMetresPerSecond, bool wasLatched,
    float releaseRatio = 0.55f) noexcept
{
    Halo4PhysicalMeleeVelocityDecision result{};
    if (!velocityValid || !std::isfinite(speedMetresPerSecond) ||
        speedMetresPerSecond < 0.0f ||
        !std::isfinite(requiredSpeedMetresPerSecond) ||
        requiredSpeedMetresPerSecond < 0.3f ||
        requiredSpeedMetresPerSecond > 10.0f ||
        !std::isfinite(releaseRatio) || releaseRatio <= 0.0f ||
        releaseRatio >= 1.0f)
        return result;
    result.latched = wasLatched;
    if (speedMetresPerSecond <= requiredSpeedMetresPerSecond * releaseRatio)
        result.latched = false;
    if (!result.latched &&
        speedMetresPerSecond >= requiredSpeedMetresPerSecond)
    {
        result.trigger = true;
        result.latched = true;
    }
    return result;
}

inline bool Halo4WorldCollisionMovementIsTeleport(
    const float accepted[3], const float desired[3], float worldScale,
    float maximumMetres = 1.5f) noexcept
{
    if (!std::isfinite(worldScale) || worldScale <= 0.0f ||
        !std::isfinite(maximumMetres) || maximumMetres <= 0.0f)
        return true;
    const float distanceSquared =
        Halo4WorldCollisionDistanceSquared(accepted, desired);
    const float maximum = maximumMetres * worldScale;
    return distanceSquared < 0.0f || distanceSquared > maximum * maximum;
}

inline Halo4WorldCollisionResolution Halo4ResolveWorldCollision(
    const float start[3], const float desired[3], bool hit,
    float hitFraction, float worldScale, float skinMetres = 0.015f) noexcept
{
    Halo4WorldCollisionResolution result{};
    if (!Halo4WorldCollisionFiniteVector(start) ||
        !Halo4WorldCollisionFiniteVector(desired) ||
        !std::isfinite(worldScale) || worldScale <= 0.0f ||
        !std::isfinite(skinMetres) || skinMetres < 0.0f)
        return result;

    const float distanceSquared =
        Halo4WorldCollisionDistanceSquared(start, desired);
    if (distanceSquared < 0.0f)
        return result;

    result.valid = true;
    if (!hit)
    {
        for (int axis = 0; axis < 3; ++axis)
            result.accepted[axis] = desired[axis];
        return result;
    }
    if (!std::isfinite(hitFraction) || hitFraction < 0.0f ||
        hitFraction > 1.0f)
    {
        result.valid = false;
        return result;
    }

    const float distance = std::sqrt(distanceSquared);
    float resolvedFraction = hitFraction;
    if (distance > 1.0e-6f)
    {
        const float skinFraction = skinMetres * worldScale / distance;
        resolvedFraction = std::fmax(0.0f, hitFraction - skinFraction);
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        result.accepted[axis] = start[axis] +
            (desired[axis] - start[axis]) * resolvedFraction;
        result.correction[axis] = result.accepted[axis] - desired[axis];
        if (!std::isfinite(result.accepted[axis]) ||
            !std::isfinite(result.correction[axis]))
        {
            result = Halo4WorldCollisionResolution{};
            return result;
        }
    }
    result.contact = true;
    return result;
}
