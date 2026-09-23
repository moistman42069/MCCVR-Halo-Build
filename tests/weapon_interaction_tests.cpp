#include "../src/common/weapon_interaction_logic.h"
#include "../src/common/weapon_reload_target.h"
#include <cstdio>
#include <limits>
#include <initializer_list>

using namespace weapon_interaction;
unsigned checks{},failures{};
void Check(bool ok,const char* message)
{ ++checks;if(!ok){++failures;std::printf("FAIL: %s\n",message);} }

struct Rig
{
    State state;
    Settings c;
    Sample s;
    Vec pouch{},holster{};
    Rig(GameTitle title=GameTitle::Halo3,bool left=false,int location=0)
    {
        c.reload=c.holsters=true;c.leftHanded=left;c.holsterLocation=location;
        s.title=title;s.generation=3;s.space=7;s.ready=true;s.now=1000;
        s.head={0,1.65f,0};s.primary={left?-0.22f:0.22f,1.25f,-0.45f};
        s.support={left?0.25f:-0.25f,1.20f,-0.40f};
        Zones(s,c,pouch,holster);state.Update(s,c);
    }
    Output Step(unsigned delta=20) {s.now+=delta;return state.Update(s,c);}
    Output GrabMagazine() {s.support=pouch;s.supportGrip=1;return Step();}
    Output InsertMagazine()
    {
        s.support=s.primary+Rotate(s.primaryRotation,{0,-0.06f,-0.04f});Step(80);Step(80);
        s.supportGrip=0;return Step();
    }
    Output GrabHolster(){s.primary=holster;s.primaryGrip=1;return Step();}
    Output DrawHolster()
    {s.primary={c.leftHanded?-0.2f:0.2f,1.25f,-0.55f};Step(80);return Step(80);}
};

