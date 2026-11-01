// The dev assessment reticle's geometry (src/common/assess_reticle_logic.h): the mark is the point the game's
// shot reaches -- from the game's own shot origin along its aim, to the first surface or a fallback -- drawn
// on the ray from the rendered eye through that point.
#include "assess_reticle_logic.h"
#include <cmath>
#include <cstdio>

namespace
{
unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool Near(float a, float b, float tolerance = 1.0e-4f) { return std::fabs(a - b) <= tolerance; }
constexpr float kTraceLength = 328.0f;   // 1 km in Blam world units
} // namespace

int main()
{
    using namespace assess_reticle;

    // ---- the range ----
    {
        const RangeChoice r = ChooseRange(true, true, 2.5f, true, true, 9.0f, 13.0f, kTraceLength);
        Check(r.source == RangeSource::GameAimTrace && r.surface && Near(r.rangeWu, 2.5f),
              "a fresh hit along the game's aim wins over everything");
    }
    {
        const RangeChoice r = ChooseRange(true, false, 0.0f, true, true, 9.0f, 13.0f, kTraceLength);
        Check(r.source == RangeSource::GameAimTrace && !r.surface && Near(r.rangeWu, kTraceLength),
              "a fresh miss along the game's aim parks at the trace's far end, not on the intent point");
    }
    {
        const RangeChoice r = ChooseRange(true, true, std::nanf(""), true, true, 9.0f, 13.0f, kTraceLength);
        Check(r.source == RangeSource::GameAimTrace && !r.surface && Near(r.rangeWu, kTraceLength),
              "a non-finite hit range counts as a miss");
    }
    {
        const RangeChoice r = ChooseRange(false, false, 0.0f, true, true, 9.0f, 13.0f, kTraceLength);
        Check(r.source == RangeSource::IntentTrace && r.surface && Near(r.rangeWu, 9.0f),
              "no game-aim trace: the intent ray's traced point, at its distance from the shot origin");
    }
    {
        const RangeChoice r = ChooseRange(false, false, 0.0f, true, false, kTraceLength, 13.0f, kTraceLength);
        Check(r.source == RangeSource::IntentTrace && !r.surface,
              "an intent-ray miss is not a surface");
    }
    {
        const RangeChoice r = ChooseRange(false, false, 0.0f, true, true, -1.0f, 13.0f, kTraceLength);
        Check(r.source == RangeSource::CrosshairDistance && !r.surface && Near(r.rangeWu, 13.0f),
              "a bad intent distance falls through to the crosshair distance");
    }
    {
        const RangeChoice r = ChooseRange(false, false, 0.0f, false, false, 0.0f, 13.0f, kTraceLength);
        Check(r.source == RangeSource::CrosshairDistance && !r.surface && Near(r.rangeWu, 13.0f),
              "nothing traced: the crosshair distance");
    }

    // ---- a point along a ray ----
    {
        const float origin[3]{1.0f, 2.0f, 3.0f};
        const float dir[3]{0.0f, 0.0f, 2.0f};   // not unit: normalized
        float p[3]{};
        Check(PointAlongRay(origin, dir, 4.0f, p) && Near(p[0], 1.0f) && Near(p[1], 2.0f) && Near(p[2], 7.0f),
              "origin + normalize(dir) * range");
        float untouched[3]{9.0f, 9.0f, 9.0f};
        const float zero[3]{};
        Check(!PointAlongRay(origin, zero, 4.0f, untouched) && untouched[0] == 9.0f,
              "a degenerate direction is refused and leaves out untouched");
        Check(!PointAlongRay(origin, dir, 0.0f, untouched) && !PointAlongRay(origin, dir, -1.0f, untouched) &&
                  !PointAlongRay(origin, dir, std::nanf(""), untouched),
              "a non-positive or non-finite range is refused");
    }
    {
        // The intent ray from the head H, traced 10 along +X, seen from an eye 1 below the head.
        const float head[3]{0.0f, 0.0f, 1.0f}, eye[3]{0.0f, 0.0f, 0.0f}, dir[3]{1.0f, 0.0f, 0.0f};
        Check(Near(DistanceToRayPoint(eye, head, dir, 10.0f), std::sqrt(101.0f)),
              "the intent point's distance from the shot origin");
        Check(DistanceToRayPoint(eye, head, dir, 0.0f) < 0.0f, "no range, no distance");
    }

    // ---- the drawn ray ----
    {
        // THE REPORTED CASE, on foot: the shot origin E sits 0.1 below the rendered eye H, the game aims level
        // along +X, the wall is 10 away along that aim. The mark must be the wall point (10, 0, -0.1) -- seen
        // from H slightly DOWN, and at that point's own distance -- not a ray from the hand.
        const float head[3]{0.0f, 0.0f, 0.0f}, shotOrigin[3]{0.0f, 0.0f, -0.1f}, aim[3]{1.0f, 0.0f, 0.0f};
        const RangeChoice range = ChooseRange(true, true, 10.0f, false, false, 0.0f, 13.0f, kTraceLength);
        Placement placed{};
        Check(Place(head, shotOrigin, aim, range, 2.0f, placed), "the placement resolves");
        Check(Near(placed.point[0], 10.0f) && Near(placed.point[1], 0.0f) && Near(placed.point[2], -0.1f),
              "the mark is the point the shot reaches, from the shot origin along the game's aim");
        const float length = std::sqrt(100.0f + 0.01f);
        Check(Near(placed.dir[0], 10.0f / length) && Near(placed.dir[2], -0.1f / length),
              "drawn along the eye-anchored ray through that point");
        Check(Near(placed.rangeVrM, length / 2.0f), "at that point's distance, in VR metres");
        Check(placed.surface && placed.source == RangeSource::GameAimTrace, "a hit is a surface");
    }
    {
        // A converged aim through the intent point T: the game-aim ray from E reaches exactly T, so the mark
        // and the traced reticle coincide whichever source gave the range.
        const float head[3]{0.0f, 0.0f, 0.0f}, eye[3]{0.0f, -0.2f, -0.3f};
        const float intentDir[3]{0.6f, 0.8f, 0.0f};
        const float intentRange = 7.0f;
        const float t[3]{intentDir[0] * intentRange, intentDir[1] * intentRange, 0.0f};
        const float aim[3]{t[0] - eye[0], t[1] - eye[1], t[2] - eye[2]};
        const float distance = DistanceToRayPoint(eye, head, intentDir, intentRange);
        const RangeChoice range = ChooseRange(false, false, 0.0f, true, true, distance, 13.0f, kTraceLength);
        Placement placed{};
        Check(Place(head, eye, aim, range, 1.0f, placed), "the converged placement resolves");
        Check(Near(placed.dir[0], 0.6f) && Near(placed.dir[1], 0.8f) && Near(placed.dir[2], 0.0f) &&
                  Near(placed.rangeVrM, intentRange, 1.0e-3f),
              "converged: the mark sits on the intent ray at the traced range");
    }
    {
        const float head[3]{0.0f, 0.0f, 0.0f}, aim[3]{1.0f, 0.0f, 0.0f};
        const RangeChoice range = ChooseRange(false, false, 0.0f, false, false, 0.0f, 13.0f, kTraceLength);
        Placement placed{};
        Check(Place(head, head, aim, range, 1.0f, placed) && Near(placed.rangeVrM, 13.0f) && !placed.surface &&
                  placed.source == RangeSource::CrosshairDistance,
              "the crosshair-distance fallback still places from the shot origin, parked (not a surface)");
        Placement untouched{};
        untouched.rangeVrM = 5.0f;
        Check(!Place(head, head, aim, range, 0.0f, untouched) && untouched.rangeVrM == 5.0f,
              "no world scale, no placement");
        const RangeChoice none = ChooseRange(false, false, 0.0f, false, false, 0.0f, 0.0f, kTraceLength);
        Check(!Place(head, head, aim, none, 1.0f, untouched), "no range at all, no placement");
        const float back[3]{-1.0f, 0.0f, 0.0f};
        const float ahead[3]{1.0f, 0.0f, 0.0f};
        const RangeChoice one = ChooseRange(true, true, 1.0f, false, false, 0.0f, 13.0f, kTraceLength);
        Check(!Place(head, back, ahead, one, 1.0f, untouched), "a point at the eye is refused");
    }

    // ---- which game aim the mark is built from ----
    {
        Check(ChooseGameAimSource(false, false) == GameAimSource::AimFeedback &&
                  ChooseGameAimSource(false, true) == GameAimSource::AimFeedback,
              "every title but Reach on foot keeps its own aim feedback");
        Check(ChooseGameAimSource(true, true) == GameAimSource::ReachUnitAim,
              "Reach on foot draws the projectile's forward, unit +0x214");
        Check(ChooseGameAimSource(true, false) == GameAimSource::None,
              "Reach on foot with no fresh unit aim draws nothing -- never the kicking camera");
        const float aim[3]{0.0f, 1.0f, 0.0f};
        Check(UnitAimUsable(7, 7, 1000, 1050, 100, aim), "a fresh unit aim of this level is usable");
        Check(!UnitAimUsable(6, 7, 1000, 1050, 100, aim), "another level's unit aim is not");
        Check(!UnitAimUsable(7, 0, 1000, 1050, 100, aim), "no level, no unit aim");
        Check(!UnitAimUsable(7, 7, 1000, 1101, 100, aim), "a stale unit aim is not");
        Check(!UnitAimUsable(7, 7, 1000, 999, 100, aim), "a unit aim from the future is not");
        Check(!UnitAimUsable(7, 7, 0, 50, 100, aim), "an unstamped unit aim is not");
        const float zero[3]{0.0f, 0.0f, 0.0f}, bad[3]{std::nanf(""), 0.0f, 1.0f};
        Check(!UnitAimUsable(7, 7, 1000, 1050, 100, zero) && !UnitAimUsable(7, 7, 1000, 1050, 100, bad) &&
                  !UnitAimUsable(7, 7, 1000, 1050, 100, nullptr),
              "a degenerate or non-finite unit aim is not");
    }

    // ---- the marker's art: a small magenta ring and dot, unmistakably not the reticle ----
    {
        const uint32_t n = kMarkerTextureSize;
        const auto alpha = [](uint32_t p) { return p >> 24; };
        const auto low = [](uint32_t p) { return p & 0xFFu; };
        const auto mid = [](uint32_t p) { return (p >> 8) & 0xFFu; };
        const auto high = [](uint32_t p) { return (p >> 16) & 0xFFu; };
        const uint32_t centre = MarkerPixel(n / 2, n / 2, n, false);
        Check(alpha(centre) == 255, "the centre dot is opaque");
        Check(low(centre) > 200 && mid(centre) < 90 && high(centre) > 180,
              "the dot is magenta (strong red and blue, little green): not the cyan reticle, not red, not green");
        const float half = n * 0.5f;
        const auto at = [&](float radius) {
            return MarkerPixel(static_cast<uint32_t>(half + radius * half), n / 2, n, false);
        };
        Check(alpha(at(kMarkerRingRadius)) == 255, "the ring is opaque on its radius");
        const float gap = 0.5f * (kMarkerDotRadius + kMarkerOutline + kMarkerRingRadius - kMarkerRingHalfWidth -
                                  kMarkerOutline);
        Check(alpha(at(gap)) == 0, "between the dot and the ring the marker is see-through");
        Check(alpha(at(0.97f)) == 0 && alpha(MarkerPixel(0, 0, n, false)) == 0 &&
                  alpha(MarkerPixel(n - 1, n - 1, n, false)) == 0,
              "outside the ring and in the corners the marker is see-through");
        const uint32_t rim = at(kMarkerRingRadius + kMarkerRingHalfWidth + kMarkerOutline * 0.5f);
        Check(alpha(rim) > 0 && low(rim) < low(at(kMarkerRingRadius)), "a dark rim keeps it legible on bright ground");
        const uint32_t swapped = MarkerPixel(n / 2, n / 2, n, true);
        Check(low(swapped) == high(centre) && high(swapped) == low(centre) && mid(swapped) == mid(centre) &&
                  alpha(swapped) == alpha(centre),
              "a BGRA swapchain gets the same colour with red and blue swapped");
        Check(MarkerPixel(1, 1, 0, false) == 0, "a zero-size texture paints nothing");
        // Concentric with the default reticle, the marker fits inside its inner ticks (radius 7 of 32).
        Check(kMarkerScaleOfReticle * MarkerOuterRadius() < 7.0f / 32.0f,
              "the marker sits inside the default reticle's inner ticks when the shot lands on the aim point");
        Check(Near(MarkerWidth(0.4f), 0.4f * kMarkerScaleOfReticle), "the marker's width follows the reticle's");
    }

    std::printf("Assess reticle: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
