#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Halo 4-only render evidence and (eventually) allocation-free policy. This
// file contains no Windows, COM, MinHook, logging, or engine writes, so its
// contents can be exhaustively tested offline before any detour is
// authorized. It currently holds pinned identity constants ONLY: no RVA,
// AOB, stride, or layout may be added here without its proof first recorded
// in docs/HALO4-SIGNATURE-EVIDENCE.md (H4EK-first; the Reach script-table
// retail-derivation chain is UNPROVEN for Halo 4).

inline constexpr size_t kHalo4RetailFileSize = 17829336;
inline constexpr size_t kHalo4RetailImageSize = 0x04A3F000;
inline constexpr uint32_t kHalo4RetailPeTimestamp = 0x68A0E7BF;

// One retail Halo 4 build, signed once per storefront. As documented for the
// other titles (docs/MCC-EDITIONS-EVIDENCE.md), the Steam and Microsoft
// Store images are byte-identical apart from the Authenticode certificate,
// so the whole-file digest differs per edition while every RVA, the PE
// timestamp, and the image size stay shared. Both digests describe the one
// code image this file pins; an MCC update invalidates the whole table
// loudly through the identity preflight.
inline constexpr const char* kHalo4RetailModuleSha256[] = {
    // Steam
    "7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84",
    // Microsoft Store / Xbox app (Game Pass)
    "5767CD564C1E8E8D012D002A8DE8E92960A3DE46442399ED054E3C4EF44AA496",
};

// Kit-vs-retail build drift, same shape Reach had: kit facts transfer as
// semantics and layouts only, never as addresses.
inline constexpr char kHalo4KitBuildTag[] = "2023.06.27.176405.1-Release";
inline constexpr char kHalo4RetailBuildTag[] = "2025.08.16.178512.1";

// Data anchors the code anchors below must decode to (E-H4-4 table).
inline constexpr uint32_t kHalo4PlayerViewArrayRva = 0x30AD1C0;
inline constexpr uint32_t kHalo4PlayerViewStride = 0xAD0;
inline constexpr uint32_t kHalo4ViewStackTopRva = 0xE84634;

// E-H4-4 retail anchors (docs/HALO4-SIGNATURE-EVIDENCE.md, PROVEN 2026-08-07;
// the prologue-inclusive constructor variant is recorded under C-H4-2). Every
// pattern below was measured to match EXACTLY ONCE over the whole pinned
// module, at exactly the recorded RVA. C-H4-2's cold observation re-runs that
// measurement against the LOADED image; it admits no hook either way.
//
// ripDispOffset, when non-zero, is the byte offset of a rip-relative disp32
// INSIDE the matched bytes (the referencing instruction ends at
// ripDispOffset + 4), and the decode must land on ripTargetRva. Keeping the
// offset beside the pattern it indexes is load-bearing: a pattern whose
// prefix is lengthened without moving its offset decodes garbage.
struct Halo4RetailAnchor
{
    const char* name;
    const char* pattern;
    uint32_t rva;
    uint8_t ripDispOffset;
    uint32_t ripTargetRva;
};

// Table order is part of the contract - the named indices below and the
// level-load gate's pattern reuse in game.cpp bind to it.
inline constexpr size_t kHalo4AnchorCtor = 0;
inline constexpr size_t kHalo4AnchorPush = 1;
inline constexpr size_t kHalo4AnchorPop = 2;
inline constexpr size_t kHalo4AnchorClamp = 3;

