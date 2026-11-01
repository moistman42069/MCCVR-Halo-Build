#pragma once

// THE VEHICLE AIM RING -- Halo Campaign Evolved's vehicle aim marker ("aimMarker"), the same picture and the same
// point.
//
// WHAT IT MARKS. Where the VEHICLE is aiming: the vehicle's own heading (the frame a camera that turns with the
// vehicle follows) at the pitch the game aims at, along the line from the eye the seat's aim is solved from (in MCC
// the seat's own engine eye; Campaign Evolved's game camera plays that part there). It sits beside the crosshair,
// which shows where the HAND points. Once the vehicle has turned to
// where the hand points the ring sits around the crosshair; while it is still turning, the gap is how far behind it
// is. In a camera that turns with the vehicle (a tethered mode) it is also exactly where the view comes to rest, so it
// shows what to aim at to keep going straight without turning.
//
// HOW DEEP. The ray from that eye is traced for what it meets, on the reticle's rule: a hit's range; on a
// miss the last range held (sky has no range, and inventing one would swing the ring every time the ray crosses a
// skyline); the trace's far end before anything has been hit in that seat. The ring is pulled back off the surface
// by the crosshair's own surface offset (at most half the range), then drawn on the line from the eye through that
// point, parked at the crosshair's far cap beyond it. A RANGE crosses from the simulation's tick to the frame, never
// a point.
//
// HOW IT LOOKS. A generated ring, no centre dot, light purple, drawn on the headset's compositor over the scene and
// under the crosshair. Its picture is 128 px square; the ring's radius is 0.40 of it, its half-thickness 0.008 of
// it, feathered over 1.25 px on both edges, colour (0.80, 0.55, 1.00) at 0.9 alpha -- Halo Campaign Evolved's
// defaults. It keeps Campaign Evolved's apparent size at whatever distance it is drawn: there the picture is as
// wide as that game's vehicle crosshair, 256 px at 0.24 scale per 500 cm, halved -- 0.06144 of its distance, about
// 3.52 degrees (a ring of about 2.8). It does not follow this mod's crosshair_size_deg.
//
// THE COLOUR'S ENCODING. Campaign Evolved draws the ring into a linear (UNORM) swapchain, so its numbers are linear
// light. This mod's swapchains are sRGB where the runtime offers one; the same light is then the sRGB encoding of
// those numbers (EncodeChannel). A linear swapchain takes them as they are.
//
// Zero engine dependencies; unit-tested in tests/vehicle_aim_marker_tests.cpp.

#include <cmath>
#include <cstdint>

