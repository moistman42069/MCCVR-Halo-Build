// Reach: navigation markers are projected with the eye's field of view (src/common/reach_nav_marker_logic.h).
// GEOMETRY ON A MODEL, not the game: the header's own model of the projection, as read from the pinned module.
// These checks say what follows IF the game projects as the model does: with the eye's field of view the marker
// is on its point's pixel for each eye, turning the head leaves it there, sweeping the gun moves nothing; and
// how far off the stock placement is. The in-game results are in docs/REACH-NAV-MARKERS-2026-10-07.md.
// Also the off-screen ring about the head (part 2): its field, its verdicts and the stand-in
// point, again on the model only (the ring is not confirmed in game).
#include "reach_nav_marker_logic.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

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
bool Near(float a, float b, float tol = 1.0e-4f) { return std::fabs(a - b) <= tol; }

using namespace reach_nav_markers;

// A real headset's raster, as the reference case: 5242 x 3780, the eye's symmetric cover
// 63.2 / 55.0 degrees (so a vertical field of view of 110 degrees), the game's own about 61 degrees.
constexpr float kWidth = 5242.0f, kHeight = 3780.0f;
const float kEyeFov = 110.0f * kDegToRad;
const float kGameFov = 61.2f * kDegToRad;

void Normalize(float v[3])
{
    const float length = std::sqrt(Dot(v, v));
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
}
// v turned about the unit axis by angle (right-hand rule).
void Rotate(float v[3], const float axis[3], float angle)
{
    const float c = std::cos(angle), s = std::sin(angle);
    float cross[3];
    Cross(axis, v, cross);
    const float along = Dot(axis, v) * (1.0f - c);
    for (int i = 0; i < 3; ++i)
        v[i] = v[i] * c + cross[i] * s + axis[i] * along;
}
// A head: yaw about world up (+Z, counter-clockwise from +X), then pitch up, then roll about its forward.
Camera Head(float yawDeg, float pitchDeg, float rollDeg, float x, float y, float z, float fov)
{
    Camera head;
    head.position[0] = x;
    head.position[1] = y;
    head.position[2] = z;
    head.verticalFov = fov;
    const float worldUp[3]{0.0f, 0.0f, 1.0f};
    float forward[3]{1.0f, 0.0f, 0.0f}, up[3]{0.0f, 0.0f, 1.0f};
    Rotate(forward, worldUp, yawDeg * kDegToRad);
    float right[3];
    Cross(forward, up, right);
    Rotate(forward, right, pitchDeg * kDegToRad);
    Rotate(up, right, pitchDeg * kDegToRad);
    Rotate(up, forward, rollDeg * kDegToRad);
    for (int i = 0; i < 3; ++i)
    {
        head.forward[i] = forward[i];
        head.up[i] = up[i];
    }
    return head;
}
// One eye of that head: `side` metres along the head's right, canted `cantDeg` about the head's up (positive
// turns the eye to the right), the way ReachApplyEyeOffset builds it.
Camera Eye(const Camera& head, float side, float cantDeg)
{
    Camera eye = head;
    float right[3];
    Cross(head.forward, head.up, right);
    for (int i = 0; i < 3; ++i)
        eye.position[i] += right[i] * side;
    Rotate(eye.forward, head.up, -cantDeg * kDegToRad);
    return eye;
}
// What the observer-camera hook leaves in the marker projection's camera: the game built it from the observer
// camera (the AIM, with the game's own field of view); the hook gives it the eye's position, forward and up; and
// this lane the eye's field of view, when it may.
Camera MarkerCamera(const Camera& fromObserver, const Camera& eye, bool switchOn, Fov* verdict = nullptr)
{
    Camera marker = fromObserver;
    for (int i = 0; i < 3; ++i)
    {
        marker.position[i] = eye.position[i];
        marker.forward[i] = eye.forward[i];
        marker.up[i] = eye.up[i];
    }
    float fov = marker.verticalFov;
    const Fov use = EyeFov(switchOn, marker.verticalFov, eye.verticalFov, fov);
    marker.verticalFov = fov;
    if (verdict)
        *verdict = use;
    return marker;
}
// A point `distance` away from the camera, `rightDeg` to the right of and `upDeg` above its axis, measured as
// the raster measures them (tangents on the camera's own axes).
void PointAt(const Camera& camera, float rightDeg, float upDeg, float distance, float out[3])
{
    float right[3];
    Cross(camera.forward, camera.up, right);
    const float tx = std::tan(rightDeg * kDegToRad), ty = std::tan(upDeg * kDegToRad);
    for (int i = 0; i < 3; ++i)
        out[i] = camera.position[i] + distance * (camera.forward[i] + right[i] * tx + camera.up[i] * ty);
}
// Where the raster of a camera with this vertical field of view puts a direction `rightDeg` / `upDeg` off its
// axis, straight from the angles (not through ProjectToRaster): the eye's own picture.
void RasterPixel(float fov, float rightDeg, float upDeg, float out[2])
{
    const float t = std::tan(fov * 0.5f), aspect = kWidth / kHeight;
    out[0] = kWidth * 0.5f * (1.0f + std::tan(rightDeg * kDegToRad) / (aspect * t));
    out[1] = kHeight * 0.5f * (1.0f - std::tan(upDeg * kDegToRad) / t);
}

void FieldOfView()
{
    float out = -1.0f;
    Check(EyeFov(true, kGameFov, kEyeFov, out) == Fov::Used && out == kEyeFov, "the eye's field of view is used");
    out = -1.0f;
    Check(EyeFov(false, kGameFov, kEyeFov, out) == Fov::SwitchOff && out == -1.0f, "switch off writes nothing");
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (float bad : {nan, inf, -inf, 0.0f, -1.0f, kMinVerticalFov, kMaxVerticalFov, kPi, 4.0f})
    {
        out = -1.0f;
        Check(EyeFov(true, bad, kEyeFov, out) == Fov::NoGameFov && out == -1.0f,
              "a camera field that is not a field of view is refused, and nothing is written");
        out = -1.0f;
        Check(EyeFov(true, kGameFov, bad, out) == Fov::NoEyeFov && out == -1.0f,
              "an eye without a field of view is refused, and nothing is written");
    }
    // Identity only: any angle the game's own range admits is used, however small or large.
    for (float fov : {0.001f, 0.2f, 1.0f, 2.0f, 3.0f, 3.14f})
    {
        out = -1.0f;
        Check(EyeFov(true, fov, 3.14f - fov * 0.5f, out) == Fov::Used && out == 3.14f - fov * 0.5f,
              "never refused for being narrow or wide inside the game's own range");
    }
    Check(FovName(Fov::Used) && FovName(Fov::SwitchOff) && FovName(Fov::NoGameFov) && FovName(Fov::NoEyeFov),
          "every verdict has a name for the log");
    Check(static_cast<int>(Fov::Count) == 4, "every verdict is counted");
    Check(kCameraVerticalFovOffset == 0x28, "the render camera's vertical field of view is at +0x28");
}

void Magnified()
{
    const float k = Magnification(kGameFov, kEyeFov);
    Check(Near(k, std::tan(55.0f * kDegToRad) / std::tan(30.6f * kDegToRad), 1.0e-4f),
          "the magnification is the ratio of the half-angles' tangents");
    Check(k > 2.3f && k < 2.5f, "about 2.4 times at the measured angles");
    Check(Magnification(kEyeFov, kEyeFov) == 1.0f, "the same field of view magnifies nothing");
    Check(Magnification(std::numeric_limits<float>::quiet_NaN(), kEyeFov) == 1.0f &&
              Magnification(kGameFov, 0.0f) == 1.0f,
          "no magnification is claimed from an unusable angle");
    // Ten degrees off the eye's axis is drawn at twenty-three by the stock placement; on the axis nothing moves.
    Check(Near(StockSeenOffAxis(10.0f * kDegToRad, kGameFov, kEyeFov) * kRadToDeg, 23.06f, 0.05f),
          "ten degrees off axis was drawn twenty-three degrees off");
    Check(StockSeenOffAxis(0.0f, kGameFov, kEyeFov) == 0.0f, "on the eye's axis the stock marker was right");
    // The same error in pixels, against the projection model: the marker projected with the game's angle minus
    // the target's own pixel.
    const Camera eye = Head(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kEyeFov);
    Camera stock = eye;
    stock.verticalFov = kGameFov;
    for (float deg : {10.0f, 20.0f, 30.0f})
    {
        float point[3], seen[2], drawn[2];
        PointAt(eye, 0.0f, deg, 25.0f, point);
        Check(ProjectToRaster(eye, point, kWidth, kHeight, seen) && ProjectToRaster(stock, point, kWidth, kHeight, drawn),
              "both projections place a point in front of the camera");
        Check(Near(seen[1] - drawn[1], StockErrorPixels(deg * kDegToRad, kGameFov, kEyeFov, kHeight), 0.5f),
              "the stock error in pixels is the magnification law");
        Check(seen[1] - drawn[1] > 100.0f, "and it is hundreds of pixels of the raster");
    }
    Check(StockErrorPixels(0.3f, kGameFov, 0.0f, kHeight) == 0.0f, "no pixel error is claimed from an unusable angle");
}

