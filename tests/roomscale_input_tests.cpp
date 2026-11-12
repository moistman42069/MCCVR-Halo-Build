// Exercise the actual camera -> atomic command -> XInput consumer transport.
// Only the clock, tracking freshness and title service are fakes. No MCC process.
#include <windows.h>
#include <cmath>
#include <iostream>
#include <thread>
#include <array>
#include "../src/common/input_logic.h"
#include "../src/common/config.h"
#include "../src/dll/vr.h"
#include "../src/dll/title_adapter.h"

namespace {
uint64_t testNow=1000;
GameTitle testTitle=GameTitle::Halo3;
RuntimeMode testMode=RuntimeMode::Gameplay;
uint32_t testGeneration=1;
bool testTracking=true;
VrContactTrackingSnapshot testRunTracking{};
uint64_t RoomscaleTestNow() noexcept { return testNow; }
// The view recenter's frame clock (microseconds), on the same fake time.
uint64_t RoomscaleTestNowUs() noexcept { return testNow*1000; }
}
GameTitle TitleAdapter_GetActiveTitle() { return testTitle; }
RuntimeMode TitleAdapter_GetRuntimeMode() {return testMode;}
uint32_t TitleAdapter_GetGeneration(GameTitle title)
{ return title==testTitle ? testGeneration : 0; }
bool VR_RoomscaleTrackingFresh() noexcept { return testTracking; }
bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& snapshot)
{snapshot=testRunTracking;return snapshot.serial!=0;}

// Windows headers were already included: replace call sites, not WinAPI declarations.
#define GetTickCount64 RoomscaleTestNow
#define ROOMSCALE_NOW_US RoomscaleTestNowUs
#include "../src/dll/roomscale.cpp"
#undef GetTickCount64

