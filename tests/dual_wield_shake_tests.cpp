#include "../src/common/dual_wield_shake_logic.h"
#include <cstdio>
#include <cstdlib>
#include "../src/common/dual_wield_reload_channel.h"
#include <limits>
static unsigned checks=0;
static void Check(bool ok,const char* message) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
struct Rig {
    dual_wield_shake::State state;
    dual_wield_shake::Sample sample;
    Rig(GameTitle title,bool left) {
        sample.title=title;sample.leftHanded=left;sample.enabled=sample.ready=sample.dualWield=true;
        sample.generation=1;sample.space=1;sample.now=1000;sample.weapons={42,43};
        sample.head={0,1.7f,0};sample.hands={weapon_interaction::Vec{.2f,1.2f,0},weapon_interaction::Vec{-.2f,1.2f,0}};
        state.Update(sample);
    }
    unsigned Leg(unsigned mask,float delta,unsigned dt=11) {
        const auto start=sample.hands;unsigned requests=0;
        for(int i=1;i<=12;++i) {
            sample.now+=dt;
            for(unsigned slot=0;slot<2;++slot) if(mask&(1u<<slot)) sample.hands[slot].x=start[slot].x+delta*i/12;
            requests|=state.Update(sample);
        }
        return requests;
    }
};
int main() {
    for(auto title:{GameTitle::Halo2,GameTitle::Halo3})for(bool left:{false,true})
        for(unsigned dt:{8u,11u,14u})for(unsigned mask:{1u,2u,3u}) {
            Rig r(title,left);
            Check(r.Leg(mask,.14f,dt)==0,"outward motion alone does not reload");
            Check(r.Leg(mask,-.14f,dt)==mask,"only shaken native slots request reload");
            Check(r.Leg(mask,.14f,dt)==0&&r.Leg(mask,-.14f,dt)==0,"continuous shaking does not repeat");
            for(int i=0;i<30;++i){r.sample.now+=dt;Check(r.state.Update(r.sample)==0,"settling is quiet");}
            r.Leg(mask,.14f,dt);Check(r.Leg(mask,-.14f,dt)==mask,"settling rearms each hand");
        }
    for(int reason=0;reason<13;++reason) {
        Rig r(GameTitle::Halo3,false);r.Leg(3,.14f);
        switch(reason) {
        case 0:r.sample.enabled=false;break;
        case 1:r.sample.ready=false;break;
        case 2:r.sample.dualWield=false;break;
        case 3:r.sample.title=GameTitle::Halo3ODST;break;
        case 4:++r.sample.generation;break;
        case 5:++r.sample.space;break;
        case 6:r.sample.leftHanded=true;break;
        case 7:++r.sample.weapons[1];break;
        case 8:r.sample.weapons[1]=UINT32_MAX;break;
        case 9:r.sample.now+=300;break;
        case 10:r.sample.busy={true,true};break;
        case 12:++r.sample.unit;break;
        case 11:r.sample.head.x=std::numeric_limits<float>::quiet_NaN();break;
        }
        Check(r.Leg(3,-.14f)==0,"transition cannot finish a stale shake");
    }
    for(unsigned moving:{0u,1u}) {
        Rig r(GameTitle::Halo2,false);r.sample.busy[1-moving]=true;
        r.Leg(1u<<moving,.14f);Check(r.Leg(1u<<moving,-.14f)==(1u<<moving),"other hand can remain busy");
    }
    Rig walking(GameTitle::Halo3,false);
    for(int i=0;i<50;++i) {
        walking.sample.now+=11;walking.sample.head.x+=.02f;
        for(auto& hand:walking.sample.hands)hand.x+=.02f;
        Check(walking.state.Update(walking.sample)==0,"common head/hand translation is not a shake");
    }
    for(auto title:{GameTitle::Halo2,GameTitle::Halo3}) {
        using namespace dual_wield_shake;
        Channel c;Owner a{title,7,42,{43,44},1,false},seen{};
        Check(!c.Read(1000,seen),"no owner before native tick");
        Check(c.ObserveAndConsume(a,1000)==0,"first tick only publishes owner");
        Check(c.Read(1001,seen)&&seen==a,"fresh owner receipt");
        Check(c.Request(a,1002,1)&&c.Request(a,1003,2),"independent requests merge");
        Check(c.ObserveAndConsume(a,1004)==3&&c.ObserveAndConsume(a,1005)==0,"both slots consumed once");
        c.Request(a,1006,3);c.CancelSlots(1);
        Check(c.ObserveAndConsume(a,1007)==2,"busy hand cancels only its request");
        c.Request(a,1008,3);c.Cancel();
        Check(c.ObserveAndConsume(a,1009)==0,"menu cancellation drops requests");
        c.Request(a,1010,3);
        Check(c.ObserveAndConsume(a,1161)==0,"stalled request expires");
        Check(!c.Request(a,1312,1)&&!c.Read(1312,seen),"stalled owner expires");
        for(unsigned field=0;field<6;++field) {
            c.ObserveAndConsume(a,2000);c.Request(a,2001,3);auto b=a;
            switch(field){case 0:++b.unit;break;case 1:++b.generation;break;case 2:++b.space;break;
                case 3:b.leftHanded=true;break;case 4:++b.weapons[1];break;case 5:b.title=GameTitle::Halo3ODST;break;}
            Check(c.ObserveAndConsume(b,2002)==0,"identity transition drops queued requests");
            Check(!c.Request(a,2003,3),"old producer cannot target a new owner");
        }
        c.ObserveAndConsume(a,3000);c.Request(a,3001,1);c.Request(a,3149,2);
        Check(c.ObserveAndConsume(a,3152)==2,"new request does not extend other hand expiry");
        c.Request(a,3153,3);Check(c.ObserveAndConsume(a,3100)==0,"clock regression rejects requests");
    }
    // Reproduce Windows' coarse tick clock at a faster-than-clock VR rate.
    // Duplicate wall-clock samples are now ignored rather than reseeding the
    // shared stroke detector, so both coarse and pose-clock samples can finish.
    for(unsigned dt:{8u,11u,14u,22u})for(auto title:{GameTitle::Halo2,GameTitle::Halo3}) {
        Rig coarse(title,false),pose(title,false);
        unsigned oldRequests=0,newRequests=0;
        const auto origin=pose.sample.hands;
        for(unsigned frame=1;frame<=24;++frame) {
            const float delta=.14f*float(frame<=12?frame:24-frame)/12.f;
            const uint64_t exact=1000+frame*dt;
            coarse.sample.now=(exact/16)*16;
            pose.sample.now=dual_wield_shake::PoseMilliseconds(int64_t(exact)*1000000);
            for(unsigned slot=0;slot<2;++slot) {
                coarse.sample.hands[slot].x=pose.sample.hands[slot].x=origin[slot].x+delta;
            }
            oldRequests|=coarse.state.Update(coarse.sample);
            newRequests|=pose.state.Update(pose.sample);
        }
        Check(newRequests==3,"pose-clock shake works at high and lower frame rates");
        if(dt<16)Check(oldRequests==3,"duplicate coarse wall-clock polls no longer reset the high-rate gesture");
    }
    Check(dual_wield_shake::PoseMilliseconds(0)==0 && dual_wield_shake::PoseMilliseconds(-1)==0,
        "missing pose time cannot start a gesture");
    std::printf("Dual-wield shake detector: %u checks passed\n",checks);
}