void Projection()
{
    const Camera camera = Head(30.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f, kEyeFov);
    float point[3], pixel[2], expect[2];
    PointAt(camera, 0.0f, 0.0f, 10.0f, point);
    Check(ProjectToRaster(camera, point, kWidth, kHeight, pixel) && Near(pixel[0], kWidth * 0.5f, 0.01f) &&
              Near(pixel[1], kHeight * 0.5f, 0.01f),
          "a point on the axis is at the raster's centre");
    // The raster's own edges: the vertical half-angle at the top, and aspect times it at the right.
    PointAt(camera, 0.0f, 55.0f, 10.0f, point);
    Check(ProjectToRaster(camera, point, kWidth, kHeight, pixel) && Near(pixel[1], 0.0f, 0.05f),
          "the vertical half-angle is the raster's top edge");
    const float halfHorizontal = std::atan(std::tan(55.0f * kDegToRad) * kWidth / kHeight) * kRadToDeg;
    Check(Near(halfHorizontal, 63.2f, 0.1f), "the measured horizontal cover follows from the vertical and the aspect");
    PointAt(camera, halfHorizontal, 0.0f, 10.0f, point);
    Check(ProjectToRaster(camera, point, kWidth, kHeight, pixel) && Near(pixel[0], kWidth, 0.05f),
          "the horizontal half-angle is the raster's right edge");
    // Right is right and up is up, at any distance.
    for (float distance : {0.5f, 5.0f, 500.0f})
    {
        PointAt(camera, 12.0f, -7.0f, distance, point);
        RasterPixel(kEyeFov, 12.0f, -7.0f, expect);
        Check(ProjectToRaster(camera, point, kWidth, kHeight, pixel) && Near(pixel[0], expect[0], 0.05f) &&
                  Near(pixel[1], expect[1], 0.05f) && pixel[0] > kWidth * 0.5f && pixel[1] > kHeight * 0.5f,
              "a point to the right of and below the axis is right of and below the centre, whatever its distance");
    }
    // Nothing at or behind the camera's plane has a pixel, and `out` is untouched.
    float behind[3]{camera.position[0] - camera.forward[0], camera.position[1] - camera.forward[1],
                    camera.position[2] - camera.forward[2]};
    pixel[0] = pixel[1] = -7.0f;
    Check(!ProjectToRaster(camera, behind, kWidth, kHeight, pixel) && pixel[0] == -7.0f && pixel[1] == -7.0f,
          "a point behind the camera has no pixel");
    Check(!ProjectToRaster(camera, camera.position, kWidth, kHeight, pixel), "the camera's own position has no pixel");
    Camera noFov = camera;
    noFov.verticalFov = 0.0f;
    PointAt(camera, 0.0f, 0.0f, 10.0f, point);
    Check(!ProjectToRaster(noFov, point, kWidth, kHeight, pixel) && !ProjectToRaster(camera, point, 0.0f, kHeight, pixel),
          "no field of view or no raster: no pixel");
}

// THE HEADSET CHECKS. The marker's pixel against its target's own pixel in the eye's raster.
void OnTheTarget()
{
    // The aim: wherever the gun points. The game builds the marker camera from it, with its own field of view.
    const Camera head = Head(40.0f, 5.0f, 0.0f, 10.0f, -4.0f, 2.0f, kEyeFov);
    for (float cant : {0.0f, 5.0f})  // parallel eyes, and eyes canted outward
        for (int side = -1; side <= 1; side += 2)
        {
            const Camera eye = Eye(head, side * 0.032f, side * cant);
            for (float deg : {0.0f, 10.0f, 20.0f, 30.0f})
                for (int direction = 0; direction < 4; ++direction)
                {
                    const float rightDeg = direction == 0 ? deg : direction == 1 ? -deg : 0.0f;
                    const float upDeg = direction == 2 ? deg : direction == 3 ? -deg : 0.0f;
                    float target[3], seen[2], marker[2], stockMarker[2];
                    PointAt(eye, rightDeg, upDeg, 18.0f, target);
                    RasterPixel(kEyeFov, rightDeg, upDeg, seen);  // where the eye's picture shows the target
                    // The gun swept all the way round, and up and down: the marker camera the game builds first.
                    float first[2]{};
                    bool have = false, still = true;
                    for (float aimYaw = -180.0f; aimYaw < 180.0f; aimYaw += 45.0f)
                        for (float aimPitch : {-40.0f, 0.0f, 40.0f})
                        {
                            const Camera aim = Head(aimYaw, aimPitch, 0.0f, 9.7f, -4.1f, 1.8f, kGameFov);
                            Fov verdict = Fov::Count;
                            const Camera fixed = MarkerCamera(aim, eye, true, &verdict);
                            Check(verdict == Fov::Used, "the eye's field of view is taken at the marker's call site");
                            Check(ProjectToRaster(fixed, target, kWidth, kHeight, marker), "the marker has a pixel");
                            if (!have)
                            {
                                first[0] = marker[0];
                                first[1] = marker[1];
                                have = true;
                            }
                            still = still && marker[0] == first[0] && marker[1] == first[1];
                        }
                    Check(still, "head still, gun swept: the marker does not move by a pixel");
                    Check(Near(marker[0], seen[0], 0.05f) && Near(marker[1], seen[1], 0.05f),
                          "the marker is on its target's own pixel in this eye (0, 10, 20, 30 degrees off axis)");
                    // The control arm: the switch off is the game's own field of view on the eye's pose.
                    Fov verdict = Fov::Count;
                    const Camera stock = MarkerCamera(Head(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kGameFov), eye, false, &verdict);
                    Check(verdict == Fov::SwitchOff && stock.verticalFov == kGameFov, "switch off keeps the game's angle");
                    Check(ProjectToRaster(stock, target, kWidth, kHeight, stockMarker), "the stock marker has a pixel");
                    const float off = std::hypot(stockMarker[0] - seen[0], stockMarker[1] - seen[1]);
                    Check(deg == 0.0f ? off < 0.05f : off > 100.0f,
                          "the stock marker was right on the eye's axis only, and hundreds of pixels off elsewhere");
                }
        }
}

