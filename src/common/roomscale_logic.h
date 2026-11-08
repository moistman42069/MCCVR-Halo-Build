#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "runtime_types.h"

// Roomscale has its own positive native on-foot camera proof. The historical
// shared-feature gate is H3-only and is not this feature's admission policy.
inline bool RoomscaleGameplayEligible(GameTitle title, RuntimeMode mode) noexcept
{
    return mode == RuntimeMode::Gameplay &&
        (title == GameTitle::HaloCE || title == GameTitle::Halo2 || title == GameTitle::Halo3 ||
         title == GameTitle::Halo3ODST || title == GameTitle::HaloReach ||
         title == GameTitle::Halo4);
}

// Native locomotion owns collision, steps, ground support and networking.
// Consume observed follow motion and its bounded native stopping tail:
// body position + remaining tracked lean then represents one physical step.
struct RoomscaleFollow
{
    // Follow hysteresis (Halo: Campaign Evolved's rule): a catch-up starts only once the head is
    // kFollowStart from the body and, once started, runs until it is inside kFollowStop. With a
    // single 1.5 cm threshold, ordinary head sway fires a stream of ~1 cm body steps (measured in
    // Reach: a 2.5 cm lean moves the body 13 mm in one burst), each a walk-cycle blip. The view is
    // head-locked either way; only the collision body lags, by at most kFollowStart.
    static constexpr float kFollowStart=0.03f, kFollowStop=0.015f;
    // How long after the last command the body's own stopping tail may still be consumed as ours (it
    // also ends after 150 ms without motion). Commands stop at the edge of the stop band, so this
    // covers a lagged native walker coasting in from there; it is still a hard cap, so later platform
    // or physics travel is never claimed.
    static constexpr uint64_t kSettleMaxMs=700;
    // The longest a released stick keeps roomscale paused. The body's braking tail after a stick walk
    // is a few hundred milliseconds; the quiet test below cannot tell it from motion that never stops.
    // Without the cap, after a stick walk the follow can stay admitted on every poll and never command
    // for up to 10 s while the player walks a metre away from the body (measured in Reach). The only
    // gate left in that state is this quiet test, and Reach's body point is the character's EYE, which
    // moves as the aim turns him (inferred, not measured: any sub-millimetre motion per frame holds the
    // pause open). Past this, the pause ends anyway.
    static constexpr uint64_t kManualPauseMaxMs=500;
    // THE OWED DISTANCE SURVIVES INTERRUPTIONS. initialLean is the head-to-reference offset the follow
    // treats as "the body is under me". It is captured only when the player is known to be over the
    // body: the first seed of a title generation, and a recenter (the reference moved to the head).
    // Every other re-seed -- a stale camera, an admission change, a tracking gap, a hitch, a teleport --
    // keeps it, so a distance walked away while the follow could not act is still owed and the body
    // catches up (Halo: Campaign Evolved's standing origin behaves the same way). Capturing the current
    // offset at every re-seed instead would make that distance the new "under me": the legs would
    // still follow the player's steps, but from wherever the interruption left them, never back
    // under the head.
    bool neutralValid=false;
    uint32_t neutralGeneration=0;
    // What is NOT owed: a tracking fault. A head that moves further between two consecutive updates
    // than a person can (a jump) is folded into the REFERENCE, and a debt larger than any play space is
    // dropped the same way. Neither is a walk. Folding into the reference (not the neutral lean) keeps
    // the view where it was -- the view is built from head minus reference -- so the player stays over
    // the body; folding into the lean instead lets the view jump with the fault while the legs stay
    // behind for good (far from the player, and never walked back).
    static constexpr float kHeadJumpM=0.5f, kHeadMaxSpeed=6.0f, kMaxDebtM=3.0f;
    bool haveLastHead=false;
    uint64_t lastHeadMs=0;
    float lastHead[2]{};
    // A UNIT THAT IGNORES THE STICK IS NOT PUSHED, AND WHAT IT DOES MEANWHILE IS NOT OURS. The game can
    // stop answering for seconds at a time (measured in Reach: the character unresponsive for 2-7 s, five
    // times in one minute; the game keeps ticking, only the player's control update stops). A follow
    // that commands into him the whole time sees the body point jump as control comes back (the error
    // changes sign within 100 ms). Consumed as a walk, that sends the follow chasing it at full stick
    // for ~3 s: the body 2.2 m out and back. So a command run that has not moved the body kNoResponseM the way it
    // pushes in any kNoResponseMs of the run stands the follow down: no command, nothing claimed. Any
    // window, not only the first: a run that walks most of the way and then stops answering short of
    // the stop band has its whole walk behind it (measured: Halo 3, Halo 2 and CE
    // steps stop 16-27 mm short with a 0.06-0.09 command running on; the walker's own braking never
    // zeroes the command, so the run would never restart). If the body point
    // moves meanwhile (control handed back, a camera settling), the follow resumes once it has been
    // still for kStallQuietMs (kStallMaxMs at the latest). If it never moves (a deadzone above the
    // stick floor, a wall, a lock that hands back quietly), the follow restarts only when the body is
    // under the head again or the head is a further kFollowStart away than where it stood down --
    // never on a timer, which would push into a wall every few seconds (measured in Halo 3: a 27 mm
    // residual commands a stick the unit never moves for). The
    // run after either is a probe: until it has moved the body kProbeConfirmM the way it pushes, a step
    // longer than kProbeStepM, or one more across the push than along it, is the game moving him, not
    // our step, and stands it down again. What is owed is kept throughout. The body point arrives in
    // sim-tick chunks (30 Hz), so these are per-update distances, never per-update speeds.
    static constexpr uint64_t kNoResponseMs=500, kStallQuietMs=150, kStallMaxMs=4000;
    static constexpr float kNoResponseM=0.02f, kStallStillM=0.003f, kProbeStepM=0.15f, kProbeConfirmM=0.05f;
    bool stalled=false, stallMoved=false, probing=false;
    uint64_t stallSince=0, stallQuietSince=0, windowSince=0;
    float runProgress=0;   // travel the way the current command run pushes (m, net)
    float windowProgress=0;   // the same, in the run's current kNoResponseMs window (m, net)
    float stalledAt=0;     // the follow error when it stood down for want of a response (m)
    // What a guard did on the last update, for the caller's report. Not state.
    enum class Guard : uint8_t { None, StoodDown, NotOurs };
    Guard guard=Guard::None;
    bool seeded=false, commanded=false, settling=false;
    bool manualPaused=false, following=false;
    uint32_t generation=0;
    uint64_t time=0, lastCommand=0, lastMotion=0;
    uint64_t manualQuietSince=0, manualSince=0;
    float body[2]{}, expectedReference[2]{}, initialLean[2]{};
    float headForward[2]{}, worldForward[2]{}, error[2]{}, velocity[2]{};
    // Tracking-space direction of the last nonzero command: the way the stopping tail must keep moving
    // to be claimed as ours. priorTailDir is the command before this run, kept for the whole run: a body
    // still coasting the old way after the command turns is still our step.
    float tailDir[2]{}, priorTailDir[2]{};