inline constexpr Halo4RetailAnchor kHalo4RetailAnchors[] = {
    // Constructor loop of the 4 x 0xAD0 player-view array; the same shape
    // (and the same pattern string) the Halo 4 level-load gate resolves the
    // array from. The lea displacement at +0x0D decodes to the array base.
    { "player-view-array-ctor",
      "48 89 5C 24 08 57 48 83 EC 20 48 8D 1D ?? ?? ?? ?? BF 04 00 00 00 "
      "48 8B CB E8 ?? ?? ?? ?? 48 81 C3 D0 0A 00 00 48 83 EF 01 75 ?? "
      "48 8B 5C 24 30",
      0x22A50, 0x0D, kHalo4PlayerViewArrayRva },
    // Render-view stack push: refuses at top >= 3, stores the re-entry
    // callback at view+0x298. The mov r8d displacement decodes to
    // g_view_stack_top.
    { "render-view-stack-push",
      "48 83 EC 28 44 8B 05 ?? ?? ?? ?? 41 83 F8 03 7D ?? 41 FF C0 "
      "48 89 91 98 02 00 00",
      0x341760, 0x07, kHalo4ViewStackTopRva },
    // Render-view stack pop; its mov eax displacement decodes to the SAME
    // g_view_stack_top the push uses, which the preflight cross-checks by
    // pinning both to one ripTargetRva.
    { "render-view-stack-pop",
      "48 83 EC 28 8B 05 ?? ?? ?? ?? 83 E8 01 89 05 ?? ?? ?? ?? 78 ?? "
      "48 8D 0D",
      0x3417A8, 0x06, kHalo4ViewStackTopRva },
    // The window count clamp(n,1,4) inside the main_render_game homolog.
    { "player-window-count-clamp",
      "B9 01 00 00 00 3B C1 0F 4F C8 B8 04 00 00 00 3B C8 0F 4C C1",
      0x1221CE, 0, 0 },
};

inline constexpr size_t kHalo4RetailAnchorCount =
    sizeof(kHalo4RetailAnchors) / sizeof(kHalo4RetailAnchors[0]);

constexpr uint32_t Halo4RetailAnchorRipTargetCount()
{
    uint32_t count = 0;
    for (const Halo4RetailAnchor& anchor : kHalo4RetailAnchors)
        if (anchor.ripDispOffset != 0)
            ++count;
    return count;
}

inline constexpr uint32_t kHalo4RetailAnchorRipTargets =
    Halo4RetailAnchorRipTargetCount();

// Everything C-H4-2's cold observation measures against the loaded image.
// Pure data so the verdict is offline-testable; the DLL side only fills it.
struct Halo4ColdObservationResult
{
    bool moduleRangeValid = false;   // base present, size == pinned image size
    bool peIdentity = false;         // machine/timestamp/SizeOfImage as pinned
    uint32_t anchorsMatchedOnce = 0; // anchors matching exactly once
    uint32_t anchorsAtPinnedRva = 0; // of those, matches at the pinned RVA
    uint32_t ripTargetsAtPinnedRva = 0; // decodes landing on ripTargetRva
    bool mappingStable = false;      // module pin still current after the scan
};

constexpr bool Halo4ColdObservationPass(const Halo4ColdObservationResult& r)
{
    return r.moduleRangeValid && r.peIdentity &&
        r.anchorsMatchedOnce == kHalo4RetailAnchorCount &&
        r.anchorsAtPinnedRva == kHalo4RetailAnchorCount &&
        r.ripTargetsAtPinnedRva == kHalo4RetailAnchorRipTargets &&
        r.mappingStable;
}

// ===========================================================================
// E-H4-6: the per-window camera transaction, retail-anchored 2026-08-07.
// Proof: docs/HALO4-SIGNATURE-EVIDENCE.md, section E-H4-6. Every RVA below was
// derived three independent ways that agree - the per-window loop's own rel32
// decodes, each function's entry signature, and the converter's copy map - and
// every pattern was measured to match EXACTLY ONCE over the pinned .text.
// ===========================================================================

// The per-window loop body inside main_render_game (fn 0x12259C-0x123115). It
// marshals all six setup arguments, calls setup, writes the first-window flag,
// then calls the inner wrapper. Anchoring the loop rather than the callees is
// what proves the ABI: its three displacements decode to the two functions we
// hook and to the element they publish.
inline constexpr uint32_t kHalo4PerWindowLoopRva = 0x122CA6;
// Byte offsets of the three displacements inside that pattern.
inline constexpr uint32_t kHalo4LoopSetupRel32Offset = 0x1E;
inline constexpr uint32_t kHalo4LoopElementRipOffset = 0x38;
inline constexpr uint32_t kHalo4LoopWrapperRel32Offset = 0x3D;

