#pragma once

#include "haloce_render_logic.h"
#include <cstring>

namespace halo_ce
{
// E-CE-4: +0x2EA720 copies 7*0x80+0x18 bytes from CE's Saber
// camera, then stores that camera at view-list +0x40 + index*0x3C8.
// The active render list is renderer+0xB0, hence camera 0 at +0xF0.
// Unknown/derived fields remain opaque and must be restored as a whole.
struct SaberCamera
{
    SaberPose pose;
    float view[16]{};
    float nearPlane{},farPlane{};
    uint8_t derived88[0xb4]{};
    float viewportX{},viewportY{},viewportWidth{},viewportHeight{};
    float horizontalFovDegrees{},verticalFovDegrees{},aspectRatio{};
    float rasterScaleX{},rasterScaleY{};
    uint8_t derived160[0x238]{};
};
static_assert(sizeof(SaberCamera)==0x398);
static_assert(offsetof(SaberCamera,nearPlane)==0x80);
static_assert(offsetof(SaberCamera,viewportX)==0x13c);
static_assert(offsetof(SaberCamera,horizontalFovDegrees)==0x14c);
static_assert(offsetof(SaberCamera,aspectRatio)==0x154);
static_assert(offsetof(SaberCamera,derived160)==0x160);

// E-CE-4: finite output guards for fields written by the native view,
// projection and culling rebuilds. Do not scan the opaque resource tail as
// floats: it also contains pointers and native flags.
inline bool FiniteSaberDerivedCamera(const SaberCamera& camera)
{
    const auto finiteRange=[&](size_t offset,size_t count) {
        for (size_t i=0;i<count;++i)
        {
            float value{};
            std::memcpy(&value,reinterpret_cast<const uint8_t*>(&camera)+offset+4*i,4);
            if (!std::isfinite(value)) return false;
        }
        return true;
    };
    return finiteRange(0x40,16)&& // native view matrix
        finiteRange(0x8c,8)&&    // four projection-bound corners
        finiteRange(0x12c,4)&&   // projection origin/width/height
        finiteRange(0x158,2)&&  // raster scale
        finiteRange(0x318,24)&& // eight world-space culling corners
        finiteRange(0x378,6);   // world-space bounds
}

inline Vec3 FromSaber(Vec3 v) { return {v.x,-v.z,v.y}; }

inline bool NativeCameraFromSaber(const SaberCamera& saber,Camera& out)
{
    const float* matrix=saber.pose.matrix;
    for (int i=0;i<16;++i) if (!std::isfinite(matrix[i])) return false;
    if (std::fabs(matrix[3])>0.0001f||std::fabs(matrix[7])>0.0001f||
        std::fabs(matrix[11])>0.0001f||std::fabs(matrix[15]-1)>0.0001f||
        !std::isfinite(saber.viewportX)||!std::isfinite(saber.viewportY)||
        !std::isfinite(saber.viewportWidth)||!std::isfinite(saber.viewportHeight)||
        saber.viewportX!=0||saber.viewportY!=0||
        saber.viewportWidth<1||saber.viewportWidth>16384||
        saber.viewportHeight<1||saber.viewportHeight>16384||
        std::floor(saber.viewportWidth)!=saber.viewportWidth||
        std::floor(saber.viewportHeight)!=saber.viewportHeight)
        return false;
    const Vec3 right{matrix[0],matrix[1],matrix[2]};
    const Vec3 up{matrix[4],matrix[5],matrix[6]};
    const Vec3 forward{matrix[8],matrix[9],matrix[10]};
    const Vec3 rightError=right-Cross(forward,up);
    if (Dot(rightError,rightError)>0.0001f) return false;
    Camera candidate{};
    candidate.position=FromSaber({matrix[12],matrix[13],matrix[14]})*
        (1.0f/kSaberUnitsPerNativeUnit);
    candidate.forward=FromSaber(forward);
    candidate.up=FromSaber(up);
    candidate.verticalFov=saber.verticalFovDegrees*0.017453292519943295f;
    candidate.viewport=candidate.window={0,0,static_cast<int16_t>(saber.viewportHeight),
        static_cast<int16_t>(saber.viewportWidth)};
    candidate.nearPlane=saber.nearPlane/kSaberUnitsPerNativeUnit;
    candidate.farPlane=saber.farPlane/kSaberUnitsPerNativeUnit;
    if (!Valid(candidate)) return false;
    out=candidate;
    return true;
}

// Stages owned pose/FOV fields only. The caller must run CE's own view,
// frustum and culling rebuilds before rendering, then restore the saved camera.
// This function alone never produces an engine-ready derived camera.
inline bool StageSaberEye(const SaberCamera& stock,const Tracking& tracking,
    const Reference& reference,int eye,float unitsPerMeter,bool positional,
    SaberCamera& out,Cover& cover)
{
    Camera native{},posed{};
    Cover candidateCover{};
    SaberPose candidatePose{};
    if (!NativeCameraFromSaber(stock,native)||
        !BuildCover(tracking,native.viewport,candidateCover)||
        !BuildEye(native,tracking,reference,eye,unitsPerMeter,positional,candidateCover,posed)||
        !BuildSaberPose(posed,{},0,candidatePose))
        return false;
    SaberCamera candidate=stock;
    candidate.pose=candidatePose;
    constexpr float radiansToDegrees=57.29577951308232f;
    candidate.horizontalFovDegrees=2*candidateCover.halfX*radiansToDegrees;
    candidate.verticalFovDegrees=2*candidateCover.halfY*radiansToDegrees;
    candidate.aspectRatio=stock.viewportHeight/stock.viewportWidth;
    out=candidate;
    cover=candidateCover;
    return true;
}

// AIM FRAME ZOOM: an eye of the frame (BuildAimFrameCamera from `centre`, the centre eye built from this same stock
// camera), staged as StageSaberEye stages an eye: the pose and both angles are the picture's, and so is the cover the
// finished construction holds the rebuilt camera to (ViewConstruction::Finish).
inline bool StageSaberAimFrameEye(const SaberCamera& stock,const Camera& centre,const AimFrameEyes& frame,int eye,
    SaberCamera& out,Cover& cover)
{
    Camera posed{};
    Cover candidateCover{};
    SaberPose candidatePose{};
    if (!AimFrameCover(frame,candidateCover)||!BuildAimFrameCamera(centre,frame,eye,{},posed)||
        !BuildSaberPose(posed,{},0,candidatePose))
        return false;
    SaberCamera candidate=stock;
    candidate.pose=candidatePose;
    constexpr float radiansToDegrees=57.29577951308232f;
    candidate.horizontalFovDegrees=2*candidateCover.halfX*radiansToDegrees;
    candidate.verticalFovDegrees=2*candidateCover.halfY*radiansToDegrees;
    candidate.aspectRatio=stock.viewportHeight/stock.viewportWidth;
    out=candidate;
    cover=candidateCover;
    return true;
}
}