int main()
{
    for(int title=1;title<=6;++title) for(bool left:{false,true}) for(int location:{0,1})
    {
        Rig rig(static_cast<GameTitle>(title),left);
        rig.c.pouchLocation=location;
        Check(Zones(rig.s,rig.c,rig.pouch,rig.holster),"reserve location supports both hands and all titles");
        const Vec original=rig.pouch,originalHolster=rig.holster;
        Check((left?original.x>0:original.x<0)&&(location?original.z>0:original.z<0),
            "reserve starts at the support-side front hip or behind its shoulder");
        rig.c.pouchOffset={.10f,.07f,-.08f};
        Check(Zones(rig.s,rig.c,rig.pouch,rig.holster)&&Near(originalHolster,rig.holster,.0001f),
            "reserve offsets never move the separate weapon holster");
        Check(Near(rig.pouch,original+Vec{left?-.10f:.10f,.07f,.08f},.0001f),
            "reserve XYZ offsets mirror side only, preserving height and forward convention");
        rig.Step();rig.GrabMagazine();
        Check(rig.Step().holdingMagazine,"adjusted reserve location can issue an ordinary magazine grab");
        rig.c.pouchOffset.x+=.02f;
        const auto changed=rig.Step();
        Check(!changed.holdingMagazine&&!changed.reloadRequested&&changed.consumeSupport,
            "moving the reserve cancels an existing grab and drains its held grip");
        rig.c.pouchOffset={std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN(),-999.f};
        Check(Zones(rig.s,rig.c,rig.pouch,rig.holster)&&Finite(rig.pouch),
            "invalid reserve coordinates never escape finite bounded geometry");
    }
    for(const auto& model:weapon_model::kModels) if(model.vertexCount)
        for(bool left:{false,true})
    {
        Rig r(model.title,left);r.s.weaponGraph=model.identity;r.c.insertRadius=.08f;
        r.Step();r.GrabMagazine();
        contact_melee::TrackingToWorld transform{},controller{};
        transform.unitsPerMetre=.328084f;transform.origin={123,45,67};
        transform.axis[0]={0,1,0};transform.axis[1]={0,0,1};transform.axis[2]={1,0,0};
        const float q[]{0,0,0,1},p[]{r.s.primary.x,r.s.primary.y,r.s.primary.z};
        Check(controller.SetPose(q,p),"receiver fixture has valid primary controller");
        const auto native=transform.World({p[0]+.30f,p[1]-.04f,p[2]-.22f});
        ReloadTarget target{};ReloadTargets targets;
        Check(BuildReloadTarget(model.title,r.s.generation,model.identity,r.s.space,r.s.now,
            left,native,transform,controller,target)&&targets.Publish(target),
            "each authored reload model admits a finite native receiver transform");
        Check(targets.Read(r.s,left)&&Near(r.s.receiver,{p[0]+.30f,p[1]-.04f,p[2]-.22f},.0001f),
            "native rotated/scaled world coordinates reconstruct the actual XR receiver");
        const Vec first=r.s.receiver;
        r.s.primary=r.s.primary+Vec{.05f,.03f,-.02f};
        r.s.primaryRotation={0,.70710678f,0,.70710678f};
        Check(targets.Read(r.s,left)&&!Near(first,r.s.receiver,.10f),
            "receiver follows new controller translation and rotation between palette samples");
        r.s.supportRotation={0,0,.70710678f,.70710678f};
        r.s.support=r.s.receiver-Rotate(r.s.supportRotation,{0,-.015f,-.035f});
        r.Step(80);r.Step(80);r.s.supportGrip=0;
        const auto inserted=r.Step();
        Check(inserted.reloadRequested&&inserted.supportHaptic>.20f,
            "releasing the visible magazine centre into its authored receiver inserts with feedback");
        for(int invalid=0;invalid<6;++invalid)
        {
            auto bad=r.s;bad.now=target.at;
            switch(invalid) {
                case 0:++bad.generation;break;
                case 1:++bad.space;break;
                case 2:++bad.weaponGraph;break;
                case 3:bad.now=target.at+101;break;
                case 4:bad.now=target.at-1;break;
                case 5:bad.primaryRotation.w=2;break;
            }
            Check(!targets.Read(bad,left)&&!bad.receiverValid,
                "stale or foreign receiver cannot leak into another gun or tracking epoch");
        }
        auto wrongHand=r.s;wrongHand.now=target.at;
        Check(!targets.Read(wrongHand,!left),"handedness change invalidates previous receiver");
        Rig miss(model.title,left);miss.s.weaponGraph=model.identity;miss.c.insertRadius=.08f;
        miss.Step();miss.GrabMagazine();miss.s.receiverValid=true;
        miss.s.receiver=miss.s.primary+Vec{.4f,0,0};
        const auto dropped=miss.InsertMagazine();
        Check(!dropped.reloadRequested&&!dropped.primaryHaptic&&!dropped.supportHaptic,
            "old generic grip location cannot insert a magazine when a different authored receiver is available");
    }
    for(int title=1;title<=6;++title)for(bool left:{false,true})for(int location:{0,1})
        for(bool slide:{false,true})for(bool click:{false,true})
    {
        Rig r(static_cast<GameTitle>(title),left,location);
        r.c.holsterSlide=slide;r.c.holsterClick=click;r.Step();
        auto o=r.GrabHolster();
        Check(o.swapRequested==click&&o.grabbedHolster==(slide||click),
            "holster checkboxes select click, draw, both or neither in all titles and hands");
        Check(r.DrawHolster().swapRequested==(slide&&!click),"both modes cannot produce a second swap on draw");
        for(int n=0;n<30;++n)Check(!r.Step().swapRequested,"held holster click cannot repeat after cooldown");
        r.s.primaryGrip=0;r.Step();
        Check(r.GrabHolster().swapRequested==click,"new released click rearms at holster");
    }
    for(float radius:{0.08f,0.20f,0.40f})
    {
        Rig r;r.c.holsterClick=true;r.c.holsterRadius=radius;r.c.zoneRadius=0.08f;r.Step();
        r.s.primary=r.holster+Vec{radius-0.001f,0,0};r.s.primaryGrip=1;
        Check(r.Step().swapRequested,"holster accepts just inside its independent radius");
        Rig outside;outside.c.holsterClick=true;outside.c.holsterRadius=radius;outside.Step();
        outside.s.primary=outside.holster+Vec{radius+0.001f,0,0};outside.s.primaryGrip=1;
        Check(!outside.Step().swapRequested,"holster rejects just outside radius");
        outside.s.primary=outside.holster;
        Check(!outside.Step().swapRequested,"moving already-held grip into click zone does not switch");
        Rig pouch;pouch.c.zoneRadius=radius;pouch.c.holsterRadius=0.08f;
        pouch.s.support=pouch.pouch+Vec{radius-0.001f,0,0};pouch.s.supportGrip=1;
        Check(pouch.Step().pickedMagazine,"pouch radius independent of holster radius");
    }
    for(float radius:{0.06f,0.18f,0.30f})for(bool inside:{false,true})
    {
        Rig r;r.c.insertRadius=radius;r.GrabMagazine();
        r.s.support=r.s.primary+Vec{radius+(inside?-0.001f:0.001f),-0.06f,-0.04f};
        r.Step(80);r.Step(80);r.s.supportGrip=0;
        Check(r.Step().reloadRequested==inside,"insertion uses the selected independent radius");
    }
    {
        Rig r;r.GrabHolster();r.c.holsterClick=true;
        Check(!r.Step().swapRequested&&!r.DrawHolster().swapRequested,"changing holster mode cancels held draw");
        Rig h;h.c.holsterClick=true;h.Step();h.s.otherAction=true;
        Check(!h.GrabHolster().swapRequested,"click holster cannot chord with a trigger or button");
        Rig shortDraw;shortDraw.c.holsterRadius=0.08f;shortDraw.c.drawDistance=0.10f;
        shortDraw.GrabHolster();shortDraw.s.primary=shortDraw.holster+Vec{0,0,-0.23f};
        Check(shortDraw.Step(160).swapRequested,"small holster admits deliberately shortened draw");
        Rig longDraw;longDraw.c.drawDistance=0.50f;longDraw.GrabHolster();
        longDraw.s.primary=longDraw.holster+Vec{0,0,-0.40f};
        Check(!longDraw.Step(160).swapRequested,"long draw threshold prevents premature switching");
    }
    // A single out-and-back is sufficient with either grip state, on any axis.
    auto leg=[](Rig& r,Vec offset,unsigned dt=11u) {
        const Vec start=r.s.primary;unsigned requests=0;
        for(int n=1;n<=12;++n) {
            r.s.primary=start+offset*(n/12.0f);const auto o=r.Step(dt);
            requests+=o.reloadRequested;
            Check(!o.consumePrimary&&!o.consumeSupport&&!o.releaseTwoHand,
                "shake never owns grips or changes support aim");
            if(o.reloadRequested)Check(o.buttons==r.c.reloadButton&&o.pulseUntil==r.s.now+120,
                "single shake uses configured native reload pulse");
        }
        return requests;
    };
    auto needle=[](Rig& r) {
        r.c.needleShake=true;r.s.weaponGraph=0x55EA2D6F6C10C375ull;r.Step();r.Step();
    };
    for(bool left:{false,true})for(unsigned dt:{8u,11u,14u})for(float grip:{0.0f,1.0f})
        for(Vec axis:{Vec{1,0,0},Vec{0,1,0},Vec{0,0,1},Vec{-.57735f,.57735f,-.57735f}})
    {
        Rig r(GameTitle::HaloCE,left);r.s.primaryGrip=grip;needle(r);
        Check(leg(r,axis*.14f,dt)==0,"one-way movement alone cannot reload");
        Check(leg(r,axis*-.14f,dt)==1,"one rapid out-and-back reloads without grip in every direction");
        for(int cycle=0;cycle<4;++cycle) {
            Check(leg(r,axis*.14f,dt)==0&&leg(r,axis*-.14f,dt)==0,
                "continued shaking produces no repeated reloads before settling");
        }
        for(int i=0;i<30;++i)Check(!r.Step().reloadRequested,"settling never requests reload");
        Check(leg(r,axis*.14f,dt)==0&&leg(r,axis*-.14f,dt)==1,
            "next single shake rearms after settling without releasing grip");
    }
    for(int reason=0;reason<16;++reason)
    {
        Rig r(GameTitle::HaloCE);needle(r);leg(r,{0,.14f,0});
        switch(reason) {
        case 0:r.c.needleShake=false;break;
        case 1:r.s.weaponGraph=0;break;
        case 2:r.s.title=GameTitle::Halo3;break;
        case 3:r.s.otherAction=true;break;
        case 4:r.s.ready=false;break;
        case 5:r.s.dualWield=true;break;
        case 6:r.s.now+=250;break;
        case 7:++r.s.space;break;
        case 8:++r.s.generation;break;
        case 9:r.c.reload=false;break;
        case 10:r.c.reloadButton=0;break;
        case 11:r.c.leftHanded=true;break;
        case 12:r.state.Cancel();break;
        case 13:r.s.primary.x=std::numeric_limits<float>::quiet_NaN();break;
        case 14:r.c.shakeTravel=.20f;break;
        case 15:r.s.primaryGrip=std::numeric_limits<float>::infinity();break;
        }
        r.Step();
        Check(leg(r,{0,-.14f,0})==0,"invalidation discards previous outward stroke");
    }
    for(int motion=0;motion<6;++motion)
    {
        Rig r(GameTitle::HaloCE);needle(r);
        for(int i=0;i<100;++i) {
            if(motion==0){r.s.primary.y+=.04f;r.s.head.y+=.04f;} // body translation
            if(motion==1)r.s.primary.y+=.02f; // one-way sweep
            if(motion==2)r.s.primary.y+=(i%2?.02f:-.02f); // jitter
            if(motion==3)r.s.primary.y+=(i%2?.15f:-.15f); // tracking jumps
            if(motion==4)r.s.support.y+=(i%2?.15f:-.15f); // wrong hand
            if(motion==5)r.s.primary.y+=(i%40<20?.008f:-.008f); // slow out-and-back
            Check(!r.Step(motion==3?8:100).reloadRequested,
                "translation, sweep, jitter, tracking jumps, support hand and slow movement do not reload");
        }
    }
    for(float travel:{.06f,.10f,.20f})for(float scale:{.8f,1.1f})
    {
        Rig r(GameTitle::HaloCE);r.c.shakeTravel=travel;needle(r);
        leg(r,{0,travel*scale,0},8);
        Check(leg(r,{0,-travel*scale,0},8)==(scale>1?1u:0u),"stroke slider changes required distance");
    }
    for(int binding=0;binding<8;++binding)
    {
        Rig r(GameTitle::HaloCE);r.c.reloadButton=Button(binding);needle(r);
        leg(r,{.14f,0,0});Check(leg(r,{-.14f,0,0})==1,"shake supports every native reload binding");
    }
    for(int title=1;title<=6;++title)
    {
        Rig r(static_cast<GameTitle>(title));r.c.needleShake=true;r.s.weaponGraph=123;r.Step();
        Check(leg(r,{0,.14f,0})==0&&leg(r,{0,-.14f,0})==0,"unknown model never shake-reloads");
        Check(NeedleWeapon(static_cast<GameTitle>(title),0x55EA2D6F6C10C375ull)==(title==5),
            "CE fingerprint cannot classify a different engine's weapon");
    }
    for(bool holster:{false,true})
    {
        Rig r(GameTitle::HaloCE);needle(r);
        if(holster)r.GrabHolster();else r.GrabMagazine();
        const Vec start=r.s.primary;
        for(int i=1;i<=24;++i) {
            r.s.primary=start+Vec{0,.14f*(i<=12?i:24-i)/12,0};
            Check(!r.Step(11).reloadRequested,"pouch and holster transactions take priority over shake");
        }
    }
    for(int title=1;title<=6;++title) for(bool left:{false,true}) for(int location:{0,1})
    {
        Rig r(static_cast<GameTitle>(title),left,location);
        auto o=r.GrabMagazine();
        Check(o.pickedMagazine&&o.consumeSupport&&!o.consumePrimary&&o.releaseTwoHand&&o.supportHaptic>0&&!o.buttons,
            "all titles/hands/locations: pouch grab owns only support grip, frees support aim, gives feedback");
        o=r.InsertMagazine();
        Check(o.reloadRequested&&!o.swapRequested&&o.buttons==0x4000&&o.pulseUntil==r.s.now+120,
            "pouch-to-weapon release produces one reload request with finite lifetime");
        Check(o.primaryHaptic>0&&o.supportHaptic>.20f,"insertion acknowledges both hands with a stronger support pulse than grabbing");
        for(int n=0;n<5;++n) {
            const auto follow=r.Step();
            Check(!follow.reloadRequested&&!follow.primaryHaptic&&!follow.supportHaptic,
                "completed insertion never repeats reload or haptic edges");
        }
        Check(!r.Step(20).buttons,"reload pulse ends at exact deadline");
        r.Step(100);r.Step(100);r.Step(100);r.Step(100);
        o=r.GrabHolster();
        Check(o.grabbedHolster&&o.consumePrimary&&!o.consumeSupport&&o.releaseTwoHand&&!o.buttons,
            "holster acquisition owns only primary grip and does not switch early");
        o=r.DrawHolster();
        Check(o.swapRequested&&o.buttons==0x8000&&o.primaryHaptic>0,"drawing switches once using native input");
        for(int n=0;n<35;++n) Check(!r.Step().swapRequested,"held grip outside holster cannot repeatedly cycle weapons");
        r.s.primaryGrip=0;Check(r.Step().consumePrimary,"release frame still belongs to claimed holster press");
        Check(!r.Step().consumePrimary,"holster ownership ends after release");
    }
    for(int binding=0;binding<8;++binding)
    {
        Rig r;r.c.reloadButton=Button(binding);r.Step();r.GrabMagazine();
        Check(r.InsertMagazine().buttons==Button(binding),"all configured native reload transports preserved");
        Rig h;h.c.swapButton=Button(binding);h.Step();h.GrabHolster();
        Check(h.DrawHolster().buttons==Button(binding),"all configured weapon switch transports preserved");
    }
    Check(!Button(-1)&&!Button(8)&&!Button(1000),"invalid layouts never invent an input");
    for(bool reload:{false,true})for(bool holsters:{false,true})
    {
        Rig r;r.c.reload=reload;r.c.holsters=holsters;r.Step();
        Check(r.GrabMagazine().pickedMagazine==reload,"manual reload independently opt-in");
        Rig h;h.c.reload=reload;h.c.holsters=holsters;h.Step();
        Check(h.GrabHolster().grabbedHolster==holsters,"holsters independently opt-in");
    }
    {
        Rig r;r.s.supportGrip=1;r.Step();r.s.support=r.pouch;
        Check(!r.Step().pickedMagazine,"entering pouch with already held ordinary grip does not claim it");
        r.s.supportGrip=0;r.Step();Check(r.GrabMagazine().pickedMagazine,"fresh grip after release can acquire pouch");
        r.s.supportGrip=0;auto o=r.Step(160);
        Check(!o.buttons&&!o.reloadRequested&&!o.primaryHaptic&&!o.supportHaptic,
            "dropping magazine at pouch cannot reload or acknowledge insertion");
    }
    {
        Rig h;h.GrabHolster();h.s.primaryGrip=0;
        Check(!h.Step(160).buttons,"letting go before draw cancels swap");
        Rig r;r.GrabMagazine();r.s.support={0,0,0};r.s.supportGrip=0;
        const auto drop=r.Step(160);
        Check(!drop.buttons&&!drop.primaryHaptic&&!drop.supportHaptic,
            "release away from weapon discards gesture without native input or insertion haptics");
    }
    // Each unavailable state cancels both partial interactions and blocks the
    // held grip on recovery. These correspond to shipping adapter admission.
    for(int reason=0;reason<14;++reason)for(bool holster:{false,true})
    {
        Rig r;if(holster)r.GrabHolster();else r.GrabMagazine();
        const Sample previous=r.s;const Settings settings=r.c;
        switch(reason)
        {
        case 0:r.s.ready=false;break; // focus, pause, menu, vehicle, death, tracking
        case 1:r.s.dualWield=true;break;
        case 2:++r.s.generation;break;
        case 3:++r.s.space;break;
        case 4:r.s.title=GameTitle::HaloCE;break;
        case 5:r.s.primary.x=std::numeric_limits<float>::quiet_NaN();break;
        case 6:r.s.headRotation.w=0;break;
        case 7:r.s.now+=250;break;
        case 8:r.c.leftHanded=true;break;
        case 9:r.c.reload=r.c.holsters=false;break;
        case 10:r.c.reloadButton=r.c.swapButton=Button(2);break;
        case 11:r.s.primaryGrip=std::numeric_limits<float>::infinity();break;
        case 12:r.s.primaryGrip=2;break;
        case 13:r.s.supportGrip=-1;break;
        }
        auto o=r.Step();Check(!o.buttons&&!o.reloadRequested&&!o.swapRequested,"state invalidation cancels in-flight request");
        const auto now=r.s.now;r.s=previous;r.c=settings;r.s.now=now;r.Step();
        o=holster?r.DrawHolster():r.InsertMagazine();
        Check(!o.buttons&&!o.reloadRequested&&!o.swapRequested,"recovery cannot complete stale gesture");
    }
    {
        Rig r;r.GrabMagazine();r.s.otherAction=true;
        Check(r.Step().holdingMagazine,"incidental trigger/button input cannot detach held magazine");
        r.s.otherAction=false;
        Check(r.InsertMagazine().reloadRequested,"held magazine still inserts after unrelated input releases");
        Rig h;h.GrabHolster();h.s.otherAction=true;
        Check(!h.DrawHolster().buttons,"shooting/button chord cancels holster action");
    }
    {
        Rig r;r.GrabMagazine();for(int i=0;i<42;++i)r.Step(100);
        Check(r.InsertMagazine().reloadRequested,"held magazine remains insertable beyond four seconds");
        Rig h;h.GrabHolster();for(int i=0;i<42;++i)h.Step(100);
        Check(!h.DrawHolster().buttons,"stale holster grab cannot become a delayed switch");
    }
    // Rigid translation/yaw leave both body zones and the physical reload
    // gesture unchanged, including a rolled primary controller.
    for(float yaw:{-2.2f,-0.5f,0.5f,2.8f})
    {
        Rig r;
        const Quat q{0,std::sin(yaw/2),0,std::cos(yaw/2)};
        const Vec offset{3,0.4f,-2};
        r.s.head=Rotate(q,r.s.head)+offset;r.s.primary=Rotate(q,r.s.primary)+offset;
        r.s.support=Rotate(q,r.s.support)+offset;r.s.headRotation=q;r.s.primaryRotation=q;
        Zones(r.s,r.c,r.pouch,r.holster);r.Step();r.GrabMagazine();
        Check(r.InsertMagazine().reloadRequested,"body gestures are tracking-space yaw/translation invariant");
    }
    for(int title=1;title<=6;++title)
    {
        const auto t=static_cast<GameTitle>(title);
        Check(Fresh(1100,1000,t,t,3,3,true),"fresh same-title/generation publication admitted");
        Check(!Fresh(1151,1000,t,t,3,3,true)&&!Fresh(999,1000,t,t,3,3,true),"stale/future sample rejected");
        Check(!Fresh(1100,1000,t,t,3,4,true)&&!Fresh(1100,1000,t,GameTitle::None,3,3,true),"foreign title/generation rejected");
        Check(!Fresh(1100,1000,t,t,3,3,false)&&!Fresh(1100,0,t,t,3,3,true),"unavailable or absent publication rejected");
    }
    for(float pitch:{-1.57079633f,1.57079633f})
    {
        Rig r;r.s.headRotation={std::sin(pitch/2),0,0,std::cos(pitch/2)};
        Check(Zones(r.s,r.c,r.pouch,r.holster),"looking vertically at pouch retains yaw from horizontal head right");
        r.GrabMagazine();Check(r.InsertMagazine().reloadRequested,"looking straight down/up does not lose reload zone");
    }
    {
        Rig r;r.GrabMagazine();r.state.Cancel();
        auto o=r.Step();
        Check(o.consumeSupport&&!o.buttons,"lost XR sync cancels command but retains claimed grip ownership");
        r.c.reload=r.c.holsters=false;o=r.Step();
        Check(o.consumeSupport&&!o.buttons,"disabling options does not turn a held magazine grip into a bumper");
        r.s.supportGrip=0;r.Step();Check(!r.Step().consumeSupport,"cancelled press relinquishes ownership only after release");
    }
    {
        Rig r;r.s.ready=false;r.s.supportGrip=0;r.Step();
        r.s.ready=true;
        Check(!r.GrabMagazine().pickedMagazine,"held grip on restored availability cannot use an earlier unavailable release");
        r.s.supportGrip=0;r.Step();Check(r.GrabMagazine().pickedMagazine,"release after restored availability rearms normally");
        r.s.supportGrip=std::numeric_limits<float>::quiet_NaN();r.s.ready=false;
        Check(r.Step().consumeSupport,"inactive XR action cannot fabricate grip release");
        r.s.ready=true;r.s.supportGrip=1;
        Check(r.Step().consumeSupport&&!r.InsertMagazine().buttons,"inactive-action recovery cannot resurrect magazine transaction");
    }
    {
        Rig r;r.Step();
        Check(BlocksSupportGrab(r.GrabHolster()),"active holster draw owns two-hand release");
        Check(r.DrawHolster().swapRequested,"holster draw completes normally");
        const auto held=r.Step();
        Check(held.consumePrimary&&!BlocksSupportGrab(held),"completed draw can retain primary grip without blocking support hand");
        r.s.primaryGrip=0;r.Step();
        r.Step(200);r.Step(200);r.Step(200); // allow the completed draw's cooldown
        Check(BlocksSupportGrab(r.GrabMagazine()),"held reload magazine still blocks support acquisition");
    }
    std::printf("Weapon interactions: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
