#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Physical crouch comes in three parts, so that ONE standing reference serves every input thread, title
// and level:
//  - CrouchHeightContinuity (the OpenXR frame thread) presents the head height in one continuous frame,
//    across the runtime recenters and tracking steps that move the LOCAL space under the player;
//  - PhysicalCrouchReference (the OpenXR frame thread) learns the player's standing height in that frame;
//  - PhysicalCrouch (each input thread) is the gesture: the hysteresis between that reference and now.
//
// The reference is deliberately NOT re-sampled from the current head at a recenter -- the automatic ones
// after a cutscene, an unpause and a level load included -- nor on a title or level change, nor by each
// input thread separately. Whatever posture the player is in at that instant would become "standing", and
// a capped upward learning cannot heal a low sample: a runtime recenter caught mid-duck (measured in
// Halo 4: a reference sampled at -0.85 m with the player's standing head at 0.0 m) leaves the player
// having to lie on the floor to crouch. Nothing re-samples it but the menu's Calibrate
// button and a tracking space the runtime replaced without saying where it went.

// Height continuity. The crouch compares heights across minutes, but the LOCAL space can move under the
// player: a runtime recenter puts its origin at the head (a jump of the whole difference between the old
// origin and the head), and a tracking relocalisation can step it. Neither is the player moving. Heights
// are presented as raw LOCAL + carry, and every such discontinuity is absorbed into the carry.
class CrouchHeightContinuity
{
public:
    // A head moves at most this fast (m/s; a hard duck is 2-3 m/s), plus this much tracking noise (m).
    // A larger step between consecutive tracked samples is the space moving, not the head.
    static constexpr float kMaxHeadSpeed=3.f,kStepNoise=.02f;
    // Consecutive = no further apart than this (ns). Across a longer gap the head may truly have moved.
    static constexpr int64_t kConsecutiveNs=50'000'000;
    // A step within this of the runtime's own offset is that recenter (m).
    static constexpr float kMatch=.05f;
    // How long after its change time an announced recenter may still show up as a step (ns), and how far
    // an unannounced step may precede the announcement and still be it (ns).
    static constexpr int64_t kWindowNs=2'000'000'000,kLateNs=1'000'000'000;

