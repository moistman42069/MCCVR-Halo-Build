#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "controller_finger_input.h"

// E-CE-1, docs/HALOCE-RENDER-EVIDENCE.md. CE-specific layout verified in
// HCEEK and MCC CE; this is not an alias for another engine's camera.
namespace halo_ce
{
struct Vec3 { float x{}, y{}, z{}; };
struct Quat { float x{}, y{}, z{}, w{1}; };
struct Rectangle { int16_t top{}, left{}, bottom{}, right{}; };
struct Camera
{
    Vec3 position, forward, up;
    uint32_t nativeFlags{};
    float verticalFov{};
    Rectangle viewport, window;
    float nearPlane{}, farPlane{};
    float nativePlane[4]{};
};
struct Window
{
    int16_t player{-1};
    uint8_t isUi{}, reserved{};
    Camera render, raster;
};
static_assert(sizeof(Camera) == 0x54);
static_assert(offsetof(Camera, verticalFov) == 0x28);
static_assert(offsetof(Camera, viewport) == 0x2c);
static_assert(offsetof(Camera, nearPlane) == 0x3c);
static_assert(sizeof(Window) == 0xac);
static_assert(offsetof(Window, render) == 4);
static_assert(offsetof(Window, raster) == 0x58);

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b)
{ return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline bool Finite(Vec3 v)
{ return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline bool Valid(Quat q)
{
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&std::fabs(n-1.0f)<0.02f;
}
inline Quat Conjugate(Quat q) { return {-q.x,-q.y,-q.z,q.w}; }
inline Quat Multiply(Quat a, Quat b)
{
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Vec3 Rotate(Quat q, Vec3 v)
{
    const Vec3 xyz{q.x,q.y,q.z};
    const Vec3 twice=Cross(xyz,v)*2.0f;
    return v+twice*q.w+Cross(xyz,twice);
}
inline bool Valid(Rectangle r)
{
    return r.left>=0&&r.top>=0&&r.right>r.left&&r.bottom>r.top&&
        r.right<=16384&&r.bottom<=16384;
}
inline bool Valid(const Camera& c)
{
    return Finite(c.position)&&Finite(c.forward)&&Finite(c.up)&&
        std::fabs(Dot(c.forward,c.forward)-1)<0.05f&&
        std::fabs(Dot(c.up,c.up)-1)<0.05f&&std::fabs(Dot(c.forward,c.up))<0.05f&&
        std::isfinite(c.verticalFov)&&c.verticalFov>0.01f&&c.verticalFov<3.13f&&
        Valid(c.viewport)&&Valid(c.window)&&
        std::isfinite(c.nearPlane)&&std::isfinite(c.farPlane)&&
        c.nearPlane>0&&c.farPlane>c.nearPlane;
}
inline bool Same(Rectangle a, Rectangle b)
{ return a.top==b.top&&a.left==b.left&&a.bottom==b.bottom&&a.right==b.right; }
inline bool ValidPrimary(const Window& w)
{
    return w.player==0&&!w.isUi&&Valid(w.render)&&Valid(w.raster)&&
        Same(w.render.viewport,w.raster.viewport)&&Same(w.render.window,w.raster.window);
}

// OpenXR values only, no engine bindings. All poses belong to one prepared
// serial. Eye offsets are relative to the head, in tracking-space axes.
struct Eye
{
    Vec3 offset;
    Quat orientation;
    float fov[4]{}; // left/right/up/down, radians
};
struct ControllerPose
{
    bool valid{};
    Vec3 position;
    Quat orientation;
};
struct ControllerRig
{
    // Role-routed just like the other titles: primary is the weapon hand,
    // support is the other hand. Physical indices stay left=0, right=1.
    ControllerPose primaryAim,independentPrimaryAim,support,physical[2];
    bool leftHanded{},handAlignment{},twoHandAimActive{},padValid{};
    // Persistent support grip: visible support presentation follows the valid
    // weapon relationship for this invocation, not aim authority. The OpenXR
    // prepared frame seeds it from `twoHandAimActive` (base behaviour); the CE
    // first-person prepare freezes the per-invocation decision into its copied
    // context. `primaryAimSupportDerived` is the provenance of the frozen
    // `primaryAim`: true only when the solve that produced this exact pose
    // accepted support-derived two-hand geometry. It is never re-sampled from
    // the live relationship and is cleared when `primaryAim` is substituted
    // with the independent one-hand carrier.
    bool supportGripAttached{},primaryAimSupportDerived{};
    bool armIk{true},floatingHands{true},shoulderLevel{true};
    float gunScale{0.96f},supportScale{0.96f};
    float visualPitchDeg{},visualYawDeg{},visualRollDeg{};
    float supportMountPitchDeg{},supportMountYawDeg{},supportMountRollDeg{};
    float gunForwardM{-0.14f},gunRightM{},gunUpM{};
    float supportForwardM{-0.063f};
    float visualLeftHandOffset[3]{},visualRightHandOffset[3]{};
    float shoulderBackM{},primaryShoulderDrop{0.06f};
    float turnX{},moveX{},moveY{};
    bool turnSmooth{true},controlsPresentationBlocked{true};
    bool vehicleSmoothTurn{};
    bool gunBarrelAim{};
    bool roomscaleEnabled{};
    bool physicalRunningEnabled{};
    bool vehicleMotion{true},vehicleViewFollow{};
    float turnSnapDeg{30.0f},turnSmoothDegS{120.0f};
};
struct HudSettings
{
    float size{0.75f},aspect{1.0f},curvature{0.5f},verticalOffset{16.0f};
    bool hidden{};
};
struct Tracking
{
    uint64_t serial{}, spaceEpoch{};
    uint32_t generation{};
    Vec3 headPosition;
    Quat headOrientation;
    Eye eyes[2];
    int64_t predictedDisplayTimeNs{};
    ControllerRig controllers;
    // Same prepared sample, support/primary roles. Native first-person
    // presentation may use this only for a verified free hand.
    ControllerFingerInput controllerFingers[2]{};
    HudSettings hud;
    bool motionBlur{};
    bool disableAnniversaryLensFlares{};
};
struct Reference
{
    Vec3 position;
    Quat orientation;
    uint64_t spaceEpoch{};
    uint32_t generation{};
};
struct Cover { float verticalFov{}, halfX{}, halfY{}; };

// A symmetric native raster covers both asymmetric OpenXR eyes; the existing
// compositor can crop this cover to each physical view. Aspect is CE's actual
// viewport, never another title's raster dimensions.
inline bool BuildCover(const Tracking& tracking, Rectangle viewport, Cover& out)
{
    if (!Valid(viewport)) return false;
    float x=0,y=0;
    for (const Eye& eye:tracking.eyes)
    {
        for (float f:eye.fov)
            if (!std::isfinite(f)||std::fabs(f)>=1.55f) return false;
        if (!(eye.fov[0]<0&&eye.fov[1]>0&&eye.fov[2]>0&&eye.fov[3]<0)) return false;
        x=std::max(x,std::max(-std::tan(eye.fov[0]),std::tan(eye.fov[1])));
        y=std::max(y,std::max(std::tan(eye.fov[2]),-std::tan(eye.fov[3])));
    }
    const float aspect=float(viewport.right-viewport.left)/float(viewport.bottom-viewport.top);
    y=std::max(y,x/aspect);
    Cover candidate{2*std::atan(y),std::atan(y*aspect),std::atan(y)};
    if (!std::isfinite(candidate.verticalFov)||candidate.verticalFov>=3.10f) return false;
    out=candidate;
    return true;
}

inline Vec3 ToNative(const Camera& native,Vec3 local)
{
    return Cross(native.forward,native.up)*local.x+native.up*local.y-native.forward*local.z;
}

// H3 parity: recenter changes heading and position only. Pitch/roll belong to
// the current physical pose, and room up remains CE world +Z. Keeping either
// the captured HMD tilt or the stock camera's look pitch tilts the entire room.
// The reference retains its complete sampled pose for receipt identity; every
// tracked eye/controller consumer derives this same horizontal mapping.
inline bool BuildTrackingFrame(const Camera& native,const Reference& reference,
    Camera& frame,Quat& inverseYaw) noexcept
{
    if (!Valid(native)||!Valid(reference.orientation)) return false;
    Vec3 forward{native.forward.x,native.forward.y,0};
    float length=std::sqrt(Dot(forward,forward));
    if (length<0.0001f)
    {
        // At the native pitch pole its up axis still records the heading.
        const float direction=native.forward.z>=0?-1.0f:1.0f;
        forward={native.up.x*direction,native.up.y*direction,0};
        length=std::sqrt(Dot(forward,forward));
    }
    if (!std::isfinite(length)||length<0.0001f) return false;
    const Vec3 headForward=Rotate(reference.orientation,{0,0,-1});
    float yaw{};
    if (headForward.x*headForward.x+headForward.z*headForward.z>=0.00000001f)
        yaw=std::atan2(-headForward.x,-headForward.z);
    else
    {
        // Exactly vertical HMD forward has no yaw. Its horizontal right axis
        // supplies a finite heading without canceling physical pitch/roll.
        const Vec3 right=Rotate(reference.orientation,{1,0,0});
        yaw=std::atan2(-right.z,right.x);
    }
    if (!std::isfinite(yaw)) return false;
    Camera horizontal=native;
    horizontal.forward=forward*(1.0f/length);horizontal.up={0,0,1};
    if (!Valid(horizontal)) return false;
    frame=horizontal;
    inverseYaw={0,-std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
    return true;
}

// Output is untouched on rejection. Only pose/FOV change; native rectangles,
// flags, near/far and plane retain their exact original bits. The user-facing
// shared world-scale value is supplied by the caller, not an inherited offset.
inline bool BuildEye(const Camera& native,const Tracking& tracking,
    const Reference& reference,int eye,float unitsPerMeter,bool positional,
    const Cover& cover,Camera& out)
{
    if (!Valid(native)||!tracking.serial||!tracking.generation||eye<0||eye>1||
        tracking.generation!=reference.generation||
        tracking.spaceEpoch!=reference.spaceEpoch||!Valid(reference.orientation)||
        !Valid(tracking.headOrientation)||!Valid(tracking.eyes[eye].orientation)||
        !Finite(reference.position)||!Finite(tracking.headPosition)||
        !Finite(tracking.eyes[eye].offset)||
        Dot(tracking.eyes[eye].offset,tracking.eyes[eye].offset)>0.25f||
        !std::isfinite(unitsPerMeter)||unitsPerMeter<=0||unitsPerMeter>10||
        !std::isfinite(cover.verticalFov)||cover.verticalFov<=0||cover.verticalFov>=3.10f)
        return false;
    Camera frame{}; Quat inverse{};
    if (!BuildTrackingFrame(native,reference,frame,inverse)) return false;
    const Quat orientation=Multiply(inverse,tracking.eyes[eye].orientation);
    Vec3 delta=tracking.headPosition-reference.position;
    if (!Finite(delta)) return false;
    delta={std::clamp(delta.x,-4.0f,4.0f),std::clamp(delta.y,-4.0f,4.0f),
           std::clamp(delta.z,-4.0f,4.0f)};
    if (!positional) delta={};
    Camera candidate=native;
    candidate.position=native.position+ToNative(frame,
        Rotate(inverse,delta+tracking.eyes[eye].offset))*unitsPerMeter;
    candidate.forward=ToNative(frame,Rotate(orientation,{0,0,-1}));
    candidate.up=ToNative(frame,Rotate(orientation,{0,1,0}));
    candidate.verticalFov=cover.verticalFov;
    if (!Valid(candidate)) return false;
    out=candidate;
    return true;
}
inline bool ExactPair(uint64_t prepared,uint64_t left,uint64_t right)
{ return prepared!=0&&left==prepared&&right==prepared; }

// E-CE-3: CE's own bridge +0x7B480 maps native (x,y,z) to Saber
// (x,z,-y), with native position multiplied by 3.048. This is only the
// 0x40 pose prefix, not permission to overwrite the derived camera object.
inline constexpr float kSaberUnitsPerNativeUnit=3.048f;
struct SaberPose { float matrix[16]{}; };
static_assert(sizeof(SaberPose)==0x40);
inline Vec3 ToSaber(Vec3 native) { return {native.x,native.z,-native.y}; }
inline bool BuildSaberPose(const Camera& camera,Vec3 worldOffset,
    float saberForwardBias,SaberPose& out)
{
    if (!Valid(camera)||!Finite(worldOffset)||!std::isfinite(saberForwardBias))
        return false;
    const Vec3 forward=ToSaber(camera.forward),up=ToSaber(camera.up);
    const Vec3 right=Cross(forward,up);
    const Vec3 position=ToSaber(camera.position)*kSaberUnitsPerNativeUnit+
        worldOffset+forward*saberForwardBias;
    if (!Finite(position)) return false;
    SaberPose candidate{{right.x,right.y,right.z,0,up.x,up.y,up.z,0,
        forward.x,forward.y,forward.z,0,position.x,position.y,position.z,1}};
    out=candidate;
    return true;
}

// E-CE-3/E-CE-FP-7: NativeCameraFromSaber maps the complete rendered origin
// back into CE axes/units. That origin still includes Saber's world offset
// and forward bias. Gameplay positions must remove both once; per-eye scene
// staging deliberately retains them to stay in the native renderer's space.
inline bool RecoverNativeCameraFromSaberBridge(const Camera& mappedCamera,
    Vec3 saberWorldOffset,float saberForwardBias,Camera& out)
{
    if (!Valid(mappedCamera)||!Finite(saberWorldOffset)||!std::isfinite(saberForwardBias))
        return false;
    const Vec3 nativeOffset{saberWorldOffset.x,-saberWorldOffset.z,saberWorldOffset.y};
    Camera candidate=mappedCamera;
    candidate.position=mappedCamera.position-
        (nativeOffset+mappedCamera.forward*saberForwardBias)*(1.0f/kSaberUnitsPerNativeUnit);
    if (!Valid(candidate)) return false;
    out=candidate;
    return true;
}
}
