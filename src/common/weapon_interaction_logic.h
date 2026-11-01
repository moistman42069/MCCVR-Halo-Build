#pragma once

#include "runtime_types.h"
#include "weapon_model_catalog.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Body gestures are an optional controller-input layer. They never mutate
// inventory, ammo, animation clocks, native tags or camera ownership.
namespace weapon_interaction
{
// User-rejected grip/four-stroke gesture remains dormant for history.
inline constexpr bool kLegacyGripNeedleShakeEnabled = false;
inline constexpr unsigned kTitleCount = 6;
inline constexpr const char* kTitleKeys[kTitleCount]{"halo3", "odst", "reach", "halo4", "ce", "halo2"};
inline constexpr const char* kTitleNames[kTitleCount]{"Halo 3", "ODST", "Reach", "Halo 4", "Halo CE", "Halo 2"};
inline constexpr const char* kButtonNames = "X\0Right bumper\0Left bumper\0B\0Y\0A\0Left trigger\0Right trigger\0";
inline constexpr uint32_t kButtons[]{0x4000, 0x200, 0x100, 0x2000, 0x8000, 0x1000, 1u<<16, 1u<<17};
inline constexpr int TitleIndex(GameTitle title) noexcept
{
    const int value = static_cast<int>(title);
    return value >= 1 && value <= 6 ? value - 1 : -1;
}
inline constexpr uint32_t Button(int index) noexcept
{
    return index >= 0 && index < 8 ? kButtons[index] : 0;
}
// The reload button a gesture presses from the per-title setting (weapon_reload_button_<title>), whose default
// X is MCC's layout. Project Reclaimer's Halo 3 reloads on Right bumper and X does not reload there, so in
// Reclaimer that default means Right bumper. Any other choice is the player's and is pressed as set.
inline constexpr uint32_t ReloadButton(GameTitle title, int configured, bool projectReclaimer) noexcept
{
    return Button(projectReclaimer && title == GameTitle::Halo3 && configured == 0 ? 1 : configured);
}
struct Vec { float x{}, y{}, z{}; };
struct Quat { float x{}, y{}, z{}, w{1}; };
inline Vec operator+(Vec a, Vec b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec operator-(Vec a, Vec b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec operator*(Vec a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Vec a, Vec b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline bool Finite(Vec a) noexcept { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
inline bool Normal(Quat q) noexcept
{
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&n>0.98f&&n<1.02f;
}
inline Vec Rotate(Quat q, Vec v) noexcept
{
    const Vec u{q.x,q.y,q.z};
    const Vec c{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    return u*(2*Dot(u,v))+v*(q.w*q.w-Dot(u,u))+c*(2*q.w);
}
inline bool Near(Vec a, Vec b, float r) noexcept { const Vec d=a-b; return Dot(d,d)<=r*r; }
inline float Setting(float v,float low,float high,float fallback) noexcept
{ return std::isfinite(v)?std::clamp(v,low,high):fallback; }

// The Campaign Evolved mod's over-the-shoulder weapon switch: where its slot is (metres from the head: to the
// weapon side, down, back) and how close the hand has to come.
inline constexpr float kOverShoulderSide=0.20f,kOverShoulderDown=0.10f,kOverShoulderBack=0.22f;
inline constexpr float kOverShoulderRadius=0.16f;
// A holster spot the player set with their own hand (location 3): each of its three distances from the head
// is kept within this (metres). An arm is shorter; it only keeps a bad config value or a tracking jump from
// putting the holster somewhere no hand can reach by a wide margin.
inline constexpr float kHolsterCustomLimit=0.80f;
// Armed in the menu and no grip came: the arming lapses after this long.
inline constexpr uint64_t kHolsterCaptureTimeoutMs=30000;

struct Settings
{
    bool reload{}, holsters{}, leftHanded{};
    float pouchDown{0.50f}, zoneRadius{0.20f};
    int pouchLocation{};
    Vec pouchOffset{}; // body-local right, up, forward; right mirrors with handedness
    float holsterRadius{0.20f}, insertRadius{0.18f}, drawDistance{0.25f};
    bool holsterSlide{true}, holsterClick{};
    bool needleShake{};
    bool genericVisual{true};
    float shakeTravel{0.10f};
    // 0 shoulder, 1 hip, 2 over the shoulder (the Halo: Campaign Evolved VR mod's weapon-switch spot: higher
    // and further back, where the hand goes when it reaches over the shoulder), 3 where the player put it
    // (holsterCustom). A single reserve slot exchanges the two native guns.
    int holsterLocation{};
    // Location 3: metres from the head, to the weapon side (mirrors with handedness), up, and forward, in the
    // same level frame as the other spots (the way the head faces, flattened). The caller supplies it from
    // the configuration; this default (the over-the-shoulder spot) is only what a bare Settings holds.
    Vec holsterCustom{kOverShoulderSide,-kOverShoulderDown,-kOverShoulderBack};
    uint32_t reloadButton{0x4000}, swapButton{0x8000};
};
struct Sample
{
    uint64_t now{}, space{};
    uint64_t poseNow{}; // Predicted pose milliseconds; zero uses now for non-XR callers.
    uint32_t generation{};
    GameTitle title{GameTitle::None};
    bool ready{}, dualWield{};
    Vec head{}, primary{}, support{};
    Quat headRotation{}, primaryRotation{};
    float primaryGrip{}, supportGrip{};
    bool otherAction{};
    uint64_t weaponGraph{};
    bool receiverValid{};
    Vec receiver{};
    Quat supportRotation{};
};

inline Vec HeldMagazineCenter(const Sample& sample,Quat supportRotation) noexcept
{ return sample.support+Rotate(supportRotation,{0,-.015f,-.035f}); }

// Title-specific identities from the official kits: CE graph fingerprint,
// H2 verified compression tuple, and H3/ODST/Reach/H4 model import checksum.
inline bool NeedleWeapon(GameTitle title,uint64_t graph) noexcept
{ const auto* model=weapon_model::Find(title,graph);return model&&model->needles; }
struct Output
{
    bool consumePrimary{}, consumeSupport{}, releaseTwoHand{};
    bool reloadRequested{}, swapRequested{}, pickedMagazine{}, grabbedHolster{};
    bool holdingMagazine{};
    float primaryHaptic{}, supportHaptic{};
    uint32_t buttons{};
    uint64_t pulseUntil{};
};
// After a holster draw has completed the weapon grip stays consumed until
// release, but that hold does not own the other hand. Only an active gesture
// or a held magazine prevents acquiring the support grip.
inline bool BlocksSupportGrab(const Output& output) noexcept
{ return output.releaseTwoHand||output.consumeSupport; }

// The level frame every body zone is laid out in: the way the head faces, flattened, and the right of that.
inline bool BodyFrame(Quat headRotation,Vec& forward,Vec& right) noexcept
{
    if(!Normal(headRotation)) return false;
    forward=Rotate(headRotation,{0,0,-1}); forward.y=0;
    const float n=Dot(forward,forward);
    if(!std::isfinite(n)) return false;
    if(n<0.04f)
    {
        // Looking straight down at the pouch must not destroy body yaw.
        Vec side=Rotate(headRotation,{1,0,0});side.y=0;
        const float length=Dot(side,side);
        if(!std::isfinite(length)||length<0.04f) return false;
        side=side*(1/std::sqrt(length));forward={side.z,0,-side.x};
    }
    else forward=forward*(1/std::sqrt(n));
    right={-forward.z,0,forward.x};
    return true;
}
// Where a hand is, as a custom holster spot (Settings::holsterCustom): the inverse of Zones for location 3.
// False when the poses are not usable; each distance is kept within kHolsterCustomLimit.
inline bool HolsterOffsetOfHand(Vec head,Quat headRotation,Vec hand,bool leftHanded,Vec& offset) noexcept
{
    Vec forward{},right{};
    if(!Finite(head)||!Finite(hand)||!BodyFrame(headRotation,forward,right)) return false;
    const Vec d=hand-head;
    const float side=leftHanded?-1.0f:1.0f;
    offset={std::clamp(Dot(d,right)*side,-kHolsterCustomLimit,kHolsterCustomLimit),
            std::clamp(d.y,-kHolsterCustomLimit,kHolsterCustomLimit),
            std::clamp(Dot(d,forward),-kHolsterCustomLimit,kHolsterCustomLimit)};
    return Finite(offset);
}
// SETTING THE HOLSTER SPOT WITH A HAND. Armed from the menu; the next FRESH squeeze of the weapon grip (the
// grip seen released since arming, then past the same 0.55 the gestures use) records where that hand is. A
// grip already held when arming is not that squeeze. Lapses after kHolsterCaptureTimeoutMs; the caller holds
// that clock (Defer) while the menu that armed it is still open, where no squeeze counts.
class HolsterCapture
{
public:
    enum class Result{Idle,Waiting,Captured,TimedOut};
    void Arm(uint64_t now) noexcept { armed_=true;released_=false;holding_=false;armedAt_=now; }
    void Cancel() noexcept { armed_=false;released_=false; }
    void Defer(uint64_t now) noexcept { if(armed_) armedAt_=now; }
    bool Armed() const noexcept { return armed_; }
    // The weapon grip is not the game's while armed, nor while the capturing squeeze is still held.
    bool ConsumesGrip() const noexcept { return armed_||holding_; }
    Result Update(uint64_t now,bool posesValid,Vec head,Quat headRotation,Vec hand,float grip,
                  bool leftHanded,Vec& offset) noexcept
    {
        const bool valid=std::isfinite(grip)&&grip>=0&&grip<=1;
        if(holding_&&(!valid||grip<0.35f)) holding_=false;
        if(!armed_) return Result::Idle;
        if(now<armedAt_||now-armedAt_>kHolsterCaptureTimeoutMs) { Cancel();return Result::TimedOut; }
        if(!valid) return Result::Waiting;
        if(grip<0.35f) released_=true;
        if(!released_||grip<=0.55f||!posesValid) return Result::Waiting;
        if(!HolsterOffsetOfHand(head,headRotation,hand,leftHanded,offset)) return Result::Waiting;
        armed_=false;released_=false;holding_=true;
        return Result::Captured;
    }
private:
    bool armed_{},released_{},holding_{};
    uint64_t armedAt_{};
};

inline bool Zones(const Sample& s,const Settings& c,Vec& pouch,Vec& holster) noexcept
{
    if(!Finite(s.head)||!Finite(s.primary)||!Finite(s.support)||
       !Normal(s.headRotation)||!Normal(s.primaryRotation)) return false;
    Vec forward{},right{};
    if(!BodyFrame(s.headRotation,forward,right)) return false;
    const float side=c.leftHanded?-1.0f:1.0f;
    const bool shoulder=c.pouchLocation==1;
    pouch=s.head+right*(-side*(shoulder?0.22f:0.25f))+forward*(shoulder?-0.13f:0.06f)+
        Vec{0,shoulder?-0.20f:-Setting(c.pouchDown,0.25f,0.85f,0.50f),0};
    pouch=pouch+right*(side*Setting(c.pouchOffset.x,-.40f,.40f,0))+
        Vec{0,Setting(c.pouchOffset.y,-.40f,.40f,0),0}+forward*Setting(c.pouchOffset.z,-.40f,.40f,0);
    // Over the shoulder: Campaign Evolved's right-shoulder slot, 0.20 m to the weapon side, 0.10 m below the
    // head and 0.22 m behind it (its holsterrs default).
    // Custom: where the player put it. A value that is not a number falls back to the over-the-shoulder spot.
    holster=c.holsterLocation==3
        ?s.head+right*(side*Setting(c.holsterCustom.x,-kHolsterCustomLimit,kHolsterCustomLimit,kOverShoulderSide))+
            Vec{0,Setting(c.holsterCustom.y,-kHolsterCustomLimit,kHolsterCustomLimit,-kOverShoulderDown),0}+
            forward*Setting(c.holsterCustom.z,-kHolsterCustomLimit,kHolsterCustomLimit,-kOverShoulderBack)
        :c.holsterLocation==1
        ?s.head+right*(side*0.27f)+Vec{0,-Setting(c.pouchDown,0.25f,0.85f,0.50f),0}
        :c.holsterLocation==2
            ?s.head+right*(side*kOverShoulderSide)+forward*(-kOverShoulderBack)+Vec{0,-kOverShoulderDown,0}
            :s.head+right*(side*0.22f)+forward*(-0.13f)+Vec{0,-0.20f,0};
    return true;
}

// One quick out-and-back translation of the weapon hand, in any direction.
// Head-relative positions reject whole-body translation. No grip is owned.
class SingleNeedleShake
{
public:
    void Reset() noexcept { *this=SingleNeedleShake{}; }
    bool Update(Vec relative,uint64_t now,float stroke) noexcept
    {
        const float travel=Setting(stroke,0.06f,0.20f,0.10f);
        if(!Finite(relative)||!now) { Reset();return false; }
        if(last_&&now==last_&&travel_==travel)return false;
        if(!last_||now<last_||now-last_>100||travel_!=travel)
        { Seed(relative,now,travel);return false; }
        const uint64_t previousAt=last_;
        const Vec previous=previous_;
        const float dt=static_cast<float>(now-last_)*0.001f;
        const Vec step=relative-previous_;
        const float distance2=Dot(step,step);
        // Drop discontinuities instead of interpreting tracking recovery as a
        // stroke. Fast human shakes remain valid at 72, 90 and 120 Hz.
        if(!std::isfinite(distance2)||distance2>0.25f*0.25f||distance2>64.0f*dt*dt)
        { Seed(relative,now,travel);return false; }
        previous_=relative;last_=now;
        const bool quiet=distance2<=0.20f*0.20f*dt*dt;
        if(latched_)
        {
            if(!quiet) quietAt_=0;
            else if(!quietAt_) quietAt_=previousAt;
            else if(now-quietAt_>=150) Seed(relative,now,travel);
            return false;
        }
        if(!returning_)
        {
            if(quiet) { origin_=relative;started_=0;return false; }
            if(!started_) { origin_=previous;started_=previousAt; }
            const Vec outward=relative-origin_;
            const float length2=Dot(outward,outward);
            const uint64_t elapsed=now-started_;
            if(elapsed>300) { Seed(relative,now,travel);return false; }
            if(length2>=travel*travel)
            {
                const float length=std::sqrt(length2);
                if(elapsed<20||length<0.50f*elapsed*0.001f)
                { Seed(relative,now,travel);return false; }
                axis_=outward*(1.0f/length);peak_=relative;peakAt_=now;returning_=true;
            }
            return false;
        }
        const float reverse=Dot(peak_-relative,axis_);
        if(reverse<0) { peak_=relative;peakAt_=now;return false; }
        const uint64_t elapsed=now-peakAt_;
        if(elapsed>300) { Seed(relative,now,travel);return false; }
        if(reverse>=travel&&elapsed>=20&&reverse>=0.50f*elapsed*0.001f)
        { latched_=true;quietAt_=0;return true; }
        return false;
    }
private:
    void Seed(Vec p,uint64_t now,float travel) noexcept
    { Reset();previous_=origin_=p;last_=now;travel_=travel; }
    Vec previous_{},origin_{},axis_{},peak_{};
    uint64_t last_{},started_{},peakAt_{},quietAt_{};
    float travel_{};
    bool returning_{},latched_{};
};

class State
{
public:
    void Reset() noexcept { *this=State{}; }
    void Cancel() noexcept
    { phase_=0;pulseUntil_=0;pulse_=0;armedP_=armedS_=false;available_=false;last_=0;singleShake_.Reset(); }
    bool HasClaimedGrip() const noexcept { return ownedP_||ownedS_; }
    Output Update(const Sample& s,const Settings& c) noexcept
    {
        Output out{};
        const bool validP=std::isfinite(s.primaryGrip)&&s.primaryGrip>=0&&s.primaryGrip<=1;
        const bool validS=std::isfinite(s.supportGrip)&&s.supportGrip>=0&&s.supportGrip<=1;
        const bool heldP=validP&&s.primaryGrip>0.55f;
        const bool heldS=validS&&s.supportGrip>0.55f;
        const bool releasedP=validP&&s.primaryGrip<0.35f;
        const bool releasedS=validS&&s.supportGrip<0.35f;
        const bool identity=title_!=s.title||generation_!=s.generation||space_!=s.space||
            left_!=c.leftHanded||reload_!=c.reload||holsters_!=c.holsters||
            slide_!=c.holsterSlide||click_!=c.holsterClick||shake_!=c.needleShake||
            (c.reload&&weaponGraph_!=s.weaponGraph)||
            reloadButton_!=c.reloadButton||swapButton_!=c.swapButton||location_!=c.holsterLocation||
            pouchLocation_!=c.pouchLocation||pouchOffset_.x!=c.pouchOffset.x||
            pouchOffset_.y!=c.pouchOffset.y||pouchOffset_.z!=c.pouchOffset.z||
            holsterCustom_.x!=c.holsterCustom.x||holsterCustom_.y!=c.holsterCustom.y||
            holsterCustom_.z!=c.holsterCustom.z;
        const bool gap=!last_||s.now<last_||s.now-last_>200;
        const bool ready=s.ready&&s.now&&s.space&&s.generation&&TitleIndex(s.title)>=0&&
            (c.reload||c.holsters)&&!s.dualWield&&
            validP&&validS;
        Vec pouch{},holster{};
        const bool poses=Zones(s,c,pouch,holster);
        const bool resumed=ready&&poses&&!available_;
        if(identity||gap||!ready||!poses||resumed)
        {
            // A held grip on re-entry is not a new gesture. Keep ownership of
            // an already claimed press until release, including cancellation.
            phase_=0; pulseUntil_=0; pulse_=0;
            singleShake_.Reset();
            armedP_=armedS_=false;
            if(left_!=c.leftHanded) std::swap(ownedP_,ownedS_);
        }
        title_=s.title;generation_=s.generation;space_=s.space;left_=c.leftHanded;
        reload_=c.reload;holsters_=c.holsters;reloadButton_=c.reloadButton;
        swapButton_=c.swapButton;location_=c.holsterLocation;last_=s.now;
        pouchLocation_=c.pouchLocation;pouchOffset_=c.pouchOffset;holsterCustom_=c.holsterCustom;
        slide_=c.holsterSlide;click_=c.holsterClick;
        shake_=c.needleShake;weaponGraph_=s.weaponGraph;
        available_=ready&&poses;
        if(releasedP) armedP_=true;
        if(releasedS) armedS_=true;
        // Ownership includes the release frame, then ends. Ordinary grips are
        // never consumed unless the player deliberately began in a body zone.
        out.consumePrimary=ownedP_;out.consumeSupport=ownedS_;
        if(releasedP) ownedP_=false;
        if(releasedS) ownedS_=false;
        if(!ready||!poses||identity||gap||resumed) return out;

        const bool occupied=phase_!=0||ownedP_||ownedS_;
        // A magazine is held until grip release; incidental trigger/button
        // input and taking time to align it must not silently drop the item.
        // Holster/shake commands retain their cancellation and timeout rules.
        if(phase_!=1&&(s.otherAction || (phase_&&s.now-started_>4000))) phase_=0;
        if(phase_==1)
        {
            out.consumeSupport=true;out.releaseTwoHand=true;
            if(!Near(s.support,pouch,Setting(c.zoneRadius,0.08f,0.40f,0.20f)+0.08f)) leftPouch_=true;
            if(releasedS)
            {
                // Insert beside/under the firing-hand grip. Controller-local
                // offset, not an invented per-title magazine marker.
                const Vec receiver=s.receiverValid?s.receiver:
                    s.primary+Rotate(s.primaryRotation,{0,-0.06f,-0.04f});
                const Vec magazine=s.receiverValid?HeldMagazineCenter(s,s.supportRotation):s.support;
                if(!s.otherAction&&leftPouch_&&s.now-started_>=120&&
                   (!s.receiverValid||(Finite(receiver)&&Normal(s.supportRotation)))&&
                   Near(magazine,receiver,Setting(c.insertRadius,0.06f,0.30f,0.18f))&&
                   !Near(s.support,pouch,Setting(c.zoneRadius,0.08f,0.40f,0.20f)+0.04f)&&
                   !Near(s.support,grabbedAt_,0.20f))
                {
                    pulse_=c.reloadButton;pulseUntil_=s.now+120;cooldown_=s.now+500;
                    out.reloadRequested=true;out.supportHaptic=0.45f;out.primaryHaptic=0.20f;
                }
                phase_=0;
            }
        }
        else if(phase_==2)
        {
            out.consumePrimary=true;out.releaseTwoHand=true;
            if(releasedP) phase_=0;
            else if(!s.otherAction&&s.now-started_>=120&&
                !Near(s.primary,holster,Setting(c.holsterRadius,0.08f,0.40f,0.20f)+0.12f)&&
                !Near(s.primary,grabbedAt_,Setting(c.drawDistance,0.10f,0.50f,0.25f)))
            {
                pulse_=c.swapButton;pulseUntil_=s.now+120;cooldown_=s.now+500;
                out.swapRequested=true;out.primaryHaptic=0.45f;phase_=0;
            }
        }
        else if(phase_==3)
        {
            out.consumePrimary=true;out.releaseTwoHand=true;
            if(releasedP) phase_=0;
            else
            {
                // Deliberate alternating vertical strokes relative to the
                // head reject walking/translation and a single melee swing.
                const Vec relative=s.primary-s.head;
                const float travel=Setting(c.shakeTravel,0.06f,0.20f,0.10f);
                if(std::fabs(relative.x-shakeOrigin_.x)>0.25f||
                   std::fabs(relative.z-shakeOrigin_.z)>0.25f||s.now-started_>1800)
                    phase_=0;
                else
                {
                    const float delta=relative.y-shakeExtreme_;
                    shakeLow_=std::fmin(shakeLow_,relative.y);shakeHigh_=std::fmax(shakeHigh_,relative.y);
                    if((shakeDirection_>0&&delta>=0)||(shakeDirection_<0&&delta<=0))
                        shakeExtreme_=relative.y;
                    if((shakeDirection_==0&&shakeHigh_-shakeLow_>=travel)||
                       (shakeDirection_>0&&delta<=-travel)||
                       (shakeDirection_<0&&delta>=travel))
                    {
                        // An implausibly rapid stroke cancels; tracking jumps
                        // cannot accumulate several reversals into a reload.
                        if(s.now-shakeAt_<60) phase_=0;
                        else
                        {
                            shakeDirection_=shakeDirection_==0?(relative.y>=(shakeHigh_+shakeLow_)*0.5f?1:-1):-shakeDirection_;
                            shakeExtreme_=relative.y;shakeAt_=s.now;++shakeStrokes_;
                            if(shakeStrokes_>=4)
                            {
                                phase_=0;pulse_=c.reloadButton;pulseUntil_=s.now+120;cooldown_=s.now+500;
                                out.reloadRequested=true;out.primaryHaptic=0.45f;
                            }
                        }
                    }
                }
            }
        }
        else if(!s.otherAction&&s.now>=cooldown_)
        {
            const float radius=Setting(c.zoneRadius,0.08f,0.40f,0.20f);
            if(c.holsters&&(c.holsterSlide||c.holsterClick)&&c.swapButton&&armedP_&&heldP&&
                Near(s.primary,holster,Setting(c.holsterRadius,0.08f,0.40f,0.20f)))
            {
                phase_=2;started_=s.now;grabbedAt_=s.primary;ownedP_=true;
                out.consumePrimary=true;out.releaseTwoHand=true;
                out.grabbedHolster=true;out.primaryHaptic=0.20f;
                if(c.holsterClick)
                {
                    // A fresh grip click in the zone exchanges once. Holding
                    // or moving cannot repeat it, even with both modes on.
                    phase_=0;pulse_=c.swapButton;pulseUntil_=s.now+120;cooldown_=s.now+500;
                    out.swapRequested=true;out.primaryHaptic=0.45f;
                }
            }
            else if(c.reload&&c.reloadButton&&armedS_&&heldS&&Near(s.support,pouch,radius))
            {
                phase_=1;started_=s.now;leftPouch_=false;ownedS_=true;grabbedAt_=s.support;
                out.consumeSupport=true;out.releaseTwoHand=true;
                out.pickedMagazine=true;out.supportHaptic=0.20f;
            }
            else if(kLegacyGripNeedleShakeEnabled&&c.reload&&c.needleShake&&NeedleWeapon(s.title,s.weaponGraph)&&
                c.reloadButton&&armedP_&&heldP)
            {
                phase_=3;started_=s.now;ownedP_=true;
                shakeOrigin_=s.primary-s.head;shakeExtreme_=shakeOrigin_.y;
                shakeLow_=shakeHigh_=shakeOrigin_.y;
                shakeDirection_=0;shakeStrokes_=0;shakeAt_=s.now;
                out.consumePrimary=true;out.releaseTwoHand=true;out.primaryHaptic=0.20f;
            }
        }
        // Pouch/holster transactions retain priority. Shake observes only the
        // weapon hand; it never consumes either grip or changes two-hand aim.
        if(c.reload&&c.needleShake&&c.reloadButton&&NeedleWeapon(s.title,s.weaponGraph)&&
           !s.otherAction&&!occupied&&!phase_&&!out.pickedMagazine&&!out.grabbedHolster)
        {
            if(singleShake_.Update(s.primary-s.head,s.poseNow?s.poseNow:s.now,c.shakeTravel)&&s.now>=cooldown_)
            {
                pulse_=c.reloadButton;pulseUntil_=s.now+120;cooldown_=s.now+500;
                out.reloadRequested=true;out.primaryHaptic=0.45f;
            }
        }
        else singleShake_.Reset();
        if(heldP) armedP_=false;
        if(heldS) armedS_=false;
        if(s.now<pulseUntil_&&!s.otherAction) { out.buttons=pulse_;out.pulseUntil=pulseUntil_; }
        else { pulseUntil_=0;pulse_=0; }
        out.holdingMagazine=phase_==1;
        return out;
    }
private:
    SingleNeedleShake singleShake_{};
    GameTitle title_{GameTitle::None}; uint32_t generation_{};
    uint64_t space_{},last_{},started_{},cooldown_{},pulseUntil_{};
    uint32_t pulse_{},reloadButton_{},swapButton_{};
    int phase_{},location_{},pouchLocation_{};
    Vec pouchOffset_{},holsterCustom_{};
    bool left_{},reload_{},holsters_{},armedP_{},armedS_{},ownedP_{},ownedS_{},leftPouch_{},available_{};
    bool slide_{true},click_{};
    bool shake_{};
    uint64_t weaponGraph_{},shakeAt_{};
    Vec shakeOrigin_{};
    float shakeExtreme_{},shakeLow_{},shakeHigh_{};
    int shakeDirection_{},shakeStrokes_{};
    Vec grabbedAt_{};
};

// Snapshot transport is validated again on XInput's thread. Render suspension,
// pause or title changes must never replay a cached reload/switch command.
inline bool Fresh(uint64_t now,uint64_t sampled,GameTitle title,GameTitle sampledTitle,
    uint32_t generation,uint32_t sampledGeneration,bool gameplay) noexcept
{
    return gameplay&&TitleIndex(title)>=0&&title==sampledTitle&&generation&&
        generation==sampledGeneration&&sampled&&now>=sampled&&now-sampled<=150;
}
} // namespace weapon_interaction