// The producer. setup(view, window, count, mode, user, observer*) writes the
// rasterizer camera, the projection, the render pair and the constant bank in
// ONE straight-line call - so per-eye state must be substituted before it, not
// after it (E-H4-5's beta-1 boundary).
inline constexpr uint32_t kHalo4SetupRva = 0x374C84;
// The render transaction: set-current, push, render, pop, clear.
// wrapper(element, view, window).
inline constexpr uint32_t kHalo4WrapperRva = 0x1222F4;
// The observer -> camera converter's copy map. Not hooked; its bytes are the
// layout proof for the observer offsets below.
inline constexpr uint32_t kHalo4ConverterCopyRva = 0x38F074;
// g_player_view_stack_element - the single camera-bearing object the wrapper
// pushes, and the destination setup writes every camera artifact into.
inline constexpr uint32_t kHalo4StackElementRva = 0x10DAFE0;
// The active c_player_view* the wrapper sets and clears.
inline constexpr uint32_t kHalo4ActiveViewRva = 0x4969AA0;

// s_observer_result layout, proven by H4EK symbols/source strings and the
// retail converter copy map at 0x38F074:
//   [rdx+0x00] -> element+0x00 (position)   [rdx+0x28] -> +0x0C (forward)
//   [rdx+0x34] -> element+0x18 (up)         [rdx+0x78] -> +0x28 (vertical FOV)
//   [rdx+0x7C] -> element+0x2C (FOV ratio)
// The snapshot covers every converter input, but C-H4-7 substitutes only the
// three pose vectors; every FOV/focus/aspect byte remains stock.
inline constexpr uint32_t kHalo4ObserverPositionOffset = 0x00;
inline constexpr uint32_t kHalo4ObserverForwardOffset = 0x28;
inline constexpr uint32_t kHalo4ObserverUpOffset = 0x34;
inline constexpr uint32_t kHalo4ObserverVerticalFovOffset = 0x78;
inline constexpr uint32_t kHalo4ObserverFovRatioOffset = 0x7C;
// Saved and restored around the whole stereo transaction. 0x80 covers every
// field the converter reads plus the +0x44..+0x5C block setup copies onto the
// view element right after it (0x374D65-0x374D7A).
inline constexpr uint32_t kHalo4ObserverSnapshotBytes = 0x80;

// setup writes the converted camera vectors directly into the stack element,
// then builds its raster projection at element+0x88. H4EK proves the final
// row-vector 4x4 begins at projection+0x78, hence element+0x100. The camera
// transaction reads these engine-held outputs after each stock setup call; it
// never guesses a projection from the observer's FOV-ratio field.
inline constexpr uint32_t kHalo4ElementPositionOffset = 0x00;
inline constexpr uint32_t kHalo4ElementForwardOffset = 0x0C;
inline constexpr uint32_t kHalo4ElementUpOffset = 0x18;
inline constexpr uint32_t kHalo4RasterProjectionOffset = 0x88;
inline constexpr uint32_t kHalo4ProjectionMatrixOffset = 0x78;
inline constexpr uint32_t kHalo4ElementProjectionMatrixOffset =
    kHalo4RasterProjectionOffset + kHalo4ProjectionMatrixOffset;

// c_player_view fields setup writes (0x374E7A-0x374E99), which let the wrapper
// detour recover setup's own arguments without re-deriving the TLS chain.
inline constexpr uint32_t kHalo4ViewWindowIndexOffset = 0x38C;
inline constexpr uint32_t kHalo4ViewWindowCountOffset = 0x390;
inline constexpr uint32_t kHalo4ViewModeOffset = 0x394;
inline constexpr uint32_t kHalo4ViewOutputUserOffset = 0x39C;
inline constexpr uint32_t kHalo4ViewFirstWindowFlagOffset = 0x389;

