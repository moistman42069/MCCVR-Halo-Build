#pragma once

#include <array>
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
    bool resourcesPrepared = false;
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
    return proof.resourcesPrepared &&
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
    bool authoredCaptureLive = false;
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
    SuppressNative,
    CaptureAuthored,
};

// This decision is intentionally fail-open.  Unowned, uninstalled, malformed,
// or unrelated CUI work remains stock; unlike the old Reach transaction, an
// optional reticle miss never rejects the stereo frame.
constexpr Halo4CuiReticleAction Halo4DecideCuiReticleAction(
    bool ownsStereoTransaction, bool authoredCaptureLive,
    bool commandReadable, uint32_t command, bool crosshairEnabled,
    bool killNativeReticle, int stereoEye, bool rightEyeFirst) noexcept
{
    if (!ownsStereoTransaction || !authoredCaptureLive || !commandReadable ||
        command != kHalo4CuiCommandBegin || stereoEye < 0 || stereoEye > 1)
    {
        return Halo4CuiReticleAction::DrawStock;
    }

    if (!crosshairEnabled)
        return Halo4CuiReticleAction::SuppressNative;
    if (!killNativeReticle)
        return Halo4CuiReticleAction::DrawStock;

    const int captureEye = rightEyeFirst ? 1 : 0;
    return stereoEye == captureEye
        ? Halo4CuiReticleAction::CaptureAuthored
        : Halo4CuiReticleAction::SuppressNative;
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
