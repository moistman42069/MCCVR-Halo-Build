#include "../src/common/hud_reveal_logic.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void Check(bool v,const char* message) {
    if(!v) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
int main() {
    for(float radius:{.10f,.22f,.40f}) {
        hud_reveal::Gesture g;uint64_t now=1000,epoch=7;
        auto step=[&](float distance,bool ready=true) {now+=20;return g.Update(now,epoch,ready,distance,radius);};
        Check(!step(radius*.5f),"already-near hand cannot reveal at enable or title entry");
        Check(!step(radius+.1f),"moving away arms without revealing");
        for(int i=0;i<10;++i) Check(!step(radius*.5f),"brief passes cannot trigger a reveal");
        Check(step(radius*.5f),"a deliberate dwell reveals");
        Check(step(radius+.02f),"hysteresis keeps a steady reveal at the edge");
        Check(!step(radius+.05f),"moving away hides without modifying preference");
        for(int i=0;i<11;++i) step(radius*.5f);
        Check(!step(radius*.5f,false),"menus, grips, dual wield, reload and loss of tracking cancel");
        for(int i=0;i<15;++i) Check(!step(radius*.5f),"blocked hand cannot replay on resume");
        step(radius+.1f);for(int i=0;i<11;++i) step(radius*.5f);
        ++epoch;Check(!step(radius*.5f),"recenter or title generation cancels");
        step(radius+.1f);for(int i=0;i<11;++i) step(radius*.5f);
        now+=101;Check(!step(radius*.5f),"stale input cancels and requires a fresh crossing");
        Check(!step(std::numeric_limits<float>::quiet_NaN()),"invalid pose never reveals");
    }
    hud_reveal::Publication p;
    for(auto title:{GameTitle::HaloCE,GameTitle::Halo2,GameTitle::Halo3,GameTitle::Halo3ODST,GameTitle::HaloReach,GameTitle::Halo4}) {
        for(uint64_t now:{1000ull,(1ull<<29)-50,(1ull<<32)-50}) {
            p.Publish(true,title,12,now);
            Check(p.Active(title,12,now+90),"fresh owned reveal survives clock wrap");
            Check(!p.Active(title,12,now+101),"rendering cannot retain a stalled gesture");
            Check(!p.Active(title,13,now),"foreign generation cannot reveal");
            Check(!p.Active(GameTitle::None,12,now),"foreign title cannot reveal");
            Check(!p.Active(title,12,now-1),"backward clock cannot reveal");
            p.Publish(false,title,12,now);
            Check(!p.Active(title,12,now),"cancellation is immediate");
        }
    }
    std::puts("HUD reveal gesture and publication checks passed");
}