    // Forget the follow in progress -- after an admission change its movement receipts cannot be
    // trusted -- but keep what is owed: the neutral lean, and the reference it was measured against
    // (so a recenter during the interruption is still recognised).
    void Rebase() noexcept
    {
        const float lean0=initialLean[0], lean1=initialLean[1];
        const float ref0=expectedReference[0], ref1=expectedReference[1];
        const bool keep=neutralValid;
        const uint32_t keepGeneration=neutralGeneration;
        *this=RoomscaleFollow{};
        initialLean[0]=lean0; initialLean[1]=lean1;
        expectedReference[0]=ref0; expectedReference[1]=ref1;
        neutralValid=keep; neutralGeneration=keepGeneration;
    }

    void SuspendForManual(uint64_t now) noexcept
    {
        // Manual travel cannot be attributed to a prior physical-follow
        // packet. Retain the tracked goal, but retire that movement receipt.
        // A follow the stick interrupted is over too: once native travel is
        // quiet again, a catch-up must re-cross kFollowStart like any other,
        // or a residual inside the start band would resume walking.
        commanded=settling=following=false;velocity[0]=velocity[1]=0;
        manualPaused=true;manualQuietSince=manualSince=now;
        stalled=probing=false;runProgress=windowProgress=0;
    }

    // The unit is not responding to the follow (or is being moved by something else): command
    // nothing, claim nothing, until the body point is still again.
    void StandDown(uint64_t now,bool moved) noexcept
    {
        stalled=true; stallMoved=moved; stallSince=stallQuietSince=now;
        commanded=settling=following=false; velocity[0]=velocity[1]=0; runProgress=windowProgress=0;
    }

