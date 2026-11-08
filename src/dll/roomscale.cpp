#include "roomscale.h"
#include "vr.h"
#include "../common/roomscale_logic.h"
#include "../common/roomscale_recenter_logic.h"
#include "../common/director_camera_logic.h"
#include "../common/physical_running_logic.h"
#include "../common/config.h"
#include "../common/log.h"
#include "title_adapter.h"
#include <windows.h>
#include <atomic>
#include <bit>
#include <cstdio>

namespace {
// Re-enabled after title admission and nested XInput cancellation fixes.
constexpr bool kEnableRoomscaleBodyFollow = true;
std::atomic<uint64_t> inputAt{0}, commandAt{0}, command{0};
std::atomic<bool> inputAllowed{false}, manualMove{false};
std::atomic<uint32_t> inputEpoch{1}, historyEpoch{1}, commandEpoch{0}, commandGeneration{0};
std::atomic<int> commandTitle{0};
std::atomic<uint32_t> version{0};
std::atomic_flag publishing = ATOMIC_FLAG_INIT;
std::atomic<uint64_t> admitted{0}, refused{0}, consumed{0};
std::atomic<uint64_t> nativeBlocked{0}, trackingBlocked{0}, inputBlocked{0};
std::atomic<uint64_t> manualSamples{0}, demandSamples{0}, travelMm{0};
// Follow quality for the report: how far the head is from where the body will be (the controller's
// error), the largest in the window, and the largest stick command issued.
std::atomic<uint64_t> errorLastMm{0}, errorMaxMm{0}, commandMaxMilli{0};
// The A/B switch (roomscale_follow_law): the law the camera hook last ran, for the report.
std::atomic<int> lawInEffect{-1};
// RoomscaleFollow's guards (see kNoResponseMs): stand-downs, and probe travel it refused to claim.
std::atomic<uint64_t> guardStoodDown{0}, guardNotOurs{0};
#if HALOMCCVR_DEV_DIAGNOSTICS
// Follow trace (dev builds only): one sample per ~100 ms while a follow is in progress (error past the
// deadband, a command, or native travel still settling). Written only by the camera hook under
// `publishing`, dumped by the status tick, so a catch-up that lurches, overshoots or stalls shows its
// shape in the log. The 64-slot ring holds ~6 s, three report periods.
struct FollowSample { uint64_t at; float errorX, errorZ, command, speed; };
constexpr unsigned kFollowTrace = 64;
FollowSample followTrace[kFollowTrace]{};
std::atomic<unsigned> followHead{0};
uint64_t followSampledAt = 0;
#endif
// THE VIEW RECENTER (roomscale_recenter_logic.h). The XInput hook says when the jump action is down; the
// camera hook owns everything else. Counters are for the report: micrometres closed per mode (jump,
// start of movement, while moving), quick blends begun, takeoffs the jump witness saw, the head-to-camera
// offset now, and wanted recenters refused, by reason.
std::atomic<uint64_t> jumpHeldAt{0};
std::atomic<uint64_t> recenterClosedUm[3]{}, recenterBlends[2]{}, recenterTakeoffs{0}, recenterOffsetMm{0};
std::atomic<uint64_t> recenterRefused[size_t(roomscale_recenter::Refusal::Count)]{};
// THE ADMISSION, BY REASON (roomscale_admission_logic.h). `blocked native` above is one number for a seat, a
// cinematic, a death camera, a menu and an unarmed player alike; the camera hook counts which reading refused,
// and keeps the first-person rungs as they stood at the last refusal (camera / Monitor / view model) and the
// title they belong to. The Monitor predicate faulting is reported once per title generation.
std::atomic<uint64_t> admissionRefused[size_t(roomscale_admission::Refusal::Count)]{};
std::atomic<uint32_t> admissionLastRungs{0};   // rungs | refusal << 8 | title << 16
std::atomic<uint32_t> admissionFaultGeneration{0};
std::atomic<int> admissionFaultTitle{0};
// The camera rung's value agreement (director_camera::RungTrust; Reach / Halo 4 / Halo 2), as the verdicts carry
// it: generation << 32 | title << 8 | proven (bit 0) | condemned (bit 1). The report says each change once.
std::atomic<uint64_t> admissionRungTrust{0};
// The blend is integrated at frame rate, and GetTickCount64 ticks in 15.6 ms steps: at 90 Hz that is
// one or two frames with no time and then a frame with two frames' worth, which would show as the
// world stepping during the slide. A test build supplies its own clock.
#ifndef ROOMSCALE_NOW_US
uint64_t RoomscaleNowUs() noexcept
{
    static const int64_t frequency=[]{ LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f.QuadPart>0 ? f.QuadPart : 1; }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return uint64_t(now.QuadPart/frequency)*1'000'000ull+uint64_t(now.QuadPart%frequency)*1'000'000ull/uint64_t(frequency);
}
#define ROOMSCALE_NOW_US RoomscaleNowUs
#endif
roomscale_recenter::Settings RecenterSettings() noexcept
{
    roomscale_recenter::Settings settings{};
    if (!kEnableRoomscaleBodyFollow || !g_config.roomscale_movement) return settings;
    settings.jump=g_config.roomscale_recenter_jump;
    settings.jumpSeconds=g_config.roomscale_recenter_jump_s;
    settings.moveStart=g_config.roomscale_recenter_move_start;
    settings.moveStartSeconds=g_config.roomscale_recenter_move_start_s;
    settings.moving=g_config.roomscale_recenter_moving;
    settings.movingSpeed=g_config.roomscale_recenter_moving_speed;
    return settings;
}
std::atomic<uint32_t> runVersion{0},runGeneration{0},runValue{0};
std::atomic<int> runTitle{0};
std::atomic<uint64_t> runAt{0};
}