void HeadTurned()
{
    // One target fixed in the world; the head turns, nods and leans. The marker stays on the target's pixel.
    const float target[3]{30.0f, 6.0f, 4.0f};
    for (float yaw : {-30.0f, -20.0f, -10.0f, 0.0f, 10.0f, 20.0f, 30.0f})
        for (float pitch : {-20.0f, 0.0f, 15.0f})
            for (float roll : {-25.0f, 0.0f, 25.0f})
            {
                const Camera head = Head(yaw, pitch, roll, 0.0f, 0.0f, 1.7f, kEyeFov);
                for (int side = -1; side <= 1; side += 2)
                {
                    const Camera eye = Eye(head, side * 0.032f, 0.0f);
                    // Where the eye's picture shows the target: its tangents on the eye's own axes.
                    float right[3], seen[2], marker[2], stockMarker[2];
                    Cross(eye.forward, eye.up, right);
                    const float d[3]{target[0] - eye.position[0], target[1] - eye.position[1],
                                     target[2] - eye.position[2]};
                    const float depth = Dot(d, eye.forward);
                    RasterPixel(kEyeFov, std::atan(Dot(d, right) / depth) * kRadToDeg,
                                std::atan(Dot(d, eye.up) / depth) * kRadToDeg, seen);
                    const Camera aim = Head(75.0f, -10.0f, 0.0f, 0.1f, 0.0f, 1.6f, kGameFov);
                    Check(ProjectToRaster(MarkerCamera(aim, eye, true), target, kWidth, kHeight, marker) &&
                              Near(marker[0], seen[0], 0.1f) && Near(marker[1], seen[1], 0.1f),
                          "the head turned, nodded and leaned: the marker stays on its target in each eye");
                    Check(ProjectToRaster(MarkerCamera(aim, eye, false), target, kWidth, kHeight, stockMarker),
                          "the stock marker has a pixel");
                    // The stock marker slid away from the target as the head turned away from it.
                    const float offAxis = std::acos(depth / std::sqrt(Dot(d, d))) * kRadToDeg;
                    const float off = std::hypot(stockMarker[0] - seen[0], stockMarker[1] - seen[1]);
                    Check(offAxis < 2.0f || off > 30.0f, "the stock marker left its target as soon as the head turned");
                }
            }
}

void BothEyes()
{
    // A target straight ahead of the head: each eye sees it toward the nose by the same amount, and the marker's
    // separation between the eyes is the target's own, so the two fuse at the target's distance. The stock
    // marker's separation was the magnification times that: it floated nearer than its target.
    const Camera head = Head(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kEyeFov);
    const float ipd = 0.064f;
    const Camera aim = Head(20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kGameFov);
    for (float distance : {1.0f, 3.0f, 10.0f, 100.0f})
    {
        const float target[3]{distance, 0.0f, 0.0f};
        float left[2], right[2], stockLeft[2], stockRight[2];
        const Camera leftEye = Eye(head, -ipd * 0.5f, 0.0f), rightEye = Eye(head, ipd * 0.5f, 0.0f);
        Check(ProjectToRaster(MarkerCamera(aim, leftEye, true), target, kWidth, kHeight, left) &&
                  ProjectToRaster(MarkerCamera(aim, rightEye, true), target, kWidth, kHeight, right) &&
                  ProjectToRaster(MarkerCamera(aim, leftEye, false), target, kWidth, kHeight, stockLeft) &&
                  ProjectToRaster(MarkerCamera(aim, rightEye, false), target, kWidth, kHeight, stockRight),
              "both eyes place the marker");
        const float t = std::tan(kEyeFov * 0.5f), aspect = kWidth / kHeight;
        const float own = kWidth * ipd / (2.0f * distance * aspect * t);  // the target's own separation, in pixels
        Check(Near(left[0] - right[0], own, 0.05f) && Near(left[1], right[1], 0.001f),
              "the marker's separation between the eyes is its target's own: no double image");
        Check(Near((stockLeft[0] - stockRight[0]) / own, Magnification(kGameFov, kEyeFov), 0.01f),
              "the stock marker's separation was the magnification times its target's");
    }
}

// ---- the projection's own sites ------------------------------------------------------------------------------------
// A pattern's bytes, wildcards as zero.
std::vector<uint8_t> PatternBytes(const char* pattern)
{
    std::vector<uint8_t> bytes;
    for (const char* at = pattern; *at;)
    {
        while (*at == ' ')
            ++at;
        if (!*at)
            break;
        if (at[0] == '?')
        {
            bytes.push_back(0);
            while (*at == '?')
                ++at;
            continue;
        }
        char* end = nullptr;
        bytes.push_back(static_cast<uint8_t>(std::strtoul(at, &end, 16)));
        at = end;
    }
    return bytes;
}
void PutI32(std::vector<uint8_t>& bytes, size_t at, int32_t value) { std::memcpy(bytes.data() + at, &value, 4); }

// The function as retail haloreach.dll lays it out (offsets from the module base).
constexpr int64_t kHeadRva = 0x2E1430, kCameraRva = 0x2E14FC, kPixelRva = 0x2E15BC, kRingRva = 0x2E172B,
                  kOffscreenRva = 0x2E1817, kConverterRva = 0x287DFC, kChudReturnRva = 0x2E1525,
                  kFractionRva = 0xA8ACB4, kPiRva = 0xA8B22C, kFloorRva = 0xA8A9B8,
                  kNavpointCallRva = 0x2E3D39, kNavpointFunctionRva = 0x2E3BAC, kNavpointReturnRva = 0x2E3D56,
                  kObjectCallRva = 0x2DAEB6, kObjectFunctionRva = 0x2DAE98, kObjectReturnRva = 0x2DAED3;
struct Image
{
    std::vector<uint8_t> head, camera, ring, offscreen, pixel;
    uint8_t fraction[4]{}, pi[4]{}, floor[4]{};
};
Image Retail()
{
    Image image;
    image.head = PatternBytes(kProjectionHeadPattern);
    image.camera = PatternBytes(kProjectionCameraPattern);
    PutI32(image.camera, kCameraConverterCall + 1,
           static_cast<int32_t>(kConverterRva - (kCameraRva + static_cast<int64_t>(kCameraConverterReturn))));
    image.ring = PatternBytes(kProjectionRingPattern);
    PutI32(image.ring, kRingFovDisp, 0xA8);  // the camera is at rbp + 0x80
    PutI32(image.ring, kRingFractionDisp,
           static_cast<int32_t>(kFractionRva - (kRingRva + static_cast<int64_t>(kRingFractionNext))));
    image.offscreen = PatternBytes(kProjectionOffscreenPattern);
    PutI32(image.offscreen, kOffscreenPiDisp,
           static_cast<int32_t>(kPiRva - (kOffscreenRva + static_cast<int64_t>(kOffscreenPiNext))));
    PutI32(image.offscreen, kOffscreenFloorDisp,
           static_cast<int32_t>(kFloorRva - (kOffscreenRva + static_cast<int64_t>(kOffscreenFloorNext))));
    image.pixel = PatternBytes(kProjectionPixelPattern);
    const float fraction = 0.3f, pi = kPi, floor = 0.0001f;
    std::memcpy(image.fraction, &fraction, 4);
    std::memcpy(image.pi, &pi, 4);
    std::memcpy(image.floor, &floor, 4);
    return image;
}
ProjectionBind Decode(const Image& image, ProjectionSites& sites, bool headIsFunction = true,
                      int64_t cameraFunction = kHeadRva, int64_t ringFunction = kHeadRva,
                      int64_t offscreenFunction = kHeadRva, int64_t pixelFunction = kHeadRva,
                      int64_t ringRva = kRingRva)
{
    return DecodeProjectionSites(image.head.data(), kHeadRva, headIsFunction, image.camera.data(), kCameraRva,
                                 cameraFunction, image.ring.data(), ringRva, ringFunction, image.offscreen.data(),
                                 kOffscreenRva, offscreenFunction, image.pixel.data(), kPixelRva, pixelFunction,
                                 sites);
}