    // THE VIEW RECENTER MOVED THE REFERENCE (roomscale_recenter_logic.h): the play area slid toward the
    // head, on purpose, leaving `keep` of the head-to-reference offset. That is not a recenter the follow
    // should notice (it would end the follow in progress), and not travel (the body did not move): it is
    // simply the new truth. Everything measured against the old offset shrinks with it -- the error, the
    // neutral lean, and the error a stand-down is waiting to see grow from -- so what is left is still
    // owed, in the same direction, and nothing is walked back or counted twice. Call it after Update,
    // with the reference as it now stands.
    void AcceptViewShift(const float reference[3],float keep) noexcept
    {
        expectedReference[0]=reference[0]; expectedReference[1]=reference[2];
        initialLean[0]*=keep; initialLean[1]*=keep;
        error[0]*=keep; error[1]*=keep; stalledAt*=keep;
    }

    bool Update(uint32_t epoch,uint64_t now,bool enabled,bool manualMove,
        const float position[3],const float head[3],float reference[3],
        float hx,float hz,float wx,float wy,float scale,float& moveX,float& moveY) noexcept
    {
        moveX=moveY=0;
        guard=Guard::None;
        const float values[]{position[0],position[1],head[0],head[2],reference[0],
            reference[2],hx,hz,wx,wy,scale};
        for (float v:values) if (!std::isfinite(v)) { seeded=false; commanded=settling=manualPaused=following=false; return false; }
        const float hl=std::hypot(hx,hz),wl=std::hypot(wx,wy);
        if (!enabled || !epoch || !now || scale<=0 || hl<0.001f || wl<0.001f)
        { seeded=false; commanded=settling=manualPaused=following=false; return false; }
        hx/=hl; hz/=hl; wx/=wl; wy/=wl;
        const bool newGeneration=!neutralValid || neutralGeneration!=epoch;
        const bool recentered=neutralValid &&
            (std::fabs(reference[0]-expectedReference[0])>0.0001f ||
             std::fabs(reference[2]-expectedReference[1])>0.0001f);
        const bool reset=!seeded || generation!=epoch || now<time || now-time>250 || recentered ||
            std::hypot(position[0]-body[0],position[1]-body[1])/scale>0.35f;
        if (reset)
        {
            // Only a new title generation or a recenter says the player is over the body now. A NEW
            // GENERATION moves the reference to the head, as a position recenter does: the title captures
            // its reference earlier -- at the level's first decodable pose, tracked or not, before a
            // cutscene, before the headset comes back on -- and whatever the head moved since then would
            // otherwise be baked into the lean, legs off to one side until a manual recenter (Halo 4:
            // 0.55 m measured at a level start).
            if (newGeneration)
            {
                reference[0]=head[0]; reference[2]=head[2];
                initialLean[0]=initialLean[1]=0;
                neutralValid=true; neutralGeneration=epoch;
            }
            else if (recentered)
            {
                // A recenter put the reference on the head at that instant: the player was over the body
                // THEN, so the lean is zero. Noticed late -- it landed while the follow was not admitted
                // (F1, a seat, a pause; ODST recenters at every unpause) -- whatever the head moved since is
                // owed like any other walk. Capturing head - reference here would bake that into the lean for good.
                initialLean[0]=initialLean[1]=0;
                neutralValid=true; neutralGeneration=epoch;
            }
            commanded=settling=following=false; velocity[0]=velocity[1]=0;
            lastCommand=lastMotion=now; seeded=true;
            manualPaused=false;
            // After any OTHER re-seed -- an admission change, a stale camera, a tracking gap, a teleport --
            // the first run is a probe: until it has moved the body the way it pushes, a jump or travel
            // across the push is the game moving him. Halo 4 leaving third person re-admits while its
            // camera is still gliding back to the eye; claimed as a walk, that glide moves the reference
            // with it and leaves the view hanging back while the body walks off.
            stalled=false; probing=!(newGeneration || recentered); runProgress=windowProgress=0;
            haveLastHead=false;   // no jump test across a re-seed: the head may really have moved
        }
        else if (haveLastHead && now>=lastHeadMs)
        {
            const float jx=head[0]-lastHead[0], jz=head[2]-lastHead[1];
            const float limit=std::max(kHeadJumpM, kHeadMaxSpeed*float(now-lastHeadMs)*0.001f);
            if (std::hypot(jx,jz)>limit)
            {
                // A tracking jump: fold it into the reference, so it is never walked and the view does
                // not jump with it.
                reference[0]+=jx; reference[2]+=jz;
                velocity[0]=velocity[1]=0; following=false;
            }
        }
        lastHead[0]=head[0]; lastHead[1]=head[2]; lastHeadMs=now; haveLastHead=true;
        if (manualMove) SuspendForManual(now);
        else if (manualPaused)
        {
            // A released stick may still be braking. Wait for observed native
            // motion to remain quiet before creating a new follow receipt;
            // continuing platform/camera movement extends the wait, up to
            // kManualPauseMaxMs after the stick let go.
            if (std::hypot(position[0]-body[0],position[1]-body[1])/scale>0.0001f)
                manualQuietSince=now;
            const bool quietEnd=now>=manualQuietSince&&now-manualQuietSince>=150;
            const bool capEnd=now>=manualSince&&now-manualSince>=kManualPauseMaxMs;
            if (quietEnd || capEnd)
            {
                manualPaused=false;
                // Ended by the cap, the body may still be moving on its own: the next run is a probe.
                probing=!quietEnd;
            }
        }
        else if (stalled)
        {
            // Stood down (see kNoResponseMs): claim nothing. Once the body point has moved, resume
            // when it is still again; if it never moves, the head decides (below, with the error).
            if (std::hypot(position[0]-body[0],position[1]-body[1])/scale>kStallStillM)
            { stallMoved=true; stallQuietSince=now; }
            const uint64_t stood=now>=stallSince ? now-stallSince : 0;
            const bool still=now>=stallQuietSince && now-stallQuietSince>=kStallQuietMs;
            if (stallMoved && (still || stood>=kStallMaxMs)) { stalled=false; probing=true; }
            velocity[0]=velocity[1]=0;
        }
        else if (!reset && (commanded || (settling && now>=lastCommand && now-lastCommand<=kSettleMaxMs &&
            now>=lastMotion && now-lastMotion<=150)))
        {
            const float dx=(position[0]-body[0])/scale;
            const float dy=(position[1]-body[1])/scale;
            const float forward=dx*worldForward[0]+dy*worldForward[1];
            const float right=dx*worldForward[1]-dy*worldForward[0];
            const float tx=forward*headForward[0]-right*headForward[1];
            const float tz=forward*headForward[1]+right*headForward[0];
            const float along=tx*tailDir[0]+tz*tailDir[1];
            const float across=std::fabs(tx*tailDir[1]-tz*tailDir[0]);
            const float stepLength=std::hypot(tx,tz);
            // Native acceleration and braking can move after the last nonzero
            // stick request, including past the target. Account for that bounded
            // tail in the SAME tracking frame instead of adding it to the view.
            // Epoch/recenter/teleport/manual-input guards above still abandon
            // authority. Never consume a requested distance before it occurred.
            // The tail (no command on the last update) is ours only while it keeps moving the way we
            // walked: travel across or against that direction is a platform, a knockback or other
            // physics, and it ends the tail unclaimed (the view then carries it, as it should).
            if (commanded && probing &&
                (stepLength>kProbeStepM || (stepLength>kStallStillM && across>along)))
            {
                // Probing a unit that just ignored the stick: a jump, or travel across or against the
                // push, is the game moving him, not our step. The view carries it, as for a knockback.
                StandDown(now,true); guard=Guard::NotOurs;
            }
            else if (!commanded && (tx!=0 || tz!=0) && along<=0)
            {
                settling=false; velocity[0]=velocity[1]=0;
            }
            else if (commanded && along<-kStallStillM &&
                tx*priorTailDir[0]+tz*priorTailDir[1]<-kStallStillM)
            {
                // Travel AGAINST the push while we push -- against the previous command's direction too,
                // so it is not the body still coasting the old way after the command turned -- is the
                // game moving him (a knockback, a camera settling back to the eye), never our step. Not
                // claimed: the view carries it, and the follow keeps pushing toward the head.
                velocity[0]=velocity[1]=0; guard=Guard::NotOurs;
            }
            else
            {
                reference[0]+=tx; reference[2]+=tz;
                const float elapsed=now>time?float(now-time)*.001f:0;
                if(elapsed>0)
                {
                    const float blend=1-std::exp(-elapsed/.06f);
                    velocity[0]+=(tx/elapsed-velocity[0])*blend;
                    velocity[1]+=(tz/elapsed-velocity[1])*blend;
                }
                if(tx!=0 || tz!=0)lastMotion=now;
                if (commanded)
                {
                    runProgress+=along; windowProgress+=along;
                    if (probing && runProgress>=kProbeConfirmM) probing=false;
                }
            }
        }
        else velocity[0]=velocity[1]=0;
        error[0]=head[0]-reference[0]-initialLean[0];
        error[1]=head[2]-reference[2]-initialLean[1];
        const float distance=std::hypot(error[0],error[1]);
        if (distance>kMaxDebtM)
        {
            // More than any play space: a tracking fault, never a long unattended walk. The reference
            // takes it, so the view returns over the body instead of keeping the fault.
            reference[0]=head[0]-initialLean[0];
            reference[2]=head[2]-initialLean[1];
            error[0]=error[1]=0;velocity[0]=velocity[1]=0;settling=following=false;
        }
        else
        {
            // A stand-down the body point never moved through ends when the body is under the head
            // again, or when the head is a further kFollowStart away than where it stood down.
            if (stalled && !stallMoved && (distance<=kFollowStop || distance>stalledAt+kFollowStart))
            { stalled=false; probing=distance>kFollowStop; }
            following=!manualMove && !manualPaused && !stalled &&
                (distance>kFollowStart || (following && distance>kFollowStop));
            // Command ONLY while a follow is running. Once it ends inside kFollowStop, the game's own
            // deceleration finishes the step: `settling` below still consumes that tail into the
            // reference, but a command here would be floored by MCC to a >=27% stick -- a fresh walk
            // step inside the band the hysteresis exists to keep quiet. A real overshoot past
            // kFollowStart starts a new follow, which corrects it.
            if (following)
            {
                // Position feedback follows promptly; measured native velocity
                // brakes before overshoot. A short filter tolerates 30 Hz body
                // updates under higher-frequency camera callbacks.
                float cx=error[0]*3.5f-velocity[0]*.18f;
                float cz=error[1]*3.5f-velocity[1]*.18f;
                // Brake by RELEASING, never by reversing. Near the target the velocity term turns
                // the command against the error, and MCC's move-stick floor (any nonzero command
                // is sent as >=27% stick) makes that a real walk the other way: measured in Reach,
                // a 0.3 m step brakes from 4 mm into an 18 mm reverse residual. Releasing lets
                // the game's own deceleration stop the unit. A genuine overshoot still corrects,
                // because the error itself has flipped and the command again points along it.
                if(cx*error[0]+cz*error[1]<=0){cx=cz=0;}
                const float magnitude=std::hypot(cx,cz);
                if(magnitude>0)
                {
                    // The previous command for the against-push rule is taken when a run starts and kept
                    // through it: MCC's deceleration spans a few 30 Hz body chunks, several updates each, so
                    // a direction replaced every update would stop covering the coast after one. A run that
                    // starts from rest has no earlier direction to coast along; one that starts while the last
                    // run's tail is still settling keeps that run's (an overshoot correction).
                    const float nx=cx/magnitude, nz=cz/magnitude;
                    if(!commanded)
                    {
                        priorTailDir[0]=settling?tailDir[0]:nx;
                        priorTailDir[1]=settling?tailDir[1]:nz;
                    }
                    tailDir[0]=nx;tailDir[1]=nz;
                }
                if(magnitude>.8f){cx*=.8f/magnitude;cz*=.8f/magnitude;}
                moveX=-cx*hz+cz*hx;
                moveY=cx*hx+cz*hz;
            }
        }
        // A command run that has not moved the body the way it pushes: the unit is ignoring the stick.
        const bool wantsMove=moveX!=0 || moveY!=0;
        if (wantsMove && !commanded) { windowSince=now; runProgress=windowProgress=0; }
        else if (wantsMove && now>=windowSince && now-windowSince>=kNoResponseMs)
        {
            if (windowProgress<kNoResponseM)
            {
                StandDown(now,false); guard=Guard::StoodDown; stalledAt=distance;
                moveX=moveY=0;
            }
            else { windowSince=now; windowProgress=0; }   // it answered in this window: measure the next
        }
        commanded=moveX!=0 || moveY!=0;
        if(commanded)lastCommand=now;
        // Do not claim unrelated later platform/physics travel. Settling ends
        // after a quiet interval and always expires after half a second without
        // a command. Fresh input/tracking admission is still required outside.
        settling=commanded || (settling && now>=lastCommand && now-lastCommand<=kSettleMaxMs &&
            now>=lastMotion && now-lastMotion<=150);
        generation=epoch; time=now;
        body[0]=position[0]; body[1]=position[1];
        expectedReference[0]=reference[0]; expectedReference[1]=reference[2];
        headForward[0]=hx; headForward[1]=hz;
        worldForward[0]=wx; worldForward[1]=wy;
        return true;
    }
};

