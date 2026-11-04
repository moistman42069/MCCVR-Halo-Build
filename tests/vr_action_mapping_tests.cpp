#include "../src/common/vr_action_mapping.h"
#include "../src/common/vr_action_passthrough.h"
#include "../src/dll/native_vr_actions.h"
#include "../src/common/config.h"
#include "../src/common/input_preset.h"
#include "../src/common/weapon_interaction_logic.h"
#include "../src/common/scope_action_input.h"
#include "../src/common/physical_crouch_input.h"
#include "../src/common/vehicle_path_identity.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <limits>

using namespace vr_mapping;
unsigned checks{};
void Check(bool ok,const char* why)
{++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void CheckInputPresets()
{
    using namespace vr_mapping;
    Check(LayoutSource(0x1000)==A&&LayoutSource(0x100)==SupportGrip&&LayoutSource(1u<<16)==SupportTrigger&&
        LayoutSource(0x4)==DpadLeft&&LayoutSource(0x10)==Unbound&&LayoutSource(0)==Unbound,"the unmapped route's buttons");
    Overrides rows{};
    rows[Fire]=Y;
    Transports layout{};
    layout[Fire]=1u<<17;layout[Jump]=0x1000;
    const auto followed=EffectiveOverrides(rows,layout,true);
    Check(followed[Fire]==Y&&followed[Jump]==A&&followed[Melee]==Unbound,"Automatic rows follow the game's layout, others stay");
    Check(EffectiveOverrides(rows,layout,false)[Jump]==Automatic,"an unknown layout leaves a row Automatic");

    Check(OneHandedGripOnGun(true,true,.25f*.25f),"a grip near a pistol's hand is on it");
    Check(!OneHandedGripOnGun(true,true,.35f*.35f),"far from it, an air grip");
    Check(!OneHandedGripOnGun(false,true,.1f*.1f)&&!OneHandedGripOnGun(true,false,.1f*.1f),"only a one-handed gun, only with the grip held");
    SupportTriggerContext t;
    Check(t.Step(false,false,false,false,false,true)==0,"no pull");
    Check(t.Step(true,false,false,false,false,true)==Bit(SupportTriggerFree),"a free hand");
    Check(t.Step(true,true,true,false,false,true)==Bit(SupportTriggerFree),"a pull keeps its meaning as the hand moves");
    t.Step(false,false,false,false,false,true);
    Check(t.Step(true,true,true,false,false,true)==Bit(SupportTriggerOnGun),"gripping the gun");
    Check(t.Step(true,false,false,false,false,true)==0,"leaving the gun ends the zoom without a grenade");
    Check(t.Step(true,false,false,false,false,true)==0,"for the rest of the pull");
    t.Step(false,false,false,false,false,true);
    Check(t.Step(true,false,true,false,false,true)==Bit(SupportTriggerOnGun),"a toggled two-hand hold counts with the grip up");
    t.Step(false,false,false,false,false,true);
    Check(t.Step(true,true,false,false,false,true)==Bit(SupportTriggerAir),"gripping air");
    t.Step(false,false,false,false,false,true);
    Check(t.Step(true,true,false,false,false,false)==Bit(SupportTriggerFree),"air off: a free hand");
    t.Step(false,false,false,false,false,true);
    Check(t.Step(true,false,false,true,false,true)==0,"a magazine or holster grip sends nothing");
    t.Step(false,false,false,false,false,true);
    Check(t.Step(true,true,true,false,true,true)==Bit(SupportTriggerFree),"dual wielding or a seat keeps the plain trigger");

    {
        // Letting go steps a two-level zoom all the way out, reading the game's level after each press.
        ZoomExit z;
        Check(!z.Step(true,true,false,true,0,1000),"on the gun: nothing");
        Check(!z.Step(true,false,false,true,0,1010),"let go: it settles first");
        Check(z.Step(true,false,false,true,0,1120),"then a press from the first level");
        Check(z.Step(true,false,false,true,1,1150),"held its full length, whatever the game reads");
        Check(!z.Step(true,false,false,true,1,1190),"let go");
        Check(!z.Step(true,false,false,true,1,1200),"the level went deeper: the gap first");
        Check(z.Step(true,false,false,true,1,1240),"then the next press");
        z.Step(true,false,false,true,1,1300);
        Check(!z.Step(true,false,false,true,-1,1320)&&z.phase==ZoomExit::Phase::Idle,"out: finished");
        Check(!z.Step(true,false,false,true,-1,1500),"and it stays finished");
    }
    {
        ZoomExit z;
        z.Step(true,true,false,true,-1,1000);
        z.Step(true,false,false,true,-1,1010);
        Check(!z.Step(true,false,false,true,-1,1200)&&z.phase==ZoomExit::Phase::Idle,"not zoomed: never a press, so it cannot zoom in");
    }
    {
        ZoomExit z;
        z.Step(true,true,false,true,0,1000);
        z.Step(true,false,false,true,0,1010);
        z.Step(true,false,false,true,0,1120);
        z.Step(true,false,false,true,0,1190);
        Check(!z.Step(true,false,false,true,0,1300),"no answer yet: it waits");
        Check(z.Step(true,false,false,true,0,1425),"a press the game did not see goes again");
    }
    {
        ZoomExit z;
        z.Step(true,true,false,true,0,1000);
        z.Step(true,false,false,false,0,1010);
        Check(!z.Step(true,false,false,false,0,1200)&&z.phase==ZoomExit::Phase::Idle,"a level not read: no press");
        z.Step(true,true,false,true,0,2000);
        z.Step(true,false,false,true,0,2010);
        Check(!z.Step(true,false,true,true,0,2020)&&z.phase==ZoomExit::Phase::Idle,"a fresh pull of the trigger: the player's own");
        z.Step(true,true,true,true,0,3000);
        z.Step(true,false,true,true,0,3010);
        Check(z.Step(true,false,true,true,0,3120),"a trigger still held from the zoom is not a fresh pull");
        Check(!z.Step(true,true,true,true,0,3130)&&z.phase==ZoomExit::Phase::Idle,"the hand coming back ends it");
        ZoomExit stuck;
        stuck.Step(true,true,false,true,0,1000);
        stuck.Step(true,false,false,true,0,1010);
        int pressed=0;
        bool was=false;
        for(uint64_t t=1020;t<9000;t+=10)
        {
            const bool now=stuck.Step(true,false,false,true,0,t);
            pressed+=now&&!was;
            was=now;
        }
        Check(pressed==ZoomExit::kMostPresses,"a level that never moves gives up");
    }

    Config scheme{};
    scheme.turn_smooth=false;scheme.vr_action_mapping=true;scheme.trigger_air_equipment=true;scheme.turn_snap_deg=45.f;
    scheme.vr_bindings[3][Zoom]=SupportTriggerOnGun;scheme.world_scale=9.f;
    Config loaded{};
    input_preset::Parse(input_preset::Serialize(scheme).c_str(),loaded);
    Check(input_preset::SameScheme(loaded,scheme),"a preset round trips");
    Check(loaded.world_scale==Config{}.world_scale,"a preset carries no setting outside the scheme");
    Config shoulder{};
    shoulder.weapon_holsters=true;shoulder.weapon_holster_location=2;shoulder.weapon_holster_click=true;
    shoulder.weapon_holster_slide=false;shoulder.weapon_holster_radius_m=weapon_interaction::kOverShoulderRadius;
    shoulder.manual_reload_skip_animations=true;shoulder.weapon_pouch_location=1;shoulder.weapon_holster_custom_x_m=.5f;
    Config shoulderLoaded{};
    input_preset::Parse(input_preset::Serialize(shoulder).c_str(),shoulderLoaded);
    Check(shoulderLoaded.weapon_holster_radius_m==weapon_interaction::kOverShoulderRadius&&
        shoulderLoaded.manual_reload_skip_animations&&shoulderLoaded.weapon_pouch_location==1,
        "a preset keeps the over-the-shoulder switch and the reload options");
    Check(shoulderLoaded.weapon_holster_custom_x_m==Config{}.weapon_holster_custom_x_m,
        "where the holster sits on your body stays out of a preset");
    Config twoHand{};
    twoHand.persistent_support_grip=false;twoHand.two_hand_offhand_influence=.25f;twoHand.left_grip_forward_m=.2f;
    Config twoHandLoaded{};
    input_preset::Parse(input_preset::Serialize(twoHand).c_str(),twoHandLoaded);
    Check(!twoHandLoaded.persistent_support_grip&&std::fabs(twoHandLoaded.two_hand_offhand_influence-.25f)<.002f&&
        twoHandLoaded.left_grip_forward_m==Config{}.left_grip_forward_m,
        "a preset keeps two-handed aiming, but not where the support hand grabs the gun");
    Config kept{};
    kept.world_scale=2.f;
    input_preset::CopyScheme(kept,scheme);
    Check(kept.world_scale==2.f&&kept.vr_bindings[3][Zoom]==SupportTriggerOnGun,"applying a preset leaves the rest alone");
    Config missing{};
    input_preset::Parse("# only one key\nturn_snap_deg = 15\nvr_bind_halo3_fire = 999\n",missing);
    Check(missing.turn_snap_deg==15.f&&missing.vr_bindings[0][Fire]==Automatic&&missing.turn_smooth==Config{}.turn_smooth,
        "keys a preset omits take their default, and bad values are refused");
    Check(input_preset::ParseDescription((input_preset::DescriptionLine(" For new\nplayers # one ")+input_preset::Serialize(scheme)).c_str())==
        "For new players # one","a description survives a preset file, on one line");
    Check(input_preset::DescriptionLine("  ").empty()&&input_preset::ParseDescription(input_preset::Serialize(scheme).c_str()).empty(),
        "no description is no line");
    Config described{};
    input_preset::Parse((input_preset::DescriptionLine("turn_snap_deg = 99")+input_preset::Serialize(scheme)).c_str(),described);
    Check(input_preset::SameScheme(described,scheme),"a description never reads as a setting");
    Config def{},plain{};
    input_preset::BuiltIn(0,def);
    Check(input_preset::SameScheme(def,plain)&&!def.vr_action_mapping&&!def.trigger_air_equipment,"Default is the canon: nothing remapped");
    Config vr{};
    input_preset::BuiltIn(1,vr);
    Check(vr.vr_action_mapping&&vr.vr_bindings[2][Jump]==kDefaults[Jump],"the VR layout is written out in full");
    Check(input_preset::SafeName("  My: preset/1 ")=="My preset1"&&input_preset::SafeName("default").empty()&&
        input_preset::SafeName("../..")=="","a preset name is a safe file name, and never a built-in's");
}

void CheckScopeAdmission()
{
    // Exercise the production release detector, not a duplicate toggle model.
    for(int source=PrimaryTrigger;source<SourceCount;++source)
    {
        ScopeActionInput scope;
        auto tick=[&](uint32_t bits,int selected,uint64_t epoch=1,bool allowed=true,bool cancel=false) {
            return scope.Update(true,bits,selected,epoch,0,allowed,cancel);
        };
        Check(!tick(Bit(source),source).changed,"held input on first scope admission cannot toggle");
        Check(!tick(0,source).changed,"first-admission release is discarded");
        Check(!tick(Bit(source),source).changed,"scope waits for release");
        auto result=tick(0,source);
        Check(result.changed&&result.active,"every mapped source can deliberately open scope");
        tick(Bit(source),source);
        Check(!tick(Bit(source),Unbound).changed,"rebinding held zoom to Unbound does not toggle");
        Check(!tick(0,Unbound).changed,"Unbound release has no scope action");
        Check(!tick(Bit(source),source).changed,"held input on rebind back is blocked");
        Check(!tick(0,source).changed,"rebind release cannot close existing scope");
        tick(Bit(source),source);
        Check(!tick(Bit(source),source,2).changed,"profile or title epoch change cancels pending scope gesture");
        Check(!tick(0,source,2).changed,"epoch release is discarded");
        tick(Bit(source),source,2);
        scope.Suspend();
        Check(!tick(Bit(source),source,2).changed&&!tick(0,source,2).changed,"reconnect requires release before scope can toggle");
        tick(Bit(source),source,2);
        Check(!tick(0,source,2,false).changed,"menu entry cannot look like a scope release");
        Check(!tick(Bit(source),source,2).changed&&!tick(0,source,2).changed,"held menu input stays consumed through resume");
        tick(Bit(source),source,2);
        Check(!tick(Bit(source),source,2,true,true).changed&&!tick(0,source,2).changed,"menu or pause chord cancels whole mapped gesture");
        tick(Bit(source),source,2);
        result=tick(0,source,2);
        Check(result.changed&&!result.active,"deliberate press after all boundaries closes original scope");
    }
    struct Pad {float trigR=0,trigL=0,gripR=0,gripL=0,moveX=0,moveY=0,dpadX=0,dpadY=0;
        bool a=false,b=false,x=false,y=false,clickL=false,clickR=false,thumbrestDpad=false,exclusiveInput=false;} pad;
    for(unsigned route=1;route<=2;++route) for(int source=DpadUp;source<=DpadRight;++source)
    {
        pad={};pad.thumbrestDpad=route==2;pad.exclusiveInput=route==2;
        ScopeActionInput scope;
        scope.Update(true,ScopeAndActionSources(pad,route==1),source,1,route,true,false);
        float x=source==DpadRight?1.f:(source==DpadLeft?-1.f:0.f);
        float y=source==DpadUp?1.f:(source==DpadDown?-1.f:0.f);
        pad.moveX=pad.dpadX=x;pad.moveY=pad.dpadY=y;
        auto bits=ScopeAndActionSources(pad,route==1);
        Check(bits==Bit(source),"both D-pad routes collect the selected directional scope source");
        scope.Update(true,bits,source,1,route,true,false);
        pad.moveX=pad.moveY=pad.dpadX=pad.dpadY=0;
        Check(scope.Update(true,ScopeAndActionSources(pad,route==1),source,1,route,true,false).changed,
            "head and exclusive thumb-rest D-pad release both toggle scope");
        scope.Update(true,bits,source,1,route,true,false);
        Check(!scope.Update(true,0,source,1,0,true,false).changed,"leaving D-pad gesture cancels rather than releases zoom");
    }
    {
        // MCC polls the pad from more than one thread, and they share one detector (input.cpp g_scopeInput): the
        // release one poll takes is not taken again by the next.
        ScopeActionInput shared;
        auto poll=[&](uint32_t bits){return shared.Update(true,bits,RightClick,1,0,true,false);};
        poll(0);poll(0);poll(Bit(RightClick));poll(Bit(RightClick));
        Check(poll(0).changed&&!poll(0).changed,"a second poller does not take the same zoom release again");
    }
    ScopeActionInput scope;
    scope.Update(true,0,A,1,0,true,false);
    scope.Update(true,Bit(A),A,1,0,true,false);
    Check(!scope.Update(true,Bit(A),B,1,0,true,false).changed,"switching held zoom to another released source does not toggle");
    scope.Update(true,0,B,1,0,true,false);
    scope.Update(true,Bit(B),B,1,0,true,false);
    Check(scope.Update(true,0,B,1,0,true,false).active,"new binding admits fresh scope gesture");
    Check(!scope.Update(false,Bit(B),B,1,0,true,false).active,"feature disable clears scope state");
    Check(!scope.Update(true,Bit(B),B,1,0,true,false).changed&&!scope.Update(true,0,B,1,0,true,false).changed,
        "re-enabling scope while held cannot enter native or VR zoom");
}
void CheckPhysicalCrouchInput()
{
    for(const auto title:{GameTitle::HaloCE,GameTitle::Halo2,GameTitle::Halo3,GameTitle::Halo3ODST,GameTitle::HaloReach,GameTitle::Halo4})
    {
        for(const uint32_t mask:{0x40u,1u<<16,1u<<17})
        {
            PhysicalCrouchInput crouch;
            uint64_t now=1;
            // The standing reference is handed in (the OpenXR frame thread learns it), with its sample count.
            float baseline=1.7f;uint64_t reference=0x12345678ABCDEF01ull;
            auto tick=[&](float y,bool available=true,uint32_t generation=0xABCDEF01u) {
                return crouch.Update(title,generation,y,baseline,reference,now++,true,available,.22f,mask);
            };
            Overrides unbound{};unbound.fill(Unbound);
            Transports native{};native[Crouch]=mask;
            Mapper mapper;mapper.Apply(0,unbound,native,1,true);
            Check(tick(1.7f)==0,"physical crouch first sample at the reference does not press");
            const auto buttons=mapper.Apply(Bit(LeftClick),unbound,native,1,true)|tick(1.4f);
            Check(buttons==mask,"physical crouch emits verified transport even when VR crouch button is Unbound");
            Check(tick(1.53f)==mask&&tick(1.56f)==0,"physical crouch output uses hysteresis and releases while standing");
            tick(1.4f);crouch.Suspend(true);
            Check(tick(1.4f)==0&&tick(1.4f)==0,"menu/pointer/focus suspension cannot replay held crouch");
            Check(tick(1.7f)==0&&tick(1.4f)==mask,"standing re-admits physical gesture after suspension");
            reference=0x12345678ABCDEF02ull;baseline=1.4f;
            Check(tick(1.4f)==0,"a new standing reference re-arms physical crouch released");
            Check(tick(1.1f)==mask,"fresh crouch measured from the new standing height");
            Check(tick(1.1f,true,0xABCDEF02u)==0,"title generation independently re-arms physical crouch");
            Check(crouch.Update(title,2,1.7f,1.7f,9,now++,false,true,.22f,mask)==0,
                "explicit feature disable clears physical crouch output");
            Check(crouch.Update(title,2,1.4f,1.7f,9,now++,true,true,.22f,mask)==0,
                "feature re-enable with the head already low stands first instead of replaying old crouch");
        }
    }
    PhysicalCrouchInput unavailable;
    Check(unavailable.Update(GameTitle::Halo3,1,1.7f,1.7f,1,1,true,true,.22f,0)==0&&
        unavailable.Update(GameTitle::Halo3,1,1.4f,1.7f,1,2,true,true,.22f,0)==0,
        "native crouch without proven transport cannot inject a guessed button");
}
// One input poll as both production routes compose it (input.cpp): in direct
// mode the mapper sees the bridge's gesture bits, the router sees the REAL
// layout, and the result is the pad buttons plus the bridge publication.
struct Poll { MappedActions mapped; RoutedActions routed; };
Poll RunPoll(Mapper& mapper,GameTitle title,bool direct,uint32_t sources,
    const Overrides& bindings,const Transports& layout)
{
    Transports mapping=layout;
    if(direct) for(unsigned a=0;a<Count;++a) mapping[a]=NativeVrActions_GestureBit(static_cast<Action>(a));
    Poll poll{};
    poll.mapped=mapper.ApplyDetailed(sources,bindings,mapping,(uint64_t(title)<<24)|1,true);
    poll.routed=RouteActions(title,direct,poll.mapped,layout);
    return poll;
}
void CheckDirectContextPassthrough()
{
    constexpr uint32_t padX=0x4000,padA=0x1000,padB=0x2000,padY=0x8000;
    constexpr uint32_t context=(1u<<Reload)|(1u<<Interact);
    // MCC's default Halo 2 layout: Reload (0x2E) and touch_device (0x23) are
    // both on X; Jump on A; Fire on the right trigger.
    Transports layout{};
    for(unsigned a=0;a<Count;++a) layout[a]=1u<<a;
    layout[Reload]=layout[Interact]=padX;layout[Jump]=padA;layout[Fire]=1u<<17;
    Overrides automatic{};
    {
        Mapper mapper;
        RunPoll(mapper,GameTitle::Halo2,true,0,automatic,layout);
        for(unsigned poll=0;poll<40;++poll) // a held X: tap, then well past H2's hold threshold
        {
            const auto p=RunPoll(mapper,GameTitle::Halo2,true,Bit(X),automatic,layout);
            Check(p.mapped.Has(Reload)&&p.mapped.Has(Interact),"H2 default X still names Reload and Use semantically");
            Check(p.routed.buttons==padX,"H2 direct: held X reaches the game as the real X button on every poll");
            Check(!(p.routed.published&context),"H2 direct: bridge publishes neither Reload 2E nor touch_device 23");
        }
        const auto released=RunPoll(mapper,GameTitle::Halo2,true,0,automatic,layout);
        Check(released.routed.buttons==0&&released.routed.published==0,"H2 direct: releasing X releases the real button");
        auto p=RunPoll(mapper,GameTitle::Halo2,true,Bit(A),automatic,layout);
        Check(p.routed.buttons==0&&p.routed.published==(1u<<Jump),"H2 direct: Jump stays on the bridge, real A withheld");
        RunPoll(mapper,GameTitle::Halo2,true,0,automatic,layout);
        p=RunPoll(mapper,GameTitle::Halo2,true,Bit(PrimaryTrigger),automatic,layout);
        Check(p.routed.buttons==0&&p.routed.published==(1u<<Fire),"H2 direct: Fire stays on the bridge, real trigger withheld");
        RunPoll(mapper,GameTitle::Halo2,true,0,automatic,layout);
        p=RunPoll(mapper,GameTitle::Halo2,true,Bit(X)|Bit(Y),automatic,layout);
        Check(p.routed.buttons==padX&&p.routed.published==(1u<<SwitchWeapon),
            "H2 direct: X passes through while Y (switch weapon) stays bridged in the same poll");
    }
    {
        // Transport fallback (no direct route) is byte-identical to before.
        Mapper mapper;
        RunPoll(mapper,GameTitle::Halo2,false,0,automatic,layout);
        const auto p=RunPoll(mapper,GameTitle::Halo2,false,Bit(X),automatic,layout);
        Check(p.routed.buttons==p.mapped.transports&&p.routed.buttons==padX&&p.routed.published==p.mapped.actions,
            "H2 transport fallback unchanged");
    }
    {
        // Use device bound in MCC to a different button than Reload: each VR
        // action sends its own real button, so the game's context actions on
        // that button still run. Neither reaches the bridge.
        Transports split=layout;split[Interact]=padB;
        Overrides bindings=automatic;bindings[Interact]=Y;
        Mapper mapper;
        RunPoll(mapper,GameTitle::Halo2,true,0,bindings,split);
        auto p=RunPoll(mapper,GameTitle::Halo2,true,Bit(Y),bindings,split);
        Check(p.routed.buttons==padB&&!(p.routed.published&context),"H2 direct: Use sends its own real MCC button");
        RunPoll(mapper,GameTitle::Halo2,true,0,bindings,split);
        p=RunPoll(mapper,GameTitle::Halo2,true,Bit(X),bindings,split);
        Check(p.routed.buttons==padX&&!(p.routed.published&context),"H2 direct: Reload sends only the real reload button");
        // A context action with NO native binding has no button to send; the
        // bridge remains its only route, exactly as before.
        Transports unbound=layout;unbound[Reload]=0;
        RunPoll(mapper,GameTitle::Halo2,true,0,automatic,unbound);
        p=RunPoll(mapper,GameTitle::Halo2,true,Bit(X),automatic,unbound);
        Check(p.routed.buttons==padX&&(p.routed.published&context)==(1u<<Reload),
            "H2 direct: natively unbound Reload stays on the bridge");
        // Binding reader unavailable: nothing to pass through, bridge unchanged.
        RunPoll(mapper,GameTitle::Halo2,true,0,automatic,Transports{});
        p=RunPoll(mapper,GameTitle::Halo2,true,Bit(X),automatic,Transports{});
        Check(p.routed.buttons==0&&(p.routed.published&context)==context,
            "H2 direct with no readable layout keeps the bridge route");
    }
    for(const auto title:{GameTitle::Halo3,GameTitle::Halo3ODST,GameTitle::HaloReach,GameTitle::Halo4,GameTitle::HaloCE})
    {
        Check(DirectPassthroughActions(title)==0,"only Halo 2 passes context actions through");
        Mapper direct,fallback;
        RunPoll(direct,title,true,0,automatic,layout);RunPoll(fallback,title,false,0,automatic,layout);
        for(unsigned poll=0;poll<40;++poll)
        {
            const auto d=RunPoll(direct,title,true,Bit(X),automatic,layout);
            Check(d.routed.buttons==0&&(d.routed.published&context)==context,
                "other titles direct: X stays withheld and the bridge publishes Reload and Use");
            const auto f=RunPoll(fallback,title,false,Bit(X),automatic,layout);
            Check(f.routed.buttons==f.mapped.transports&&f.routed.published==f.mapped.actions,
                "other titles transport fallback unchanged");
        }
    }
    Check(std::strcmp(ActionName(GameTitle::Halo2,Interact),kNames[Interact])!=0,
        "Halo 2 index 5 is labelled as touch_device, not pick up");
    for(const auto title:{GameTitle::Halo3,GameTitle::Halo3ODST,GameTitle::HaloReach,GameTitle::Halo4,GameTitle::HaloCE,GameTitle::Halo2})
        for(unsigned a=0;a<Count;++a)
            if(!(title==GameTitle::Halo2&&a==Interact))
                Check(std::strcmp(ActionName(title,static_cast<Action>(a)),kNames[a])==0,"other labels unchanged");
}
int main()
{
    CheckInputPresets();
    CheckDirectContextPassthrough();
    CheckScopeAdmission();
    CheckPhysicalCrouchInput();
    {
        // Real collisions: CE uses distinct action identities on the same
        // default pad button; H3/ODST/H4 also alias the native action itself.
        // The pre-transport publication must preserve both semantic choices.
        Overrides overrides{}; overrides.fill(Unbound);
        Transports aliases{}; aliases[Reload]=aliases[Interact]=0x4000;
        Mapper isolated;
        overrides[Reload]=X;
        isolated.ApplyDetailed(0,overrides,aliases,1,true);
        auto mapped=isolated.ApplyDetailed(Bit(X),overrides,aliases,1,true);
        Check(mapped.Has(Reload)&&!mapped.Has(Interact)&&mapped.transports==0x4000,
            "semantic reload remains distinct from unbound Use despite a shared native transport");
        overrides[Reload]=Unbound;overrides[Interact]=X;
        Check(isolated.ApplyDetailed(Bit(X),overrides,aliases,1,true).actions==0,
            "held alias remap cannot become a different semantic action");
        isolated.ApplyDetailed(0,overrides,aliases,1,true);
        mapped=isolated.ApplyDetailed(Bit(X),overrides,aliases,1,true);
        Check(!mapped.Has(Reload)&&mapped.Has(Interact)&&mapped.transports==0x4000,
            "semantic Use remains distinct from unbound reload despite a shared native transport");
        overrides[Reload]=X;
        isolated.ApplyDetailed(0,overrides,aliases,1,true);
        mapped=isolated.ApplyDetailed(Bit(X),overrides,aliases,1,true);
        Check(mapped.Has(Reload)&&mapped.Has(Interact),
            "intentionally shared VR source admits both independently named actions");
        aliases[Fire]=0;overrides[Fire]=PrimaryTrigger;
        isolated.ApplyDetailed(0,overrides,aliases,1,true);
        mapped=isolated.ApplyDetailed(Bit(PrimaryTrigger),overrides,aliases,1,true);
        Check(mapped.Has(Fire)&&mapped.transports==0,
            "native-unbound action still carries semantic identity for a proven direct consumer");
    }
    Transports native{}; Overrides bindings{};
    for(unsigned a=0;a<Count;++a) native[a]=1u<<a;
    for(unsigned a=0;a<Count;++a) for(int source=Unbound;source<SourceCount;++source)
    {
        Mapper mapper;bindings.fill(Unbound);bindings[a]=source;
        mapper.Apply(0,bindings,native,1,true);
        const auto value=mapper.Apply(Bit(source),bindings,native,1,true);
        Check(value==(source==Unbound?0:native[a]),"each action can be independently rebound or unbound");
    }
    bindings.fill(Unbound);bindings[Fire]=PrimaryTrigger;
    Mapper mapper;
    Check(mapper.Apply(Bit(PrimaryTrigger),bindings,native,1,true)==0,"held trigger at admission is blocked");
    mapper.Apply(0,bindings,native,1,true);
    Check(mapper.Apply(Bit(PrimaryTrigger),bindings,native,1,true)==native[Fire],"release admits deliberate fire");
    Check(mapper.Apply(Bit(PrimaryTrigger),bindings,native,2,true)==0,"profile/title generation change cancels hold");
    mapper.Apply(0,bindings,native,2,true);
    mapper.Apply(Bit(PrimaryTrigger),bindings,native,2,false);
    Check(mapper.Apply(Bit(PrimaryTrigger),bindings,native,2,true)==0,"pause/unavailable recovery requires release");
    mapper.Apply(0,bindings,native,2,true);
    bindings[Fire]=Unbound;bindings[Melee]=PrimaryTrigger;
    Check(mapper.Apply(Bit(PrimaryTrigger),bindings,native,2,true)==0,"changing mapping cannot turn held fire into melee");
    mapper.Apply(0,bindings,native,2,true);
    Check(mapper.Apply(Bit(PrimaryTrigger),bindings,native,2,true)==native[Melee],"new deliberate press uses changed action");
    bindings.fill(Unbound);bindings[Flashlight]=SupportGrip;bindings[Equipment]=SupportGrip;
    mapper.Apply(0,bindings,native,2,true,true);
    Check(mapper.Apply(Bit(SupportGrip),bindings,native,2,true,true)==native[Equipment],"flashlight suppression preserves another grip action");
    Check(mapper.Apply(Bit(SupportGrip),bindings,native,2,true,false)==native[Equipment],"re-enabling flashlight cannot expose a consumed held grip");
    mapper.Apply(0,bindings,native,2,true,false);
    Check(mapper.Apply(Bit(SupportGrip),bindings,native,2,true,false)==(native[Equipment]|native[Flashlight]),"fresh press restores flashlight without changing other actions");
    Check(Resolve(Fire,Unbound)==Unbound&&Resolve(Fire,-1)==Unbound&&Resolve(Fire,999)==Unbound,"invalid bindings never fall through to automatic");
    Check(DetectProfile("/interaction_profiles/oculus/touch_controller")==Profile::Touch,"Touch detection");
    Check(DetectProfile("/interaction_profiles/valve/index_controller")==Profile::Index,"Index detection");
    Check(DetectProfile("/interaction_profiles/htc/vive_controller")==Profile::Vive,"Vive detection");
    Check(DetectProfile("/interaction_profiles/microsoft/motion_controller")==Profile::Wmr,"WMR detection");
    Check(DetectProfile("/interaction_profiles/vendor/not_known")==Profile::Unknown,"unknown profile stays explicitly unknown");
    PadFaces faces;
    Check(faces.Update(.9f,false)==0,"touch alone cannot press a face button");
    Check(faces.Update(.9f,true)==1&&faces.Update(-.9f,true)==1,"pressed pad zone remains latched while thumb moves");
    faces.Update(0,false);
    Check(faces.Update(-.9f,true)==-1,"release permits the other face zone");
    faces.Update(0,false);
    Check(faces.Update(0,true)==0,"center click remains a stick click");
    struct Pad {float trigR=0,trigL=0,gripR=0,gripL=0;bool a=false,b=false,x=false,y=false,clickL=false,clickR=false;} pad;
    pad.trigR=std::numeric_limits<float>::quiet_NaN();pad.gripL=std::numeric_limits<float>::infinity();
    Check(Sources(pad)==0,"nonfinite input never presses a button");
    pad.trigR=1;pad.gripL=0;pad.a=true;
    Check(Sources(pad)==(Bit(PrimaryTrigger)|Bit(A)),"physical sources remain independent of transport");

    const auto path=std::filesystem::temp_directory_path()/(L"mccvr-vr-mappings-"+std::to_wstring(GetCurrentProcessId())+L".cfg");
    {std::ofstream file(path);file<<"vr_bind_halo3_fire = 1\nvr_bind_reach_fire = 4\nvr_bind_halo2_reload = 999\nvr_bind_ce_melee = junk\nupscaler = 1\ndlss_mode = 2\n";}
    ConfigLoad(path.c_str());
    Check(g_config.vr_bindings[0][Fire]==Unbound&&g_config.vr_bindings[2][Fire]==PrimaryGrip,"per-game overrides load independently");
    Check(g_config.vr_bindings[5][Reload]==Automatic&&g_config.vr_bindings[4][Melee]==Automatic,"invalid file values retain defaults");
    Check(!g_config.disable_autoexposure_odst,"ODST exposure is native by default");
    {std::ofstream file(path);file<<"bloom_enabled_odst = 1\nbloom_enabled_reach = 1\n";}
    ConfigLoad(path.c_str());
    Check(g_config.bloom_enabled[1]&&g_config.bloom_enabled[2]&&!g_config.bloom_chosen[1]&&!g_config.bloom_chosen[2],
        "a saved legacy bloom setting is kept as it was");
    Check(!Config_BloomWanted(1)&&!Config_BloomWanted(2)&&Config_BloomWanted(0),
        "ODST and Reach play without bloom in VR until the player chooses");
    {std::ifstream file(path);const std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        Check(text=="bloom_enabled_odst = 1\nbloom_enabled_reach = 1\n","loading leaves the bloom file untouched");}
    g_config.bloom_chosen[1]=g_config.bloom_chosen[2]=true;
    ConfigSave();ConfigLoad(path.c_str());
    Check(Config_BloomWanted(1)&&Config_BloomWanted(2),"a chosen bloom survives reload");
    for(unsigned t=0;t<6;++t) g_config.bloom_chosen[t]=false;
    const bool defaultFlashes[6]{false,false,false,true,false,true};
    for(unsigned t=0;t<6;++t) Check(g_config.hide_muzzle_flash[t]==defaultFlashes[t],"muzzle-flash suppression defaults stay title-specific");
    for(unsigned t=0;t<6;++t) Check(Config{}.bloom_enabled[t]&&!Config{}.bloom_chosen[t],"saved bloom defaults to on and unchosen");
    {std::ofstream file(path);
        file<<"virtual_stock = 1\nvirtual_stock_rear_reference = 3\nvirtual_stock_strength = nan\n"
            "virtual_stock_shoulder_side_m = inf\nvirtual_stock_chest_back_m = garbage\n"
            "virtual_stock_rear_height_m = -999\nvirtual_stock_adaptive_top_half_width_m = 999\n"
            "virtual_stock_proximity_full_m = .6\nvirtual_stock_proximity_release_m = .2\n"
            "virtual_stock_adaptive_top_height_m = -.35\nvirtual_stock_adaptive_bottom_height_m = -.2\n"
            "weapon_pouch_location = 99\nweapon_pouch_offset_x_m = -999\nweapon_pouch_offset_y_m = 999\nweapon_pouch_offset_z_m = nan\n"
            "hide_muzzle_flash_halo3 = 1\nhide_muzzle_flash_odst = 7\nhide_muzzle_flash_halo4 = 0\nhide_muzzle_flash_halo2 = junk\n"
            "bloom_enabled_halo3 = 0\nbloom_enabled_odst = junk\nbloom_enabled_reach = 7\n"
            "disable_autoexposure_odst = 1\ndisable_autoexposure_odst = junk\ndisable_autoexposure_odst = 7\n";}
    ConfigLoad(path.c_str());
    Check(g_config.disable_autoexposure_odst,"exposure accepts one and rejects malformed boolean overrides");
    const Config defaults{};
    Check(g_config.virtual_stock&&g_config.virtual_stock_rear_reference==3,"stock activation and mode load independently of invalid geometry");
    Check(g_config.virtual_stock_standard_strength==defaults.virtual_stock_standard_strength&&
        g_config.virtual_stock_plus_strength==defaults.virtual_stock_plus_strength&&
        g_config.virtual_stock_shoulder_side_m==defaults.virtual_stock_shoulder_side_m&&g_config.virtual_stock_chest_back_m==0,
        "malformed and nonfinite stock inputs preserve safe defaults");
    Check(g_config.virtual_stock_rear_height_m==-.3f&&g_config.virtual_stock_adaptive_top_half_width_m==.25f,
        "stock geometry clamps finite out-of-range values");
    Check(g_config.virtual_stock_proximity_full_m==.27f&&g_config.virtual_stock_proximity_release_m==.425f,
        "inverted proximity pairs restore valid geometry after parsing");
    Check(g_config.virtual_stock_adaptive_top_height_m==-.35f&&g_config.virtual_stock_adaptive_bottom_height_m==-.2f,
        "dormant Adaptive geometry keeps its own finite clamps without a product pair rule");
    Check(g_config.hide_muzzle_flash[0]&&!g_config.hide_muzzle_flash[1]&&!g_config.hide_muzzle_flash[3]&&g_config.hide_muzzle_flash[5],
        "per-game muzzle-flash values parse strictly and independently");
    Check(!g_config.bloom_enabled[0]&&g_config.bloom_enabled[1]&&g_config.bloom_enabled[2],
        "bloom parses independently with strict boolean values (malformed keeps the default)");
    Check(g_config.weapon_pouch_location==0&&g_config.weapon_pouch_offset_x_m==-.4f&&
        g_config.weapon_pouch_offset_y_m==.4f&&g_config.weapon_pouch_offset_z_m==0,
        "pouch location and offsets reject invalid or nonfinite values and clamp finite geometry");
    struct StockSetting {const char* key;float Config::*member;float value;};
    const StockSetting stockSettings[]{
        {"virtual_stock_standard_strength",&Config::virtual_stock_standard_strength,.75f},
        {"virtual_stock_plus_strength",&Config::virtual_stock_plus_strength,.85f},
        {"virtual_stock_rear_height_m",&Config::virtual_stock_rear_height_m,-.17f},
        {"virtual_stock_shoulder_back_m",&Config::virtual_stock_shoulder_back_m,.1f},
        {"virtual_stock_shoulder_side_m",&Config::virtual_stock_shoulder_side_m,.12f},
        {"virtual_stock_chest_height_m",&Config::virtual_stock_chest_height_m,-.4f},
        {"virtual_stock_chest_back_m",&Config::virtual_stock_chest_back_m,.11f},
        {"virtual_stock_chest_side_m",&Config::virtual_stock_chest_side_m,.09f},
        {"virtual_stock_adaptive_top_height_m",&Config::virtual_stock_adaptive_top_height_m,-.1f},
        {"virtual_stock_adaptive_bottom_height_m",&Config::virtual_stock_adaptive_bottom_height_m,-.5f},
        {"virtual_stock_adaptive_top_half_width_m",&Config::virtual_stock_adaptive_top_half_width_m,.1f},
        {"virtual_stock_adaptive_bottom_half_width_m",&Config::virtual_stock_adaptive_bottom_half_width_m,.2f},
        {"virtual_stock_proximity_full_m",&Config::virtual_stock_proximity_full_m,.3f},
        {"virtual_stock_proximity_release_m",&Config::virtual_stock_proximity_release_m,.6f}};
    {std::ofstream file(path);file<<"virtual_stock = 1\nvirtual_stock_rear_reference = 2\nvirtual_stock_proximity_release = 0\nupscaler = 1\ndlss_mode = 2\n";
        file<<"weapon_pouch_location = 1\nweapon_pouch_offset_x_m = -.12\nweapon_pouch_offset_y_m = .23\nweapon_pouch_offset_z_m = -.34\n";
        for(const auto& field:stockSettings) file<<field.key<<" = "<<field.value<<'\n';
        for(unsigned t=0;t<6;++t) file<<"hide_muzzle_flash_"<<weapon_interaction::kTitleKeys[t]<<" = "<<(t%2?0:1)<<'\n';}
    ConfigLoad(path.c_str());
    for(const auto& field:stockSettings) Check(std::fabs(g_config.*field.member-field.value)<.00001f,"each stock field loads from its public config key");
    for(unsigned t=0;t<6;++t) for(unsigned a=0;a<Count;++a) g_config.vr_bindings[t][a]=static_cast<int>((t+a)%SourceCount);
    for(unsigned t=0;t<6;++t) {g_config.bloom_enabled[t]=(t%2)==0;g_config.bloom_chosen[t]=(t%3)==0;}
    g_config.disable_autoexposure_odst=true;
    const auto expected=g_config;
    ConfigSave();ConfigLoad(path.c_str());
    Check(g_config.disable_autoexposure_odst==expected.disable_autoexposure_odst,"exposure survives save and reload");
    for(unsigned t=0;t<6;++t) Check(g_config.vr_bindings[t]==expected.vr_bindings[t],"all six per-game mappings survive round trip");
    for(unsigned t=0;t<6;++t) Check(g_config.hide_muzzle_flash[t]==expected.hide_muzzle_flash[t],"all six muzzle-flash settings survive round trip");
    for(unsigned t=0;t<6;++t) Check(g_config.bloom_enabled[t]==expected.bloom_enabled[t],"all six bloom settings survive round trip");
    for(unsigned t=0;t<6;++t) Check(g_config.bloom_chosen[t]==expected.bloom_chosen[t],"all six bloom choices survive round trip");
    for(const auto& field:stockSettings) Check(std::fabs(g_config.*field.member-expected.*field.member)<.00001f,"each stock geometry field survives config save and reload");
    Check(g_config.virtual_stock&&g_config.virtual_stock_rear_reference==0&&!g_config.virtual_stock_proximity_release,
        "stock switches survive config save and reload (Chest is no longer a product selection)");
    Check(g_config.weapon_pouch_location==1&&std::fabs(g_config.weapon_pouch_offset_x_m+.12f)<.00001f&&
        std::fabs(g_config.weapon_pouch_offset_y_m-.23f)<.00001f&&std::fabs(g_config.weapon_pouch_offset_z_m+.34f)<.00001f,
        "shoulder pouch and all three custom offsets survive config save and reload");
    Check(g_config.upscaler==1&&g_config.dlss_mode==2,"optional rendering preferences survive config save");
    {std::ofstream file(path);file<<"vehicle_cam_forward_m = .27\nvehicle_cam_up_m = .19\nvehicle_cam_right_m = -.13\n"
        "vehicle_game_halo2_forward_m = nan\nvehicle_game_halo4_up_m = 999\nvehicle_game_ce_right_m = junk\n";}
    ConfigLoad(path.c_str());
    for(unsigned title=0;title<6;++title)
    {
        const auto game=static_cast<GameTitle>(title+1);
        Check(ConfigGameVehicleCam(g_config,game,0)==.27f&&ConfigGameVehicleCam(g_config,game,2)==-.13f,
            "legacy vehicle trims migrate by inheritance without changing any title");
        Check(ConfigGameVehicleCam(g_config,game,1)==(title==3?kVehicleCamUpMax:.19f),
            "only finite per-game override clamps; malformed values retain legacy fallback");
        for(unsigned axis=0;axis<3;++axis)
        {g_config.vehicle_cam_game[title][axis]=.01f*float(1+title*3+axis);g_config.vehicle_cam_game_set[title][axis]=true;}
    }
    // Use an unset authored slot to prove production fallback routing, then
    // restore its explicit seat value to prove the two scopes stay separate.
    g_config.vehicle_cam_forward_set[0]=false;g_config.odst_vehicle_cam_up_set[0]=false;
    Check(std::fabs(ConfigSeatCamForward(g_config,0)-.01f)<.00001f&&std::fabs(ConfigOdstSeatCamUp(g_config,0)-.05f)<.00001f&&
        std::fabs(ConfigReachSeatCamRight(g_config,-1)-.09f)<.00001f,"existing seat banks use their own game's fallback");
    g_config.vehicle_cam_forward_v[0]=.43f;g_config.vehicle_cam_forward_set[0]=true;
    Check(ConfigSeatCamForward(g_config,0)==.43f,"explicit per-seat trim takes priority over game fallback");
    ConfigSave();ConfigLoad(path.c_str());
    for(unsigned title=0;title<6;++title)for(unsigned axis=0;axis<3;++axis)
        Check(g_config.vehicle_cam_game_set[title][axis]&&
            std::fabs(ConfigGameVehicleCam(g_config,static_cast<GameTitle>(title+1),axis)-.01f*float(1+title*3+axis))<.00001f,
            "all eighteen per-game seat offsets persist independently");
    Check(ConfigSeatCamForward(g_config,0)==.43f,"per-seat identity and override survive alongside per-game defaults");
    {std::ofstream file(path);file<<"vehicle_game_halo2_forward_m = .12\n"
        "vehicle_model_halo2_123456789abcdef0_seat0_forward_m = .44\n"
        "vehicle_model_halo2_123456789abcdef0_seat1_forward_m = -.2\n"
        "vehicle_model_ce_123456789abcdef0_seat0_forward_m = .31\n"
        "vehicle_model_halo2_223456789abcdef0_seat0_up_m = 999\n"
        "vehicle_model_halo2_0000000000000000_seat0_up_m = .8\n"
        "vehicle_model_halo2_323456789abcdef0_seat32_up_m = .8\n"
        "vehicle_model_halo2_423456789abcdef0_seat0_up_m = nan\n";}
    ConfigLoad(path.c_str());
    Check(ConfigVehicleModelCam(g_config,GameTitle::Halo2,0x123456789abcdef0ull,0,0)==.44f&&
        ConfigVehicleModelCam(g_config,GameTitle::Halo2,0x123456789abcdef0ull,1,0)==-.2f&&
        ConfigVehicleModelCam(g_config,GameTitle::HaloCE,0x123456789abcdef0ull,0,0)==.31f,
        "stable model identity persists independent title and seat scopes");
    Check(ConfigVehicleModelCam(g_config,GameTitle::Halo2,0x223456789abcdef0ull,0,1)==kVehicleCamUpMax&&
        ConfigVehicleModelCam(g_config,GameTitle::Halo2,0x123456789abcdef0ull,2,0)==.12f,
        "model trim clamps while another seat inherits per-game fallback");
    Check(ConfigVehicleModelTrimSlot(g_config,GameTitle::Halo2,0,0)<0&&
        ConfigVehicleModelTrimSlot(g_config,GameTitle::Halo2,0x323456789abcdef0ull,32)<0&&
        ConfigVehicleModelTrimSlot(g_config,GameTitle::Halo2,0x423456789abcdef0ull,0)<0,
        "invalid identity, out-of-range seat and nonfinite value do not create saved profiles");
    ConfigSave();ConfigLoad(path.c_str());
    Check(ConfigVehicleModelCam(g_config,GameTitle::Halo2,0x123456789abcdef0ull,0,0)==.44f&&
        ConfigVehicleModelCam(g_config,GameTitle::Halo2,0x123456789abcdef0ull,1,0)==-.2f&&
        ConfigVehicleModelCam(g_config,GameTitle::HaloCE,0x123456789abcdef0ull,0,0)==.31f,
        "identified vehicle presets survive save/reload without transient instance IDs");
    // A seat with a built-in offset: placed with no config line, moved by a line one axis at a time, and handed
    // back to the built-in offset (not to the game's base) by its reset.
    {
        const uint64_t mantis=0x000000551b05181eull;
        const Config fresh{};
        Check(ConfigVehicleModelCam(fresh,GameTitle::Halo4,mantis,0,0)==.30f&&
            ConfigVehicleModelCam(fresh,GameTitle::Halo4,mantis,0,1)==.80f&&
            ConfigVehicleModelCam(fresh,GameTitle::Halo4,mantis,0,2)==0.f,
            "the Mantis pilot's seat camera ships above the cockpit");
        Check(ConfigVehicleModelCam(fresh,GameTitle::Halo4,mantis,1,1)==ConfigGameVehicleCam(fresh,GameTitle::Halo4,1)&&
            ConfigVehicleModelCam(fresh,GameTitle::Halo4,mantis+1,0,1)==ConfigGameVehicleCam(fresh,GameTitle::Halo4,1)&&
            ConfigVehicleModelCam(fresh,GameTitle::Halo3,mantis,0,1)==ConfigGameVehicleCam(fresh,GameTitle::Halo3,1),
            "a built-in seat offset reaches only its own game, model and seat");
        for(const ConfigShippedVehicleModelTrim& trim:kConfigShippedVehicleModelTrims)
            Check(trim.identity&&trim.seat>=0&&trim.seat<=31&&trim.name&&*trim.name&&
                trim.forward>=kVehicleCamForwardMin&&trim.forward<=kVehicleCamForwardMax&&
                trim.up>=kVehicleCamUpMin&&trim.up<=kVehicleCamUpMax&&
                trim.right>=kVehicleCamRightMin&&trim.right<=kVehicleCamRightMax&&
                ConfigShippedVehicleModelTrimFor(trim.title,trim.identity,trim.seat)==&trim,
                "every built-in seat offset is one its sliders can reach, and is named once");
        {std::ofstream file(path);file<<"vehicle_model_halo4_000000551b05181e_seat0_up_m = 1.1\n";}
        ConfigLoad(path.c_str());
        Check(ConfigVehicleModelCam(g_config,GameTitle::Halo4,mantis,0,1)==1.1f&&
            ConfigVehicleModelCam(g_config,GameTitle::Halo4,mantis,0,0)==.30f,
            "a config line moves one axis of a built-in seat offset and leaves the others");
        ConfigSave();ConfigLoad(path.c_str());
        Check(ConfigVehicleModelCam(g_config,GameTitle::Halo4,mantis,0,1)==1.1f&&
            ConfigVehicleModelCam(g_config,GameTitle::Halo4,mantis,0,0)==.30f,
            "a moved built-in seat offset survives save and reload");
        const int slot=ConfigVehicleModelTrimSlot(g_config,GameTitle::Halo4,mantis,0);
        ConfigResetVehicleModelTrim(g_config,slot);
        Check(slot>=0&&ConfigVehicleModelCam(g_config,GameTitle::Halo4,mantis,0,1)==.80f&&
            ConfigVehicleModelCam(g_config,GameTitle::Halo4,mantis,0,0)==.30f,
            "resetting a seat with a built-in offset returns to that offset");
        const int other=ConfigEnsureVehicleModelTrim(g_config,GameTitle::Halo4,mantis+1,0);
        g_config.vehicle_model_trims[other].value[1]=.5f;g_config.vehicle_model_trims[other].set[1]=true;
        ConfigResetVehicleModelTrim(g_config,other);
        Check(other>=0&&ConfigVehicleModelTrimSlot(g_config,GameTitle::Halo4,mantis+1,0)<0,
            "resetting a seat with no built-in offset hands it back to its game's base");
        {std::ofstream file(path);}
        ConfigLoad(path.c_str());
        Check(ConfigVehicleModelCam(g_config,GameTitle::Halo4,mantis,0,1)==.80f,
            "a config file with no line for the seat keeps its built-in offset");
    }
    Check(vehicle_identity::Path("vehicles\\warthog\\warthog")==vehicle_identity::Path("VEHICLES/WARTHOG/WARTHOG")&&
        vehicle_identity::Path("vehicles\\warthog\\warthog")!=vehicle_identity::Path("vehicles\\ghost\\ghost"),
        "authored path identity normalizes cache spelling and keeps different vehicles separate");
    const char noTerminator[]{'v','e','h','i'};
    Check(!vehicle_identity::Path(nullptr)&&!vehicle_identity::Path("")&&!vehicle_identity::Path(noTerminator,4)&&
        !vehicle_identity::Path("vehicle\ninvalid"),"malformed or unbounded native tag names cannot create profiles");
    std::filesystem::remove(path);
    std::printf("VR mappings: %u checks passed\n",checks);
}