inline constexpr size_t kHalo4CameraAnchorLoop = 0;
inline constexpr size_t kHalo4CameraAnchorSetup = 1;
inline constexpr size_t kHalo4CameraAnchorWrapper = 2;
inline constexpr size_t kHalo4CameraAnchorConverter = 3;

// Reuses Halo4RetailAnchor so the cold observation's proven matcher validates
// this table with no new scanning code.
inline constexpr Halo4RetailAnchor kHalo4CameraAnchors[] = {
    // The loop body. Its lea displacement decodes to the stack element; the two
    // rel32s are checked separately by Halo4CameraLoopTargetsAgree below,
    // because a Halo4RetailAnchor carries only one displacement.
    { "per-window-camera-loop",
      "48 8B 47 08 48 89 44 24 28 8B 07 89 44 24 20 44 8B 4C 24 50 "
      "45 8B C7 8B 57 10 49 8B CD E8 ?? ?? ?? ?? 85 F6 0F 94 C0 "
      "41 88 85 89 03 00 00 44 8B 47 10 49 8B D5 48 8D 0D ?? ?? ?? ?? "
      "E8 ?? ?? ?? ??",
      kHalo4PerWindowLoopRva, kHalo4LoopElementRipOffset,
      kHalo4StackElementRva },
    // Setup's entry. Its lea r13 displacement decodes to the same stack
    // element the loop's lea does - an independent second derivation.
    { "player-view-setup-entry",
      "48 89 5C 24 20 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 "
      "48 8B D9 0F 29 74 24 40 4C 8D 2D ?? ?? ?? ?? 49 63 E8",
      kHalo4SetupRva, 0x1F, kHalo4StackElementRva },
    // The wrapper's entry. Its displacement decodes to the active-view global.
    { "player-view-wrapper-entry",
      "48 89 5C 24 08 48 89 7C 24 10 41 56 48 83 EC 20 48 8B FA "
      "48 89 15 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 41 8B D8 E8",
      kHalo4WrapperRva, 0x16, kHalo4ActiveViewRva },
    // The converter's copy map: the literal instructions that read the observer
    // offsets this file pins. If the layout ever moves, this stops matching.
    { "observer-camera-copy-map",
      "F2 0F 10 42 28 F2 0F 11 43 0C 8B 42 30 89 43 14 "
      "F2 0F 10 42 34 F2 0F 11 43 18 8B 42 3C 89 43 20 "
      "F3 0F 10 42 78 F3 0F 11 43 28 F3 0F 10 72 7C F3 0F 11 73 2C",
      kHalo4ConverterCopyRva, 0, 0 },
};

inline constexpr size_t kHalo4CameraAnchorCount =
    sizeof(kHalo4CameraAnchors) / sizeof(kHalo4CameraAnchors[0]);

constexpr uint32_t Halo4CameraAnchorRipTargetCount()
{
    uint32_t count = 0;
    for (const Halo4RetailAnchor& anchor : kHalo4CameraAnchors)
        if (anchor.ripDispOffset != 0)
            ++count;
    return count;
}

inline constexpr uint32_t kHalo4CameraAnchorRipTargets =
    Halo4CameraAnchorRipTargetCount();

// The loop's own two rel32 call targets must be the functions we are about to
// hook. This is the edge that makes the hook a proven caller relationship
// rather than two addresses that merely matched a pattern.
constexpr bool Halo4CameraLoopTargetsAgree(
    uint32_t setupTargetRva, uint32_t wrapperTargetRva)
{
    return setupTargetRva == kHalo4SetupRva &&
        wrapperTargetRva == kHalo4WrapperRva;
}

