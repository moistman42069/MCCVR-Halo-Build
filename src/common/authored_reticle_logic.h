#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
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

// THE ART'S IDENTITY INCLUDES THE SCALE IT WAS DRAWN AT. The crosshair's picture is taken from the HUD as the
// game draws it and kept for as long as the same art is drawn (the key). The HUD layout's scale is not part of
// what the game draws with: the mod writes it into the game's layout, some seconds into a map where the layout
// has to be searched for. A picture taken before that write is of the art at the game's own scale, and the same
// key afterwards kept it on the quad: too large, and of the wrong shape wherever the quad is shaped for the
// layout, until a change of weapon brought another key. So the key carries how many times the layout's scale has
// been changed (`scaleEpoch`); a change makes it a new identity, which is retaken once it has settled.
// 0 stays 0 (no art), and an art key never becomes 0.
inline uint64_t AuthoredReticleKeyAtScale(uint64_t artKey, uint32_t scaleEpoch) noexcept
{
    if (!artKey || !scaleEpoch)
        return artKey;
    const uint64_t key = artKey ^ (static_cast<uint64_t>(scaleEpoch) * 0x9E3779B97F4A7C15ULL);
    return key ? key : 1;
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

// WHERE THE ART SITS IN ITS TEXTURE. The reticle quad is centred on the aim
// point, so the art's own centre has to be the texture's centre, or the player
// aims with one edge of it. Halo 4's capture framing is fitted per HUD layout,
// and a layout can leave the art wholly above the centre (measured with baseY
// 647.256: the magnum's bottom edge sits on the aim point, about 0.18 of the
// texture high, 1.3 deg at the 6 m cap). The centre is measured instead: the
// ink-weighted centroid of a downsampled copy of the capture. Reticles are
// symmetric about their aim point, so the centroid is that point.
struct AuthoredReticleArtCentre
{
    float u = 0.5f;  // 0..1 across the texture, left to right
    float v = 0.5f;  // 0..1 down the texture, top to bottom
    // How far the ink spreads about that centre, across and down (its standard deviation, as a fraction of the
    // texture). A reticle authored as wide as it is tall has the two equal; their ratio is how much wider than
    // tall the captured art is, whatever its outline.
    float spreadU = 0.0f;
    float spreadV = 0.0f;
    uint64_t ink = 0;
    bool valid = false;
};

// HOW MUCH WIDER THAN AUTHORED THE HUD DRAWS ITS ART. Halo 3 lays its HUD out on a 16:9 canvas and stretches
// that canvas over the whole raster, then scales it by the safe frame, across and down separately. The mod moves
// the safe frame for the HUD's size, its width setting and the headset's field of view, so a widget authored
// round is drawn `safeHorizontal / safeVertical * rasterAspect / canvasAspect` times as wide as it is tall
// (measured in a headset: 1.36 for the ring crosshair at 0.5246 / 0.2989 on a 2912x2100 raster, against 1.37
// from this rule; and 1.14 at 0.5246 / 0.3581 on the same raster, where the fitted capture measured 1.00). The
// crosshair's quad is square, so its capture has to undo that. 1 when an input is unusable.
// Reach is given the same pair but lays its HUD out for the raster itself: its canvas aspect is the raster's, and
// only the pair stretches its art (its ring crosshair measured 1.06 times as wide as tall under the game's own
// safe frame, and 1.51 at 0.5246 / 0.3581, whose ratio is 1.46).
inline constexpr float kHalo3HudCanvasAspect = 16.0f / 9.0f;
inline float AuthoredReticleCanvasAspect(bool laidOutForRaster, float rasterAspect) noexcept
{
    return laidOutForRaster ? rasterAspect : kHalo3HudCanvasAspect;
}
inline float AuthoredReticleCanvasStretch(float safeHorizontal, float safeVertical, float rasterAspect,
                                          float canvasAspect) noexcept
{
    if (!std::isfinite(safeHorizontal) || !std::isfinite(safeVertical) || !std::isfinite(rasterAspect) ||
        !std::isfinite(canvasAspect) || safeHorizontal <= 0.0f || safeVertical <= 0.0f || rasterAspect <= 0.0f ||
        canvasAspect <= 0.0f)
        return 1.0f;
    const float stretch = safeHorizontal / safeVertical * rasterAspect / canvasAspect;
    return std::isfinite(stretch) && stretch >= 0.25f && stretch <= 4.0f ? stretch : 1.0f;
}

// The factors that give a capture of such art its authored shape back: the wider axis is narrowed, never the
// other widened, so nothing that fitted the capture is pushed out of it.
struct AuthoredReticleCaptureFit
{
    float across = 1.0f;
    float down = 1.0f;
};
inline AuthoredReticleCaptureFit AuthoredReticleFitForStretch(float stretch) noexcept
{
    AuthoredReticleCaptureFit fit{};
    if (!std::isfinite(stretch) || stretch <= 0.0f)
        return fit;
    if (stretch > 1.0f)
        fit.across = 1.0f / stretch;
    else
        fit.down = stretch;
    return fit;
}

// The fit for one capture. `modLayoutApplied`: the pair is the mod's and every one of the title's slots holds
// it. While it is not, the title draws with its own safe frame, the same across and down (1 / 1 here), and only
// the canvas stretches its art: nothing for a HUD laid out for the raster.
inline AuthoredReticleCaptureFit AuthoredReticleFitForCapture(bool modLayoutApplied, float safeHorizontal,
                                                              float safeVertical, float rasterAspect,
                                                              float canvasAspect) noexcept
{
    if (!modLayoutApplied)
        safeHorizontal = safeVertical = 1.0f;
    return AuthoredReticleFitForStretch(
        AuthoredReticleCanvasStretch(safeHorizontal, safeVertical, rasterAspect, canvasAspect));
}

// HOW MUCH THE CAPTURE MAGNIFIES HALO 3'S HUD. The capture shows the raster's centre magnified, and Halo 3 draws
// its art at a raster size in proportion to `rasterHeight * safeVertical`. The safe frame is the HUD size setting
// corrected for the raster's shape against the headset's, so one fixed magnification showed the same crosshair at
// a different size per render resolution and raster shape: Project Reclaimer's 2560x1440 raster at 0.2331 drew it
// 0.53 times as tall as MCC's 2912x2100 at 0.2989, both at a HUD size of 0.43. The magnification is scaled to hold
// the art at the size it had where `baseScale` was tuned, for the same HUD size: `hudSize` still scales the
// crosshair with the rest of the HUD, as it did. Under the title's own safe frame it is left as it was. Kept
// within a quarter and four times `baseScale`.
inline constexpr float kHalo3ReticleReferenceRasterHeight = 2100.0f;
inline constexpr float kHalo3ReticleReferenceVerticalPerSize = 0.2989f / 0.43f;
inline float Halo3ReticleCaptureScale(float baseScale, bool modLayoutApplied, float rasterHeight,
                                      float safeVertical, float hudSize) noexcept
{
    if (!modLayoutApplied || !std::isfinite(baseScale) || !std::isfinite(rasterHeight) ||
        !std::isfinite(safeVertical) || !std::isfinite(hudSize) || baseScale <= 0.0f || rasterHeight <= 0.0f ||
        safeVertical <= 0.0f || hudSize <= 0.0f)
        return baseScale;
    const float scale = baseScale * kHalo3ReticleReferenceRasterHeight * kHalo3ReticleReferenceVerticalPerSize /
        (rasterHeight * safeVertical / hudSize);
    if (!std::isfinite(scale))
        return baseScale;
    return std::clamp(scale, baseScale * 0.25f, baseScale * 4.0f);
}

// The captured art's width over its height, from its ink spread; 0 when it has no height to compare with.
inline float AuthoredReticleArtAspect(const AuthoredReticleArtCentre& centre) noexcept
{
    return centre.valid && centre.spreadV > 1.0e-6f ? centre.spreadU / centre.spreadV : 0.0f;
}

// `rows`: `size` x `size` RGBA8 texels, `rowPitch` bytes apart. `colourInk`
// weighs each texel by r+g+b (Halo 4's CUI target writes colour with a zero
// alpha channel), otherwise by alpha. Less than `minimumInk` in total is no art.
inline AuthoredReticleArtCentre MeasureAuthoredReticleArtCentre(
    const uint8_t* rows, size_t rowPitch, uint32_t size, bool colourInk,
    uint64_t minimumInk)
{
    AuthoredReticleArtCentre out{};
    if (!rows || size == 0 || rowPitch < static_cast<size_t>(size) * 4)
        return out;
    double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumYY = 0.0;
    uint64_t ink = 0;
    for (uint32_t y = 0; y < size; ++y)
    {
        const uint8_t* texel = rows + static_cast<size_t>(y) * rowPitch;
        for (uint32_t x = 0; x < size; ++x, texel += 4)
        {
            const uint32_t weight = colourInk
                ? static_cast<uint32_t>(texel[0]) + texel[1] + texel[2]
                : texel[3];
            if (!weight)
                continue;
            sumX += (x + 0.5) * weight;
            sumY += (y + 0.5) * weight;
            sumXX += (x + 0.5) * (x + 0.5) * weight;
            sumYY += (y + 0.5) * (y + 0.5) * weight;
            ink += weight;
        }
    }
    out.ink = ink;
    if (ink == 0 || ink < minimumInk)
        return out;
    const double total = static_cast<double>(ink);
    const double meanX = sumX / total, meanY = sumY / total;
    out.u = static_cast<float>(meanX / size);
    out.v = static_cast<float>(meanY / size);
    const double varianceX = sumXX / total - meanX * meanX, varianceY = sumYY / total - meanY * meanY;
    out.spreadU = static_cast<float>(std::sqrt(varianceX > 0.0 ? varianceX : 0.0) / size);
    out.spreadV = static_cast<float>(std::sqrt(varianceY > 0.0 ? varianceY : 0.0) / size);
    out.valid = std::isfinite(out.u) && std::isfinite(out.v);
    return out;
}

// Bloom and anti-aliasing move a capture's centroid by a texel or so from one
// sample to the next. The centre in use only follows a measurement that moves
// it by more than `deadband` (a fraction of the texture), so the quad does not
// shimmer with them; a new weapon or layout moves it at once. A sample that
// could not be measured keeps the centre in use.
inline AuthoredReticleArtCentre SettleAuthoredReticleArtCentre(
    const AuthoredReticleArtCentre& applied,
    const AuthoredReticleArtCentre& measured, float deadband)
{
    if (!measured.valid)
        return applied;
    if (!applied.valid || std::fabs(measured.u - applied.u) > deadband ||
        std::fabs(measured.v - applied.v) > deadband)
        return measured;
    return applied;
}

// The shift that puts the art's centre, not the texture's, on the quad's
// intended centre: metres along the quad's own right and up axes, for a square
// quad `width` wide whose texture runs top to bottom down it. Art above the
// texture centre (v < 0.5) moves the quad down. Clamped to a third of the
// quad, so a stray measurement cannot carry the reticle far off its ray.
struct AuthoredReticleQuadShift
{
    float right = 0.0f;
    float up = 0.0f;
};

inline AuthoredReticleQuadShift AuthoredReticleArtCentreShift(
    const AuthoredReticleArtCentre& centre, float width)
{
    constexpr float kMaximumOffset = 1.0f / 3.0f;
    AuthoredReticleQuadShift shift{};
    if (!centre.valid || !std::isfinite(width) || width <= 0.0f ||
        !std::isfinite(centre.u) || !std::isfinite(centre.v))
        return shift;
    const auto clampOffset = [](float value) {
        return value < -kMaximumOffset ? -kMaximumOffset
            : (value > kMaximumOffset ? kMaximumOffset : value);
    };
    shift.right = -clampOffset(centre.u - 0.5f) * width;
    shift.up = clampOffset(centre.v - 0.5f) * width;
    return shift;
}

// WHERE A CAPTURE LOOKS. A title whose capture cuts a fixed window out of the view (Halo 2: the texture's own
// size in view pixels, one to one, about the view's centre) holds its art only when the HUD draws the crosshair
// in the middle of the view. A reticle drawn elsewhere (Halo 2 draws a vehicle's well below the middle) lands
// off the texture's centre, cut by its edge when it is far enough, and the player aims with art that is not on
// the aim point: a Scorpion's shells land a degree and a half above its crosshair. The window follows the art
// instead: the measured centre of each capture moves it, so the art comes to rest in the middle, whole.
//
// Art that already sits near the middle is left exactly where the capture puts it (`engage`): a centred
// crosshair is drawn as it always was, whatever small asymmetry its art has. Once the window has moved it
// tracks the art to within `deadband`, and it is back in the middle outright when the art is. A measurement
// from a capture no newer than the window's last move describes another window and is ignored. Captures
// with no art in them while the window is away from the middle send it back there after `blankLimit` of them:
// what it followed is gone, and the middle is where a crosshair is looked for first.
struct ReticleCaptureFollow
{
    float x = 0.0f;              // where the window's centre sits, in view pixels right of the view's centre
    float y = 0.0f;              // ...and down from it
    uint64_t movedSerial = 0;    // captures after this prepared-frame serial were cut with (x, y)
    uint32_t blank = 0;          // consecutive artless captures while away from the middle
    uint32_t moves = 0;

    // `centre`: the measured centre of the capture taken in frame `captureSerial`; `nowSerial`: the frame being
    // prepared; `size`: the texture's side in texels (= view pixels). True when the window moved.
    bool Step(const AuthoredReticleArtCentre& centre, uint64_t captureSerial, uint64_t nowSerial, float size,
              float engage, float deadband, float limit, uint32_t blankLimit) noexcept
    {
        if (captureSerial <= movedSerial)
            return false;
        const bool away = x != 0.0f || y != 0.0f;
        if (!centre.valid)
        {
            if (!away || ++blank < blankLimit)
                return false;
            x = y = 0.0f;
            blank = 0;
            movedSerial = nowSerial;
            ++moves;
            return true;
        }
        blank = 0;
        const float dx = (centre.u - 0.5f) * size;
        const float dy = (centre.v - 0.5f) * size;
        if (!std::isfinite(dx) || !std::isfinite(dy))
            return false;
        const float reach = std::fabs(dx) > std::fabs(dy) ? std::fabs(dx) : std::fabs(dy);
        if (reach <= (away ? deadband : engage))
            return false;
        const auto bound = [limit](float value) {
            return value < -limit ? -limit : (value > limit ? limit : value);
        };
        float nextX = bound(x + dx), nextY = bound(y + dy);
        if (std::fabs(nextX) < engage * 0.5f && std::fabs(nextY) < engage * 0.5f)
            nextX = nextY = 0.0f;   // the art is back in the middle
        if (nextX == x && nextY == y)
            return false;
        x = nextX;
        y = nextY;
        movedSerial = nowSerial;
        ++moves;
        return true;
    }
};

// WHAT A CAPTURED CROSSHAIR TEXEL HOLDS, AND WHAT THE COMPOSITOR MUST BE GIVEN.
//
// The capture is cleared to transparent black and the game then draws its
// widget into it with its OWN blend state. Whatever that state is, a widget
// that looks translucent over the flat scene ADDS some colour C to it, and
// over a zero target that added colour is exactly what lands in RGB: the
// capture's colour is PREMULTIPLIED (colour already scaled by the widget's
// opacity). Its alpha channel is whatever the game's alpha blend factors left
// there (the coverage itself for the usual premultiplied/over encodings).
//
// The aim quad is submitted with XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT,
// so the runtime multiplies colour by alpha AGAIN. Copying the capture
// straight in would therefore apply the game's fade twice (C x A instead of C),
// and the sRGB decode of the already-darkened bytes dims it further: Reach's
// DMR, whose crosshair idles well below full opacity, then reads as far fainter
// in the headset than on the monitor.
//
// So it is converted to straight alpha before upload, in the shared blit shader's
// `fix()` (src/common/vr_blit_shader.h), which this function mirrors exactly:
// alpha = max(A, max(R,G,B)), colour = C / alpha. Straight-alpha compositing
// then adds alpha x C/alpha = C - precisely the colour the flat game adds,
// whatever encoding the alpha channel used - and covers the background by the
// capture's own coverage. Taking the max also keeps the result a valid colour
// (<= 1) when the alpha channel under-reports coverage.
struct AuthoredReticleStraightTexel
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

inline AuthoredReticleStraightTexel AuthoredReticleStraightAlpha(
    float r, float g, float b, float a)
{
    const float alpha = std::fmax(a, std::fmax(r, std::fmax(g, b)));
    AuthoredReticleStraightTexel out{r, g, b, alpha};
    if (alpha > 0.0f)
    {
        out.r = r / alpha;
        out.g = g / alpha;
        out.b = b / alpha;
    }
    return out;
}

// One channel of a quad layer composited over `background`, as OpenXR defines
// the two encodings (all values 0..1, same colour space on both sides).
inline float CompositeUnpremultipliedChannel(
    float colour, float alpha, float background)
{
    return colour * alpha + background * (1.0f - alpha);
}

inline float CompositePremultipliedChannel(
    float colour, float alpha, float background)
{
    return colour + background * (1.0f - alpha);
}

// Development evidence for the conversion above: what the capture holds, what
// the flat game adds, what the headset would be shown without the straight-alpha
// conversion and what it is shown with it. Values are 0..255 and come from the
// coverage probe's small mip level, so they are box-filtered MEANS of the art
// (a 1-texel line reads fainter there than it is); compare them with each
// other, not with a screenshot.
struct AuthoredReticleAlphaSummary
{
    uint32_t inkTexels = 0;
    // Texels whose colour exceeds their alpha: the alpha channel
    // under-reports coverage, so the game's alpha blend factors are not the
    // premultiplied/over pair. Zero means the classic premultiplied encoding.
    uint32_t alphaBelowColourTexels = 0;
    uint32_t capturedAlphaPeak = 0;
    uint32_t capturedAlphaMean = 0;
    // Brightest colour channel = the colour the flat game ADDS to the scene.
    uint32_t addedColourPeak = 0;
    // What the headset would add unconverted: colour x alpha a second time.
    uint32_t legacyAddedColourPeak = 0;
    // Alpha the compositor is given (straight), and the colour it adds.
    uint32_t drawnAlphaPeak = 0;
    uint32_t drawnAlphaMean = 0;
    uint32_t drawnAddedColourPeak = 0;
};

inline AuthoredReticleAlphaSummary SummarizeAuthoredReticleAlpha(
    const uint8_t* rows, size_t rowPitch, uint32_t size)
{
    AuthoredReticleAlphaSummary out{};
    if (!rows || size == 0 || rowPitch < static_cast<size_t>(size) * 4)
        return out;
    uint64_t capturedSum = 0;
    uint64_t drawnSum = 0;
    for (uint32_t y = 0; y < size; ++y)
    {
        const uint8_t* texel = rows + static_cast<size_t>(y) * rowPitch;
        for (uint32_t x = 0; x < size; ++x, texel += 4)
        {
            const uint32_t colour = texel[0] > texel[1]
                ? (texel[0] > texel[2] ? texel[0] : texel[2])
                : (texel[1] > texel[2] ? texel[1] : texel[2]);
            const uint32_t alpha = texel[3];
            if (!colour && !alpha)
                continue;
            const uint32_t drawn = alpha > colour ? alpha : colour;
            const AuthoredReticleStraightTexel straight =
                AuthoredReticleStraightAlpha(
                    colour / 255.0f, 0.0f, 0.0f, alpha / 255.0f);
            const uint32_t drawnAdded = static_cast<uint32_t>(
                straight.r * straight.a * 255.0f + 0.5f);
            const uint32_t legacy = (colour * alpha + 127) / 255;
            ++out.inkTexels;
            if (colour > alpha)
                ++out.alphaBelowColourTexels;
            capturedSum += alpha;
            drawnSum += drawn;
            if (alpha > out.capturedAlphaPeak) out.capturedAlphaPeak = alpha;
            if (colour > out.addedColourPeak) out.addedColourPeak = colour;
            if (legacy > out.legacyAddedColourPeak)
                out.legacyAddedColourPeak = legacy;
            if (drawn > out.drawnAlphaPeak) out.drawnAlphaPeak = drawn;
            if (drawnAdded > out.drawnAddedColourPeak)
                out.drawnAddedColourPeak = drawnAdded;
        }
    }
    if (out.inkTexels)
    {
        out.capturedAlphaMean =
            static_cast<uint32_t>(capturedSum / out.inkTexels);
        out.drawnAlphaMean = static_cast<uint32_t>(drawnSum / out.inkTexels);
    }
    return out;
}