    // The runtime announced that the LOCAL space moves at changeTimeNs. When poseValid, dy is the new
    // origin's height in the old space: a height h becomes h-dy.
    void NoteSpaceChange(int64_t changeTimeNs,bool poseValid,float dy) noexcept
    {
        if(!std::isfinite(dy)) poseValid=false;
        // A runtime that switches its poses before the event is polled has already shown the step, and it
        // was absorbed as unannounced. Do not move the frame a second time.
        if(lastAbsorbAt_&&std::llabs(changeTimeNs-lastAbsorbAt_)<=kLateNs&&
            (!poseValid||std::fabs(lastAbsorbStep_+dy)<=kMatch))
        {++late_;lastAbsorbAt_=0;return;}
        // Polled after the frame already crossed the tracking gap it fell in (the events are polled before
        // present, the head is sampled after the wait): no pair across a gap is ever absorbed, so its step
        // went in as motion, and the samples since are in the new space. Take the runtime's word now; without
        // its offset, nothing relates the two spaces.
        if(gapEndAt_&&changeTimeNs>gapStartAt_&&changeTimeNs<=gapEndAt_)
        {
            if(poseValid) {carry_+=dy;++applied_;}
            else ++breaks_;
            gapEndAt_=0;return;
        }
        if(pending_.active) ++unseen_;
        pending_={true,changeTimeNs,poseValid,poseValid?dy:0.f};
    }
    // The space was replaced outright (a new session): nothing relates the old heights to the new ones.
    void Break() noexcept {have_=false;pending_={};lastAbsorbAt_=0;gapEndAt_=0;++breaks_;}
    // One TRACKED raw LOCAL head height at its display time (ns); returns it in the continuous frame.
    float Update(float raw,int64_t timeNs) noexcept
    {
        if(!std::isfinite(raw)) return raw;
        if(!have_)
        {
            // Nothing to keep continuous with yet, so a pending recenter is moot.
            have_=true;lastRaw_=raw;lastAt_=timeNs;pending_={};
            return raw+carry_;
        }
        const int64_t gap=timeNs-lastAt_;
        // A repeated (or out-of-order) display time is the same instant: nothing can be learned from it.
        if(gap<=0) return raw+carry_;
        const float step=raw-lastRaw_;
        const bool consecutive=gap<=kConsecutiveNs;
        const float motion=kStepNoise+kMaxHeadSpeed*float(consecutive?gap:0)*1e-9f;
        if(pending_.active)
        {
            const bool visible=pending_.poseValid&&std::fabs(pending_.dy)>motion;
            if(consecutive&&(std::fabs(step)>motion||(visible&&std::fabs(step+pending_.dy)<=kMatch)))
                Absorb(step,timeNs,true);
            else if(timeNs>=pending_.at)
            {
                if(pending_.poseValid&&(!visible||!consecutive))
                {
                    // Too small to tell from motion, or it fell inside a tracking gap: take the runtime's
                    // word, at its time.
                    carry_+=pending_.dy;pending_={};++applied_;
                }
                else if(!consecutive) {pending_={};++breaks_;}   // a gap hid a move nobody measured
                else if(timeNs-pending_.at>=kWindowNs) {pending_={};++unseen_;}
            }
        }
        else if(consecutive&&std::fabs(step)>motion) Absorb(step,timeNs,false);
        if(!consecutive) {gapStartAt_=lastAt_;gapEndAt_=timeNs;}
        lastRaw_=raw;lastAt_=timeNs;
        return raw+carry_;
    }
    // Increments whenever continuity could not be kept: the reference must be sampled anew.
    uint32_t Breaks() const noexcept {return breaks_;}
    // Diagnostics.
    float Carry() const noexcept {return carry_;}
    uint32_t Announced() const noexcept {return announced_;}
    uint32_t Unannounced() const noexcept {return unannounced_;}
    uint32_t Applied() const noexcept {return applied_;}
    uint32_t Unseen() const noexcept {return unseen_;}
    uint32_t Late() const noexcept {return late_;}
private:
    void Absorb(float step,int64_t at,bool announced) noexcept
    {
        if(announced&&pending_.poseValid&&std::fabs(step+pending_.dy)<=kMatch) carry_+=pending_.dy;
        else carry_-=step;
        if(announced) {pending_={};++announced_;}
        else {++unannounced_;lastAbsorbAt_=at;lastAbsorbStep_=step;}
    }
    struct Pending {bool active{};int64_t at{};bool poseValid{};float dy{};};
    Pending pending_{};
    int64_t lastAt_{},lastAbsorbAt_{},gapStartAt_{},gapEndAt_{};
    float lastRaw_{},carry_{},lastAbsorbStep_{};
    uint32_t breaks_{},announced_{},unannounced_{},applied_{},unseen_{},late_{};
    bool have_{};
};

// The player's standing height in the continuous frame. It is sampled when there is none yet and when
// continuity breaks -- moments when the player's posture is unknown -- so that reference is LEARNED: it
// rises to the median head height over the last 10 s. A few seconds standing settle it; a crouch, a
// glance at the floor or a headset held up for a moment do not. The menu's Calibrate button is the
// player's own word instead ("this is my height", standing or seated): a calibrated reference then rises
// only to a height held steady for a second, by at most a little under two thirds of the activation
// depth, so a seated player who half-rises is not left crouched when they sit back down.
class PhysicalCrouchReference
{
public:
    // A calibrated reference ignores a height more than this far above the calibration: not a head at all
    // (a headset lifted off or set on a shelf, or a seated player standing up).
    static constexpr float kMaxRise=.30f;
    // A calibrated rise must be held this steady (m) for this long (ms) before it counts.
    static constexpr float kRiseSteady=.02f;
    static constexpr uint64_t kRiseHoldMs=1000;
    // A learned reference samples the head every kSampleMs and rises to the median of the last
    // kWindowSamples once it has kMinSamples of them.
    static constexpr uint64_t kSampleMs=100;
    static constexpr size_t kWindowSamples=100,kMinSamples=50;
    // A learned reference the head has stayed below the release line for this long (ms) was learned too
    // high (a headset lifted up and held there, a seated player who stood up for a few seconds) and falls
    // back to the median, so a mistake there cannot hold a crouch forever. A player's own Calibrate is
    // never second-guessed.
    static constexpr uint64_t kStaleLowMs=60'000;
    // ...but a reference CONFIRMED by kConfirmedMs of standing near it is the player's height: a kneel
    // behind cover for longer than a minute is a crouch, not a mistake. It falls back only after
    // kStaleLowConfirmedMs, so even a seated player who stood for a while is not held crouched for good.
    static constexpr uint64_t kConfirmedMs=60'000,kStaleLowConfirmedMs=300'000;

