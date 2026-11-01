// HALO 4: THE MOTION TRACKER FOLLOWS THE HEAD, the pure half (no game, engine or OpenXR includes).
//
// EVIDENCE CLASS: RETAIL-DERIVED LEAD, NOT H4EK-VERIFIED. This binding was made without the Halo 4 editing kit
// (no editing-kit symbols), and docs/EDITING-KIT-EVIDENCE.md's Halo 4 rule lets halo4.dll only match and verify what
// the kit explains. Everything below was read from retail halo4.dll (SHA-256 7C53E7D5...FA84) alone, so every name
// is ours and nothing here is a kit fact. Two things stand in for the kit:
//   * the hook proves the one doubtful fact at RUN TIME before it changes anything (ArgumentProof, below);
//   * docs/HALO4-HEAD-RELATIVE-2026-10-06.md lists, per binding, the kit artefact that would settle it.
// Other titles' lanes are a lead for where to look and are not evidence for anything here.
//
// WHAT RETAIL DOES (tools/verify-halo4-head-relative.py re-derives every address from the patterns here).
// A HUD component's method (+0x423648, reached only through a table slot) runs when its state byte is 2 and its
// user index is at most 3:
//     flag == 0 :  call +0x4CF7A4 (component + 0x78, user)        ; the player-centred build
//     flag != 0 :  jump +0x4CF97C (component + 0x78, user)        ; a build centred on another object
// The player-centred build reads one yaw,
//     yaw = [engine TLS + 0xB0] + user' * 0x108 + 0x8F8           ; TLS + 0xB0 is the player-control block the
//                                                                 ; direct drive binds for this title
// adds pi/2, and passes the sum as the ELEVENTH argument of the builder (+0x4CFCB4), which stores it in the
// tracker state (+0xC) and hands it to five functions that place the contacts. Each contact is placed by
//     a = -heading;  x = sinf(a) * dy - cosf(a) * dx;  y = sinf(a) * dx + cosf(a) * dy      (+0x40B8FB..+0x40B943)
// so with heading = yaw + pi/2: x = offset . right, y = -(offset . forward).
// The other build passes a different value ([player + 0x364] + pi/2) to the same builder.
//
// THE SEAM: a detour on the builder with its exact eleven-argument signature. For the ONE call that returns to
// the player-centred build it replaces the heading argument with the head's; every other call is forwarded
// untouched. No game memory is written by this lane at all: not player control, not the aim, not a camera.
//
// ADDR-HYGIENE: resolved + guarded. No address and no offset ships: the component method, the heading's
// read-add-pass site and the builder's head are each matched uniquely; the method's call must be the function
// the site lies in, the site's call must land on the matched builder head, the constant must be pi/2, and the
// state-to-user distance is read from the method's own operands. And at run time the argument must AGREE with
// the aim yaw this mod's own direct drive is writing, over a reference that moved, before one value is replaced.
#pragma once

