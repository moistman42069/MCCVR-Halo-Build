#pragma once

#include <cmath>
#include <cstdint>

inline uint32_t ClassifyAuthoredReticleColorOrdering(const float channel[4])
{
    if (!channel)
        return 0;
    for (unsigned int i = 0; i < 4; ++i)
    {
        const float value = channel[i];
        if (!std::isfinite(value) || value < -0.01f || value > 4.0f)
            return 0;
    }

    // Encode pairwise ordering instead of naming channels. Halo 3's placement
    // output is known to contain four colour/alpha floats, but its alpha slot
    // need not be assumed here. Uniform opacity scaling preserves ordering and
    // therefore cannot create recurring swapchain uploads.
    uint32_t ordering = 1;
    unsigned int shift = 1;
    for (unsigned int i = 0; i < 4; ++i)
    {
        for (unsigned int j = i + 1; j < 4; ++j)
        {
            const float tolerance = 0.05f *
                std::fmax(std::fabs(channel[i]), std::fabs(channel[j]));
            if (channel[i] > channel[j] + tolerance)
                ordering |= 1u << shift;
            else if (channel[j] > channel[i] + tolerance)
                ordering |= 2u << shift;
            shift += 2;
        }
    }
    return ordering;
}

enum class AuthoredReticleRefreshPolicy : uint8_t
{
    IdentityAndColorState,
    IdentityImmediate,
    BoundedAnimation,
};

// Floor on how often the blocking OpenXR acquire/wait/copy/release may run.
// Measured at ~4-5 ms of the render window, which is the whole margin between
// a 120 Hz budget (8.33 ms) and missing it, so it must never run every frame.
inline constexpr uint64_t kMinimumUploadGapFrames = 6;

struct AuthoredReticleRefreshState
{
    uint64_t ownerEpoch = 0;
    uint64_t lastPublishedKey = 0;
    uint64_t lastUploadFrame = 0;
    uint64_t settlingKey = 0;
    uint64_t settlingSinceFrame = 0;
    uint32_t lastPublishedColorState = 0;
    uint32_t lastPublishedDraws = 0;
};

inline void ResetAuthoredReticleRefreshState(
    AuthoredReticleRefreshState& state, uint64_t ownerEpoch)
{
    state = {};
    state.ownerEpoch = ownerEpoch;
}

// A captured art identity must persist before it is allowed to replace art
// already on the quad. Two independent reasons, one mechanism:
//
// - A new identity can precede visible pixels during a level or weapon
//   transition, so publishing it on sight replaces good art with a blank
//   capture.
// - Reach emits a SHORT-LIVED alternate class-2 widget set during combat. The
//   preserved accepted Reach run
//   `out/test-runs/74e1477-reach-outer-camera-commit-pass-20260729-112718`
//   records the published key flipping to one and the same weapon-independent
//   value and back within ~50 ms, repeatedly (11:24:12.173/.224,
//   13.491/.542, 16.136/.187, 16.237/.288, 28.548/.599, 36.782/.834,
//   50.010/.061, 54.432/.484, 11:25:06.629/.680, 07.446/.496). The same
//   transient value appears against two different steady weapon keys, so it
//   is not the held weapon changing. Across every one of those flips the quad
//   heartbeat still reads `SUBMITTED ... chain=1 heldArt=1`, which proves the
//   quad and the swapchain were never the problem - the ART was replaced.
//
// Official HREK CHUD tag exports independently rule out the engine hiding the
// crosshair on damage: no collection whose scripting class is `crosshair`
// carries any damage-driven state, and the `<player> taking damage` condition
// is defined in the globals enum but used by no widget in any of the 63
// exported definitions.
inline constexpr uint64_t kAuthoredIdentitySettleFrames = 24;

inline bool AuthoredReticleIdentitySettled(
    uint64_t capturedKey, uint64_t frameSerial,
    AuthoredReticleRefreshState& state)
{
    if (capturedKey != state.settlingKey ||
        frameSerial < state.settlingSinceFrame)
    {
        state.settlingKey = capturedKey;
        state.settlingSinceFrame = frameSerial;
        return false;
    }
    return frameSerial - state.settlingSinceFrame >=
        kAuthoredIdentitySettleFrames;
}

