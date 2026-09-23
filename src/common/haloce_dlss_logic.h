#pragma once
#include "haloce_anniversary_logic.h"
#include "dlss_logic.h"

namespace halo_ce {
// CE's native camera upload writes a separate Saber-space origin and a
// transposed rotation-only world projection. Remove only the camera rotation;
// depth/clip/centre terms come from those actual uploaded constants.
inline dlss::CameraSample DlssSaberCamera(const SaberCamera& camera,
    const float origin[4],const float packed[16]) noexcept
{
    Camera verified{};
    if(!origin||!packed||!NativeCameraFromSaber(camera,verified)) return {};
    const float* pose=camera.pose.matrix;
    for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(origin[axis])||std::abs(origin[axis]-pose[12+axis])>.001f) return {};
    if(origin[3]!=1) return {};
    float projection[16]{};
    // Each row is the clip-space response to one local camera basis vector.
    // Native packing is column-major; no engine/view offsets are invented.
    for(unsigned row=0;row<3;++row)
        for(unsigned column=0;column<4;++column)
            for(unsigned axis=0;axis<3;++axis)
                projection[row*4+column]+=pose[row*4+axis]*packed[column*4+axis];
    for(unsigned column=0;column<4;++column) projection[12+column]=packed[column*4+3];
    auto result=dlss::MakeCameraSample(origin,pose+8,pose+4,projection,0,0);
    if(!result.valid) return {};
    // CE's ordinary camera is native units; Saber uses rotated 3.048x units.
    // Keep one world convention across Original/Anniversary switches.
    const Vec3 position=FromSaber({origin[0],origin[1],origin[2]})*(1.f/kSaberUnitsPerNativeUnit);
    const Vec3 forward=FromSaber({pose[8],pose[9],pose[10]});
    const Vec3 up=FromSaber({pose[4],pose[5],pose[6]});
    result.position[0]=position.x;result.position[1]=position.y;result.position[2]=position.z;
    result.forward[0]=forward.x;result.forward[1]=forward.y;result.forward[2]=forward.z;
    result.up[0]=up.x;result.up[1]=up.y;result.up[2]=up.z;
    dlss::Cross(result.forward,result.up,result.right);
    result.projection.depthB/=kSaberUnitsPerNativeUnit;
    return result;
}
inline dlss::CameraSample DlssClassicCamera(const Camera& camera,const float projection[16]) noexcept {
    if(!Valid(camera)) return {};
    const float position[]{camera.position.x,camera.position.y,camera.position.z};
    const float forward[]{camera.forward.x,camera.forward.y,camera.forward.z};
    const float up[]{camera.up.x,camera.up.y,camera.up.z};
    return dlss::MakeCameraSample(position,forward,up,projection,0,0);
}
}
