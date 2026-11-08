#pragma once
#include "runtime_types.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vr_mapping
{
enum Action : unsigned { Fire, Grenade, Jump, Melee, Reload, Interact, SwitchWeapon,
    SwitchGrenade, Equipment, Crouch, Zoom, Flashlight, Sprint, Count };
// Files keep these numbers: a new control goes at the end. The last three are the mod's own: the support hand holding the
// gun (a state, held while it lasts), and the right stick pushed up or down on foot.
enum Source : int { Automatic, Unbound, PrimaryTrigger, SupportTrigger, PrimaryGrip,
    SupportGrip, A, B, X, Y, LeftClick, RightClick, DpadUp, DpadDown, DpadLeft,
    DpadRight, SupportTriggerFree, SupportTriggerOnGun, SupportTriggerAir, HandOnGun, RightStickUp, RightStickDown,
    SourceCount };
enum class Profile : unsigned { Unknown, Touch, Index, Vive, Wmr, Simple };
inline constexpr const char* kKeys[Count]{"fire","grenade","jump","melee","reload",
    "interact","switch_weapon","switch_grenade","equipment","crouch","zoom","flashlight","sprint"};
inline constexpr const char* kNames[Count]{"Fire","Grenade / secondary fire","Jump","Melee",
    "Reload","Use / pick up","Switch weapon","Switch grenade","Equipment / ability",
    "Crouch","Zoom","Flashlight / night vision","Sprint"};
inline constexpr const char* kSourceNames[SourceCount]{"Automatic","Unbound","Primary trigger",
    "Support trigger","Primary grip","Support grip","Right A / lower face","Right B / upper face",
    "Left X / lower face","Left Y / upper face","Left stick / pad click","Right stick / pad click",
    "D-pad gesture up","D-pad gesture down","D-pad gesture left","D-pad gesture right",
    "Support trigger, hand free","Support trigger, gripping the gun","Support trigger, gripping air",
    "Support hand on the gun","Right stick up","Right stick down"};
using Overrides = std::array<int, Count>;
using Transports = std::array<uint32_t, Count>;
inline constexpr int kDefaults[Count]{PrimaryTrigger,SupportTrigger,A,PrimaryGrip,X,X,
    Y,DpadLeft,SupportGrip,LeftClick,RightClick,DpadUp,Unbound};
inline constexpr unsigned kNoAction=0xFF;
// The support trigger by what the support hand is doing was a row's choice once, then two settings (Grip-contextual
// Trigger, Empty Grip + Trigger), and is now a chord on a row: Support hand on the gun + Support trigger, Support grip +
// Support trigger (kGunZoom, kAirEquipment). Files from before keep the three context numbers, read only to convert.
inline constexpr int kRowSourceCount=SupportTriggerFree;
inline constexpr bool ContextSource(int source) noexcept
{ return source==SupportTriggerFree||source==SupportTriggerOnGun||source==SupportTriggerAir; }
// A control a row may list: every one but Automatic and the old contexts. Unbound is no control.
inline constexpr bool ListedSource(int source) noexcept
{ return (source>Automatic&&source<kRowSourceCount)||source==HandOnGun||source==RightStickUp||source==RightStickDown; }
// A row saved while the context sources were rows' choices: Zoom on the gun turns the grip-contextual trigger on, and
// Equipment on air the empty-grip equipment, each row going back to Automatic; a free hand is the plain support trigger;
// any other row on one goes back to Automatic. True if a row changed.
inline bool ConvertContextRows(std::array<int,Count>& row,bool& contextualZoom,bool& airEquipment) noexcept
{
    bool changed=false;
    for(unsigned a=0;a<Count;++a)
    {
        const int source=row[a];
        if(!ContextSource(source)) continue;
        if(source==SupportTriggerOnGun&&a==Zoom) contextualZoom=true;
        if(source==SupportTriggerAir&&a==Equipment) airEquipment=true;
        row[a]=source==SupportTriggerFree?SupportTrigger:Automatic;
        changed=true;
    }
    return changed;
}
// The control that presses a gamepad button when nothing is remapped: what the unmapped route in input.cpp copies.
// Start and Back have no row. Zero is a button the game gives this action none of.
inline constexpr int LayoutSource(uint32_t transport) noexcept
{
    constexpr struct { uint32_t bit; int source; } route[]{{1u<<17,PrimaryTrigger},{1u<<16,SupportTrigger},
        {0x200,PrimaryGrip},{0x100,SupportGrip},{0x1000,A},{0x2000,B},{0x4000,X},{0x8000,Y},{0x40,LeftClick},
        {0x80,RightClick},{0x1,DpadUp},{0x2,DpadDown},{0x4,DpadLeft},{0x8,DpadRight}};
    for(const auto& r:route) if(transport&r.bit) return r.source;
    return Unbound;
}
// Named UI glyph -> action switch in each official editing kit. These are
// action identities, never raw buttons. See VR-ACTION-BINDING-EVIDENCE-2026-09-23.md.
inline constexpr unsigned NativeAction(GameTitle title, Action action) noexcept
{
    constexpr unsigned h3[Count]{8,7,0,5,3,3,2,1,6,13,14,17,kNoAction};
    constexpr unsigned odst[Count]{8,7,0,5,3,3,2,1,kNoAction,13,14,17,kNoAction};
    constexpr unsigned reach[Count]{7,6,0,4,0x3E,3,2,1,5,8,9,0x16,kNoAction};
    constexpr unsigned h4[Count]{6,5,0,3,2,2,1,10,4,7,8,0x18,0x28};
    constexpr unsigned h2[Count]{10,7,0,5,0x2E,0x23,4,3,kNoAction,13,14,6,kNoAction};
    constexpr unsigned ce[Count]{7,6,0,4,13,2,3,1,kNoAction,10,11,5,kNoAction};
    if(action>=Count) return kNoAction;
    switch(title) {
    case GameTitle::Halo3:return h3[action];
    case GameTitle::Halo3ODST:return odst[action];
    case GameTitle::HaloReach:return reach[action];
    case GameTitle::Halo4:return h4[action];
    case GameTitle::Halo2:return h2[action];
    case GameTitle::HaloCE:return ce[action];
    default:return kNoAction;
    }
}
inline Profile DetectProfile(const char* path) noexcept
{
    if(!path) return Profile::Unknown;
    if(std::strstr(path,"oculus/touch_controller")||std::strstr(path,"meta/touch_controller")||
       std::strstr(path,"meta/touch_pro_controller")||std::strstr(path,"meta/touch_plus_controller")||
       std::strstr(path,"facebook/touch_controller_pro")) return Profile::Touch;
    if(std::strstr(path,"valve/index_controller")) return Profile::Index;
    if(std::strstr(path,"htc/vive_controller")) return Profile::Vive;
    if(std::strstr(path,"microsoft/motion_controller")||std::strstr(path,"hp/mixed_reality_controller")) return Profile::Wmr;
    if(std::strstr(path,"khr/simple_controller")) return Profile::Simple;
    return Profile::Unknown;
}
inline const char* ProfileName(Profile profile) noexcept
{
    switch(profile) {
    case Profile::Touch:return "Touch-compatible";
    case Profile::Index:return "Valve Index";
    case Profile::Vive:return "Vive wands";
    case Profile::Wmr:return "Windows Mixed Reality";
    case Profile::Simple:return "OpenXR simple controller (limited buttons)";
    default:return "OpenXR fallback (profile unavailable)";
    }
}
inline constexpr int Resolve(Action action,int source) noexcept
{ return source==Automatic&&action<Count?kDefaults[action]:(source>Automatic&&source<SourceCount?source:Unbound); }
// A row left on Automatic follows the game's own button layout, so a player who remaps one action keeps every other
// where it was. Until that layout is known the row stays Automatic, and Resolve answers with kDefaults.
inline Overrides EffectiveOverrides(const Overrides& bindings,const Transports& layout,bool layoutKnown) noexcept
{
    Overrides effective=bindings;
    if(layoutKnown)
        for(unsigned a=0;a<Count;++a)
            if(effective[a]==Automatic) effective[a]=LayoutSource(layout[a]);
    return effective;
}
inline constexpr uint32_t Bit(int source) noexcept
{return source>Unbound&&source<SourceCount?1u<<source:0;}
inline uint32_t DpadSources(float x,float y) noexcept
{
    if(!std::isfinite(x)||!std::isfinite(y)) return 0;
    return (x>.5f?Bit(DpadRight):0)|(x<-.5f?Bit(DpadLeft):0)|
        (y>.5f?Bit(DpadUp):0)|(y<-.5f?Bit(DpadDown):0);
}
// Wands/WMR have no A/B/X/Y face buttons. Upper/lower pad clicks supply
// those equivalent controls. Latch the chosen zone until release, so sliding
// a pressed thumb cannot change reload into switch weapon halfway through.
struct PadFaces
{
    bool held{}; int zone{};
    int Update(float y,bool pressed) noexcept
    {
        if(!pressed) {held=false;zone=0;return 0;}
        if(!held) {zone=std::isfinite(y)?(y>.35f?1:(y<-.35f?-1:0)):0;held=true;}
        return zone;
    }
};
template<class Pad> uint32_t Sources(const Pad& p) noexcept
{
    uint32_t bits=0;
    if(std::isfinite(p.trigR)&&p.trigR>0.15f) bits|=Bit(PrimaryTrigger);
    if(std::isfinite(p.trigL)&&p.trigL>0.15f) bits|=Bit(SupportTrigger);
    if(std::isfinite(p.gripR)&&p.gripR>0.6f) bits|=Bit(PrimaryGrip);
    if(std::isfinite(p.gripL)&&p.gripL>0.6f) bits|=Bit(SupportGrip);
    if(p.a) bits|=Bit(A); if(p.b) bits|=Bit(B); if(p.x) bits|=Bit(X); if(p.y) bits|=Bit(Y);
    if(p.clickL) bits|=Bit(LeftClick); if(p.clickR) bits|=Bit(RightClick);
    return bits;
}
inline uint64_t Fingerprint(const Overrides& bindings,const Transports& native) noexcept
{
    uint64_t value=14695981039346656037ull;
    for(unsigned a=0;a<Count;++a) {value^=uint64_t(bindings[a])|(uint64_t(native[a])<<8);value*=1099511628211ull;}
    return value;
}
// A one-handed weapon has no front grip to reach for, and its two-handed zone is small: for the support trigger, a grip
// held anywhere near the gun hand is on the gun, so its zoom stays in reach. Beyond this it is an air grip.
inline constexpr float kOneHandedGripRangeMetres=.30f;
inline constexpr bool OneHandedGripOnGun(bool oneHanded,bool gripDown,float distanceSquared) noexcept
{ return oneHanded&&gripDown&&distanceSquared<=kOneHandedGripRangeMetres*kOneHandedGripRangeMetres; }
// Out of the game's zoom when the support hand leaves the gun. Zoom is one button stepping through a weapon's levels (a
// sniper: first level, second, out), so the way out is pressed like any other, and the game's own zoom level says what
// each press did: lower or the same level is judged afresh, deeper presses again, and not zoomed ends it. A press only
// ever begins on a level that reads zoomed, so this cannot zoom in. A level the game has stopped reporting stands it
// down, as do the hand coming back and a fresh pull of the trigger: the player's own input wins.
struct ZoomExit
{
    static constexpr uint64_t kSettleMs=100,kPressMs=60,kGapMs=40,kUnseenMs=300;
    static constexpr int kMostPresses=4;
    enum class Phase{Idle,Settle,Press,Answer};
    Phase phase=Phase::Idle;
    bool wasOnGun=false,triggerRested=false;
    uint64_t at=0;
    int pressLevel=-1,presses=0;
    bool Step(bool enabled,bool onGun,bool triggerDown,bool levelKnown,int level,uint64_t now) noexcept
    {
        const bool letGo=wasOnGun&&!onGun;
        wasOnGun=onGun;
        if(!enabled||onGun) {phase=Phase::Idle;return false;}
        if(phase==Phase::Idle)
        {
            if(!letGo) return false;
            phase=Phase::Settle;at=now;presses=0;triggerRested=!triggerDown;
            return false;
        }
        if(triggerRested&&triggerDown) {phase=Phase::Idle;return false;}
        triggerRested=triggerRested||!triggerDown;
        const auto press=[&]
        {
            if(presses>=kMostPresses) {phase=Phase::Idle;return false;}
            ++presses;phase=Phase::Press;at=now;pressLevel=level;
            return true;
        };
        switch(phase)
        {
        case Phase::Settle:
            // A zoom pulled on the way out of the grip takes a moment to read.
            if(now-at<kSettleMs) return false;
            if(!levelKnown||level<0) {phase=Phase::Idle;return false;}
            return press();
        case Phase::Press:
            // A press once begun is held its full length, as a shorter one may go unseen.
            if(now-at<kPressMs) return true;
            phase=Phase::Answer;
            return false;
        case Phase::Answer:
            if(!levelKnown||level<0) {phase=Phase::Idle;return false;}
            if(level!=pressLevel) {if(now-at>=kPressMs+kGapMs) return press();return false;}
            if(now-at>=kUnseenMs) return press();
            return false;
        default:
            return false;
        }
    }
};
// Preserve semantic identity before transport encoding. Two actions sharing a
// native gamepad bit are still different actions, and a post-mapping native
// consumer must never try to reconstruct that distinction from the bitmask.
struct MappedActions
{
    uint32_t actions = 0;
    uint32_t transports = 0;
    bool Has(Action action) const noexcept
    { return action < Count && (actions & (1u << action)) != 0; }
};

// A ROW'S INPUTS (VR Mappings). A row presses its action from its own input -- its own control (Automatic: where the
// game's layout has the action), pressed while any controls listed to hold first are down -- and from up to three
// further inputs. Every input is a chord of up to three controls: up to two held first, then its press. It presses its
// action while they are all down (Hold), or each press turns the action on or off (Toggle). A chord starts only on its
// press, with the rest already down, and it CLAIMS that press: the control presses nothing else until it is let go, so
// Support grip + Support trigger uses equipment without the trigger's own grenade. Plain controls never wait for a chord.
//
// A file names a row's own input with the row's key and its further ones with _2, _3, _4, each as its held controls and
// its press joined by '+', the press last, then " toggle" for a toggle: "5+3", "21 toggle", "7".
inline constexpr unsigned kHeld=2,kMoreInputs=3,kRowInputs=1+kMoreInputs;
enum class InputMode : int { Hold, Toggle };
inline constexpr const char* kModeNames[2]{"Hold","Toggle"};
inline constexpr const char* kControlSuffix[kRowInputs]{"","_2","_3","_4"};
using HeldControls=std::array<int,kHeld>;
inline constexpr HeldControls kNoneHeld{Unbound,Unbound};
struct Input
{
    HeldControls held=kNoneHeld;
    int press=Unbound;
    InputMode mode=InputMode::Hold;
    constexpr bool operator==(const Input&) const=default;
};
struct RowInputs
{
    HeldControls held=kNoneHeld;       // held before the row's own control
    InputMode mode=InputMode::Hold;    // of the row's own input
    std::array<Input,kMoreInputs> more{};
    constexpr bool operator==(const RowInputs&) const=default;
};
using Inputs=std::array<RowInputs,Count>;
// The mod's own controls: no gamepad button stands for them, so they act with remapping off too.
inline constexpr uint32_t kOwnControls=Bit(HandOnGun)|Bit(RightStickUp)|Bit(RightStickDown);
inline constexpr uint32_t HeldBits(const HeldControls& held) noexcept
{ uint32_t bits=0; for(const int source:held) if(ListedSource(source)) bits|=Bit(source); return bits; }

// The right stick on foot, as every game starts: down toggles a crouch, and held up sprints -- in Reach, where sprint is
// an armor ability, the armor ability; in Halo 3, which has no sprint, the equipment; in ODST, which has neither, VISR.
// Halo CE and Halo 2 have no sprint. Crouch needs MCC's hold-to-crouch setting.
inline constexpr Input kStickCrouch{kNoneHeld,RightStickDown,InputMode::Toggle};
inline constexpr Input kStickUp{kNoneHeld,RightStickUp,InputMode::Hold};
// The support trigger with the support hand on the gun zooms; with the grip closed on nothing, it uses equipment.
inline constexpr Input kGunZoom{{HandOnGun,Unbound},SupportTrigger,InputMode::Hold};
inline constexpr Input kAirEquipment{{SupportGrip,Unbound},SupportTrigger,InputMode::Hold};
inline constexpr Action StickUpAction(GameTitle title) noexcept
{
    return title==GameTitle::HaloReach||title==GameTitle::Halo3?Equipment:title==GameTitle::Halo3ODST?Flashlight:
        title==GameTitle::Halo4?Sprint:Count;
}
inline constexpr Inputs DefaultInputs(GameTitle title) noexcept
{
    Inputs inputs{};
    inputs[Crouch].more[0]=kStickCrouch;
    if(StickUpAction(title)<Count) inputs[StickUpAction(title)].more[0]=kStickUp;
    return inputs;
}
// Adds an input in a row's first free place; false if the row has it already or no place is free.
inline bool AddInput(RowInputs& row,const Input& input) noexcept
{
    for(const auto& more:row.more) if(more==input) return false;
    for(auto& more:row.more) if(more.press==Unbound) {more=input;return true;}
    return false;
}
// Removes an input from a row, closing the gap.
inline bool RemoveInput(RowInputs& row,const Input& input) noexcept
{
    bool removed=false;
    unsigned kept=0;
    for(unsigned i=0;i<kMoreInputs;++i)
    {
        if(row.more[i]==input) {removed=true;continue;}
        row.more[kept++]=row.more[i];
    }
    while(kept<kMoreInputs) row.more[kept++]=Input{};
    return removed;
}
// A file from before rows held inputs: the settings that stood for these become the inputs they meant. Right stick
// crouch off takes the stick's inputs away.
inline void ConvertSettingsToInputs(Inputs& inputs,GameTitle title,bool stick,bool gunZoom,bool airEquipment) noexcept
{
    if(!stick) for(auto& row:inputs) {RemoveInput(row,kStickCrouch);RemoveInput(row,kStickUp);}
    else
    {
        AddInput(inputs[Crouch],kStickCrouch);
        if(StickUpAction(title)<Count) AddInput(inputs[StickUpAction(title)],kStickUp);
    }
    if(gunZoom) AddInput(inputs[Zoom],kGunZoom);
    if(airEquipment&&NativeAction(title,Equipment)!=kNoAction) AddInput(inputs[Equipment],kAirEquipment);
}

// An input's text in a file (see above). `out` takes at most 32 characters.
inline void FormatInput(char (&out)[32],const HeldControls& held,int press,InputMode mode) noexcept
{
    int used=0;
    for(const int source:held)
        if(source!=Unbound) used+=std::snprintf(out+used,sizeof(out)-used,"%d+",source);
    std::snprintf(out+used,sizeof(out)-used,"%d%s",press,mode==InputMode::Toggle?" toggle":"");
}
// Reads FormatInput's text. A row's own press (`own`) may be Automatic or an old context number, which the caller
// converts; every other control must be one a row may list. False leaves the outputs as they were.
inline bool ParseInput(const char* text,HeldControls& held,int& press,InputMode& mode,bool own) noexcept
{
    int values[kHeld+1]{};
    unsigned count=0;
    const char* at=text?text:"";
    for(;;)
    {
        while(*at==' '||*at=='\t') ++at;
        char* end=nullptr;
        const long value=std::strtol(at,&end,10);
        if(end==at||count==kHeld+1||value<0||value>=SourceCount) return false;
        values[count++]=static_cast<int>(value);
        at=end;
        while(*at==' '||*at=='\t') ++at;
        if(*at!='+') break;
        ++at;
    }
    InputMode readMode=InputMode::Hold;
    if(!std::strncmp(at,"toggle",6)) {readMode=InputMode::Toggle;at+=6;}
    else if(!std::strncmp(at,"hold",4)) at+=4;
    while(*at==' '||*at=='\t'||*at=='\r'||*at=='\n') ++at;
    if(*at) return false;
    const int readPress=values[count-1];
    if(own?false:!ListedSource(readPress)) return false;
    HeldControls readHeld=kNoneHeld;
    for(unsigned i=0;i+1<count;++i)
    {
        if(!ListedSource(values[i])||values[i]==Unbound) return false;
        readHeld[i]=values[i];
    }
    held=readHeld;press=readPress;mode=readMode;
    return true;
}

// ONE INPUT AS A POLL READS IT: what is held first and the press, as control bits.
struct ResolvedInput { uint32_t held=0,press=0; InputMode mode=InputMode::Hold; };
// What turns inputs on, shared by every poll of the pad: keep ONE instance and step it under a lock, so each press is
// seen once. `usable` is what is down and may act (not held across a change); `heldOk` the controls that may be held
// first this poll. Returns, per input, whether it presses its action.
template<unsigned N> struct ChordState
{
    uint32_t previous{},claimed{};
    std::array<bool,N> on{},toggled{};
    void Reset(bool toggles) noexcept
    {
        on.fill(false);claimed=0;
        if(toggles) toggled.fill(false);
    }
    std::array<bool,N> Step(const std::array<ResolvedInput,N>& inputs,uint32_t sources,uint32_t usable,
        uint32_t heldOk) noexcept
    {
        const uint32_t pressed=sources&~previous;
        previous=sources;
        claimed&=sources;
        const auto was=on;
        // Chords first: one starts on its press with the rest down, claims the press, and lasts while all stay down.
        for(unsigned i=0;i<N;++i)
        {
            const auto& input=inputs[i];
            if(!input.press||!input.held) continue;
            const bool down=(usable&input.press)&&(sources&input.held)==input.held&&(heldOk&input.held)==input.held;
            const bool starts=down&&!on[i]&&(pressed&input.press);
            on[i]=on[i]?down:starts;
            if(starts) claimed|=input.press;
        }
        // Plain controls: down, and their press not claimed by a chord. An input with no press (gone, or out of its
        // context) is off.
        for(unsigned i=0;i<N;++i)
            if(!inputs[i].press) on[i]=false;
            else if(!inputs[i].held) on[i]=(usable&inputs[i].press)&&!(claimed&inputs[i].press);
        std::array<bool,N> presses{};
        for(unsigned i=0;i<N;++i)
        {
            if(inputs[i].mode==InputMode::Toggle&&on[i]&&!was[i]) toggled[i]=!toggled[i];
            if(!inputs[i].press) toggled[i]=false;
            presses[i]=inputs[i].mode==InputMode::Toggle?toggled[i]:on[i];
        }
        return presses;
    }
};

// Every control an action's inputs use, held first or pressed. An input with no press uses none, whatever it holds.
inline uint32_t ControlBits(Action action,const Overrides& bindings,const Inputs* inputs) noexcept
{
    if(action>=Count) return 0;
    const uint32_t own=Bit(Resolve(action,bindings[action]));
    uint32_t bits=own;
    if(inputs)
    {
        const auto& row=(*inputs)[action];
        if(own) bits|=HeldBits(row.held);
        for(const auto& more:row.more)
            if(ListedSource(more.press)&&Bit(more.press)) bits|=Bit(more.press)|HeldBits(more.held);
    }
    return bits;
}
// True if an input of a row is a chord pressed by `source` (something held first): the claimed-trigger rule needs one.
inline bool ChordPresses(const Overrides& bindings,const Inputs& inputs,int source) noexcept
{
    for(unsigned a=0;a<Count;++a)
    {
        const auto& row=inputs[a];
        if(Resolve(static_cast<Action>(a),bindings[a])==source&&HeldBits(row.held)) return true;
        for(const auto& more:row.more) if(more.press==source&&HeldBits(more.held)) return true;
    }
    return false;
}
// What presses an action, as one number: its inputs in any order. Two actions with the same number are pressed by the
// same inputs.
inline uint64_t InputsSignature(Action action,const Overrides& bindings,const Inputs* inputs) noexcept
{
    if(action>=Count) return 0;
    uint64_t codes[kRowInputs]{};
    unsigned count=0;
    const auto code=[](uint32_t held,uint32_t press,InputMode mode)
    { return (uint64_t(held)<<32)|press|(mode==InputMode::Toggle?1ull<<63:0); };
    const RowInputs none{};
    const auto& row=inputs?(*inputs)[action]:none;
    const uint32_t own=Bit(Resolve(action,bindings[action]));
    codes[count++]=code(own?HeldBits(row.held):0u,own,row.mode);
    for(const auto& more:row.more)
        if(ListedSource(more.press)&&Bit(more.press)) codes[count++]=code(HeldBits(more.held),Bit(more.press),more.mode);
    for(unsigned i=1;i<count;++i)
        for(unsigned j=i;j>0&&codes[j-1]>codes[j];--j) {const uint64_t swap=codes[j];codes[j]=codes[j-1];codes[j-1]=swap;}
    uint64_t value=14695981039346656037ull;
    for(unsigned i=0;i<count;++i) {value^=codes[i];value*=1099511628211ull;}
    return value;
}
inline uint64_t Fingerprint(const Inputs& inputs) noexcept
{
    uint64_t value=14695981039346656037ull;
    const auto mix=[&](uint64_t part) {value^=part;value*=1099511628211ull;};
    for(const auto& row:inputs)
    {
        for(const int source:row.held) mix(uint64_t(source));
        mix(uint64_t(row.mode));
        for(const auto& more:row.more)
        {
            for(const int source:more.held) mix(uint64_t(source));
            mix(uint64_t(more.press));mix(uint64_t(more.mode));
        }
    }
    return value;
}
// True if a game's rows use any of `controls`.
inline bool UsesControls(const Overrides& bindings,const Inputs& inputs,uint32_t controls) noexcept
{
    for(unsigned a=0;a<Count;++a) if(ControlBits(static_cast<Action>(a),bindings,&inputs)&controls) return true;
    return false;
}
// True if an action's inputs, its own or a further one, hold the support hand on the gun (the gun grip's zoom).
inline bool HoldsHandOnGun(Action action,const Overrides& bindings,const Inputs& inputs) noexcept
{ return (ControlBits(action,bindings,&inputs)&Bit(HandOnGun))!=0; }

// THE EXTRA ROWS (VR Mappings, under the actions): the game's actions no action row names, each read in one context -- a
// vehicle seat, on foot, or anywhere in play. Each has inputs as a row does; any of them presses the action's own MCC
// button beside whatever the action rows press, only in its context. Its own input may be Automatic: it then presses
// nothing of its own, and the action stays on the game's button, pressed whenever another row sends that button, as
// before it had a row. A control already down as a context comes up waits for its release, so the press that boarded
// presses nothing.
enum class ExtraContext : unsigned { Seat, OnFoot, Anywhere, Count };
// Files keep these keys: a new row goes at the end.
enum ExtraAction : unsigned { BansheeBomb, BansheeTrick, FireLeft, ReloadLeft, DualWield, PreviousGrenade,
    ActivateWaypoint, VehicleSecondary, VehicleTrick2, FlyUp, FlyDown, Scoreboard, SpecialAction, SeatSwitchWeapon,
    ExtraCount };
inline constexpr const char* kExtraKeys[ExtraCount]{"banshee_bomb","banshee_trick","fire_left","reload_left",
    "dual_wield","previous_grenade","activate_waypoint","vehicle_secondary","vehicle_trick_2","fly_up","fly_down",
    "scoreboard","special_action","seat_switch_weapon"};
inline constexpr const char* kExtraNames[ExtraCount]{"Banshee bomb","Banshee trick","Fire left weapon (dual wielding)",
    "Reload left weapon (dual wielding)","Dual wield (pick up a second weapon)","Previous grenade","Activate waypoint",
    "Vehicle secondary fire / boost","Second vehicle trick / brake","Fly up","Fly down","Scoreboard",
    "Special action (some missions)","Switch weapon (in a seat)"};
inline constexpr ExtraContext kExtraContexts[ExtraCount]{ExtraContext::Seat,ExtraContext::Seat,ExtraContext::OnFoot,
    ExtraContext::OnFoot,ExtraContext::OnFoot,ExtraContext::OnFoot,ExtraContext::OnFoot,ExtraContext::Seat,
    ExtraContext::Seat,ExtraContext::Seat,ExtraContext::Seat,ExtraContext::Anywhere,ExtraContext::Anywhere,
    ExtraContext::Seat};
using ExtraRow=std::array<Input,kRowInputs>;
using ExtraBindings=std::array<ExtraRow,ExtraCount>;
using ExtraTransports=std::array<uint32_t,ExtraCount>;
inline constexpr ExtraRow kAutomaticRow{Input{kNoneHeld,Automatic},Input{},Input{},Input{}};
inline constexpr ExtraBindings kExtraDefaults{{
    {Input{kNoneHeld,PrimaryGrip},Input{kNoneHeld,RightClick},Input{},Input{}},
    {Input{kNoneHeld,A},Input{},Input{},Input{}},
    kAutomaticRow,kAutomaticRow,kAutomaticRow,kAutomaticRow,kAutomaticRow,kAutomaticRow,kAutomaticRow,kAutomaticRow,
    kAutomaticRow,kAutomaticRow,kAutomaticRow,kAutomaticRow}};
// Each game's own action for an extra row, as NativeAction names the rows' (kNoAction: the game has none), in the games'
// order Halo 3, ODST, Reach, Halo 4, Halo CE, Halo 2: what each game's converter makes of MCC's own control for it. See
// BANSHEE-SEAT-ACTIONS-EVIDENCE-2026-11-05.md and EXTRA-ACTIONS-EVIDENCE-2026-11-05.md. Reach and Halo 4 have no bomb
// button: their vehicle secondary fire (0x0A, 0x0B, the secondary trigger) is the boost, and a Banshee's fuel rod or a
// Pelican's missiles are a weapon picked with Switch weapon and fired with Fire -- in the headset a bomb row there boosted. Halo 3's and ODST's trick is the seat's jump; the second
// trick is the Banshee's brake in Halo 2, Reach and Halo 4. In Reach and Halo 4 the brake is the lower of the two
// (seated control bit 2), the trick the higher (bit 0x14 / 0x15): in the headset Halo 4's were the other way round. ODST keeps Halo 3's dual-wield actions but has no dual wielding; Halo 2's
// dual-wield pickup is a held binding the reader leaves alone; Back is Halo 2's and Halo CE's graphics switch, so neither
// has a Scoreboard row.
inline constexpr unsigned kExtraNative[ExtraCount][6]{
    {0x0A,0x0A,kNoAction,kNoAction,kNoAction,0x15},
    {0x1E,0x1F,0x0D,0x0E,kNoAction,0x01},
    {0x41,kNoAction,kNoAction,kNoAction,kNoAction,0x17},
    {0x42,kNoAction,kNoAction,kNoAction,kNoAction,kNoAction},
    {0x1F,kNoAction,kNoAction,kNoAction,kNoAction,kNoAction},
    {0x20,0x21,0x48,0x09,0x17,0x03},
    {kNoAction,0x25,kNoAction,kNoAction,kNoAction,kNoAction},
    {0x09,0x09,0x0A,0x0B,0x12,0x19},
    {0x27,0x2B,0x0C,0x0D,kNoAction,0x02},
    {0x22,0x23,0x0E,0x10,kNoAction,kNoAction},
    {0x23,0x24,0x0F,0x11,kNoAction,kNoAction},
    {0x1D,0x0C,0x3B,0x25,kNoAction,kNoAction},
    {kNoAction,kNoAction,kNoAction,0x54,kNoAction,kNoAction},
    {0x02,0x02,0x02,0x01,0x03,0x04}};
// The name an extra row shows in a game: ODST's Back brings up its map as well as the scores.
inline constexpr const char* ExtraName(GameTitle title,ExtraAction action) noexcept
{
    if(action>=ExtraCount) return "";
    if(title==GameTitle::Halo3ODST&&action==Scoreboard) return "Map / scoreboard";
    return kExtraNames[action];
}
inline constexpr unsigned NativeExtraAction(GameTitle title,ExtraAction action) noexcept
{
    const int game=static_cast<int>(title)-1;
    return action<ExtraCount&&game>=0&&game<6?kExtraNative[action][game]:kNoAction;
}
// A game's extra rows as they start. In a seat left Y is the vehicle cameras', so Switch weapon has a seat row of its
// own: in Reach and Halo 4 it picks a Banshee's fuel rod or a Pelican's missiles, which have no bomb button, and it starts
// on the controls the Banshee bomb starts on in the other games. There the bomb row, not in the game, gives them up.
inline constexpr ExtraBindings ExtraDefaults(GameTitle title) noexcept
{
    ExtraBindings defaults=kExtraDefaults;
    if(title==GameTitle::HaloReach||title==GameTitle::Halo4)
    {
        defaults[SeatSwitchWeapon]=kExtraDefaults[BansheeBomb];
        defaults[BansheeBomb]=kAutomaticRow;
    }
    return defaults;
}
// An extra row's input in a file: its own may be Automatic, no input an old context.
inline bool ParseExtraInput(const char* text,Input& input,bool own) noexcept
{
    Input read=input;
    if(!ParseInput(text,read.held,read.press,read.mode,own)||ContextSource(read.press)) return false;
    input=read;
    return true;
}
inline uint32_t ExtraControlBits(const ExtraRow& row) noexcept
{
    uint32_t bits=0;
    for(const auto& input:row) if(ListedSource(input.press)&&Bit(input.press)) bits|=Bit(input.press)|HeldBits(input.held);
    return bits;
}
inline constexpr unsigned ContextBit(ExtraContext context) noexcept { return 1u<<static_cast<unsigned>(context); }
struct ExtraMapper
{
    static constexpr unsigned kContexts=static_cast<unsigned>(ExtraContext::Count);
    std::array<bool,kContexts> live{};
    std::array<uint32_t,kContexts> blocked{};
    ChordState<ExtraCount*kRowInputs> chords;
    // `contexts`: the contexts live this poll (ContextBit).
    // `bridged` (ExtraBridgeOnly): rows the bridge carries, which press no button; `pressed` takes every row pressed.
    uint32_t Apply(uint32_t sources,unsigned contexts,const ExtraBindings& bindings,const ExtraTransports& native,
        uint32_t heldOk=~0u,uint32_t* pressed=nullptr,uint32_t bridged=0) noexcept
    {
        for(unsigned c=0;c<kContexts;++c)
        {
            if(!((contexts>>c)&1u)) {live[c]=false;blocked[c]=0;continue;}
            if(!live[c]) {live[c]=true;blocked[c]=sources;}
            blocked[c]&=sources;
        }
        std::array<ResolvedInput,ExtraCount*kRowInputs> inputs{};
        for(unsigned a=0;a<ExtraCount;++a)
        {
            const unsigned c=static_cast<unsigned>(kExtraContexts[a]);
            if(!live[c]) continue;
            for(unsigned i=0;i<kRowInputs;++i)
            {
                const auto& input=bindings[a][i];
                if(ListedSource(input.press)&&!(Bit(input.press)&blocked[c]))
                    inputs[a*kRowInputs+i]={HeldBits(input.held),Bit(input.press),input.mode};
            }
        }
        const auto presses=chords.Step(inputs,sources,sources,heldOk);
        uint32_t buttons=0,on=0;
        for(unsigned a=0;a<ExtraCount;++a)
            for(unsigned i=0;i<kRowInputs;++i)
                if(presses[a*kRowInputs+i])
                {
                    on|=1u<<a;
                    if(!((bridged>>a)&1u)) buttons|=native[a];
                }
        if(pressed) *pressed=on;
        return buttons;
    }
};

// An extra row's inputs as one number, the way InputsSignature numbers an action row's: an Automatic own input is none.
inline uint64_t ExtraInputsSignature(const ExtraRow& row) noexcept
{
    uint64_t codes[kRowInputs]{};
    unsigned count=0;
    for(const auto& input:row)
        if(ListedSource(input.press)&&Bit(input.press))
            codes[count++]=(uint64_t(HeldBits(input.held))<<32)|Bit(input.press)|
                (input.mode==InputMode::Toggle?1ull<<63:0);
    for(unsigned i=1;i<count;++i)
        for(unsigned j=i;j>0&&codes[j-1]>codes[j];--j) {const uint64_t swap=codes[j];codes[j]=codes[j-1];codes[j-1]=swap;}
    uint64_t value=14695981039346656037ull;
    for(unsigned i=0;i<count;++i) {value^=codes[i];value*=1099511628211ull;}
    return value;
}
// THE EXTRA ROWS ONLY THE BRIDGE CAN CARRY, as BridgeOnlyActions finds them among the action rows: a row with inputs of
// its own whose game button also carries an action on other controls -- an action row, or another extra row with inputs
// of its own. Halo 3's A is Jump, a flyer's climb, the Banshee's trick and its brake at once: a brake on the support grip
// pressing A climbed a Hornet too. Through the bridge the row presses its own action alone, and presses no button.
inline uint32_t ExtraBridgeOnly(const ExtraBindings& extra,const ExtraTransports& extraNative,const Transports& native,
    const Overrides& bindings,const Inputs* inputs) noexcept
{
    uint32_t only=0;
    for(unsigned x=0;x<ExtraCount;++x)
    {
        if(!ExtraControlBits(extra[x])) continue;   // Automatic, or nothing: the game's own button
        const uint32_t button=extraNative[x];
        if(!button) continue;
        const uint64_t own=ExtraInputsSignature(extra[x]);
        bool shared=false;
        for(unsigned a=0;a<Count&&!shared;++a)
            shared=(native[a]&button)&&InputsSignature(static_cast<Action>(a),bindings,inputs)!=own;
        for(unsigned y=0;y<ExtraCount&&!shared;++y)
            shared=y!=x&&(extraNative[y]&button)&&ExtraControlBits(extra[y])&&ExtraInputsSignature(extra[y])!=own;
        if(shared) only|=1u<<x;
    }
    return only;
}

// THE MOD'S OWN ROWS (VR Mappings, every game): actions of the mod's, not the game's. Pause: the pause view and Start, as
// the controller's menu button gives them; it starts on Y held and B pressed, or B held and Y pressed. While one of its
// inputs is on, the controls it uses press nothing else, until each is let go.
enum ModAction : unsigned { Pause, ModCount };
inline constexpr const char* kModKeys[ModCount]{"pause"};
inline constexpr const char* kModNames[ModCount]{"Pause (Start)"};
using ModBindings=std::array<ExtraRow,ModCount>;
inline constexpr ModBindings kModDefaults{{
    {Input{{Y,Unbound},B},Input{{B,Unbound},Y},Input{},Input{}}}};
// True if a row presses one control while the other is held, either way round (the Y + B pause, as Reclaimer's keys
// and the physical pad's pause read it).
inline bool HoldsTogether(const ExtraRow& row,int one,int other) noexcept
{
    for(const auto& input:row)
        if((input.press==one&&(HeldBits(input.held)&Bit(other)))||(input.press==other&&(HeldBits(input.held)&Bit(one))))
            return true;
    return false;
}
struct ModMapper
{
    ChordState<ModCount*kRowInputs> chords;
    std::array<bool,ModCount> was{};
    uint32_t consumed{};
    // One poll: the actions that start on it (a bit per ModAction). `live` false (a menu, the pointer, or no pause to
    // give) starts nothing and lets every control go.
    uint32_t Step(uint32_t sources,bool live,const ModBindings& bindings) noexcept
    {
        if(!live) {chords.Reset(true);chords.previous=sources;was={};consumed=0;return 0;}
        std::array<ResolvedInput,ModCount*kRowInputs> inputs{};
        for(unsigned a=0;a<ModCount;++a)
            for(unsigned i=0;i<kRowInputs;++i)
            {
                const auto& input=bindings[a][i];
                if(ListedSource(input.press)) inputs[a*kRowInputs+i]={HeldBits(input.held),Bit(input.press),input.mode};
            }
        const auto presses=chords.Step(inputs,sources,sources,~0u);
        consumed&=sources;
        uint32_t started=0;
        for(unsigned a=0;a<ModCount;++a)
        {
            bool on=false;
            for(unsigned i=0;i<kRowInputs;++i)
                if(presses[a*kRowInputs+i]) {on=true;consumed|=inputs[a*kRowInputs+i].held|inputs[a*kRowInputs+i].press;}
            if(on&&!was[a]) started|=1u<<a;
            was[a]=on;
        }
        return started;
    }
};

// What one poll adds to the rows.
struct PollOptions
{
    uint32_t heldOk=~0u;      // controls that may be held first (a seat or two guns keep the support trigger plain)
    uint32_t forced=0;        // actions pressed whatever the controls (Zoom, pressed out as the hand leaves the gun)
    uint32_t absent=0;        // actions the game has none of: their inputs press and claim nothing
    bool ownOnly=false;       // only inputs made of the mod's own controls (kOwnControls): remapping is off
    bool standUp=false;       // toggles end: the player has just left their feet
};

// Reconfiguration, pause, reconnect and title changes must wait for held
// controls to release. A held trigger cannot become a new action on resume.
struct Mapper
{
    static constexpr unsigned kSlots=kRowInputs+1; // a row's inputs, and `also`
    uint64_t identity{},setup{}; uint32_t blocked{}, flashlightHeld{}; bool ready{};
    // Times a held control was latched by a change (a held press then reaches the game as a tap at most).
    uint32_t relatched{};
    ChordState<Count*kSlots> chords;
    // `also`: one more control for each action (Unbound for none), beside its row's. `inputs`: each row's held-first
    // controls, mode and further inputs (RowInputs).
    MappedActions ApplyDetailed(uint32_t sources,const Overrides& bindings,const Transports& native,
        uint64_t epoch,bool available,bool disableFlashlight=false,const Overrides* also=nullptr,
        const Inputs* inputs=nullptr,const PollOptions& poll={}) noexcept
    {
        // Toggles start over when what the rows say changes, not when the game's layout is read again: Automatic rows
        // follow that read, and a read that fails for a moment must not stand a crouched player up.
        const uint64_t nextSetup=epoch^(also?Fingerprint(*also,Transports{})*31u:0u)^
            (inputs?Fingerprint(*inputs)*37u:0u)^(poll.ownOnly?0x9E3779B97F4A7C15ull:0u);
        const uint64_t next=nextSetup^Fingerprint(bindings,native);
        if(!available||!ready||identity!=next) {relatched+=(sources&~blocked)!=0;blocked|=sources;}
        if(identity!=next) chords.Reset(setup!=nextSetup);
        if(poll.standUp) chords.toggled.fill(false);
        blocked&=sources; identity=next; setup=nextSetup; ready=available;
        std::array<ResolvedInput,Count*kSlots> list{};
        // Flashlight suppression is for the gamepad's controls: an input made only of the mod's own (ODST's VISR on the
        // right stick) is never a flashlight button pressed by mistake.
        uint32_t flashlightSource=0,flashlightOwnSlots=0;
        const RowInputs none{};
        for(unsigned a=0;a<Count;++a)
        {
            if((poll.absent>>a)&1u) continue;
            const auto& row=inputs?(*inputs)[a]:none;
            ResolvedInput* slot=&list[a*kSlots];
            slot[0]={HeldBits(row.held),Bit(Resolve(static_cast<Action>(a),bindings[a])),row.mode};
            if(also) slot[1]={0,Bit((*also)[a]),InputMode::Hold};
            for(unsigned i=0;i<kMoreInputs;++i)
                if(ListedSource(row.more[i].press))
                    slot[2+i]={HeldBits(row.more[i].held),Bit(row.more[i].press),row.more[i].mode};
            for(unsigned i=0;i<kSlots;++i)
            {
                const bool own=slot[i].press&&!((slot[i].held|slot[i].press)&~kOwnControls);
                if(poll.ownOnly&&!own) slot[i]={};
                if(a==Flashlight&&own) flashlightOwnSlots|=1u<<i;
                else if(a==Flashlight) flashlightSource|=slot[i].press;
            }
        }
        const auto presses=chords.Step(list,sources,sources&~blocked,poll.heldOk);
        flashlightHeld&=sources;
        if(disableFlashlight) flashlightHeld|=sources&flashlightSource;
        if(!available) return {};
        const bool flashlightSuppressed=disableFlashlight||(flashlightHeld&flashlightSource);
        MappedActions result{};
        for(unsigned a=0;a<Count;++a)
        {
            bool pressed=(poll.forced>>a)&1u;
            for(unsigned i=0;i<kSlots;++i)
                pressed=pressed||(presses[a*kSlots+i]&&
                    !(a==Flashlight&&flashlightSuppressed&&!((flashlightOwnSlots>>i)&1u)));
            if(pressed)
            {
                result.actions |= 1u << a;
                result.transports |= native[a];
            }
        }
        return result;
    }
    uint32_t Apply(uint32_t sources,const Overrides& bindings,const Transports& native,
        uint64_t epoch,bool available,bool disableFlashlight=false) noexcept
    {
        return ApplyDetailed(sources,bindings,native,epoch,available,
            disableFlashlight).transports;
    }
};
}
