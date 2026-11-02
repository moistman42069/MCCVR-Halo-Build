#pragma once
#include "weapon_muzzle.h"

namespace weapon_alignment
{
// Re-express an authored muzzle in the matching pre-presentation frame.
// The caller supplies same-sample rigid frames from its own title adapter.
inline bool Reframe(const float from[9],const float fromPosition[3],
    const float to[9],const float toPosition[3],weapon_muzzle::Ray& ray) noexcept
{
    for (int i=0;i<9;++i) if(!std::isfinite(from[i])||!std::isfinite(to[i]))return false;
    for (int col=0;col<3;++col) {
        float a=0,b=0;
        for(int row=0;row<3;++row) {a+=from[col*3+row]*from[col*3+row];b+=to[col*3+row]*to[col*3+row];}
        if(std::fabs(a-1)>.05f||std::fabs(b-1)>.05f)return false;
    }
    for(int a=0;a<3;++a)for(int b=a+1;b<3;++b) {
        float x=0,y=0;
        for(int row=0;row<3;++row) {x+=from[a*3+row]*from[b*3+row];y+=to[a*3+row]*to[b*3+row];}
        if(std::fabs(x)>.05f||std::fabs(y)>.05f)return false;
    }
    const auto rotate=[&](const float* value,float* output) {
        float local[3]{};
        for(int col=0;col<3;++col)for(int row=0;row<3;++row)local[col]+=from[col*3+row]*value[row];
        for(int row=0;row<3;++row)output[row]=to[row]*local[0]+to[3+row]*local[1]+to[6+row]*local[2];
    };
    auto result=ray;float relative[3]{};
    for(int i=0;i<3;++i)relative[i]=ray.position[i]-fromPosition[i];
    rotate(relative,result.position);rotate(ray.direction,result.direction);rotate(ray.up,result.up);
    for(int i=0;i<3;++i) {
        result.position[i]+=toPosition[i];
        if(!std::isfinite(result.position[i])||!std::isfinite(result.direction[i])||!std::isfinite(result.up[i]))return false;
    }
    ray=result;return true;
}
}