bool PhysicalRunning_Move(float& x,float& y) noexcept
{
    if(!g_config.physical_running||!VR_RoomscaleTrackingFresh()||
       TitleAdapter_GetRuntimeMode()!=RuntimeMode::Gameplay)return false;
    const auto v=runVersion.load(std::memory_order_acquire);
    if(v&1)return false;
    const auto at=runAt.load(std::memory_order_relaxed);
    const auto title=static_cast<GameTitle>(runTitle.load(std::memory_order_relaxed));
    const auto generation=runGeneration.load(std::memory_order_relaxed);
    const float value=std::bit_cast<float>(runValue.load(std::memory_order_relaxed));
    const auto now=GetTickCount64();
    if(runVersion.load(std::memory_order_acquire)!=v||!at||now<at||now-at>100||
       title!=TitleAdapter_GetActiveTitle()||!generation||generation!=TitleAdapter_GetGeneration(title)||
       !std::isfinite(value)||value<=.01f||value>1)return false;
    x=0;y=value;return true;
}

void Roomscale_Input(bool allowed,float x,float y) noexcept
{
    const auto now=GetTickCount64();
    const auto previousAt=inputAt.load(std::memory_order_acquire);
    const bool expired=previousAt&&(now<previousAt||now-previousAt>100);
    allowed=kEnableRoomscaleBodyFollow && allowed && g_config.roomscale_movement && VR_RoomscaleTrackingFresh();
    const bool manual=!std::isfinite(x)||!std::isfinite(y)||x*x+y*y>0.02f;
    const bool allowedChanged=inputAllowed.exchange(allowed,std::memory_order_acq_rel)!=allowed;
    const bool manualChanged=manualMove.exchange(manual,std::memory_order_acq_rel)!=manual;
    // Retire the command even when a short stick press/release occurs entirely
    // between camera callbacks. That camera cannot observe the manual interval
    // and must not replay a pre-stick body-follow packet after release.
    if (allowedChanged || manualChanged || expired)
        inputEpoch.fetch_add(1,std::memory_order_acq_rel);
    // Admission loss ends the follow in progress (the camera rebases it, keeping what is owed).
    // Manual movement only retires the packet; the camera can retain the tracked step until native
    // travel stops.
    if (allowedChanged || expired) historyEpoch.fetch_add(1,std::memory_order_acq_rel);
    inputAt.store(now,std::memory_order_release);
}

// The jump action as the game is being fed it on this poll. Only a held state is stamped: a poll path
// that never reports (a menu, the exclusive route) simply lets the stamp go stale.
void Roomscale_JumpInput(bool held) noexcept
{
    if (held) jumpHeldAt.store(GetTickCount64(),std::memory_order_release);
}

