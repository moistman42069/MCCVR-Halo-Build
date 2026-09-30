#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Arm-swing locomotion inspired by the user's Project06-VR behavior request.
// Outputs ordinary bounded native locomotion, never a physics speed override.
namespace physical_running {
struct Vec { float x=0,y=0,z=0; };
inline Vec Sub(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline bool Finite(Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
struct Sample {
    uint64_t serial=0,epoch=0;
    int64_t timeNs=0;
    Vec head{},hands[2]{};
    float forwardX=0,forwardZ=-1;
    bool valid=false;
};
class Gesture {
    Sample previous_{};
    int direction_[2]{};
    int64_t reversal_[2]{},motionAt_=0;
    float envelope_=0,output_=0;
public:
    void Reset() noexcept {*this={};}
    float Update(const Sample& s,float sensitivity,float maximum) noexcept {
        if(!s.valid||!s.serial||!s.epoch||s.timeNs<=0||!Finite(s.head)||
           !Finite(s.hands[0])||!Finite(s.hands[1])||!std::isfinite(s.forwardX)||
           !std::isfinite(s.forwardZ)||!std::isfinite(sensitivity)||!std::isfinite(maximum))
        {Reset();return 0;}
        const float heading=std::hypot(s.forwardX,s.forwardZ);
        if(heading<.2f){Reset();return 0;}
        if(previous_.serial==s.serial&&previous_.timeNs==s.timeNs&&previous_.epoch==s.epoch)
            return output_;
        if(!previous_.serial||s.epoch!=previous_.epoch||s.serial<=previous_.serial||
           s.timeNs<=previous_.timeNs||s.timeNs-previous_.timeNs>100'000'000)
        {Reset();previous_=s;return 0;}
        const float dt=float(double(s.timeNs-previous_.timeNs)*1e-9);
        float f[2]{},up[2]{},speed[2]{};
        for(int h=0;h<2;++h) {
            const Vec d=Sub(Sub(s.hands[h],s.head),Sub(previous_.hands[h],previous_.head));
            const Vec v{d.x/dt,d.y/dt,d.z/dt};
            speed[h]=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
            if(!std::isfinite(speed[h])||speed[h]>8){Reset();previous_=s;return 0;}
            f[h]=(v.x*s.forwardX+v.z*s.forwardZ)/heading;up[h]=v.y;
            const int sign=f[h]>.12f?1:(f[h]<-.12f?-1:0);
            if(sign&&direction_[h]&&sign!=direction_[h])reversal_[h]=s.timeNs;
            if(sign)direction_[h]=sign;
        }
        const float sagittal[2]{std::hypot(f[0],up[0]),std::hypot(f[1],up[1])};
        const bool alternating=f[0]*f[1]+up[0]*up[1]<-.1f*sagittal[0]*sagittal[1];
        const bool cycling=reversal_[0]&&reversal_[1]&&
            s.timeNs-reversal_[0]<800'000'000&&s.timeNs-reversal_[1]<800'000'000;
        envelope_*=std::exp(-dt*3.f);
        if(alternating&&cycling&&sagittal[0]>.55f*speed[0]&&sagittal[1]>.55f*speed[1]) {
            envelope_=std::max(envelope_,std::min(sagittal[0],sagittal[1]));
            if(envelope_>.15f)motionAt_=s.timeNs;
        }
        float target=0;
        if(motionAt_&&s.timeNs-motionAt_<250'000'000)
            target=std::clamp((envelope_-.15f)*std::clamp(sensitivity,.25f,3.f),0.f,1.f)*
                std::clamp(maximum,.1f,1.f);
        const float step=dt*(target>output_?1.5f:4.f);
        output_+=std::clamp(target-output_,-step,step);
        output_=std::clamp(output_,0.f,std::clamp(maximum,.1f,1.f));
        if(target==0&&output_<.001f)output_=0;
        previous_=s;return output_;
    }
};
}
