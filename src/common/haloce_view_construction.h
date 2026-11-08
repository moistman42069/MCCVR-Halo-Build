#pragma once
#include "haloce_view_pair.h"

namespace halo_ce
{
// E-CE-12: native append derives a separate origin table from the input
// camera. Applying tracking after append leaves that table at the stock pose.
// Own only private input cameras; native append owns every list/resource field.
struct ViewConstruction
{
    Tracking tracking;
    Reference reference;
    float unitsPerMeter{};
    bool positional{};
    uint32_t width{},height{},mask{};
    SaberCamera stock[2];
    StagedViewPair staged;
    // AIM FRAME ZOOM (game.cpp Game_AimFrameForEyes): set before eye 0, every eye is staged as the frame's, from the
    // centre eye `centre` (CentreEye).
    bool aimFrame{};
    AimFrameEyes frame;
    Camera centre;

    // The stock camera `source` at this construction's raster size, as PrepareEye stages it: its native camera and
    // the centre eye's (CentreEyeTracking) in the same space. False: no camera to lay a frame from.
    bool CentreEye(const SaberCamera& source,Camera& stockNative,Camera& centreEye) const
    {
        if (!width||width>16384||!height||height>16384) return false;
        SaberCamera raster=source;
        raster.viewportWidth=static_cast<float>(width);
        raster.viewportHeight=static_cast<float>(height);
        Camera native{},candidate{};
        Cover cover{};
        if (!NativeCameraFromSaber(raster,native)||!BuildCover(tracking,native.viewport,cover)||
            !BuildEye(native,CentreEyeTracking(tracking),reference,0,unitsPerMeter,positional,cover,candidate))
            return false;
        stockNative=native;
        centreEye=candidate;
        return true;
    }

    template<class Rebuild>
    bool PrepareEye(int eye,const SaberCamera& source,Rebuild&& rebuild)
    {
        if (eye<0||eye>1||mask!=(eye?1u:0u)||!width||width>16384||
            !height||height>16384) return false;
        Camera checked{};
        if (!NativeCameraFromSaber(source,checked)) return false;
        if (eye&&std::memcmp(&source.pose,&stock[0].pose,sizeof(SaberPose))) return false;
        SaberCamera raster=source;
        raster.viewportWidth=static_cast<float>(width);
        raster.viewportHeight=static_cast<float>(height);
        SaberCamera camera{}; Cover cover{};
        if (!(aimFrame?StageSaberAimFrameEye(raster,centre,frame,eye,camera,cover):
                StageSaberEye(raster,tracking,reference,eye,unitsPerMeter,positional,camera,cover))) return false;
        const SaberPose requested=camera.pose;
        if (!rebuild(camera)||!NativeCameraFromSaber(camera,checked)||
            !FiniteSaberDerivedCamera(camera)||!SamePose(camera.pose,requested)) return false;
        stock[eye]=source;
        staged.cameras[eye]=camera; staged.covers[eye]=cover;
        staged.serial=tracking.serial; staged.spaceEpoch=tracking.spaceEpoch;
        staged.generation=tracking.generation;
        mask|=1u<<eye;
        return true;
    }

    static bool SamePose(const SaberPose& a,const SaberPose& b)
    {
        for (int i=0;i<16;++i)
            if (!std::isfinite(a.matrix[i])||!std::isfinite(b.matrix[i])||
                std::fabs(a.matrix[i]-b.matrix[i])>0.0001f) return false;
        return true;
    }

    template<class Rebuild>
    bool Finish(const SaberViewPair& native,Rebuild&& rebuild,StagedViewPair& out) const
    {
        if (mask!=3||native.flags!=1||native.count!=2) return false;
        StagedViewPair result=staged;
        for (int eye=0;eye<2;++eye)
        {
            const auto& view=native.views[eye];
            if ((view.flags&~0x1000u)!=(eye?0x20bu:0x10bu)||view.viewIndex!=eye||
                !SamePose(view.camera.pose,staged.cameras[eye].pose)) return false;
            // Retain the native append's opaque fields. The ordinary secondary
            // branch omits the primary clip override; CE's native stereo branch
            // applies that same override to both eyes (454A7B / 454B52).
            auto& camera=result.cameras[eye];
            camera=view.camera;
            camera.nearPlane=native.views[0].camera.nearPlane;
            camera.farPlane=native.views[0].camera.farPlane;
            Camera checked{};
            if (!rebuild(camera)||!NativeCameraFromSaber(camera,checked)||
                !FiniteSaberDerivedCamera(camera)||!SamePose(camera.pose,staged.cameras[eye].pose)||
                camera.viewportWidth!=width||camera.viewportHeight!=height||
                camera.nearPlane!=native.views[0].camera.nearPlane||
                camera.farPlane!=native.views[0].camera.farPlane||
                !std::isfinite(camera.horizontalFovDegrees)||!std::isfinite(camera.verticalFovDegrees)||
                std::fabs(camera.horizontalFovDegrees*0.017453292519943295f-2*staged.covers[eye].halfX)>0.0001f||
                std::fabs(camera.verticalFovDegrees*0.017453292519943295f-2*staged.covers[eye].halfY)>0.0001f) return false;
        }
        out=result;
        return true;
    }
};
}