void Sites()
{
    Check(PatternBytes(kProjectionHeadPattern).size() == kProjectionHeadBytes &&
              PatternBytes(kProjectionCameraPattern).size() == kProjectionCameraBytes &&
              PatternBytes(kProjectionRingPattern).size() == kProjectionRingBytes &&
              PatternBytes(kProjectionOffscreenPattern).size() == kProjectionOffscreenBytes &&
              PatternBytes(kProjectionPixelPattern).size() == kProjectionPixelBytes,
          "every site's length is its pattern's");
    const Image retail = Retail();
    ProjectionSites sites;
    Check(Decode(retail, sites) == ProjectionBind::Bound, "retail's layout decodes");
    Check(sites.headRva == kHeadRva && sites.converterRva == kConverterRva &&
              sites.converterReturnRva == kChudReturnRva,
          "the hook, and the camera call's target and return address, are read from the instructions");
    Check(sites.cameraFovOffset == kCameraVerticalFovOffset && sites.cameraFovOffset == 0x28,
          "the ring reads the camera's vertical field of view at +0x28: where part 1 writes the eye's");
    Check(sites.recordPoint == 4 && sites.recordPixel == 0x10 && sites.recordOffscreen == 0x20,
          "the record: the point at +4, the pixel at +0x10, how far outside the ring at +0x20");
    Check(sites.fractionRva == kFractionRva && sites.piRva == kPiRva && sites.floorRva == kFloorRva,
          "the ring's three constants are where the instructions point");
    Check(CheckProjectionConverter(sites, kConverterRva, kChudReturnRva) == ProjectionBind::Bound,
          "the camera call is the observer-camera hook's function at its CHUD site");
    Check(CheckProjectionConstants(retail.fraction, retail.pi, retail.floor) == ProjectionBind::Bound,
          "the ring's constants are 0.3, pi and 0.0001");

    // Every missing or ambiguous site binds nothing, and a refusal fills nothing.
    ProjectionSites untouched;
    Check(DecodeProjectionSites(nullptr, kHeadRva, true, retail.camera.data(), kCameraRva, kHeadRva,
                                retail.ring.data(), kRingRva, kHeadRva, retail.offscreen.data(), kOffscreenRva,
                                kHeadRva, retail.pixel.data(), kPixelRva, kHeadRva,
                                untouched) == ProjectionBind::HeadMissing,
          "no unique head: nothing binds");
    Check(Decode(retail, untouched, false) == ProjectionBind::HeadMissing,
          "a head that is not a function's beginning: nothing binds");
    Check(DecodeProjectionSites(retail.head.data(), kHeadRva, true, nullptr, kCameraRva, kHeadRva,
                                retail.ring.data(), kRingRva, kHeadRva, retail.offscreen.data(), kOffscreenRva,
                                kHeadRva, retail.pixel.data(), kPixelRva, kHeadRva,
                                untouched) == ProjectionBind::CameraMissing,
          "no unique camera build: nothing binds");
    Check(DecodeProjectionSites(retail.head.data(), kHeadRva, true, retail.camera.data(), kCameraRva, kHeadRva,
                                nullptr, kRingRva, kHeadRva, retail.offscreen.data(), kOffscreenRva, kHeadRva,
                                retail.pixel.data(), kPixelRva, kHeadRva, untouched) == ProjectionBind::RingMissing,
          "no unique ring: nothing binds");
    Check(DecodeProjectionSites(retail.head.data(), kHeadRva, true, retail.camera.data(), kCameraRva, kHeadRva,
                                retail.ring.data(), kRingRva, kHeadRva, nullptr, kOffscreenRva, kHeadRva,
                                retail.pixel.data(), kPixelRva, kHeadRva,
                                untouched) == ProjectionBind::OffscreenMissing,
          "no unique off-screen store: nothing binds");
    Check(DecodeProjectionSites(retail.head.data(), kHeadRva, true, retail.camera.data(), kCameraRva, kHeadRva,
                                retail.ring.data(), kRingRva, kHeadRva, retail.offscreen.data(), kOffscreenRva,
                                kHeadRva, nullptr, kPixelRva, kHeadRva, untouched) == ProjectionBind::PixelMissing,
          "no unique pixel store: nothing binds");
    // A site in another function, in no function, or out of order.
    Check(Decode(retail, untouched, true, kHeadRva + 0x40) == ProjectionBind::OutsideFunction &&
              Decode(retail, untouched, true, kHeadRva, -1) == ProjectionBind::OutsideFunction &&
              Decode(retail, untouched, true, kHeadRva, kHeadRva, kHeadRva - 0x100) ==
                  ProjectionBind::OutsideFunction &&
              Decode(retail, untouched, true, kHeadRva, kHeadRva, kHeadRva, 0) == ProjectionBind::OutsideFunction,
          "a site outside the matched function is refused");
    Check(Decode(retail, untouched, true, kHeadRva, kHeadRva, kHeadRva, kHeadRva, kCameraRva - 8) ==
              ProjectionBind::OutsideFunction,
          "sites out of order are refused");

    // THE NEIGHBOURING FIELD. The likeliest way a patch goes wrong: the ring reads the field beside the vertical
    // field of view (+0x2C, a second angle the converter also scales). Part 1 would then write the eye's angle
    // into a field the projection does not use.
    Image moved = retail;
    PutI32(moved.ring, kRingFovDisp, 0xA8 + 4);
    Check(Decode(moved, untouched) == ProjectionBind::FovMismatch, "a ring reading the neighbouring field is refused");
    moved = retail;
    PutI32(moved.ring, kRingFovDisp, 0xA8 - 4);
    Check(Decode(moved, untouched) == ProjectionBind::FovMismatch, "a ring reading the field before is refused");
    moved = retail;
    PutI32(moved.camera, kCameraBufferDisp, 0x90);  // the camera buffer moved, the ring's read did not
    Check(Decode(moved, untouched) == ProjectionBind::FovMismatch, "a camera buffer that moved alone is refused");
    moved = retail;
    PutI32(moved.camera, kCameraPositionYDisp, 0x88);
    Check(Decode(moved, untouched) == ProjectionBind::FovMismatch,
          "a position that is not the camera's first field is refused");
    // The record.
    moved = retail;
    moved.camera[kCameraPointX] = 8;
    Check(Decode(moved, untouched) == ProjectionBind::RecordMismatch, "a point that moved alone is refused");
    moved = retail;
    moved.camera[kCameraPointX] = 5;
    moved.camera[kCameraPointY] = 9;
    Check(Decode(moved, untouched) == ProjectionBind::RecordMismatch, "a point that is not float-aligned is refused");
    moved = retail;
    moved.pixel[kPixelX] = 0x14;
    Check(Decode(moved, untouched) == ProjectionBind::RecordMismatch, "a pixel that moved alone is refused");
    moved = retail;
    moved.pixel[kPixelY] = 0x18;
    Check(Decode(moved, untouched) == ProjectionBind::RecordMismatch, "a pixel whose halves are apart is refused");
    moved = retail;
    moved.offscreen[kOffscreenRecord] = 0x24;
    Check(Decode(moved, untouched) == ProjectionBind::RecordMismatch, "an off-screen value that moved alone is refused");
    // The camera call.
    moved = retail;
    moved.camera[kCameraConverterCall] = 0xE9;
    Check(Decode(moved, untouched) == ProjectionBind::ConverterMismatch, "a camera call that is not a call is refused");
    Check(untouched.headRva == 0 && untouched.recordPoint == 0 && untouched.converterRva == 0,
          "a refusal fills nothing");
    Check(CheckProjectionConverter(sites, kConverterRva + 0x10, kChudReturnRva) == ProjectionBind::ConverterMismatch &&
              CheckProjectionConverter(sites, kConverterRva, kChudReturnRva + 5) ==
                  ProjectionBind::ConverterMismatch &&
              CheckProjectionConverter(sites, 0, kChudReturnRva) == ProjectionBind::ConverterMismatch,
          "a camera call that is not the hooked function at the CHUD site is refused");
    // The constants.
    uint8_t wrong[4];
    for (float value : {0.25f, 0.6f, 0.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        std::memcpy(wrong, &value, 4);
        Check(CheckProjectionConstants(wrong, retail.pi, retail.floor) == ProjectionBind::ConstantMismatch &&
                  CheckProjectionConstants(retail.fraction, wrong, retail.floor) == ProjectionBind::ConstantMismatch &&
                  CheckProjectionConstants(retail.fraction, retail.pi, wrong) == ProjectionBind::ConstantMismatch,
              "a ring whose constants are not 0.3, pi and 0.0001 is refused");
    }
    Check(CheckProjectionConstants(nullptr, retail.pi, retail.floor) == ProjectionBind::ConstantMismatch &&
              CheckProjectionConstants(retail.fraction, nullptr, retail.floor) == ProjectionBind::ConstantMismatch &&
              CheckProjectionConstants(retail.fraction, retail.pi, nullptr) == ProjectionBind::ConstantMismatch,
          "an unreadable constant is refused");
    // THE NAVPOINT LIST'S CALL: the one caller the stand-in acts for.
    Check(PatternBytes(kNavpointCallPattern).size() == kNavpointCallBytes && kNavpointReturn == kNavpointCallBytes &&
              kNavpointCall + 5 == kNavpointReturn,
          "the navpoint call's length is its pattern's, and the call is its last instruction");
    std::vector<uint8_t> navpoint = PatternBytes(kNavpointCallPattern);
    PutI32(navpoint, kNavpointCall + 1,
           static_cast<int32_t>(kHeadRva - (kNavpointCallRva + static_cast<int64_t>(kNavpointReturn))));
    ProjectionSites bound = sites;
    Check(bound.navpointReturnRva == 0, "the projection's own sites say nothing about its callers");
    Check(DecodeNavpointCall(navpoint.data(), kNavpointCallRva, kNavpointFunctionRva, bound) ==
                  ProjectionBind::Bound &&
              bound.navpointReturnRva == kNavpointReturnRva && bound.navpointReturnRva == 0x2E3D56,
          "retail's navpoint call lands on the projection and returns to +0x2E3D56");
    ProjectionSites refused = sites;
    Check(DecodeNavpointCall(nullptr, kNavpointCallRva, kNavpointFunctionRva, refused) ==
              ProjectionBind::NavpointCallMissing,
          "a missing or ambiguous navpoint call is refused");
    std::vector<uint8_t> wrongCall = navpoint;
    wrongCall[kNavpointCall] = 0xE9;
    Check(DecodeNavpointCall(wrongCall.data(), kNavpointCallRva, kNavpointFunctionRva, refused) ==
              ProjectionBind::NavpointCallMismatch,
          "a navpoint site that jumps instead of calling is refused");
    for (int32_t off : {-5, -1, 1, 0x10})
    {
        wrongCall = navpoint;
        PutI32(wrongCall, kNavpointCall + 1,
               static_cast<int32_t>(kHeadRva + off - (kNavpointCallRva + static_cast<int64_t>(kNavpointReturn))));
        Check(DecodeNavpointCall(wrongCall.data(), kNavpointCallRva, kNavpointFunctionRva, refused) ==
                  ProjectionBind::NavpointCallMismatch,
              "a navpoint call that lands anywhere but the projection's head is refused");
    }
    Check(DecodeNavpointCall(navpoint.data(), kNavpointCallRva + 1, kNavpointFunctionRva, refused) ==
              ProjectionBind::NavpointCallMismatch,
          "the same bytes one byte further on do not land on the projection: refused");
    Check(DecodeNavpointCall(navpoint.data(), kNavpointCallRva, -1, refused) == ProjectionBind::NavpointCallMismatch &&
              DecodeNavpointCall(navpoint.data(), kNavpointCallRva, kHeadRva, refused) ==
                  ProjectionBind::NavpointCallMismatch,
          "a navpoint call in no function, or inside the projection itself, is refused");
    ProjectionSites unbound;
    Check(DecodeNavpointCall(navpoint.data(), kNavpointCallRva, kNavpointFunctionRva, unbound) ==
              ProjectionBind::NavpointCallMismatch,
          "without a bound projection there is nothing for the call to land on");
    Check(refused.navpointReturnRva == 0 && unbound.navpointReturnRva == 0, "a refusal fills nothing");
    // THE OBJECT MARKERS' CALL: the second caller the stand-in acts for (the off-screen arrows of ordinary play).
    Check(PatternBytes(kObjectMarkerCallPattern).size() == kObjectMarkerCallBytes &&
              kObjectMarkerReturn == kObjectMarkerCallBytes && kObjectMarkerCall + 5 == kObjectMarkerReturn,
          "the object markers' call's length is its pattern's, and the call is its last instruction");
    Check(std::strcmp(kObjectMarkerCallPattern, kNavpointCallPattern) != 0, "it is its own pattern");
    std::vector<uint8_t> objectCall = PatternBytes(kObjectMarkerCallPattern);
    PutI32(objectCall, kObjectMarkerCall + 1,
           static_cast<int32_t>(kHeadRva - (kObjectCallRva + static_cast<int64_t>(kObjectMarkerReturn))));
    Check(DecodeObjectMarkerCall(objectCall.data(), kObjectCallRva, kObjectFunctionRva, bound) == kObjectReturnRva &&
              kObjectReturnRva == 0x2DAED3,
          "retail's object-marker call lands on the projection and returns to +0x2DAED3");
    Check(bound.navpointReturnRva == kNavpointReturnRva, "decoding it leaves the navpoint list's return as it was");
    Check(DecodeObjectMarkerCall(nullptr, kObjectCallRva, kObjectFunctionRva, bound) == 0 &&
              DecodeObjectMarkerCall(objectCall.data(), 0, kObjectFunctionRva, bound) == 0,
          "a missing or ambiguous object-marker call gives no caller");
    {
        std::vector<uint8_t> wrong = objectCall;
        wrong[kObjectMarkerCall] = 0xE9;
        Check(DecodeObjectMarkerCall(wrong.data(), kObjectCallRva, kObjectFunctionRva, bound) == 0,
              "an object-marker site that jumps instead of calling gives no caller");
        for (int32_t off : {-5, -1, 1, 0x10})
        {
            wrong = objectCall;
            PutI32(wrong, kObjectMarkerCall + 1,
                   static_cast<int32_t>(kHeadRva + off - (kObjectCallRva + static_cast<int64_t>(kObjectMarkerReturn))));
            Check(DecodeObjectMarkerCall(wrong.data(), kObjectCallRva, kObjectFunctionRva, bound) == 0,
                  "an object-marker call that lands anywhere but the projection's head gives no caller");
        }
    }
    Check(DecodeObjectMarkerCall(objectCall.data(), kObjectCallRva + 1, kObjectFunctionRva, bound) == 0,
          "the same bytes one byte further on do not land on the projection: no caller");
    Check(DecodeObjectMarkerCall(objectCall.data(), kObjectCallRva, -1, bound) == 0 &&
              DecodeObjectMarkerCall(objectCall.data(), kObjectCallRva, kHeadRva, bound) == 0,
          "an object-marker call in no function, or inside the projection itself, gives no caller");
    Check(DecodeObjectMarkerCall(objectCall.data(), kObjectCallRva, kObjectFunctionRva, ProjectionSites{}) == 0,
          "without a bound projection there is nothing for the object-marker call to land on");
    for (int why = 0; why < static_cast<int>(ProjectionBind::Count); ++why)
        Check(ProjectionBindName(static_cast<ProjectionBind>(why))[0] != '?', "every refusal has a name for the log");
    Check(kRingFraction == 0.3f && kOffscreenFloor == 0.0001f, "the game's own ring numbers");
}

// The witness's arithmetic: the eye's own picture by its projection matrix agrees with the model by field of view.
void WitnessMath()
{
    const Camera camera = Head(-35.0f, 12.0f, 8.0f, 4.0f, 5.0f, 6.0f, kEyeFov);
    EyeRaster eye;
    eye.camera = camera;
    const float t = std::tan(kEyeFov * 0.5f), aspect = kWidth / kHeight;
    eye.xScale = 1.0f / (aspect * t);
    eye.yScale = 1.0f / t;
    eye.width = kWidth;
    eye.height = kHeight;
    for (float rightDeg : {-40.0f, -10.0f, 0.0f, 25.0f})
        for (float upDeg : {-30.0f, 0.0f, 15.0f})
        {
            float point[3], byMatrix[2], byFov[2];
            PointAt(camera, rightDeg, upDeg, 12.0f, point);
            Check(EyeRasterPixel(eye, point, byMatrix) && ProjectToRaster(camera, point, kWidth, kHeight, byFov) &&
                      Near(byMatrix[0], byFov[0], 0.05f) && Near(byMatrix[1], byFov[1], 0.05f),
                  "the eye's picture by its projection matrix is the model's by its field of view");
            const float expect = std::acos(1.0f / std::sqrt(1.0f + std::pow(std::tan(rightDeg * kDegToRad), 2.0f) +
                                                            std::pow(std::tan(upDeg * kDegToRad), 2.0f)));
            Check(Near(OffAxis(camera, point), expect, 1.0e-3f), "the angle off the eye's axis");
        }
    float out[2]{-1.0f, -1.0f};
    const float behind[3]{camera.position[0] - camera.forward[0], camera.position[1] - camera.forward[1],
                          camera.position[2] - camera.forward[2]};
    Check(!EyeRasterPixel(eye, behind, out) && out[0] == -1.0f, "a point behind the eye has no pixel in its picture");
    EyeRaster broken = eye;
    broken.xScale = 0.0f;
    float front[3];
    PointAt(camera, 0.0f, 0.0f, 3.0f, front);
    Check(!EyeRasterPixel(broken, front, out), "a raster without a scale has no pixel");
    broken = eye;
    broken.height = 0.0f;
    Check(!EyeRasterPixel(broken, front, out), "a raster without a size has no pixel");
    Check(OffAxis(camera, camera.position) == 0.0f, "the eye's own position is on its axis");
    for (int what = 0; what < static_cast<int>(Witness::Count); ++what)
        Check(WitnessName(static_cast<Witness>(what))[0] != '?', "every witness verdict has a name for the log");
}

// Where an eye drew something, as a direction (the arrow's witness): the inverse of the eye's picture, and the
// two eyes compared in the head's frame.
void ArrowDirections()
{
    const Camera head = Head(20.0f, -5.0f, 0.0f, 4.0f, 5.0f, 6.0f, kEyeFov);
    const float t = std::tan(kEyeFov * 0.5f), aspect = kWidth / kHeight, separation = 0.064f;
    EyeRaster eyes[2];
    for (int side = 0; side < 2; ++side)
    {
        eyes[side].camera = Eye(head, side == 0 ? -0.5f * separation : 0.5f * separation, 0.0f);
        eyes[side].xScale = 1.0f / (aspect * t);
        eyes[side].yScale = 1.0f / t;
        eyes[side].width = kWidth;
        eyes[side].height = kHeight;
    }
    for (float rightDeg : {-30.0f, -8.0f, 0.0f, 12.0f, 35.0f})
        for (float upDeg : {-25.0f, 0.0f, 20.0f})
            for (float depth : {2.0f, 30.0f})
            {
                float point[3], angles[2][2];
                PointAt(head, rightDeg, upDeg, depth, point);
                for (int side = 0; side < 2; ++side)
                {
                    float pixel[2], across = 0.0f, up = 0.0f, ray[3];
                    Check(EyeRasterPixel(eyes[side], point, pixel) &&
                              EyePixelTangents(eyes[side], pixel, across, up),
                          "a pixel the eye drew has a line of sight");
                    EyeRay(eyes[side].camera, across, up, ray);
                    float to[3]{point[0] - eyes[side].camera.position[0], point[1] - eyes[side].camera.position[1],
                                point[2] - eyes[side].camera.position[2]};
                    Normalize(to);
                    Check(Near(ray[0], to[0], 2.0e-4f) && Near(ray[1], to[1], 2.0e-4f) && Near(ray[2], to[2], 2.0e-4f),
                          "the line of sight through a point's pixel goes to the point");
                    Check(DirectionAngles(head, ray, angles[side][0], angles[side][1]),
                          "a line of sight has its two angles in the head's frame");
                }
                // A real point: the left eye sees it further to the right than the right eye does, by what the
                // eye separation subtends at its depth; and at the same height.
                const float x = depth * std::tan(rightDeg * kDegToRad);
                const float expect = std::atan((x + 0.5f * separation) / depth) - std::atan((x - 0.5f * separation) / depth);
                Check(Near(angles[0][0] - angles[1][0], expect, 5.0e-5f) && expect > 0.0f,
                      "left minus right, across, is the eye separation seen from the point's depth");
                Check(Near(angles[0][1], angles[1][1], 5.0e-5f), "and nothing up or down");
            }
    // The two eyes drawing the SAME direction (an arrow laid out about each eye's own axis, parallel eyes): no
    // difference at all, which is a point at infinity.
    {
        float pixel[2], across = 0.0f, up = 0.0f, ray[3], angles[2][2];
        for (int side = 0; side < 2; ++side)
        {
            float direction[3];
            PointAt(eyes[side].camera, 25.0f, -10.0f, 1.0f, direction);
            Check(EyeRasterPixel(eyes[side], direction, pixel) && EyePixelTangents(eyes[side], pixel, across, up),
                  "the same direction in each eye");
            EyeRay(eyes[side].camera, across, up, ray);
            Check(DirectionAngles(head, ray, angles[side][0], angles[side][1]), "in the head's frame");
        }
        Check(Near(angles[0][0], angles[1][0], 5.0e-5f) && Near(angles[0][1], angles[1][1], 5.0e-5f),
              "parallel eyes drawing one direction do not differ");
    }
    EyeRaster broken = eyes[0];
    broken.yScale = 0.0f;
    float pixel[2]{100.0f, 100.0f}, across = -7.0f, up = -7.0f;
    Check(!EyePixelTangents(broken, pixel, across, up) && across == -7.0f, "a raster without a scale has no line of sight");
    float behind[3]{-head.forward[0], -head.forward[1], -head.forward[2]};
    Check(!DirectionAngles(head, behind, across, up) && across == -7.0f, "a direction behind the head has no angles");
    Check(Near(kArcMinutesPerRadian, 3437.7468f, 1.0e-2f), "arc-minutes in a radian");
}

// ---- part 2: the off-screen ring -----------------------------------------------------------------------------------
// A real headset's eye fields, as the reference case: each eye 54 degrees outward, 40 inward, 44 up, 55 down.
void SessionEyes(EyeField eyes[2], float cantDeg = 0.0f)
{
    for (int eye = 0; eye < 2; ++eye)
    {
        const bool left = eye == 0;
        eyes[eye].angleLeft = -(left ? 54.0f : 40.0f) * kDegToRad;
        eyes[eye].angleRight = (left ? 40.0f : 54.0f) * kDegToRad;
        eyes[eye].angleUp = 44.0f * kDegToRad;
        eyes[eye].angleDown = -55.0f * kDegToRad;
        // Canted outward: the left eye turned to the left. OpenXR: +Y up, a positive angle about it turns -Z
        // (forward) toward -X (left).
        const float half = (left ? cantDeg : -cantDeg) * kDegToRad * 0.5f;
        eyes[eye].orientation[0] = 0.0f;
        eyes[eye].orientation[1] = std::sin(half);
        eyes[eye].orientation[2] = 0.0f;
        eyes[eye].orientation[3] = std::cos(half);
    }
}
// The game's own off-screen placement for one camera, from retail +0x2E160C..+0x2E1831: inside its ring the point
// itself; outside, the direction with both angles divided by the ratio. Returns the direction it projects and
// what it stores as the record's off-screen value.
float GamePlaces(const Camera& camera, const RingField& gameRing, const float point[3], float placed[3])
{
    float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
    if (!RingAngles(camera, point, yaw, pitch, &distance))
        return -1.0f;
    const float ratio = RingRatio(gameRing, yaw, pitch);
    const float scale = ratio > 1.0f ? ratio : 1.0f;
    yaw /= scale;
    pitch /= scale;
    float left[3];
    Cross(camera.up, camera.forward, left);
    for (int i = 0; i < 3; ++i)
        placed[i] = camera.position[i] + distance * (camera.forward[i] * std::cos(pitch) * std::cos(yaw) +
                                                     left[i] * std::cos(pitch) * std::sin(yaw) +
                                                     camera.up[i] * std::sin(pitch));
    return ratio > 1.0f ? OffscreenValue(gameRing, ratio) : 0.0f;
}
float AngleBetween(const Camera& from, const float a[3], const float b[3])
{
    float da[3], db[3];
    for (int i = 0; i < 3; ++i)
    {
        da[i] = a[i] - from.position[i];
        db[i] = b[i] - from.position[i];
    }
    float cross[3];
    Cross(da, db, cross);
    return std::atan2(std::sqrt(Dot(cross, cross)), Dot(da, db)) * kRadToDeg;
}

void SharedFields()
{
    EyeField eyes[2];
    SessionEyes(eyes);
    RingField shared;
    Check(SharedField(eyes, shared) && Near(shared.halfYaw * kRadToDeg, 40.0f, 0.01f) &&
              Near(shared.halfPitch * kRadToDeg, 44.0f, 0.01f),
          "the field both eyes see: the nose-side limit sideways (40), the smaller of up and down (44)");
    const RingField ring = HeadRing(shared);
    Check(Near(ring.halfYaw * kRadToDeg, 24.0f, 0.01f) && Near(ring.halfPitch * kRadToDeg, 26.4f, 0.01f),
          "the head's ring is six tenths of it: the game's own 0.3 of a whole field");
    Check(kHeadRingFraction == 2.0f * kRingFraction, "the fraction is the game's own, of the half field");
    // Canted outward by five degrees: each eye's nose-side limit moves five degrees toward the nose's far side,
    // so the shared field narrows by five; up and down are unchanged to a hair.
    SessionEyes(eyes, 5.0f);
    Check(SharedField(eyes, shared) && Near(shared.halfYaw * kRadToDeg, 35.0f, 0.05f) &&
              Near(shared.halfPitch * kRadToDeg, 44.0f, 0.3f),
          "canted eyes share a narrower field, measured about the head's axis");
    // Canted inward (toed in): the outward limits come in instead.
    SessionEyes(eyes, -20.0f);
    Check(SharedField(eyes, shared) && Near(shared.halfYaw * kRadToDeg, 34.0f, 0.05f),
          "toed-in eyes: the outward limit, turned in, is the smaller");
    // Unusable eyes: nothing is claimed and the output is untouched.
    RingField untouched;
    untouched.halfYaw = -1.0f;
    for (int which = 0; which < 7; ++which)
    {
        SessionEyes(eyes);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        if (which == 0) eyes[1].angleLeft = nan;
        if (which == 1) eyes[0].angleRight = -0.1f;      // a right limit to the left of the axis
        if (which == 2) eyes[0].angleUp = 0.0f;
        if (which == 3) eyes[1].angleDown = 0.2f;
        if (which == 4) eyes[0].orientation[3] = nan;
        if (which == 5) eyes[1].orientation[1] = eyes[1].orientation[3] = 0.0f;  // no rotation at all
        if (which == 6) eyes[0].angleLeft = -1.6f;       // past a right angle
        Check(!SharedField(eyes, untouched) && untouched.halfYaw == -1.0f, "an unusable eye field gives no ring");
    }
    // The game's own ring for the same eye is never the smaller one: the head's ring fits inside it.
    const RingField game = GameRing(kEyeFov, 920.0f / 690.0f);
    Check(Near(game.halfPitch * kRadToDeg, 33.0f, 0.01f) && Near(game.halfYaw * kRadToDeg, 44.0f, 0.01f),
          "the game's ring with the eye's field of view: 33 degrees up and down, 44 sideways (4:3 canvas)");
    Check(ring.halfYaw < game.halfYaw && ring.halfPitch < game.halfPitch, "the head's ring is inside the game's");
}

void RingVerdicts()
{
    const Camera head = Head(25.0f, 0.0f, 0.0f, 3.0f, 4.0f, 1.0f, kEyeFov);
    EyeField eyes[2];
    SessionEyes(eyes);
    RingField shared;
    SharedField(eyes, shared);
    const RingField ring = HeadRing(shared);
    float inside[3], outside[3];
    PointAt(head, 5.0f, 5.0f, 10.0f, inside);
    PointAt(head, 60.0f, 0.0f, 10.0f, outside);
    RingMove untouched;
    untouched.offscreen = -7.0f;
    // Every reason to leave the game's own projection alone, in the order they are asked.
    Check(RingPoint(false, true, 0, true, true, true, false, head, true, ring, outside, untouched) == Ring::SwitchOff, "switch off");
    Check(RingPoint(true, false, 0, true, true, true, false, head, true, ring, outside, untouched) == Ring::NoEyeScope,
          "not inside an eye's render");
    Check(RingPoint(true, true, 1, true, true, true, false, head, true, ring, outside, untouched) == Ring::NotLocalUser &&
              RingPoint(true, true, -1, true, true, true, false, head, true, ring, outside, untouched) == Ring::NotLocalUser,
          "another user's HUD");
    Check(RingPoint(true, true, 0, false, true, true, false, head, true, ring, outside, untouched) == Ring::NotValid,
          "a record the game will not project");
    Check(RingPoint(true, true, 0, true, false, true, false, head, true, ring, outside, untouched) == Ring::Plain &&
              RingPoint(true, true, 0, true, false, false, true, head, true, ring, outside, untouched) == Ring::Plain,
          "a caller that asked for no ring gets its point as it is, however far off");
    // WHOSE CALL. Only the navpoint list's and the object-anchored markers' (the detour passes true for either),
    // and only with pitch in the camera's own frame.
    Check(RingPoint(true, true, 0, true, true, false, false, head, true, ring, outside, untouched) ==
              Ring::OtherCaller,
          "a ring asked for by any other caller (an aim point, a name tag) is the game's own");
    Check(RingPoint(true, true, 0, true, true, false, true, head, true, ring, outside, untouched) ==
              Ring::OtherCaller,
          "the damage indicators (another caller, world pitch) are the game's own, counted as another caller");
    Check(RingPoint(true, true, 0, true, true, true, true, head, true, ring, outside, untouched) == Ring::WorldPitch,
          "the navpoint list asking for a world-pitch ring would be the game's own too");
    Check(RingPoint(true, true, 0, true, true, false, false, head, true, ring, inside, untouched) ==
              Ring::OtherCaller,
          "another caller is left alone whether its point is inside the head's ring or not");
    Check(RingPoint(true, true, 0, true, true, true, false, head, false, ring, outside, untouched) == Ring::NoField &&
              RingPoint(true, true, 0, true, true, true, false, head, true, RingField{}, outside, untouched) == Ring::NoField,
          "no eye field: the game's own ring");
    Camera broken = head;
    broken.up[0] = broken.forward[0];
    broken.up[1] = broken.forward[1];
    broken.up[2] = broken.forward[2];
    Check(RingPoint(true, true, 0, true, true, true, false, broken, true, ring, outside, untouched) == Ring::NoHead,
          "a head whose up lies along its forward");
    broken = head;
    broken.position[1] = std::numeric_limits<float>::infinity();
    Check(RingPoint(true, true, 0, true, true, true, false, broken, true, ring, outside, untouched) == Ring::NoHead,
          "a head that is not finite");
    const float notFinite[3]{outside[0], std::numeric_limits<float>::quiet_NaN(), outside[2]};
    Check(RingPoint(true, true, 0, true, true, true, false, head, true, ring, notFinite, untouched) == Ring::NoHead,
          "a point that is not finite");
    Check(RingPoint(true, true, 0, true, true, true, false, head, true, ring, head.position, untouched) == Ring::AtHead,
          "a point at the head has no direction");
    Check(RingPoint(true, true, 0, true, true, true, false, head, true, ring, inside, untouched) == Ring::OnTarget,
          "a point inside the ring is drawn on itself");
    Check(untouched.offscreen == -7.0f && untouched.point[0] == 0.0f, "nothing but a move fills the stand-in");
    for (int what = 0; what < static_cast<int>(Ring::Count); ++what)
        Check(RingName(static_cast<Ring>(what))[0] != '?', "every verdict has a name for the log");
    // On the ring's edge, just inside and just outside.
    float edge[3];
    PointAt(head, 23.9f, 0.0f, 10.0f, edge);
    Check(RingPoint(true, true, 0, true, true, true, false, head, true, ring, edge, untouched) == Ring::OnTarget, "just inside");
    PointAt(head, 24.01f, 0.0f, 10.0f, edge);
    RingMove move;
    Check(RingPoint(true, true, 0, true, true, true, false, head, true, ring, edge, move) == Ring::Moved, "just outside");
    Check(move.offscreen == kOffscreenFloor, "just outside: the game's own floor for the off-screen value");
}

void RingGeometry()
{
    EyeField eyes[2];
    RingField shared;
    for (float cant : {0.0f, 5.0f})
    {
        SessionEyes(eyes, cant);
        Check(SharedField(eyes, shared), "the measured eyes share a field");
        const RingField ring = HeadRing(shared);
        const RingField gameRing = GameRing(kEyeFov, 920.0f / 690.0f);
        for (float headYaw : {0.0f, 130.0f})
            for (float headPitch : {0.0f, -20.0f})
                for (float headRoll : {0.0f, 30.0f})
                {
                    const Camera head = Head(headYaw, headPitch, headRoll, 1.0f, 2.0f, 1.5f, kEyeFov);
                    float headLeft[3];
                    Cross(head.up, head.forward, headLeft);
                    // Targets all the way round the head and above and below it, near and far.
                    for (float yawDeg = -180.0f; yawDeg < 180.0f; yawDeg += 22.5f)
                        for (float pitchDeg : {-70.0f, -30.0f, 0.0f, 30.0f, 70.0f})
                            for (float distance : {0.2f, 3.0f, 60.0f})
                            {
                                const float y = yawDeg * kDegToRad, p = pitchDeg * kDegToRad;
                                float target[3];
                                for (int i = 0; i < 3; ++i)
                                    target[i] = head.position[i] +
                                                distance * (head.forward[i] * std::cos(p) * std::cos(y) +
                                                            headLeft[i] * std::cos(p) * std::sin(y) +
                                                            head.up[i] * std::sin(p));
                                RingMove move;
                                const Ring what = RingPoint(true, true, 0, true, true, true, false, head, true, ring, target, move);
                                const float ratio = RingRatio(ring, y, p);
                                if (ratio <= 1.0f)
                                {
                                    Check(what == Ring::OnTarget, "a target inside the head's ring is left on itself");
                                    continue;
                                }
                                Check(what == Ring::Moved, "a target outside the head's ring is brought onto it");
                                // The stand-in: on the ring, in the target's own direction from the centre, at the
                                // target's distance (or the comfortable minimum).
                                float standYaw = 0.0f, standPitch = 0.0f, standDistance = 0.0f;
                                Check(RingAngles(head, move.point, standYaw, standPitch, &standDistance),
                                      "the stand-in has a direction");
                                Check(Near(RingRatio(ring, standYaw, standPitch), 1.0f, 2.0e-3f),
                                      "the stand-in is on the head's ring");
                                // (A target exactly behind has no side: the float sine of pi is not zero, so
                                // one side or the other is chosen by the last bit and the check has no meaning.)
                                Check(std::fabs(std::fabs(y) - kPi) < 1.0e-4f ||
                                          Near(standYaw * p, standPitch * y, 2.0e-3f),
                                      "on the ring in the target's own direction from the head's axis");
                                Check((standYaw >= 0.0f) == (y >= 0.0f) || std::fabs(y) < 1.0e-4f ||
                                          std::fabs(std::fabs(y) - kPi) < 1.0e-4f,
                                      "a target to the left stays to the left, a target to the right to the right");
                                Check(Near(standDistance, distance > kMinRingDistance ? distance : kMinRingDistance,
                                           distance * 1.0e-3f + 1.0e-3f),
                                      "at the target's own distance, never nearer than the minimum");
                                Check(Near(move.offscreen, OffscreenValue(ring, ratio), 1.0e-5f) &&
                                          move.offscreen >= kOffscreenFloor,
                                      "the off-screen value is the game's own formula on the head's ring");
                                // BOTH EYES: each eye's own camera hands the game the same stand-in, the game finds
                                // it inside its own ring, draws it where it stands, and reports it on screen.
                                float seen[2][3];
                                for (int side = -1; side <= 1; side += 2)
                                {
                                    const Camera eye = Eye(head, side * 0.032f, side * cant);
                                    float placed[3];
                                    const float gameValue = GamePlaces(eye, gameRing, move.point, placed);
                                    Check(gameValue == 0.0f, "the game does not clamp the stand-in again");
                                    Check(AngleBetween(eye, placed, move.point) < 0.01f,
                                          "each eye draws the stand-in where it stands");
                                    for (int i = 0; i < 3; ++i)
                                        seen[side < 0 ? 0 : 1][i] = placed[i];
                                }
                                Check(std::fabs(seen[0][0] - seen[1][0]) + std::fabs(seen[0][1] - seen[1][1]) +
                                              std::fabs(seen[0][2] - seen[1][2]) <
                                          1.0e-3f * (1.0f + standDistance),
                                      "both eyes are handed one point in the world: one arrow, not two");
                            }
                }
    }
    // The control arm: without the stand-in, the game clamps per eye about each eye's own axis. With canted eyes
    // the two eyes then draw the same arrow in two different directions.
    SessionEyes(eyes, 5.0f);
    const Camera head = Head(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kEyeFov);
    const RingField gameRing = GameRing(kEyeFov, 920.0f / 690.0f);
    float target[3];
    PointAt(head, 80.0f, 0.0f, 20.0f, target);
    float perEye[2][3];
    for (int side = -1; side <= 1; side += 2)
    {
        const Camera eye = Eye(head, side * 0.032f, side * 5.0f);
        Check(GamePlaces(eye, gameRing, target, perEye[side < 0 ? 0 : 1]) > 0.0f, "the game's own ring clamps it");
    }
    Check(AngleBetween(head, perEye[0], perEye[1]) > 5.0f,
          "stock, canted eyes: the same arrow in two directions, ten degrees apart");
}

void RingNumbers()
{
    // The game's formula, as read: half-width / pi * (ratio - 1), floored.
    RingField ring;
    ring.halfYaw = 24.0f * kDegToRad;
    ring.halfPitch = 26.4f * kDegToRad;
    Check(Near(OffscreenValue(ring, 2.0f), ring.halfYaw / kPi, 1.0e-6f), "twice as far out as the ring");
    Check(OffscreenValue(ring, 1.0f) == kOffscreenFloor && OffscreenValue(ring, 1.0000001f) == kOffscreenFloor,
          "on the ring: the floor, never zero (zero means drawn on the point)");
    Check(Near(RingRatio(ring, ring.halfYaw, 0.0f), 1.0f, 1.0e-6f) &&
              Near(RingRatio(ring, 0.0f, -ring.halfPitch), 1.0f, 1.0e-6f) &&
              Near(RingRatio(ring, ring.halfYaw, ring.halfPitch), std::sqrt(2.0f), 1.0e-6f),
          "the ring is an ellipse in the two angles");
    // The angles, the game's way: yaw positive to the left, pitch positive up.
    const Camera camera = Head(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kEyeFov);
    float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
    const float leftUp[3]{1.0f, 1.0f, 1.0f};  // +X forward, +Y left, +Z up
    Check(RingAngles(camera, leftUp, yaw, pitch, &distance) && Near(yaw, kPi / 4.0f) &&
              Near(pitch, std::atan(1.0f / std::sqrt(2.0f))) && Near(distance, std::sqrt(3.0f)),
          "a point ahead, to the left and above: yaw 45 degrees left, pitch up");
    const float behind[3]{-1.0f, -0.001f, 0.0f};
    Check(RingAngles(camera, behind, yaw, pitch) && yaw < -3.0f, "a point behind and a hair to the right: yaw near -180");
    Check(!RingAngles(camera, camera.position, yaw, pitch), "the camera's own position has no angles");
    Check(Near(kMinRingDistance * 3.048f, 2.0f, 1.0e-4f), "the stand-in is never nearer than two metres");
    Camera head = Head(10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f, kEyeFov);
    Check(HeadUsable(head), "a head turned, nodded and leaned is a unit perpendicular pair");
    head.forward[0] *= 1.5f;
    Check(!HeadUsable(head), "a forward that is not unit length is not a head");
}
} // namespace

int main()
{
    Sites();
    WitnessMath();
    ArrowDirections();
    SharedFields();
    RingVerdicts();
    RingGeometry();
    RingNumbers();
    FieldOfView();
    Magnified();
    Projection();
    OnTheTarget();
    HeadTurned();
    BothEyes();
    std::printf("Reach navigation markers: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