bool Roomscale_Move(float& x,float& y) noexcept
{
    if (!g_config.roomscale_movement || !VR_RoomscaleTrackingFresh() || !std::isfinite(x) || !std::isfinite(y) ||
        x*x+y*y>0.02f || manualMove.load(std::memory_order_acquire) ||
        !inputAllowed.load(std::memory_order_acquire)) return false;
    const auto now=GetTickCount64();
    const auto inputTime=inputAt.load(std::memory_order_acquire);
    if (!inputTime || now<inputTime || now-inputTime>100) return false;
    const auto before=version.load(std::memory_order_acquire);
    if (before&1u) return false;
    const auto at=commandAt.load(std::memory_order_relaxed);
    const auto title=static_cast<GameTitle>(commandTitle.load(std::memory_order_relaxed));
    const auto generation=commandGeneration.load(std::memory_order_relaxed);
    const auto epoch=commandEpoch.load(std::memory_order_relaxed);
    const uint64_t value=command.load(std::memory_order_relaxed);
    if (version.load(std::memory_order_acquire)!=before || !at || now<at || now-at>100 ||
        title!=TitleAdapter_GetActiveTitle() || !generation ||
        generation!=TitleAdapter_GetGeneration(title) || epoch!=inputEpoch.load()) return false;
    const float cx=std::bit_cast<float>(uint32_t(value));
    const float cy=std::bit_cast<float>(uint32_t(value>>32));
    if (!std::isfinite(cx) || !std::isfinite(cy) || cx*cx+cy*cy<=1e-6f) return false;
    x=cx; y=cy;
    consumed.fetch_add(1,std::memory_order_relaxed);
    return true;
}

void Roomscale_Camera(GameTitle title,bool allowed,const float body[3],
    const float head[3],const float q[4],const float forward[3],
    float reference[3],float scale) noexcept
{
    Roomscale_Camera(title,Roomscale_Refuse(allowed ? roomscale_admission::Refusal::None
        : roomscale_admission::Refusal::Unspecified),body,head,q,forward,reference,scale);
}