// Everything the camera core proves before it creates a single hook. Pure data
// so core_tests can prove each field fails closed on its own.
struct Halo4CameraInstallProof
{
    bool coldObservationPassed = false; // C-H4-2's identity+anchor preflight
    uint32_t anchorsMatchedOnce = 0;
    uint32_t anchorsAtPinnedRva = 0;
    uint32_t ripTargetsAtPinnedRva = 0;
    bool loopCallTargetsAgree = false;  // the loop calls setup and the wrapper
    bool executableRange = false;       // both hook sites inside .text
    bool mappingStable = false;
};

constexpr bool Halo4CameraInstallComplete(const Halo4CameraInstallProof& p)
{
    return p.coldObservationPassed &&
        p.anchorsMatchedOnce == kHalo4CameraAnchorCount &&
        p.anchorsAtPinnedRva == kHalo4CameraAnchorCount &&
        p.ripTargetsAtPinnedRva == kHalo4CameraAnchorRipTargets &&
        p.loopCallTargetsAgree && p.executableRange && p.mappingStable;
}

// ---------------------------------------------------------------------------
// Per-eye camera math. Allocation-free, engine-free and exhaustively testable:
// the hot detour does nothing here that core_tests cannot reproduce offline.
// ---------------------------------------------------------------------------

struct Halo4CameraBasis
{
    float position[3]{};
    float forward[3]{};
    float up[3]{};
    float verticalFov = 0.0f;
    float fovRatio = 0.0f;
};

// Rejects anything the engine could not have produced, so a torn or
// mid-transition observer read can never become a rendered eye.
inline bool Halo4ValidateCameraBasis(const Halo4CameraBasis& basis) noexcept
{
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(basis.position[axis]) ||
            !std::isfinite(basis.forward[axis]) ||
            !std::isfinite(basis.up[axis]))
        {
            return false;
        }
    }
    const float forwardLengthSquared =
        basis.forward[0] * basis.forward[0] +
        basis.forward[1] * basis.forward[1] +
        basis.forward[2] * basis.forward[2];
    const float upLengthSquared =
        basis.up[0] * basis.up[0] + basis.up[1] * basis.up[1] +
        basis.up[2] * basis.up[2];
    const float forwardUpDot =
        basis.forward[0] * basis.up[0] +
        basis.forward[1] * basis.up[1] +
        basis.forward[2] * basis.up[2];
    // Eye displacement uses forward x up as the right axis, so admitting a
    // merely finite but skewed basis would visibly distort IPD. Halo's camera
    // producer supplies an orthonormal basis; leave a small float-drift band
    // while refusing geometry that no longer has that shape.
    return std::fabs(forwardLengthSquared - 1.0f) < 0.05f &&
        std::fabs(upLengthSquared - 1.0f) < 0.05f &&
        std::fabs(forwardUpDot) < 0.05f &&
        std::isfinite(basis.verticalFov) && basis.verticalFov > 1.0e-4f &&
        basis.verticalFov < 3.14149284f && std::isfinite(basis.fovRatio) &&
        basis.fovRatio > 0.0f;
}

constexpr bool Halo4PreparedPairMatches(
    uint64_t expectedSerial, uint64_t leftSerial, uint64_t rightSerial) noexcept
{
    return expectedSerial != 0 && leftSerial == expectedSerial &&
        rightSerial == expectedSerial;
}

constexpr bool Halo4EyeCaptureIsCurrent(
    int requestedEye, int activeRasterEye, bool redirected,
    bool cacheAvailable) noexcept
{
    return requestedEye >= 0 && requestedEye < 2 &&
        activeRasterEye == requestedEye && redirected && cacheAvailable;
}

constexpr bool Halo4XrPairUploadComplete(
    bool acquired, bool waited, bool bothEyesUploaded,
    bool released) noexcept
{
    return acquired && waited && bothEyesUploaded && released;
}

constexpr bool Halo4XrPairSubmissionAccepted(
    bool projectionQueued, bool exactEndFrameSuccess) noexcept
{
    return projectionQueued && exactEndFrameSuccess;
}

