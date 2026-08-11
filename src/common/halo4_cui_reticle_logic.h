#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Halo 4's authored reticle is emitted by the CUI command dispatcher.  These
// are retail facts for the pinned halo4.dll image, not offsets copied from
// Halo 3, ODST, or Reach.  The entry and its only proven caller edge must both
// match uniquely at the recorded RVAs before the optional feature is installed.
inline constexpr uint32_t kHalo4CuiReticleDispatcherRva = 0x003F0EA4;
inline constexpr char kHalo4CuiReticleDispatcherEntryAob[] =
    "48 8B C4 55 56 57 41 56 41 57 48 8D A8 B8 FC FF FF 48 81 EC 50 04 00 00";
inline constexpr std::array<uint8_t, 24>
    kHalo4CuiReticleDispatcherEntryBytes{
        0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x8D, 0xA8, 0xB8, 0xFC, 0xFF,
        0xFF, 0x48, 0x81, 0xEC, 0x50, 0x04, 0x00, 0x00};

inline constexpr uint32_t kHalo4CuiReticleCallerRva = 0x003F4B6B;
inline constexpr char kHalo4CuiReticleCallerAob[] =
    "49 8B 8F 10 04 00 00 4D 8D 8F 20 04 00 00 49 8B D6 E8 ?? ?? ?? ??";
inline constexpr std::array<uint8_t, 18> kHalo4CuiReticleCallerFixedBytes{
    0x49, 0x8B, 0x8F, 0x10, 0x04, 0x00, 0x00, 0x4D, 0x8D,
    0x8F, 0x20, 0x04, 0x00, 0x00, 0x49, 0x8B, 0xD6, 0xE8};
inline constexpr size_t kHalo4CuiReticleCallerCallOpcodeOffset = 17;
inline constexpr size_t kHalo4CuiReticleCallerCallDisplacementOffset = 18;
inline constexpr size_t kHalo4CuiReticleCallerCallNextOffset = 22;

// The dispatcher also services Halo 4's 216x96 auxiliary texture pass and
// later menu/overlay UI.  Only the full-size gameplay-HUD call below is inside
// the accepted per-eye render transaction.  A second optional hook brackets
// this exact call and supplies the TLS phase that admits reticle commands.
inline constexpr uint32_t kHalo4CuiGameplayRenderRva = 0x003ACD60;
inline constexpr char kHalo4CuiGameplayRenderEntryAob[] =
    "48 8B C4 55 53 56 57 41 56 41 57 48 8D 68 B1 48 81 EC A8 00 00 00 0F 29 78 B8 44 0F 29 40 A8";
inline constexpr std::array<uint8_t, 31>
    kHalo4CuiGameplayRenderEntryBytes{
        0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41,
        0x56, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xB1, 0x48,
        0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, 0x0F, 0x29,
        0x78, 0xB8, 0x44, 0x0F, 0x29, 0x40, 0xA8};

inline constexpr uint32_t kHalo4CuiGameplayCallerRva = 0x00375C51;
inline constexpr char kHalo4CuiGameplayCallerAob[] =
    "8B 8E 8C 03 00 00 4C 8D 45 A0 45 33 C9 44 88 6C 24 28 33 D2 89 7C 24 20 E8 ?? ?? ?? ?? 83 FB 03";
inline constexpr size_t kHalo4CuiGameplayCallerCallOpcodeOffset = 24;
inline constexpr size_t kHalo4CuiGameplayCallerCallDisplacementOffset = 25;
inline constexpr size_t kHalo4CuiGameplayCallerCallNextOffset = 29;
inline constexpr uint32_t kHalo4CuiGameplayCallerReturnRva = 0x00375C6E;

inline constexpr uint32_t kHalo4CuiCommandBegin = 0x28;
inline constexpr uint32_t kHalo4CuiCommandEnd = 0x29;
inline constexpr uint16_t kHalo4CuiCommandBeginPayloadSize = 0x0C;

inline constexpr uint32_t kHalo4CuiReticleAnchorCount = 4;

constexpr bool Halo4CuiReticleCallerTargetsDispatcher(
    uint32_t decodedTargetRva) noexcept
{
    return decodedTargetRva == kHalo4CuiReticleDispatcherRva;
}

constexpr bool Halo4CuiGameplayCallerTargetsRender(
    uint32_t decodedTargetRva) noexcept
{
    return decodedTargetRva == kHalo4CuiGameplayRenderRva;
}

// The CUI interception is optional. Its resources, both hooked entries, and
// both caller edges are proven before either hook is created; any missing fact
// leaves only this feature on the stock path.
struct Halo4CuiReticleInstallProof
{
    bool transformLayoutProven = false;
    uint32_t anchorsMatchedOnce = 0;
    uint32_t anchorsAtPinnedRva = 0;
    bool callerDecodesDispatcher = false;
    bool gameplayCallerDecodesRender = false;
    bool executableRange = false;
    bool mappingStable = false;
};

constexpr bool Halo4CuiReticleInstallComplete(
    const Halo4CuiReticleInstallProof& proof) noexcept
{
    return proof.transformLayoutProven &&
        proof.anchorsMatchedOnce == kHalo4CuiReticleAnchorCount &&
        proof.anchorsAtPinnedRva == kHalo4CuiReticleAnchorCount &&
        proof.callerDecodesDispatcher && proof.gameplayCallerDecodesRender &&
        proof.executableRange &&
        proof.mappingStable;
}

enum class Halo4CuiReticleOptionalInstallState : uint8_t
{
    StockFallback,
    CleanupRequired,
    Installed,
};