inline bool ShouldUploadAuthoredReticle(
    AuthoredReticleRefreshPolicy policy,
    bool capturedThisFrame,
    bool uploadAdmitted,
    uint64_t capturedKey,
    uint32_t capturedColorState,
    uint32_t capturedDraws,
    uint64_t frameSerial,
    uint64_t ownerEpoch,
    AuthoredReticleRefreshState& state)
{
    if (state.ownerEpoch != ownerEpoch)
        ResetAuthoredReticleRefreshState(state, ownerEpoch);

    if (!capturedThisFrame || !uploadAdmitted || capturedKey == 0)
        return false;

    if (state.lastUploadFrame != 0 && frameSerial >= state.lastUploadFrame &&
        frameSerial - state.lastUploadFrame < kMinimumUploadGapFrames)
        return false;

    if (policy == AuthoredReticleRefreshPolicy::BoundedAnimation)
    {
        // A capture that painted FEWER widget pieces than the art already on
        // the quad is the crosshair on its way OUT, not a new crosshair.
        // Reach's class-2 draws thin out before they stop (preserved c2d9149
        // log: a window uploading 19 where the steady rate was 30), so a
        // frame can capture a fragment of the reticle, and publishing that
        // fragment is what the player sees as the crosshair disappearing.
        // A count of 0 means the title supplies no count, which leaves this
        // inert - that is how Halo 3 and ODST keep their accepted behavior.
        const bool thinnerThanPublished =
            capturedDraws != 0 && state.lastPublishedDraws != 0 &&
            capturedDraws < state.lastPublishedDraws;
        // Republishing the identity ALREADY on the quad is the animation
        // itself - the crosshair kicks on fire and tints on a target without
        // changing which widgets drew - so it keeps the unconditional bounded
        // cadence and this change costs it nothing.
        if (capturedKey == state.lastPublishedKey && !thinnerThanPublished)
            return true;
        // Nothing published yet (first arm, or an owner reset): the crosshair
        // must appear immediately rather than waiting out a settle window.
        if (state.lastPublishedKey == 0)
            return true;
        // A DIFFERENT or THINNER capture must prove it is not a momentary
        // flicker before it replaces good held art. This is the same
        // protection Halo 3's identity policy below already had, which the
        // bounded-animation path was returning above. A crosshair that really
        // has changed - or really has fewer pieces from now on - still
        // publishes once the window passes, so nothing is permanently stuck.
        return AuthoredReticleIdentitySettled(capturedKey, frameSerial, state);
    }
    if (policy == AuthoredReticleRefreshPolicy::IdentityImmediate)
        return capturedKey != state.lastPublishedKey;

    // Once Halo 3 has published a settled widget, a categorical CHUD colour
    // transition is safe to publish immediately. It costs one upload per real
    // blue/green/red edge and no work while the state is unchanged.
    if (capturedKey == state.lastPublishedKey && capturedColorState != 0 &&
        capturedColorState != state.lastPublishedColorState)
        return true;

    if (capturedKey == state.lastPublishedKey)
        return false;

    // A new widget identity can precede visible pixels during level/weapon
    // transitions. Settle that identity before its first publish so a blank
    // capture cannot replace good held art.
    return AuthoredReticleIdentitySettled(capturedKey, frameSerial, state);
}

inline void MarkAuthoredReticleUploaded(
    AuthoredReticleRefreshState& state,
    uint64_t capturedKey,
    uint32_t capturedColorState,
    uint32_t capturedDraws,
    uint64_t frameSerial)
{
    state.lastPublishedKey = capturedKey;
    state.lastPublishedColorState = capturedColorState;
    state.lastPublishedDraws = capturedDraws;
    state.lastUploadFrame = frameSerial;
    state.settlingKey = 0;
    state.settlingSinceFrame = 0;
}

inline bool AuthoredReticleLayerHasContent(
    bool titleCapturesAuthoredArt,
    bool authoredArtHeld)
{
    return !titleCapturesAuthoredArt || authoredArtHeld;
}

// A title renderer can rebind scene colour after a private authored capture
// starts. That exact bind must stay inside the capture instead of returning to
// the normal eye target.
inline bool AuthoredReticleCaptureOwnsSceneBind(
    bool captureActive, bool sceneTargetMatches, bool privateTargetReady)
{
    return captureActive && sceneTargetMatches && privateTargetReady;
}

// The private authored-crosshair texture is small (kReticleSize square) and the
// title draws its reticle at full raster scale, so WHICH pixels land in that
// texture is decided entirely by the viewport in force at the draw. Sampling a
// viewport once at capture entry is not enough for a title that rebinds its
// scene target inside the capture: every such rebind carries the engine's own
// viewport and silently reframes the capture. This builds the framing from a
// stable source extent so it can be re-applied on each rebind instead.
struct AuthoredCaptureFraming
{
    float width = 0.0f;
    float height = 0.0f;
    float topLeftX = 0.0f;
    float topLeftY = 0.0f;
    bool valid = false;
};

