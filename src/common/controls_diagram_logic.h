#pragma once
#include "vr_action_passthrough.h"
#include <array>
#include <string>
#include <vector>

// THE CONTROLS DIAGRAM (the VR menu's side panel on the Controls page): what each physical button of the two
// controllers does in one game and one context, read from the same rows VR Mappings edits. The menu draws the
// controllers and puts these lines beside each control; nothing here touches input, so the diagram can never change
// what a button does.
//
// It follows the input path's own rules: the triggers and grips swap with left-handed play (the face buttons and sticks
// do not); Automatic is where the game's layout has the action, or the usual layout until the game is running; with
// remapping off every button is the game's own, and only inputs that use the mod's own controls (the right stick's up
// and down, the support hand on the gun) still act; the extra rows need remapping on; the right stick's up and down
// count on foot only; and in a seat left X, left Y and the left stick click are the vehicle cameras'.

namespace controls_diagram
{
enum class Context : unsigned { OnFoot, Vehicle, Count };
inline constexpr const char* kContextNames[static_cast<unsigned>(Context::Count)]{"On foot","In a vehicle"};

// The physical controls, left controller then right. Each is one place on the drawing and one callout beside it.
enum Control : unsigned { LeftTrigger, LeftGrip, X, Y, LeftStick, LeftStickClick, LeftMenu,
    RightTrigger, RightGrip, A, B, RightStick, RightStickClick, ControlCount };
inline constexpr const char* kControlNames[ControlCount]{"Left trigger","Left grip","X","Y","Left stick",
    "Left stick click","Menu button","Right trigger","Right grip","A","B","Right stick","Right stick click"};
inline constexpr bool OnLeftController(Control control) noexcept { return control<RightTrigger; }

// Where a mapping control sits on the controllers, and what part of it (the right stick's directions). False for a
// control that is not a button (the support hand on the gun) or none at all.
struct Place { Control control=ControlCount; const char* part=nullptr; };
inline bool PlaceOf(int source,bool leftHanded,bool thumbrestDpad,Place& out) noexcept
{
    using namespace vr_mapping;
    const Control primaryTrigger=leftHanded?LeftTrigger:RightTrigger, supportTrigger=leftHanded?RightTrigger:LeftTrigger;
    const Control primaryGrip=leftHanded?LeftGrip:RightGrip, supportGrip=leftHanded?RightGrip:LeftGrip;
    switch(source)
    {
    case PrimaryTrigger: out={primaryTrigger,nullptr}; return true;
    case SupportTrigger: out={supportTrigger,nullptr}; return true;
    case PrimaryGrip: out={primaryGrip,nullptr}; return true;
    case SupportGrip: out={supportGrip,nullptr}; return true;
    case vr_mapping::A: out={Control::A,nullptr}; return true;
    case vr_mapping::B: out={Control::B,nullptr}; return true;
    case vr_mapping::X: out={Control::X,nullptr}; return true;
    case vr_mapping::Y: out={Control::Y,nullptr}; return true;
    case LeftClick: out={LeftStickClick,nullptr}; return true;
    case RightClick: out={RightStickClick,nullptr}; return true;
    case RightStickUp: out={RightStick,"Up"}; return true;
    case RightStickDown: out={RightStick,"Down"}; return true;
    // The D-pad: the right stick with the left thumb on its rest, or else the left stick with a controller held at the
    // head (the D-pad gesture).
    case DpadUp: out={thumbrestDpad?RightStick:LeftStick,thumbrestDpad?"Thumbrest + up":"D-pad up"}; return true;
    case DpadDown: out={thumbrestDpad?RightStick:LeftStick,thumbrestDpad?"Thumbrest + down":"D-pad down"}; return true;
    case DpadLeft: out={thumbrestDpad?RightStick:LeftStick,thumbrestDpad?"Thumbrest + left":"D-pad left"}; return true;
    case DpadRight: out={thumbrestDpad?RightStick:LeftStick,thumbrestDpad?"Thumbrest + right":"D-pad right"}; return true;
    default: return false;
    }
}

// A control's short name inside a line ("Zoom (support hand on the gun)", "Pause (B held)").
inline const char* HeldName(int source,bool leftHanded) noexcept
{
    using namespace vr_mapping;
    switch(source)
    {
    case PrimaryTrigger: return leftHanded?"left trigger":"right trigger";
    case SupportTrigger: return leftHanded?"right trigger":"left trigger";
    case PrimaryGrip: return leftHanded?"left grip":"right grip";
    case SupportGrip: return leftHanded?"right grip":"left grip";
    case vr_mapping::A: return "A";
    case vr_mapping::B: return "B";
    case vr_mapping::X: return "X";
    case vr_mapping::Y: return "Y";
    case LeftClick: return "left stick click";
    case RightClick: return "right stick click";
    case DpadUp: return "D-pad up";
    case DpadDown: return "D-pad down";
    case DpadLeft: return "D-pad left";
    case DpadRight: return "D-pad right";
    case HandOnGun: return "support hand on the gun";
    case RightStickUp: return "right stick up";
    case RightStickDown: return "right stick down";
    default: return "";
    }
}

// What the diagram is drawn from: one game's rows as the config holds them, and the game's own layout when it is
// running (null otherwise).
struct Setup
{
    GameTitle game=GameTitle::Halo3;
    Context context=Context::OnFoot;
    bool leftHanded=false;
    bool remap=false;            // VR Mappings' Remap buttons
    bool thumbrestDpad=false;    // the D-pad is the left thumb on its rest and the right stick
    const vr_mapping::Overrides* bindings=nullptr;
    const vr_mapping::Inputs* inputs=nullptr;
    const vr_mapping::ExtraBindings* extra=nullptr;
    const vr_mapping::ModBindings* mod=nullptr;
    const vr_mapping::Transports* layout=nullptr;
    const vr_mapping::ExtraTransports* extraLayout=nullptr;
};

// Per control, the lines its callout shows, in order and without repeats.
using Labels=std::array<std::vector<std::string>,ControlCount>;

namespace detail
{
inline void Add(Labels& labels,Control control,std::string line)
{
    auto& lines=labels[control];
    for(const auto& had:lines) if(had==line) return;
    lines.push_back(std::move(line));
}
// One input as a line on its control: the part of the control, the action, what is held first, and a toggle.
inline void AddInput(Labels& labels,const Setup& setup,const char* action,const vr_mapping::HeldControls& held,
    int press,vr_mapping::InputMode mode,bool seatCameras=true)
{
    using namespace vr_mapping;
    Place place{};
    if(!PlaceOf(press,setup.leftHanded,setup.thumbrestDpad,place)) return;
    const uint32_t uses=Bit(press)|HeldBits(held);
    // The right stick's up and down count on foot only.
    if(setup.context!=Context::OnFoot&&(uses&(Bit(RightStickUp)|Bit(RightStickDown)))) return;
    // In a seat left X, left Y and the left stick click are the vehicle cameras' (the pause still reads them).
    if(seatCameras&&setup.context==Context::Vehicle&&(uses&(Bit(vr_mapping::X)|Bit(vr_mapping::Y)|Bit(LeftClick)))) return;
    std::string line;
    if(place.part) {line+=place.part;line+=": ";}
    line+=action;
    std::string with;
    for(const int source:held)
    {
        if(source==Unbound||!ListedSource(source)) continue;
        if(!with.empty()) with+=" and ";
        with+=HeldName(source,setup.leftHanded);
        if(source!=HandOnGun) with+=" held";
    }
    if(!with.empty()||mode==InputMode::Toggle)
    {
        line+=" (";
        line+=with;
        if(mode==InputMode::Toggle) line+=with.empty()?"toggle":", toggle";
        line+=")";
    }
    Add(labels,place.control,std::move(line));
}
// An input that still acts with remapping off: one that uses only the mod's own controls.
inline bool ActsUnmapped(const vr_mapping::Input& input) noexcept
{
    using namespace vr_mapping;
    return !((Bit(input.press)|HeldBits(input.held))&~kOwnControls);
}
}

// The mapped actions on each control: what VR Mappings' rows put there.
inline Labels Build(const Setup& setup)
{
    using namespace vr_mapping;
    Labels labels{};
    const bool vehicle=setup.context==Context::Vehicle;
    const int game=static_cast<int>(setup.game)-1;
    if(game<0||game>=6||!setup.bindings||!setup.inputs) return labels;

    // The action rows: everywhere, as their inputs allow.
    for(unsigned a=0;a<Count;++a)
    {
        const auto action=static_cast<Action>(a);
        if(NativeAction(setup.game,action)==kNoAction) continue;
        if(setup.layout&&!(*setup.layout)[a]) continue;   // the game has no button for it
        const char* name=ActionName(setup.game,action);
        const auto& row=(*setup.inputs)[a];
        int own=(*setup.bindings)[a];
        if(!setup.remap||own==Automatic) own=setup.layout?LayoutSource((*setup.layout)[a]):kDefaults[a];
        detail::AddInput(labels,setup,name,setup.remap?row.held:kNoneHeld,own,setup.remap?row.mode:InputMode::Hold);
        for(const auto& more:row.more)
            if(more.press!=Unbound&&(setup.remap||detail::ActsUnmapped(more)))
                detail::AddInput(labels,setup,name,more.held,more.press,more.mode);
    }

    // The extra rows, in their context, with remapping on. Automatic is the game's own button when it is known.
    if(setup.remap&&setup.extra)
        for(unsigned x=0;x<ExtraCount;++x)
        {
            const auto extra=static_cast<ExtraAction>(x);
            const ExtraContext context=kExtraContexts[x];
            if(context!=ExtraContext::Anywhere&&(context==ExtraContext::Seat)!=vehicle) continue;
            if(NativeExtraAction(setup.game,extra)==kNoAction) continue;
            const char* name=ExtraName(setup.game,extra);
            const auto& row=(*setup.extra)[x];
            for(unsigned i=0;i<kRowInputs;++i)
            {
                const Input& input=row[i];
                if(input.press==Automatic)
                {
                    if(i==0&&setup.extraLayout&&(*setup.extraLayout)[x])
                        detail::AddInput(labels,setup,name,kNoneHeld,LayoutSource((*setup.extraLayout)[x]),
                            InputMode::Hold);
                }
                else if(input.press!=Unbound)
                    detail::AddInput(labels,setup,name,input.held,input.press,input.mode);
            }
        }

    // The mod's own rows: every game, remapping or not, in a seat too.
    if(setup.mod)
        for(unsigned m=0;m<ModCount;++m)
            for(const auto& input:(*setup.mod)[m])
                if(input.press!=Unbound&&input.press!=Automatic)
                    detail::AddInput(labels,setup,m==Pause?"Pause":kModNames[m],input.held,input.press,input.mode,false);
    return labels;
}

// WHAT NO ROW MAPS: the jobs a control has whatever the rows say (built in), and the gestures, each shown only while
// the setting that turns it on is on.
struct Gestures
{
    bool twoHanded=false;          // weapon gripping
    bool holsters=false;
    int holsterLocation=0;         // weapon_holster_location: shoulder, hip, over the shoulder, the player's own place
    bool manualReload=false;
    bool gestureMelee=false;
    bool meleeNeedsGrip=false;     // gesture_melee_require_grip
    bool meleeOffHand=false;       // gesture_melee_off_hand
    bool physicalMelee=false;
    bool shakeReload=false;
    bool physicalCrouch=false;
    bool physicalRunning=false;
    int dpadHand=0;                // dpad_hand: the controller held at the head for the D-pad, left or right
};

// Each control's built-in jobs. The grips' are on both grips: either hand may come to hold the gun.
inline Labels BuiltIns(const Setup& setup,const Gestures& gestures)
{
    Labels labels{};
    const bool vehicle=setup.context==Context::Vehicle;
    detail::Add(labels,LeftStick,vehicle?"Drive":"Move");
    detail::Add(labels,RightStick,"Turn");
    detail::Add(labels,LeftMenu,"Pause (Start)");
    detail::Add(labels,LeftStickClick,"Both stick clicks: recentre, open menu");
    detail::Add(labels,RightStickClick,"Both stick clicks: recentre, open menu");
    if(vehicle)
    {
        detail::Add(labels,Y,"Next camera (hold: recentre view)");
        detail::Add(labels,X,"Camera tether (hold: recentre view)");
        detail::Add(labels,LeftStickClick,"Next way of controlling the seat");
        return labels;
    }
    for(const Control grip:{LeftGrip,RightGrip})
    {
        if(gestures.twoHanded) detail::Add(labels,grip,"Weapon gripping");
        if(gestures.holsters)
        {
            constexpr const char* kSwitching[]{"Grip-at-shoulder weapon switching","Grip-at-hip weapon switching",
                "Grip-over-shoulder weapon switching","Grip-at-holster weapon switching"};
            detail::Add(labels,grip,kSwitching[gestures.holsterLocation>=0&&gestures.holsterLocation<4?
                gestures.holsterLocation:0]);
        }
        if(gestures.manualReload) detail::Add(labels,grip,"Grip-at-hip magazine reload");
    }
    return labels;
}

// The gestures, under the diagram: a name and what to do.
struct GestureLine { std::string name, how; };
inline std::vector<GestureLine> GestureLines(const Setup& setup,const Gestures& gestures)
{
    std::vector<GestureLine> lines;
    if(setup.context==Context::OnFoot)
    {
        if(gestures.physicalMelee) lines.push_back({"Physical melee","Strike with a hand or the gun."});
        else if(gestures.gestureMelee)
        {
            const std::string hand=gestures.meleeOffHand?"either controller":"your primary controller";
            lines.push_back({"Gesture melee",gestures.meleeNeedsGrip?"Grip "+hand+" and punch away from your body.":
                "Punch away from your body with "+hand+"."});
        }
        // Only Halo 3 and Halo 2 dual wield; Halo 2's shake reloads its right-hand gun for now.
        if(gestures.shakeReload&&setup.game==GameTitle::Halo3)
            lines.push_back({"Dual-wield reload","Shake a hand: reload that hand's gun."});
        else if(gestures.shakeReload&&setup.game==GameTitle::Halo2)
            lines.push_back({"Dual-wield reload","Shake a hand: reload the right-hand gun."});
        if(gestures.physicalCrouch) lines.push_back({"Physical crouch","Physically duck your head in real space to crouch."});
        if(gestures.physicalRunning) lines.push_back({"Physical running","Swing both arms to run."});
    }
    if(setup.thumbrestDpad)
        lines.push_back({"D-pad","Rest your left thumb on its thumb rest and push the right thumbstick the way you want the "
            "D-pad pressed."});
    else
    {
        // Halo CE always takes the left hand.
        const bool right=gestures.dpadHand==1&&setup.game!=GameTitle::HaloCE;
        lines.push_back({"D-pad",std::string("Hold the ")+(right?"right controller to your head and push the left":
            "left controller to your head and push its")+" thumbstick the way you want the D-pad pressed."});
    }
    return lines;
}
}