int RunRoomscaleInputTests()
{
    int failures=0;
    auto check=[&](bool ok,const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    const bool saved=g_config.roomscale_movement;
    g_config.roomscale_movement=true;
    for (GameTitle title : {GameTitle::HaloCE,GameTitle::Halo2,GameTitle::Halo3,GameTitle::Halo3ODST,
                           GameTitle::HaloReach,GameTitle::Halo4})
    {
        testTitle=title; ++testGeneration; testNow+=1000; testTracking=true;
        const float q[4]{0,0,0,1}, forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        Roomscale_Input(false,0,0);
        const auto camera=[&](bool native=true) {
            Roomscale_Camera(title,native,body,head,q,forward,ref,0.328084f);
        };
        const bool eligible=RoomscaleGameplayEligible(title,RuntimeMode::Gameplay);
        Roomscale_Input(eligible,0,0); camera();
        testNow+=16; head[2]=-0.2f; Roomscale_Input(eligible,0,0); camera();
        float x=0,y=0;
        check(Roomscale_Move(x,y) && y>0 && x==0,
            "every supported title transports physical steps to native walking");

        // Import shim -> export -> forwarded export. Inner wrappers must not
        // merge; otherwise the outer merge detects its own output as manual
        // input, invalidates the epoch, and destroys the follow command.
        unsigned depth=0,merges=0;
        auto poll=[&](auto&& self,int wrappers,float physicalY) -> float {
            InputPollMergeScope scope(depth);
            float output=wrappers ? self(self,wrappers-1,physicalY) : physicalY;
            if (scope.IsOutermost()) {
                ++merges;
                Roomscale_Input(eligible && std::fabs(output)<0.24f,0,0);
                float rx=0,ry=output;
                if (Roomscale_Move(rx,ry)) output=ry;
            }
            return output;
        };
        for (int wrappers : {0,1,2}) {
            merges=0;
            check(poll(poll,wrappers,0)>0 && merges==1 && depth==0,
                "nested XInput paths preserve follow demand and merge exactly once");
        }
        check(poll(poll,2,0.75f)==0.75f,
            "nested XInput preserves an actual physical movement stick");
        x=y=0;
        check(!Roomscale_Move(x,y),"physical input cancels already-published follow immediately");
        Roomscale_Input(eligible,0,0); testNow+=16; camera();
        x=y=0;
        // The step owed from before the stick survives it (RoomscaleFollow::Rebase): the camera
        // derives a fresh demand for it, in the new epoch, rather than replaying the old one.
        check(Roomscale_Move(x,y) && y>0,
            "after the physical stick the owed step is followed again, as a fresh demand");
        testNow+=16; head[2]-=0.2f; camera();
        x=y=0; check(Roomscale_Move(x,y),"a fresh physical step resumes body follow");
        body[0]+=0.1f*0.328084f; testNow+=16; camera();
        check(std::fabs(ref[2]+0.1f)<1e-5f && ref[1]==1.7f,
            "only observed native horizontal travel consumes tracking offset");
        testNow+=101; x=y=0;
        check(!Roomscale_Move(x,y),"expired input/command cannot walk unattended");
        Roomscale_Input(eligible,0,0); camera();
        testTracking=false; x=y=0;
        check(!Roomscale_Move(x,y),"tracking interruption cancels body follow before another camera");
        testTracking=true; Roomscale_Input(false,0,0); camera(false);
        x=y=0; check(!Roomscale_Move(x,y),"native admission loss cancels follow without tearing down VR");
        Roomscale_Input(eligible,0,0); camera(); testNow+=16; head[0]+=0.15f; camera();
        ++testGeneration; x=y=0;
        check(!Roomscale_Move(x,y),"new generation rejects an old movement packet");
        camera(); testNow+=16; head[0]+=0.15f; camera();
        g_config.roomscale_movement=false; x=y=0;
        check(!Roomscale_Move(x,y),"toggle off cancels movement immediately");
        g_config.roomscale_movement=true;
        for (RuntimeMode mode : {RuntimeMode::Shell,RuntimeMode::Loading,RuntimeMode::Paused,
             RuntimeMode::Cutscene,RuntimeMode::Vehicle,RuntimeMode::Turret,RuntimeMode::Dead,
             RuntimeMode::Unsupported})
            check(!RoomscaleGameplayEligible(title,mode),"roomscale never admits non-gameplay modes");
    }
    // THE VIEW RECENTER, through the real camera hook: the title's reference slides toward the head on
    // its two horizontal axes only, in every title, and only when an option is on and roomscale admits.
    for (GameTitle title : {GameTitle::HaloCE,GameTitle::Halo2,GameTitle::Halo3,GameTitle::Halo3ODST,
                           GameTitle::HaloReach,GameTitle::Halo4})
    for (int law : {1,0})
    {
        const int savedLaw=g_config.roomscale_follow_law;
        g_config.roomscale_follow_law=law;
        // Every option off to start with (they default on); each is switched on in turn below.
        g_config.roomscale_recenter_jump=g_config.roomscale_recenter_move_start=g_config.roomscale_recenter_moving=false;
        testTitle=title; ++testGeneration; testNow+=1000; testTracking=true;
        const float q[4]{0,0,0,1}, forward[3]{1,0,0};
        const float scale=0.328084f;
        float body[3]{0,0,0.6f},head[3]{0,1.7f,0},ref[3]{0,1.62f,0};
        float stickY=0;
        const auto frame=[&](bool native=true) {
            testNow+=11; Roomscale_Input(true,0,stickY);
            Roomscale_Camera(title,native,body,head,q,forward,ref,scale);
        };
        const auto offset=[&]{ return std::hypot(head[0]-ref[0],head[2]-ref[2]); };
        Roomscale_Input(false,0,0);
        frame(); frame();
        // Stick travel with the head 0.3 m off to the side and every option off: the reference stays.
        head[0]=0.3f; head[2]=0; stickY=0.75f;
        for (int i=0;i<30;++i) { body[0]+=0.02f*scale; frame(); }
        check(std::fabs(offset()-0.3f)<1e-5f && ref[1]==1.62f,
            "recenter off: stick travel leaves the head-to-camera offset alone");
        // While moving, 0.5 m/s: about 0.1 m in 0.2 s.
        g_config.roomscale_recenter_moving=true; g_config.roomscale_recenter_moving_speed=0.5f;
        const float before=offset();
        for (int i=0;i<18;++i) { body[0]+=0.02f*scale; frame(); }
        check(std::fabs((before-offset())-0.5f*0.198f)<0.012f,
            "while moving: the camera hook slides the reference toward the head at the set speed");
        check(ref[1]==1.62f && std::fabs(ref[2])<1e-6f,
            "the slide moves the reference along the offset only: not its height, not the other axis");
        // Standing still afterwards, what the slide left is still owed: the follow asks the body to walk it
        // (under the threshold law too, which would otherwise take the moved reference for a recenter).
        stickY=0; for (int i=0;i<20;++i) frame();
        { float owedX=0,owedY=0;
          check(Roomscale_Move(owedX,owedY) && std::hypot(owedX,owedY)>0.05f && std::fabs(offset()-(before-0.5f*0.198f))<0.012f,
              "after the slide, standing still, the legs are still asked to walk what is left (both follow laws)"); }
        stickY=0.75f; frame();
        // Not admitted (a seat, a cinematic, third person): nothing moves, and it is counted.
        const float held=offset();
        const auto refusedBefore=recenterRefused[size_t(roomscale_recenter::Refusal::NotOnFoot)].load();
        stickY=0; frame(false); stickY=0.75f;
        for (int i=0;i<18;++i) frame(false);
        check(offset()==held && recenterRefused[size_t(roomscale_recenter::Refusal::NotOnFoot)].load()==refusedBefore+1,
            "not on foot in first person: the recenter refuses, once per stick hold, and the reference stays");
        // An untracked head refuses too.
        testTracking=false; stickY=0; frame(); stickY=0.75f;
        const auto untrackedBefore=recenterRefused[size_t(roomscale_recenter::Refusal::Untracked)].load();
        for (int i=0;i<10;++i) frame();
        check(offset()==held && recenterRefused[size_t(roomscale_recenter::Refusal::Untracked)].load()==untrackedBefore+1,
            "an untracked head: the recenter refuses and the reference stays");
        testTracking=true;
        // The start-of-movement blend: 0.2 s to zero from the push.
        g_config.roomscale_recenter_moving=false;
        g_config.roomscale_recenter_move_start=true; g_config.roomscale_recenter_move_start_s=0.2f;
        stickY=0; for (int i=0;i<40;++i) frame();
        const float left=offset();
        // Standing still for 0.44 s the follow has been asking the body to come; this fixture's body
        // does not move, so the offset is what it was.
        check(std::fabs(left-held)<1e-5f, "fixture: the offset waits while the body does not move");
        stickY=0.75f;
        for (int i=0;i<21;++i) { body[0]+=0.02f*scale; frame(); }
        check(offset()<1e-4f && ref[1]==1.62f,
            "start of movement: one quick blend brings the reference under the head, height untouched");
        // The jump blend: the jump action, then the character's eye rising.
        g_config.roomscale_recenter_move_start=false;
        g_config.roomscale_recenter_jump=true; g_config.roomscale_recenter_jump_s=0.15f;
        stickY=0; for (int i=0;i<40;++i) frame();
        head[0]+=0.02f; head[2]-=0.25f;      // a step forward inside the follow's reach
        for (int i=0;i<3;++i) frame();
        const float grounded=offset();
        check(grounded>0.2f, "fixture: displaced from the character before the jump");
        for (int i=0;i<10;++i) { body[2]+=0.05f*scale; frame(); }
        check(offset()==grounded, "rising without the jump action is not a jump: no recenter");
        const auto takeoffsBefore=recenterTakeoffs.load();
        for (int i=0;i<24;++i) { Roomscale_JumpInput(i<6); body[2]+=0.05f*scale; frame(); }
        check(recenterTakeoffs.load()==takeoffsBefore+1 && offset()<1e-4f && ref[1]==1.62f,
            "a jump: the action and the rising eye start the blend, which lands the reference under the head");
        g_config.roomscale_recenter_jump=false; g_config.roomscale_follow_law=savedLaw;
    }
    // A stick pulse can begin and end between two native camera callbacks.
    // Its cancellation must retire the old packet even though the camera never
    // observed the stick held down.
    {
        testTitle=GameTitle::Halo3;++testGeneration;testNow+=1000;
        const float q[4]{0,0,0,1},forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        Roomscale_Input(false,0,0);Roomscale_Input(true,0,0);
        Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);
        testNow+=16;head[2]=-.2f;Roomscale_Input(true,0,0);
        Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);
        float x=0,y=0;check(Roomscale_Move(x,y),"stick-pulse fixture has a published walking packet");
        testNow+=1;Roomscale_Input(true,0,.75f);
        testNow+=1;Roomscale_Input(true,0,0);
        x=y=0;
        check(!Roomscale_Move(x,y),"between-camera manual stick pulse retires the preceding movement packet");
        Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);
        x=y=0;
        check(!Roomscale_Move(x,y),"missed manual interval waits for native quiet instead of replaying a pre-stick packet");
    }
    // Expiration must be tested before consuming motion after a callback gap.
    // This exercises the actual follow policy used by the production camera.
    {
        RoomscaleFollow follow;
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0},x=0,y=0;
        uint64_t now=1000;
        follow.Update(1,now,true,false,body,head,ref,0,-1,1,0,1,x,y);
        head[2]=-.2f;
        bool tailFound=false;
        for(unsigned frame=0;frame<1000;++frame)
        {
            body[0]+=y*.02f;
            now+=10;
            follow.Update(1,now,true,false,body,head,ref,0,-1,1,0,1,x,y);
            if(follow.settling&&!follow.commanded&&follow.lastMotion==now)
            {tailFound=true;break;}
        }
        check(tailFound,"native walker enters bounded uncommanded stopping tail");
        const float before=ref[2];
        now+=180;body[0]+=.1f;
        follow.Update(1,now,true,false,body,head,ref,0,-1,1,0,1,x,y);
        check(ref[2]==before,"expired stopping tail cannot consume unrelated platform travel after callback gap");
    }
    // Retain physical steps taken during VR-stick travel, but never attribute
    // manual travel (including its braking tail) to the physical follow command.
    // The bounded fallback resumes after observed native motion becomes quiet;
    // it does not claim simultaneous manual and roomscale body movement.
    for (GameTitle title : {GameTitle::HaloCE,GameTitle::Halo2,GameTitle::Halo3,GameTitle::Halo3ODST,
                           GameTitle::HaloReach,GameTitle::Halo4})
    {
        testTitle=title;++testGeneration;testNow+=1000;testTracking=true;
        const float q[4]{0,0,0,1},forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        auto camera=[&]{Roomscale_Camera(title,true,body,head,q,forward,ref,1);};
        Roomscale_Input(false,0,0);Roomscale_Input(true,0,0);camera();
        bool manualPreserved=true;
        for(int frame=1;frame<=30;++frame)
        {
            testNow+=16;body[0]+=.04f;head[2]=-.2f*frame/30;
            Roomscale_Input(true,0,.75f);camera();
            float x=0,y=0;
            manualPreserved&=!Roomscale_Move(x,y)&&ref[2]==0;
        }
        check(manualPreserved,"manual movement preserves world travel and never consumes physical debt");
        bool tailPreserved=true;
        for(int frame=0;frame<12;++frame)
        {
            testNow+=16;body[0]+=.01f;Roomscale_Input(true,0,0);camera();
            float x=0,y=0;
            tailPreserved&=!Roomscale_Move(x,y)&&ref[2]==0;
        }
        check(tailPreserved,"manual braking and continuing platform motion cannot be claimed as physical follow");
        float x=0,y=0;
        for(int frame=0;frame<12;++frame)
        {testNow+=16;Roomscale_Input(true,0,0);camera();x=y=0;Roomscale_Move(x,y);}
        check(y>.5f&&ref[2]==0,"physical step taken during manual travel survives until fresh quiet catch-up");
        const float manualDistance=body[0];
        for(int frame=0;frame<300;++frame)
        {
            body[0]+=y*.032f;testNow+=16;Roomscale_Input(true,0,0);camera();
            x=y=0;Roomscale_Move(x,y);
        }
        check(std::fabs(body[0]-manualDistance-.2f)<.021f&&
            std::fabs(body[0]-manualDistance+ref[2])<.0001f,
            "deferred physical catch-up adds exactly one tracked step without cancelling manual world distance");
        float allManualDistance=manualDistance;
        bool repeatedDebt=true;
        for(int cycle=0;cycle<3;++cycle)
        {
            for(int frame=0;frame<10;++frame)
            {
                testNow+=16;body[0]+=.02f;allManualDistance+=.02f;head[2]-=.01f;
                Roomscale_Input(true,0,.5f);camera();
            }
            x=y=0;
            for(int frame=0;frame<300;++frame)
            {
                body[0]+=y*.032f;testNow+=16;Roomscale_Input(true,0,0);camera();
                x=y=0;Roomscale_Move(x,y);
            }
            repeatedDebt&=std::fabs(body[0]-allManualDistance+head[2])<.021f&&
                std::fabs(body[0]-allManualDistance+ref[2])<.0001f;
        }
        check(repeatedDebt,"repeated manual/physical cycles preserve accumulated physical travel without baseline drift");
        // Tracking loss keeps what is owed (dropping it would leave the body wherever the player
        // had walked away from it, no longer under him). When the headset is fresh again the body
        // walks the physical step it still owes.
        testNow+=16;Roomscale_Input(true,0,.75f);head[2]-=.2f;camera();
        testTracking=false;Roomscale_Input(true,0,0);camera();
        testTracking=true;Roomscale_Input(true,0,0);camera();
        for(int frame=0;frame<12;++frame)
        {testNow+=16;Roomscale_Input(true,0,0);camera();}
        x=y=0;
        check(Roomscale_Move(x,y)&&y>0,"tracking recovery resumes the physical step still owed");
        testNow+=16;Roomscale_Input(true,0,.75f);head[2]-=.2f;camera();
        // There may be no camera or input callback during an interruption. A stale polling gap ends
        // the follow in progress; the step it owed is walked once input is fresh again.
        testNow+=120;Roomscale_Input(true,0,0);camera();
        for(int frame=0;frame<12;++frame)
        {testNow+=16;Roomscale_Input(true,0,0);camera();}
        x=y=0;
        check(Roomscale_Move(x,y)&&y>0,"a stale polling gap keeps the physical step still owed");
    }
    // Closed-loop synthetic native walker: physical motion must end up in the
    // body, while body + remaining lean stays exactly one tracked displacement.
    // This validates feedback arithmetic, not Halo's unmeasured acceleration.
    for (int hz : {60,90,120}) for (float scale : {0.164f,0.328084f,0.656f})
    {
        testTitle=GameTitle::Halo3; ++testGeneration; testNow+=1000;
        Roomscale_Input(false,0,0);
        const float q[4]{0,0,0,1},forward[3]{1,0,0};
        float head[3]{0,1.7f,0},ref[3]{0,1.7f,0},body[3]{};
        float mx=0,my=0; bool singleMotion=true;
        for (int frame=0;frame<hz*4;++frame)
        {
            body[0]+=my*2.0f/hz*scale;
            body[1]-=mx*2.0f/hz*scale;
            const float t=std::min(1.0f,float(frame)/hz);
            head[0]=0.15f*t; head[2]=-0.3f*t;
            head[1]=1.7f-0.2f*t; // crouching must not alter the height reference
            testNow+=1000/hz;
            Roomscale_Input(true,0,0);
            Roomscale_Camera(testTitle,true,body,head,q,forward,ref,scale);
            singleMotion &= std::fabs(body[0]/scale-head[2]+ref[2]-0.3f*t)<0.0001f &&
                std::fabs(-body[1]/scale+head[0]-ref[0]-0.15f*t)<0.0001f && ref[1]==1.7f;
            mx=my=0; Roomscale_Move(mx,my);
        }
        check(singleMotion,"60/90/120 Hz follow consumes native travel once without changing height");
        check(std::hypot(body[0]/scale-0.3f,-body[1]/scale-0.15f)<=0.021f && mx==0 && my==0,
            "body reaches the physical step and stays stopped within the 2 cm deadband");
    }
    // A native walker does not stop instantly when the requested stick reaches
    // zero. Every resulting centimetre must be consumed exactly once, including
    // a bounded stopping tail; otherwise the rendered view slides past the head.
    {
        testTitle=GameTitle::Halo3;++testGeneration;testNow+=1000;
        const float q[4]{0,0,0,1},forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        Roomscale_Input(false,0,0);Roomscale_Input(true,0,0);
        auto sample=[&]{Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);};
        sample();testNow+=16;head[2]=-.2f;Roomscale_Input(true,0,0);sample();
        float x=0,y=0;check(Roomscale_Move(x,y),"coasting fixture begins with a delivered command");
        for(float nativePosition:{.19f,.21f,.25f})
        {
            testNow+=16;body[0]=nativePosition;Roomscale_Input(true,0,0);sample();
            check(std::fabs(body[0]-head[2]+ref[2]-.2f)<1e-5f,
                "native stopping travel cannot slide the view away from the physical head");
            x=y=0;Roomscale_Move(x,y);
        }
        check(y<0,"an overshoot past the start band (5 cm) requests a correction");
    }
    // ...but one inside the start band (2.5 cm) is left alone, like any residual under kFollowStart.
    {
        testTitle=GameTitle::Halo3;++testGeneration;testNow+=1000;
        const float q[4]{0,0,0,1},forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        Roomscale_Input(false,0,0);Roomscale_Input(true,0,0);
        auto sample=[&]{Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);};
        sample();testNow+=16;head[2]=-.2f;Roomscale_Input(true,0,0);sample();
        float x=0,y=0;check(Roomscale_Move(x,y),"small-overshoot fixture begins with a delivered command");
        for(float nativePosition:{.19f,.21f,.225f})
        {
            testNow+=16;body[0]=nativePosition;Roomscale_Input(true,0,0);sample();
            x=y=0;Roomscale_Move(x,y);
        }
        check(std::fabs(body[0]-head[2]+ref[2]-.2f)<1e-5f && x==0 && y==0,
            "a 2.5 cm overshoot is consumed and left uncommanded");
    }
    // The same title's engine callbacks can migrate threads. A global tracking
    // reference must not be paired with separate per-thread feedback histories.
    {
        testTitle=GameTitle::Halo3;++testGeneration;testNow+=1000;
        const float q[4]{0,0,0,1},forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        Roomscale_Input(false,0,0);Roomscale_Input(true,0,0);
        Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);
        testNow+=16;head[2]=-.2f;Roomscale_Input(true,0,0);
        std::thread first([&]{Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);});first.join();
        float x=0,y=0;check(Roomscale_Move(x,y)&&y>0,"callback migration preserves the physical step");
        testNow+=16;body[0]=.1f;Roomscale_Input(true,0,0);
        std::thread second([&]{Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);});second.join();
        check(std::fabs(ref[2]+.1f)<1e-5f,"another camera thread consumes the same owned body travel once");
    }
    // Delayed, accelerating native locomotion rather than instantaneous velocity.
    // A head turn during the step changes the input basis, not the physical goal.
    for(int hz:{60,90,120})for(int bodyHz:{30,hz})for(int delayMs:{0,33,66})for(float speed:{2.0f,4.0f})
    {
        testTitle=GameTitle::Halo3;++testGeneration;testNow+=1000;
        Roomscale_Input(false,0,0);
        float q[4]{0,0,0,1},forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        std::array<std::array<float,2>,32> pending{};float velocity[2]{},nativeBody[2]{};
        const int lag=delayMs*hz/1000;float worstViewError=0;
        for(int frame=0;frame<hz*6;++frame)
        {
            const auto applied=pending[(frame+32-lag)%32];
            const float blend=1-std::exp(-1.0f/(hz*.09f));
            for(int axis=0;axis<2;++axis){velocity[axis]+=(applied[axis]*speed-velocity[axis])*blend;nativeBody[axis]+=velocity[axis]/hz;}
            if(frame==0||frame*bodyHz/hz!=(frame-1)*bodyHz/hz)
                for(int axis=0;axis<2;++axis)body[axis]=nativeBody[axis];
            const float t=std::min(1.0f,float(frame)/hz);head[0]=.15f*t;head[2]=-.3f*t;
            if(frame==hz+hz/4){q[1]=-.707106781f;q[3]=.707106781f;forward[0]=0;forward[1]=-1;}
            testNow+=1000/hz;Roomscale_Input(true,0,0);
            Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);
            worstViewError=std::max(worstViewError,std::hypot(body[0]+ref[2],body[1]+ref[0]));
            float x=0,y=0;Roomscale_Move(x,y);
            pending[frame%32]={y*forward[0]+x*forward[1],y*forward[1]-x*forward[0]};
            // Zero-delay transport means the newest packet applies next frame.
            if(lag==0)pending[(frame+1)%32]=pending[frame%32];
        }
        if(worstViewError>=.0001f||std::hypot(body[0]-.3f,body[1]+.15f)>=.025f)std::cerr << "roomscale model hz=" << hz << " body-hz=" << bodyHz << " lag=" << delayMs << " speed=" << speed << " view-error=" << worstViewError << " body-error=" << std::hypot(body[0]-.3f,body[1]+.15f) << '\n';
        check(worstViewError<.0001f,"delayed native acceleration and turning never double physical camera travel");
        check(std::hypot(body[0]-.3f,body[1]+.15f)<.025f,
            "delayed native walker settles near the tracked body without recentering");
    }
    // Repeated out-and-back walks must not accumulate a need to recenter.
    {
        testTitle=GameTitle::Halo3;++testGeneration;testNow+=1000;Roomscale_Input(false,0,0);
        const float q[4]{0,0,0,1},forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0},velocity=0,requested=0,worstDrift=0;
        for(int frame=0;frame<120*4*24;++frame)
        {
            velocity+=(requested*3-velocity)*(1-std::exp(-1.0f/(120*.09f)));body[0]+=velocity/120;
            const float phase=float(frame%(120*4))/120;
            head[2]=-.3f*(phase<1?phase:phase<2?1:phase<3?3-phase:0);
            testNow+=8;Roomscale_Input(true,0,0);Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);
            worstDrift=std::max(worstDrift,std::fabs(body[0]+ref[2]));
            float x=0,y=0;Roomscale_Move(x,y);requested=y;
        }
        check(worstDrift<.001f&&std::fabs(body[0])<.025f,"repeated physical walks do not accumulate body/view drift");
        // Let the final native stopping tail finish, then move the native body
        // externally with no physical step or follow request.
        for(int frame=0;frame<180;++frame)
        {
            velocity+=(requested*3-velocity)*(1-std::exp(-1.0f/(120*.09f)));body[0]+=velocity/120;
            testNow+=8;Roomscale_Input(true,0,0);Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);
            float x=0,y=0;Roomscale_Move(x,y);requested=y;
        }
        const float savedReference=ref[2];body[0]+=.1f;testNow+=8;Roomscale_Input(true,0,0);
        Roomscale_Camera(testTitle,true,body,head,q,forward,ref,1);
        check(ref[2]==savedReference,"expired settling does not consume unrelated native motion");
    }
    for (GameTitle title : {GameTitle::None,GameTitle::Unknown})
        check(!RoomscaleGameplayEligible(title,RuntimeMode::Gameplay),
            "roomscale cannot acquire an unsupported title");
    // Every title has roomscale: CE's body following follows the same saved setting as the others.
    check(RoomscaleGameplayEligible(GameTitle::HaloCE,RuntimeMode::Gameplay),"CE is eligible for roomscale");
    {
        testTitle=GameTitle::HaloCE; ++testGeneration; testNow+=1000;
        Roomscale_Input(false,0,0);
        const float q[4]{0,0,0,1},forward[3]{1,0,0};
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        bool ceMoved=false;
        for (int sample=0;sample<3;++sample) {
            Roomscale_Input(RoomscaleGameplayEligible(testTitle,RuntimeMode::Gameplay),0,0);
            Roomscale_Camera(testTitle,true,body,head,q,forward,ref,0.328084f);
            float x=0,y=0;
            if (Roomscale_Move(x,y)&&(x!=0||y!=0)) ceMoved=true;
            testNow+=16; head[2]-=0.2f;
        }
        check(ceMoved,"CE roomscale follows a physical step with the saved setting enabled");
    }
    // CE still requires independent on-foot native camera proof; a saved option
    // and fresh physical motion cannot grant it on their own.
    testTitle=GameTitle::HaloCE; ++testGeneration; testNow+=1000;
    Roomscale_Input(false,0,0);
    const float ceOrientation[4]{0,0,0,1},ceForward[3]{1,0,0};
    float ceBody[3]{},ceHead[3]{0,1.7f,0},ceReference[3]{0,1.7f,0};
    for (int sample=0;sample<3;++sample) {
        Roomscale_Input(RoomscaleGameplayEligible(testTitle,RuntimeMode::Gameplay),0,0);
        Roomscale_Camera(testTitle,false,ceBody,ceHead,ceOrientation,ceForward,ceReference,0.328084f);
        float x=0,y=0;
        check(!Roomscale_Move(x,y)&&x==0&&y==0,
            "CE roomscale remains inactive without native on-foot admission");
        testNow+=16; ceHead[2]-=0.2f;
    }
    g_config.roomscale_movement=saved;
    const bool savedRun=g_config.physical_running;
    for(int hz:{60,90,120,240}) {
        physical_running::Gesture gesture;
        physical_running::Sample sample{};
        sample.valid=true;sample.epoch=1;sample.head.y=1.7f;
        float result=0;
        for(int i=0;i<hz*3;++i) {
            sample.serial=uint64_t(i)+1;sample.timeNs=1'000'000'000+int64_t(i)*1'000'000'000/hz;
            const float z=.25f*std::sin(float(i)*10.f/hz);
            sample.hands[0].z=z;sample.hands[1].z=-z;
            result=gesture.Update(sample,1,.7f);
        }
        check(result>.2f&&result<=.7f,"alternating arms work across headset refresh rates");
        check(gesture.Update(sample,1,.7f)==result,"duplicate eyes do not advance run acceleration");
        ++sample.epoch;++sample.serial;sample.timeNs+=16'000'000;
        check(gesture.Update(sample,1,.7f)==0,"recenter clears running momentum");
        sample.valid=false;check(gesture.Update(sample,1,.7f)==0,"invalid tracking stops running immediately");
        for(int mode=0;mode<3;++mode) {
            gesture.Reset();sample={};sample.valid=true;sample.epoch=1;
            bool stayedStill=true;
            for(int i=0;i<hz*2;++i) {
                sample.serial=uint64_t(i)+1;sample.timeNs=1'000'000'000+int64_t(i)*1'000'000'000/hz;
                const float z=.25f*std::sin(float(i)*10.f/hz);
                sample.head={0,1.7f,mode==0?z:0};
                sample.hands[0].z=z;sample.hands[1].z=mode==2?0:z;
                stayedStill&=gesture.Update(sample,1,1)==0;
            }
            check(stayedStill,"body translation, parallel arm motion and one-hand swings do not run");
        }
    }
    const float savedSpeed=g_config.physical_running_speed;
    g_config.physical_running=true;g_config.physical_running_speed=.6f;
    testTitle=GameTitle::Halo3;++testGeneration;testNow+=1000;
    testRunTracking={};testRunTracking.headValid=true;
    testRunTracking.hands[0].valid=true;testRunTracking.rawPrimaryValid=true;
    testRunTracking.referenceEpoch=1;testRunTracking.headPosition[1]=1.7f;
    float rx=0,ry=0;
    for(int i=0;i<180;++i) {
        testRunTracking.serial=uint64_t(i)+1;testRunTracking.timeNs=1'000'000'000+int64_t(i)*16'666'667;
        const float z=.25f*std::sin(float(i)*.18f);
        testRunTracking.hands[0].position[2]=z;testRunTracking.rawPrimaryPosition[2]=-z;
        testNow+=17;
        Roomscale_Camera(testTitle,true,ceBody,ceHead,ceOrientation,ceForward,ceReference,.328084f);
    }
    check(PhysicalRunning_Move(rx,ry)&&rx==0&&ry>0&&ry<=.6f,
        "arm swings publish bounded forward native movement");
    for(auto mode:{RuntimeMode::Vehicle,RuntimeMode::Turret}) {
        testMode=mode;
        check(!PhysicalRunning_Move(rx,ry),"seat entry immediately rejects the last on-foot running command");
    }
    testMode=RuntimeMode::Gameplay;
    testNow+=101;
    Roomscale_Camera(testTitle,true,ceBody,ceHead,ceOrientation,ceForward,ceReference,.328084f);
    check(!PhysicalRunning_Move(rx,ry),"repeated frozen XR sample cannot renew running command");
    ++testRunTracking.serial;testRunTracking.timeNs+=16'666'667;
    Roomscale_Camera(testTitle,false,ceBody,ceHead,ceOrientation,ceForward,ceReference,.328084f);
    check(!PhysicalRunning_Move(rx,ry),"native on-foot admission loss stops running");
    testRunTracking.locomotionBlocked=true;
    Roomscale_Camera(testTitle,true,ceBody,ceHead,ceOrientation,ceForward,ceReference,.328084f);
    check(!PhysicalRunning_Move(rx,ry),"reload/holster grip ownership suppresses arm-swing locomotion");
    g_config.physical_running=savedRun;g_config.physical_running_speed=savedSpeed;

    // THE ADMISSION VERDICT (roomscale_admission_logic.h). A refusal stops the follow whatever its reason and is
    // counted under that reason; the first admitted frame of the next title re-arms it with nothing carried over
    // (several titles in one MCC session, e.g. ODST -> Halo 3).
    {
        using roomscale_admission::Refusal;
        const bool savedRoomscale=g_config.roomscale_movement;
        g_config.roomscale_movement=true;
        const float q[4]{0,0,0,1}, forward[3]{1,0,0};
        const auto walk=[&](GameTitle title,const roomscale_admission::Verdict& verdict,float step,float& x,float& y) {
            testTitle=title; ++testGeneration; testNow+=1000; testTracking=true; testMode=RuntimeMode::Gameplay;
            float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
            Roomscale_Input(true,0,0);
            Roomscale_Camera(title,verdict,body,head,q,forward,ref,0.328084f);
            testNow+=16; head[2]=-step; Roomscale_Input(true,0,0);
            Roomscale_Camera(title,verdict,body,head,q,forward,ref,0.328084f);
            x=y=0;
            return Roomscale_Move(x,y);
        };
        for (auto& counter:admissionRefused) counter.store(0);
        float x=0,y=0;
        check(walk(GameTitle::Halo3ODST,Roomscale_Refuse(Refusal::None),0.2f,x,y) && y>0,
            "an admitted verdict walks the body (ODST)");
        // A title that refuses for want of a view model (an unarmed Halo 3 player with no camera rung).
        check(!walk(GameTitle::Halo3,Roomscale_Refuse(Refusal::NoFirstPersonModel),0.2f,x,y),
            "a refused verdict issues no follow command");
        check(admissionRefused[size_t(Refusal::NoFirstPersonModel)].load()==2 &&
              admissionRefused[size_t(Refusal::Seated)].load()==0,
            "the refusal is counted under the reading that refused, once per camera frame");
        check((admissionLastRungs.load()>>16)==unsigned(GameTitle::Halo3) &&
              ((admissionLastRungs.load()>>8)&0xFFu)==unsigned(Refusal::NoFirstPersonModel),
            "the last refusal remembers its title and reason for the report");
        // ... and after it: the same title, admitted, walks at once.
        check(walk(GameTitle::Halo3,Roomscale_Refuse(Refusal::None),0.2f,x,y) && y>0,
            "the title that refused walks again on its first admitted frames");
        // Back and forth between titles: each switch starts over and re-arms.
        check(walk(GameTitle::HaloReach,Roomscale_Refuse(Refusal::None),0.2f,x,y) && y>0 &&
              walk(GameTitle::Halo3,Roomscale_Refuse(Refusal::None),0.2f,x,y) && y>0,
            "switching titles in one session re-arms the follow each time");
        check(errorLastMm.load()<1000 && recenterOffsetMm.load()<1000,
            "the report's 'now' numbers are the active title's");
        // The bare yes / no overload is the same call.
        float body[3]{},head[3]{0,1.7f,0},ref[3]{0,1.7f,0};
        for (auto& counter:admissionRefused) counter.store(0);
        Roomscale_Camera(testTitle,false,body,head,q,forward,ref,0.328084f);
        check(admissionRefused[size_t(Refusal::Unspecified)].load()==1, "a bare 'not allowed' is counted as the caller's");
        // The status tick drains the window's counters (the line itself is roomscale_admission::FormatRefusals).
        testNow+=5000; Roomscale_Report();
        check(admissionRefused[size_t(Refusal::Unspecified)].load()==0, "the report starts a new window");
        g_config.roomscale_movement=savedRoomscale;
    }
    return failures;
}