struct Halo4CuiReticleLifecycleAction
{
    bool nativeTransformLive = false;
    bool cleanupFeature = false;
    bool disarmCameraCore = false;
    bool endOpenXrSession = false;
};

// Even a partially-created optional two-hook transaction owns only its own
// cleanup. Camera ownership and the OpenXR session are never lifecycle
// consequences of this feature's install result.
constexpr Halo4CuiReticleLifecycleAction Halo4CuiReticleLifecycleFor(
    Halo4CuiReticleOptionalInstallState state,
    const Halo4CuiReticleInstallProof& proof) noexcept
{
    return {
        state == Halo4CuiReticleOptionalInstallState::Installed &&
            Halo4CuiReticleInstallComplete(proof),
        state == Halo4CuiReticleOptionalInstallState::CleanupRequired,
        false,
        false,
    };
}

enum class Halo4CuiReticleAction : uint8_t
{
    DrawStock,
    HideNative,
    MoveNative,
};

// This decision is intentionally fail-open.  Unowned, uninstalled, malformed,
// or unrelated CUI work remains stock; unlike the old Reach transaction, an
// optional reticle miss never rejects the stereo frame.
constexpr Halo4CuiReticleAction Halo4DecideCuiReticleAction(
    bool ownsStereoTransaction, bool nativeTransformLive,
    bool commandReadable, uint32_t command, bool crosshairEnabled,
    bool killNativeReticle, int stereoEye, bool rightEyeFirst) noexcept
{
    if (!ownsStereoTransaction || !nativeTransformLive || !commandReadable ||
        command != kHalo4CuiCommandBegin || stereoEye < 0 || stereoEye > 1)
    {
        return Halo4CuiReticleAction::DrawStock;
    }

    if (!crosshairEnabled)
        return Halo4CuiReticleAction::HideNative;
    if (!killNativeReticle)
        return Halo4CuiReticleAction::DrawStock;

    (void)rightEyeFirst;
    return Halo4CuiReticleAction::MoveNative;
}

// Retail's type-0x28 handler pushes one 0x34-byte real_matrix4x3 entry. The
// reticle-only translation is the final float3 at +0x28. Moving that entry
// preserves Halo 4's own bitmap animation, spread, hit marker, and target
// colour while leaving every draw command and every other HUD transform stock.
inline constexpr uint32_t kHalo4CuiTransformStackCountOffset = 0x870;
inline constexpr uint32_t kHalo4CuiTransformStackEntriesOffset = 0x878;
inline constexpr uint32_t kHalo4CuiTransformStride = 0x34;
inline constexpr uint32_t kHalo4CuiTransformTranslationOffset = 0x28;
inline constexpr uint32_t kHalo4CuiTransformStackMaximum = 0x60;

struct Halo4CuiAimOffset
{
    float x = 0.0f;
    float y = 0.0f;
    bool valid = false;
};

inline Halo4CuiAimOffset Halo4ProjectAimToCuiOffset(
    const float cameraForward[3], const float cameraUp[3],
    const float aimForward[3], float halfFovX, float halfFovY) noexcept
{
    Halo4CuiAimOffset result{};
    if (!cameraForward || !cameraUp || !aimForward ||
        !std::isfinite(halfFovX) || !std::isfinite(halfFovY) ||
        halfFovX <= 0.01f || halfFovX >= 1.56f ||
        halfFovY <= 0.01f || halfFovY >= 1.56f)
        return result;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(cameraForward[i]) || !std::isfinite(cameraUp[i]) ||
            !std::isfinite(aimForward[i]))
            return result;

    const float right[3] = {
        cameraForward[1] * cameraUp[2] - cameraForward[2] * cameraUp[1],
        cameraForward[2] * cameraUp[0] - cameraForward[0] * cameraUp[2],
        cameraForward[0] * cameraUp[1] - cameraForward[1] * cameraUp[0]};
    const float forward = aimForward[0] * cameraForward[0] +
        aimForward[1] * cameraForward[1] +
        aimForward[2] * cameraForward[2];
    const float tanX = std::tan(halfFovX);
    const float tanY = std::tan(halfFovY);
    if (!std::isfinite(forward) || forward <= 0.01f ||
        !std::isfinite(tanX) || !std::isfinite(tanY) ||
        tanX <= 0.0f || tanY <= 0.0f)
        return result;

    result.x = (aimForward[0] * right[0] + aimForward[1] * right[1] +
                aimForward[2] * right[2]) / (forward * tanX);
    // CUI's local Y axis points down while camera up points up.
    result.y = -(aimForward[0] * cameraUp[0] + aimForward[1] * cameraUp[1] +
                 aimForward[2] * cameraUp[2]) / (forward * tanY);
    result.valid = std::isfinite(result.x) && std::isfinite(result.y) &&
        std::fabs(result.x) <= 8.0f && std::fabs(result.y) <= 8.0f;
    return result;
}

// Enabling the dispatcher hook is not proof that a non-blank authored image
// has reached the OpenXR swapchain. Halo 4 alone keeps the procedural gun-ray
// pixels during that bootstrap interval; the first validated authored upload
// replaces them without ever exposing a no-reticle frame.
constexpr bool Halo4CuiReticleNeedsProceduralBootstrap(
    bool authoredCaptureLive, bool authoredArtHeld, bool crosshairEnabled,
    bool killNativeReticle) noexcept
{
    return authoredCaptureLive && !authoredArtHeld && crosshairEnabled &&
        killNativeReticle;
}