    static float Threshold(float depth) noexcept {return std::isfinite(depth)?std::clamp(depth,.08f,.65f):.22f;}
    static float Release(float threshold) noexcept {return threshold*.70f;}
    static float MaxLearnedRise(float threshold) noexcept {return std::min(kMaxRise,Release(threshold)-.02f);}

    // One head height in the continuous frame. calibration = the Calibrate count; continuity = the
    // frame's break count. available = a tracked head in a focused session.
    void Update(float height,uint32_t calibration,uint32_t continuity,uint64_t now,bool available,float depth) noexcept
    {
        if(!available||!std::isfinite(height)||!now) {riseSince_=0;return;}
        const bool calibrate=valid_&&calibration!=calibration_;
        if(!valid_||calibrate||continuity!=continuity_)
        {
            valid_=true;calibrated_=calibrate;calibration_=calibration;continuity_=continuity;
            baseline_=seed_=height;riseCapped_=false;riseSince_=0;count_=0;next_=0;sampleAt_=0;
            standingAt_=now;heardAt_=0;confirmedMs_=0;confirmedBase_=height;++seeds_;
            return;
        }
        const float threshold=Threshold(depth);
        if(calibrated_) LearnCalibrated(height,now,threshold);
        else LearnFromWindow(height,now,threshold);
    }
    bool Valid() const noexcept {return valid_;}
    float Baseline() const noexcept {return baseline_;}
    // Counts every (re)sampling: a gesture re-arms (released, standing first) when it changes.
    uint32_t Seeds() const noexcept {return seeds_;}
    bool Calibrated() const noexcept {return calibrated_;}
    // The reference is the player's height: calibrated, or confirmed by kConfirmedMs of standing near it. A
    // merely learned one may fall back within kStaleLowMs, so nothing that outlives it is built on it. (A
    // confirmed one can still fall back, after kStaleLowConfirmedMs low, and Calibrate re-seeds; what was built
    // on the old one -- a recenter's standing lift -- lasts until the next recenter.)
    bool Confirmed() const noexcept {return calibrated_||confirmedMs_>=kConfirmedMs;}
    // A calibrated reference saw a steady height above what it may learn: it now sits below the player's
    // standing height, and calibrating again standing tall gives them the full depth back.
    bool RiseCapped() const noexcept {return riseCapped_;}
private:
    void LearnCalibrated(float height,uint64_t now,float threshold) noexcept
    {
        // The cap follows the CURRENT depth: a rise learned at a deeper setting comes down with a shallower
        // one, or the posture the player calibrated in reads as a crouch until they calibrate again. Only the
        // learned rise moves; the calibration itself never does.
        baseline_=std::min(baseline_,seed_+MaxLearnedRise(threshold));
        if(height<=baseline_+kRiseSteady||height>seed_+kMaxRise) {riseSince_=0;return;}
        if(!riseSince_||std::fabs(height-riseValue_)>kRiseSteady) {riseSince_=now;riseValue_=height;return;}
        if(now>=riseSince_&&now-riseSince_>=kRiseHoldMs)
        {
            const float learned=std::min(riseValue_,height),cap=seed_+MaxLearnedRise(threshold);
            if(learned>cap) riseCapped_=true;
            baseline_=std::max(baseline_,std::min(learned,cap));
            riseSince_=0;
        }
    }
    void LearnFromWindow(float height,uint64_t now,float threshold) noexcept
    {
        if(height>=baseline_-Release(threshold))
        {
            standingAt_=now;
            if(heardAt_&&now>heardAt_) confirmedMs_+=std::min<uint64_t>(now-heardAt_,250);
        }
        heardAt_=now;
        if(sampleAt_&&now>=sampleAt_&&now-sampleAt_<kSampleMs) return;
        sampleAt_=now;
        window_[next_]=height;next_=(next_+1)%kWindowSamples;
        if(count_<kWindowSamples) ++count_;
        if(count_<kMinSamples) return;
        std::array<float,kWindowSamples> sorted;
        std::copy_n(window_.begin(),count_,sorted.begin());
        const auto mid=sorted.begin()+count_/2;
        std::nth_element(sorted.begin(),mid,sorted.begin()+count_);
        const float median=*mid;
        if(median>baseline_+kRiseSteady)
        {
            baseline_=median;
            // A rise the posture its confirmation was earned at would read as a crouch under (seated to
            // standing) is a new reference: it has to earn its confirmation again. Measured from where that
            // confirmation was earned, never per sample: through an ordinary one-second stand-up the median
            // climbs a few centimetres a sample, never a whole release band at once.
            if(baseline_-confirmedBase_>Release(threshold)) {confirmedMs_=0;confirmedBase_=baseline_;}
        }
        else if(now>=standingAt_&&
                now-standingAt_>=(confirmedMs_>=kConfirmedMs?kStaleLowConfirmedMs:kStaleLowMs))
        {baseline_=median;standingAt_=now;confirmedMs_=0;confirmedBase_=baseline_;}
    }

