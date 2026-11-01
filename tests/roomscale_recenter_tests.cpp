// The roomscale view recenter (src/common/roomscale_recenter_logic.h): the jump witness, the two quick
// blends, the constant-speed slide, their refusals, and how a shift composes with both body-follow laws.
// Pure logic: no clock, no engine. The camera -> reference wiring is tested in roomscale_input_tests.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include "../src/common/roomscale_logic.h"
#include "../src/common/roomscale_recenter_logic.h"

int RunRoomscaleRecenterTests()
{
    using namespace roomscale_recenter;
    int failures=0;
    auto Check=[&](bool ok,const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    constexpr uint64_t kFrameUs=11'111;   // 90 Hz
    constexpr float kHaloGravity=12.7f;   // m/s^2 (4.17 world units)

    // ---- The jump witness -------------------------------------------------------------------------
    {
        JumpWitness witness;
        uint64_t now=1'000'000;
        // A jump: the press, then the eye rising on a ballistic arc (v0 5.6 m/s, about 0.9 s of air).
        bool tookOff=false; unsigned takeoffs=0; uint64_t tookOffAt=0, landedAt=0;
        const uint64_t pressedAt=now;
        for (int i=0;i<200;++i)
        {
            const float t=float(now-pressedAt)*1e-6f;
            const float height=std::max(0.0f,5.6f*t-0.5f*kHaloGravity*t*t);
            const bool takeoff=witness.Update(now,i<10,1.7f+height);
            if (takeoff) { ++takeoffs; if (!tookOff) { tookOff=true; tookOffAt=now; } }
            if (tookOff && !landedAt && !witness.airborne) landedAt=now;
            now+=kFrameUs;
        }
        const uint64_t airUs=uint64_t(2.0f*5.6f/kHaloGravity*1e6f);
        Check(takeoffs==1 && tookOffAt-pressedAt<=3*kFrameUs,
            "a press followed by the eye rising is one takeoff, seen within a few frames");
        Check(landedAt>=pressedAt+airUs && landedAt<=pressedAt+airUs+JumpWitness::kLandQuietUs+2*kFrameUs,
            "the jump lands when the eye has been still, never at the top of the arc");
    }
    {
        // A quarter of Halo's gravity (a low-gravity level): the top of the arc is slower, and still
        // not a landing.
        JumpWitness witness;
        uint64_t now=1'000'000; const uint64_t pressedAt=now;
        const float gravity=kHaloGravity*0.25f, v0=3.0f;
        const uint64_t airUs=uint64_t(2.0f*v0/gravity*1e6f);
        bool early=false, tookOff=false;
        for (uint64_t t=0;t<airUs-kFrameUs;t+=kFrameUs)
        {
            const float s=float(t)*1e-6f;
            tookOff|=witness.Update(now+t,t<100'000,1.7f+v0*s-0.5f*gravity*s*s);
            early|=tookOff && !witness.airborne;
        }
        Check(tookOff && !early, "a slow arc under low gravity stays airborne through its top");
        (void)pressedAt;
    }
    {
        JumpWitness witness;
        uint64_t now=1'000'000;
        bool any=false;
        // Vertical motion with no press: a ramp, stairs, a lift.
        for (int i=0;i<100;++i) { any|=witness.Update(now,false,1.7f+0.03f*float(i)); now+=kFrameUs; }
        Check(!any && !witness.airborne, "rising without a jump press is not a jump");
        // A press the game ignored: nothing rises.
        for (int i=0;i<100;++i) { any|=witness.Update(now,i<10,4.67f); now+=kFrameUs; }
        Check(!any && !witness.airborne, "a jump press the eye never answers is not a jump");
        // A press, then a rise that starts after the arming window (a lift that happens to start).
        for (int i=0;i<200;++i)
        { any|=witness.Update(now,i<5,4.67f+(i>45 ? 0.01f*float(i-45) : 0.0f)); now+=kFrameUs; }
        Check(!any, "a rise that begins after the arming window does not belong to the press");
        // A press, a dip, and the rise measured from the dip.
        witness.Reset();
        float height=1.7f; bool dipped=false;
        for (int i=0;i<20;++i)
        {
            height=i<4 ? 1.7f-0.02f*float(i) : 1.64f+0.02f*float(i-4);
            dipped|=witness.Update(now,true,height); now+=kFrameUs;
        }
        Check(dipped, "a jump that dips first is measured from its lowest point");
        // Landed late (walking on downhill keeps the eye moving): the next press still counts.
        bool second=false;
        for (int i=0;i<10;++i) { witness.Update(now,false,height-=0.02f); now+=kFrameUs; }
        Check(witness.airborne, "fixture: still reads airborne while the eye keeps moving");
        for (int i=0;i<10;++i) { second|=witness.Update(now,true,height+=0.03f); now+=kFrameUs; }
        Check(second, "a new press arms again even while the last jump still reads airborne");
        Check(!witness.Update(now,false,std::numeric_limits<float>::quiet_NaN()) && !witness.airborne && !witness.armed,
            "a non-finite eye height resets the witness");
    }

    // A small world for the blends: the head stays put, the reference moves by whatever Update returns.
    struct World
    {
        ViewRecenter recenter;
        Settings settings;
        float head[3]{0.30f,1.7f,-0.40f};      // 0.5 m from the reference, on both axes
        float reference[3]{0,1.62f,0};
        float eye=1.7f;                        // the character's eye height (metres)
        uint64_t now=1'000'000;
        bool stick=false, jump=false;
        Refusal blocked=Refusal::None;
        Result last{};
        float Offset() const { return std::hypot(head[0]-reference[0],head[2]-reference[2]); }
        Result Step(uint64_t frameUs=11'111)
        {
            now+=frameUs;
            last=recenter.Update(now,settings,blocked,stick,jump,eye,head[0]-reference[0],head[2]-reference[2]);
            reference[0]+=last.dx; reference[2]+=last.dz;
            return last;
        }
        // Start a jump: hold the action and raise the eye until the witness sees it.
        bool TakeOff()
        {
            jump=true;
            for (int i=0;i<8;++i) { eye+=0.03f; if (Step().takeoff) return true; }
            return false;
        }
    };

    // ---- Everything is off unless switched on ------------------------------------------------------
    {
        World w; w.stick=true;
        bool moved=false;
        for (int i=0;i<60;++i) { w.Step(); moved|=w.last.dx!=0 || w.last.dz!=0; }
        w.jump=true;
        for (int i=0;i<60;++i) { w.eye+=0.03f; w.Step(); moved|=w.last.dx!=0 || w.last.dz!=0 || w.last.takeoff; }
        Check(!moved && w.reference[0]==0 && w.reference[2]==0,
            "with every option off the reference never moves: stick held, jump or not");
    }

    // ---- The jump blend: linear, exact, never past zero ---------------------------------------------
    for (uint64_t frameUs : {uint64_t(16'667),uint64_t(11'111),uint64_t(6'944)})
    {
        World w; w.settings.jump=true; w.settings.jumpSeconds=0.20f;
        const float start=w.Offset();
        const float dirX=(w.head[0]-w.reference[0])/start, dirZ=(w.head[2]-w.reference[2])/start;
        for (int i=0;i<5;++i) w.Step(frameUs);
        Check(w.Offset()==start, "the jump option waits for a jump");
        Check(w.TakeOff() && w.last.started==Mode::Jump, "a takeoff begins the jump blend");
        const uint64_t began=w.now;
        bool monotonic=true, straight=true, linear=true, overshoot=false;
        float previous=w.Offset(); uint64_t doneAt=0;
        for (int i=0;i<60;++i)
        {
            w.eye+=0.03f;                       // still rising: airborne
            w.Step(frameUs);
            const float offset=w.Offset();
            monotonic&=offset<=previous+1e-6f;
            // Along the original direction only: no sideways component, never through zero.
            const float along=(w.head[0]-w.reference[0])*dirX+(w.head[2]-w.reference[2])*dirZ;
            const float across=(w.head[0]-w.reference[0])*dirZ-(w.head[2]-w.reference[2])*dirX;
            straight&=std::fabs(across)<1e-5f;
            overshoot|=along<-1e-5f;
            const float elapsed=float(w.now-began)*1e-6f;
            const float expected=start*std::max(0.0f,1.0f-elapsed/0.20f);
            linear&=std::fabs(offset-expected)<1e-4f;
            if (!doneAt && offset<1e-5f) doneAt=w.now;
            previous=offset;
        }
        Check(monotonic && straight && !overshoot, "the blend closes the offset in a straight line and never overshoots");
        Check(linear, "the blend is linear in time at every frame rate (60, 90 and 144 Hz)");
        Check(doneAt && doneAt-began+frameUs>200'000 && doneAt-began<200'000+frameUs,
            "the blend reaches zero offset at its duration, to the frame");
        Check(w.reference[1]==1.62f, "the blend never touches the reference's height");
        const float settledX=w.reference[0], settledZ=w.reference[2];
        for (int i=0;i<30;++i) w.Step(frameUs);
        Check(w.reference[0]==settledX && w.reference[2]==settledZ, "after it ends, nothing more moves");
    }
    {
        // The head keeps moving during the blend: it still lands on the head at the end.
        World w; w.settings.jump=true; w.settings.jumpSeconds=0.30f;
        Check(w.TakeOff(), "fixture: takeoff");
        const uint64_t began=w.now;
        do { w.eye+=0.03f; w.head[0]+=0.004f; w.head[2]-=0.002f; w.Step(); } while (w.recenter.blend!=Mode::None);
        Check(w.Offset()<1e-4f && w.now-began+kFrameUs>300'000 && w.now-began<300'000+kFrameUs,
            "a head that moves during the blend is still met at the end of it");
    }
    {
        // Landing ends it: a long blend, a short hop.
        World w; w.settings.jump=true; w.settings.jumpSeconds=1.0f;
        const float start=w.Offset();
        Check(w.TakeOff(), "fixture: takeoff");
        for (int i=0;i<20;++i) { w.eye+=0.03f; w.Step(); }
        w.jump=false;
        const float midAir=w.Offset();
        Check(midAir<start && midAir>start*0.5f, "fixture: the long blend is part of the way in");
        float landedOffset=-1;
        for (int i=0;i<60;++i)
        {
            w.Step();                            // the eye holds still: landed
            if (landedOffset<0 && !w.recenter.jumpWitness.airborne) landedOffset=w.Offset();
        }
        Check(landedOffset>0 && w.Offset()==landedOffset && w.recenter.blend==Mode::None,
            "landing stops the jump blend where it is; the rest is left for the body follow");
    }

    // ---- The start-of-movement blend -----------------------------------------------------------------
    {
        World w; w.settings.moveStart=true; w.settings.moveStartSeconds=0.20f;
        const float start=w.Offset();
        for (int i=0;i<5;++i) w.Step();
        w.stick=true;
        Check(w.Step().started==Mode::MoveStart && w.Offset()==start,
            "the first deflection of the stick begins the blend, on the next frame");
        for (int i=0;i<9;++i) w.Step();
        const float half=w.Offset();
        Check(std::fabs(half-start*0.5f)<start*0.02f, "half way through its time it has closed half the offset");
        w.stick=false;
        for (int i=0;i<30;++i) w.Step();
        Check(w.Offset()==half && w.recenter.blend==Mode::None,
            "releasing the stick stops the blend cleanly, exactly where it was");
        w.stick=true; w.Step();
        Check(w.last.started==Mode::MoveStart, "the next push begins a fresh blend");
        for (int i=0;i<30;++i) w.Step();
        Check(w.Offset()<1e-5f, "which closes what the first one left");
        w.settings.moveStart=false; w.head[0]+=0.2f; w.stick=false; w.Step(); w.stick=true;
        for (int i=0;i<30;++i) w.Step();
        Check(std::fabs(w.Offset()-0.2f)<1e-5f, "switched off, a push no longer starts one");
    }
    {
        // A jump begun mid-walk: the blend carries on in the air after the stick lets go.
        World w; w.settings.moveStart=true; w.settings.jump=true;
        w.settings.moveStartSeconds=0.50f; w.settings.jumpSeconds=0.50f;
        w.stick=true; w.Step();
        for (int i=0;i<8;++i) w.Step();
        Check(w.TakeOff(), "fixture: takeoff during the start-of-movement blend");
        w.stick=false;
        for (int i=0;i<60;++i) { w.eye+=0.03f; w.Step(); }
        Check(w.Offset()<1e-5f, "a blend begun by the stick finishes in the air of a jump that joined it");
    }

    // ---- The constant-speed slide while moving ---------------------------------------------------------
    for (float speed : {0.25f, 1.0f})
    {
        World w; w.settings.moving=true; w.settings.movingSpeed=speed;
        const float start=w.Offset();
        for (int i=0;i<30;++i) w.Step();
        Check(w.Offset()==start, "the while-moving option does nothing with the stick at rest");
        w.stick=true; w.Step();
        const uint64_t began=w.now; const float atBegin=w.Offset();
        bool respected=true, overshoot=false; uint64_t doneAt=0;
        const float dirX=(w.head[0]-w.reference[0])/atBegin, dirZ=(w.head[2]-w.reference[2])/atBegin;
        for (int i=0;i<400 && !doneAt;++i)
        {
            const float before=w.Offset();
            w.Step();
            const float step=std::hypot(w.last.dx,w.last.dz);
            respected&=step<=speed*float(kFrameUs)*1e-6f+1e-6f && std::fabs(before-w.Offset()-step)<1e-5f;
            overshoot|=(w.head[0]-w.reference[0])*dirX+(w.head[2]-w.reference[2])*dirZ<-1e-5f;
            if (w.Offset()<ViewRecenter::kDoneM) doneAt=w.now;
        }
        const float expectedSeconds=atBegin/speed;
        Check(respected, "each frame closes no more than speed x frame time");
        Check(doneAt && std::fabs(float(doneAt-began)*1e-6f-expectedSeconds)<0.03f,
            "the offset closes at the set speed (m/s), at both speeds tested");
        Check(!overshoot, "it stops at zero and never goes past it");
        for (int i=0;i<30;++i) w.Step();
        Check(w.Offset()<ViewRecenter::kDoneM && w.last.dx==0 && w.last.dz==0, "at zero it rests");
        Check(w.reference[1]==1.62f, "the slide never touches the reference's height");
    }
    {
        // Not all the way: the stick is released first, and what is left stays exactly as it is.
        World w; w.settings.moving=true; w.settings.movingSpeed=0.5f;
        w.stick=true;
        for (int i=0;i<46;++i) w.Step();          // 0.5 s
        const float left=w.Offset();
        Check(std::fabs(left-0.25f)<0.01f, "0.5 s at 0.5 m/s closes 0.25 m of a 0.5 m offset");
        w.stick=false;
        for (int i=0;i<60;++i) w.Step();
        Check(w.Offset()==left, "releasing the stick leaves the rest untouched");
        // A hitch: one long frame moves at most kMaxStepUs worth.
        w.stick=true; w.Step();
        const float before=w.Offset();
        w.Step(1'000'000);
        Check(before-w.Offset()<=0.5f*float(ViewRecenter::kMaxStepUs)*1e-6f+1e-6f,
            "the frame after a one-second hitch cannot leap");
    }
    {
        // Both together: the quick blend leads, then the slide holds the view over the character.
        World w; w.settings.moveStart=true; w.settings.moving=true;
        w.settings.moveStartSeconds=0.20f; w.settings.movingSpeed=0.5f;
        w.stick=true; w.Step();
        for (int i=0;i<20;++i) w.Step();
        Check(w.Offset()<1e-5f, "with both on, the quick blend sets the pace");
        bool held=true;
        for (int i=0;i<90;++i) { w.head[0]+=0.002f; w.Step(); held&=w.Offset()<0.004f; }   // the head drifts 0.18 m/s
        Check(held, "and the slide then keeps the view over the character while the head drifts");
    }

    // ---- Refusals: an offset that cannot be trusted is not acted on ------------------------------------
    {
        World w; w.settings.moving=true; w.settings.moveStart=true; w.settings.jump=true;
        const float start=w.Offset();
        unsigned counted[size_t(Refusal::Count)]{};
        auto step=[&]{ const auto r=w.Step(); ++counted[size_t(r.refused)]; return r; };
        w.blocked=Refusal::NotOnFoot; w.stick=true;
        for (int i=0;i<90;++i) step();
        Check(w.Offset()==start && counted[size_t(Refusal::NotOnFoot)]==1,
            "in a seat (or a cinematic, third person, a menu) nothing moves, counted once per stick hold");
        w.stick=false; step(); w.stick=true;
        for (int i=0;i<10;++i) step();
        Check(counted[size_t(Refusal::NotOnFoot)]==2, "the next hold is a new refusal");
        w.stick=false; step();
        w.blocked=Refusal::Untracked; w.jump=true;
        for (int i=0;i<10;++i) { w.eye+=0.03f; step(); }
        w.jump=false; step();
        Check(w.Offset()==start && counted[size_t(Refusal::Untracked)]==1,
            "an untracked head refuses a jump recenter, counted once per press");
        // Blocked in the middle of a blend: it ends, and does not resume on its own when admitted again.
        w.blocked=Refusal::None; w.settings.moving=false; w.settings.jump=false;
        w.stick=true; step();
        for (int i=0;i<6;++i) step();
        const float part=w.Offset();
        Check(part<start && part>0.1f, "fixture: a start-of-movement blend is part of the way in");
        w.blocked=Refusal::NoInput; step();
        w.blocked=Refusal::None;
        for (int i=0;i<60;++i) step();
        Check(w.Offset()==part && w.recenter.blend==Mode::None,
            "a blend interrupted by a refusal is over: it never resumes on stale time");
        // Non-finite input is a refusal too, never a shift.
        w.settings.moving=true;
        w.head[0]=std::numeric_limits<float>::quiet_NaN();
        const auto bad=w.recenter.Update(w.now+kFrameUs,w.settings,Refusal::None,true,false,w.eye,
            w.head[0]-w.reference[0],w.head[2]-w.reference[2]);
        Check(bad.dx==0 && bad.dz==0 && bad.keep==1.0f, "a non-finite offset moves nothing");
    }

    // ---- With the body follow: hysteresis law (A) -------------------------------------------------------
    // Tracking +x (head right) is world -y for the body here, as in the follow's own tests.
    {
        RoomscaleFollow follow; ViewRecenter recenter;
        Settings settings; settings.moving=true; settings.movingSpeed=0.5f;
        float body[3]{}, head[3]{0,1.7f,0}, reference[3]{0,1.7f,0};
        float mx=0,my=0; uint64_t now=100;
        bool stick=false, walkedBack=false;
        float shifted=0;
        auto step=[&]{
            now+=11;
            const bool valid=follow.Update(3,now,true,stick,body,head,reference,0,-1,1,0,1.0f,mx,my);
            const auto shift=recenter.Update(now*1000,settings,valid ? Refusal::None : Refusal::NoFollow,stick,false,
                body[2],head[0]-reference[0],head[2]-reference[2]);
            if (shift.dx!=0 || shift.dz!=0)
            {
                reference[0]+=shift.dx; reference[2]+=shift.dz;
                follow.AcceptViewShift(reference,shift.keep);
                shifted+=std::hypot(shift.dx,shift.dz);
            }
            // The follow's command, in tracking space, must never point away from the head.
            if (mx!=0 || my!=0) walkedBack|=follow.error[0]*(head[0]-reference[0])<0;
            return valid;
        };
        step();
        // The player walks with the stick and steps 0.4 m to the right meanwhile.
        stick=true; head[0]=0.4f;
        for (int i=0;i<46;++i) { body[0]+=0.03f; step(); }                   // ~0.5 s of stick travel
        Check(std::fabs(shifted-0.25f)<0.01f && std::fabs((head[0]-reference[0])-0.15f)<0.01f,
            "law A: while the stick is held the recenter closes 0.25 m of a 0.4 m offset in 0.5 s at 0.5 m/s");
        Check(follow.seeded && follow.manualPaused && mx==0 && my==0,
            "law A: the follow stays suspended for the stick; it does not read the shift as a recenter");
        Check(reference[1]==1.7f, "law A: the reference's height is untouched");
        // Standing still: the follow owes exactly what the recenter left, and walks the legs to the head.
        stick=false;
        const float left=head[0]-reference[0];
        float walked=0; bool commanded=false;
        for (int i=0;i<400;++i)
        {
            step();
            if (mx!=0 || my!=0) { commanded=true; body[1]-=0.008f; walked+=0.008f; }
        }
        Check(commanded && std::fabs(follow.error[0])<=RoomscaleFollow::kFollowStart,
            "law A: standing still afterwards, the legs come to the head as before");
        Check(std::fabs(walked-left)<0.04f && walked<0.2f,
            "law A: the body walks only what the recenter left, not the whole 0.4 m (nothing counted twice)");
        Check(!walkedBack, "law A: the follow never walks the closed offset back");
    }
    {
        // A jump with no stick: the follow is already walking toward the head when the blend starts. Body
        // travel during the blend is still claimed once, and the two together never overshoot.
        RoomscaleFollow follow; ViewRecenter recenter;
        Settings settings; settings.jump=true; settings.jumpSeconds=0.20f;
        float body[3]{}, head[3]{0,1.7f,0}, reference[3]{0,1.7f,0};
        float mx=0,my=0; uint64_t now=100;
        bool jump=false, overshoot=false; float shifted=0, walked=0;
        auto step=[&]{
            now+=11;
            const float before=reference[0];
            follow.Update(3,now,true,false,body,head,reference,0,-1,1,0,1.0f,mx,my);
            const float claimed=reference[0]-before;
            const auto shift=recenter.Update(now*1000,settings,Refusal::None,false,jump,body[2],
                head[0]-reference[0],head[2]-reference[2]);
            if (shift.dx!=0 || shift.dz!=0)
            { reference[0]+=shift.dx; reference[2]+=shift.dz; follow.AcceptViewShift(reference,shift.keep); shifted+=shift.dx; }
            overshoot|=head[0]-reference[0]<-RoomscaleFollow::kFollowStart;
            return claimed;
        };
        step();
        head[0]=0.30f; step();
        Check(follow.following && (mx!=0 || my!=0), "fixture: a 0.3 m step has the follow walking");
        jump=true; float claimedDuringBlend=0;
        for (int i=0;i<40;++i)
        {
            body[2]+=0.03f;                                   // airborne
            if (mx!=0 || my!=0) { body[1]-=0.004f; walked+=0.004f; }   // air control answers a little
            claimedDuringBlend+=step();
        }
        Check(shifted>0.2f && std::fabs(head[0]-reference[0])<=RoomscaleFollow::kFollowStart,
            "law A: a jump blend closes the offset while the follow is walking");
        Check(std::fabs(claimedDuringBlend-walked)<0.005f && walked>0.01f,
            "law A: body travel during the blend is still claimed, once (the shift is not read as a recenter)");
        Check(std::fabs(shifted+walked-0.30f)<=RoomscaleFollow::kFollowStart && !overshoot,
            "law A: the blend and the walk together close the 0.3 m, never more");
        for (int i=0;i<60;++i) step();
        Check(mx==0 && my==0 && !follow.stalled, "law A: once closed, the follow rests (no stand-down left behind)");
    }
    {
        // A follow that stood down (the body did not answer) and a recenter that closes only part of the
        // offset: the stand-down's own threshold shrinks with it, so 3 cm further still restarts it.
        RoomscaleFollow follow;
        float body[3]{}, head[3]{0,1.7f,0}, reference[3]{0,1.7f,0};
        float mx=0,my=0; uint64_t now=100;
        auto step=[&]{now+=11;return follow.Update(3,now,true,false,body,head,reference,0,-1,1,0,1.0f,mx,my);};
        step(); head[0]=0.20f; step();
        for (int i=0;i<60;++i) step();
        Check(follow.stalled && std::fabs(follow.stalledAt-0.20f)<1e-4f, "fixture: the follow stood down 0.2 m out");
        reference[0]+=0.10f; follow.AcceptViewShift(reference,0.5f);
        step();
        Check(follow.stalled && mx==0 && my==0 && std::fabs(follow.error[0]-0.10f)<1e-4f,
            "a half-closed offset leaves a stood-down follow stood down, owing the other half");
        head[0]+=0.035f; step();
        Check((mx!=0 || my!=0) && follow.following, "and 3.5 cm further restarts it, measured from the shifted offset");
    }

    // ---- With the body follow: threshold law (B) ----------------------------------------------------------
    // This law keeps a neutral lean and re-captures it whenever it sees the reference change. Handing it
    // the shift keeps what is left owed; the twin that is not told bakes it into "under me" (the reason
    // AcceptViewShift exists).
    for (bool told : {true,false})
    {
        RoomscaleFollowOriginal follow; ViewRecenter recenter;
        Settings settings; settings.moving=true; settings.movingSpeed=0.5f;
        float body[3]{}, head[3]{0.2f,1.7f,0}, reference[3]{0,1.7f,0};   // seeded 0.2 m off: this law's lean
        float mx=0,my=0; uint64_t now=100; bool stick=false;
        auto step=[&]{
            now+=11;
            follow.Update(3,now,true,stick,body,head,reference,0,-1,1,0,1.0f,mx,my);
            const auto shift=recenter.Update(now*1000,settings,Refusal::None,stick,false,body[2],
                head[0]-reference[0],head[2]-reference[2]);
            if (shift.dx!=0 || shift.dz!=0)
            {
                reference[0]+=shift.dx; reference[2]+=shift.dz;
                if (told) follow.AcceptViewShift(reference,shift.keep);
            }
        };
        step();
        Check(std::fabs(follow.initialLean[0]-0.2f)<1e-5f, "fixture: law B seeds with a 0.2 m neutral lean");
        stick=true; head[0]=0.5f;                 // 0.5 m from the camera, 0.3 m of it owed
        for (int i=0;i<46;++i) { body[0]+=0.03f; step(); }
        const float offset=head[0]-reference[0];
        Check(std::fabs(offset-0.25f)<0.01f, "law B: 0.5 s at 0.5 m/s closes half of the 0.5 m to the camera");
        stick=false;
        for (int i=0;i<20;++i) step();
        if (told)
        {
            Check(std::fabs(follow.initialLean[0]-0.1f)<0.01f && std::fabs(follow.error[0]-0.15f)<0.01f,
                "law B: the neutral lean and what is owed shrink by the same share as the offset");
            Check(mx!=0 || my!=0, "law B: what is left is still walked once the stick is released");
        }
        else
            Check(std::fabs(follow.error[0])<1e-4f && mx==0 && my==0,
                "law B, not told (the control): the rest is baked into the lean and never walked");
    }
    {
        // All the way: zero offset to the camera means zero lean and nothing owed.
        RoomscaleFollowOriginal follow; ViewRecenter recenter;
        Settings settings; settings.moveStart=true; settings.moveStartSeconds=0.2f;
        float body[3]{}, head[3]{0.2f,1.7f,0.1f}, reference[3]{0,1.7f,0};
        float mx=0,my=0; uint64_t now=100; bool stick=false;
        auto step=[&]{
            now+=11;
            follow.Update(3,now,true,stick,body,head,reference,0,-1,1,0,1.0f,mx,my);
            const auto shift=recenter.Update(now*1000,settings,Refusal::None,stick,false,body[2],
                head[0]-reference[0],head[2]-reference[2]);
            if (shift.dx!=0 || shift.dz!=0)
            { reference[0]+=shift.dx; reference[2]+=shift.dz; follow.AcceptViewShift(reference,shift.keep); }
        };
        step(); head[0]=0.45f; stick=true;
        for (int i=0;i<40;++i) step();
        stick=false;
        for (int i=0;i<40;++i) step();
        Check(std::hypot(head[0]-reference[0],head[2]-reference[2])<1e-4f &&
              std::hypot(follow.initialLean[0],follow.initialLean[1])<1e-4f && mx==0 && my==0 && reference[1]==1.7f,
            "law B: a completed blend leaves the view on the camera, no lean and nothing to walk");
    }

    // ---- Settings bounds ------------------------------------------------------------------------------
    Check(ClampSeconds(0.0f,0.2f)==kMinSeconds && ClampSeconds(9.0f,0.2f)==kMaxSeconds &&
          ClampSeconds(std::numeric_limits<float>::quiet_NaN(),0.2f)==0.2f &&
          ClampSpeed(0.0f,0.5f)==kMinSpeed && ClampSpeed(50.0f,0.5f)==kMaxSpeed &&
          ClampSpeed(std::numeric_limits<float>::infinity(),0.5f)==0.5f,
        "durations and speeds are held to their ranges; non-finite values fall back to the default");
    Check(!Settings{}.Any(), "every recenter option defaults to off");
    return failures;
}