// THE THRESHOLD FOLLOW LAW (commit 56fed9e, as it shipped in Alpha 0.6.0), kept VERBATIM for the live A/B
// switch roomscale_follow_law = 0: walks from 1.5 cm and brakes by reversing. RoomscaleFollow above is the
// hysteresis law (default): 3 cm start / 1.5 cm stop, brake by release, command only while a follow runs,
// the stopping tail claimed only in the walked direction, the 500 ms pause cap, and an owed distance that
// survives interruptions. Do not "fix" this copy: its whole purpose is to be exactly what shipped before, so
// a player can compare the two.
struct RoomscaleFollowOriginal
{
    bool seeded=false, commanded=false, settling=false;
    bool manualPaused=false;
    uint32_t generation=0;
    uint64_t time=0, lastCommand=0, lastMotion=0;
    uint64_t manualQuietSince=0;
    float body[2]{}, expectedReference[2]{}, initialLean[2]{};
    float headForward[2]{}, worldForward[2]{}, error[2]{}, velocity[2]{};

    void SuspendForManual(uint64_t now) noexcept
    {
        // Manual travel cannot be attributed to a prior physical-follow
        // packet. Retain the tracked goal, but retire that movement receipt.
        commanded=settling=false;velocity[0]=velocity[1]=0;
        manualPaused=true;manualQuietSince=now;
    }

