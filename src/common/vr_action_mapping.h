#pragma once
#include "runtime_types.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace vr_mapping
{
enum Action : unsigned { Fire, Grenade, Jump, Melee, Reload, Interact, SwitchWeapon,
    SwitchGrenade, Equipment, Crouch, Zoom, Flashlight, Sprint, Count };
enum Source : int { Automatic, Unbound, PrimaryTrigger, SupportTrigger, PrimaryGrip,
    SupportGrip, A, B, X, Y, LeftClick, RightClick, DpadUp, DpadDown, DpadLeft,
    DpadRight, SupportTriggerFree, SupportTriggerOnGun, SupportTriggerAir, SourceCount };
enum class Profile : unsigned { Unknown, Touch, Index, Vive, Wmr, Simple };
inline constexpr const char* kKeys[Count]{"fire","grenade","jump","melee","reload",
    "interact","switch_weapon","switch_grenade","equipment","crouch","zoom","flashlight","sprint"};
inline constexpr const char* kNames[Count]{"Fire","Grenade / secondary fire","Jump","Melee",
    "Reload","Use / pick up","Switch weapon","Switch grenade","Equipment / ability",
    "Crouch","Zoom","Flashlight / night vision","Sprint"};
inline constexpr const char* kSourceNames[SourceCount]{"Automatic","Unbound","Weapon trigger",
    "Support trigger","Weapon grip","Support grip","Right A / lower face","Right B / upper face",
    "Left X / lower face","Left Y / upper face","Left stick / pad click","Right stick / pad click",
    "D-pad gesture up","D-pad gesture down","D-pad gesture left","D-pad gesture right",
    "Support trigger, hand free","Support trigger, gripping the gun","Support trigger, gripping air"};
using Overrides = std::array<int, Count>;
using Transports = std::array<uint32_t, Count>;
inline constexpr int kDefaults[Count]{PrimaryTrigger,SupportTrigger,A,PrimaryGrip,X,X,
    Y,DpadLeft,SupportGrip,LeftClick,RightClick,DpadUp,Unbound};
inline constexpr unsigned kNoAction=0xFF;
// The support trigger by what the support hand is doing (SupportTriggerContext) belongs to the Input combinations, not
// to a row: gripping the gun zooms (grip_contextual_trigger) and gripping air uses equipment (trigger_air_equipment),
// beside whatever Zoom's and Equipment's own rows press. The three sources stay at the end of the list, out of the rows'
// choices (kRowSourceCount).
inline constexpr int kRowSourceCount=SupportTriggerFree;
inline constexpr bool ContextSource(int source) noexcept
{ return source==SupportTriggerFree||source==SupportTriggerOnGun||source==SupportTriggerAir; }
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
// Each action's second source for Mapper::ApplyDetailed: the Input combinations' contexts.
inline std::array<int,Count> ContextSources(bool contextualZoom,bool airEquipment) noexcept
{
    std::array<int,Count> also{};
    also.fill(Unbound);
    if(contextualZoom) also[Zoom]=SupportTriggerOnGun;
    if(airEquipment) also[Equipment]=SupportTriggerAir;
    return also;
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
// The support trigger by what the support hand is doing as it is pulled: free, on the gun, or gripping air. The pull
// keeps its meaning until it is let go, so moving the hand cannot throw a grenade halfway through a zoom. The one
// exception is the gun: when the hand leaves it the zoom ends, and that pull does nothing more.
// `plain` is a hand with no such meaning (dual wielding, where the trigger fires the other gun, or a vehicle seat).
// `claimed` is a grip the mod is using for a magazine or a holster: the pull does nothing.
// `gunUsable` false makes a hand on the gun a free hand; `airUsable` false does the same for an air grip.
struct SupportTriggerContext
{
    bool down{}; int latched=Unbound;
    uint32_t Step(bool triggerDown,bool gripDown,bool onGun,bool claimed,bool plain,bool gunUsable,
        bool airUsable) noexcept
    {
        if(!triggerDown) {down=false;latched=Unbound;return 0;}
        if(!down)
        {
            down=true;
            latched=plain?SupportTriggerFree:claimed?Unbound:onGun&&gunUsable?SupportTriggerOnGun:
                onGun||!gripDown||!airUsable?SupportTriggerFree:SupportTriggerAir;
        }
        else if(latched==SupportTriggerOnGun&&!onGun) latched=Unbound;
        return Bit(latched);
    }
};
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

// Reconfiguration, pause, reconnect and title changes must wait for held
// controls to release. A held trigger cannot become a new action on resume.
struct Mapper
{
    uint64_t identity{}; uint32_t blocked{}, flashlightHeld{}; bool ready{};
    // `also`: a second source for each action (Unbound for none), beside its row's: ContextSources.
    MappedActions ApplyDetailed(uint32_t sources,const Overrides& bindings,const Transports& native,
        uint64_t epoch,bool available,bool disableFlashlight=false,const Overrides* also=nullptr) noexcept
    {
        const uint64_t next=Fingerprint(bindings,native)^epoch^(also?Fingerprint(*also,Transports{})*31u:0u);
        if(!available||!ready||identity!=next) blocked|=sources;
        blocked&=sources; identity=next; ready=available;
        const uint32_t flashlightSource=Bit(Resolve(Flashlight,bindings[Flashlight]));
        flashlightHeld&=sources;
        if(disableFlashlight) flashlightHeld|=sources&flashlightSource;
        if(!available) return {};
        const uint32_t active=sources&~blocked;
        MappedActions result{};
        for(unsigned a=0;a<Count;++a)
            if(!(a==Flashlight&&(disableFlashlight||(flashlightHeld&flashlightSource)))&&
               (active&(Bit(Resolve(static_cast<Action>(a),bindings[a]))|(also?Bit((*also)[a]):0u))))
            {
                result.actions |= 1u << a;
                result.transports |= native[a];
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
