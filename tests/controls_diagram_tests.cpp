#include "../src/common/controls_diagram_logic.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace controls_diagram;
namespace
{
unsigned checks{};
void Check(bool ok,const char* why)
{++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
bool Has(const Labels& labels,Control control,const char* line)
{ return std::find(labels[control].begin(),labels[control].end(),std::string(line))!=labels[control].end(); }
bool Mentions(const Labels& labels,Control control,const char* text)
{
    for(const auto& line:labels[control]) if(line.find(text)!=std::string::npos) return true;
    return false;
}

struct Rows
{
    vr_mapping::Overrides bindings{};
    vr_mapping::Inputs inputs=vr_mapping::DefaultInputs(GameTitle::Halo3);
    vr_mapping::ExtraBindings extra=vr_mapping::kExtraDefaults;
    vr_mapping::ModBindings mod=vr_mapping::kModDefaults;
    Setup Make(Context context,bool remap=true) const
    {
        Setup setup{};
        setup.game=GameTitle::Halo3;
        setup.context=context;
        setup.remap=remap;
        setup.bindings=&bindings;
        setup.inputs=&inputs;
        setup.extra=&extra;
        setup.mod=&mod;
        return setup;
    }
};

void CheckDefaults()
{
    Rows rows;
    const Labels foot=Build(rows.Make(Context::OnFoot));
    Check(Has(foot,RightTrigger,"Fire")&&Has(foot,LeftTrigger,"Grenade / secondary fire")&&Has(foot,A,"Jump")&&
        Has(foot,RightGrip,"Melee"),"Automatic rows take the usual layout until the game is running");
    Check(Has(foot,X,"Reload")&&Has(foot,X,"Use / pick up"),"two actions on one control are both listed");
    Check(Has(foot,RightStick,"Down: Crouch (toggle)")&&Has(foot,RightStick,"Up: Equipment / ability"),
        "the right stick's crouch and Halo 3's stick-up equipment, on foot");
    const Labels fixed=BuiltIns(rows.Make(Context::OnFoot),Gestures{});
    Check(Has(fixed,LeftStick,"Move")&&Has(fixed,RightStick,"Turn")&&Has(fixed,LeftMenu,"Pause (Start)")&&
        !Mentions(foot,LeftStick,"Move"),"the fixed controls are built in, not mapped");
    Check(Has(foot,B,"Pause (Y held)")&&Has(foot,Y,"Pause (B held)"),"the mod's Y + B pause on both buttons");
    Check(!Mentions(foot,RightGrip,"Banshee"),"seat rows are not on foot");

    const Labels seat=Build(rows.Make(Context::Vehicle));
    const Labels seatFixed=BuiltIns(rows.Make(Context::Vehicle),Gestures{});
    Check(Has(seatFixed,LeftStick,"Drive"),"the left stick drives in a vehicle");
    Check(!Mentions(seat,RightStick,"Crouch")&&!Mentions(seat,RightStick,"Equipment"),
        "the right stick's up and down count on foot only");
    Check(Has(seat,RightGrip,"Banshee bomb")&&Has(seat,RightStickClick,"Banshee bomb")&&Has(seat,A,"Banshee trick"),
        "the Banshee's seat rows");
    Check(!Mentions(seat,X,"Reload")&&Has(seatFixed,X,"Camera tether (hold: recentre view)")&&
        Has(seatFixed,Y,"Next camera (hold: recentre view)"),"left X and Y are the vehicle cameras' in a seat");
    Check(Has(seat,Y,"Pause (B held)"),"the pause still reads left Y in a seat");
}

void CheckHandsAndChords()
{
    Rows rows;
    rows.inputs[vr_mapping::Zoom].more[0]=vr_mapping::kGunZoom;
    rows.inputs[vr_mapping::Equipment].more[1]=vr_mapping::kAirEquipment;
    Setup setup=rows.Make(Context::OnFoot);
    Labels labels=Build(setup);
    Check(Has(labels,LeftTrigger,"Zoom (support hand on the gun)")&&
        Has(labels,LeftTrigger,"Equipment / ability (left grip held)"),"chords name what is held first");
    setup.leftHanded=true;
    labels=Build(setup);
    Check(Has(labels,LeftTrigger,"Fire")&&Has(labels,RightTrigger,"Zoom (support hand on the gun)")&&
        Has(labels,RightTrigger,"Equipment / ability (right grip held)")&&Has(labels,LeftGrip,"Melee"),
        "left-handed play swaps the triggers and grips");
    Check(Has(labels,A,"Jump")&&Has(labels,X,"Reload"),"left-handed play leaves the face buttons where they are");

    rows.bindings[vr_mapping::Jump]=vr_mapping::B;
    rows.inputs[vr_mapping::Jump].mode=vr_mapping::InputMode::Toggle;
    rows.inputs[vr_mapping::Jump].held={vr_mapping::PrimaryGrip,vr_mapping::Unbound};
    labels=Build(rows.Make(Context::OnFoot));
    Check(Has(labels,B,"Jump (right grip held, toggle)")&&!Has(labels,A,"Jump"),"a remapped row with options");
    labels=Build(rows.Make(Context::OnFoot,false));
    Check(Has(labels,A,"Jump")&&!Mentions(labels,B,"Jump"),"with remapping off every button is the game's own");
    Check(Has(labels,RightStick,"Down: Crouch (toggle)"),"the mod's own controls act with remapping off");
    Check(!Mentions(labels,LeftTrigger,"Equipment / ability (left grip held)")&&
        !Mentions(labels,LeftTrigger,"Zoom (support hand on the gun)"),"an input on a game button needs remapping on");
    Check(!Mentions(Build(rows.Make(Context::Vehicle,false)),RightGrip,"Banshee"),"the extra rows need remapping on");
}

void CheckLayout()
{
    Rows rows;
    vr_mapping::Transports layout{};
    layout[vr_mapping::Fire]=0x1000;      // the game has Fire on A
    layout[vr_mapping::Grenade]=1u<<16;
    vr_mapping::ExtraTransports extraLayout{};
    extraLayout[vr_mapping::VehicleSecondary]=1u<<16;
    Setup setup=rows.Make(Context::Vehicle);
    setup.layout=&layout;
    setup.extraLayout=&extraLayout;
    const Labels labels=Build(setup);
    Check(Has(labels,A,"Fire")&&!Mentions(labels,RightTrigger,"Fire"),"Automatic follows the running game's layout");
    Check(!Mentions(labels,A,"Jump"),"an action the game has no button for is left out");
    Check(Has(labels,LeftTrigger,"Vehicle secondary fire / boost"),"an Automatic extra row on the game's own button");
    setup.game=GameTitle::HaloCE;
    Check(!Mentions(Build(setup),RightGrip,"Banshee bomb"),"an extra row the game does not have is left out");
    setup.game=GameTitle::Halo3;
    setup.thumbrestDpad=true;
    setup.layout=nullptr;
    setup.context=Context::OnFoot;
    Check(Has(Build(setup),RightStick,"Thumbrest + up: Flashlight / night vision"),"the D-pad as the thumbrest gives it");
    setup.thumbrestDpad=false;
    Check(Has(Build(setup),LeftStick,"D-pad up: Flashlight / night vision")&&
        !Mentions(Build(setup),RightStick,"Flashlight"),"the D-pad gesture's D-pad is the left stick");
}

void CheckBuiltInsAndGestures()
{
    Rows rows;
    Setup setup=rows.Make(Context::OnFoot);
    Gestures gestures{};
    gestures.twoHanded=gestures.holsters=gestures.gestureMelee=gestures.physicalCrouch=gestures.shakeReload=true;
    gestures.holsterLocation=2;
    const Labels fixed=BuiltIns(setup,gestures);
    Check(Has(fixed,LeftGrip,"Weapon gripping")&&Has(fixed,RightGrip,"Weapon gripping")&&
        Has(fixed,LeftGrip,"Grip-over-shoulder weapon switching")&&Has(fixed,RightGrip,"Grip-over-shoulder weapon switching"),
        "both grips grip the gun and switch weapons at the holster");
    gestures.holsters=false;
    Check(!Mentions(BuiltIns(setup,gestures),LeftGrip,"switching"),"no holster switching while holsters are off");
    Check(!Mentions(BuiltIns(rows.Make(Context::Vehicle),gestures),LeftGrip,"Weapon gripping"),"no weapon grip in a seat");

    auto lines=GestureLines(setup,gestures);
    const auto how=[&](const char* name) {
        for(const auto& line:lines) if(line.name==name) return line.how;
        return std::string("(none)");
    };
    Check(how("Gesture melee")=="Punch away from your body with your primary controller."&&
        how("Dual-wield reload")=="Shake a hand: reload that hand's gun."&&
        how("Physical crouch")=="Physically duck your head in real space to crouch."&&
        how("D-pad")=="Hold the left controller to your head and push its thumbstick the way you want the D-pad pressed.",
        "the gestures, as set");
    gestures.meleeNeedsGrip=gestures.meleeOffHand=true;
    gestures.dpadHand=1;
    lines=GestureLines(setup,gestures);
    Check(how("Gesture melee")=="Grip either controller and punch away from your body."&&
        how("D-pad")=="Hold the right controller to your head and push the left thumbstick the way you want the D-pad pressed.",
        "melee by either hand with a grip, and the D-pad at the right hand");
    setup.game=GameTitle::HaloReach;
    lines=GestureLines(setup,gestures);
    Check(how("Dual-wield reload")=="(none)","no dual-wield reload where the game has no dual wielding");
    setup.game=GameTitle::HaloCE;
    lines=GestureLines(setup,gestures);
    Check(how("D-pad").find("left controller")!=std::string::npos,"Halo CE takes the left hand for the D-pad");
    setup.game=GameTitle::Halo3;
    gestures.physicalCrouch=false;
    lines=GestureLines(setup,gestures);
    Check(how("Physical crouch")=="(none)","a gesture that is off is not listed");
}
}

int main()
{
    CheckDefaults();
    CheckHandsAndChords();
    CheckLayout();
    CheckBuiltInsAndGestures();
    std::printf("Controls diagram: %u checks passed\n",checks);
    return 0;
}
