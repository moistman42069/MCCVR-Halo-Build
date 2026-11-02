#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace weapon_animation
{
// Rigid animation poses only. Adapters remove their own native camera/root
// before observation; a world-space pose must never become a reference.
struct Pose {
    float rotation[9]{1,0,0,0,1,0,0,0,1};
    float translation[3]{};
};
inline bool Valid(const Pose& p)
{
    for(float v:p.rotation)if(!std::isfinite(v))return false;
    for(float v:p.translation)if(!std::isfinite(v))return false;
    for(int a=0;a<3;++a)for(int b=a;b<3;++b) {
        float dot=0;for(int r=0;r<3;++r)dot+=p.rotation[a*3+r]*p.rotation[b*3+r];
        if(std::fabs(dot-(a==b?1.f:0.f))>.03f)return false;
    }
    const auto* r=p.rotation;
    return r[0]*(r[4]*r[8]-r[7]*r[5])-r[3]*(r[1]*r[8]-r[7]*r[2])+r[6]*(r[1]*r[5]-r[4]*r[2])>.95f;
}
inline Pose Relative(const Pose& reference,const Pose& current)
{
    Pose out;
    for(int r=0;r<3;++r) {
        out.translation[r]=0;
        for(int k=0;k<3;++k)out.translation[r]+=reference.rotation[r*3+k]*(current.translation[k]-reference.translation[k]);
        for(int c=0;c<3;++c) {
            out.rotation[c*3+r]=0;
            for(int k=0;k<3;++k)out.rotation[c*3+r]+=reference.rotation[r*3+k]*current.rotation[c*3+k];
        }
    }
    return out;
}
inline Pose Compose(const Pose& a,const Pose& b)
{
    Pose out;
    for(int r=0;r<3;++r) {
        out.translation[r]=a.translation[r];
        for(int k=0;k<3;++k)out.translation[r]+=a.rotation[k*3+r]*b.translation[k];
        for(int c=0;c<3;++c) {
            out.rotation[c*3+r]=0;
            for(int k=0;k<3;++k)out.rotation[c*3+r]+=a.rotation[k*3+r]*b.rotation[c*3+k];
        }
    }
    return out;
}
// Smooth authored rotation only, before composing with the live controller.
// H3's 0.02 blend runs per interpolation invocation, not per headset frame.
// The supplied 90Hz session reports about 520 samples/second; use that response
// consistently across titles (about a 95ms time constant). Duplicate
// eye/palette observations at one timestamp must not speed up the response.
struct RotationFilter
{
    std::array<uint64_t,4> key{};
    Pose value{};
    uint64_t last{};
    bool ready=false;
    bool Update(std::array<uint64_t,4> identity,const Pose& input,uint64_t now,Pose& output)
    {
        if(!Valid(input)){*this={};return false;}
        Pose target=input;for(float& v:target.translation)v=0;
        if(!ready||identity!=key||now<last||now-last>500) {
            key=identity;last=now;ready=true;value=target;output=value;return true;
        }
        const float alpha=1.f-std::pow(.98f,float(now-last)*.52f);
        last=now;
        const Pose relative=Relative(value,target);
        const auto* m=relative.rotation;
        float q[4]{};
        const float trace=m[0]+m[4]+m[8];
        if(trace>0) {
            const float s=2.f*std::sqrt(trace+1.f);
            q[3]=.25f*s;q[0]=(m[5]-m[7])/s;q[1]=(m[6]-m[2])/s;q[2]=(m[1]-m[3])/s;
        } else {
            const int i=m[0]>m[4]?(m[0]>m[8]?0:2):(m[4]>m[8]?1:2);
            const int j=(i+1)%3,k=(i+2)%3;
            const float s=2.f*std::sqrt(1.f+m[i*3+i]-m[j*3+j]-m[k*3+k]);
            if(!(s>0)){*this={};return false;}
            q[i]=.25f*s;q[j]=(m[j*3+i]+m[i*3+j])/s;
            q[k]=(m[k*3+i]+m[i*3+k])/s;q[3]=(m[j*3+k]-m[k*3+j])/s;
        }
        if(q[3]<0)for(float& v:q)v=-v;
        // Slerp from identity along the shortest arc, including near-pi poses.
        const float length=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]);
        Pose step{};
        if(length>1.e-7f) {
            const float angle=std::atan2(length,q[3])*alpha;
            const float scale=std::sin(angle)/length;
            const float x=q[0]*scale,y=q[1]*scale,z=q[2]*scale,w=std::cos(angle);
            const float basis[9]={1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),
                2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),
                2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)};
            for(int i=0;i<9;++i)step.rotation[i]=basis[i];
        }
        value=Compose(value,step);
        if(!Valid(value)){*this={};return false;}
        output=value;return true;
    }
};
// Preserve tracking translation and parent rotation exactly; only the authored
// parent-local rotation is filtered. A failed optional filter leaves output alone.
inline bool SmoothLocalRotation(RotationFilter& filter,std::array<uint64_t,4> key,
    const Pose& parent,const Pose& world,uint64_t now,Pose& output)
{
    if(!Valid(parent)||!Valid(world)){filter={};return false;}
    Pose local=Relative(parent,world),smoothed{};
    if(!filter.Update(key,local,now,smoothed))return false;
    Pose result=Compose(parent,smoothed);
    for(int i=0;i<3;++i)result.translation[i]=world.translation[i];
    if(!Valid(result))return false;
    output=result;return true;
}
// Bounded, caller-owned cache. Only a fully settled reference may be reused;
// the complete owner/model/renderer key must match, never just a weapon name.
struct ReferenceCache
{
    struct Entry {std::array<uint64_t,4> key{};Pose pose{};bool valid=false;};
    std::array<Entry,16> entries{};
    unsigned next=0;
    bool Find(const std::array<uint64_t,4>& key,Pose& pose) const {
        for(const auto& e:entries)if(e.valid&&e.key==key){pose=e.pose;return true;}
        return false;
    }
    void Store(const std::array<uint64_t,4>& key,const Pose& pose) {
        for(auto& e:entries)if(e.valid&&e.key==key){e.pose=pose;return;}
        entries[next]={key,pose,true};next=(next+1)%entries.size();
    }
};
// Disabled after an equip-time reload poisoned a reused offhand reference.
// Keep the cache implementation dormant; each acquisition must settle again.
inline constexpr bool kReuseSettledReferences=false;
struct Reference
{
    std::array<uint64_t,4> key{};
    Pose anchor{},reference{};
    RotationFilter smoothing{};
    uint64_t started{},stableSince{},last{};
    bool observed=false,ready=false;
    // A bounded settling heuristic, not a native idle-animation detector.
    // Keep controller alignment while drawing/equipping; freeze the reference
    // only after the initial delay AND a quiet window. Never chase later recoil
    // or reload motion with a moving average.
    bool Update(std::array<uint64_t,4> identity,const Pose& local,uint64_t now,
                float unitsPerMetre,Pose& delta,bool smooth=false,ReferenceCache* cache=nullptr)
    {
        if(!kReuseSettledReferences)cache=nullptr;
        delta={};
        if(!Valid(local)||!std::isfinite(unitsPerMetre)||unitsPerMetre<=0) {
            observed=ready=false;smoothing={};return false;
        }
        if(!observed||key!=identity||now<last||now-last>500) {
            *this={};observed=true;key=identity;anchor=local;
            started=stableSince=last=now;
            if(!cache||!cache->Find(identity,reference))return false;
            ready=true;
        }
        last=now;
        if(!ready) {
            const Pose change=Relative(anchor,local);
            const float trace=change.rotation[0]+change.rotation[4]+change.rotation[8];
            if(trace<2.997259f) {
                anchor=local;stableSince=now;
            }
            if(now-started<500||now-stableSince<500)return false;
            reference=local;ready=true;
            if(cache)cache->Store(identity,reference);
        }
        delta=Relative(reference,local);
        // Controllers own wrist position. Retain pitch/yaw/roll animation only;
        // camera/stance translations must neither delay settling nor move hands.
        for(float& value:delta.translation)value=0;
        return smooth?smoothing.Update(identity,delta,now,delta):Valid(delta);
    }
};
}
