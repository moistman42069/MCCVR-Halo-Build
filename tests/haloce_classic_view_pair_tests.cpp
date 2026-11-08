#include "haloce_classic_view_pair.h"
#include <cstdio>
#include <limits>

using namespace halo_ce;
int main()
{
    int failures=0;
    const auto check=[&](bool ok,const char* message) {
        if (!ok) { std::fprintf(stderr,"CE Classic: %s\n",message); ++failures; }
    };
    const auto near=[](float a,float b) { return std::fabs(a-b)<0.00001f; };
    Window source{};
    source.player=0; source.reserved=0x91;
    source.render.position={12,34,56};
    source.render.forward={1,0,0}; source.render.up={0,0,1};
    source.render.viewport=source.render.window={0,0,1080,1920};
    source.render.verticalFov=1.2f;
    source.render.nearPlane=0.01f; source.render.farPlane=1000;
    source.render.nativeFlags=0x55aa;
    source.render.nativePlane[3]=43;
    source.raster=source.render;
    // The native alternate-camera path may give raster and visibility distinct
    // stock positions. Each receives the same tracked displacement from its
    // own source; these bytes must never be replaced with an assumed alias.
    source.raster.position.x+=0.125f;
    source.raster.nativeFlags=0xa55a;
    Tracking tracking{};
    tracking.generation=4; tracking.spaceEpoch=3; tracking.serial=77;
    tracking.headPosition={0.2f,0.3f,-0.4f};
    tracking.eyes[0].offset={-0.032f,0,0};
    tracking.eyes[1].offset={0.032f,0,0};
    for (auto& eye:tracking.eyes)
    {
        eye.fov[0]=-0.9f; eye.fov[1]=1.0f;
        eye.fov[2]=0.85f; eye.fov[3]=-0.95f;
    }
    Reference reference{}; reference.generation=4; reference.spaceEpoch=3;
    ClassicViewPair pair{};
    check(StageClassicViewPair(source,tracking,reference,2,1.0f/3.048f,true,pair)==
        ClassicPairResult::Ready,"both eyes and both native cameras stage");
    check(near(pair.eyes[0].raster.position.x-pair.eyes[0].render.position.x,0.125f)&&
        near(pair.eyes[1].raster.position.x-pair.eyes[1].render.position.x,0.125f),
        "alternate native camera relation is preserved in each eye");
    check(near(pair.eyes[0].render.position.y-pair.eyes[1].render.position.y,
        0.064f/3.048f),"native binocular separation uses CE unit conversion once");
    for (const auto& eye:pair.eyes)
        check(eye.reserved==0x91&&eye.render.nativeFlags==0x55aa&&
            eye.raster.nativeFlags==0xa55a&&eye.render.nativePlane[3]==43&&
            Same(eye.render.viewport,source.render.viewport)&&
            eye.render.nearPlane==source.render.nearPlane&&
            eye.raster.farPlane==source.raster.farPlane,
            "per-eye staging preserves opaque fields, clip and raster");
    check(ClassicPairCurrent(pair,source,4,3,77,2),"exact native frame is current");
    check(!ClassicPairCurrent(pair,source,4,3,77,3)&&
        !ClassicPairCurrent(pair,source,4,4,77,2)&&
        !ClassicPairCurrent(pair,source,5,3,77,2)&&
        !ClassicPairCurrent(pair,source,4,3,78,2),
        "renderer switch, reference-space reset, title change and serial revoke pair");
    Window changed=source; changed.raster.position.z+=1;
    check(!ClassicPairCurrent(pair,changed,4,3,77,2),"stock native motion revokes pair");
    const auto preserved=pair;
    tracking.eyes[1].orientation.w=std::numeric_limits<float>::quiet_NaN();
    check(StageClassicViewPair(source,tracking,reference,2,0.33f,true,pair)==
        ClassicPairResult::InvalidEye&&std::memcmp(&pair,&preserved,sizeof(pair))==0,
        "bad second eye never publishes a good first eye alone");
    tracking=preserved.tracking;
    changed=source; changed.isUi=1;
    check(StageClassicViewPair(changed,tracking,reference,2,0.33f,true,pair)==
        ClassicPairResult::InvalidWindow,"UI cannot take primary stereo ownership");
    changed=source; changed.player=1;
    check(StageClassicViewPair(changed,tracking,reference,2,0.33f,true,pair)==
        ClassicPairResult::InvalidWindow,"another native player cannot be overwritten");
    check(StageClassicViewPair(source,tracking,reference,0,0.33f,true,pair)==
        ClassicPairResult::InvalidRendererEpoch,"uninitialized graphics mode rejected");
    check(StageClassicViewPair(source,tracking,reference,2,0.33f,false,pair)==
        ClassicPairResult::Ready&&near(pair.eyes[0].render.position.x,source.render.position.x)&&
        near(pair.eyes[0].render.position.z,source.render.position.z),
        "positional toggle leaves binocular eye offsets active");
    {
        // MONO: one camera at the midpoint of the eyes, turned the head's way (no cant), in both slots, under the
        // same cover. The pair's tracking keeps the real eyes, for what is submitted.
        Tracking eyes=preserved.tracking;
        eyes.eyes[0].offset={-0.030f,0.002f,0.001f};
        eyes.eyes[1].offset={0.034f,0.002f,-0.001f};
        eyes.eyes[0].orientation={0,0.0436f,0,0.99905f};
        eyes.eyes[1].orientation={0,-0.0436f,0,0.99905f};
        ClassicViewPair stereo{},mono{};
        check(StageClassicViewPair(source,eyes,reference,2,1.0f,true,stereo)==ClassicPairResult::Ready&&
            !stereo.mono,"canted stereo pair stages");
        eyes.mono=true;
        check(StageClassicViewPair(source,eyes,reference,2,1.0f,true,mono)==ClassicPairResult::Ready&&
            mono.mono,"mono pair stages");
        check(std::memcmp(&mono.eyes[0],&mono.eyes[1],sizeof(Window))==0,"both slots hold the one camera");
        const auto midway=[&](const Camera& a,const Camera& b,const Camera& m) {
            return near(m.position.x,(a.position.x+b.position.x)*0.5f)&&
                near(m.position.y,(a.position.y+b.position.y)*0.5f)&&
                near(m.position.z,(a.position.z+b.position.z)*0.5f);
        };
        check(midway(stereo.eyes[0].render,stereo.eyes[1].render,mono.eyes[0].render)&&
            midway(stereo.eyes[0].raster,stereo.eyes[1].raster,mono.eyes[0].raster),
            "the mono camera sits at the midpoint of the eyes, render and raster alike");
        check(near(mono.eyes[0].render.forward.x,source.render.forward.x)&&
            near(mono.eyes[0].render.forward.y,source.render.forward.y)&&
            near(mono.eyes[0].render.forward.z,source.render.forward.z)&&
            !near(stereo.eyes[0].render.forward.y,stereo.eyes[1].render.forward.y),
            "the mono camera looks the head's way, not along either canted eye");
        check(std::memcmp(&mono.cover,&stereo.cover,sizeof(Cover))==0&&
            near(mono.eyes[0].render.verticalFov,stereo.eyes[0].render.verticalFov),
            "mono keeps the pair's cover, wide enough for either eye");
        check(near(mono.tracking.eyes[0].offset.x,-0.030f)&&near(mono.tracking.eyes[1].offset.x,0.034f),
            "the real eyes stay in the pair's tracking");
        check(ClassicPairCurrent(mono,source,4,3,77,2),"a mono pair is current exactly as a stereo pair is");
    }
    {
        // AIM FRAME ZOOM: the pair becomes the frame's -- one pass unless 3D, along the aim from the centre eye, at the
        // picture's angles -- the raster camera keeping its own offset, and nothing else of the windows changing.
        ClassicViewPair staged{};
        check(StageClassicViewPair(source,tracking,reference,2,1.0f,true,staged)==ClassicPairResult::Ready,
            "a pair stages for the frame");
        const Tracking centreTracking=CentreEyeTracking(staged.tracking);
        Camera centreRender{},centreRaster{};
        check(BuildEye(source.render,centreTracking,reference,0,1.0f,true,staged.cover,centreRender)&&
            BuildEye(source.raster,centreTracking,reference,0,1.0f,true,staged.cover,centreRaster),
            "the centre eye builds for both cameras");
        AimFrameEyes frame{};
        frame.tanX=0.2f*1080.0f/1920.0f; frame.tanY=0.2f; frame.widthDeg=60.0f;
        for (int eye=0;eye<2;++eye)
        {
            frame.position[eye]=centreRender.position+Vec3{0,eye?0.01f:-0.01f,0};
            frame.forward[eye]={0,1,0}; frame.up[eye]={0,0,1};
        }
        ClassicViewPair flat=staged;
        check(StageClassicAimFrame(frame,centreRender,centreRaster,flat)&&flat.mono&&flat.tracking.mono&&
            flat.tracking.aimFrame.active&&near(flat.tracking.aimFrame.tanY,0.2f)&&
            near(flat.tracking.aimFrame.widthDeg,60.0f),"a flat frame is one pass, and says it is the frame");
        check(std::memcmp(&flat.eyes[0],&flat.eyes[1],sizeof(Window))==0&&
            near(flat.eyes[0].render.position.y,frame.position[0].y)&&near(flat.eyes[0].render.forward.y,1.0f)&&
            near(flat.eyes[0].render.verticalFov,2*std::atan(0.2f))&&
            near(flat.cover.halfY,std::atan(0.2f))&&near(flat.cover.halfX,std::atan(frame.tanX)),
            "the one pass looks along the aim at the picture's angles, and the cover is the picture's");
        check(near(flat.eyes[0].raster.position.x-flat.eyes[0].render.position.x,0.125f)&&
            flat.eyes[0].raster.nativeFlags==0xa55a&&flat.eyes[0].reserved==0x91&&
            Same(flat.eyes[0].render.viewport,source.render.viewport),
            "the raster camera keeps its own offset and the windows their opaque fields");
        frame.stereo=true;
        ClassicViewPair stereo=staged;
        check(StageClassicAimFrame(frame,centreRender,centreRaster,stereo)&&!stereo.mono&&!stereo.tracking.mono&&
            near(stereo.eyes[0].render.position.y,frame.position[0].y)&&
            near(stereo.eyes[1].render.position.y,frame.position[1].y),
            "a frame in 3D is a pair, each eye at its own place");
        check(near(stereo.tracking.eyes[0].offset.x,-0.032f),"the real eyes stay in the frame's tracking");
        ClassicViewPair refused=staged;
        frame.forward[1]={0,0,1};
        check(!StageClassicAimFrame(frame,centreRender,centreRaster,refused)&&
            std::memcmp(&refused.eyes[1],&staged.eyes[1],sizeof(Window))==0&&!refused.tracking.aimFrame.active,
            "a camera that is no camera refuses the frame and leaves the pair as it was");
        Tracking turned=tracking;
        turned.headOrientation={0,0.7071068f,0,0.7071068f};
        turned.eyes[0].offset={0,0,0.032f};
        const Vec3 inHead=EyeOffsetInHead(turned,0);
        check(near(inHead.x,-0.032f)&&near(inHead.y,0)&&near(inHead.z,0),
            "an eye's offset is laid in the head's own axes");
    }
    return failures?1:0;
}
