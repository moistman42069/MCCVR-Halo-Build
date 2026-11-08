// REACH: THE MOTION TRACKER AND THE SOUND LISTENER FOLLOW THE HEAD, the pure half (no game, engine or OpenXR
// includes). The tracker is first; the listener starts at "THE SOUND LISTENER FOLLOWS THE HEAD" below.
//
// WHAT REACH DOES (HREK first, then matched in retail; docs/REACH-HEAD-RELATIVE-2026-10-06.md).
// HREK reach_tag_test.exe, chud_motion_sensor.cpp: the per-user tracker object's update (+0x954DE0) asks
// player_mapping for the output user's unit, copies the unit's position as the tracker's origin, and takes its
// heading from ONE place -- player_control.cpp's accessor of `&player_control_output->output.desired_angles`
// (+0x1E60D0, the assert names the field):
//     heading = desired_angles.yaw + pi/2          -> stored in the tracker object (+0x13D4)
// It then calls three functions in a row with the tracker as their only object; the first and the third read
// that stored heading and turn every contact's world offset by -heading (cosf / sinf), on foot and in a seat
// alike -- there is no vehicle branch. Nothing at draw time reads a heading again: a drawn contact is the stored
// point times a scale.
// Retail haloreach.dll has the same function at +0x2ED0C8 with the accessor inlined (engine TLS + 0xD8, the
// per-output-user stride 0xB0, the yaw at +0x694) and the same three calls (+0x2EC164, +0x2EC404, +0x2ECEB0).
//
// desired_angles is what this mod's direct drive writes the HAND's direction into, so the stock tracker turns
// with the gun. THE SEAM: a detour on the first of those three calls. It runs after the game stored the
// heading and before anything read it, and it rewrites that one float in the tracker's own object. The aim
// record, the camera evaluators, the rendered view and everything replicated are never touched or even read.
//
// ADDR-HYGIENE: resolved -- no address and no offset ships. The store site is matched uniquely in the whole
// module; the hook target is the call that follows it, and must be the uniquely matched head; the heading's
// offset is read from the store instruction and must be the displacement both readers use (ValueAgreement, two
// independent sites); the constant the game adds must be pi/2; the user index offset is read from the update's
// own head. Any link failing binds nothing: the game's own tracker, with the reason in the log.
#pragma once