    std::array<float,kWindowSamples> window_{};
    size_t count_{},next_{};
    uint64_t riseSince_{},sampleAt_{},standingAt_{},heardAt_{},confirmedMs_{};
    float baseline_{},seed_{},riseValue_{},confirmedBase_{};
    uint32_t calibration_{},continuity_{},seeds_{};
    bool valid_{},calibrated_{},riseCapped_{};
};

// The gesture, per input thread: crouched while the head is the activation depth below the reference,
// released once it is back within 70% of it.
class PhysicalCrouch
{
public:
    // reference identifies the reference sample (PhysicalCrouchReference::Seeds(); 0 = none yet).
    bool Update(float height,float baseline,uint64_t reference,uint64_t now,bool available,float depth) noexcept
    {
        const float threshold=PhysicalCrouchReference::Threshold(depth);
        if(!available||!std::isfinite(height)||!std::isfinite(baseline)||!reference||!now)
        {active_=false;suspended_=true;last_=0;return false;}
        const float down=baseline-height,release=PhysicalCrouchReference::Release(threshold);
        if(!initialized_||reference_!=reference)
        {
            // A new reference, a new title or level, or the first sample: start released. A head already
            // below the release line stands first, so nothing begun before it can replay as a crouch.
            initialized_=true;reference_=reference;active_=false;suspended_=down>release;last_=now;
            return false;
        }
        if(last_&&(now<last_||now-last_>250)) {active_=false;suspended_=true;}
        last_=now;
        // Resume must never replay a crouch that began in a menu or vehicle: stand up first.
        if(suspended_)
        {if(down<=release)suspended_=false;return false;}
        if(active_)
        {if(down<=release)active_=false;}
        else if(down>=threshold) active_=true;
        return active_;
    }
    void Reset() noexcept {*this=PhysicalCrouch{};}
    // Diagnostics only (the input hook's 1 Hz crouch line).
    bool Active() const noexcept {return active_;}
    bool Suspended() const noexcept {return suspended_;}
    bool Initialized() const noexcept {return initialized_;}
private:
    uint64_t reference_{},last_{};
    bool initialized_{},active_{},suspended_{};
};
