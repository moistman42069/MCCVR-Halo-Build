#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/common/authored_grab_zone.h"
#include <cassert>
#include <limits>
int main(){
    using namespace authored_grab;
    const Key pistol{1,2,3,4},rifle{1,2,3,5};
    const Point rear{-.025f,0,.01f},front{0,0,-.35f};
    assert(Hit(rear,{0,0,0}));assert(!Hit(front,{0,0,0}));assert(Hit(front,{.02f,0,-.34f}));
    assert(!Hit(rear,{0,0,-.6f}));assert(!Hit(rear,{std::numeric_limits<float>::quiet_NaN(),0,0}));
    Learner learner;assert(!learner.Observe(pistol,rear,100));
    for(uint64_t t=200;t<600;t+=100)assert(!learner.Observe(pistol,rear,t));
    assert(learner.Observe(pistol,rear,600));
    for(uint64_t t=700;t<=1600;t+=100)assert(learner.Observe(pistol,rear,t));
    assert(!learner.Observe(pistol,front,1700)); // reload movement revokes readiness
    assert(!learner.Observe(pistol,rear,1800));
    for(uint64_t t=1900;t<2300;t+=100)assert(!learner.Observe(pistol,rear,t));
    assert(learner.Observe(pistol,rear,2300)); // never freezes the reload pose
    assert(!learner.Observe(rifle,front,2400));
    for(uint64_t t=2500;t<2900;t+=100)assert(!learner.Observe(rifle,front,t));
    assert(learner.Observe(rifle,front,2900)); // replacement also waits exactly 500ms
    assert(!learner.Observe(rifle,front,3200)); // tracking gap restarts learning
    for(uint64_t t=3300;t<3700;t+=100)assert(!learner.Observe(rifle,front,t));
    assert(learner.Observe(rifle,front,3700));
    // Live acquisition has no settling timer, including replacement and motion.
    for(uint32_t title=1;title<=6;++title) {
        Publication live;Point current{};
        const Key first{title,2,3,4},second{title,2,3,5};
        live.Publish(first,rear,100);
        assert(live.Read(first,100,current)&&current==rear);
        live.Publish(second,front,101);
        assert(!live.Read(first,101,current));
        assert(live.Read(second,101,current)&&current==front);
        live.Publish(second,rear,102); // equip/reload motion is not permanently cached
        assert(live.Read(second,102,current)&&current==rear);
        live.Refresh(second,103); // attached hand preserves its current endpoint
        assert(live.Read(second,103,current)&&current==rear);
        live.Publish(second,front,104); // release resumes live geometry immediately
        assert(live.Read(second,104,current)&&current==front);
        live.Publish(second,{std::numeric_limits<float>::quiet_NaN(),0,0},105);
        assert(!live.Read(second,105,current));
        live.Refresh(second,106);
        assert(!live.Read(second,106,current));
        live.Publish(second,front,107);
        assert(live.Read(second,107,current)&&current==front);
        assert(!live.Read(second,208,current));
        live.Publish(second,rear,209);
        assert(live.Read(second,209,current)&&current==rear);
    }
    Publication pub;Point out{};
    pub.Refresh(pistol,1900);
    assert(!pub.Owns(pistol)&&!pub.Read(pistol,1900,out)); // first legacy grip cannot poison acquisition
    pub.Publish(pistol,rear,2000);
    assert(pub.Read(pistol,2050,out));assert(out==rear);
    assert(!pub.Read(rifle,2050,out));assert(!pub.Read(pistol,2101,out));assert(!pub.Read(pistol,1999,out));
    for(int i=0;i<20;++i){assert(!Hit(rear,{0,0,-.6f}));assert(pub.Read(pistol,2050,out));}
    assert(Hit(out,{0,0,0})); // an outside attempt never poisons the next attempt
    assert(pub.Owns(pistol)&&!pub.Owns(rifle));
    pub.Refresh(pistol,2200); // attachment heartbeat preserves learned zone
    assert(pub.Read(pistol,2250,out)&&out==rear);
    pub.Publish(rifle,front,2300,false);
    assert(pub.Owns(rifle)&&!pub.Read(rifle,2300,out)); // pending never inherits old gun
    pub.Publish(rifle,front,2400,true);assert(pub.Read(rifle,2400,out)&&out==front);
    pub.Refresh(pistol,2450); // another owner cannot steal or refresh the current endpoint
    assert(pub.Owns(rifle)&&!pub.Owns(pistol));
    assert(!pub.Read(rifle,2501,out));
    for(uint64_t t=2500;t<=3000;t+=100) {
        pub.Refresh(rifle,t);
        assert(pub.Read(rifle,t,out)&&out==front); // repeated grip/release cycles retain the point
    }
    assert(Hit(front,{0,-.16f,-.35f}));assert(!Hit(front,{0,-.18f,-.35f}));
    assert(!Hit(front,{0,.13f,-.35f})); // upward limit unchanged
    assert(ShortHit({0,-.18f,0}));assert(!ShortHit({0,-.20f,0}));
    assert(!ShortHit({0,.15f,0}));
    assert(Short(rear)&&!Short(front));assert(ShortHit({-.12f,0,0}));
    assert(!ShortHit({0,0,-.3f}));assert(!ShortHit({.3f,0,0}));
    pub.Publish(pistol,rear,0);assert(!pub.Read(pistol,2050,out));
    // Wider acquisition, still shorter than the old forward barrel interval.
    assert(Hit(front,{.10f,0,-.50f}));assert(Hit(front,{0,0,-.57f}));
    assert(!Hit(front,{0,0,-.70f}));assert(!Hit(front,{.13f,0,-.35f}));
    assert(ShortHit({-.20f,.12f,-.14f}));assert(!ShortHit({0,0,-.20f}));
    assert(Hit(front,{0,0,-.579f}));assert(!Hit(front,{0,0,-.581f}));
    assert(Hit(front,{0,0,-.121f}));assert(!Hit(front,{0,0,-.119f}));
    assert(ShortHit({0,0,-.139f}));assert(!ShortHit({0,0,-.141f}));
    assert(ShortHit({0,0,.119f}));assert(!ShortHit({0,0,.121f}));
    assert(CloseHandAuthority({-.22f,0,0})==0);
    assert(CloseHandAuthority({.22f,.05f,-.05f})==0);
    assert(CloseHandAuthority({0,0,-.15f})==0);
    assert(CloseHandAuthority({0,0,-.17f})==1);
    assert(CloseHandAuthority({0,0,-.23f})==1);
    assert(CloseHandAuthority({0,0,-.35f})==1);
    // The range scales the whole volume.
    assert(CloseHandAuthority({0,0,-.15f},.12f)==1);
    assert(CloseHandAuthority({0,0,-.07f},.12f)==0);
    assert(CloseHandAuthority({.11f,0,0},.12f)==0);
    assert(CloseHandAuthority({.13f,0,0},.12f)==1);
    assert(CloseHandAuthority({0,0,-.31f},.48f)==0);
    assert(CloseHandAuthority({0,0,-.33f},.48f)==1);
    // The kind follows the range exactly while not gripping.
    {
        const Key gun{2,1,10,20},other{2,1,10,21};
        uint64_t t=1000;
        GripZoneKindMemory zone;
        assert(zone.Step(gun,{0,0,-.23f},.24f,false,t)==2);
        assert(zone.Step(gun,{0,0,-.26f},.24f,false,t+=50)==1);
        // At rest and gripping, an animation moving the grip point does not change it.
        zone.Step(gun,{0,0,-.26f},.24f,false,t+=kGripZoneRestMs);
        assert(zone.Step(gun,{0,0,-.26f},.24f,true,t+=50)==1);
        assert(zone.Step(gun,{0,0,-.10f},.24f,true,t+=50)==1);
        assert(zone.Step(gun,{0,0,-.60f},.24f,true,t+=50)==1);
        // A change of range applies at once, even gripping, and then holds once at rest.
        assert(zone.Step(gun,{0,0,-.25f},.26f,true,t+=50)==2);
        zone.Step(gun,{0,0,-.25f},.26f,true,t+=kGripZoneRestMs);
        assert(zone.Step(gun,{0,0,-.40f},.26f,true,t+=50)==2);
        // A grip at the range: sensor noise between grips does not change the answer, grip after grip.
        {
            GripZoneKindMemory limit;
            const int first=limit.Step(gun,{0,0,-.2500f},.25f,false,t+=50);
            for(float noise:{-.0030f,.0030f,-.0060f,.0060f,0.f})
                assert(limit.Step(gun,{0,0,-.25f+noise},.25f,false,t+=50)==first);
            // Beyond the hold it does follow; a slider change decides afresh at once.
            assert(limit.Step(gun,{0,0,-.30f},.25f,false,t+=50)==1);
            assert(limit.Step(gun,{0,0,-.245f},.25f,false,t+=50)==1);
            assert(limit.Step(gun,{0,0,-.20f},.25f,false,t+=50)==2);
            assert(limit.Step(gun,{0,0,-.255f},.25f,false,t+=50)==2);
            assert(limit.Step(gun,{0,0,-.255f},.26f,false,t+=50)==2);
            assert(limit.Step(gun,{0,0,-.255f},.25f,false,t+=50)==1);
        }
        // A reading with no usable point keeps the answer.
        assert(zone.Step(gun,{9,9,9},.26f,false,t+=50)==2);
        // A weapon drawn with the grip held: its draw animation does not decide, its rest does.
        assert(zone.Step(other,{0,0,-.37f},.24f,true,t+=50)==1);
        assert(zone.Step(other,{0,0,-.20f},.24f,true,t+=50)==2);
        assert(zone.Step(other,{0,0,-.13f},.24f,true,t+=50)==2);
        assert(zone.Step(other,{0,0,-.13f},.24f,true,t+=kGripZoneRestMs)==2);
        assert(zone.Step(other,{0,0,-.37f},.24f,true,t+=50)==2);
        // Gripping in the middle of an animation: it goes on until the point rests.
        GripZoneKindMemory moving;
        moving.Step(gun,{0,0,-.10f},.24f,false,t+=50);
        moving.Step(gun,{0,0,-.10f},.24f,false,t+=kGripZoneRestMs);
        assert(moving.Step(gun,{0,0,-.40f},.24f,false,t+=50)==1);
        assert(moving.Step(gun,{0,0,-.15f},.24f,true,t+=50)==2);
        // A first reading while already gripping still decides.
        GripZoneKindMemory fresh;
        assert(fresh.Step(gun,{0,0,-.10f},.24f,true,t+=50)==2);
    }
    // A grip point no hand could hold the gun from: held side by side.
    assert(OnGun({-.095f,0,-.231f}));
    assert(OnGun({-.09f,.01f,-.36f}));
    assert(OnGun({-.08f,-.09f,-.06f}));
    assert(OnGun({-.10f,0,-.58f}));
    assert(!OnGun({-.45f,-.64f,.43f}));
    assert(!OnGun({-.15f,-.23f,.10f}));
    assert(!OnGun({-.31f,-.31f,-.12f}));
    // A weapon arriving attached publishes through its draw, then only refreshes.
    {
        const Key first{1,1,10,20},second{1,1,10,21};
        AttachedOwnerWindow window;
        assert(window.Publishes(first,1000));
        assert(window.Publishes(first,1000+kAttachedNewOwnerMs-1));
        assert(!window.Publishes(first,1000+kAttachedNewOwnerMs));
        assert(window.Publishes(second,5000));
        assert(!window.Publishes(second,5000+kAttachedNewOwnerMs));
        assert(window.Publishes(first,9000));
    }
    // By the grip zone: where the hands read does not change the answer.
    assert(GripZoneAuthority(true,{0,0,-.05f})==0);
    assert(GripZoneAuthority(true,{0,0,-.60f})==0);
    assert(GripZoneAuthority(false,{0,0,-kGripZoneFullLineMetres-.001f})==1);
    assert(GripZoneAuthority(false,{.22f,.05f,-.05f})==1);
    assert(GripZoneAuthority(false,{0,0,-.60f})==1);
    assert(GripZoneAuthority(false,{0,0,-kGripZoneNoLineMetres+.001f})==0);
    assert(kGripZoneFullLineMetres==2*kGripZoneNoLineMetres);
    // Between them the aim lets go in proportion: half way is half.
    assert(std::fabs(GripZoneAuthority(false,{0,0,-kGripZoneNoLineMetres*1.5f})-.5f)<.001f);
    assert(std::fabs(GripZoneAuthority(false,{0,0,-kGripZoneNoLineMetres*1.25f})-.25f)<.001f);
    assert(std::fabs(GripZoneAuthority(false,{0,0,-kGripZoneNoLineMetres*1.875f})-.875f)<.001f);
    assert(GripZoneAuthority(true,{0,0,-kGripZoneNoLineMetres*1.5f})==0);
}
