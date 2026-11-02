#pragma once
#include "halo2_render_logic.h"
#include "weapon_muzzle.h"

// Remove only the presentation-frame rotation/translation from an authored
// muzzle. Preserve animation, scale and the caller's collision displacement.
inline bool Halo2RemoveVisualFrame(const Halo2CameraBasis& visual,
    const Halo2CameraBasis& neutral,const float collisionDelta[3],
    weapon_muzzle::Ray& ray) noexcept
{
    if (!Halo2ValidateCameraBasis(visual)||!Halo2ValidateCameraBasis(neutral)) return false;
    float fromLeft[3]{},toLeft[3]{};
    for (int i=0;i<3;++i) {
        int j=(i+1)%3,k=(i+2)%3;
        fromLeft[i]=visual.up[j]*visual.forward[k]-visual.up[k]*visual.forward[j];
        toLeft[i]=neutral.up[j]*neutral.forward[k]-neutral.up[k]*neutral.forward[j];
        if (!std::isfinite(collisionDelta[i])) return false;
    }
    auto result=ray;
    const auto rotate=[&](const float* value,float* output) {
        float local[3]{};
        for (int i=0;i<3;++i) {
            local[0]+=visual.forward[i]*value[i];
            local[1]+=fromLeft[i]*value[i];
            local[2]+=visual.up[i]*value[i];
        }
        for (int i=0;i<3;++i) output[i]=neutral.forward[i]*local[0]+toLeft[i]*local[1]+neutral.up[i]*local[2];
    };
    float relative[3]{};
    for (int i=0;i<3;++i) relative[i]=ray.position[i]-visual.position[i]-collisionDelta[i];
    rotate(relative,result.position);rotate(ray.direction,result.direction);rotate(ray.up,result.up);
    for (int i=0;i<3;++i) {
        result.position[i]+=neutral.position[i]+collisionDelta[i];
        if (!std::isfinite(result.position[i])||!std::isfinite(result.direction[i])||!std::isfinite(result.up[i])) return false;
    }
    ray=result;return true;
}
