#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
namespace authored_grab {
using Key=std::array<uint32_t,4>; // title, generation, unit, weapon
using Point=std::array<float,3>; // primary-controller local OpenXR metres
inline bool Valid(Point p){float n=0;for(float v:p){if(!std::isfinite(v))return false;n+=v*v;}return n<=1.0f;}
inline float Distance2(Point a,Point b){float n=0;for(int i=0;i<3;++i)n+=(a[i]-b[i])*(a[i]-b[i]);return n;}
// Retained settling implementation; live grip acquisition no longer uses this
// timer. Animation reference settling is separate and remains unchanged.
struct Learner {
    Key key{};Point anchor{};uint64_t start{},quiet{},last{};bool active=false;
    bool Observe(Key k,Point p,uint64_t now){
        if(!Valid(p)){active=false;return false;}
        if(!active||key!=k||now<last||now-last>250){key=k;anchor=p;start=quiet=last=now;active=true;return false;}
        last=now;
        if(Distance2(anchor,p)>.0009f){anchor=p;quiet=now;}
        return now-start>=500&&now-quiet>=500;
    }
};
struct Publication {
    std::atomic<uint64_t> sequence{0},at{0};std::atomic<bool> calibrated{false};std::atomic<uint32_t> key[4]{};std::atomic<float> point[3]{};
    void Publish(Key k,Point p,uint64_t now,bool ready=true){
        auto seq=sequence.load();if((seq&1)||!sequence.compare_exchange_strong(seq,seq+1))return;
        bool same=true;for(int i=0;i<4;++i)same=same&&key[i].load()==k[i];
        if(!same)calibrated.store(false);
        for(int i=0;i<4;++i)key[i].store(k[i]);
        if(ready){
            // Invalid live geometry must not refresh an older valid endpoint.
            const bool valid=now&&Valid(p);
            if(valid)for(int i=0;i<3;++i)point[i].store(p[i]);
            calibrated.store(valid);
        }
        at.store(now);sequence.store(seq+2);
    }
    // An attached frame can refresh an existing endpoint, never claim a new
    // owner before any valid geometry was observed for that owner.
    void Refresh(Key k,uint64_t now){
        auto seq=sequence.load();if((seq&1)||!sequence.compare_exchange_strong(seq,seq+1))return;
        bool same=true;for(int i=0;i<4;++i)same=same&&key[i].load()==k[i];
        if(same)at.store(now);
        sequence.store(seq+2);
    }
    bool Owns(Key k)const{
        const auto seq=sequence.load();if(!seq||(seq&1))return false;
        bool same=true;for(int i=0;i<4;++i)same=same&&key[i].load()==k[i];
        return same&&sequence.load()==seq;
    }
    bool Read(Key k,uint64_t now,Point& p)const{
        const auto seq=sequence.load();if(!seq||(seq&1))return false;
        const auto time=at.load();const bool ready=calibrated.load();bool matches=true;
        for(int i=0;i<4;++i)matches=matches&&key[i].load()==k[i];
        for(int i=0;i<3;++i)p[i]=point[i].load();
        return sequence.load()==seq&&ready&&matches&&time&&now>=time&&now-time<=100&&Valid(p);
    }
};
inline Publication publication;
inline std::atomic<float> lastX{0},lastY{0},lastZ{0};
inline std::atomic<uint64_t> hits{0},misses{0},fallbacks{0};
// Short authored separation permits a side-by-side support grip. Longer arms
// use a short longitudinal capsule; no weapon-specific calibration.
inline bool Short(Point centre){return Valid(centre)&&Distance2(centre,{0,0,0})<=.24f*.24f;}
inline bool ShortHit(Point p){return Valid(p)&&std::fabs(p[0])<=.22f&&p[1]>=-.19f&&p[1]<=.14f&&p[2]>=-.14f&&p[2]<=.12f;}
// Binary exclusion: outside the close-hand volume, keep the user's full
// configured influence. Physical grip separation ignores controller rotation.
// `rangeMetres` is the volume's side measure (two_hand_one_hand_range_m); the others scale with it.
inline constexpr float kOneHandRangeDefaultMetres=.24f;
inline float CloseHandAuthority(Point local,float rangeMetres=kOneHandRangeDefaultMetres){
    if(!Valid(local))return 0;
    const float range=rangeMetres/kOneHandRangeDefaultMetres;
    const float close=.16f*range;
    const bool withinClose=Distance2(local,{0,0,0})<=close*close;
    const bool lateral=std::fabs(local[0])<=.24f*range&&
        std::fabs(local[1])<=.14f*range&&std::fabs(local[2])<=.12f*range;
    return withinClose||lateral?0.f:1.f;
}
// Decided by the weapon's grip zone and not by where the hands are (two_hand_grip_zone_authority). A brief tracking
// drop-out of an occluded support controller reads as the hand far away, which on a pistol hold switched the aim to
// two-handed for those frames. A short grip zone (a side-by-side hold) never steers; a long one always does, except
// with the hands almost together, where the line between them has no usable direction. It lets go of the aim across
// the distance out to double that, so the aim does not snap to the main hand.
inline constexpr float kGripZoneNoLineMetres=.04f;
inline constexpr float kGripZoneFullLineMetres=2*kGripZoneNoLineMetres;
inline float GripZoneAuthority(bool shortZone,Point local){
    if(shortZone||!Valid(local))return 0;
    const float distance=std::sqrt(Distance2(local,{0,0,0}));
    const float authority=(distance-kGripZoneNoLineMetres)/(kGripZoneFullLineMetres-kGripZoneNoLineMetres);
    return authority<=0?0.f:authority>=1?1.f:authority;
}
// Which kind a zone is for the aim: 2 short, 1 long, by the player's range (two_hand_one_hand_range_m). The grip point
// moves with the weapon's animations (one gun read 0.16 m and 0.23 m within a second, in play), so while the support
// hand holds, the kind stands until the weapon or the range changes. It stands only once the point has come to rest: a
// weapon drawn with the grip held reads its draw animation first (a pistol read 0.37 m, then 0.13 m at rest, in play).
// Between grips a gun whose grip sits at the range would read both ways from sensor noise alone (an SMG at 0.25 m with
// the range at 0.25 m, in play), and each grip would keep whichever came last: its answer stands across 1 cm either
// side of the range, until the weapon or the range changes. Single writer: the frame thread.
inline constexpr uint64_t kGripZoneRestMs=300;
inline constexpr float kGripZoneRestMetres=.03f;
inline constexpr float kGripZoneKindHoldMetres=.01f;
struct GripZoneKindMemory{
    Key key{};float range=0;int kind=0;Point anchor{};uint64_t restSince=0;bool atRest=false;
    int Step(Key k,Point centre,float rangeMetres,bool gripping,uint64_t now){
        if(!Valid(centre))return kind;
        if(!kind||key!=k||range!=rangeMetres){
            key=k;range=rangeMetres;anchor=centre;restSince=now;atRest=false;kind=0;
        }else if(atRest&&gripping)return kind;
        else if(Distance2(anchor,centre)>kGripZoneRestMetres*kGripZoneRestMetres){
            anchor=centre;restSince=now;atRest=false;
        }else atRest=now-restSince>=kGripZoneRestMs;
        const float d2=Distance2(centre,{0,0,0});
        const float low=rangeMetres-kGripZoneKindHoldMetres,high=rangeMetres+kGripZoneKindHoldMetres;
        if(kind==2&&d2<=high*high)return kind;
        if(kind==1&&d2>=low*low)return kind;
        kind=d2<=rangeMetres*rangeMetres?2:1;
        return kind;
    }
};
inline bool Hit(Point centre,Point sample){
    if(!Valid(centre)||!Valid(sample))return false;
    // 22cm segment plus 12cm rounded ends: 46cm total. OpenXR forward
    // is negative Z: trim 4cm from the front and 4cm from the rear.
    Point nearest=centre;
    // Extend only downward by 5cm; keep the top and lateral boundaries.
    nearest[1]+=std::fmax(-.05f,std::fmin(0.f,sample[1]-centre[1]));
    nearest[2]+=std::fmax(-.11f,std::fmin(.11f,sample[2]-centre[2]));
    return Distance2(nearest,sample)<.12f*.12f;
}
}
