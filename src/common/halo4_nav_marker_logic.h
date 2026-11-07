// HALO 4: OBJECTIVE MARKERS FOLLOW THE HEAD, NOT THE GUN -- the pure half (no game, engine or OpenXR includes).
// The runtime half is src/dll/halo4_nav_markers.inl.
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED. The Halo 4 editing kit is not on the machine this was
// written on, so everything below was read from retail halo4.dll (SHA-256 7C53E7D5...FA84) alone and every name is
// ours (docs/EDITING-KIT-EVIDENCE.md, the Halo 4 rule). As with the motion tracker (halo4_head_relative_logic.h),
// the one fact the bytes cannot settle -- that the camera the markers are placed with IS the game's aim camera --
// is proven at RUN TIME before anything is changed (MarkerProof, below).
//
// WHAT RETAIL DOES (read statically).
// Halo 4 lays its HUD out in its interface pass, not inside the picture each eye is drawn with: a HUD component's
// method (+0x4224F0, reached only through a table) calls the per-user layout +0x4CF2C4, which builds ONE camera
//     camera = render_camera_from_observer(engine TLS [+0x680] + user * 0x428 + 0x15C)   ; +0x40B0B8
// and hands it to nine widget groups (+0x4D788C .. +0x4DA6A0); a second family (+0x3F25BC, under +0x3F3808) uses
// the player-view stack element's camera instead. Both place every world-anchored widget through one function,
//     +0x40B5F4 (point, clamp, camera, projection, bounds, ...)
// which projects the point (+0x40B180: in front of the camera and on screen, else a direction toward it) and, for a
// clamped widget, pulls it to the edge of the bounds and returns the arrow's angle. The laid-out HUD is then
// replayed into each eye's picture (docs/HALO4-CUI-EVIDENCE.md: the CUI arrives inside the captured scene target).
//
// In this mod the observer camera is the AIM between frames: the stereo transaction writes each eye's camera into
// it only around that eye's render and restores the game's own afterwards (game.cpp, Halo4RestoreMonoCamera), and
// the stack element is rebuilt from the restored observer. So every marker was placed as if the player looked down
// the gun: on screen only while the gun pointed near the objective, drawn where the gun would see it, and its
// off-screen arrow pointing from the gun.
//
// THE SEAM. A detour on +0x40B5F4. Outside the eye passes, once the camera it is handed has been proven to be the
// aim camera, the call is given a copy of that camera with the head's position, forward and up (and, under
// nav_markers_on_target, the vertical field of view the eyes were last drawn with, the same single field the
// stereo transaction widens), and a projection the game's own builder (+0x38F658) makes from that copy. The game's
// camera, its projection and the observer are never written. Inside the eye passes nothing is changed.
//
// ADDR-HYGIENE: resolved + guarded. Two patterns, each matched exactly once in the pinned image, each the beginning
// of a function by the image's own table: the marker projector's head through its call of the point projection,
// and the whole camera build (its observer constants 0x680, 0x428 and 0x15C are literal in it). The projection
// builder is the build's last call, decoded from its rel32, and must begin a function too.
#pragma once

