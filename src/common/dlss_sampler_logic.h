#pragma once
#include <algorithm>
#include <cmath>
namespace dlss_sampler
{
// DLSS18 was headset-rejected for cost/smear. Keep this experiment inert,
// including its resource-creation hook, until a measured replacement is accepted.
inline constexpr bool kEnabled=false;
inline constexpr unsigned kModes=5;
inline constexpr float kBias[kModes]{-1.0f,-1.5849625007f,-1.7858751946f,-2.0f,-2.5849625007f};
inline int Mode(float bias) noexcept
{
    if(!std::isfinite(bias)) return -1;
    int selected=-1;float error=.015f;
    for(unsigned i=0;i<kModes;++i)
        if(const float difference=std::fabs(bias-kBias[i]);difference<error)
        {error=difference;selected=static_cast<int>(i);}
    return selected;
}
inline float Adjust(float nativeBias,unsigned mode) noexcept
{return std::clamp(nativeBias+kBias[mode],-16.0f,15.99f);}
}
