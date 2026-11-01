#pragma once
// THE VEHICLE CAMERA BUTTONS, in a seat the vehicle cameras run in (vehicle_cameras.inl):
//
//   left Y tap        the next camera
//   left X tap        the camera's next tethering mode
//   left Y or X held  re-centre the view (the moment the hold is long enough, not on release)
//   left stick click  the next way to control the seat (vehicle_control: hand, stick, wheel)
//   right B           the game's own B -- in Halo 3 a vehicle's B action, the Banshee's bomb -- unless the action
//                     mapper is on and binds B to nothing (its default): B then does nothing in the game, and right
//                     B is what left X does there everywhere else (use, exit the vehicle, reload), the only way out
//                     of a seat whose left X is the tethering button (RightBFor)
//   support trigger   in a passenger's seat, switch weapon (what left Y does everywhere else)
//
// The three left buttons are the mod's alone while this holds: none of them reaches the game. A tap acts on
// RELEASE, so a hold can mean something else. Only a press that STARTS in the seat counts: the first poll in
// a seat records what is already down (the button that boarded the vehicle is still held when the seat comes
// up) and acts on nothing. A left stick click joined by the right stick's is the menu's chord, not a click, and
// left Y joined by right B is the pause chord (the caller reads it from the buttons as they were): that Y is
// not a tap or a hold, and that B is neither the game's B nor its use.
//
// Pure: the caller hands in the poll's buttons and gets back what reaches the game and what was asked for. Keep ONE
// instance for every thread that polls the pad and step it under a lock, with the time read inside the lock: a
// copy per thread sees each press once per thread, and a rarely polling copy can miss a hold and call it a tap.

#include <cmath>
#include <cstdint>

namespace vehicle_seat_buttons
{
inline constexpr float kTriggerPress = 0.6f;
inline constexpr float kTriggerRelease = 0.3f;
inline constexpr uint32_t kDefaultHoldMs = 1000;

// What right B gives the game in a seat.
enum class RightB : uint8_t
{
    Itself,    // the game's B
    LeftXsJob, // what left X does in the game everywhere else: use, exit, reload
};

// `actionMapperOn`: vr_action_mapping, where a VR button reaches the game as the action bound to it.
// `bBoundToAnAction`: the mapper binds right B to some action (by default it binds it to none).
inline constexpr RightB RightBFor(bool actionMapperOn, bool bBoundToAnAction) noexcept
{
    return actionMapperOn && !bBoundToAnAction ? RightB::LeftXsJob : RightB::Itself;
}

// The buttons this works on, as the pad has them: the support trigger is the hand that does not hold the gun.
struct Buttons
{
    bool x = false, y = false, b = false;
    bool clickL = false, clickR = false;
    float supportTrigger = 0.0f;
};

struct Requests
{
    int camera = 0; // +1: the next camera
    int mode = 0;   // +1: the camera's next tethering mode
    bool control = false;
    bool recenter = false;
};

class Seat
{
public:
    // One poll. `live`: the vehicle cameras run in the seat the player is in. `passenger`: it is a passenger's.
    // `holdMs`: how long a hold re-centres (0 = never). `rightB`: what right B gives the game (RightBFor).
    // `buttons` is changed in place to what the game gets.
    Requests Step(Buttons& buttons, bool live, bool passenger, uint64_t nowMs, uint32_t holdMs,
                  RightB rightB = RightB::Itself) noexcept
    {
        Requests asked;
        if (!live)
        {
            *this = Seat{};
            return asked;
        }
        const Buttons raw = buttons;
        if (raw.y && raw.b)
            pauseChord_ = true;
        else if (!raw.y && !raw.b)
            pauseChord_ = false;
        if (pauseChord_)
            y_.downMs = 0;
        if (primed_)
        {
            Tap(x_, raw.x, nowMs, holdMs, asked.recenter, asked.mode);
            Tap(y_, raw.y, nowMs, holdMs, asked.recenter, asked.camera);
            if (raw.clickL && !clickWas_)
            {
                clickArmed_ = true;
                clickChord_ = false;
            }
            if (clickArmed_ && raw.clickR)
                clickChord_ = true;
            if (clickArmed_ && !raw.clickL)
            {
                asked.control = !clickChord_;
                clickArmed_ = false;
            }
        }
        x_.was = raw.x;
        y_.was = raw.y;
        clickWas_ = raw.clickL;
        primed_ = true;

        // What reaches the game.
        const bool rightBDown = raw.b && !pauseChord_;
        buttons.x = rightB == RightB::LeftXsJob && rightBDown;
        buttons.b = rightB == RightB::Itself && rightBDown;
        buttons.y = false;
        buttons.clickL = false;
        if (passenger)
        {
            const float pull = std::isfinite(raw.supportTrigger) ? raw.supportTrigger : 0.0f;
            triggerDown_ = triggerDown_ ? pull > kTriggerRelease : pull > kTriggerPress;
            buttons.y = triggerDown_;
            buttons.supportTrigger = 0.0f;
        }
        else
        {
            triggerDown_ = false;
        }
        return asked;
    }

private:
    struct Press
    {
        bool was = false;
        uint64_t downMs = 0; // when a press that started in the seat began; 0 = none
        bool held = false;   // it has already re-centred
    };

    static void Tap(Press& press, bool now, uint64_t nowMs, uint32_t holdMs, bool& recenter, int& step) noexcept
    {
        if (now && !press.was)
        {
            press.downMs = nowMs ? nowMs : 1;
            press.held = false;
        }
        if (now && press.downMs && !press.held && holdMs > 0 && nowMs - press.downMs >= holdMs)
        {
            press.held = true;
            recenter = true;
        }
        if (!now && press.was)
        {
            if (press.downMs && !press.held)
                step = 1;
            press.downMs = 0;
        }
    }

    bool primed_ = false;
    bool pauseChord_ = false; // left Y and right B have been down together since both were last up
    Press x_, y_;
    bool clickWas_ = false, clickArmed_ = false, clickChord_ = false;
    bool triggerDown_ = false;
};
} // namespace vehicle_seat_buttons