// Generic OpenXR cover math retained for later projection work. These tangents
// are NOT the values stored at observer +0x78/+0x7C; those fields are a full
// vertical FOV in radians and an engine-defined FOV ratio. C-H4-7 deliberately
// leaves both observer fields byte-identical and reads the projection Halo 4
// actually built instead. Angles here are left/right/up/down.
inline bool Halo4SymmetricCoverFromFov(
    const float fov[4], float& tangentX, float& tangentY) noexcept
{
    if (!fov)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!std::isfinite(fov[i]))
            return false;
    const float halfHorizontal = fov[1] > -fov[0] ? fov[1] : -fov[0];
    const float halfVertical = fov[2] > -fov[3] ? fov[2] : -fov[3];
    constexpr float kMaximumHalfAngle = 1.5533f; // ~89 degrees
    if (halfHorizontal <= 0.0f || halfHorizontal >= kMaximumHalfAngle ||
        halfVertical <= 0.0f || halfVertical >= kMaximumHalfAngle)
    {
        return false;
    }
    tangentX = std::tan(halfHorizontal);
    tangentY = std::tan(halfVertical);
    return std::isfinite(tangentX) && std::isfinite(tangentY) &&
        tangentX > 0.0f && tangentY > 0.0f;
}

inline void Halo4RotateAboutAxis(
    float vector[3], const float axis[3], float cosAngle,
    float sinAngle) noexcept
{
    const float dot =
        axis[0] * vector[0] + axis[1] * vector[1] + axis[2] * vector[2];
    const float cross[3] = {
        axis[1] * vector[2] - axis[2] * vector[1],
        axis[2] * vector[0] - axis[0] * vector[2],
        axis[0] * vector[1] - axis[1] * vector[0]};
    for (int i = 0; i < 3; ++i)
    {
        vector[i] = vector[i] * cosAngle + cross[i] * sinAngle +
            axis[i] * dot * (1.0f - cosAngle);
    }
}

inline bool Halo4NormalizeQuaternion(
    const float input[4], float output[4]) noexcept
{
    if (!input || !output)
        return false;
    float lengthSquared = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        if (!std::isfinite(input[i]))
            return false;
        lengthSquared += input[i] * input[i];
    }
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f)
        return false;
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    for (int i = 0; i < 4; ++i)
        output[i] = input[i] * inverseLength;
    return true;
}