    // Not part of the law as it shipped: only the view recenter calls this, and it is off by default.
    // This law re-captures its neutral lean whenever it sees the reference change, which would turn
    // whatever the recenter had not closed yet into "under me" for good. So the recenter hands it the
    // moved reference and shrinks the lean by the same share as the offset (RoomscaleFollow's twin).
    void AcceptViewShift(const float reference[3],float keep) noexcept
    {
        expectedReference[0]=reference[0]; expectedReference[1]=reference[2];
        initialLean[0]*=keep; initialLean[1]*=keep;
        error[0]*=keep; error[1]*=keep;
    }

    bool Update(uint32_t epoch,uint64_t now,bool enabled,bool manualMove,
        const float position[3],const float head[3],float reference[3],
        float hx,float hz,float wx,float wy,float scale,float& moveX,float& moveY) noexcept
    {
        moveX=moveY=0;
        const float values[]{position[0],position[1],head[0],head[2],reference[0],
            reference[2],hx,hz,wx,wy,scale};
        for (float v:values) if (!std::isfinite(v)) { seeded=false; commanded=settling=manualPaused=false; return false; }
        const float hl=std::hypot(hx,hz),wl=std::hypot(wx,wy);
        if (!enabled || !epoch || !now || scale<=0 || hl<0.001f || wl<0.001f)
        { seeded=false; commanded=settling=manualPaused=false; return false; }
        hx/=hl; hz/=hl; wx/=wl; wy/=wl;
        const bool reset=!seeded || generation!=epoch || now<time || now-time>250 ||
            std::fabs(reference[0]-expectedReference[0])>0.0001f ||
            std::fabs(reference[2]-expectedReference[1])>0.0001f ||
            std::hypot(position[0]-body[0],position[1]-body[1])/scale>0.35f;
        if (reset)
        {
            initialLean[0]=head[0]-reference[0];
            initialLean[1]=head[2]-reference[2];
            commanded=settling=false; velocity[0]=velocity[1]=0;
            lastCommand=lastMotion=now; seeded=true;
            manualPaused=false;
        }
        if (manualMove) SuspendForManual(now);
        else if (manualPaused)
        {
            // A released stick may still be braking. Wait for observed native
            // motion to remain quiet before creating a new follow receipt;
            // continuing platform/camera movement extends this bounded wait.
            if (std::hypot(position[0]-body[0],position[1]-body[1])/scale>0.0001f)
                manualQuietSince=now;
            if (now>=manualQuietSince&&now-manualQuietSince>=150)
                manualPaused=false;
        }
        else if (!reset && (commanded || (settling && now>=lastCommand && now-lastCommand<=500 &&
            now>=lastMotion && now-lastMotion<=150)))
        {
            const float dx=(position[0]-body[0])/scale;
            const float dy=(position[1]-body[1])/scale;
            const float forward=dx*worldForward[0]+dy*worldForward[1];
            const float right=dx*worldForward[1]-dy*worldForward[0];
            const float tx=forward*headForward[0]-right*headForward[1];
            const float tz=forward*headForward[1]+right*headForward[0];
            // Native acceleration and braking can move after the last nonzero
            // stick request, including past the target. Account for that bounded
            // tail in the SAME tracking frame instead of adding it to the view.
            // Epoch/recenter/teleport/manual-input guards above still abandon
            // authority. Never consume a requested distance before it occurred.
            reference[0]+=tx; reference[2]+=tz;
            const float elapsed=now>time?float(now-time)*.001f:0;
            if(elapsed>0)
            {
                const float blend=1-std::exp(-elapsed/.06f);
                velocity[0]+=(tx/elapsed-velocity[0])*blend;
                velocity[1]+=(tz/elapsed-velocity[1])*blend;
            }
            if(tx!=0 || tz!=0)lastMotion=now;
        }
        else velocity[0]=velocity[1]=0;
        error[0]=head[0]-reference[0]-initialLean[0];
        error[1]=head[2]-reference[2]-initialLean[1];
        const float distance=std::hypot(error[0],error[1]);
        if (distance>1.2f)
        {
            // A tracking jump must not become a long unattended walk.
            initialLean[0]=head[0]-reference[0];
            initialLean[1]=head[2]-reference[2];
            error[0]=error[1]=0;velocity[0]=velocity[1]=0;settling=false;
        }
        else if (!manualMove && !manualPaused && (distance>0.015f ||
            (settling && std::hypot(velocity[0],velocity[1])>.05f)))
        {
            // Position feedback follows promptly; measured native velocity
            // brakes before overshoot. A short filter tolerates 30 Hz body
            // updates under higher-frequency camera callbacks.
            float cx=error[0]*3.5f-velocity[0]*.18f;
            float cz=error[1]*3.5f-velocity[1]*.18f;
            const float magnitude=std::hypot(cx,cz);
            if(magnitude>.8f){cx*=.8f/magnitude;cz*=.8f/magnitude;}
            moveX=-cx*hz+cz*hx;
            moveY=cx*hx+cz*hz;
        }
        commanded=moveX!=0 || moveY!=0;
        if(commanded)lastCommand=now;
        // Do not claim unrelated later platform/physics travel. Settling ends
        // after a quiet interval and always expires after half a second without
        // a command. Fresh input/tracking admission is still required outside.
        settling=commanded || (settling && now>=lastCommand && now-lastCommand<=500 &&
            now>=lastMotion && now-lastMotion<=150);
        generation=epoch; time=now;
        body[0]=position[0]; body[1]=position[1];
        expectedReference[0]=reference[0]; expectedReference[1]=reference[2];
        headForward[0]=hx; headForward[1]=hz;
        worldForward[0]=wx; worldForward[1]=wy;
        return true;
    }
};