#include "head_relative_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace reach_head_relative
{
inline constexpr float kHalfPi = head_relative::kPi * 0.5f;
inline constexpr float kTwoPi = head_relative::kPi * 2.0f;
inline constexpr float kWorldUp[3]{0.0f, 0.0f, 1.0f};

// ---- the sites -------------------------------------------------------------------------------------------------
// ADDR-HYGIENE: resolved (the comment above). The numbers beside each pattern are positions INSIDE that matched
// pattern, never image addresses.
// The update's read-add-store and the three calls after it (retail +0x2ED187):
//   imul rcx, r9, 0xB0 / mov rax, [rdx + rbx] / movss xmm0, [rax + rcx + 0x694]   ; output desired_angles.yaw
//   mov rcx, rdi / addss xmm0, [rip + HALF PI] / movss [rdi + HEADING], xmm0
//   call FIRST / mov rcx, rdi / call SECOND / mov rdx, r15 / mov rcx, rdi / call THIRD
inline constexpr const char* kTrackerHeadingSitePattern =
    "49 69 C9 B0 00 00 00 48 8B 04 1A F3 0F 10 84 08 94 06 00 00 48 8B CF F3 0F 58 05 ?? ?? ?? ?? "
    "F3 0F 11 87 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 49 8B D7 48 8B CF E8";
inline constexpr size_t kSiteBytes = 0x3F;  // through the third call's rel32
inline constexpr size_t kSiteHalfPiDisp = 0x1B, kSiteHalfPiNext = 0x1F, kSiteHeadingDisp = 0x23,
                        kSiteCallFirst = 0x27, kSiteCallSecond = 0x2F, kSiteCallThird = 0x3A;
// The update's head (retail +0x2ED0C8): prologue / movsxd r9, [rcx + USER] / mov rdi, rcx / mov ecx, r9d /
// mov r15, r8 / call (the output user's player).
inline constexpr const char* kTrackerUpdateHeadPattern =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 57 48 83 EC 20 4C 63 89 ?? ?? ?? ?? 48 8B F9 "
    "41 8B C9 4D 8B F8 E8";
inline constexpr size_t kUpdateHeadBytes = 0x29;
inline constexpr size_t kUpdateUserDisp = 0x1B;
// The first call's head (retail +0x2EC164, the hook): prologue / mov edx, [rip + TLS index] /
// lea rbx, [rcx + 0x1204] / movaps [rax - 0x38], xmm6 / mov rdi, rcx. One argument, the tracker, in rcx.
inline constexpr const char* kTrackerFirstReaderHeadPattern =
    "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC C0 00 00 00 "
    "8B 15 ?? ?? ?? ?? 48 8D 99 04 12 00 00 0F 29 70 C8 48 8B F9";
inline constexpr size_t kFirstReaderHeadBytes = 0x37;
// ADDR-HYGIENE: guarded (ValueAgreement). Where each reader's load sits inside its function is a measurement of
// this build. It is used only to PROVE the field: the load's opcode must be there and its displacement must be
// the one the store instruction carries. A build where either differs binds nothing.
// Its read of the heading: movss xmm10, [rdi + HEADING] (F3 44 0F 10 97 disp32).
inline constexpr size_t kFirstReaderRead = 0xD6, kFirstReaderReadDisp = 0xDB, kFirstReaderBytes = 0xDF;
inline constexpr uint8_t kFirstReaderReadOpcode[5]{0xF3, 0x44, 0x0F, 0x10, 0x97};
// The third call's read: movss xmm7, [rcx + HEADING] (F3 0F 10 B9 disp32), before it has moved rcx.
inline constexpr size_t kThirdReaderRead = 0x35, kThirdReaderReadDisp = 0x39, kThirdReaderBytes = 0x3D;
inline constexpr uint8_t kThirdReaderReadOpcode[4]{0xF3, 0x0F, 0x10, 0xB9};

inline uint32_t ReadU32(const uint8_t* at)
{
    uint32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
inline int32_t ReadI32(const uint8_t* at)
{
    int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}
// Where the E8 at `site + call` lands, as an offset from the module base (`siteRva` is the site's own).
inline int64_t CallRva(const uint8_t* site, size_t call, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(call) + 5 + ReadI32(site + call + 1);
}
inline int64_t RipRva(const uint8_t* site, size_t disp, size_t next, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(next) + ReadI32(site + disp);
}

struct TrackerSites
{
    uint32_t headingOffset = 0;  // the tracker object's heading (retail and HREK 0x13D4)
    uint32_t userOffset = 0;     // its output user index (retail and HREK 0x13B8)
    int64_t firstReaderRva = 0;  // the hook: called right after the store, before any read
    int64_t thirdReaderRva = 0;  // the other reader of the heading
    int64_t halfPiRva = 0;       // the constant the game adds to the yaw
};
enum class TrackerBind : uint8_t
{
    Bound = 0,
    SiteMissing,        // the heading's read-add-store: no match, or more than one
    UpdateHeadMissing,  // the update's head: no match, or more than one
    SiteOutsideUpdate,  // the store is not inside the function whose head matched
    FirstHeadMissing,   // the first reader's head: no match, or more than one
    CallMismatch,       // the call after the store does not land on the matched head
    ReaderMismatch,     // a reader does not read the heading where it should
    FieldMismatch,      // the store and the readers disagree on the heading's offset
    Implausible,        // an offset out of range or misaligned, or the user index is not before the heading
    ConstantMismatch,   // what the game adds to the yaw is not pi/2
    Count
};
inline const char* TrackerBindName(TrackerBind why)
{
    switch (why)
    {
    case TrackerBind::Bound: return "bound";
    case TrackerBind::SiteMissing: return "the tracker's heading store is missing or ambiguous";
    case TrackerBind::UpdateHeadMissing: return "the tracker update's head is missing or ambiguous";
    case TrackerBind::SiteOutsideUpdate: return "the heading store is not inside the matched tracker update";
    case TrackerBind::FirstHeadMissing: return "the first reader's head is missing or ambiguous";
    case TrackerBind::CallMismatch: return "the call after the heading store does not land on the matched reader";
    case TrackerBind::ReaderMismatch: return "a reader does not read the heading where it should";
    case TrackerBind::FieldMismatch: return "the store and the readers disagree on the heading's offset";
    case TrackerBind::Implausible: return "the decoded offsets are implausible";
    case TrackerBind::ConstantMismatch: return "the game does not add pi/2 to the yaw";
    default: return "?";
    }
}
// `site`, `updateHead`, `firstHead`: the unique matches (null = missing or ambiguous) with their offsets from the
// module base. `updateEndRva`: the end of the function `site` lies in, from the image's own function table, and
// `siteFunctionRva` its beginning. Fills the two call targets and the constant's place; the caller then reads
// the third reader's bytes and the constant, and finishes with CheckTrackerReaders.
inline TrackerBind DecodeTrackerSites(const uint8_t* site, int64_t siteRva, const uint8_t* updateHead,
                                      int64_t updateHeadRva, int64_t siteFunctionRva, const uint8_t* firstHead,
                                      int64_t firstHeadRva, TrackerSites& out)
{
    if (!site)
        return TrackerBind::SiteMissing;
    if (!updateHead)
        return TrackerBind::UpdateHeadMissing;
    if (siteFunctionRva != updateHeadRva || siteRva <= updateHeadRva)
        return TrackerBind::SiteOutsideUpdate;
    if (!firstHead)
        return TrackerBind::FirstHeadMissing;
    if (CallRva(site, kSiteCallFirst, siteRva) != firstHeadRva)
        return TrackerBind::CallMismatch;
    const uint32_t heading = ReadU32(site + kSiteHeadingDisp), user = ReadU32(updateHead + kUpdateUserDisp);
    if (!heading || heading >= 0x100000 || (heading & 3) || !user || user >= 0x100000 || (user & 3) ||
        user + sizeof(int32_t) > heading)
        return TrackerBind::Implausible;
    out.headingOffset = heading;
    out.userOffset = user;
    out.firstReaderRva = firstHeadRva;
    out.thirdReaderRva = CallRva(site, kSiteCallThird, siteRva);
    out.halfPiRva = RipRva(site, kSiteHalfPiDisp, kSiteHalfPiNext, siteRva);
    if (out.thirdReaderRva <= 0 || out.thirdReaderRva == firstHeadRva || out.halfPiRva <= 0)
        return TrackerBind::Implausible;
    return TrackerBind::Bound;
}
// `first`: kFirstReaderBytes at the first reader's head; `third`: kThirdReaderBytes at the third's; `halfPi`:
// the four bytes at sites.halfPiRva.
inline TrackerBind CheckTrackerReaders(const uint8_t* first, const uint8_t* third, const uint8_t* halfPi,
                                       const TrackerSites& sites)
{
    if (!first || !third || !halfPi)
        return TrackerBind::ReaderMismatch;
    if (std::memcmp(first + kFirstReaderRead, kFirstReaderReadOpcode, sizeof(kFirstReaderReadOpcode)) != 0 ||
        std::memcmp(third + kThirdReaderRead, kThirdReaderReadOpcode, sizeof(kThirdReaderReadOpcode)) != 0)
        return TrackerBind::ReaderMismatch;
    if (ReadU32(first + kFirstReaderReadDisp) != sites.headingOffset ||
        ReadU32(third + kThirdReaderReadDisp) != sites.headingOffset)
        return TrackerBind::FieldMismatch;
    float value = 0.0f;
    std::memcpy(&value, halfPi, sizeof(value));
    if (!(std::fabs(value - kHalfPi) <= 1.0e-6f))
        return TrackerBind::ConstantMismatch;
    return TrackerBind::Bound;
}

// ---- the heading -----------------------------------------------------------------------------------------------
// The stored field is the yaw plus pi/2; a yaw is counter-clockwise from +X seen from above (+Z up), the angle
// the direct drive writes as atan2(direction.y, direction.x).
inline float FieldFromYaw(float yaw) { return yaw + kHalfPi; }
inline float YawFromField(float field) { return field - kHalfPi; }
inline void ForwardOfYaw(float yaw, float out[3])
{
    out[0] = std::cos(yaw);
    out[1] = std::sin(yaw);
    out[2] = 0.0f;
}
// A yaw in [0, 2 pi), the range the game's own accessor asserts for desired_angles.yaw.
inline float WrapYaw(float yaw)
{
    yaw = std::fmod(yaw, kTwoPi);
    if (yaw < 0.0f)
        yaw += kTwoPi;
    return yaw < kTwoPi ? yaw : 0.0f;
}

// WHERE THE GAME PUTS A CONTACT, from its own arithmetic (retail +0x2EC27B..+0x2EC35F, the scale left out):
//   a = -field;  x = sinf(a) * dy - cosf(a) * dx;  y = sinf(a) * dx + cosf(a) * dy
// With field = yaw + pi/2 that is x = offset . right, y = -(offset . forward): a contact straight ahead lands at
// (0, -r), which the tracker draws at its top; a contact to the right lands at (+r, 0).
inline void TrackerPoint(const float offset[3], float field, float out[2])
{
    const float a = -field;
    const float c = std::cos(a), s = std::sin(a);
    out[0] = s * offset[1] - c * offset[0];
    out[1] = s * offset[0] + c * offset[1];
}
// The angle of a tracker point from the tracker's top, positive toward its LEFT (the same sense as
// head_relative::Bearing: a contact to the left of the heading is positive).
inline float TrackerPointBearing(const float point[2]) { return std::atan2(-point[0], -point[1]); }

struct TrackerRequest
{
    bool switchOn = false;    // g_config.tracker_follows_head, read at this use
    int32_t outputUser = -1;  // the tracker object's own output user
    bool gameplay = false;    // the frame the head sample came from was not a cinematic's
    uint32_t generation = 0;  // the title generation now
    uint64_t nowUs = 0;       // the sight clock now
    int sign = 1;             // g_config.tracker_head_sign
    float offsetDeg = 0.0f;   // g_config.tracker_head_offset_deg
};
// The field the tracker should place its contacts by. Ok = `fieldOut` is the game's field with the head's yaw
// (in the game's own range, [pi/2, 5 pi/2)); anything else names why the game's own field stands, and
// `fieldOut` is untouched. `hold` keeps the last heading the head had, for a head looking straight up or down.
inline head_relative::Use TrackerField(const TrackerRequest& request, const head_relative::HeadSample& sample,
                                       head_relative::HeadingHold& hold, float nativeField, float& fieldOut,
                                       float* headYawOut = nullptr)
{
    using head_relative::Use;
    if (!request.switchOn)
        return Use::SwitchOff;
    if (request.outputUser != 0)
        return Use::NotLocalUser;
    const Use use = head_relative::SampleUse(sample, request.generation, request.nowUs);
    if (use != Use::Ok)
        return use;
    if (!request.gameplay)
        return Use::NotGameplay;
    if (!std::isfinite(nativeField))
        return Use::NoHeading;
    float headHeading[3], nativeForward[3];
    if (!head_relative::HeldHeading(hold, sample.forward, kWorldUp, headHeading))
        return Use::NoHeading;
    const float nativeYaw = YawFromField(nativeField);
    ForwardOfYaw(nativeYaw, nativeForward);
    float turn = 0.0f;
    if (!head_relative::YawToHead(nativeForward, headHeading, kWorldUp, request.sign,
                                  request.offsetDeg * head_relative::kDegToRad, turn))
        return Use::NoHeading;
    const float yaw = WrapYaw(nativeYaw + turn);
    if (!std::isfinite(yaw))
        return Use::NoHeading;
    fieldOut = FieldFromYaw(yaw);
    if (headYawOut)
        *headYawOut = std::atan2(headHeading[1], headHeading[0]);
    return Use::Ok;
}

// ====================================================================================================================
// THE SOUND LISTENER FOLLOWS THE HEAD
//
// WHAT REACH DOES. HREK reach_tag_test.exe, sound_manager.cpp: the listener update (+0xC71E0) walks the four output
// users; for each it takes observer.cpp's camera (`observer_try_and_get_camera` +0x1344B0, then
// `observer_get_camera` +0x132C30: position +0x00, forward +0x28, up +0x34) and builds the user's listener from
// it: a matrix from that forward and up with that position, the velocity turned into the matrix's space, the
// location, and `valid`. (In a seat seen from outside it blends the camera with the occupant's head by the seat's
// "listener interpolation factor"; with no unit it takes the camera as it is.) The listeners are an array in the
// sound manager's globals, each delimited by the signatures 'lshd' and 'lsft' the game itself asserts on.
// Retail haloreach.dll has it at +0x28820 (the listeners at globals + 0xBC, 0x54 apart).
//
// The observer camera is the one this mod's aim steering points down the controller ray, while the world renders
// from a private head copy (src/common/reach_render_logic.h, kReachRenderCameraFromObserverRva). So the stock
// listener's ears turn with the GUN.
//
// THE SEAM: a detour on the listener update that lets the game build every listener and then, for output user 0
// only, replaces the three rows of its matrix with the head's forward, left and up, and re-expresses its stored
// velocity in that frame. The position, the location and every other listener are left as the game wrote them.
// Nothing else is read or written: not the observer camera, not the aim, nothing replicated.
//
// WHICH BUILD THE MIX USES. Retail calls the update from four places, and each one rebuilds all four listeners:
//   +0x25DA7  the sound manager's own update (+0x25C24; HREK +0xCE5BC, the only caller that passes the flag).
//             The same function then places every playing sound against the listeners it has just built, so
//             THIS is the build the mix is made from.
//   +0xBEC42, +0x576FB, +0xBD216: flag clear (HREK reaches the update there through two `xor ecx, ecx / jmp`
//             wrappers, +0xD00C0 and +0xCD550). By the shape of the code around each call (not proven one by
//             one): game_sound.cpp's timed update (+0xBEC28; HREK +0x1F4881), game.cpp's tick after the
//             players' update (+0x575AC; HREK +0x11E8FC, beside the asserts of game.cpp lines 1933-1934) and
//             a game_sound.cpp music-state function (+0xBD008; HREK +0x1F3642). Game-side sound code that
//             reads a listener's validity, location and position follows.
// All four go through the hooked function, so every build is turned before its caller reads a row of it, and
// which one the mix uses cannot change the result. NOT established statically: the thread each call runs on
// (the tick's and the sound manager's may differ), their order within a frame, and whether anything reads the
// ROWS between a flag-clear build and the sound manager's own. Dev builds count the builds by call site and
// thread, which answers it at run time.
//
// ADDR-HYGIENE: resolved + guarded. No address and no offset ships: the update's head (the hook), the store
// site inside it and the game's own signature writer are each matched uniquely; the globals pointer is read from
// two instructions that must agree; the stride from two functions that must agree; the matrix, velocity and valid
// offsets are read from the store instructions and must sit where the signature-delimited record puts them
// (matrix = header + 8, velocity = matrix + 0x34, footer = velocity + 12, valid = footer + 4). The rows inside
// the matrix (forward +4, left +0x10, up +0x1C, position +0x28) are structural: the store site's two callees
// must carry the builder's and the transform's bytes, and at EVERY use the live record must carry both
// signatures, scale 1, unit perpendicular rows and left = up x forward (ValueAgreement) before a byte is written.
// ====================================================================================================================

// The update's head (retail +0x28820): prologue / security cookie / mov [rsp+0x24], cl / call / xor r12d, r12d /
// test al, al / je / call / mov r9d, [rip + TLS index] / or r11, -1 / gs:[0x58] / mov edi, r12d /
// mov r15, [rip + GLOBALS] / mov r13d, r12d / .. / imul rsi, r13, STRIDE
inline constexpr const char* kListenerUpdateHeadPattern =
    "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D A8 98 FE FF FF "
    "48 81 EC 40 02 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 30 01 00 00 88 4C 24 24 E8 ?? ?? ?? ?? "
    "45 33 E4 84 C0 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B 0D ?? ?? ?? ?? 49 83 CB FF "
    "65 4C 8B 14 25 58 00 00 00 41 8B FC 4C 8B 3D ?? ?? ?? ?? 45 8B EC 89 44 24 2C 44 89 64 24 28 49 6B F5 ??";
inline constexpr size_t kListenerHeadBytes = 126;
inline constexpr size_t kListenerHeadGlobalsDisp = 106, kListenerHeadGlobalsNext = 110, kListenerHeadStride = 125;
// The store (retail +0x28C79): mov byte [rsi + r15 + VALID], 1 / .. / lea rcx, [r15 + MATRIX] /
// mov [rsi + r15 + VALID + 1], al / add rcx, rsi / lea r8, [up] / lea rdx, [forward] / call BUILDER /
// movsd xmm0, [position] / lea r8, [r15 + VELOCITY] / .. / lea rdx, [velocity] / movsd [rcx + 0x28], xmm0 /
// add r8, rsi / mov [rcx + 0x30], eax / call TRANSFORM / gs:[0x58] / or r11, -1 /
// mov [rsi + r15 + LOCATION], bx / mov r15, [rip + GLOBALS]
inline constexpr const char* kListenerStoreSitePattern =
    "42 C6 84 3E ?? ?? ?? ?? 01 7F ?? 45 33 C9 48 8D 54 24 ?? 45 33 C0 48 8D 4D ?? E8 ?? ?? ?? ?? 84 C0 B0 01 "
    "75 03 41 8A C4 49 8D 8F ?? ?? ?? ?? 42 88 84 3E ?? ?? ?? ?? 48 03 CE 4C 8D 44 24 ?? 48 8D 54 24 ?? "
    "E8 ?? ?? ?? ?? F2 0F 10 45 ?? 4D 8D 87 ?? ?? ?? ?? 8B 45 ?? 48 8D 54 24 ?? F2 0F 11 41 28 4C 03 C6 "
    "89 41 30 E8 ?? ?? ?? ?? 65 4C 8B 14 25 58 00 00 00 49 83 CB FF 66 42 89 9C 3E ?? ?? ?? ?? "
    "4C 8B 3D ?? ?? ?? ??";
inline constexpr size_t kListenerStoreBytes = 138;
inline constexpr size_t kStoreValidDisp = 4, kStoreMatrixDisp = 43, kStoreValidNextDisp = 51, kStoreCallBuilder = 68,
                        kStoreVelocityDisp = 81, kStoreCallTransform = 104, kStoreLocationDisp = 127,
                        kStoreGlobalsDisp = 134, kStoreGlobalsNext = 138;
// The game's own writer of the two signatures, in the sound manager's initialisation (retail +0x24E94):
// imul rax, rcx, STRIDE / inc r8d / mov edx, r15d / mov dword [rax + rbx + HEADER], 'lshd' /
// mov dword [rax + rbx + FOOTER], 'lsft'
inline constexpr const char* kListenerSignatureWriterPattern =
    "48 6B C1 ?? 41 FF C0 41 8B D7 C7 84 18 ?? ?? ?? ?? 64 68 73 6C C7 84 18 ?? ?? ?? ?? 74 66 73 6C";
inline constexpr size_t kListenerWriterBytes = 32;
inline constexpr size_t kWriterStride = 3, kWriterHeaderDisp = 13, kWriterFooterDisp = 24;
inline constexpr uint32_t kListenerHeaderSignature = 0x6C736864, kListenerFooterSignature = 0x6C736674;
// ADDR-HYGIENE: guarded. The two callees' sizes and where the compared windows sit are a measurement of this
// build, used only to PROVE the store's callees are these two functions (the bytes must match, and the caller
// requires each to be a function of its own in the image's function table). A build where they are not binds
// nothing.
// The builder the store calls (retail +0xA7BEC, 0xA2 bytes, no relocation in it): matrix(forward, up) --
// scale = 1 at +0, forward at +4, left = up x forward at +0x10, up at +0x1C, position zeroed at +0x28.
inline constexpr size_t kMatrixBuilderBytes = 0xA2, kMatrixBuilderTailAt = 0x83;
inline constexpr uint8_t kMatrixBuilderHead[25]{0x48, 0x83, 0xEC, 0x28, 0xC7, 0x01, 0x00, 0x00, 0x80, 0x3F, 0xF2, 0x0F, 0x10,
                                               0x02, 0xF2, 0x0F, 0x11, 0x41, 0x04, 0x8B, 0x42, 0x08, 0x89, 0x41, 0x0C};
inline constexpr uint8_t kMatrixBuilderTail[31]{0xF2, 0x41, 0x0F, 0x10, 0x00, 0xF2, 0x0F, 0x11, 0x41, 0x1C, 0x41,
                                               0x8B, 0x40, 0x08, 0x89, 0x41, 0x24, 0x33, 0xC0, 0x48, 0x89, 0x41,
                                               0x28, 0x89, 0x41, 0x30, 0x48, 0x83, 0xC4, 0x28, 0xC3};
// The transform the store calls for the velocity (retail +0xA8F7C, 0xCF bytes): out = (v . forward, v . left,
// v . up), the three dot products against the rows at +4, +0x10 and +0x1C. Its last 0x74 bytes are those dots.
inline constexpr size_t kVelocityTransformBytes = 0xCF, kVelocityTransformDotsAt = 0x5B;
inline constexpr uint8_t kVelocityTransformDots[0x74]{
    0x0F, 0x28, 0xD4, 0x0F, 0x28, 0xCE, 0xF3, 0x0F, 0x59, 0x49, 0x0C, 0x0F, 0x28, 0xDC, 0x49, 0x8B, 0xC0, 0xF3, 0x0F, 0x59,
    0x51, 0x08, 0x0F, 0x28, 0xC5, 0xF3, 0x0F, 0x59, 0x41, 0x04, 0xF3, 0x0F, 0x58, 0xD0, 0x0F, 0x28, 0xC5, 0xF3, 0x0F, 0x58,
    0xD1, 0x0F, 0x28, 0xCE, 0xF3, 0x41, 0x0F, 0x11, 0x10, 0xF3, 0x0F, 0x59, 0x59, 0x14, 0xF3, 0x0F, 0x59, 0x41, 0x10, 0xF3,
    0x0F, 0x59, 0x49, 0x18, 0xF3, 0x0F, 0x58, 0xD8, 0xF3, 0x0F, 0x58, 0xD9, 0xF3, 0x41, 0x0F, 0x11, 0x58, 0x04, 0xF3, 0x0F,
    0x59, 0x61, 0x20, 0xF3, 0x0F, 0x59, 0x71, 0x24, 0xF3, 0x0F, 0x59, 0x69, 0x1C, 0xF3, 0x0F, 0x58, 0xE5, 0xF3, 0x0F, 0x58,
    0xE6, 0x0F, 0x28, 0x34, 0x24, 0xF3, 0x41, 0x0F, 0x11, 0x60, 0x08, 0x48, 0x83, 0xC4, 0x18, 0xC3};
// ADDR-HYGIENE: structural + guarded. Inside the matrix: the builder's bytes above put the rows at these places,
// and every use checks the live record (scale 1, unit perpendicular rows, left = up x forward) before writing.
inline constexpr size_t kMatrixScale = 0x00, kMatrixForward = 0x04, kMatrixLeft = 0x10, kMatrixUp = 0x1C,
                        kMatrixPosition = 0x28, kMatrixBytes = 0x34;

struct ListenerSites
{
    uint32_t stride = 0;          // one listener (retail and HREK 0x54)
    uint32_t headerOffset = 0;    // listener 0's 'lshd' in the sound manager's globals (retail and HREK 0xBC)
    uint32_t matrixOffset = 0;    // its matrix (0xC4)
    uint32_t velocityOffset = 0;  // its velocity in the matrix's space (0xF8)
    uint32_t footerOffset = 0;    // its 'lsft' (0x104)
    uint32_t validOffset = 0;     // "a listener was built this update" (0x108)
    int64_t globalsRva = 0;       // where the image keeps the pointer to the sound manager's globals
    int64_t builderRva = 0;       // matrix(forward, up)
    int64_t transformRva = 0;     // the velocity into the matrix's space
};
enum class ListenerBind : uint8_t
{
    Bound = 0,
    UpdateHeadMissing,   // the listener update's head: no match, or more than one
    StoreSiteMissing,    // its store of a listener: no match, or more than one
    StoreOutsideUpdate,  // the store is not inside the function whose head matched
    WriterMissing,       // the game's writer of the listener signatures: no match, or more than one
    GlobalsMismatch,     // the head and the store load the globals from different places
    StrideMismatch,      // the update and the signature writer disagree on a listener's size
    RecordMismatch,      // the stored fields do not sit where the signature-delimited record puts them
    Implausible,         // an offset out of range or misaligned
    HelperMismatch,      // the store's callees are not the matrix builder and the transform
    Count
};
inline const char* ListenerBindName(ListenerBind why)
{
    switch (why)
    {
    case ListenerBind::Bound: return "bound";
    case ListenerBind::UpdateHeadMissing: return "the listener update's head is missing or ambiguous";
    case ListenerBind::StoreSiteMissing: return "the listener store is missing or ambiguous";
    case ListenerBind::StoreOutsideUpdate: return "the listener store is not inside the matched update";
    case ListenerBind::WriterMissing: return "the game's listener-signature writer is missing or ambiguous";
    case ListenerBind::GlobalsMismatch: return "the update loads the sound globals from two different places";
    case ListenerBind::StrideMismatch: return "the update and the signature writer disagree on a listener's size";
    case ListenerBind::RecordMismatch: return "the stored fields are not where the listener record puts them";
    case ListenerBind::Implausible: return "the decoded listener offsets are implausible";
    case ListenerBind::HelperMismatch: return "the store's callees are not the matrix builder and the transform";
    default: return "?";
    }
}
// `head`, `store`, `writer`: the unique matches (null = missing or ambiguous); `storeFunctionRva`: the beginning
// of the function `store` lies in, from the image's own function table.
inline ListenerBind DecodeListenerSites(const uint8_t* head, int64_t headRva, const uint8_t* store, int64_t storeRva,
                                        int64_t storeFunctionRva, const uint8_t* writer, ListenerSites& out)
{
    if (!head)
        return ListenerBind::UpdateHeadMissing;
    if (!store)
        return ListenerBind::StoreSiteMissing;
    if (storeFunctionRva != headRva || storeRva <= headRva)
        return ListenerBind::StoreOutsideUpdate;
    if (!writer)
        return ListenerBind::WriterMissing;
    const int64_t globals = RipRva(head, kListenerHeadGlobalsDisp, kListenerHeadGlobalsNext, headRva);
    if (globals <= 0 || globals != RipRva(store, kStoreGlobalsDisp, kStoreGlobalsNext, storeRva))
        return ListenerBind::GlobalsMismatch;
    const uint32_t stride = head[kListenerHeadStride];
    if (!stride || stride != writer[kWriterStride])
        return ListenerBind::StrideMismatch;
    const uint32_t header = ReadU32(writer + kWriterHeaderDisp), footer = ReadU32(writer + kWriterFooterDisp);
    const uint32_t valid = ReadU32(store + kStoreValidDisp), matrix = ReadU32(store + kStoreMatrixDisp);
    const uint32_t velocity = ReadU32(store + kStoreVelocityDisp), location = ReadU32(store + kStoreLocationDisp);
    if (!header || header >= 0x10000 || (header & 3) || stride < 0x50 || stride > 0x200 || (stride & 3) ||
        footer <= header || footer >= header + stride)
        return ListenerBind::Implausible;
    if (matrix != header + 8 || velocity != matrix + kMatrixBytes || footer != velocity + 12 ||
        valid != footer + 4 || ReadU32(store + kStoreValidNextDisp) != valid + 1 || location != header + 4 ||
        valid + 2 > header + stride)
        return ListenerBind::RecordMismatch;
    out.stride = stride;
    out.headerOffset = header;
    out.matrixOffset = matrix;
    out.velocityOffset = velocity;
    out.footerOffset = footer;
    out.validOffset = valid;
    out.globalsRva = globals;
    out.builderRva = CallRva(store, kStoreCallBuilder, storeRva);
    out.transformRva = CallRva(store, kStoreCallTransform, storeRva);
    if (out.builderRva <= 0 || out.transformRva <= 0 || out.builderRva == out.transformRva)
        return ListenerBind::Implausible;
    return ListenerBind::Bound;
}
// `builder`: kMatrixBuilderBytes at sites.builderRva; `transform`: kVelocityTransformBytes at sites.transformRva.
inline ListenerBind CheckListenerHelpers(const uint8_t* builder, const uint8_t* transform)
{
    if (!builder || !transform)
        return ListenerBind::HelperMismatch;
    if (std::memcmp(builder, kMatrixBuilderHead, sizeof(kMatrixBuilderHead)) != 0 ||
        std::memcmp(builder + kMatrixBuilderTailAt, kMatrixBuilderTail, sizeof(kMatrixBuilderTail)) != 0 ||
        std::memcmp(transform + kVelocityTransformDotsAt, kVelocityTransformDots, sizeof(kVelocityTransformDots)) != 0)
        return ListenerBind::HelperMismatch;
    return ListenerBind::Bound;
}

// ---- the orientation -------------------------------------------------------------------------------------------
struct ListenerBasis
{
    float forward[3]{}, left[3]{}, up[3]{};
};
// The game's own third row: left = up x forward (the builder at retail +0xA7BEC).
inline void LeftOf(const float forward[3], const float up[3], float left[3]) { head_relative::Cross(up, forward, left); }
// The head as a listener's rows: forward as given (unit length), up made perpendicular to it, left by the
// game's rule. False when the head's forward and up do not span a plane.
inline bool HeadBasis(const float headForward[3], const float headUp[3], ListenerBasis& out)
{
    using namespace head_relative;
    if (!Finite3(headForward) || !Finite3(headUp))
        return false;
    const float forwardLength = Length(headForward);
    if (!(forwardLength > 1.0e-6f))
        return false;
    float forward[3]{headForward[0] / forwardLength, headForward[1] / forwardLength, headForward[2] / forwardLength};
    const float along = Dot(headUp, forward);
    float up[3]{headUp[0] - forward[0] * along, headUp[1] - forward[1] * along, headUp[2] - forward[2] * along};
    const float upLength = Length(up);
    if (!(upLength > 1.0e-3f * (Length(headUp) > 1.0f ? Length(headUp) : 1.0f)))
        return false;
    for (int i = 0; i < 3; ++i)
    {
        up[i] /= upLength;
        out.forward[i] = forward[i];
        out.up[i] = up[i];
    }
    LeftOf(out.forward, out.up, out.left);
    return true;
}
// Is this what the game's builder writes: scale 1, unit perpendicular forward and up, left = up x forward?
inline bool BasisAsBuilt(float scale, const ListenerBasis& basis)
{
    using namespace head_relative;
    constexpr float tolerance = 2.0e-3f;
    if (!(scale == 1.0f) || !Finite3(basis.forward) || !Finite3(basis.left) || !Finite3(basis.up))
        return false;
    if (std::fabs(Length(basis.forward) - 1.0f) > tolerance || std::fabs(Length(basis.up) - 1.0f) > tolerance ||
        std::fabs(Dot(basis.forward, basis.up)) > tolerance)
        return false;
    float left[3];
    LeftOf(basis.forward, basis.up, left);
    return std::fabs(left[0] - basis.left[0]) <= tolerance && std::fabs(left[1] - basis.left[1]) <= tolerance &&
           std::fabs(left[2] - basis.left[2]) <= tolerance;
}
// A world vector in a listener's space, as the game's transform computes it (retail +0xA8F7C, scale 1).
inline void ToListenerSpace(const ListenerBasis& basis, const float world[3], float out[3])
{
    const float x = head_relative::Dot(world, basis.forward);
    const float y = head_relative::Dot(world, basis.left);
    const float z = head_relative::Dot(world, basis.up);
    out[0] = x;
    out[1] = y;
    out[2] = z;
}
// And back: the world vector whose listener-space image is `local` (the rows are orthonormal).
inline void FromListenerSpace(const ListenerBasis& basis, const float local[3], float out[3])
{
    for (int i = 0; i < 3; ++i)
        out[i] = basis.forward[i] * local[0] + basis.left[i] * local[1] + basis.up[i] * local[2];
}

enum class ListenerUse : uint8_t
{
    Used = 0,
    SwitchOff,        // the player's switch is off
    NoListener,       // the game built no listener for output user 0 in this update
    NotAListener,     // the record does not carry the listener signatures
    UnexpectedBasis,  // its matrix is not what the game's builder writes
    NoSample,         // no frame has published a head yet, or the head view was not live in it
    OtherGeneration,  // the sample belongs to another load of the title
    FromTheFuture,    // the sample's clock is ahead of this one's
    Stale,            // older than the age limit
    NotGameplay,      // a cinematic held the camera in that frame
    NoHeadBasis,      // the head's forward and up do not make a frame
    Count
};
inline const char* ListenerUseName(ListenerUse use)
{
    switch (use)
    {
    case ListenerUse::Used: return "used";
    case ListenerUse::SwitchOff: return "switch off";
    case ListenerUse::NoListener: return "the game built no listener";
    case ListenerUse::NotAListener: return "not a listener record";
    case ListenerUse::UnexpectedBasis: return "unexpected listener matrix";
    case ListenerUse::NoSample: return "no head sample";
    case ListenerUse::OtherGeneration: return "another generation";
    case ListenerUse::FromTheFuture: return "sample from the future";
    case ListenerUse::Stale: return "stale head sample";
    case ListenerUse::NotGameplay: return "not gameplay";
    case ListenerUse::NoHeadBasis: return "no head frame";
    default: return "?";
    }
}
struct ListenerRequest
{
    bool switchOn = false;    // g_config.audio_follows_head, read at this use
    bool gameplay = false;    // the frame the head sample came from was not a cinematic's
    uint32_t generation = 0;  // the title generation now
    uint64_t nowUs = 0;       // the sight clock now
};
// Output user 0's listener as the game just wrote it.
struct ListenerRecord
{
    uint32_t header = 0, footer = 0;
    uint8_t valid = 0;
    float scale = 0.0f;
    ListenerBasis basis;
    float velocity[3]{};  // in the listener's space
};
// The rows and the velocity output user 0's listener should carry. Used = `basisOut` is the head's frame and
// `velocityOut` the same world velocity seen from it; anything else names why the game's listener stands, and
// both outputs are untouched. The position is not an input: it is never changed.
inline ListenerUse ListenerOrientation(const ListenerRequest& request, const head_relative::HeadSample& sample,
                                       const ListenerRecord& native, ListenerBasis& basisOut, float velocityOut[3])
{
    using head_relative::Use;
    if (!request.switchOn)
        return ListenerUse::SwitchOff;
    if (native.header != kListenerHeaderSignature || native.footer != kListenerFooterSignature)
        return ListenerUse::NotAListener;
    if (native.valid != 1)
        return ListenerUse::NoListener;
    if (!BasisAsBuilt(native.scale, native.basis) || !head_relative::Finite3(native.velocity))
        return ListenerUse::UnexpectedBasis;
    switch (head_relative::SampleUse(sample, request.generation, request.nowUs))
    {
    case Use::Ok: break;
    case Use::OtherGeneration: return ListenerUse::OtherGeneration;
    case Use::FromTheFuture: return ListenerUse::FromTheFuture;
    case Use::Stale: return ListenerUse::Stale;
    case Use::NoHeading: return ListenerUse::NoHeadBasis;
    default: return ListenerUse::NoSample;
    }
    if (!request.gameplay)
        return ListenerUse::NotGameplay;
    ListenerBasis head;
    if (!HeadBasis(sample.forward, sample.up, head))
        return ListenerUse::NoHeadBasis;
    float world[3], local[3];
    FromListenerSpace(native.basis, native.velocity, world);
    ToListenerSpace(head, world, local);
    if (!head_relative::Finite3(local))
        return ListenerUse::NoHeadBasis;
    basisOut = head;
    velocityOut[0] = local[0];
    velocityOut[1] = local[1];
    velocityOut[2] = local[2];
    return ListenerUse::Used;
}
} // namespace reach_head_relative