inline bool Halo4RotateCameraByLocalOrientation(
    Halo4CameraBasis& camera, const float orientation[4]) noexcept
{
    float q[4];
    if (!Halo4ValidateCameraBasis(camera) ||
        !Halo4NormalizeQuaternion(orientation, q))
    {
        return false;
    }
    const float sinHalf = std::sqrt(
        q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    if (sinHalf <= 1.0e-5f)
        return true;

    float angle = 2.0f * std::atan2(sinHalf, q[3]);
    if (angle > 3.14159265f)
        angle -= 6.2831853f;
    const float right[3] = {
        camera.forward[1] * camera.up[2] -
            camera.forward[2] * camera.up[1],
        camera.forward[2] * camera.up[0] -
            camera.forward[0] * camera.up[2],
        camera.forward[0] * camera.up[1] -
            camera.forward[1] * camera.up[0]};
    const float worldAxis[3] = {
        (q[0] / sinHalf) * right[0] + (q[1] / sinHalf) * camera.up[0] -
            (q[2] / sinHalf) * camera.forward[0],
        (q[0] / sinHalf) * right[1] + (q[1] / sinHalf) * camera.up[1] -
            (q[2] / sinHalf) * camera.forward[1],
        (q[0] / sinHalf) * right[2] + (q[1] / sinHalf) * camera.up[2] -
            (q[2] / sinHalf) * camera.forward[2]};
    const float cosAngle = std::cos(angle);
    const float sinAngle = std::sin(angle);
    Halo4RotateAboutAxis(camera.forward, worldAxis, cosAngle, sinAngle);
    Halo4RotateAboutAxis(camera.up, worldAxis, cosAngle, sinAngle);
    return Halo4ValidateCameraBasis(camera);
}

// Displaces and cants the mono camera into one eye. eyePosition/eyeOrientation
// are this eye's offset from the stereo midpoint in OpenXR view axes
// (+X right, +Y up, -Z forward); worldScale converts meters to world units.
// The cant is applied because a headset whose panels are angled outward reports
// its FOV around that canted axis - rendering both eyes straight ahead leaves
// the outer lens edge uncovered.
inline bool Halo4BuildEyeCamera(
    const Halo4CameraBasis& mono, const float eyePosition[3],
    const float* eyeOrientation, float worldScale,
    Halo4CameraBasis& out) noexcept
{
    if (!Halo4ValidateCameraBasis(mono) || !eyePosition ||
        !std::isfinite(worldScale) || worldScale <= 0.0f)
    {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(eyePosition[axis]))
            return false;

    const float right[3] = {
        mono.forward[1] * mono.up[2] - mono.forward[2] * mono.up[1],
        mono.forward[2] * mono.up[0] - mono.forward[0] * mono.up[2],
        mono.forward[0] * mono.up[1] - mono.forward[1] * mono.up[0]};

    out = mono;
    for (int axis = 0; axis < 3; ++axis)
    {
        out.position[axis] = mono.position[axis] +
            (right[axis] * eyePosition[0] + mono.up[axis] * eyePosition[1] -
             mono.forward[axis] * eyePosition[2]) * worldScale;
    }

    return eyeOrientation
        ? Halo4RotateCameraByLocalOrientation(out, eyeOrientation)
        : Halo4ValidateCameraBasis(out);
}

inline bool Halo4CameraOutputMatches(
    const Halo4CameraBasis& requested, const float position[3],
    const float forward[3], const float up[3]) noexcept
{
    if (!Halo4ValidateCameraBasis(requested) || !position || !forward || !up)
        return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(position[axis]) || !std::isfinite(forward[axis]) ||
            !std::isfinite(up[axis]))
        {
            return false;
        }
    }
    // Retail converter instructions copy these nine floats directly. Exact
    // bytes are therefore the proof that setup consumed our observer write;
    // a tolerance could mislabel a small ignored transform as TAKING.
    return std::memcmp(
               position, requested.position, sizeof(requested.position)) == 0 &&
        std::memcmp(
               forward, requested.forward, sizeof(requested.forward)) == 0 &&
        std::memcmp(up, requested.up, sizeof(requested.up)) == 0;
}

// Decode only the proven normal H4 row-vector projection. The retail setup
// passes an exact zero center to this builder and produces positive X/Y scales.
// H4 also has a custom-window path whose p[8]/p[9] center terms cannot be
// represented by the compositor's current symmetric-half-FOV API. Reject that
// distinct path instead of publishing a geometrically false projection.
inline bool Halo4DecodeSymmetricProjectionHalfFovs(
    const float matrix[16], float& halfX, float& halfY,
    float& centerX, float& centerY) noexcept
{
    if (!matrix)
        return false;
    const float scaleX = matrix[0];
    const float scaleY = matrix[5];
    centerX = matrix[8];
    centerY = matrix[9];
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) ||
        !std::isfinite(centerX) || !std::isfinite(centerY) ||
        !std::isfinite(matrix[11]) || scaleX <= 0.0f || scaleY <= 0.0f ||
        matrix[11] != -1.0f ||
        centerX != 0.0f || centerY != 0.0f)
    {
        return false;
    }
    halfX = std::atan(1.0f / scaleX);
    halfY = std::atan(1.0f / scaleY);
    return std::isfinite(halfX) && std::isfinite(halfY) && halfX > 0.0f &&
        halfX < 1.5707f && halfY > 0.0f && halfY < 1.5707f;
}
