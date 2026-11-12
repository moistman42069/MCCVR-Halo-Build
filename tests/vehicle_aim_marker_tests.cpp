// The vehicle aim ring (src/common/vehicle_aim_marker_logic.h): Halo Campaign Evolved's ring picture pixel for pixel,
// its colour in either kind of swapchain, the way the vehicle aims, the range held through a miss, the pull-back off
// a surface and the apparent size.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../src/common/vehicle_aim_marker_logic.h"

using namespace vehicle_aim_marker;

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool Near(float a, float b, float tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

// Campaign Evolved's ring, written out on its own terms (its generate_ring: the band, the feather, the clamps), to
// hold Coverage to it.
static float ReferenceRing(int x, int y, int dim, float radiusFrac, float thickFrac, float dotFrac)
{
    const float c = (float)dim * 0.5f - 0.5f;
    float rf = radiusFrac; if (rf < 0.02f) rf = 0.02f; if (rf > 0.48f) rf = 0.48f;
    const float R = (float)dim * rf;
    float tf = thickFrac; if (tf < 0.004f) tf = 0.004f; if (tf > 0.200f) tf = 0.200f;
    const bool no_dot = !(dotFrac > 0.0f);
    float df = dotFrac; if (df < 0.000f) df = 0.000f; if (df > 0.200f) df = 0.200f;
    const float T = (float)dim * tf;
    const float D = (float)dim * df;
    const float AA = 1.25f;
    const float dx = (float)x - c, dy = (float)y - c;
    const float d = std::sqrt(dx * dx + dy * dy);
    float ring = 1.0f - (std::fabs(d - R) - T) / AA;
    if (ring > 1.0f) ring = 1.0f;
    if (ring < 0.0f) ring = 0.0f;
    float dot = no_dot ? 0.0f : 1.0f - (d - D) / AA;
    if (dot > 1.0f) dot = 1.0f;
    if (dot < 0.0f) dot = 0.0f;
    return ring > dot ? ring : dot;
}

static void TestPicture()
{
    Check(kTextureSize == 128 && kRadius == 0.40f && kHalfThickness == 0.008f && kDot == 0.0f && kEdgePixels == 1.25f,
          "Campaign Evolved's ring: 128 px, radius 0.40, half-thickness 0.008, no dot, 1.25 px feather");
    Check(kRed == 0.80f && kGreen == 0.55f && kBlue == 1.00f && kAlpha == 0.9f, "light purple at 0.9 alpha");

    bool same = true;
    for (int y = 0; y < kTextureSize; ++y)
        for (int x = 0; x < kTextureSize; ++x)
            same = same && Coverage(x, y, kTextureSize) == ReferenceRing(x, y, kTextureSize, 0.40f, 0.008f, 0.0f);
    Check(same, "every pixel of the picture is Campaign Evolved's");
    bool tunedSame = true;
    const float tunings[][3] = {{0.25f, 0.05f, 0.03f}, {0.6f, 0.001f, -1.0f}, {0.01f, 0.5f, 0.5f}};
    for (const auto& t : tunings)
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                tunedSame = tunedSame && Coverage(x, y, 64, t[0], t[1], t[2]) == ReferenceRing(x, y, 64, t[0], t[1], t[2]);
    Check(tunedSame, "and so is it with other numbers, clamped as Campaign Evolved clamps them");

    // The shape: empty in the middle (no dot), full on the band at 0.40 x 128 = 51.2 px from the centre (63.5),
    // empty well inside and outside it.
    Check(Coverage(63, 63, kTextureSize) == 0.0f && Coverage(64, 64, kTextureSize) == 0.0f, "no centre dot");
    Check(Coverage(115, 64, kTextureSize) > 0.9f, "the band, on the ring's radius");
    Check(Coverage(64 + 40, 64, kTextureSize) == 0.0f && Coverage(127, 64, kTextureSize) == 0.0f,
          "nothing well inside or outside the band");
    Check(Coverage(0, 0, kTextureSize) == 0.0f && Coverage(0, 0, 0) == 0.0f, "nothing in the corners, or in no picture");
    int across = 0;
    for (int x = 64; x < kTextureSize; ++x)
        across += Coverage(x, 64, kTextureSize) > 0.0f ? 1 : 0;
    Check(across >= 3 && across <= 6, "a thin band: a few pixels across, feather included");
}

static void TestColour()
{
    Check(EncodeChannel(0.80f, false) == 204 && EncodeChannel(0.55f, false) == 140 && EncodeChannel(1.0f, false) == 255,
          "a linear swapchain takes the numbers as they are");
    Check(EncodeChannel(0.80f, true) == 231 && EncodeChannel(0.55f, true) == 196 && EncodeChannel(1.0f, true) == 255,
          "an sRGB swapchain gets the same light, sRGB encoded");
    Check(EncodeChannel(0.0f, true) == 0 && EncodeChannel(-1.0f, true) == 0 && EncodeChannel(2.0f, false) == 255 &&
              EncodeChannel(NAN, true) == 0,
          "out of range and not-a-number are held to 0..1");

    const uint32_t band = Pixel(115, 64, kTextureSize, false, true);
    Check((band & 0xFF) == 231 && ((band >> 8) & 0xFF) == 196 && ((band >> 16) & 0xFF) == 255,
          "RGBA order: red, green, blue");
    Check(((band >> 24) & 0xFF) == static_cast<uint32_t>(Coverage(115, 64, kTextureSize) * kAlpha * 255.0f + 0.5f),
          "the alpha is the coverage at 0.9");
    const uint32_t swapped = Pixel(115, 64, kTextureSize, true, true);
    Check((swapped & 0xFF) == 255 && ((swapped >> 16) & 0xFF) == 231 && ((swapped >> 24) & 0xFF) == (band >> 24),
          "BGRA order swaps red and blue only");
    const uint32_t centre = Pixel(64, 64, kTextureSize, false, true);
    Check((centre >> 24) == 0 && (centre & 0x00FFFFFFu) == (band & 0x00FFFFFFu),
          "straight alpha: the middle is clear and keeps the ring's colour, so a filtered edge has no dark fringe");
    Check(((Pixel(115, 64, kTextureSize, false, false) & 0xFF)) == 204, "and in a linear swapchain, the plain numbers");
}

static void TestDirection()
{
    float out[3]{};
    const float east[3]{1.0f, 0.0f, 0.0f};
    const float levelCamera[3]{0.0f, 1.0f, 0.0f};
    Check(Direction(east, levelCamera, out) && Near(out[0], 1.0f, 1e-6f) && Near(out[1], 0.0f, 1e-6f) &&
              Near(out[2], 0.0f, 1e-6f),
          "the vehicle's heading, not the camera's: the camera looking aside does not move it");
    const float upCamera[3]{0.0f, 0.866025f, 0.5f};
    Check(Direction(east, upCamera, out) && Near(out[0], 0.866025f, 1e-5f) && Near(out[1], 0.0f, 1e-6f) &&
              Near(out[2], 0.5f, 1e-5f),
          "at the camera's pitch: 30 degrees up");
    const float climbing[3]{0.0f, 2.0f, 2.0f};   // nose 45 degrees up, heading north, not unit
    const float downCamera[3]{3.0f, 0.0f, -3.0f};
    Check(Direction(climbing, downCamera, out) && Near(out[0], 0.0f, 1e-6f) && Near(out[1], 0.707107f, 1e-5f) &&
              Near(out[2], -0.707107f, 1e-5f),
          "a pitched hull gives its heading only; neither input need be unit");
    const float lengthSquared = out[0] * out[0] + out[1] * out[1] + out[2] * out[2];
    Check(Near(lengthSquared, 1.0f, 1e-5f), "the direction is unit");
    const float straightUp[3]{0.0f, 0.01f, 1.0f};
    Check(!Direction(straightUp, levelCamera, out), "no heading for a vehicle pointing straight up");
    const float none[3]{0.0f, 0.0f, 0.0f};
    const float bad[3]{NAN, 0.0f, 0.0f};
    Check(!Direction(none, levelCamera, out) && !Direction(east, none, out) && !Direction(bad, levelCamera, out) &&
              !Direction(east, bad, out) && !Direction(nullptr, levelCamera, out),
          "nothing from a zero or non-finite direction");
}

static void TestRange()
{
    HeldRange held;
    Check(held.Use(false, false, 0.0f, 300.0f) == 300.0f && !held.Holding(), "the far end before anything is hit");
    Check(held.Use(true, false, 300.0f, 300.0f) == 300.0f && !held.Holding(), "a miss before any hit is the far end");
    Check(held.Use(true, true, 12.5f, 300.0f) == 12.5f && held.Holding(), "a hit's range");
    Check(held.Use(true, false, 300.0f, 300.0f) == 12.5f, "held through a miss: sky has no range");
    Check(held.Use(false, true, 40.0f, 300.0f) == 12.5f, "and through a trace that is not fresh");
    Check(held.Use(true, true, 40.0f, 300.0f) == 40.0f, "the next hit moves it");
    Check(held.Use(true, true, NAN, 300.0f) == 40.0f && held.Use(true, true, -1.0f, 300.0f) == 40.0f,
          "a range that is not a distance moves nothing");
    held.Reset();
    Check(held.Use(true, false, 300.0f, 300.0f) == 300.0f && !held.Holding(), "a new seat measures afresh");
    Check(held.Use(false, false, 0.0f, NAN) == 0.0f, "no far end, no range");

    Check(PullBack(100.0f, 0.165f) == 100.0f - 0.165f, "pulled back off the surface by the offset");
    Check(PullBack(0.2f, 0.165f) == 0.1f, "never by more than half the range");
    Check(PullBack(10.0f, -1.0f) == 10.0f && PullBack(10.0f, NAN) == 10.0f, "no offset, no pull-back");
    Check(PullBack(0.0f, 0.1f) == 0.0f && PullBack(NAN, 0.1f) == 0.0f, "no range, nowhere to draw");
}

static void TestSize()
{
    const float width = WidthAt(10.0f);
    Check(Near(width, 0.6144f, 1e-5f), "Campaign Evolved's vehicle crosshair width: 0.6144 m at 10 m");
    const float degrees = 2.0f * std::atan(width * 0.5f / 10.0f) / 0.0174532925f;
    Check(degrees > 3.51f && degrees < 3.53f, "about 3.52 degrees");
    Check(Near(WidthAt(1000.0f) / 1000.0f, width / 10.0f, 1e-6f), "the same apparent size at any distance");
    Check(WidthAt(0.0f) == 0.0f && WidthAt(-1.0f) == 0.0f && WidthAt(NAN) == 0.0f, "nothing from a bad distance");

    Check(DrawDistance(12.0f, 50.0f) == 12.0f && DrawDistance(900.0f, 50.0f) == 50.0f,
          "drawn where it is, parked at the crosshair's cap beyond it");
    Check(DrawDistance(900.0f, 0.0f) == 900.0f && DrawDistance(900.0f, NAN) == 900.0f,
          "a cap that is not a distance parks nothing");
    Check(DrawDistance(0.0f, 50.0f) == 0.0f && DrawDistance(NAN, 50.0f) == 0.0f, "no distance, nothing to draw");
}

int main()
{
    TestPicture();
    TestColour();
    TestDirection();
    TestRange();
    TestSize();
    std::printf("PASS: %u vehicle aim ring checks\n", g_checks);
    return 0;
}