// `magnification` is how much larger than 1:1 the source is drawn into the
// capture: 1.0 keeps `textureSize` source pixels centred on the source centre,
// 4.0 keeps a quarter of that width magnified four times. The result is always
// centred, so the source's centre pixel is the texture's centre pixel.
inline AuthoredCaptureFraming BuildAuthoredCaptureFraming(
    float sourceWidth, float sourceHeight, float magnification,
    float textureSize) noexcept
{
    AuthoredCaptureFraming framing{};
    if (!std::isfinite(sourceWidth) || !std::isfinite(sourceHeight) ||
        !std::isfinite(magnification) || !std::isfinite(textureSize) ||
        sourceWidth < 1.0f || sourceHeight < 1.0f ||
        sourceWidth > 65536.0f || sourceHeight > 65536.0f ||
        magnification < 0.03125f || magnification > 32.0f ||
        textureSize < 1.0f || textureSize > 65536.0f)
    {
        return framing;
    }

    const float width = sourceWidth * magnification;
    const float height = sourceHeight * magnification;
    if (!std::isfinite(width) || !std::isfinite(height))
        return framing;

    framing.width = width;
    framing.height = height;
    framing.topLeftX = (textureSize - width) * 0.5f;
    framing.topLeftY = (textureSize - height) * 0.5f;
    framing.valid = std::isfinite(framing.topLeftX) &&
        std::isfinite(framing.topLeftY);
    if (!framing.valid)
        return AuthoredCaptureFraming{};
    return framing;
}

// Halo 4 authors its CUI in the full eye raster: the runtime-measured reticle
// transform base is exactly (-rasterWidth/2, +the 16:9 half height of that
// width). The viewport bound when the CUI dispatcher is first entered is a
// tail-of-scene-render value instead - a 947x683 quarter-size viewport was
// measured on the learned scene target - so it cannot define the framing. A
// title that authors in the raster uses the raster whenever it is known.
inline bool AuthoredCaptureSourceIsTitleRaster(
    bool titleAuthorsInFullRaster, bool rasterKnown,
    bool liveViewportPresent) noexcept
{
    return (titleAuthorsInFullRaster && rasterKnown) || !liveViewportPresent;
}

// Offscreen CHUD capture cadence.
//
// Capturing is not free: every class-2 widget piece redirects the render
// target, saves and restores viewport/scissor/RTV state on the immediate
// context, and draws itself into the private reticle texture. A title that
// already holds valid released art only has to re-sample the game's widget
// often enough to see it change - between samples the compositor keeps
// showing the last released image at no cost. This is the mechanism ODST
// already ships (`60f3929`); the gap is the only per-title difference.
//
// Every widget drawn during a selected frame must be admitted, so a compound
// reticle is captured whole rather than in pieces from different frames.
inline bool ShouldSampleAuthoredCapture(
    uint64_t gapFrames, uint64_t lastSampleSerial, uint64_t frameSerial)
{
    if (gapFrames == 0 || lastSampleSerial == 0)
        return true;
    if (frameSerial == lastSampleSerial)
        return true;
    // A serial restart (title change, session restart) re-samples immediately
    // rather than waiting out a gap measured against a stale frame number.
    if (frameSerial < lastSampleSerial)
        return true;
    return frameSerial - lastSampleSerial >= gapFrames;
}

// Halo 3's authored crosshair animates - it kicks when the weapon fires and
// turns red on a hostile / green on a friendly target - and none of that
// changes which widgets drew, so an identity-keyed publish freezes one
// snapshot. Publishing on a bounded cadence is what makes it live again, and
// the same cadence throttles the capture that feeds it, so the animation is
// paid for out of work the title was already doing every frame.
//
// 0 disables the animation entirely and holds one captured image, which is
// the cheapest possible behavior. Anything else is clamped to at least
// `kMinimumUploadGapFrames`, because a shorter gap cannot produce extra
// publishes anyway - it would only spend capture work that the upload floor
// then throws away.
inline uint64_t ResolveAuthoredAnimationGapFrames(int configuredFrames)
{
    constexpr int kMaximumAnimationGapFrames = 60;
    if (configuredFrames <= 0)
        return 0;
    if (configuredFrames < static_cast<int>(kMinimumUploadGapFrames))
        return kMinimumUploadGapFrames;
    if (configuredFrames > kMaximumAnimationGapFrames)
        return static_cast<uint64_t>(kMaximumAnimationGapFrames);
    return static_cast<uint64_t>(configuredFrames);
}
