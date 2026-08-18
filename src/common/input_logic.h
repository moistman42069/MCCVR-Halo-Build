#pragma once

#include <cstdint>
#include "runtime_types.h"

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
        activeTitle == GameTitle::Halo4;
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
