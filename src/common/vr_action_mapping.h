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
    DpadRight, SourceCount };
enum class Profile : unsigned { Unknown, Touch, Index, Vive, Wmr, Simple };
inline constexpr const char* kKeys[Count]{"fire","grenade","jump","melee","reload",
    "interact","switch_weapon","switch_grenade","equipment","crouch","zoom","flashlight","sprint"};
inline constexpr const char* kNames[Count]{"Fire","Grenade / secondary fire","Jump","Melee",
    "Reload","Use / pick up","Switch weapon","Switch grenade","Equipment / ability",
    "Crouch","Zoom","Flashlight / night vision","Sprint"};
inline constexpr const char* kSourceNames[SourceCount]{"Automatic","Unbound","Weapon trigger",
    "Support trigger","Weapon grip","Support grip","Right A / lower face","Right B / upper face",
    "Left X / lower face","Left Y / upper face","Left stick / pad click","Right stick / pad click",
    "D-pad gesture up","D-pad gesture down","D-pad gesture left","D-pad gesture right"};
using Overrides = std::array<int, Count>;
using Transports = std::array<uint32_t, Count>;
inline constexpr int kDefaults[Count]{PrimaryTrigger,SupportTrigger,A,PrimaryGrip,X,X,
    Y,DpadLeft,SupportGrip,LeftClick,RightClick,DpadUp,Unbound};
inline constexpr unsigned kNoAction=0xFF;
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
// Reconfiguration, pause, reconnect and title changes must wait for held
// controls to release. A held trigger cannot become a new action on resume.
struct Mapper
{
    uint64_t identity{}; uint32_t blocked{}, flashlightHeld{}; bool ready{};
    uint32_t Apply(uint32_t sources,const Overrides& bindings,const Transports& native,
        uint64_t epoch,bool available,bool disableFlashlight=false) noexcept
    {
        const uint64_t next=Fingerprint(bindings,native)^epoch;
        if(!available||!ready||identity!=next) blocked|=sources;
        blocked&=sources; identity=next; ready=available;
        const uint32_t flashlightSource=Bit(Resolve(Flashlight,bindings[Flashlight]));
        flashlightHeld&=sources;
        if(disableFlashlight) flashlightHeld|=sources&flashlightSource;
        if(!available) return 0;
        const uint32_t active=sources&~blocked;
        uint32_t result=0;
        for(unsigned a=0;a<Count;++a)
            if(!(a==Flashlight&&(disableFlashlight||(flashlightHeld&flashlightSource)))&&
               (active&Bit(Resolve(static_cast<Action>(a),bindings[a])))) result|=native[a];
        return result;
    }
};
}