void Roomscale_Camera(GameTitle title,const roomscale_admission::Verdict& admission,const float body[3],
    const float head[3],const float q[4],const float forward[3],
    float reference[3],float scale) noexcept
{
    const bool allowed=admission.Admitted();
    if (title!=TitleAdapter_GetActiveTitle()) return;
    // Fail immediately on overlapping title callbacks; no locks or waits in a hook.
    if (publishing.test_and_set(std::memory_order_acquire)) return;
    // The published tracking reference belongs to the title, not one engine
    // thread. The nonblocking publishing guard above protects this shared
    // history when native camera callbacks migrate between threads.
    // Two follow laws for the A/B switch (roomscale_follow_law): hysteresis (1, the default) and threshold
    // (0, verbatim as it shipped in Alpha 0.6.0). Only the selected one runs; a switch starts both afresh,
    // so each begins from where the player stands.
    static RoomscaleFollow state;
    static RoomscaleFollowOriginal original;
    static roomscale_recenter::ViewRecenter recenter;
    static int priorLaw=-1;
    const int law=g_config.roomscale_follow_law ? 1 : 0;
    static GameTitle prior=GameTitle::None;
    static uint32_t priorInputEpoch=0, priorHistoryEpoch=0;
    const auto epoch=inputEpoch.load(std::memory_order_acquire);
    const auto history=historyEpoch.load(std::memory_order_acquire);
    const auto now=GetTickCount64(),at=inputAt.load(std::memory_order_acquire);
    if (law!=priorLaw)
    { state={}; original={}; recenter.Reset(); priorLaw=law; lawInEffect.store(law,std::memory_order_relaxed); }
    // A different title starts over. An admission change (priorHistoryEpoch) only ends the follow in
    // progress: the distance the player is owed survives it (RoomscaleFollow::Rebase). The threshold
    // law starts over there too, as in Alpha 0.6.0.
    if (prior!=title)
    {
        state={}; original={}; recenter.Reset(); prior=title; priorHistoryEpoch=history;
        // The report's "now" numbers belong to the title that measured them.
        errorLastMm.store(0,std::memory_order_relaxed); recenterOffsetMm.store(0,std::memory_order_relaxed);
    }
    else if (priorHistoryEpoch!=history)
    {
        if (law) state.Rebase(); else original={};
        priorHistoryEpoch=history;
    }
    else if (priorInputEpoch!=epoch)
    {
        if (law && state.seeded) state.SuspendForManual(now);
        else if (!law && original.seeded) original.SuspendForManual(now);
    }
    priorInputEpoch=epoch;
    const auto generation=TitleAdapter_GetGeneration(title);
    // A new level of the same title starts the recenter over as well (the follow re-seeds itself on the
    // generation: RoomscaleFollow::Update): a jump witness or a blend must not straddle a load.
    static uint32_t priorGeneration=0;
    if (priorGeneration!=generation)
    {
        recenter.Reset(); priorGeneration=generation;
        errorLastMm.store(0,std::memory_order_relaxed); recenterOffsetMm.store(0,std::memory_order_relaxed);
    }
    const bool tracking = VR_RoomscaleTrackingFresh();
    // Native on-foot admission belongs to the camera owner, never an arbitrary
    // XInput/OpenXR thread calling engine TLS accessors.
    static physical_running::Gesture runGesture;
    static uint32_t previousRunGeneration=0;
    static GameTitle previousRunTitle=GameTitle::None;
    static uint64_t previousRunSerial=0,runSampleAt=0;
    if(previousRunGeneration!=generation||previousRunTitle!=title)runGesture.Reset();
    previousRunGeneration=generation;previousRunTitle=title;
    VrContactTrackingSnapshot runTracking{};
    physical_running::Sample runSample{};
    if(g_config.physical_running&&allowed&&tracking&&VR_GetContactTrackingSnapshot(runTracking)&&
       runTracking.headValid&&runTracking.hands[0].valid&&runTracking.rawPrimaryValid&&
       !runTracking.twoHandAimActive&&!runTracking.locomotionBlocked) {
        runSample.valid=true;runSample.serial=runTracking.serial;
        runSample.epoch=runTracking.referenceEpoch;runSample.timeNs=runTracking.timeNs;
        runSample.head={runTracking.headPosition[0],runTracking.headPosition[1],runTracking.headPosition[2]};
        runSample.hands[0]={runTracking.hands[0].position[0],runTracking.hands[0].position[1],runTracking.hands[0].position[2]};
        runSample.hands[1]={runTracking.rawPrimaryPosition[0],runTracking.rawPrimaryPosition[1],runTracking.rawPrimaryPosition[2]};
        const auto* hq=runTracking.headOrientation;
        runSample.forwardX=-2*(hq[3]*hq[1]+hq[0]*hq[2]);
        runSample.forwardZ=-(1-2*(hq[0]*hq[0]+hq[1]*hq[1]));
    }
    const float run=runGesture.Update(runSample,g_config.physical_running_sensitivity,g_config.physical_running_speed);
    if(!runSample.valid) {previousRunSerial=0;runSampleAt=0;}
    else if(runSample.serial!=previousRunSerial) {previousRunSerial=runSample.serial;runSampleAt=now;}
    runVersion.fetch_add(1,std::memory_order_acq_rel);
    runValue.store(std::bit_cast<uint32_t>(run),std::memory_order_relaxed);
    runAt.store(runSampleAt,std::memory_order_relaxed);runTitle.store(static_cast<int>(title),std::memory_order_relaxed);
    runGeneration.store(generation,std::memory_order_relaxed);
    runVersion.fetch_add(1,std::memory_order_release);
    const bool input = at && now>=at && now-at<=100 && inputAllowed.load(std::memory_order_acquire);
    const bool active=kEnableRoomscaleBodyFollow && allowed && g_config.roomscale_movement &&
        tracking && input && title==TitleAdapter_GetActiveTitle();
    if (g_config.roomscale_movement)
    {
        if (!allowed)
        {
            nativeBlocked.fetch_add(1, std::memory_order_relaxed);
            const auto why=size_t(admission.refusal)<size_t(roomscale_admission::Refusal::Count)
                ? admission.refusal : roomscale_admission::Refusal::Unspecified;
            admissionRefused[size_t(why)].fetch_add(1,std::memory_order_relaxed);
            admissionLastRungs.store(uint32_t(admission.rungs)|(uint32_t(why)<<8)|(uint32_t(title)<<16),
                std::memory_order_relaxed);
        }
        if (admission.monitorCheckFaulted)
        {
            admissionFaultTitle.store(static_cast<int>(title),std::memory_order_relaxed);
            admissionFaultGeneration.store(generation,std::memory_order_release);
        }
        if (admission.cameraRungProven || admission.cameraRungCondemned)
            admissionRungTrust.store((uint64_t(generation)<<32)|(uint64_t(uint8_t(title))<<8)|
                (admission.cameraRungProven ? 1u : 0u)|(admission.cameraRungCondemned ? 2u : 0u),
                std::memory_order_release);
        if (!tracking) trackingBlocked.fetch_add(1, std::memory_order_relaxed);
        if (!input) inputBlocked.fetch_add(1, std::memory_order_relaxed);
    }
    const float hx=-2*(q[3]*q[1]+q[0]*q[2]);
    const float hz=-(1-2*(q[0]*q[0]+q[1]*q[1]));
    float x=0,y=0;
    const float oldRefX=reference[0], oldRefZ=reference[2];
    const bool manual=manualMove.load(std::memory_order_acquire);
    const bool valid=law
        ? state.Update(generation,now,active,manual,body,head,reference,hx,hz,forward[0],forward[1],scale,x,y)
        : original.Update(generation,now,active,manual,body,head,reference,hx,hz,forward[0],forward[1],scale,x,y);
    const float* followError=law ? state.error : original.error;
    if (g_config.roomscale_movement)
    {
        (valid ? admitted : refused).fetch_add(1,std::memory_order_relaxed);
        // The hysteresis law's guards (the threshold law has none).
        if (law && state.guard==RoomscaleFollow::Guard::StoodDown) guardStoodDown.fetch_add(1,std::memory_order_relaxed);
        else if (law && state.guard==RoomscaleFollow::Guard::NotOurs) guardNotOurs.fetch_add(1,std::memory_order_relaxed);
        if (valid && manual) manualSamples.fetch_add(1,std::memory_order_relaxed);
        if (valid && (x!=0 || y!=0)) demandSamples.fetch_add(1,std::memory_order_relaxed);
        const float distance=std::hypot(reference[0]-oldRefX,reference[2]-oldRefZ);
        if (valid && std::isfinite(distance) && distance<=0.4f)
            travelMm.fetch_add(uint64_t(distance*1000.0f+0.5f),std::memory_order_relaxed);
        if (valid)
        {
            const float err=std::hypot(followError[0],followError[1]);
            const float cmd=std::hypot(x,y);
            if (std::isfinite(err))
            {
                const uint64_t mm=uint64_t(err*1000.0f+0.5f);
                errorLastMm.store(mm,std::memory_order_relaxed);
                uint64_t prior=errorMaxMm.load(std::memory_order_relaxed);
                while (mm>prior && !errorMaxMm.compare_exchange_weak(prior,mm,std::memory_order_relaxed)) {}
            }
            if (std::isfinite(cmd))
            {
                const uint64_t milli=uint64_t(cmd*1000.0f+0.5f);
                uint64_t prior=commandMaxMilli.load(std::memory_order_relaxed);
                while (milli>prior && !commandMaxMilli.compare_exchange_weak(prior,milli,std::memory_order_relaxed)) {}
            }
#if HALOMCCVR_DEV_DIAGNOSTICS
            // Only while a follow is actually running (or stood down, which is a follow held off): a
            // steady lean inside the start band is the normal idle state and must not log a line every
            // report.
            const float* followVelocity=law ? state.velocity : original.velocity;
            const float speed=std::hypot(followVelocity[0],followVelocity[1]);
            const bool inProgress=law ? (state.following || state.settling || state.stalled) : original.settling;
            if (std::isfinite(err) && std::isfinite(cmd) && std::isfinite(speed) &&
                (inProgress || cmd>0) &&
                (now<followSampledAt || now-followSampledAt>=100))
            {
                const unsigned slot=followHead.load(std::memory_order_relaxed);
                followTrace[slot%kFollowTrace]={now,followError[0],followError[1],cmd,speed};
                followHead.store(slot+1,std::memory_order_release);
                followSampledAt=now;
            }
#endif
        }
    }
    // THE VIEW RECENTER, after the follow has had this frame: its own handling of the reference (a new
    // level's re-seat, a tracking jump, observed travel) is done, so the offset left is the real one. A
    // shift moves the title's reference (the caller composes this frame's view, hands and reticle from
    // it) on its two horizontal axes only, and the follow is handed the result as the new truth. Refused
    // whenever the follow's own admission fails: a seat, a cinematic, third person, death, a menu, an
    // untracked head.
    {
        using roomscale_recenter::Refusal;
        const auto settings=RecenterSettings();
        const Refusal blocked=!allowed ? Refusal::NotOnFoot : !tracking ? Refusal::Untracked :
            !input ? Refusal::NoInput : !valid ? Refusal::NoFollow : Refusal::None;
        const auto jumpAt=jumpHeldAt.load(std::memory_order_acquire);
        const bool jumpHeld=jumpAt && now>=jumpAt && now-jumpAt<=100;
        const auto shift=recenter.Update(ROOMSCALE_NOW_US(),settings,blocked,manual,jumpHeld,
            scale>0 ? body[2]/scale : 0.0f,head[0]-reference[0],head[2]-reference[2]);
        if (shift.refused!=Refusal::None)
            recenterRefused[size_t(shift.refused)].fetch_add(1,std::memory_order_relaxed);
        if (shift.takeoff) recenterTakeoffs.fetch_add(1,std::memory_order_relaxed);
        if (shift.started==roomscale_recenter::Mode::Jump) recenterBlends[0].fetch_add(1,std::memory_order_relaxed);
        else if (shift.started==roomscale_recenter::Mode::MoveStart) recenterBlends[1].fetch_add(1,std::memory_order_relaxed);
        if (shift.dx!=0 || shift.dz!=0)
        {
            reference[0]+=shift.dx; reference[2]+=shift.dz;
            if (law) state.AcceptViewShift(reference,shift.keep);
            else original.AcceptViewShift(reference,shift.keep);
            const size_t slot=shift.mode==roomscale_recenter::Mode::Jump ? 0 :
                shift.mode==roomscale_recenter::Mode::MoveStart ? 1 : 2;
            recenterClosedUm[slot].fetch_add(uint64_t(std::hypot(shift.dx,shift.dz)*1'000'000.0f+0.5f),
                std::memory_order_relaxed);
        }
        if (settings.Any() && blocked==Refusal::None)
        {
            const float offset=std::hypot(head[0]-reference[0],head[2]-reference[2]);
            if (std::isfinite(offset))
                recenterOffsetMm.store(uint64_t(offset*1000.0f+0.5f),std::memory_order_relaxed);
        }
    }
    version.fetch_add(1,std::memory_order_acq_rel);
    command.store(uint64_t(std::bit_cast<uint32_t>(x))|
        (uint64_t(std::bit_cast<uint32_t>(y))<<32),std::memory_order_relaxed);
    commandTitle.store(static_cast<int>(title),std::memory_order_relaxed);
    commandGeneration.store(generation,std::memory_order_relaxed);
    commandEpoch.store(epoch,std::memory_order_relaxed);
    commandAt.store(now,std::memory_order_relaxed);
    version.fetch_add(1,std::memory_order_release);
    publishing.clear(std::memory_order_release);
}

