#pragma once

#include <cstdint>
#include "runtime_types.h"

// Import shims may call a hooked export (and exports can forward to each other).
// Only the outermost call may fabricate/merge a pad; inner calls return native
// input so our own locomotion is never mistaken for physical stick movement.
class InputPollMergeScope
{
public:
    explicit InputPollMergeScope(unsigned& depth) noexcept
        : m_depth(depth), m_outermost(depth++ == 0) {}
    ~InputPollMergeScope() { --m_depth; }
    bool IsOutermost() const noexcept { return m_outermost; }
    InputPollMergeScope(const InputPollMergeScope&) = delete;
    InputPollMergeScope& operator=(const InputPollMergeScope&) = delete;
private:
    unsigned& m_depth;
    bool m_outermost;
};

struct MenuChordResult
{
    bool toggled = false;
    bool consumeClicks = false;
};

class MenuChordDetector
{
public:
    MenuChordResult Update(uint64_t nowMs, bool leftClick, bool rightClick);
    void Reset();

private:
    uint64_t m_firstDownMs = 0;
    uint8_t m_firstSide = 0;
    bool m_latched = false;
    bool m_expired = false;
};

struct MenuPointerHit
{
    bool hit = false;
    float u = 0.0f;
    float v = 0.0f;
};

// Ray/panel intersection in anchor-local space. `centerX`/`centerY` place the
// panel off straight-ahead, which is what the grab handle moves; they must match
// the composition quad's own offsets or the pointer lands somewhere the user
// cannot see.
MenuPointerHit IntersectMenuQuad(const float origin[3], const float direction[3],
    float distance, float width, float height, float centerY, float centerX = 0.0f);

float BlendXInputMotors(uint16_t lowFrequencyMotor, uint16_t highFrequencyMotor);

// Distance is from the tracked head centre, preserving the original 30 cm
// gesture by default. Title-specific side-of-head requirements stay at caller.
// A Project Reclaimer key from the controller: a button with the left controller at the head presses the key, and the
// button's release lets it go, so the host's own handling (its Hold or Toggle talk mode, say) decides what that means.
// Only a fresh press at the head starts it, the button then belongs to the key until it is let go, and a press that
// stops being allowed lets the key go.
struct HeadKeyGesture
{
    enum class Key { None, Press, Release };
    // The host misses a key held much under this, so a quick press stays down this long. Stopping being allowed still
    // lets go at once.
    static constexpr uint64_t kMinHoldMs = 150;
    bool talking = false;
    bool xWasDown = false;
    uint64_t pressedAtMs = 0;
    Key Step(bool atHead, bool xDown, bool allowed, uint64_t nowMs) noexcept
    {
        const bool rising = xDown && !xWasDown;
        xWasDown = xDown;
        if (talking)
        {
            if (allowed && (xDown || nowMs - pressedAtMs < kMinHoldMs))
                return Key::None;
            talking = false;
            return Key::Release;
        }
        if (rising && atHead && allowed)
        {
            talking = true;
            pressedAtMs = nowMs;
            return Key::Press;
        }
        return Key::None;
    }
};

// Y at the head gives way to the Y + B pause. Y waits kHoldOffMs for B before it counts: a Tab tap can leave the host's
// scoreboard up, so not even a moment of it may go out ahead of a chord. Y away from the head is the game's at once. B at
// any point in the press gives the whole press to the chord, until Y is let go. A tap let go during the wait counts on
// its release, so the wait never swallows a quick press.
struct HeadYChord
{
    static constexpr uint64_t kHoldOffMs = 120;
    enum class Y { Game, Waiting, Key };
    uint64_t downSinceMs = 0;
    bool atHead = false;
    bool chord = false;
    bool counted = false;
    Y Step(bool yDown, bool atHeadNow, bool bDown, bool chordEnabled, uint64_t nowMs) noexcept
    {
        if (!yDown)
        {
            const bool tap = downSinceMs && atHead && !chord && !counted;
            downSinceMs = 0;
            atHead = chord = counted = false;
            return tap ? Y::Key : Y::Game;
        }
        if (!downSinceMs)
        {
            downSinceMs = nowMs ? nowMs : 1;
            atHead = atHeadNow;
        }
        if (!atHead)
            return Y::Game;
        if (chordEnabled && bDown)
            chord = true;
        if (chord)
            return Y::Game;
        if (chordEnabled && nowMs - downSinceMs < kHoldOffMs)
            return Y::Waiting;
        counted = true;
        return Y::Key;
    }
};

