#pragma once
#include <algorithm>
#include <cmath>

namespace weapon_alignment
{
// Profile order follows Config_TitleProfileName; values are metres in each
// hand's local forward/right/up frame. Left baselines are already signed.
struct Offsets { float forward,right,up; };
inline constexpr Offsets kRightOffsets[]{
    {-.16f,-.04f,-.06f},{-.16f,-.03f,-.05f},{-.16f,-.04f,-.06f},
    {-.20f,0.f,-.04f},{-.16f,.02f,-.04f},{-.16f,.02f,-.10f},{-.16f,.02f,-.05f}};
inline constexpr Offsets kLeftOffsets[]{
    {-.16f,.04f,0.f},{-.16f,.03f,-.01f},{-.16f,.04f,-.06f},
    {-.20f,-.02f,-.09f},{-.10f,0.f,-.04f},{-.12f,.01f,-.04f},{-.16f,-.02f,-.05f}};
inline constexpr bool HasProfile(int profile) noexcept {return profile>=0 && profile<7;}
inline constexpr Offsets Baseline(int profile) noexcept
{return HasProfile(profile)?kRightOffsets[profile]:Offsets{-.14f,0,0};}
// Absolute safety envelopes cover +/-0.30 m around every shipped hand baseline.
inline constexpr float kForwardMinimum=-.50f,kForwardMaximum=.20f;
inline constexpr float kRightMinimum=-.34f,kRightMaximum=.34f;
inline constexpr float kUpMinimum=-.40f,kUpMaximum=.30f;
// Stored values remain absolute for existing configs and weapon profiles.
// Shared slider deltas move both hands, mirroring only the lateral delta.
inline float Forward(bool left,float gun,float leftExtra,int profile=-1) noexcept
{
    const float correction=left&&HasProfile(profile)?kLeftOffsets[profile].forward-kRightOffsets[profile].forward:0.f;
    return std::clamp(gun,kForwardMinimum,kForwardMaximum)+correction+(left?std::clamp(leftExtra,-.15f,.30f):0.f);
}
inline float Right(bool left,float value,int profile=-1) noexcept
{
    const float correction=left&&HasProfile(profile)?kLeftOffsets[profile].right+kRightOffsets[profile].right:0.f;
    return (left?-1.f:1.f)*std::clamp(value,kRightMinimum,kRightMaximum)+correction;
}
inline float Up(float value,bool left=false,int profile=-1) noexcept
{
    const float correction=left&&HasProfile(profile)?kLeftOffsets[profile].up-kRightOffsets[profile].up:0.f;
    return std::clamp(value,kUpMinimum,kUpMaximum)+correction;
}
inline constexpr float kH2ClassicVisualPitch=-9.5f;
inline constexpr float kH2ClassicVisualYaw=-1.f;
// The retired Classic sliders' defaults. A saved halo2_classic_* value keeps its difference from
// these on top of the fixed correction above (forward/right/up/roll defaulted to 0).
inline constexpr float kH2ClassicLegacyPitch=-9.5f;
inline constexpr float kH2ClassicLegacyYaw=1.f;
inline float H3SecondaryYaw(unsigned slot,unsigned checksum) noexcept
{ return slot==1 && (checksum==0x171C0B10u || checksum==0x16120C11u) ? 30.f : 0.f; }
inline bool RotateYaw(const float frame[9],const float pivot[3],float degrees,
    float basis[9],float position[3]) noexcept
{
    if (!std::isfinite(degrees)) return false;
    for(int i=0;i<9;++i) if(!std::isfinite(frame[i])||!std::isfinite(basis[i]))return false;
    for(int i=0;i<3;++i) if(!std::isfinite(pivot[i])||!std::isfinite(position[i]))return false;
    const float angle=-degrees*.01745329252f,c=std::cos(angle),s=std::sin(angle);
    const auto rotate=[&](const float* v,float* out) {
        float local[3]{};
        for(int col=0;col<3;++col)for(int row=0;row<3;++row)local[col]+=frame[col*3+row]*v[row];
        const float x=c*local[0]-s*local[1],y=s*local[0]+c*local[1];
        for(int row=0;row<3;++row)out[row]=frame[row]*x+frame[3+row]*y+frame[6+row]*local[2];
    };
    float result[9]{},offset[3]{},p[3]{};
    for(int col=0;col<3;++col)rotate(basis+col*3,result+col*3);
    for(int i=0;i<3;++i)offset[i]=position[i]-pivot[i];
    rotate(offset,p);
    for(int i=0;i<9;++i) { if(!std::isfinite(result[i]))return false; }
    for(int i=0;i<3;++i) { p[i]+=pivot[i];if(!std::isfinite(p[i]))return false; }
    for(int i=0;i<9;++i)basis[i]=result[i];
    for(int i=0;i<3;++i)position[i]=p[i];
    return true;
}
}