namespace vehicle_aim_marker
{
inline constexpr int kTextureSize = 128;
inline constexpr float kRadius = 0.40f;          // ring radius, a fraction of the picture
inline constexpr float kHalfThickness = 0.008f;  // ring half-thickness, a fraction of the picture
inline constexpr float kDot = 0.0f;              // centre dot radius; 0 = none: the ring frames the crosshair
inline constexpr float kEdgePixels = 1.25f;      // feather on both edges of the band
inline constexpr float kRed = 0.80f, kGreen = 0.55f, kBlue = 1.00f;   // light purple, linear light
inline constexpr float kAlpha = 0.9f;
// The picture's width per metre of distance: Campaign Evolved's vehicle crosshair, 256 * 0.24 * 0.5 / 500.
inline constexpr float kWidthPerDistance = 256.0f * 0.24f * 0.5f / 500.0f;

// How much of pixel (x, y) of a `dim` square picture the ring covers, 0..1: 1 inside the band, feathering to 0 over
// kEdgePixels on both edges. The fractions are clamped as Campaign Evolved clamps them, so a bad value can neither
// erase the ring nor fill the picture. `dotFrac` <= 0 draws no centre dot at all.
inline float Coverage(int x, int y, int dim, float radiusFrac = kRadius, float thickFrac = kHalfThickness,
                      float dotFrac = kDot)
{
    if (dim <= 0)
        return 0.0f;
    const float c = static_cast<float>(dim) * 0.5f - 0.5f;
    float rf = radiusFrac;
    if (!(rf >= 0.02f))
        rf = 0.02f;
    if (rf > 0.48f)
        rf = 0.48f;
    float tf = thickFrac;
    if (!(tf >= 0.004f))
        tf = 0.004f;
    if (tf > 0.200f)
        tf = 0.200f;
    const bool noDot = !(dotFrac > 0.0f);
    float df = noDot ? 0.0f : dotFrac;
    if (df > 0.200f)
        df = 0.200f;
    const float radius = static_cast<float>(dim) * rf;
    const float half = static_cast<float>(dim) * tf;
    const float dot = static_cast<float>(dim) * df;
    const float dx = static_cast<float>(x) - c, dy = static_cast<float>(y) - c;
    const float d = std::sqrt(dx * dx + dy * dy);
    float ring = 1.0f - (std::fabs(d - radius) - half) / kEdgePixels;
    ring = ring > 1.0f ? 1.0f : (ring < 0.0f ? 0.0f : ring);
    float centre = noDot ? 0.0f : 1.0f - (d - dot) / kEdgePixels;
    centre = centre > 1.0f ? 1.0f : (centre < 0.0f ? 0.0f : centre);
    return ring > centre ? ring : centre;
}

// A linear-light value 0..1 as the 8-bit channel a swapchain of that kind stores: the sRGB encoding for an sRGB
// swapchain, the value itself for a linear one.
inline uint8_t EncodeChannel(float linear, bool srgb)
{
    float v = std::isfinite(linear) ? linear : 0.0f;
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    if (srgb)
        v = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

// Pixel (x, y) of the ring's picture as four bytes in memory order, packed little-endian into a uint32_t: R, G, B, A
// (B, G, R, A when `bgra`). STRAIGHT alpha -- the layer is submitted unpremultiplied -- so the colour is the ring's
// everywhere and only the alpha carries the shape: a filtered edge never darkens toward a black fringe.
inline uint32_t Pixel(int x, int y, int dim, bool bgra, bool srgb)
{
    const uint8_t r = EncodeChannel(kRed, srgb), g = EncodeChannel(kGreen, srgb), b = EncodeChannel(kBlue, srgb);
    const float cover = Coverage(x, y, dim) * kAlpha;
    const uint8_t a = static_cast<uint8_t>(cover * 255.0f + 0.5f);
    const uint8_t first = bgra ? b : r, third = bgra ? r : b;
    return static_cast<uint32_t>(first) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(third) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

// The way the vehicle aims, world (Halo's axes: z up): the HEADING of `vehicleForward` (its yaw only) at the PITCH of
// `cameraForward` (the game's camera, which looks along the aim the game holds). False when the vehicle points (nearly)
// straight up or down, where its heading is not a direction, or on non-finite input.
inline bool Direction(const float vehicleForward[3], const float cameraForward[3], float out[3])
{
    if (!vehicleForward || !cameraForward || !out)
        return false;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(vehicleForward[i]) || !std::isfinite(cameraForward[i]))
            return false;
    const float level = std::sqrt(vehicleForward[0] * vehicleForward[0] + vehicleForward[1] * vehicleForward[1]);
    const float length = std::sqrt(level * level + vehicleForward[2] * vehicleForward[2]);
    if (!(length > 1.0e-6f) || level < 0.05f * length)
        return false;
    const float cameraLength = std::sqrt(cameraForward[0] * cameraForward[0] + cameraForward[1] * cameraForward[1] +
                                         cameraForward[2] * cameraForward[2]);
    if (!(cameraLength > 1.0e-6f))
        return false;
    float up = cameraForward[2] / cameraLength;
    up = up < -1.0f ? -1.0f : (up > 1.0f ? 1.0f : up);
    const float across = std::sqrt(1.0f - up * up);
    out[0] = vehicleForward[0] / level * across;
    out[1] = vehicleForward[1] / level * across;
    out[2] = up;
    return true;
}

// THE RANGE ALONG THE RAY, kept for one seat: a hit's range; the last one held through a miss or a trace that is not
// fresh; `farWu` (the trace's far end) before anything has been hit. Reset() on getting into a seat.
class HeldRange
{
public:
    void Reset() noexcept { rangeWu_ = 0.0f; }
    float Use(bool traced, bool hit, float rangeWu, float farWu) noexcept
    {
        if (traced && hit && std::isfinite(rangeWu) && rangeWu > 0.0f)
            rangeWu_ = rangeWu;
        if (rangeWu_ > 0.0f)
            return rangeWu_;
        return std::isfinite(farWu) && farWu > 0.0f ? farWu : 0.0f;
    }
    bool Holding() const noexcept { return rangeWu_ > 0.0f; }

private:
    float rangeWu_ = 0.0f;
};

// How far along the ray the ring is drawn: `rangeWu` pulled back toward the camera by `offsetWu`, never by more than
// half the range, so a near surface still has the ring in front of it.
inline float PullBack(float rangeWu, float offsetWu)
{
    if (!std::isfinite(rangeWu) || rangeWu <= 0.0f)
        return 0.0f;
    float off = std::isfinite(offsetWu) && offsetWu > 0.0f ? offsetWu : 0.0f;
    if (off > rangeWu * 0.5f)
        off = rangeWu * 0.5f;
    return rangeWu - off;
}

// The distance the ring is DRAWN at, metres: its own, parked at the crosshair's cap (`capM`, reticle_max_dist_m)
// beyond it, as the crosshair parks (aim_solve::traced_reticle_placement). On a ray from the eye every depth lands on
// the same point of the picture, so parking it moves nothing but its depth in the two eyes, which then matches the
// crosshair's when the two coincide. A cap that is not a distance parks nothing.
inline float DrawDistance(float distanceM, float capM)
{
    if (!std::isfinite(distanceM) || distanceM <= 0.0f)
        return 0.0f;
    return std::isfinite(capM) && capM > 0.0f && distanceM > capM ? capM : distanceM;
}

// The picture's width at `distanceM`, metres: Campaign Evolved's (kWidthPerDistance). 0 on bad input.
inline float WidthAt(float distanceM)
{
    if (!std::isfinite(distanceM) || distanceM <= 0.0f)
        return 0.0f;
    return distanceM * kWidthPerDistance;
}
} // namespace vehicle_aim_marker
