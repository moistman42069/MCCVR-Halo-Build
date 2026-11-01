// The vehicle camera buttons in a seat (src/common/vehicle_seat_buttons.h): taps, holds, the left stick click and
// its chord, the press that boarded the vehicle, and what reaches the game.
#include <cstdio>
#include <cstdlib>
#include "../src/common/vehicle_seat_buttons.h"

using namespace vehicle_seat_buttons;

static unsigned g_checks = 0;
static void Check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct Poll
{
    Seat seat;
    uint64_t now = 1000;
    Buttons out;
    RightB rightB = RightB::LeftXsJob;
    Requests Run(Buttons in, bool live = true, bool passenger = false, uint64_t step = 10)
    {
        now += step;
        out = in;
        return seat.Step(out, live, passenger, now, kDefaultHoldMs, rightB);
    }
};

static Buttons With(bool x, bool y, bool b = false, bool l3 = false, bool r3 = false, float trigger = 0.0f)
{
    Buttons buttons;
    buttons.x = x;
    buttons.y = y;
    buttons.b = b;
    buttons.clickL = l3;
    buttons.clickR = r3;
    buttons.supportTrigger = trigger;
    return buttons;
}

static void TestTaps()
{
    Poll p;
    Check(p.Run(With(false, false)).camera == 0, "the first poll in a seat asks for nothing");
    Requests r = p.Run(With(false, true));
    Check(r.camera == 0 && !r.recenter, "a press asks for nothing yet");
    Check(!p.out.y, "and left Y does not reach the game");
    r = p.Run(With(false, false));
    Check(r.camera == 1 && r.mode == 0 && !r.recenter, "its release steps the camera");
    p.Run(With(true, false));
    Check(!p.out.x, "left X does not reach the game");
    r = p.Run(With(false, false));
    Check(r.mode == 1 && r.camera == 0, "a tap of left X steps the tethering mode");
}

static void TestHold()
{
    Poll p;
    p.Run(With(false, false));
    p.Run(With(true, false));
    Requests r = p.Run(With(true, false), true, false, 500);
    Check(!r.recenter, "half a second is not yet a hold");
    r = p.Run(With(true, false), true, false, 600);
    Check(r.recenter && r.mode == 0, "a second re-centres, while the button is still held");
    r = p.Run(With(true, false), true, false, 600);
    Check(!r.recenter, "once");
    r = p.Run(With(false, false));
    Check(r.mode == 0 && !r.recenter, "and the release steps nothing");
    p.Run(With(false, true));
    r = p.Run(With(false, true), true, false, 1100);
    Check(r.recenter, "left Y held re-centres too");
    Check(p.Run(With(false, false)).camera == 0, "and steps no camera");

    Poll never;
    never.out = With(false, false);
    never.seat.Step(never.out, true, false, 5, 0);
    never.out = With(true, false);
    never.seat.Step(never.out, true, false, 10, 0);
    never.out = With(true, false);
    Check(!never.seat.Step(never.out, true, false, 5000, 0).recenter, "a hold of 0 never re-centres");
    never.out = With(false, false);
    Check(never.seat.Step(never.out, true, false, 5010, 0).mode == 1, "and a long press is then still a tap");
}

// Several threads poll the pad, each at its own rate, into the one state machine: a poll that comes rarely is just
// another sample. The hold re-centres once, and the rare poll that next sees the button up steps nothing.
static void TestPollsFromSeveralThreads()
{
    Seat seat;
    Buttons out;
    uint64_t now = 1000;
    out = With(false, false);
    seat.Step(out, true, false, now, kDefaultHoldMs);
    unsigned recentres = 0;
    int steps = 0;
    // The frequent poller every 16 ms while left Y is held for 1.4 s, a rare one every 600 ms.
    for (uint64_t t = 16; t <= 1400; t += 16)
    {
        out = With(false, true);
        const Requests r = seat.Step(out, true, false, now + t, kDefaultHoldMs);
        recentres += r.recenter ? 1u : 0u;
        steps += r.camera;
        if (t % 592 == 0)
        {
            out = With(false, true);
            const Requests rare = seat.Step(out, true, false, now + t, kDefaultHoldMs);
            recentres += rare.recenter ? 1u : 0u;
            steps += rare.camera;
        }
    }
    out = With(false, false);
    steps += seat.Step(out, true, false, now + 1800, kDefaultHoldMs).camera;
    out = With(false, false);
    steps += seat.Step(out, true, false, now + 1810, kDefaultHoldMs).camera;
    Check(recentres == 1, "a hold polled from several threads re-centres once");
    Check(steps == 0, "and its release, whichever thread sees it, steps no camera");
}