#include "head_relative_logic.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace halo4_head_relative
{
inline constexpr float kHalfPi = head_relative::kPi * 0.5f;
inline constexpr float kTwoPi = head_relative::kPi * 2.0f;
inline constexpr float kWorldUp[3]{0.0f, 0.0f, 1.0f};

// ---- the sites -------------------------------------------------------------------------------------------------
// ADDR-HYGIENE: resolved (the comment above). The numbers beside each pattern are positions INSIDE that matched
// pattern, never image addresses.
// The component's method (retail +0x423648, the whole function): sub rsp, 0x28 / cmp byte [rcx + 0x28], 2 / jne /
// mov edx, [rcx + USER] / cmp edx, 3 / ja / cmp dword [rcx + 0x74], 0 / je / add rcx, STATE / add rsp, 0x28 /
// jmp OTHER BUILD / add rcx, STATE / call PLAYER BUILD / add rsp, 0x28 / ret
inline constexpr const char* kTrackerMethodPattern =
    "48 83 EC 28 80 79 28 02 75 ?? 8B 51 70 83 FA 03 77 ?? 83 79 74 00 74 ?? 48 83 C1 78 48 83 C4 28 E9 ?? ?? ?? ?? "
    "48 83 C1 78 E8 ?? ?? ?? ?? 48 83 C4 28 C3";
inline constexpr size_t kMethodBytes = 51;
inline constexpr size_t kMethodUserDisp = 12, kMethodStateOther = 27, kMethodJumpOther = 32, kMethodStatePlayer = 40,
                        kMethodCallPlayer = 41;
// The player-centred build, from its read of the yaw to its test of the builder's answer (retail +0x4CF841):
//   imul rcx, rbp, 0x108 / mov edx, 0xB0 / mov rax, [rdx + r15] / movss xmm2, [rax + rcx + 0x8F8]
//   mov rcx, rdi / addss xmm2, [rip + HALF PI] / call .. (four calls, none of them given xmm2)
//   movss [rsp + 0x50], xmm2                 ; the eleventh argument
//   .. / call BUILDER / test al, al / jne
inline constexpr const char* kTrackerHeadingSitePattern =
    "48 69 CD 08 01 00 00 BA B0 00 00 00 4A 8B 04 3A F3 0F 10 94 08 F8 08 00 00 48 8B CF F3 0F 58 15 ?? ?? ?? ?? "
    "E8 ?? ?? ?? ?? F3 41 0F 10 5E 10 83 CA FF 8B 4F 2C 0F 28 E0 F3 0F 59 1D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0 "
    "0F 84 ?? ?? ?? ?? 8B 40 24 48 8B CF 83 F8 FF 41 B8 18 00 00 00 0F 44 47 2C 0F B7 C0 4F 8B 04 38 48 8D 14 40 "
    "49 8B 40 50 4C 8B 44 D0 10 48 8D 54 24 70 41 0F B7 80 BE 01 00 00 F2 42 0F 10 44 00 28 42 8B 44 00 30 "
    "F2 0F 11 44 24 60 89 44 24 68 E8 ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 8B CD 49 81 C0 C0 29 00 00 "
    "C7 43 10 00 00 80 3F E8 ?? ?? ?? ?? F3 0F 11 54 24 50 4C 8B C8 48 8D 44 24 60 8B D6 48 89 44 24 48 48 8B CB "
    "F3 0F 11 5C 24 40 48 8D 44 24 70 F3 0F 11 64 24 38 F3 0F 11 64 24 30 48 89 44 24 28 E8 ?? ?? ?? ?? 84 C0 75 ??";
inline constexpr size_t kSiteBytes = 246;
inline constexpr size_t kSiteStride = 3, kSiteTlsBlock = 8, kSiteYaw = 21, kSiteHalfPiDisp = 32, kSiteHalfPiNext = 36,
                        kSiteHeadingSlot = 190, kSiteCallBuilder = 237, kSiteReturn = 242;
// The builder's head (retail +0x4CFCB4, the hook), through its store of the heading: prologue (five pushes and
// 0x80: its entry rsp is rsp + 0xA8) / .. / mov rbp, [rsp + 0xF8] / .. / movss xmm8, [rsp + 0x100] / .. /
// movss [rcx + 0xC], xmm8. rsp + 0x100 is entry + 0x58: with the fifth argument at entry + 0x28, the eleventh.
inline constexpr const char* kTrackerBuilderHeadPattern =
    "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 20 4C 89 40 18 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 "
    "0F 29 70 C8 45 33 F6 0F 29 78 B8 4D 8B E1 44 0F 29 40 A8 44 8B EA 4C 8B F9 83 FA FF 0F 84 ?? ?? ?? ?? "
    "48 8B AC 24 F8 00 00 00 48 8D 99 40 0A 00 00 F3 44 0F 10 84 24 00 01 00 00 48 8D B9 84 12 00 00 "
    "F3 0F 10 B4 24 E8 00 00 00 48 8D B1 C8 1A 00 00 F3 0F 10 BC 24 E0 00 00 00 4D 8B C4 F2 0F 10 45 00 8B 45 08 "
    "4C 8B 8C 24 D8 00 00 00 F2 0F 11 01 F3 0F 10 84 24 F0 00 00 00 89 41 08 44 89 B1 3C 0A 00 00 "
    "F3 44 0F 11 41 0C";
inline constexpr size_t kBuilderHeadBytes = 174;
// ADDR-HYGIENE: resolved + structural. In the builder's head: where its frame and its read of the heading are
// (positions inside the matched pattern). kBuilderPushedBytes is the five pushes the pattern's own bytes fix
// (push rdi, r12, r13, r14, r15: no wildcard among them), so it cannot differ on an image the pattern matches.
// kHeadingArgument is a claim, not a measurement: DecodeTrackerSites refuses unless the site's write and the
// builder's read both land on that argument's slot.
inline constexpr size_t kBuilderFrameSub = 31, kBuilderHeadingDisp = 90;
inline constexpr uint32_t kBuilderPushedBytes = 0x28;
inline constexpr uint32_t kHeadingArgument = 11;        // counted from 1

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
// Where the E8 / E9 at `site + opcode` lands, as an offset from the module base (`siteRva` is the site's own).
inline int64_t BranchRva(const uint8_t* site, size_t opcode, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(opcode) + 5 + ReadI32(site + opcode + 1);
}
inline int64_t RipRva(const uint8_t* site, size_t disp, size_t next, int64_t siteRva)
{
    return siteRva + static_cast<int64_t>(next) + ReadI32(site + disp);
}

struct TrackerSites
{
    int32_t userFromState = 0;    // the component's user index, relative to the state the builder is handed (-8)
    uint32_t tlsBlock = 0;        // the yaw's block in engine TLS (0xB0)
    uint32_t stride = 0;          // per user (0x108)
    uint32_t yawOffset = 0;       // the yaw in it (0x8F8)
    int64_t playerBuildRva = 0;   // the player-centred build (the method's call)
    int64_t otherBuildRva = 0;    // the other build (the method's jump)
    int64_t builderRva = 0;       // the hook
    int64_t returnRva = 0;        // where the player-centred build's call of the builder returns
    int64_t halfPiRva = 0;        // the constant the game adds to the yaw
};
enum class TrackerBind : uint8_t
{
    Bound = 0,
    MethodMissing,      // the component's method: no match, or more than one
    SiteMissing,        // the heading's read-add-pass: no match, or more than one
    BuilderMissing,     // the builder's head: no match, or more than one
    SiteOutsideBuild,   // the site is not inside the function the method calls
    CallMismatch,       // the site's call does not land on the matched builder head
    NotTheArgument,     // the builder does not read the slot the site writes as its eleventh argument
    Implausible,        // an operand out of range, or the method's two state operands differ
    ConstantMismatch,   // what the game adds to the yaw is not pi/2
    Count
};
inline const char* TrackerBindName(TrackerBind why)
{
    switch (why)
    {
    case TrackerBind::Bound: return "bound";
    case TrackerBind::MethodMissing: return "the tracker component's method is missing or ambiguous";
    case TrackerBind::SiteMissing: return "the heading's read-add-pass site is missing or ambiguous";
    case TrackerBind::BuilderMissing: return "the builder's head is missing or ambiguous";
    case TrackerBind::SiteOutsideBuild: return "the heading site is not inside the build the method calls";
    case TrackerBind::CallMismatch: return "the heading site does not call the matched builder";
    case TrackerBind::NotTheArgument: return "the builder does not read the slot the site writes";
    case TrackerBind::Implausible: return "the decoded operands are implausible";
    case TrackerBind::ConstantMismatch: return "the game does not add pi/2 to the yaw";
    default: return "?";
    }
}
// `method`, `site`, `builder`: the unique matches (null = missing or ambiguous) with their offsets from the module
// base; `siteFunctionRva`: the beginning of the function `site` lies in, from the image's own function table;
// `halfPi`: the four bytes the site's addss reads (null when unreadable).
inline TrackerBind DecodeTrackerSites(const uint8_t* method, int64_t methodRva, const uint8_t* site, int64_t siteRva,
                                      int64_t siteFunctionRva, const uint8_t* builder, int64_t builderRva,
                                      const uint8_t* halfPi, TrackerSites& out)
{
    if (!method)
        return TrackerBind::MethodMissing;
    if (!site)
        return TrackerBind::SiteMissing;
    if (!builder)
        return TrackerBind::BuilderMissing;
    const int64_t playerBuild = BranchRva(method, kMethodCallPlayer, methodRva);
    const int64_t otherBuild = BranchRva(method, kMethodJumpOther, methodRva);
    if (siteFunctionRva != playerBuild || siteRva <= playerBuild)
        return TrackerBind::SiteOutsideBuild;
    if (BranchRva(site, kSiteCallBuilder, siteRva) != builderRva)
        return TrackerBind::CallMismatch;
    // The caller writes the heading at its rsp + SLOT; the callee sees that at entry + SLOT + 8. The builder reads
    // rsp + DISP with its entry at rsp + FRAME. Both must be the eleventh argument's slot (the fifth is entry + 0x28).
    const uint32_t frame = kBuilderPushedBytes + ReadU32(builder + kBuilderFrameSub);
    const uint32_t read = ReadU32(builder + kBuilderHeadingDisp);
    const uint32_t eleventh = 0x28 + (kHeadingArgument - 5) * 8;
    if (read < frame || read - frame != eleventh || static_cast<uint32_t>(site[kSiteHeadingSlot]) + 8 != eleventh)
        return TrackerBind::NotTheArgument;
    const int32_t user = static_cast<int8_t>(method[kMethodUserDisp]);
    const int32_t state = static_cast<int8_t>(method[kMethodStatePlayer]);
    if (state != static_cast<int8_t>(method[kMethodStateOther]) || user < 0 || state <= 0 || (user & 3) ||
        (state & 7) || otherBuild <= 0 || otherBuild == playerBuild)
        return TrackerBind::Implausible;
    float value = 0.0f;
    if (!halfPi)
        return TrackerBind::ConstantMismatch;
    std::memcpy(&value, halfPi, sizeof(value));
    if (!(std::fabs(value - kHalfPi) <= 1.0e-6f))
        return TrackerBind::ConstantMismatch;
    out.userFromState = user - state;
    out.stride = ReadU32(site + kSiteStride);
    out.tlsBlock = ReadU32(site + kSiteTlsBlock);
    out.yawOffset = ReadU32(site + kSiteYaw);
    out.playerBuildRva = playerBuild;
    out.otherBuildRva = otherBuild;
    out.builderRva = builderRva;
    out.returnRva = siteRva + static_cast<int64_t>(kSiteReturn);
    out.halfPiRva = RipRva(site, kSiteHalfPiDisp, kSiteHalfPiNext, siteRva);
    return TrackerBind::Bound;
}

// ---- the heading -----------------------------------------------------------------------------------------------
// The argument is a yaw plus pi/2; a yaw is counter-clockwise from +X seen from above (+Z up).
inline float FieldFromYaw(float yaw) { return yaw + kHalfPi; }
inline float YawFromField(float field) { return field - kHalfPi; }
inline void ForwardOfYaw(float yaw, float out[3])
{
    out[0] = std::cos(yaw);
    out[1] = std::sin(yaw);
    out[2] = 0.0f;
}
inline float WrapPi(float angle)
{
    angle = std::fmod(angle, kTwoPi);
    if (angle > head_relative::kPi)
        angle -= kTwoPi;
    else if (angle <= -head_relative::kPi)
        angle += kTwoPi;
    return angle;
}
// A yaw in [0, 2 pi).
inline float WrapYaw(float yaw)
{
    yaw = std::fmod(yaw, kTwoPi);
    if (yaw < 0.0f)
        yaw += kTwoPi;
    return yaw < kTwoPi ? yaw : 0.0f;
}
// WHERE THE GAME PUTS A CONTACT, from its own arithmetic (retail +0x40B8FB..+0x40B943, before the range clamp):
//   a = -heading;  x = sinf(a) * dy - cosf(a) * dx;  y = sinf(a) * dx + cosf(a) * dy
inline void TrackerPoint(const float offset[3], float field, float out[2])
{
    const float a = -field;
    const float c = std::cos(a), s = std::sin(a);
    out[0] = s * offset[1] - c * offset[0];
    out[1] = s * offset[0] + c * offset[1];
}
// The angle of a tracker point from the tracker's top, positive toward its LEFT.
inline float TrackerPointBearing(const float point[2]) { return std::atan2(-point[0], -point[1]); }

// ---- THE PROOF TAKEN AT RUN TIME -------------------------------------------------------------------------------
// The one thing the bytes cannot say without the kit is what the yaw at that place IS. So before the first
// substitution the argument must agree with a reference that does not come from the same read: the aim yaw this
// mod's own direct drive is writing into the game (the input thread's published target, usable only while the
// drive is live and the player is on foot; game.cpp Halo4TrackerAimReference). kProofSamples agreeing samples,
// and the reference must have MOVED across them by more than twice the tolerance, so no constant and no
// unrelated field can pass. The reference leads the game by up to a tick while the hand moves, so a miss between
// the tolerance and the disagreement bound counts for nothing either way. Once proven, kProofWithdrawRun
// disagreements in a row withdraw it. A pass also settles the SIGN: the argument is then the aim's yaw in the
// convention the drive writes, and the head's yaw is measured the same way, so the two are like for like.
// (These five are thresholds of the proof, not addresses or offsets: nothing here locates anything in the game.)
inline constexpr uint32_t kProofSamples = 30;
inline constexpr uint32_t kProofWithdrawRun = 60;
inline constexpr float kProofToleranceRad = 2.0f * head_relative::kDegToRad;
inline constexpr float kProofSpreadRad = 10.0f * head_relative::kDegToRad;
inline constexpr float kProofDisagreeRad = 10.0f * head_relative::kDegToRad;
static_assert(kProofSpreadRad > 2.0f * kProofToleranceRad, "a constant must not be able to agree across the spread");

struct ArgumentProof
{
    uint32_t agreed = 0;       // agreeing samples since the last disagreement (until proven)
    float anchorYaw = 0.0f;    // the reference of the first of them
    float low = 0.0f, high = 0.0f;  // the extremes of (reference - anchor) among them
    uint32_t disagreed = 0;    // disagreements in a row (once proven)
    bool proven = false;
};
enum class ProofStep : uint8_t
{
    NoReference,  // nothing to compare with now: neither for nor against
    Agreed,
    Lagging,      // between the tolerance and the disagreement bound: neither for nor against
    Disagreed,
    Proven,       // this sample completed the proof
    Withdrawn,    // this sample withdrew it
    Count
};
inline ProofStep StepProof(ArgumentProof& proof, float gameYaw, bool referenceUsable, float referenceYaw)
{
    if (!referenceUsable || !std::isfinite(gameYaw) || !std::isfinite(referenceYaw))
        return ProofStep::NoReference;
    const float miss = std::fabs(WrapPi(gameYaw - referenceYaw));
    if (miss <= kProofToleranceRad)
    {
        proof.disagreed = 0;
        if (proof.proven)
            return ProofStep::Agreed;
        if (!proof.agreed)
        {
            proof.anchorYaw = referenceYaw;
            proof.low = proof.high = 0.0f;
        }
        const float moved = WrapPi(referenceYaw - proof.anchorYaw);
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
        proof = ArgumentProof{};
        return ProofStep::Disagreed;
    }
    if (++proof.disagreed >= kProofWithdrawRun)
    {
        proof = ArgumentProof{};
        return ProofStep::Withdrawn;
    }
    return ProofStep::Disagreed;
}

// ---- the verdict -----------------------------------------------------------------------------------------------
enum class TrackerUse : uint8_t
{
    Used = 0,
    OtherCallSite,    // not the player-centred build's call: forwarded untouched
    NotLocalUser,     // the component's user is not 0
    Unproven,         // the argument has not (yet) agreed with the aim this mod presented
    SwitchOff,        // the player's switch is off
    NoSample,         // no frame has published a head yet, or the head view was not live in it
    OtherGeneration,  // the sample belongs to another load of the title
    FromTheFuture,    // the sample's clock is ahead of this one's
    Stale,            // older than the age limit
    NotGameplay,      // a cinematic held the camera in that frame
    NoHeading,        // non-finite, or the head never had a heading
    Overlapped,       // user 0's build arrived while another build of user 0 was inside (the runtime's flag)
    Count
};
inline const char* TrackerUseName(TrackerUse use)
{
    switch (use)
    {
    case TrackerUse::Used: return "used";
    case TrackerUse::OtherCallSite: return "another build's call";
    case TrackerUse::NotLocalUser: return "not the local user";
    case TrackerUse::Unproven: return "unproven";
    case TrackerUse::SwitchOff: return "switch off";
    case TrackerUse::NoSample: return "no head sample";
    case TrackerUse::OtherGeneration: return "another generation";
    case TrackerUse::FromTheFuture: return "sample from the future";
    case TrackerUse::Stale: return "stale head sample";
    case TrackerUse::NotGameplay: return "not gameplay";
    case TrackerUse::NoHeading: return "no heading";
    case TrackerUse::Overlapped: return "an overlapping build";
    default: return "?";
    }
}
struct TrackerRequest
{
    bool playerBuild = false;  // this call returns to the player-centred build
    int32_t user = -1;         // the component's own user index
    bool proven = false;       // ArgumentProof::proven after this call's step
    bool switchOn = false;     // g_config.tracker_follows_head, read at this use
    bool gameplay = false;     // the frame the head sample came from was the player's, not a cinematic's
    uint32_t generation = 0;   // the title generation now
    uint64_t nowUs = 0;        // the sight clock now
    int sign = 1;              // g_config.tracker_head_sign
    float offsetDeg = 0.0f;    // g_config.tracker_head_offset_deg
};
// The heading the builder should be handed. Used = `fieldOut` is the game's argument with the head's yaw, in the
// game's own range [pi/2, 5 pi/2); anything else names why the game's own argument stands, and `fieldOut` is
// untouched. `hold` keeps the last heading the head had, for a head looking straight up or down.
inline TrackerUse TrackerHeading(const TrackerRequest& request, const head_relative::HeadSample& sample,
                                 head_relative::HeadingHold& hold, float nativeField, float& fieldOut,
                                 float* headYawOut = nullptr)
{
    using head_relative::Use;
    if (!request.playerBuild)
        return TrackerUse::OtherCallSite;
    if (request.user != 0)
        return TrackerUse::NotLocalUser;
    if (!request.proven)
        return TrackerUse::Unproven;
    if (!request.switchOn)
        return TrackerUse::SwitchOff;
    switch (head_relative::SampleUse(sample, request.generation, request.nowUs))
    {
    case Use::Ok: break;
    case Use::OtherGeneration: return TrackerUse::OtherGeneration;
    case Use::FromTheFuture: return TrackerUse::FromTheFuture;
    case Use::Stale: return TrackerUse::Stale;
    case Use::NoHeading: return TrackerUse::NoHeading;
    default: return TrackerUse::NoSample;
    }
    if (!request.gameplay)
        return TrackerUse::NotGameplay;
    if (!std::isfinite(nativeField))
        return TrackerUse::NoHeading;
    float headHeading[3], nativeForward[3];
    if (!head_relative::HeldHeading(hold, sample.forward, kWorldUp, headHeading))
        return TrackerUse::NoHeading;
    const float nativeYaw = YawFromField(nativeField);
    ForwardOfYaw(nativeYaw, nativeForward);
    float turn = 0.0f;
    if (!head_relative::YawToHead(nativeForward, headHeading, kWorldUp, request.sign,
                                  request.offsetDeg * head_relative::kDegToRad, turn))
        return TrackerUse::NoHeading;
    const float yaw = WrapYaw(nativeYaw + turn);
    if (!std::isfinite(yaw))
        return TrackerUse::NoHeading;
    fieldOut = FieldFromYaw(yaw);
    if (headYawOut)
        *headYawOut = std::atan2(headHeading[1], headHeading[0]);
    return TrackerUse::Used;
}
} // namespace halo4_head_relative
