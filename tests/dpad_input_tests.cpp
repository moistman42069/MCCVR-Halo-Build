#include "../src/common/input_logic.h"
#include "../src/common/aim_no_unit_logic.h"
#include "../src/common/exclusive_input.h"
#include "../src/common/flashlight_input.h"
#include <windows.h>
#include <Xinput.h>
#include "../src/common/weapon_hand_logic.h"
#include "../src/dll/vr.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

namespace
{
unsigned checks = 0, failures = 0;
void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", name); }
}

void Sample(VrPadState& pad, bool enabled, bool touched, bool inputReady)
{
    const auto selected = ConsumeThumbrestDpad(enabled, touched, inputReady,
        pad.turnX, pad.turnY);
    pad.thumbrestDpad = selected.active;
    pad.dpadX = selected.x;
    pad.dpadY = selected.y;
}
}

int main()
{
    for(uint32_t title=1;title<=6;++title) for(int button=0;button<flashlight_input::kCount;++button) {
        flashlight_input::Filter filter;
        const uint32_t selected=flashlight_input::Mask(button);
        auto full=[] {XINPUT_GAMEPAD p{};p.wButtons=0xffff;p.bLeftTrigger=p.bRightTrigger=255;
            p.sThumbLX=12345;p.sThumbRY=-7654;return p;};
        auto pad=full();filter.Apply(pad,false,true,title,button);
        Check(pad.wButtons==0xffff&&pad.bLeftTrigger==255&&pad.bRightTrigger==255,
            "flashlight option off preserves existing controls");
        pad=full();filter.Apply(pad,true,true,title,button);
        Check(pad.wButtons==uint16_t(0xffff&~selected)&&
            pad.bLeftTrigger==((selected&(1u<<16))?0:255)&&
            pad.bRightTrigger==((selected&(1u<<17))?0:255)&&
            pad.sThumbLX==12345&&pad.sThumbRY==-7654,
            "flashlight filter removes only configured final merged control for each title");
        pad=full();filter.Apply(pad,false,true,title,button);
        Check(pad.wButtons==uint16_t(0xffff&~selected)&&
            pad.bLeftTrigger==((selected&(1u<<16))?0:255)&&pad.bRightTrigger==((selected&(1u<<17))?0:255),
            "disabling flashlight filter cannot expose an already consumed hold");
        pad={};filter.Apply(pad,false,true,title,button);
        pad=full();filter.Apply(pad,false,true,title,button);
        Check(pad.wButtons==0xffff&&pad.bLeftTrigger==255&&pad.bRightTrigger==255,
            "released flashlight control works normally after option is disabled");
        pad=full();filter.Apply(pad,true,false,title,button);
        Check(pad.wButtons==0xffff&&pad.bLeftTrigger==255&&pad.bRightTrigger==255,
            "flashlight filter keeps menus and F1 usable");
    }
    Check(flashlight_input::Mask(flashlight_input::kGripDefault)==XINPUT_GAMEPAD_LEFT_SHOULDER,"default blocks actual support grip output, not right bumper");
    Check(std::strcmp(flashlight_input::Label(15,false,false),"Support grip: left (recommended)")==0&&
        std::strcmp(flashlight_input::Label(15,true,false),"Support grip: right (recommended)")==0,"Quest grip labels follow handedness");
    Check(std::strcmp(flashlight_input::Label(6,false,true),"Left trigger (X)")==0&&
        std::strcmp(flashlight_input::Label(13,false,true),"Left controller X (LT)")==0,"Reach Quest labels follow trigger-X swap");
    Check(!flashlight_input::Mask(-1)&&!flashlight_input::Mask(99),"invalid flashlight mapping cannot block arbitrary input");
    const float head[3]{}, diagonalInside[3]{0.2f, 0.2f, 0},
        diagonalOutside[3]{0.18f, 0.18f, 0.18f};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    struct RadiusCase { float radius, distance; bool inside; };
    const RadiusCase radii[]{
        {0.30f, 0.299f, true}, {0.30f, 0.30f, false}, {0.30f, 0.301f, false},
        {0.10f, 0.099f, true}, {0.10f, 0.10f, false},
        {0.50f, 0.499f, true}, {0.50f, 0.50f, false},
        {-1.0f, 0.099f, true}, {-1.0f, 0.101f, false},
        {2.0f, 0.499f, true}, {2.0f, 0.501f, false},
        {nan, 0.299f, true}, {nan, 0.301f, false},
        {infinity, 0.299f, true}, {-infinity, 0.301f, false}};
    for (const auto& test : radii)
    {
        const float controller[3]{test.distance, 0, 0};
        Check(DpadHeadWithinRadius(head, controller, test.radius) == test.inside,
            "radius preserves strict boundary, limits and invalid-setting fallback");
    }
    {
        using Key = HeadKeyGesture::Key;
        HeadKeyGesture talk;
        Check(talk.Step(false, true, true) == Key::None && !talk.talking, "X away from the head is the game's X");
        Check(talk.Step(true, true, true) == Key::None && !talk.talking, "a hand raised with X already held does not talk");
        Check(talk.Step(true, false, true) == Key::None, "and letting go does nothing");
        Check(talk.Step(true, true, true) == Key::Press && talk.talking, "a fresh X at the head presses the key");
        Check(talk.Step(true, true, true) == Key::None && talk.talking, "held, once");
        Check(talk.Step(false, true, true) == Key::None && talk.talking, "lowering the hand keeps it held");
        Check(talk.Step(false, false, true) == Key::Release && !talk.talking, "X's release lets it go");
        Check(talk.Step(true, false, true) == Key::None, "nothing is down");
        Check(talk.Step(true, true, true) == Key::Press, "pressed again");
        Check(talk.Step(true, true, false) == Key::Release && !talk.talking, "focus lost or the menu opened lets it go");
        Check(talk.Step(true, true, true) == Key::None && !talk.talking, "X still held does not press again");
        Check(talk.Step(true, false, true) == Key::None, "until it is let go");
        Check(talk.Step(true, true, false) == Key::None && !talk.talking, "not allowed: no press");
    }
    {
        using Y = HeadYChord::Y;
        HeadYChord y;
        Check(y.Step(true, false, false, true, 1000) == Y::Game, "Y away from the head is the game's at once");
        Check(y.Step(true, true, false, true, 2000) == Y::Game, "and stays so if the hand comes up during that press");
        y.Step(false, true, false, true, 2100);
        Check(y.Step(true, true, false, true, 3000) == Y::Waiting, "Y at the head waits for B");
        Check(y.Step(true, true, false, true, 3000 + HeadYChord::kHoldOffMs) == Y::Key, "alone, it counts");
        Check(y.Step(true, true, true, true, 3200) == Y::Game, "B with the scoreboard up gives the press to the chord");
        Check(y.Step(true, true, false, true, 3300) == Y::Game, "for the rest of the press");
        y.Step(false, true, false, true, 3400);
        Check(y.Step(true, true, false, true, 4000) == Y::Waiting, "a new press waits again");
        Check(y.Step(true, true, true, true, 4050) == Y::Game, "B during the wait: nothing is sent");
        y.Step(false, true, false, true, 4100);
        Check(y.Step(true, true, true, true, 5000) == Y::Game, "B first: the chord");
        y.Step(false, true, false, true, 5100);
        Check(y.Step(true, true, true, false, 6000) == Y::Key, "the chord turned off: no wait, and B changes nothing");
    }
    {
        // A screen holds the controller only while the character is really gone, and lets go at once.
        using aim_no_unit::Presence;
        aim_no_unit::NoCharacterNow none;
        Check(!none.Step(Presence::Absent, 1000, 1000), "a blip of no character takes nothing");
        Check(none.Step(Presence::Absent, 1000 + aim_no_unit::kEnterMs, 1000 + aim_no_unit::kEnterMs), "a held absence does");
        Check(!none.Step(Presence::Present, 1300, 1300), "a respawn lets go on its first sample");
        Check(!none.Step(Presence::Absent, 1350, 1350), "and an absence starts its wait again");
        Check(!none.Step(Presence::Absent, 1350, 1350 + aim_no_unit::kStaleMs + 1), "a sampler gone quiet holds nothing");
    }
    {
        DpadMovementLatch latch;
        Check(!latch.Step(false, true), "hand down: never the D-pad");
        Check(!latch.Step(true, true), "hand comes up while walking: the stick keeps walking");
        Check(!latch.Step(true, true), "and goes on walking while it stays pushed");
        Check(latch.Step(true, false), "the stick returns to centre: now the D-pad");
        Check(latch.Step(true, true), "pushed again from centre at the head: a D-pad press");
        Check(!latch.Step(false, true), "hand down: walking again");
        DpadMovementLatch still;
        Check(still.Step(true, false), "hand comes up standing still: the D-pad at once");
        Check(still.Step(true, true), "and the stick presses it");
    }
    Check(DpadHeadWithinRadius(head, diagonalInside, 0.30f) &&
        !DpadHeadWithinRadius(head, diagonalOutside, 0.30f), "head radius measures all three axes");
    Check(!DpadHeadWithinRadius(nullptr, head, 0.30f) &&
        !DpadHeadWithinRadius(head, nullptr, 0.30f), "missing pose cannot activate gesture");
    for (float invalid : {nan, infinity, -infinity})
    {
        const float badPose[3]{0, invalid, 0};
        Check(!DpadHeadWithinRadius(head, badPose, 0.30f) &&
            !DpadHeadWithinRadius(badPose, head, 0.30f), "invalid tracked pose stays inactive");
    }

    VrPadState defaults{};
    Check(!defaults.thumbrestDpad && defaults.dpadX == 0 && defaults.dpadY == 0,
        "existing default pad has no optional gesture");
    for (unsigned flags = 0; flags < 8; ++flags)
    {
        VrPadState pad{};
        pad.valid = true; pad.moveX = -0.7f; pad.moveY = 0.8f;
        pad.turnX = 0.9f; pad.turnY = -0.8f; pad.clickL = pad.clickR = true;
        pad.trigL = 0.2f; pad.trigR = 0.9f; pad.menu = pad.a = true;
        Sample(pad, (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0);
        const bool active = flags == 7;
        Check(pad.thumbrestDpad == active, "option, touch and focused ready input are all required");
        Check(pad.turnX == (active ? 0.0f : 0.9f) && pad.turnY == (active ? 0.0f : -0.8f),
            "only selected physical right axes are consumed");
        Check(pad.dpadX == (active ? 0.9f : 0.0f) && pad.dpadY == (active ? -0.8f : 0.0f),
            "inactive or unavailable touch cannot retain direction");
        Check(pad.moveX == -0.7f && pad.moveY == 0.8f && pad.clickL && pad.clickR &&
            pad.menu && pad.a && pad.trigL == 0.2f && pad.trigR == 0.9f,
            "left movement, head-gesture clicks and other actions are preserved");
    }

    for (float x : {-0.6f, -0.5f, 0.0f, 0.5f, 0.6f})
        for (float y : {-0.6f, -0.5f, 0.0f, 0.5f, 0.6f})
        {
            const uint16_t expectedX = x == -0.6f ? 0x4 : x == 0.6f ? 0x8 : 0;
            const uint16_t expectedY = y == -0.6f ? 0x2 : y == 0.6f ? 0x1 : 0;
            Check(DpadDirectionButtons(x, y) == (expectedX | expectedY),
                "cardinals, diagonals and strict half-stick deadzone");
        }
    for (float invalid : {nan, infinity, -infinity})
    {
        Check(DpadDirectionButtons(invalid, invalid) == 0 &&
            DpadDirectionButtons(invalid, -0.8f) == 0x2,
            "nonfinite direction axes cannot fabricate buttons or suppress a valid axis");
        VrPadState pad{}; pad.turnX = invalid; pad.turnY = 2.0f;
        Sample(pad, true, true, true);
        Check(pad.thumbrestDpad && pad.dpadX == 0 && pad.dpadY == 1 &&
            pad.turnX == 0 && pad.turnY == 0 && DpadDirectionButtons(pad.dpadX, pad.dpadY) == 0x1,
            "invalid axis is neutral and finite out-of-range axis clamps before publication");
    }
    for (bool leftHanded : {false, true})
    {
        Check(PhysicalHandForWeaponSlot(0, leftHanded) == (leftHanded ? 0 : 1),
            "representative weapon roles differ with handedness");
        VrPadState pad{}; pad.moveX = -0.75f; pad.turnX = 0.8f;
        Sample(pad, true, true, true);
        Check(pad.moveX == -0.75f && pad.turnX == 0 && pad.dpadX == 0.8f,
            "D-pad selects physical right stick independently of weapon role");
        pad.turnX = -0.9f; pad.turnY = 0.7f; // next actual action sample
        Sample(pad, true, false, true);
        Check(!pad.thumbrestDpad && pad.dpadX == 0 && pad.dpadY == 0 &&
            pad.turnX == -0.9f && pad.turnY == 0.7f,
            "touch release clears D-pad and restores next right-stick turn/zoom sample");
    }
    for(bool pointer:{false,true}) {
        ExclusiveInputHolds holds,physical;
        VrPadState p{};p.valid=true;p.moveY=1;p.turnX=.8f;p.trigR=1;p.gripL=1;
        p.a=p.menu=true;p.weaponButtons=0xffff;
        p.b=p.x=p.y=p.clickL=p.clickR=true;
        p.thumbrestDpad=!pointer;p.dpadY=.9f;
        holds.ApplyVr(p,true,pointer);
        Check(p.valid&&!p.moveY&&!p.turnX&&!p.trigR&&!p.gripL&&p.a==pointer&&
            !p.b&&!p.x&&!p.y&&!p.clickL&&!p.clickR&&!p.menu&&!p.weaponButtons,
            "pointer preserves A only; D-pad suppresses all other gameplay without disconnecting controllers");
        Check(p.dpadY==.9f,"D-pad direction survives exclusive filtering");
        p.moveY=1;p.turnX=.8f;p.trigR=1;p.gripL=1;p.a=p.menu=true;
        holds.ApplyVr(p,false);
        Check(!p.moveY&&!p.turnX&&!p.trigR&&!p.gripL&&!p.a&&!p.menu,
            "held actions cannot fire or turn when leaving selection mode");
        p={};holds.ApplyVr(p,false);
        p.moveY=1;p.trigR=1;p.a=true;holds.ApplyVr(p,false);
        Check(p.moveY==1&&p.trigR==1&&p.a,"release then fresh input resumes normally");
        XINPUT_GAMEPAD raw{};raw.wButtons=0xffff;raw.sThumbLX=31000;raw.sThumbRY=-29000;
        raw.bLeftTrigger=raw.bRightTrigger=255;physical.ApplyPhysical(raw,true,pointer);
        Check(raw.wButtons==(pointer?XINPUT_GAMEPAD_A:0)&&!raw.sThumbLX&&!raw.sThumbRY&&!raw.bLeftTrigger&&!raw.bRightTrigger,
            "physical pointer confirmation passes A only; D-pad still suppresses it");
        raw.wButtons=0xffff;raw.sThumbLX=31000;raw.bRightTrigger=255;physical.ApplyPhysical(raw,false);
        Check(!raw.wButtons&&!raw.sThumbLX&&!raw.bRightTrigger,"physical holds drain independently");
    }
    for(float angle:{-.8f,-.02f,0.f,.02f,.8f,1.6f}) {
        const float x=std::sin(angle)*.7f,y=std::cos(angle)*.7f;
        int16_t rx{},ry{};RadialMoveStick(x,y,rx,ry);
        Check(std::fabs(float(rx)*y-float(ry)*x)<1.1f,
            "radial deadzone preserves head-relative movement direction");
    }
    int16_t rx=1,ry=1;RadialMoveStick(nan,1,rx,ry);
    Check(rx==0&&ry==0,"invalid movement is neutral");
    std::printf("D-pad input: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
