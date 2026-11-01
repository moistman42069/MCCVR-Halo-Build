// REACH: NAVIGATION MARKERS ARE PROJECTED WITH THE EYE'S FIELD OF VIEW, the pure half (no game, engine or OpenXR
// includes). The write-up is docs/REACH-NAV-MARKERS-2026-10-07.md; the runtime half is
// src/dll/reach_nav_markers.inl.
//
// Two kinds of statement are kept apart below. READ STATICALLY: from HREK and the retail image, never run.
// RUN IN A GAME: measured on a simulated OpenXR runtime with ONE marker (the King of the Hill hill marker), on
// foot. Objective waypoints of other kinds, marked players and name tags are not measured.
//
// WHAT REACH DOES (read statically: HREK first, then matched in retail).
// HREK reach_tag_test.exe, chud_draw.cpp: ONE function turns a world point into a place on the HUD (+0x92D980;
// retail haloreach.dll +0x2E1430, eleven direct callers). By its callers, the world-anchored elements reach it:
// the targets a user's HUD tracks, the object / player / scripted-object anchors, the flag markers, the navpoint
// list, the damage arrows. It does not use the view being drawn. It BUILDS a camera of its own:
//     camera     = render_camera_from_observer_camera(observer_get_camera(user))   ; position, forward, up and
//                                                                                 ; the vertical field of view
//     projection = render_projection_from_render_camera(camera, the user's window)
//     pixel      = render_camera_view_to_screen(projection, point - camera.position)
// and the widget is then drawn at that pixel, in the same pass that draws the rest of the HUD over the eye.
//
// In this mod the eye is rasterised from the HEAD with a field of view that covers the headset's (the compact
// camera's +0x28, written by ReachApplyEyeOffset). The existing observer-camera hook hands the marker projection
// the eye's position, forward and up at its call site (the "CHUD site"); it left the FIELD OF VIEW the game's
// own, about half the eye's. A marker is then right only on the eye's axis and is pushed outward everywhere
// else by tan(eye half-angle) / tan(game half-angle).
//   RUN IN A GAME, switch off: eye 100.00 deg, game 57.89 deg, ratio 2.155. The marker's pixel was 2.155 times
//   its world point's offset from the raster's centre in x and in y on every sample, both eyes: 311 px off at
//   9.6 deg, 442-461 px at about 14 deg, 732-745 px at about 22 deg, of a 5242 x 3780 raster; from about 26 deg
//   the game had it on its own off-screen ring. In the composited left eye (840 x 880) the glyph sat 76 px from
//   its world point at 9.6 deg and 176 px at 21.8 deg.
//   The error does NOT depend on aim: with the head still, sweeping the gun 45 deg either way moved the stock
//   marker by 0.1 px. It is the head-angle error above.
//
// Halo 3 (read statically, retail only): its marker projection (halo3.dll +0x2F2424) builds no camera. It reads
// the CURRENT VIEW's own rasteriser camera and projection, the pair this mod rebuilds per eye.
//
// THE SEAM (part 1): at the CHUD site the camera is also given the eye's vertical field of
// view. Read statically: Reach derives its horizontal scale from that one angle and the window's aspect, and
// rasterises a frustum that is symmetric about the camera's axis in both directions (a headset's asymmetric or
// canted eye is covered by a symmetric frustum about the eye's own axis and cropped at submission), so with the
// same position, forward, up and vertical field of view the game should build, for the marker, the projection
// the eye was rasterised with.
//   RUN IN A GAME, switch on: the projection's pixel equalled this header's model of the eye's raster on every
//   compared sample (0.00 px, both eyes, 8.3 to 31.1 deg). That check shares its inputs with the thing checked:
//   it shows the copy took effect, not that the model is the picture. Independently, in the composited LEFT eye
//   the glyph's tip was within 0.8 px in x and 1.6-2.1 px above its world point's predicted pixel at 0, 10, 20
//   and 30 deg to either side (about the same 2 px is there with the switch off, against the stock pixel, and
//   at HUD height 0: it is not the error corrected here, and its cause is unknown).
//   NOT MEASURED: the marker against the hill ITSELF; the right eye's composited image; depth, lag and
//   reprojection; a real headset; an asymmetric or canted eye; any other marker.
//
// THE HUD HEIGHT SHIFT DOES NOT REACH THIS MARKER, so world-anchored HUD bases are not exempted from it here. With
// the height at +16 and at 0 the hill marker stayed within 0.5 px of the same place against its world point while
// the radar and the score panel moved 8 px. Which markers such an exemption would act on is not known; see the
// write-up for the anchor types read from HREK.
//
// Nothing here is read by a shot, an aim or the network (by reading the code: the camera written is the marker
// projection's own stack copy).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace reach_nav_markers
{
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

// ---- the camera's vertical field of view -----------------------------------------------------------------------
// ADDR-HYGIENE: structural + guarded. The render camera's vertical field of view is at +0x28: the game's own
// converter stores it there (retail +0x287E6E / +0x287F1B `movss [rbx + 0x28], xmm0`, the copy block
// reach_render_logic.h already documents), the frustum builder reads it from there (+0x2882CF
// `movss xmm0, [r8 + 0x28]` then tanf), the marker projection reads the same field of the camera it built for
// its off-screen ring (+0x2E172B `movss xmm1, [rbp + 0xA8]`, the camera being at rbp + 0x80), and this mod's own
// per-eye camera writes the eye's angle there (ReachApplyEyeOffset). tools/verify-reach-nav-markers.py decodes
// all three game sites from the pinned module. The observer-camera hook this rides on is installed only on that
// pinned image. At every use both values must also lie in the range the game itself asserts for this field
// (HREK render_camera.cpp: > 0.0001 and < 3.14149), or nothing is written.
inline constexpr unsigned kCameraVerticalFovOffset = 0x28;
inline constexpr float kMinVerticalFov = 0.0001f;
inline constexpr float kMaxVerticalFov = 3.14149f;

inline bool FovPlausible(float verticalFov)
{
    return std::isfinite(verticalFov) && verticalFov > kMinVerticalFov && verticalFov < kMaxVerticalFov;
}

// What the CHUD site did with the camera's field of view. Identity only: never refused for being large or small
// inside the game's own range.
enum class Fov : uint8_t
{
    Used,       // the camera now carries the eye's vertical field of view
    SwitchOff,  // the player's switch is off: the game's own field of view stands
    NoGameFov,  // the camera's field is not a field of view (not finite, or outside the game's own range)
    NoEyeFov,   // the eye's camera has no usable field of view
    Count
};
inline const char* FovName(Fov use)
{
    switch (use)
    {
    case Fov::Used: return "used";
    case Fov::SwitchOff: return "switch off";
    case Fov::NoGameFov: return "the game's camera has no field of view where it should";
    case Fov::NoEyeFov: return "the eye's camera has no field of view";
    default: return "?";
    }
}
// `gameFov`: what the game's converter left in the marker camera. `eyeFov`: the same field of the camera this
// eye was rasterised with. Used = `out` is the eye's; anything else leaves `out` untouched.
inline Fov EyeFov(bool switchOn, float gameFov, float eyeFov, float& out)
{
    if (!switchOn)
        return Fov::SwitchOff;
    if (!FovPlausible(gameFov))
        return Fov::NoGameFov;
    if (!FovPlausible(eyeFov))
        return Fov::NoEyeFov;
    out = eyeFov;
    return Fov::Used;
}

// HOW FAR OFF THE STOCK PLACEMENT WAS. A point at `offAxis` radians from the eye's axis belongs at
// tan(offAxis) / tan(eye / 2) of the raster's half extent; projected with the game's angle it was drawn at
// tan(offAxis) / tan(game / 2). The ratio of the two is the same everywhere: the magnification.
inline float Magnification(float gameFov, float eyeFov)
{
    if (!FovPlausible(gameFov) || !FovPlausible(eyeFov))
        return 1.0f;
    return std::tan(eyeFov * 0.5f) / std::tan(gameFov * 0.5f);
}
// Where a marker whose target is `offAxis` radians off the eye's axis was SEEN, in radians off that axis.
inline float StockSeenOffAxis(float offAxis, float gameFov, float eyeFov)
{
    return std::atan(std::tan(offAxis) * Magnification(gameFov, eyeFov));
}
// The same error in pixels of a raster `rasterExtentPx` high (or wide, with the horizontal half-angles).
inline float StockErrorPixels(float offAxis, float gameFov, float eyeFov, float rasterExtentPx)
{
    if (!FovPlausible(eyeFov))
        return 0.0f;
    return std::tan(offAxis) * (Magnification(gameFov, eyeFov) - 1.0f) / std::tan(eyeFov * 0.5f) * rasterExtentPx *
           0.5f;
}

// ---- the game's projection, as a model for the tests -----------------------------------------------------------
// retail +0x2881CC (frustum from a camera, no bounds given: -1..1 in both axes), +0x2884BC (the projection) and
// +0x288C10 (a view-space vector to a pixel), read from the pinned module:
//     t = tan(verticalFov / 2);   aspect = window width / window height
//     x = 0.5 + 0.5 * (d . right) / ((d . forward) * aspect * t)         ; of the window's width
//     y = 0.5 - 0.5 * (d . up)    / ((d . forward) * t)                  ; of its height, down the screen
// with d = point - camera.position and right = forward x up. A point at or behind the camera's plane has no pixel.
struct Camera
{
    float position[3]{};
    float forward[3]{};
    float up[3]{};
    float verticalFov = 0.0f;
};
inline float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void Cross(const float a[3], const float b[3], float out[3])
{
    const float x = a[1] * b[2] - a[2] * b[1];
    const float y = a[2] * b[0] - a[0] * b[2];
    const float z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}
// False (out untouched) when the point is not in front of the camera or an input is not usable.
inline bool ProjectToRaster(const Camera& camera, const float point[3], float width, float height, float out[2])
{
    if (!FovPlausible(camera.verticalFov) || !(width > 0.0f) || !(height > 0.0f))
        return false;
    const float d[3]{point[0] - camera.position[0], point[1] - camera.position[1], point[2] - camera.position[2]};
    float right[3];
    Cross(camera.forward, camera.up, right);
    const float depth = Dot(d, camera.forward);
    if (!(depth > 0.0001f))
        return false;
    const float t = std::tan(camera.verticalFov * 0.5f);
    const float aspect = width / height;
    const float x = (0.5f + 0.5f * Dot(d, right) / (depth * aspect * t)) * width;
    const float y = (0.5f - 0.5f * Dot(d, camera.up) / (depth * t)) * height;
    if (!std::isfinite(x) || !std::isfinite(y))
        return false;
    out[0] = x;
    out[1] = y;
    return true;
}

// ====================================================================================================================
// THE MARKER PROJECTION ITSELF (HREK reach_tag_test.exe +0x92D980, chud_draw.cpp; retail +0x2E1430)
//
//     void project(record*, int user, bool ring, bool behind, bool worldPitch)
// The record: a valid byte at +0, the world point at +4, the pixel it lands on (relative to the user's window) at
// +0x10, the same place in the HUD's virtual canvas at +0x18, and at +0x20 how far outside the game's off-screen
// ring the point is (0 = drawn on the point itself). tools/audit_caller_area.py haloreach.dll 2E1430:9:v is
// clean: the function reads no caller slot past its fifth argument (entry+0x28) and none of its eleven native
// callers keeps a volatile register across the call or reads a return value.
//
// ADDR-HYGIENE: resolved. No address ships. Five patterns, each matched exactly once in the image, all inside the
// one function the first begins; the call the second carries must land on the function the observer-camera hook
// hooks and return to the address that hook knows as its CHUD site (so the function bound here is the one whose
// camera part 1 corrects); the vertical field of view the ring reads must be the camera's +0x28 (the ring's
// displacement minus the camera buffer's); the ring's fraction must be 0.3 and its two other constants pi and
// 0.0001 (ValueAgreement); the record's offsets are read from the matched instructions and must follow one
// another the way the record does. A sixth pattern, matched exactly once as well, is the navpoint list's call of
// this function (the one caller the stand-in acts for): its call must land on the head matched here, from
// another function; the address it returns to is what the detour compares its own return address with. Any link
// failing binds nothing.
// ====================================================================================================================

// The head (retail +0x2E1430): the frame, the cookie, the HUD's window count and index, the record in rsi, the two
// bool arguments spilled (r9b, r8b), the user in r12, the record's valid byte tested at [rcx].
inline constexpr const char* kProjectionHeadPattern =
    "48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 28 FE FF FF 48 81 EC A0 02 00 00 "
    "0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF "
    "44 0F 29 98 68 FF FF FF 44 0F 29 A0 58 FF FF FF 44 0F 29 B0 48 FF FF FF "
    "48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 10 01 00 00 8B 3D ?? ?? ?? ?? 45 33 ED 48 8B F1 "
    "8B 1D ?? ?? ?? ?? 44 88 4C 24 31 44 88 44 24 30 4C 63 E2 44 38 29 0F 84";
inline constexpr size_t kProjectionHeadBytes = 135;
// The camera it builds (retail +0x2E14FC): the user's observer camera (engine TLS slot, +0x154, 0x410 apart)
// converted into the camera at rbp + CAMERA; that camera's bounds; the projection; then the record's point minus
// the camera's position.
//   mov ecx, SLOT / imul rdx, r12, STRIDE / mov r9, [rax + r8*8] / mov rax, [r9 + rcx] / lea rcx, [rbp + CAMERA]
//   add rax, BASE / add rdx, rax / call CONVERTER / lea rcx, [rbp + CAMERA] / call / lea r9, [rbp + CAMERA+0x40]
//   mov edx, edi / lea r8, [rbp + CAMERA+0x38] / mov ecx, ebx / call / xorps xmm3, xmm3 / lea rcx, [rbp + CAMERA]
//   xor edx, edx / lea r8, [rbp - 0x60] / call PROJECTION / movss xmm0, [rsi + POINT] / lea rcx, [rsp + 0x50]
//   subss xmm0, [rbp + CAMERA] / movss xmm1, [rsi + POINT+4] / subss xmm1, [rbp + CAMERA+4]
inline constexpr const char* kProjectionCameraPattern =
    "B9 88 06 00 00 49 69 D4 10 04 00 00 4E 8B 0C C0 49 8B 04 09 48 8D 8D 80 00 00 00 "
    "48 05 54 01 00 00 48 03 D0 E8 ?? ?? ?? ?? 48 8D 8D 80 00 00 00 E8 ?? ?? ?? ?? "
    "4C 8D 8D C0 00 00 00 8B D7 4C 8D 85 B8 00 00 00 8B CB E8 ?? ?? ?? ?? 0F 57 DB "
    "48 8D 8D 80 00 00 00 33 D2 4C 8D 45 A0 E8 ?? ?? ?? ?? F3 0F 10 46 04 48 8D 4C 24 50 "
    "F3 0F 5C 85 80 00 00 00 F3 0F 10 4E 08 F3 0F 5C 8D 84 00 00 00";
inline constexpr size_t kProjectionCameraBytes = 128;
inline constexpr size_t kCameraBufferDisp = 23, kCameraConverterCall = 36, kCameraConverterReturn = 41,
                        kCameraPointX = 101, kCameraPositionXDisp = 111, kCameraPointY = 119,
                        kCameraPositionYDisp = 124;
// The off-screen ring (retail +0x2E172B): the camera's vertical field of view times a fraction is the ring's
// half-height in radians, and that times the HUD's width over its height its half-width.
//   movss xmm1, [rbp + CAMERA+FOV] / movaps xmm2, xmm11 / mulss xmm1, [rip + FRACTION] / movaps xmm7, xmm1 /
//   divss xmm2, xmm1 / mulss xmm7, xmm12 / movaps xmm1, xmm8 / mulss xmm2, xmm2 / divss xmm7, xmm14
inline constexpr const char* kProjectionRingPattern =
    "F3 0F 10 8D ?? ?? ?? ?? 41 0F 28 D3 F3 0F 59 0D ?? ?? ?? ?? 0F 28 F9 F3 0F 5E D1 F3 41 0F 59 FC "
    "41 0F 28 C8 F3 0F 59 D2 F3 41 0F 5E FE";
inline constexpr size_t kProjectionRingBytes = 45;
inline constexpr size_t kRingFovDisp = 4, kRingFractionDisp = 16, kRingFractionNext = 20;
// How far outside the ring (retail +0x2E1817): half-width / pi * (ratio - 1), at least 0.0001, into the record.
//   divss xmm7, [rip + PI] / subss xmm12, xmm10 / mulss xmm7, xmm12 / maxss xmm7, [rip + FLOOR] /
//   movss [rsi + OFFSCREEN], xmm7
inline constexpr const char* kProjectionOffscreenPattern =
    "F3 0F 5E 3D ?? ?? ?? ?? F3 45 0F 5C E2 F3 41 0F 59 FC F3 0F 5F 3D ?? ?? ?? ?? F3 0F 11 7E 20";
inline constexpr size_t kProjectionOffscreenBytes = 31;
inline constexpr size_t kOffscreenPiDisp = 4, kOffscreenPiNext = 8, kOffscreenFloorDisp = 22, kOffscreenFloorNext = 26,
                        kOffscreenRecord = 30;
// The pixel (retail +0x2E15BC): lea r14, [rsi + PIXEL] / movss xmm10, [rip + 1.0] / mov [r14], r13d /
// mov [rsi + PIXEL+4], r13d -- both cleared before the point is projected into them.
inline constexpr const char* kProjectionPixelPattern = "4C 8D 76 10 F3 44 0F 10 15 ?? ?? ?? ?? 45 89 2E 44 89 6E 14";
inline constexpr size_t kProjectionPixelBytes = 20;
inline constexpr size_t kPixelX = 3, kPixelY = 19;
// THE NAVPOINT LIST'S CALL (HREK +0x92874F in +0x9285A0, chud_draw.cpp; retail +0x2E3D51 in +0x2E3BAC): the one
// caller the stand-in acts for. Per navpoint, up to twenty a user: the record in rcx, the user from the HUD's own
// state, no "behind", pitch in the camera's own frame (the fifth argument is dl, cleared before the loop), and
// the ring when the navpoint asks for it.
//   mov [rsp + 0x20], dl / xor r9d, r9d / mov rcx, rsi / mov edx, [rbx + USER] / test dil, dil / jne +3 /
//   xor r8d, r8d / call PROJECTION
// WHY ONLY THIS CALLER. The projection has eleven native callers (read statically; the names are HREK's). Besides
// this one: five per-user AIM POINTS (the anchor producer copies them for anchors 21 weapon target, 22 ghost
// reticule, 23 hologram target, 24 airstrike target and 15 tracked target; all but the hologram ask for the
// ring); the DAMAGE FLASHES and the DAMAGE ARROWS (always the ring, with pitch measured against the world, which
// the stand-in's geometry is not); the anchor producer's object paths (name tags, fireteam and objective
// objects, by their anchors' names) and the HUD's own state build. An aim point held 24 degrees from the head
// would be drawn away from where the weapon points, and none of the others has been measured in a game. They
// keep the game's own ring, counted by reason. RUN IN A GAME: the hill marker of King of the Hill comes through
// this call.
inline constexpr const char* kNavpointCallPattern =
    "88 54 24 20 45 33 C9 48 8B CE 8B 93 ?? ?? ?? ?? 40 84 FF 75 03 45 33 C0 E8 ?? ?? ?? ??";
inline constexpr size_t kNavpointCallBytes = 29;
inline constexpr size_t kNavpointCall = 24, kNavpointReturn = 29;
// THE OBJECT MARKERS' CALL (retail +0x2DAECE in the wrapper +0x2DAE98, which the HUD's anchor producer +0x2DB54C
// calls for a widget anchored on an object: by the anchors' HREK names, 16 tracking object, 25 player, 26
// scripted object, 40-49 fireteam and objective objects). The wrapper builds a record from the object's point
// and calls the projection with no "behind", pitch in the camera's own frame (the fifth argument is the
// constant 0) and the ring when the widget asks for it:
//   mov rcx, r10 / xor r9d, r9d / mov byte [rsp + 0x20], 0 / test r8b, r8b / je +5 / mov r8b, 1 / jmp +3 /
//   xor r8d, r8d / call PROJECTION
// MEASURED IN A HEADSET: the off-screen arrows a player sees in ordinary play come through THIS call (about 120
// ring projections a second, none from the navpoint list). Left to the game they stay on its own ring, outside
// what the player can see, and the ring setting has no effect on them. The same geometry as the navpoint list's
// (world pitch 0), so the stand-in acts for it too. Optional: if this pattern is missing or ambiguous the
// navpoint list keeps its stand-in and these keep the game's ring.
inline constexpr const char* kObjectMarkerCallPattern =
    "49 8B CA 45 33 C9 C6 44 24 20 00 45 84 C0 74 05 41 B0 01 EB 03 45 33 C0 E8 ?? ?? ?? ??";
inline constexpr size_t kObjectMarkerCallBytes = 29;
inline constexpr size_t kObjectMarkerCall = 24, kObjectMarkerReturn = 29;

// The game's own numbers for the ring: 0.3 of the vertical field of view, and the floor of "outside".
inline constexpr float kRingFraction = 0.3f;
inline constexpr float kOffscreenFloor = 0.0001f;

inline int32_t ReadI32(const uint8_t* at)
{
    int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
inline float ReadF32(const uint8_t* at)
{
    float value = 0.0f;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

struct ProjectionSites
{
    int64_t headRva = 0;             // the projection: the hook
    int64_t converterRva = 0;        // what its camera call lands on: render_camera_from_observer_camera
    int64_t converterReturnRva = 0;  // and where that call returns: the observer-camera hook's CHUD site
    int64_t fractionRva = 0, piRva = 0, floorRva = 0;  // the ring's three constants
    int64_t navpointReturnRva = 0;   // where the navpoint list's call of it returns: the stand-in's one caller
    uint32_t cameraFovOffset = 0;    // the vertical field of view inside the camera it builds
    uint32_t recordPoint = 0;        // the record's world point
    uint32_t recordPixel = 0;        // its pixel, relative to the user's window
    uint32_t recordOffscreen = 0;    // how far outside the ring
};
enum class ProjectionBind : uint8_t
{
    Bound = 0,
    HeadMissing,       // the projection's head: no match, more than one, or not a function's beginning
    CameraMissing,     // the camera it builds
    RingMissing,       // the off-screen ring
    OffscreenMissing,  // the store of how far outside
    PixelMissing,      // the pixel's place in the record
    OutsideFunction,   // a site is not inside the function whose head matched, or they are out of order
    FovMismatch,       // the ring does not read the camera's vertical field of view where part 1 writes it
    RecordMismatch,    // the record's offsets do not follow one another as the record does
    ConverterMismatch, // the camera call is not the observer-camera hook's function at its CHUD site
    ConstantMismatch,  // the ring's fraction is not 0.3, or its pi / floor are not
    NavpointCallMissing,   // the navpoint list's call of the projection: no match, or more than one
    NavpointCallMismatch,  // it is not a call that lands on the matched projection from another function
    Count
};
inline const char* ProjectionBindName(ProjectionBind why)
{
    switch (why)
    {
    case ProjectionBind::Bound: return "bound";
    case ProjectionBind::HeadMissing: return "the marker projection's head is missing, ambiguous or not a function";
    case ProjectionBind::CameraMissing: return "the marker projection's camera build is missing or ambiguous";
    case ProjectionBind::RingMissing: return "the marker projection's off-screen ring is missing or ambiguous";
    case ProjectionBind::OffscreenMissing: return "the marker projection's off-screen store is missing or ambiguous";
    case ProjectionBind::PixelMissing: return "the marker projection's pixel store is missing or ambiguous";
    case ProjectionBind::OutsideFunction: return "a site is not inside the matched marker projection, in order";
    case ProjectionBind::FovMismatch: return "the ring does not read the camera's field of view at +0x28";
    case ProjectionBind::RecordMismatch: return "the marker record's offsets do not follow one another";
    case ProjectionBind::ConverterMismatch:
        return "the projection's camera call is not the observer-camera hook's call site";
    case ProjectionBind::ConstantMismatch: return "the ring's constants are not 0.3, pi and 0.0001";
    case ProjectionBind::NavpointCallMissing: return "the navpoint list's call of the projection is missing or ambiguous";
    case ProjectionBind::NavpointCallMismatch:
        return "the navpoint list's call does not land on the matched projection from another function";
    default: return "?";
    }
}
// Each pointer: the bytes at that pattern's unique match (null = missing or ambiguous), with its offset from the
// module base. `headIsFunction`: the image's function table begins a function at the head. `*FunctionRva`: the
// beginning of the function each other site lies in, from the same table. The caller then checks the converter
// against the observer-camera hook and reads the three constants for CheckProjectionConstants.
inline ProjectionBind DecodeProjectionSites(const uint8_t* head, int64_t headRva, bool headIsFunction,
                                            const uint8_t* camera, int64_t cameraRva, int64_t cameraFunctionRva,
                                            const uint8_t* ring, int64_t ringRva, int64_t ringFunctionRva,
                                            const uint8_t* offscreen, int64_t offscreenRva,
                                            int64_t offscreenFunctionRva, const uint8_t* pixel, int64_t pixelRva,
                                            int64_t pixelFunctionRva, ProjectionSites& out)
{
    if (!head || !headIsFunction)
        return ProjectionBind::HeadMissing;
    if (!camera)
        return ProjectionBind::CameraMissing;
    if (!ring)
        return ProjectionBind::RingMissing;
    if (!offscreen)
        return ProjectionBind::OffscreenMissing;
    if (!pixel)
        return ProjectionBind::PixelMissing;
    if (cameraFunctionRva != headRva || ringFunctionRva != headRva || offscreenFunctionRva != headRva ||
        pixelFunctionRva != headRva || !(headRva < cameraRva && cameraRva < pixelRva && pixelRva < ringRva &&
                                         ringRva < offscreenRva))
        return ProjectionBind::OutsideFunction;
    const int32_t cameraBuffer = ReadI32(camera + kCameraBufferDisp);
    const int32_t ringFov = ReadI32(ring + kRingFovDisp);
    // The camera's position is its first field: the point is subtracted from the buffer itself.
    if (ReadI32(camera + kCameraPositionXDisp) != cameraBuffer ||
        ReadI32(camera + kCameraPositionYDisp) != cameraBuffer + 4 ||
        ringFov - cameraBuffer != static_cast<int32_t>(kCameraVerticalFovOffset))
        return ProjectionBind::FovMismatch;
    const uint32_t point = camera[kCameraPointX], pointY = camera[kCameraPointY];
    const uint32_t pixelX = pixel[kPixelX], pixelY = pixel[kPixelY], off = offscreen[kOffscreenRecord];
    // valid byte (+0), then the point (three floats), then the pixel (two), the virtual place (two), then this.
    if (point < 4 || (point & 3) || pointY != point + 4 || pixelX != point + 12 || pixelY != pixelX + 4 ||
        off != pixelX + 16 || off >= 0x80)
        return ProjectionBind::RecordMismatch;
    ProjectionSites sites;
    sites.headRva = headRva;
    sites.converterRva = cameraRva + static_cast<int64_t>(kCameraConverterReturn) +
                         ReadI32(camera + kCameraConverterCall + 1);
    sites.converterReturnRva = cameraRva + static_cast<int64_t>(kCameraConverterReturn);
    sites.fractionRva = ringRva + static_cast<int64_t>(kRingFractionNext) + ReadI32(ring + kRingFractionDisp);
    sites.piRva = offscreenRva + static_cast<int64_t>(kOffscreenPiNext) + ReadI32(offscreen + kOffscreenPiDisp);
    sites.floorRva =
        offscreenRva + static_cast<int64_t>(kOffscreenFloorNext) + ReadI32(offscreen + kOffscreenFloorDisp);
    sites.cameraFovOffset = static_cast<uint32_t>(ringFov - cameraBuffer);
    sites.recordPoint = point;
    sites.recordPixel = pixelX;
    sites.recordOffscreen = off;
    if (camera[kCameraConverterCall] != 0xE8 || sites.converterRva <= 0 || sites.fractionRva <= 0 ||
        sites.piRva <= 0 || sites.floorRva <= 0)
        return ProjectionBind::ConverterMismatch;
    out = sites;  // a refusal fills nothing
    return ProjectionBind::Bound;
}
// `observerHookRva` / `chudSiteReturnRva`: the function the observer-camera hook is installed on and the return
// address it classifies as its CHUD site, both as offsets from the module base.
inline ProjectionBind CheckProjectionConverter(const ProjectionSites& sites, int64_t observerHookRva,
                                               int64_t chudSiteReturnRva)
{
    return observerHookRva > 0 && sites.converterRva == observerHookRva &&
                   sites.converterReturnRva == chudSiteReturnRva
               ? ProjectionBind::Bound
               : ProjectionBind::ConverterMismatch;
}
// The four bytes at each of sites.fractionRva, piRva and floorRva.
inline ProjectionBind CheckProjectionConstants(const uint8_t* fraction, const uint8_t* pi, const uint8_t* floor)
{
    if (!fraction || !pi || !floor)
        return ProjectionBind::ConstantMismatch;
    return std::fabs(ReadF32(fraction) - kRingFraction) <= 1.0e-6f && std::fabs(ReadF32(pi) - kPi) <= 1.0e-6f &&
                   std::fabs(ReadF32(floor) - kOffscreenFloor) <= 1.0e-9f
               ? ProjectionBind::Bound
               : ProjectionBind::ConstantMismatch;
}
// The object markers' call: the bytes at its pattern's unique match (null = missing or ambiguous), where they
// are, and the beginning of the function they lie in (-1 = none). The place its call of the projection returns
// to, as an offset from the image; 0 = it is not a call that lands on the projection bound in `sites` from
// another function (nothing is acted for then).
inline int64_t DecodeObjectMarkerCall(const uint8_t* call, int64_t callRva, int64_t callFunctionRva,
                                      const ProjectionSites& sites)
{
    if (!call || callRva <= 0)
        return 0;
    const int64_t returnRva = callRva + static_cast<int64_t>(kObjectMarkerReturn);
    if (call[kObjectMarkerCall] != 0xE8 || sites.headRva <= 0 || callFunctionRva < 0 ||
        callFunctionRva == sites.headRva || returnRva + ReadI32(call + kObjectMarkerCall + 1) != sites.headRva)
        return 0;
    return returnRva;
}
// The navpoint list's call: the bytes at its pattern's unique match (null = missing or ambiguous), where they are,
// and the beginning of the function they lie in (-1 = none). Fills sites.navpointReturnRva, and only when the
// call lands on the projection bound in `sites` from another function. A refusal fills nothing.
inline ProjectionBind DecodeNavpointCall(const uint8_t* call, int64_t callRva, int64_t callFunctionRva,
                                         ProjectionSites& sites)
{
    if (!call)
        return ProjectionBind::NavpointCallMissing;
    const int64_t returnRva = callRva + static_cast<int64_t>(kNavpointReturn);
    if (call[kNavpointCall] != 0xE8 || sites.headRva <= 0 || callFunctionRva < 0 ||
        callFunctionRva == sites.headRva || returnRva + ReadI32(call + kNavpointCall + 1) != sites.headRva)
        return ProjectionBind::NavpointCallMismatch;
    sites.navpointReturnRva = returnRva;
    return ProjectionBind::Bound;
}

// ---- the witness (dev builds): the game's pixel against the eye's own picture ----------------------------------
// The eye's raster as the mod committed it: the camera it was rasterised from, the two scales of the projection
// matrix the game derived for it (|m00|, |m11|: 1 / (aspect * t) and 1 / t), and the window it fills.
struct EyeRaster
{
    Camera camera;
    float xScale = 0.0f, yScale = 0.0f;
    float width = 0.0f, height = 0.0f;
};
// Where the eye's own picture shows `point`, by the projection matrix it was rasterised with (not by a field of
// view). False (out untouched) when the point is not in front of the eye or the raster is not usable.
inline bool EyeRasterPixel(const EyeRaster& eye, const float point[3], float out[2])
{
    if (!(eye.xScale > 0.0f) || !(eye.yScale > 0.0f) || !(eye.width > 0.0f) || !(eye.height > 0.0f) ||
        !std::isfinite(eye.xScale) || !std::isfinite(eye.yScale))
        return false;
    const float d[3]{point[0] - eye.camera.position[0], point[1] - eye.camera.position[1],
                     point[2] - eye.camera.position[2]};
    float right[3];
    Cross(eye.camera.forward, eye.camera.up, right);
    const float depth = Dot(d, eye.camera.forward);
    if (!(depth > 0.0001f))
        return false;
    const float x = (0.5f + 0.5f * eye.xScale * Dot(d, right) / depth) * eye.width;
    const float y = (0.5f - 0.5f * eye.yScale * Dot(d, eye.camera.up) / depth) * eye.height;
    if (!std::isfinite(x) || !std::isfinite(y))
        return false;
    out[0] = x;
    out[1] = y;
    return true;
}
// The angle between the camera's axis and the point, in radians; 0 for a point at the camera.
inline float OffAxis(const Camera& camera, const float point[3])
{
    const float d[3]{point[0] - camera.position[0], point[1] - camera.position[1], point[2] - camera.position[2]};
    const float length = std::sqrt(Dot(d, d));
    if (!(length > 1.0e-6f))
        return 0.0f;
    float c = Dot(d, camera.forward) / length;
    c = c > 1.0f ? 1.0f : c < -1.0f ? -1.0f : c;
    return std::acos(c);
}
// WHERE AN EYE DRAWS SOMETHING, as a direction. The inverse of EyeRasterPixel: the tangents, in the eye's own
// frame, of the line of sight through a raster pixel (across: positive to the right; up: positive up).
inline bool EyePixelTangents(const EyeRaster& eye, const float pixel[2], float& across, float& up)
{
    if (!(eye.xScale > 0.0f) || !(eye.yScale > 0.0f) || !(eye.width > 0.0f) || !(eye.height > 0.0f) ||
        !std::isfinite(eye.xScale) || !std::isfinite(eye.yScale))
        return false;
    const float a = (pixel[0] / eye.width - 0.5f) * 2.0f / eye.xScale;
    const float u = (0.5f - pixel[1] / eye.height) * 2.0f / eye.yScale;
    if (!std::isfinite(a) || !std::isfinite(u))
        return false;
    across = a;
    up = u;
    return true;
}
// That line of sight in the world, unit length (right = forward x up, as everywhere in this header).
inline void EyeRay(const Camera& eye, float across, float up, float out[3])
{
    float right[3];
    Cross(eye.forward, eye.up, right);
    float v[3];
    for (int i = 0; i < 3; ++i)
        v[i] = eye.forward[i] + across * right[i] + up * eye.up[i];
    const float length = std::sqrt(Dot(v, v));
    for (int i = 0; i < 3; ++i)
        out[i] = length > 0.0f ? v[i] / length : eye.forward[i];
}
// A world direction's two angles in a camera's frame, radians: across (positive to the right) and up, each the
// arctangent of its tangent, as a screen would show them. False for a direction not in front of the camera.
inline bool DirectionAngles(const Camera& camera, const float direction[3], float& across, float& up)
{
    float right[3];
    Cross(camera.forward, camera.up, right);
    const float depth = Dot(direction, camera.forward);
    if (!(depth > 1.0e-6f))
        return false;
    across = std::atan(Dot(direction, right) / depth);
    up = std::atan(Dot(direction, camera.up) / depth);
    return std::isfinite(across) && std::isfinite(up);
}
inline constexpr float kArcMinutesPerRadian = 60.0f * 180.0f / kPi;

// What the witness made of one projection. Counted by reason.
enum class Witness : uint8_t
{
    Compared,     // drawn on the point itself: the game's pixel was compared with the eye's own
    OnRing,       // the game put it on its off-screen ring: nothing to compare
    NotValid,     // the record's valid byte is clear: the game projected nothing
    NoEyeScope,   // not inside an eye's own render
    NotInFront,   // the point is not in front of the eye
    NoRaster,     // the eye's raster is not usable
    Fault,        // the record could not be read
    Count
};
inline const char* WitnessName(Witness what)
{
    switch (what)
    {
    case Witness::Compared: return "compared";
    case Witness::OnRing: return "on the off-screen ring";
    case Witness::NotValid: return "not a valid record";
    case Witness::NoEyeScope: return "not inside an eye's render";
    case Witness::NotInFront: return "not in front of the eye";
    case Witness::NoRaster: return "no eye raster";
    case Witness::Fault: return "record read fault";
    default: return "?";
    }
}

// ====================================================================================================================
// PART 2: THE OFF-SCREEN RING, IN THE HEADSET
//
// WHAT THE GAME DOES (retail +0x2E160C..+0x2E1831, the `ring` argument set). A caller that wants an off-screen
// indicator gets the point's two angles in the camera's own space,
//     yaw = atan2(left, forward)        pitch = atan2(up, hypot(forward, left))
// and the ring is an ellipse in those angles, 0.3 of the camera's vertical field of view high and that times the
// HUD's width over its height wide. Inside it the marker is drawn on the point itself. Outside it both angles
// are divided by ratio = sqrt((yaw / halfWidth)^2 + (pitch / halfHeight)^2), the marker is drawn at that place on
// the ring, and the record's off-screen value is max(halfWidth / pi * (ratio - 1), 0.0001), which the widget's
// arrow turns on.
//
// With the eye's field of view (part 1) that ring is 0.3 of the eye's vertical field high and wider still.
//   RUN IN A GAME (simulated runtime: eye raster 100 deg and 5242 x 3780, eyes parallel, 40 deg toward the nose;
//   one far target): the ring is 30.0 deg up and down and 41.6 deg sideways. For a target to the side the two
//   eyes draw the arrow in the same direction (within 0.2 arc-minutes across, 0.9 up), 41.0 to 41.6 deg from each
//   eye's own axis. That is past the 40 deg the other eye sees toward the nose: THE ARROW IS IN ONE EYE ONLY.
//   COMPUTED for a headset with 40 deg toward the nose, an eye raster of 110 deg and the same aspect: 45.8 deg.
//   NOT MEASURED: a near target (each eye clamps about its own axis, so the two arrows would diverge); canted
//   eyes.
//
// THE SEAM. A detour on the projection. When the caller asked for the ring and the point lies outside a ring laid
// out about the HEAD's axis, the detour hands the game a stand-in point for that one call: the point's direction
// brought onto the head's ring, at the point's own distance. Both eyes then project the SAME world point, inside
// the game's own (larger) ring, so the game draws it on that point and the two eyes agree; when the game returns
// the record's point is put back and its off-screen value is set by the game's own formula, so the widget still
// shows its arrow. The game still draws its own art, text and colour. Nothing else is written. Only for the
// navpoint list's call (kNavpointCallPattern) and the object-anchored markers' call (kObjectMarkerCallPattern),
// and only when the call measures pitch in the camera's own frame.
//
// THE HEAD'S RING is the game's own rule applied to the field BOTH eyes see: the game's ring is six tenths of the
// half field of view (0.3 of the whole), so the head's is six tenths of the half-angles, about the head's axis,
// inside which a direction is visible to both eyes -- sideways, the nose-side limit of the two eyes; up and down,
// the smaller of the two. It is never larger than the game's own ring for the same eye, so the game does not
// clamp the stand-in again.
// ====================================================================================================================

// One eye as OpenXR reports it relative to the head: the four half-angles (left and down negative, radians) and
// the eye's orientation in the head's space (x, y, z, w; +X right, +Y up, -Z forward).
struct EyeField
{
    float angleLeft = 0.0f, angleRight = 0.0f, angleUp = 0.0f, angleDown = 0.0f;
    float orientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
};
// Half-angles of a ring, or of a field, about the head's axis: sideways and up-down, radians.
struct RingField
{
    float halfYaw = 0.0f, halfPitch = 0.0f;
};
inline constexpr float kHeadRingFraction = 2.0f * kRingFraction;  // of the HALF field: the game's own 0.3 of the whole
// The stand-in is never nearer than this (world units; about two metres): an indicator on the ring is read like
// HUD, and a target at arm's length would pull the eyes in.
inline constexpr float kMinRingDistance = 2.0f / 3.048f;

// The field both eyes see, about the head's axis. False when an eye's angles or orientation are not usable.
inline bool SharedField(const EyeField eyes[2], RingField& out)
{
    float right = 1.0e9f, left = 1.0e9f, up = 1.0e9f, down = 1.0e9f;
    for (int eye = 0; eye < 2; ++eye)
    {
        const EyeField& e = eyes[eye];
        float q[4]{e.orientation[0], e.orientation[1], e.orientation[2], e.orientation[3]};
        const float norm = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (!std::isfinite(norm) || !(norm > 1.0e-6f) || !std::isfinite(e.angleLeft) ||
            !std::isfinite(e.angleRight) || !std::isfinite(e.angleUp) || !std::isfinite(e.angleDown) ||
            !(e.angleLeft < 0.0f) || !(e.angleRight > 0.0f) || !(e.angleUp > 0.0f) || !(e.angleDown < 0.0f) ||
            !(e.angleLeft > -1.5f) || !(e.angleRight < 1.5f) || !(e.angleUp < 1.5f) || !(e.angleDown > -1.5f))
            return false;
        for (float& c : q)
            c /= norm;
        // The middle of each edge of the eye's field, as a ray in the eye's space, turned into the head's.
        const float rays[4][3]{{std::tan(e.angleLeft), 0.0f, -1.0f},
                               {std::tan(e.angleRight), 0.0f, -1.0f},
                               {0.0f, std::tan(e.angleUp), -1.0f},
                               {0.0f, std::tan(e.angleDown), -1.0f}};
        float angle[4];
        for (int i = 0; i < 4; ++i)
        {
            // v' = v + 2 w (q x v) + 2 q x (q x v)
            const float* v = rays[i];
            float qv[3], qqv[3];
            Cross(q, v, qv);
            Cross(q, qv, qqv);
            const float x = v[0] + 2.0f * (q[3] * qv[0] + qqv[0]);
            const float y = v[1] + 2.0f * (q[3] * qv[1] + qqv[1]);
            const float z = v[2] + 2.0f * (q[3] * qv[2] + qqv[2]);
            if (!(z < -1.0e-3f))
                return false;
            angle[i] = std::atan2(i < 2 ? x : y, -z);
        }
        left = -angle[0] < left ? -angle[0] : left;
        right = angle[1] < right ? angle[1] : right;
        up = angle[2] < up ? angle[2] : up;
        down = -angle[3] < down ? -angle[3] : down;
    }
    const float halfYaw = left < right ? left : right, halfPitch = up < down ? up : down;
    if (!(halfYaw > 0.01f) || !(halfPitch > 0.01f))
        return false;
    out.halfYaw = halfYaw;
    out.halfPitch = halfPitch;
    return true;
}
// The head's ring is never further out than this, whatever the headset's field: 30 degrees, the outer edge of
// the cone a headset shows comfortably (about 25 to 30 degrees). It is a round upper bound, not a measured value.
inline constexpr float kMaxRingAngle = 30.0f * kDegToRad;
// The head's ring: `fraction` of the shared field's half-angles, each capped at kMaxRingAngle.
inline RingField HeadRing(const RingField& shared, float fraction = kHeadRingFraction)
{
    RingField ring;
    ring.halfYaw = shared.halfYaw * fraction;
    ring.halfPitch = shared.halfPitch * fraction;
    if (ring.halfYaw > kMaxRingAngle)
        ring.halfYaw = kMaxRingAngle;
    if (ring.halfPitch > kMaxRingAngle)
        ring.halfPitch = kMaxRingAngle;
    return ring;
}
// The game's own ring for a camera: 0.3 of its vertical field of view high, times the HUD's aspect wide.
inline RingField GameRing(float verticalFov, float hudAspect)
{
    RingField ring;
    ring.halfPitch = verticalFov * kRingFraction;
    ring.halfYaw = ring.halfPitch * hudAspect;
    return ring;
}
// A point's two angles in a camera's own space, as the game measures them (yaw positive to the left).
// False for a point at the camera.
inline bool RingAngles(const Camera& camera, const float point[3], float& yaw, float& pitch, float* distance = nullptr)
{
    const float d[3]{point[0] - camera.position[0], point[1] - camera.position[1], point[2] - camera.position[2]};
    const float length = std::sqrt(Dot(d, d));
    if (!std::isfinite(length) || !(length > 1.0e-5f))
        return false;
    float left[3];
    Cross(camera.up, camera.forward, left);
    const float x = Dot(d, camera.forward), y = Dot(d, left), z = Dot(d, camera.up);
    yaw = std::atan2(y, x);
    pitch = std::atan2(z, std::sqrt(x * x + y * y));
    if (distance)
        *distance = length;
    return true;
}
// How far outside a ring: 1 on it, below 1 inside.
inline float RingRatio(const RingField& ring, float yaw, float pitch)
{
    const float a = yaw / ring.halfYaw, b = pitch / ring.halfPitch;
    return std::sqrt(a * a + b * b);
}
// The record's off-screen value for a point `ratio` times as far out as the ring (ratio above 1).
inline float OffscreenValue(const RingField& ring, float ratio)
{
    const float value = ring.halfYaw / kPi * (ratio - 1.0f);
    return value > kOffscreenFloor ? value : kOffscreenFloor;
}

// What the detour did with one projection. Counted by reason; only Moved writes anything.
enum class Ring : uint8_t
{
    Moved,         // outside the head's ring: the game was handed the stand-in on it
    OnTarget,      // inside the head's ring: the game draws the marker on the point itself
    Plain,         // the caller did not ask for a ring: the point is projected as it is
    OtherCaller,   // a ring was asked for, but not by the navpoint list's or an object marker's call: the game's own
    WorldPitch,    // the call measures pitch against the world: the stand-in's geometry is not that
    SwitchOff,     // the player's switch is off
    NoEyeScope,    // not inside an eye's own render
    NotLocalUser,  // not output user 0's HUD
    NotValid,      // the record's valid byte is clear: the game projects nothing
    NoHead,        // the head's frame is not usable (not finite, not a unit perpendicular pair)
    NoField,       // the eyes' fields are not usable
    AtHead,        // the point is at the head: it has no direction
    Fault,         // the record could not be read or written
    Count
};
inline const char* RingName(Ring what)
{
    switch (what)
    {
    case Ring::Moved: return "brought onto the head's ring";
    case Ring::OnTarget: return "on its target";
    case Ring::Plain: return "no ring asked for";
    case Ring::OtherCaller: return "not the navpoint list's or an object marker's call";
    case Ring::WorldPitch: return "a world-pitch ring";
    case Ring::SwitchOff: return "switch off";
    case Ring::NoEyeScope: return "not inside an eye's render";
    case Ring::NotLocalUser: return "not the local user";
    case Ring::NotValid: return "not a valid record";
    case Ring::NoHead: return "no head frame";
    case Ring::NoField: return "no eye field";
    case Ring::AtHead: return "the point is at the head";
    case Ring::Fault: return "record fault";
    default: return "?";
    }
}
// The stand-in the game is handed for one call, and what its record's off-screen value becomes afterwards.
struct RingMove
{
    float point[3]{};
    float offscreen = 0.0f;
    float yaw = 0.0f, pitch = 0.0f;  // the point's own angles off the head's axis (radians), for the report
    float ratio = 0.0f;
};
// A unit perpendicular pair, finite.
inline bool HeadUsable(const Camera& head)
{
    const float ff = Dot(head.forward, head.forward), uu = Dot(head.up, head.up), fu = Dot(head.forward, head.up);
    return std::isfinite(head.position[0]) && std::isfinite(head.position[1]) && std::isfinite(head.position[2]) &&
           std::isfinite(ff) && std::isfinite(uu) && std::fabs(ff - 1.0f) <= 1.0e-3f &&
           std::fabs(uu - 1.0f) <= 1.0e-3f && std::fabs(fu) <= 1.0e-3f;
}
// The verdict for one projection. `head`: the head's own camera for this frame (position, forward, up), the one
// both eyes were built from. `ring`: the head's ring. Moved fills `out`; anything else leaves it untouched and
// the game projects the point as it is. Identity only: no point is declined for being near, far or far off axis.
// `fromNavpoints`: the game's call came from a caller the stand-in acts for: the navpoint list, or the
// object-anchored markers' wrapper.
// `worldPitch`: the call's fifth argument (the navpoint list passes 0; the damage indicators pass 1).
inline Ring RingPoint(bool switchOn, bool eyeScope, int user, bool valid, bool wantsRing, bool fromNavpoints,
                      bool worldPitch, const Camera& head, bool haveField, const RingField& ring,
                      const float point[3], RingMove& out)
{
    if (!switchOn)
        return Ring::SwitchOff;
    if (!eyeScope)
        return Ring::NoEyeScope;
    if (user != 0)
        return Ring::NotLocalUser;
    if (!valid)
        return Ring::NotValid;
    if (!wantsRing)
        return Ring::Plain;
    if (!fromNavpoints)
        return Ring::OtherCaller;
    if (worldPitch)
        return Ring::WorldPitch;
    if (!HeadUsable(head) || !std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2]))
        return Ring::NoHead;
    if (!haveField || !(ring.halfYaw > 0.0f) || !(ring.halfPitch > 0.0f) || !std::isfinite(ring.halfYaw) ||
        !std::isfinite(ring.halfPitch))
        return Ring::NoField;
    float yaw = 0.0f, pitch = 0.0f, distance = 0.0f;
    if (!RingAngles(head, point, yaw, pitch, &distance))
        return Ring::AtHead;
    const float ratio = RingRatio(ring, yaw, pitch);
    if (!std::isfinite(ratio))
        return Ring::NoHead;
    if (ratio <= 1.0f)
        return Ring::OnTarget;
    const float ringYaw = yaw / ratio, ringPitch = pitch / ratio;
    const float reach = distance > kMinRingDistance ? distance : kMinRingDistance;
    float left[3];
    Cross(head.up, head.forward, left);
    const float cp = std::cos(ringPitch), sp = std::sin(ringPitch), cy = std::cos(ringYaw), sy = std::sin(ringYaw);
    RingMove move;
    for (int i = 0; i < 3; ++i)
        move.point[i] = head.position[i] + reach * (head.forward[i] * cp * cy + left[i] * cp * sy + head.up[i] * sp);
    if (!std::isfinite(move.point[0]) || !std::isfinite(move.point[1]) || !std::isfinite(move.point[2]))
        return Ring::NoHead;
    move.offscreen = OffscreenValue(ring, ratio);
    move.yaw = yaw;
    move.pitch = pitch;
    move.ratio = ratio;
    out = move;
    return Ring::Moved;
}
} // namespace reach_nav_markers