// Existing non-hook status tick owns logging. Camera/input hooks only count.
void Roomscale_Report() noexcept
{
    static uint64_t last=0;
    static bool previous=false;
    static bool previousRun=false;
    const auto now=GetTickCount64();
    const bool enabled=g_config.roomscale_movement;
    if(previousRun!=g_config.physical_running) {
        previousRun=g_config.physical_running;
        LOG("Physical running %s: alternating tracked arms, native on-foot movement only; speed fraction=%.2f sensitivity=%.2f; tracking or ownership loss cancels movement",
            previousRun?"ON":"OFF",g_config.physical_running_speed,g_config.physical_running_sensitivity);
    }
    if (enabled!=previous)
    {
        LOG("Roomscale body movement %s: native walking, head-relative movement; physical steps during VR-stick travel wait for native quiet before catch-up",
            enabled ? "ON" : "OFF");
        previous=enabled;
    }
    static int previousLaw=-2;
    const int law=g_config.roomscale_follow_law ? 1 : 0;
    if (enabled && law!=previousLaw)
    {
        LOG("Roomscale follow: %s (A/B switch roomscale_follow_law = %d)",
            law ? "A, hysteresis (3 cm start / 1.5 cm stop, brake by release, owed distance kept)"
                : "B, threshold (1.5 cm, reverse brake; as it shipped in Alpha 0.6.0)", law);
        previousLaw=law;
    }
    // One line whenever the recenter options that are ON change (release-safe; nothing while all are off
    // from the start). The values are logged as they stood at that moment; a slider moved later is in
    // the cfg.
    static unsigned previousRecenter=0;
    const auto recenterSettings=RecenterSettings();
    const unsigned recenterNow=(recenterSettings.jump ? 1u : 0u)|(recenterSettings.moveStart ? 2u : 0u)|
        (recenterSettings.moving ? 4u : 0u);
    if (recenterNow!=previousRecenter)
    {
        if (!recenterNow) LOG("Roomscale view recenter OFF: the view stays on your head; the body follow alone closes the offset");
        else LOG("Roomscale view recenter ON: on a jump %s (%.2f s), when you start moving %s (%.2f s), "
            "while moving %s (%.2f m/s). The play area slides so the view returns over the character: "
            "position only, horizontal, never a turn or your height; the body follow walks whatever is left",
            recenterSettings.jump ? "on" : "off",
            double(roomscale_recenter::ClampSeconds(recenterSettings.jumpSeconds,0.2f)),
            recenterSettings.moveStart ? "on" : "off",
            double(roomscale_recenter::ClampSeconds(recenterSettings.moveStartSeconds,0.2f)),
            recenterSettings.moving ? "on" : "off",
            double(roomscale_recenter::ClampSpeed(recenterSettings.movingSpeed,0.5f)));
        previousRecenter=recenterNow;
    }
    // The two "now" numbers are the active title's own: another title's (or level's) last value is not
    // carried into the lines printed while the new one loads, before its camera hook has run a frame.
    {
        static GameTitle reportTitle=GameTitle::None;
        static uint32_t reportGeneration=0;
        const GameTitle activeTitle=TitleAdapter_GetActiveTitle();
        const uint32_t activeGeneration=TitleAdapter_GetGeneration(activeTitle);
        if (activeTitle!=reportTitle || activeGeneration!=reportGeneration)
        {
            reportTitle=activeTitle; reportGeneration=activeGeneration;
            errorLastMm.store(0,std::memory_order_relaxed); recenterOffsetMm.store(0,std::memory_order_relaxed);
        }
    }
    if (now-last<2000) return;
    last=now;
    const auto good=admitted.exchange(0),bad=refused.exchange(0),moves=consumed.exchange(0);
    const auto native=nativeBlocked.exchange(0), tracking=trackingBlocked.exchange(0), input=inputBlocked.exchange(0);
    const auto manual=manualSamples.exchange(0), demand=demandSamples.exchange(0), travel=travelMm.exchange(0);
    const auto errLast=errorLastMm.load(std::memory_order_relaxed), errMax=errorMaxMm.exchange(0);
    const auto cmdMax=commandMaxMilli.exchange(0);
    if (enabled)
        LOG("Roomscale: title=%u admitted=%llu unavailable=%llu movement polls=%llu; "
            "blocked native=%llu tracking=%llu input=%llu manual=%llu demand=%llu consumed-mm=%llu; "
            "follow error now=%llu max=%llu mm, stick cmd max=%.2f; law=%s; %s",
            unsigned(TitleAdapter_GetActiveTitle()), (unsigned long long)good,
            (unsigned long long)bad,(unsigned long long)moves,
            (unsigned long long)native,(unsigned long long)tracking,(unsigned long long)input,
            (unsigned long long)manual,(unsigned long long)demand,(unsigned long long)travel,
            (unsigned long long)errLast,(unsigned long long)errMax,double(cmdMax)*0.001,
            lawInEffect.load(std::memory_order_relaxed)==0 ? "threshold" : "hysteresis",
            good ? "native body follow available" :
            "StockFallback: awaiting fresh on-foot camera, tracking and gameplay input; VR stays active");
    // Why the title refused, by reading (counted in every build; printed in dev builds, and only for a window
    // that had a refusal). The rungs are the first-person evidence as it stood at the last refusal.
    {
        using roomscale_admission::Refusal;
        uint64_t reasons[size_t(Refusal::Count)]{};
        uint64_t total=0;
        for (size_t i=0;i<size_t(Refusal::Count);++i) { reasons[i]=admissionRefused[i].exchange(0); total+=reasons[i]; }
        const uint32_t last=admissionLastRungs.load(std::memory_order_relaxed);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (enabled && total)
        {
            char line[1024];
            const int head=snprintf(line,sizeof line,
                "Roomscale admission: title=%u refused %llu of %llu camera frames:",
                unsigned(last>>16),(unsigned long long)total,(unsigned long long)(good+bad));
            // Each rung is named for the title it was read in (the game's camera / the Monitor answer / the view
            // model are different readings per title).
            const roomscale_admission::RungSources sources=
                roomscale_admission::TitleRungSources(GameTitle(uint8_t(last>>16)));
            if (head>0 && size_t(head)<sizeof line)
                roomscale_admission::FormatRefusals(line+head,sizeof line-size_t(head),reasons,
                    Refusal((last>>8)&0xFFu),uint8_t(last),&sources);
            LOG("%s",line);
        }
#else
        (void)reasons; (void)total; (void)last;
#endif
        // The Monitor predicate faulting is a degraded feature: said once per title generation, in every build.
        static uint32_t faultSaid=0;
        const uint32_t faultGeneration=admissionFaultGeneration.load(std::memory_order_acquire);
        if (faultGeneration && faultGeneration!=faultSaid)
        {
            faultSaid=faultGeneration;
            LOG("Roomscale admission: title=%d StockFallback: the Forge Monitor check FAULTED and is off until "
                "the next level; an unarmed player is admitted only while a first-person view model is drawn. "
                "VR and the body follow are otherwise unaffected",
                admissionFaultTitle.load(std::memory_order_relaxed));
        }
        // The camera rung's value agreement with the view model (Reach / Halo 4 / Halo 2), said once per change.
        static uint64_t trustSaid=0;
        const uint64_t trust=admissionRungTrust.load(std::memory_order_acquire);
        if (trust && trust!=trustSaid)
        {
            trustSaid=trust;
            const roomscale_admission::RungSources sources=
                roomscale_admission::TitleRungSources(GameTitle(uint8_t(trust>>8)));
            if (trust&2u)
                LOG("Roomscale admission: title=%u StockFallback: the game camera rung [%s] said \"not first "
                    "person\" for %u on-foot frames in a row while the first-person view model [%s] was drawn, and "
                    "never once agreed with it: the rung is withdrawn until the next level and the view model "
                    "decides, as before (an unarmed player is refused). VR and the body follow are otherwise "
                    "unaffected", unsigned(uint8_t(trust>>8)), sources.camera ? sources.camera : "?",
                    unsigned(director_camera::RungTrust::kCondemn), sources.viewModel ? sources.viewModel : "?");
            else
                LOG("Roomscale admission: title=%u the game camera rung [%s] is proven for this level: it said "
                    "\"first person\" on %u on-foot frames on which the first-person view model [%s] was drawn; "
                    "an unarmed first-person player is admitted by it",
                    unsigned(uint8_t(trust>>8)), sources.camera ? sources.camera : "?",
                    unsigned(director_camera::RungTrust::kProven), sources.viewModel ? sources.viewModel : "?");
        }
    }
    const auto stoodDown=guardStoodDown.exchange(0), notOurs=guardNotOurs.exchange(0);
    if (enabled && (stoodDown || notOurs))
        LOG("Roomscale guard: stood down %llu time(s) (the character ignored the follow's stick for %llu ms); "
            "%llu move(s) while probing it again were the game moving him, not counted as walking",
            (unsigned long long)stoodDown,(unsigned long long)RoomscaleFollow::kNoResponseMs,
            (unsigned long long)notOurs);
    // The view recenter's two seconds. Counted in every build; printed in dev builds, and only when
    // something happened.
    {
        using roomscale_recenter::Refusal;
        const auto closedJump=recenterClosedUm[0].exchange(0), closedStart=recenterClosedUm[1].exchange(0),
            closedMoving=recenterClosedUm[2].exchange(0);
        const auto blendsJump=recenterBlends[0].exchange(0), blendsStart=recenterBlends[1].exchange(0);
        const auto takeoffs=recenterTakeoffs.exchange(0);
        const auto notOnFoot=recenterRefused[size_t(Refusal::NotOnFoot)].exchange(0),
            untracked=recenterRefused[size_t(Refusal::Untracked)].exchange(0),
            noInput=recenterRefused[size_t(Refusal::NoInput)].exchange(0),
            noFollow=recenterRefused[size_t(Refusal::NoFollow)].exchange(0);
#if HALOMCCVR_DEV_DIAGNOSTICS
        if (enabled && recenterNow && (closedJump || closedStart || closedMoving || blendsJump || blendsStart ||
            takeoffs || notOnFoot || untracked || noInput || noFollow))
            LOG("Roomscale recenter: closed jump=%llu mm (%llu blend(s), %llu takeoff(s) seen) "
                "move-start=%llu mm (%llu blend(s)) moving=%llu mm; head-to-camera offset now=%llu mm; "
                "refused: not on foot/first person=%llu untracked=%llu no gameplay input=%llu follow=%llu",
                (unsigned long long)((closedJump+500)/1000),(unsigned long long)blendsJump,
                (unsigned long long)takeoffs,
                (unsigned long long)((closedStart+500)/1000),(unsigned long long)blendsStart,
                (unsigned long long)((closedMoving+500)/1000),
                (unsigned long long)recenterOffsetMm.load(std::memory_order_relaxed),
                (unsigned long long)notOnFoot,(unsigned long long)untracked,
                (unsigned long long)noInput,(unsigned long long)noFollow);
#else
        (void)closedJump; (void)closedStart; (void)closedMoving; (void)blendsJump; (void)blendsStart;
        (void)takeoffs; (void)notOnFoot; (void)untracked; (void)noInput; (void)noFollow;
#endif
    }
#if HALOMCCVR_DEV_DIAGNOSTICS
    static unsigned printed=0;
    const unsigned head=followHead.load(std::memory_order_acquire);
    if (enabled && head!=printed)
    {
        if (head-printed>kFollowTrace) printed=head-kFollowTrace;
        char line[1024];
        // The error is a tracking-space vector (x right, z back), so a catch-up that walks at an
        // angle to it shows up as the other component growing.
        int used=snprintf(line,sizeof line,
            "Roomscale trace (error x,z mm / stick / speed mm/s, ~100 ms apart; | = gap):");
        uint64_t previousAt=0;
        for (unsigned i=printed; i!=head && used>0 && used<int(sizeof line)-48; ++i)
        {
            const FollowSample s=followTrace[i%kFollowTrace];
            if (previousAt && s.at-previousAt>250) used+=snprintf(line+used,sizeof line-used," |");
            used+=snprintf(line+used,sizeof line-used," %.0f,%.0f/%.2f/%.0f",
                double(s.errorX)*1000.0,double(s.errorZ)*1000.0,double(s.command),double(s.speed)*1000.0);
            previousAt=s.at;
        }
        LOG("%s",line);
        printed=head;
    }
#endif
}