// The head D-pad gesture when a hand comes up while walking: the stick goes on walking until it returns to centre, and
// only then does it become the D-pad. `moving` false always gives the plain gesture.
struct DpadMovementLatch
{
    bool walking = false;
    bool wasAtHead = false;
    bool Step(bool atHead, bool moving) noexcept
    {
        if (!atHead)
        {
            walking = wasAtHead = false;
            return false;
        }
        if (!moving)
            walking = false;
        else if (!wasAtHead)
            walking = true;
        wasAtHead = true;
        return !walking;
    }
};

bool DpadHeadWithinRadius(const float head[3], const float controller[3],
    float radiusMetres) noexcept;

struct DpadStickInput
{
    bool active = false;
    float x = 0.0f, y = 0.0f;
};
// Consume the physical right stick before any title, turn or scope snapshot.
// Only the optional thumb-rest gesture changes those axes; left movement,
// handedness, buttons and the existing head gesture are independent.
DpadStickInput ConsumeThumbrestDpad(bool enabled, bool touched, bool inputReady,
    float& rightX, float& rightY) noexcept;
uint16_t DpadDirectionButtons(float x, float y) noexcept;

// Peak-hold haptic sampling. Halo drives controller rumble as short XInput
// SetState pulses, but the VR frame loop samples the requested amplitude far
// less often than those pulses arrive. A plain last-value latch therefore
// aliases a brief gunfire pulse (SetState(high) then SetState(0) within one
// frame) to zero and drops it, so shooting stops rumbling reliably. Peak-hold
// keeps the maximum amplitude requested since the last applied frame: `peak` is
// that running maximum and `latest` the most recent instantaneous value.
// `apply` = max(peak, latest) and `carry` = latest, so a one-shot pulse fires
// exactly once and a sustained rumble persists. All values are clamped to [0,1].
struct HapticPeakSample
{
    float apply = 0.0f;
    float carry = 0.0f;
};

HapticPeakSample SampleHapticPeak(float peak, float latest);

// Contact feedback is hand-specific while Halo's ordinary XInput rumble is
// shared. Each hand receives the stronger request, clamped to OpenXR's legal
// amplitude range, so collision feedback cannot suppress weapon/vehicle rumble.
float MergeHapticAmplitude(float game, float contact);

// The mod owns virtual slot 0 when no physical pad is present. A valid
// XInputSetState request must therefore observe a connected controller even
// when title capability policy suppresses the actual haptic effect.
uint32_t NormalizeVirtualXInputSetStateResult(
    uint32_t originalResult, uint32_t userIndex, bool hasVibrationRequest);

// Menu/Start is always passed through as controller input, but only a proven
// Halo 3 gameplay owner may use its edge to change VR pause presentation.
bool PausePresentationInputAllowed(bool sharedGameplayOwner);

// The Y+B fallback may inject Start for either Halo 3's shared gameplay path or
// a title-specific ODST/Reach/Halo 4 owner. This is deliberately separate from
// PausePresentationInputAllowed: ODST and Reach follow their proven native pause
// flags instead of guessing presentation state from the controller edge.
bool PauseToggleInputAllowed(
    bool sharedGameplayOwner, bool titleSpecificPauseOwner);

// ODST's camera-only ownership may not publish an explicit active title, while
// Reach and Halo 4 do. Keep that title admission in one testable helper so a
// newly explicit title cannot silently lose the shared Y+B chord.
inline constexpr bool TitleSpecificPauseToggleOwner(
    GameTitle activeTitle, bool odstCameraOnly)
{
    return odstCameraOnly || activeTitle == GameTitle::HaloReach ||
        activeTitle == GameTitle::Halo4 || activeTitle == GameTitle::Halo2 ||
        activeTitle == GameTitle::HaloCE;
}

// Reach's on-foot VR layout trades the physical left trigger and X button so
// armour ability stays on the trigger and grenade moves to X. Vehicles must
// receive Reach's native layout instead. `playerInVehicleProven` is deliberately
// fail-open for the existing remap: an unavailable or stale title sample is
// false, so a failed optional detector cannot silently change on-foot controls.
inline constexpr bool ReachShouldSwapLeftHandActions(
    bool reachActive, bool playerInVehicleProven)
{
    return reachActive && !playerInVehicleProven;
}

// Hold-mode two-hand aiming uses the barrel zone only to acquire the support
// grip. Once acquired, ordinary movement of either hand must not drop the hold;
// releasing the grip is the explicit disengage action.
bool UpdateTwoHandHold(bool wasEngaged, bool gripHeld, bool inGrabZone);

// Tracks the loading gap caused by Restart Level while Halo's pause menu owns
// the screen. Restart bypasses the normal Start/unpause edge, so the next
// stable gameplay camera must clear the 2D pause presentation.
class PauseLevelRecovery
{
public:
    bool Update(bool pausePresentation, bool cameraStale, bool levelStable);
    void Reset();

private:
    bool m_sawLoading = false;
};