// Left Y with right B is the pause chord: neither a camera step nor a re-centre, and B is not the game's use
// while it lasts, whichever of the two is let go first.
static void TestPauseChord()
{
    Poll p;
    p.Run(With(false, false));
    p.Run(With(false, true));
    Requests r = p.Run(With(false, true, true));
    Check(!p.out.x && !p.out.y, "left Y with right B: the chord, so B is not the game's use");
    r = p.Run(With(false, true, true), true, false, 1200);
    Check(!r.recenter, "held together they do not re-centre");
    r = p.Run(With(false, false, true));
    Check(r.camera == 0 && !p.out.x, "letting go of Y steps no camera, and B still held is not use");
    r = p.Run(With(false, false, false));
    Check(r.camera == 0, "nor does letting go of B");
    p.Run(With(false, false, true));
    Check(p.out.x, "a later B on its own is the game's use again");
    p.Run(With(false, false, false));
    p.Run(With(false, true));
    Check(p.Run(With(false, false)).camera == 1, "and a later Y on its own steps the camera");
}

static void TestBoardingPress()
{
    Poll p;
    Check(p.Run(With(true, true)).mode == 0, "the buttons that boarded are held when the seat comes up");
    Requests r = p.Run(With(false, false));
    Check(r.mode == 0 && r.camera == 0, "and their release steps nothing");
    p.Run(With(true, false));
    Check(p.Run(With(false, false)).mode == 1, "a press that starts in the seat does");
}

static void TestLeavingTheSeat()
{
    Poll p;
    p.Run(With(false, false));
    p.Run(With(false, true));
    Buttons out = With(false, true);
    Requests r = p.Run(out, false);
    Check(r.camera == 0 && p.out.y, "out of the seat nothing is asked and the game has left Y again");
    r = p.Run(With(false, false), true);
    Check(r.camera == 0, "a press from before the seat is not one when the seat comes back");
}

static void TestStickClick()
{
    Poll p;
    p.Run(With(false, false));
    p.Run(With(false, false, false, true));
    Check(!p.out.clickL, "the left stick click does not reach the game");
    Check(p.Run(With(false, false)).control, "its release asks for the next way to control the seat");
    p.Run(With(false, false, false, true));
    p.Run(With(false, false, false, true, true));
    Check(!p.Run(With(false, false, false, false, true)).control, "joined by the right stick click it is the menu's chord");
    Check(!p.Run(With(false, false)).control, "and nothing follows");
    Check(p.Run(With(false, false, false, true)).control == false, "a click held over the seat's first poll");
    Poll boarded;
    boarded.Run(With(false, false, false, true));
    Check(!boarded.Run(With(false, false)).control, "a click that was down when the seat came up is not one");
}

static void TestWhatTheGameGets()
{
    Poll p;
    p.Run(With(false, false, true));
    Check(p.out.x && !p.out.b, "with the mapper binding B to nothing, right B is what left X is everywhere else");
    p.Run(With(true, false, false));
    Check(!p.out.x, "and left X is not");

    Poll own;
    own.rightB = RightB::Itself;
    own.Run(With(false, false, true));
    Check(own.out.b && !own.out.x, "otherwise right B is the game's own B: the Banshee's bomb");
    own.Run(With(false, false, false));
    Check(!own.out.b, "for as long as it is held");
    own.Run(With(false, true, false));
    own.Run(With(false, true, true));
    Check(!own.out.b && !own.out.y, "and left Y with right B is the pause chord, not B");

    Check(RightBFor(true, false) == RightB::LeftXsJob, "the mapper on, B bound to nothing: left X's job");
    Check(RightBFor(true, true) == RightB::Itself, "the mapper on, B bound to an action: that action");
    Check(RightBFor(false, false) == RightB::Itself && RightBFor(false, true) == RightB::Itself,
          "the mapper off: the game's B");
    p.Run(With(false, false, false, false, false, 0.9f));
    Check(!p.out.y && p.out.supportTrigger == 0.9f, "a driver's or gunner's support trigger is its own");

    Poll rider;
    rider.Run(With(false, false), true, true);
    rider.Run(With(false, false, false, false, false, 0.7f), true, true);
    Check(rider.out.y && rider.out.supportTrigger == 0.0f, "a passenger's support trigger switches weapon");
    rider.Run(With(false, false, false, false, false, 0.4f), true, true);
    Check(rider.out.y, "held until it is let go");
    rider.Run(With(false, false, false, false, false, 0.2f), true, true);
    Check(!rider.out.y, "and released below the release point");
    rider.Run(With(false, true, false, false, false, 0.0f), true, true);
    Check(!rider.out.y, "left Y is the camera's in a passenger seat too");
}

int main()
{
    TestTaps();
    TestHold();
    TestPollsFromSeveralThreads();
    TestPauseChord();
    TestBoardingPress();
    TestLeavingTheSeat();
    TestStickClick();
    TestWhatTheGameGets();
    std::printf("PASS: %u vehicle seat button checks\n", g_checks);
    return 0;
}