#include "head_relative_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace halo4_nav_markers
{
// The marker projector's head (retail +0x40B5F4), through its call of the point projection (+0x40B180).
inline constexpr const char* kProjectorPattern =
    "48 8B C4 48 89 58 08 48 89 70 18 55 57 41 56 48 8B EC 48 83 EC 60 8A DA 0F 29 70 D8 48 8D 55 D8 0F 29 78 C8 "
    "48 89 54 24 28 49 8B C0 48 8D 55 D0 45 32 F6 48 89 54 24 20 4D 8B C1 48 8B D0 44 88 75 28 40 32 FF "
    "E8 ?? ?? ?? ??";
inline constexpr size_t kProjectorBytes = 74;
inline constexpr size_t kProjectorPointCall = 69;
// The per-user camera build (retail +0x40B0B8): the window count, the observer of `user` from the engine's TLS
// block (+0x680, 0x428 apart, +0x15C), the converter, the window, its bounds, then the projection.
inline constexpr const char* kCameraBuildPattern =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 56 48 83 EC 20 49 8B E9 4C 63 F2 49 8B D8 8B F9 "
    "E8 ?? ?? ?? ?? 8B F0 3B F8 7D 7B 44 8B 1D ?? ?? ?? ?? 48 8B CB 65 4C 8B 14 25 58 00 00 00 B8 80 06 00 00 "
    "4D 69 CE 28 04 00 00 4F 8B 14 DA 49 81 C1 5C 01 00 00 49 8B 14 02 49 03 D1 E8 ?? ?? ?? ?? 48 8B CB "
    "E8 ?? ?? ?? ?? 0F BF 4B 36 0F BF 43 32 2B C8 85 C9 7E 2F 0F BF 4B 34 4C 8D 43 30 41 0F BF 00 2B C8 85 C9 "
    "7E 1D 4C 8D 4B 38 8B D6 8B CF E8 ?? ?? ?? ?? 0F 57 DB 33 D2 48 8B CB 4C 8B C5 E8 ?? ?? ?? ?? "
    "48 8B 5C 24 30 48 8B 6C 24 38 48 8B 74 24 40 48 8B 7C 24 48 48 83 C4 20 41 5E C3";
inline constexpr size_t kCameraBuildBytes = 197;
inline constexpr size_t kCameraBuildConverterCall = 0x60, kCameraBuildProjectionCall = 0xA5;

// The render camera the projector reads (the player-view stack element's layout, halo4_render_logic.h): position
// +0x00, forward +0x0C, up +0x18, vertical field of view +0x28; the projection builder reads up to +0x84. The
// projection it writes ends at +0xC4 (its last store), inside the 0xD0 the per-user layout reserves for it.
inline constexpr size_t kCameraBytes = 0x88;
inline constexpr size_t kProjectionBytes = 0xD0;
inline constexpr size_t kCameraPosition = 0x00, kCameraForward = 0x0C, kCameraUp = 0x18, kCameraVerticalFov = 0x28;

inline int32_t ReadI32(const uint8_t* at)
{
    int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

struct Sites
{
    int64_t projectorRva = 0;   // the hook
    int64_t projectionRva = 0;  // the projection builder the camera build calls last
};
enum class Bind : uint8_t
{
    Bound = 0,
    ProjectorMissing,     // no match, more than one, or not a function's beginning
    CameraBuildMissing,   // the same, for the camera build
    NotACall,             // a decoded site is not a call
    BuilderNotFunction,   // the projection builder does not begin a function
    Count
};
inline const char* BindName(Bind why)
{
    switch (why)
    {
    case Bind::Bound: return "bound";
    case Bind::ProjectorMissing: return "the marker projector is missing, ambiguous or not a function";
    case Bind::CameraBuildMissing: return "the per-user camera build is missing, ambiguous or not a function";
    case Bind::NotACall: return "a decoded site is not a call";
    case Bind::BuilderNotFunction: return "the projection builder does not begin a function";
    default: return "?";
    }
}
// Each pointer: the bytes at that pattern's unique match (null = missing or ambiguous). `*IsFunction`: the image's
// function table begins a function there. `builderIsFunction(rva)` is asked about the decoded projection builder.
template <typename IsFunction>
inline Bind DecodeSites(const uint8_t* projector, int64_t projectorRva, bool projectorIsFunction,
                        const uint8_t* build, int64_t buildRva, bool buildIsFunction, IsFunction builderIsFunction,
                        Sites& out)
{
    if (!projector || projectorRva <= 0 || !projectorIsFunction)
        return Bind::ProjectorMissing;
    if (!build || buildRva <= 0 || !buildIsFunction)
        return Bind::CameraBuildMissing;
    if (projector[kProjectorPointCall] != 0xE8 || build[kCameraBuildConverterCall] != 0xE8 ||
        build[kCameraBuildProjectionCall] != 0xE8)
        return Bind::NotACall;
    const int64_t builder = buildRva + static_cast<int64_t>(kCameraBuildProjectionCall) + 5 +
                            ReadI32(build + kCameraBuildProjectionCall + 1);
    if (builder <= 0 || !builderIsFunction(builder))
        return Bind::BuilderNotFunction;
    out.projectorRva = projectorRva;
    out.projectionRva = builder;
    return Bind::Bound;
}

// ---- THE PROOF TAKEN AT RUN TIME -------------------------------------------------------------------------------
// What the bytes cannot say without the kit is whose camera the projector is handed. Before the first substitution
// the camera's heading must agree with a reference read another way: the game's own camera of the last frame
// before the head pose was applied (the aim; the stereo transaction publishes it, halo4_head_relative.inl).
// kProofSamples agreeing calls, the reference moving across them by more than kProofSpreadRad so that no constant
// heading can pass; the two are read at different moments while the hand moves, so a miss between the tolerance
// and the disagreement bound counts for nothing. Once proven, kProofWithdrawRun disagreements in a row withdraw
// it. (Thresholds of the proof, not addresses or offsets.)
inline constexpr uint32_t kProofSamples = 30;
inline constexpr uint32_t kProofWithdrawRun = 120;
inline constexpr float kProofToleranceRad = 3.0f * head_relative::kDegToRad;
inline constexpr float kProofSpreadRad = 10.0f * head_relative::kDegToRad;
inline constexpr float kProofDisagreeRad = 15.0f * head_relative::kDegToRad;
static_assert(kProofSpreadRad > 2.0f * kProofToleranceRad, "a constant must not be able to agree across the spread");

inline float WrapPi(float angle)
{
    while (angle > head_relative::kPi)
        angle -= 2.0f * head_relative::kPi;
    while (angle < -head_relative::kPi)
        angle += 2.0f * head_relative::kPi;
    return angle;
}
// A forward vector's heading about Halo's world up (+Z). False when it has none (non-finite, or nearly vertical).
inline bool Heading(const float forward[3], float& yaw)
{
    if (!head_relative::Finite3(forward))
        return false;
    const float horizontal = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
    const float length = std::sqrt(horizontal * horizontal + forward[2] * forward[2]);
    if (!(length > 1.0e-6f) || std::atan2(std::fabs(forward[2]), horizontal) >
                                   head_relative::kMaxElevationDeg * head_relative::kDegToRad)
        return false;
    yaw = std::atan2(forward[1], forward[0]);
    return std::isfinite(yaw);
}

struct MarkerProof
{
    uint32_t agreed = 0;
    float anchorYaw = 0.0f;
    float low = 0.0f, high = 0.0f;
    uint32_t disagreed = 0;
    bool proven = false;
};
enum class ProofStep : uint8_t
{
    NoReference,  // nothing to compare with: neither for nor against
    Agreed,
    Lagging,      // between the tolerance and the disagreement bound
    Disagreed,
    Proven,       // this call completed the proof
    Withdrawn,    // this call withdrew it
    Count
};
inline const char* ProofStepName(ProofStep step)
{
    switch (step)
    {
    case ProofStep::NoReference: return "no reference";
    case ProofStep::Agreed: return "agreed";
    case ProofStep::Lagging: return "lagging";
    case ProofStep::Disagreed: return "disagreed";
    case ProofStep::Proven: return "proven";
    case ProofStep::Withdrawn: return "withdrawn";
    default: return "?";
    }
}
// `cameraForward`: the camera the projector was handed; `aimForward`: the published aim of the last frame.
inline ProofStep StepProof(MarkerProof& proof, const float cameraForward[3], bool referenceUsable,
                           const float aimForward[3])
{
    float cameraYaw = 0.0f, aimYaw = 0.0f;
    if (!referenceUsable || !Heading(cameraForward, cameraYaw) || !Heading(aimForward, aimYaw))
        return ProofStep::NoReference;
    const float miss = std::fabs(WrapPi(cameraYaw - aimYaw));
    if (miss <= kProofToleranceRad)
    {
        proof.disagreed = 0;
        if (proof.proven)
            return ProofStep::Agreed;
        if (!proof.agreed)
        {
            proof.anchorYaw = aimYaw;
            proof.low = proof.high = 0.0f;
        }
        const float moved = WrapPi(aimYaw - proof.anchorYaw);
        if (moved < proof.low)
            proof.low = moved;
        if (moved > proof.high)
            proof.high = moved;
        ++proof.agreed;
        if (proof.agreed >= kProofSamples && proof.high - proof.low >= kProofSpreadRad)
        {
            proof.proven = true;
            return ProofStep::Proven;
        }
        return ProofStep::Agreed;
    }
    if (miss < kProofDisagreeRad)
        return ProofStep::Lagging;
    if (!proof.proven)
    {
        proof = MarkerProof{};
        return ProofStep::Disagreed;
    }
    if (++proof.disagreed >= kProofWithdrawRun)
    {
        proof = MarkerProof{};
        return ProofStep::Withdrawn;
    }
    return ProofStep::Disagreed;
}

// ---- the verdict -----------------------------------------------------------------------------------------------
enum class Use : uint8_t
{
    Used = 0,
    InEyePass,     // inside an eye's render: the camera is that eye's already
    SwitchOff,     // the player's switch is off
    Unproven,      // the camera has not (yet) been proven to be the aim camera
    NotGameplay,   // a cinematic, a menu, or no coherent head read
    NoSample,      // no live head published yet
    OtherGeneration,
    FromTheFuture,
    Stale,
    NoHead,        // non-finite head pose
    Fault,         // a camera or projection could not be read or built
    Count
};
inline const char* UseName(Use use)
{
    switch (use)
    {
    case Use::Used: return "given the head";
    case Use::InEyePass: return "inside an eye's render";
    case Use::SwitchOff: return "switch off";
    case Use::Unproven: return "camera not yet proven to be the aim";
    case Use::NotGameplay: return "not gameplay";
    case Use::NoSample: return "no head sample";
    case Use::OtherGeneration: return "another generation";
    case Use::FromTheFuture: return "sample from the future";
    case Use::Stale: return "stale head sample";
    case Use::NoHead: return "no head pose";
    case Use::Fault: return "camera fault";
    default: return "?";
    }
}
// Everything but the proof and the fault. `read`: a coherent head frame was read; `gameplay`: the frame says the
// player had control.
inline Use HeadUse(bool switchOn, bool inEyePass, bool read, bool gameplay, const head_relative::HeadSample& sample,
                   const float position[3], uint32_t generation, uint64_t nowUs)
{
    if (inEyePass)
        return Use::InEyePass;
    if (!switchOn)
        return Use::SwitchOff;
    if (!read || !gameplay)
        return Use::NotGameplay;
    switch (head_relative::SampleUse(sample, generation, nowUs))
    {
    case head_relative::Use::Ok: break;
    case head_relative::Use::NoSample: return Use::NoSample;
    case head_relative::Use::OtherGeneration: return Use::OtherGeneration;
    case head_relative::Use::FromTheFuture: return Use::FromTheFuture;
    case head_relative::Use::Stale: return Use::Stale;
    default: return Use::NoHead;
    }
    return head_relative::Finite3(position) ? Use::Used : Use::NoHead;
}
// The head written into a copy of the projector's camera: pose always, the vertical field of view only when `fov`
// is a usable angle (0 keeps the game's).
inline void WriteHeadCamera(uint8_t* camera, const float position[3], const float forward[3], const float up[3],
                            float fov)
{
    std::memcpy(camera + kCameraPosition, position, sizeof(float) * 3);
    std::memcpy(camera + kCameraForward, forward, sizeof(float) * 3);
    std::memcpy(camera + kCameraUp, up, sizeof(float) * 3);
    if (std::isfinite(fov) && fov > 0.0001f && fov < 3.14149f)
        std::memcpy(camera + kCameraVerticalFov, &fov, sizeof(fov));
}
} // namespace halo4_nav_markers
