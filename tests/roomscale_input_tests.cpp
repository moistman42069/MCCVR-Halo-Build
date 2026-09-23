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
uint32_t testGeneration=1;
bool testTracking=true;
uint64_t RoomscaleTestNow() noexcept { return testNow; }
}
GameTitle TitleAdapter_GetActiveTitle() { return testTitle; }
uint32_t TitleAdapter_GetGeneration(GameTitle title)
{ return title==testTitle ? testGeneration : 0; }
bool VR_RoomscaleTrackingFresh() noexcept { return testTracking; }

// Windows headers were already included: replace call sites, not WinAPI declarations.
#define GetTickCount64 RoomscaleTestNow
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
    for (GameTitle title : {GameTitle::Halo2,GameTitle::Halo3,GameTitle::Halo3ODST,
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
        check(!Roomscale_Move(x,y),"physical stick release cannot replay old follow demand");
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
    for (GameTitle title : {GameTitle::Halo2,GameTitle::Halo3,GameTitle::Halo3ODST,
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
        // Tracking loss must drop retained debt instead of walking later when
        // a controller or headset becomes fresh again.
        testNow+=16;Roomscale_Input(true,0,.75f);head[2]-=.2f;camera();
        testTracking=false;Roomscale_Input(true,0,0);camera();
        testTracking=true;Roomscale_Input(true,0,0);camera();
        for(int frame=0;frame<12;++frame)
        {testNow+=16;Roomscale_Input(true,0,0);camera();}
        x=y=0;
        check(!Roomscale_Move(x,y),"tracking recovery never resumes stale physical debt from manual travel");
        testNow+=16;Roomscale_Input(true,0,.75f);head[2]-=.2f;camera();
        // There may be no camera or input callback during an interruption.
        // A fresh poll must not revive debt merely because allowed stays true.
        testNow+=120;Roomscale_Input(true,0,0);camera();
        for(int frame=0;frame<12;++frame)
        {testNow+=16;Roomscale_Input(true,0,0);camera();}
        x=y=0;
        check(!Roomscale_Move(x,y),"fresh input after a stale polling gap drops retained physical debt");
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
        for(float nativePosition:{.19f,.21f,.23f})
        {
            testNow+=16;body[0]=nativePosition;Roomscale_Input(true,0,0);sample();
            check(std::fabs(body[0]-head[2]+ref[2]-.2f)<1e-5f,
                "native stopping travel cannot slide the view away from the physical head");
            x=y=0;Roomscale_Move(x,y);
        }
        check(y<0,"overshoot requests correction instead of accepting body drift");
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
    for (GameTitle title : {GameTitle::None,GameTitle::Unknown,GameTitle::HaloCE})
        check(!RoomscaleGameplayEligible(title,RuntimeMode::Gameplay),
            "roomscale cannot acquire an unsupported title");
    // A saved experimental setting cannot inject body-follow movement into
    // CE's basic VR bring-up, even with fresh tracking and physical motion.
    testTitle=GameTitle::HaloCE; ++testGeneration; testNow+=1000;
    Roomscale_Input(false,0,0);
    const float ceOrientation[4]{0,0,0,1},ceForward[3]{1,0,0};
    float ceBody[3]{},ceHead[3]{0,1.7f,0},ceReference[3]{0,1.7f,0};
    for (int sample=0;sample<3;++sample) {
        Roomscale_Input(RoomscaleGameplayEligible(testTitle,RuntimeMode::Gameplay),0,0);
        Roomscale_Camera(testTitle,true,ceBody,ceHead,ceOrientation,ceForward,ceReference,0.328084f);
        float x=0,y=0;
        check(!Roomscale_Move(x,y)&&x==0&&y==0,
            "CE experimental roomscale stays deferred with saved setting enabled");
        testNow+=16; ceHead[2]-=0.2f;
    }
    g_config.roomscale_movement=saved;
    return failures;
}
