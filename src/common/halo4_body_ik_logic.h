#pragma once
#include "anatomical_palette_logic.h"
#include "halo4_body_finger_bind.generated.h"
#include "controller_finger_pose_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "controller_finger_input.h"
#include "halo4_render_logic.h"

// Exact H4EK storm_masterchief full-body graph, not the separate 80-node
// storm_fp hands model. The input/output transform is one 0x34 record:
// scalar, 3x3 basis, XYZ translation.
inline constexpr uint32_t kHalo4BodyIkNodeCount = 120;
inline constexpr uint32_t kHalo4BodyIkImportChecksum = 0x17010100;
inline constexpr uint32_t kHalo4BodyIkLeftUpperArm = 34;
inline constexpr uint32_t kHalo4BodyIkLeftForearm = 58;
inline constexpr uint32_t kHalo4BodyIkLeftHand = 73;
inline constexpr uint32_t kHalo4BodyIkRightUpperArm = 38;
inline constexpr uint32_t kHalo4BodyIkRightForearm = 50;
inline constexpr uint32_t kHalo4BodyIkRightHand = 68;
inline constexpr uint32_t kHalo4BodyIkLeftThigh = 5;
inline constexpr uint32_t kHalo4BodyIkLeftCalf = 10;
inline constexpr uint32_t kHalo4BodyIkLeftFoot = 18;
inline constexpr uint32_t kHalo4BodyIkRightThigh = 6;
inline constexpr uint32_t kHalo4BodyIkRightCalf = 9;
inline constexpr uint32_t kHalo4BodyIkRightFoot = 22;
inline constexpr uint32_t kHalo4BodyIkHead = 54;

// H4EK storm_masterchief's own hand markers are not storm_fp's wrist axes.
// Transport the shared semantic palm frame from the solved native FP hands.
inline bool Halo4BodyWristsFromFirstPerson(const Halo4FloatingTransform (&fp)[2],
    Halo4FloatingTransform (&world)[2]) noexcept
{
    AnatomicalPalmMarkers firstPerson{};
    if(!Halo4AnatomicalPalmMarkers(firstPerson))return false;
    const float position[2][3]{{.00890922f,-.0127749f,.000334086f},
        {-.00890919f,.0127749f,-.000334197f}};
    const float orientation[2][4]{{.404408f,.357829f,.639067f,.547728f},
        {-.130058f,-.173354f,.711616f,.668309f}};
    Halo4FloatingTransform candidate[2]{};
    for(unsigned side=0;side<2;++side) {
        Halo4FloatingTransform marker{},inverse{},palm{};
        if(!BuildAnatomicalPalmMarker(position[side],orientation[side],marker)||
           !Halo4InvertFloatingTransform(marker,inverse)||
           !Halo4ComposeFloatingTransforms(fp[side],side?firstPerson.right:firstPerson.left,palm)||
           !Halo4ComposeFloatingTransforms(palm,inverse,candidate[side]))return false;
    }
    std::memcpy(world,candidate,sizeof(candidate));return true;
}

inline bool Halo4BodyHeadFromHmd(const Halo4FloatingTransform& hmd,
    Halo4FloatingTransform& head) noexcept
{
    // Own storm_masterchief semantic head marker on node 54. The marker's
    // editor display scale is not a scale change to the avatar skeleton.
    const float p[3]{-1.47467e-6f,.0336001f,-.0322376f};
    const float q[4]{-.492315f,.507569f,.507569f,.492315f};
    Halo4FloatingTransform marker{},inverse{},candidate{};
    if(!BuildAnatomicalPalmMarker(p,q,marker)||
       !Halo4InvertFloatingTransform(marker,inverse)||
       !Halo4ComposeFloatingTransforms(hmd,inverse,candidate))return false;
    head=candidate;return true;
}

struct Halo4BodyIkRequest
{
    bool bodyRootDeltaValid=false;
    Halo4FloatingTransform bodyRootDelta{}; // roomscale/body-follow adjustment
    bool headTargetValid=false;
    Halo4FloatingTransform desiredWorldHead{};
    bool wristValid[2]{}; // anatomical left, right
    Halo4FloatingTransform desiredWorldWrist[2]{};
    float elbowPoleWorld[2][3]{};
    bool footValid[2]{}; // anatomical left, right
    Halo4FloatingTransform desiredWorldFoot[2]{};
    float kneePoleWorld[2][3]{};
    bool fingerPoseAllowed[2]{}; // caller disables while authored grip is needed
    ControllerFingerInput fingerInput[2]{};
};

struct Halo4BodyIkResult
{
    Halo4FloatingTransform transforms[kHalo4BodyIkNodeCount]{};
};

struct Halo4BodyIkPacketMutation
{
    Halo4BodyIkResult body{};
    uint32_t bodyRegionMask=0;
    uint32_t duplicateHandsRegionMask=0;
};

namespace halo4_body_ik_detail
{
inline constexpr int16_t kParent[kHalo4BodyIkNodeCount]={
 -1,0,0,0,3,3,3,6,6,6,5,5,6,4,4,5,5,9,10,13,10,13,9,22,19,19,19,19,19,18,
 26,26,26,24,27,26,26,26,28,24,26,34,36,38,38,34,34,35,38,38,38,33,34,34,
 33,34,38,38,34,58,50,54,58,50,50,58,58,42,50,50,58,58,50,58,47,50,58,58,
 50,50,68,68,68,73,73,68,68,68,68,73,73,68,68,73,73,68,68,73,96,94,84,85,
 93,90,89,80,87,81,105,103,100,101,106,102,104,99,107,98,109,108
};

inline void Mul(const float a[9],const float b[9],float out[9]) noexcept
{
    float r[9]{};
    for(int c=0;c<3;++c) for(int row=0;row<3;++row)
        for(int k=0;k<3;++k) r[c*3+row]+=a[k*3+row]*b[c*3+k];
    std::memcpy(out,r,sizeof(r));
}
inline void RotateVector(const float r[9],const float v[3],float out[3]) noexcept
{
    for(int row=0;row<3;++row)
        out[row]=r[row]*v[0]+r[3+row]*v[1]+r[6+row]*v[2];
}
inline float Dot(const float a[3],const float b[3]) noexcept
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline void Cross(const float a[3],const float b[3],float out[3]) noexcept
{ out[0]=a[1]*b[2]-a[2]*b[1]; out[1]=a[2]*b[0]-a[0]*b[2]; out[2]=a[0]*b[1]-a[1]*b[0]; }
inline bool Normalize(float v[3]) noexcept
{
    const float n2=Dot(v,v);
    if(!std::isfinite(n2)||n2<1.0e-10f)return false;
    const float inv=1.0f/std::sqrt(n2);
    for(int i=0;i<3;++i)v[i]*=inv;
    return true;
}
inline bool Finite3(const float v[3]) noexcept
{ return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]); }
inline float Distance(const float a[3],const float b[3]) noexcept
{
    const float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];
    return std::sqrt(x*x+y*y+z*z);
}
inline bool FromToRotation(const float fromIn[3],const float toIn[3],float out[9]) noexcept
{
    float a[3]{fromIn[0],fromIn[1],fromIn[2]},b[3]{toIn[0],toIn[1],toIn[2]};
    if(!Normalize(a)||!Normalize(b))return false;
    const float d=std::clamp(Dot(a,b),-1.0f,1.0f);
    float axis[3]{}; Cross(a,b,axis);
    float s2=Dot(axis,axis);
    if(s2<1.0e-10f)
    {
        if(d>0.0f){const float id[9]{1,0,0,0,1,0,0,0,1};std::memcpy(out,id,sizeof(id));return true;}
        const float helper[3]{std::fabs(a[0])<.8f?1.f:0.f,std::fabs(a[0])<.8f?0.f:1.f,0.f};
        Cross(a,helper,axis); if(!Normalize(axis))return false;
        out[0]=2*axis[0]*axis[0]-1;out[1]=2*axis[0]*axis[1];out[2]=2*axis[0]*axis[2];
        out[3]=2*axis[1]*axis[0];out[4]=2*axis[1]*axis[1]-1;out[5]=2*axis[1]*axis[2];
        out[6]=2*axis[2]*axis[0];out[7]=2*axis[2]*axis[1];out[8]=2*axis[2]*axis[2]-1;
        return true;
    }
    const float s=std::sqrt(s2),x=axis[0]/s,y=axis[1]/s,z=axis[2]/s;
    const float c=d,t=1-c;
    // Column-major storage, matching Halo4FloatingTransform.
    out[0]=t*x*x+c; out[1]=t*x*y+s*z; out[2]=t*x*z-s*y;
    out[3]=t*x*y-s*z; out[4]=t*y*y+c; out[5]=t*y*z+s*x;
    out[6]=t*x*z+s*y; out[7]=t*y*z-s*x; out[8]=t*z*z+c;
    return true;
}
inline bool IsDescendant(uint32_t node,uint32_t root) noexcept
{
    int p=kParent[node];
    for(uint32_t steps=0;p>=0&&steps<kHalo4BodyIkNodeCount;++steps)
    { if(uint32_t(p)==root)return true; p=kParent[p]; }
    return false;
}
inline bool RotateSubtree(Halo4FloatingTransform (&pose)[kHalo4BodyIkNodeCount],
    uint32_t root,const float delta[9]) noexcept
{
    const float pivot[3]{pose[root].translation[0],pose[root].translation[1],pose[root].translation[2]};
    float basis[9]; Mul(delta,pose[root].rotation,basis); std::memcpy(pose[root].rotation,basis,sizeof(basis));
    for(uint32_t node=0;node<kHalo4BodyIkNodeCount;++node)
    {
        if(!IsDescendant(node,root))continue;
        float relative[3]{pose[node].translation[0]-pivot[0],pose[node].translation[1]-pivot[1],pose[node].translation[2]-pivot[2]},rotated[3];
        RotateVector(delta,relative,rotated);
        for(int i=0;i<3;++i)pose[node].translation[i]=pivot[i]+rotated[i];
        Mul(delta,pose[node].rotation,basis); std::memcpy(pose[node].rotation,basis,sizeof(basis));
    }
    return true;
}
inline bool SolveChain(Halo4FloatingTransform (&pose)[kHalo4BodyIkNodeCount],
    uint32_t upper,uint32_t fore,uint32_t hand,
    const Halo4FloatingTransform& target,const float pole[3],float maxStretch=1.f) noexcept
{
    float shoulder[3],elbowTarget[3],targetPos[3];
    std::memcpy(shoulder,pose[upper].translation,sizeof(shoulder));
    std::memcpy(targetPos,target.translation,sizeof(targetPos));
    if(!Finite3(pole)||!Halo4FloatingTransformValid(target))return false;
    float upperLen=Distance(pose[upper].translation,pose[fore].translation);
    float lowerLen=Distance(pose[fore].translation,pose[hand].translation);
    if(!std::isfinite(upperLen)||!std::isfinite(lowerLen)||upperLen<1e-5f||lowerLen<1e-5f||upperLen>2||lowerLen>2)return false;
    const float span=Distance(shoulder,targetPos),maxReach=upperLen+lowerLen;
    if(!std::isfinite(span)||span<1e-5f||!std::isfinite(maxStretch)||maxStretch<1||
       maxStretch>1.75f||span>maxReach*maxStretch+1e-4f)return false;
    const float stretch=std::max(1.f,span/maxReach);
    upperLen*=stretch;lowerLen*=stretch;
    float axis[3]{targetPos[0]-shoulder[0],targetPos[1]-shoulder[1],targetPos[2]-shoulder[2]};
    if(!Normalize(axis))return false;
    float bend[3]{pole[0],pole[1],pole[2]};
    const float alongPole=Dot(bend,axis);for(int i=0;i<3;++i)bend[i]-=axis[i]*alongPole;
    if(!Normalize(bend)) {
        // A straight native elbow has no usable bend-plane projection. Use
        // this arm's own animated basis, not a fixed world-axis convention.
        // Choosing the strongest perpendicular column also handles a target
        // parallel to any one column and remains covariant with body turns.
        if(upper!=kHalo4BodyIkLeftUpperArm&&upper!=kHalo4BodyIkRightUpperArm)return false;
        float bestSquared=0.f;
        for(int column=0;column<3;++column) {
            float candidate[3];
            const float* basis=pose[upper].rotation+column*3;
            const float along=Dot(basis,axis);
            for(int i=0;i<3;++i)candidate[i]=basis[i]-axis[i]*along;
            const float squared=Dot(candidate,candidate);
            if(std::isfinite(squared)&&squared>bestSquared) {
                bestSquared=squared;std::memcpy(bend,candidate,sizeof(bend));
            }
        }
        if(!Normalize(bend))return false;
    }
    const float reach=std::max(span,std::fabs(upperLen-lowerLen)+1e-5f);
    const float along=(reach*reach+upperLen*upperLen-lowerLen*lowerLen)/(2*reach);
    const float h2=std::max(0.f,upperLen*upperLen-along*along),height=std::sqrt(h2);
    for(int i=0;i<3;++i)elbowTarget[i]=shoulder[i]+axis[i]*along+bend[i]*height;
    float from[3]{pose[fore].translation[0]-shoulder[0],pose[fore].translation[1]-shoulder[1],pose[fore].translation[2]-shoulder[2]};
    float to[3]{elbowTarget[0]-shoulder[0],elbowTarget[1]-shoulder[1],elbowTarget[2]-shoulder[2]},delta[9];
    if(!FromToRotation(from,to,delta)||!RotateSubtree(pose,upper,delta))return false;
    // Distribute tracked reach across both links, preserving complete child
    // subtrees rather than stretching only the wrist/forearm seam.
    if(stretch>1.f) {
        float shift[3];for(int i=0;i<3;++i)shift[i]=elbowTarget[i]-pose[fore].translation[i];
        for(uint32_t node=0;node<kHalo4BodyIkNodeCount;++node)
            if(node==fore||IsDescendant(node,fore))for(int i=0;i<3;++i)pose[node].translation[i]+=shift[i];
    }
    float currentElbow[3]{pose[fore].translation[0],pose[fore].translation[1],pose[fore].translation[2]};
    float currentHand[3]{pose[hand].translation[0],pose[hand].translation[1],pose[hand].translation[2]};
    float handVec[3]{currentHand[0]-currentElbow[0],currentHand[1]-currentElbow[1],currentHand[2]-currentElbow[2]};
    float targetVec[3]{targetPos[0]-currentElbow[0],targetPos[1]-currentElbow[1],targetPos[2]-currentElbow[2]};
    if(std::fabs(Distance(currentElbow,targetPos)-lowerLen)>2e-3f||!FromToRotation(handVec,targetVec,delta)||!RotateSubtree(pose,fore,delta))return false;
    if(stretch>1.f) {
        float shift[3];for(int i=0;i<3;++i)shift[i]=target.translation[i]-pose[hand].translation[i];
        for(uint32_t node=0;node<kHalo4BodyIkNodeCount;++node)
            if(node==hand||IsDescendant(node,hand))for(int i=0;i<3;++i)pose[node].translation[i]+=shift[i];
    }
    // Swing the hand and all authored finger descendants to controller
    // orientation while keeping their attachment point fixed.
    float inverse[9]; for(int c=0;c<3;++c)for(int r=0;r<3;++r)inverse[c*3+r]=pose[hand].rotation[r*3+c];
    float wristDelta[9]; Mul(target.rotation,inverse,wristDelta);
    if(!RotateSubtree(pose,hand,wristDelta))return false;
    const float handScale=target.scale/pose[hand].scale;
    if(!std::isfinite(handScale)||handScale<=0)return false;
    for(uint32_t node=0;node<kHalo4BodyIkNodeCount;++node) {
        if(IsDescendant(node,hand)) {
            for(int i=0;i<3;++i)pose[node].translation[i]=pose[hand].translation[i]+
                (pose[node].translation[i]-pose[hand].translation[i])*handScale;
            pose[node].scale*=handScale;
        }
    }
    pose[hand].scale=target.scale;
    std::memcpy(pose[hand].translation,target.translation,sizeof(target.translation));
    return true;
}
inline bool RotateAroundAxis(const float axisIn[3],float angle,float out[9]) noexcept
{
    float axis[3]{axisIn[0],axisIn[1],axisIn[2]};
    if(!Normalize(axis)||!std::isfinite(angle))return false;
    const float x=axis[0],y=axis[1],z=axis[2],c=std::cos(angle),s=std::sin(angle),t=1-c;
    out[0]=t*x*x+c;out[1]=t*x*y+s*z;out[2]=t*x*z-s*y;
    out[3]=t*x*y-s*z;out[4]=t*y*y+c;out[5]=t*y*z+s*x;
    out[6]=t*x*z+s*y;out[7]=t*y*z-s*x;out[8]=t*z*z+c;
    return true;
}
inline bool CurlFinger(Halo4FloatingTransform (&pose)[kHalo4BodyIkNodeCount],
    uint32_t wrist,const uint32_t* joints,unsigned jointCount,
    float curl,const float axis[3]) noexcept
{
    if(!std::isfinite(curl)||curl<0||curl>1||!Finite3(axis))return false;
    constexpr float jointFlex[4]{0.42f,0.60f,0.74f,0.78f};
    for(unsigned i=0;i<jointCount;++i)
    {
        float rotation[9];
        if(!RotateAroundAxis(axis,curl*jointFlex[i],rotation)||
           !RotateSubtree(pose,joints[i],rotation))return false;
    }
    (void)wrist;
    return true;
}
inline constexpr uint16_t FingerChains[2][5][4]{
    {{84,100,110,0},{94,99,115,0},{89,104,114,0},{93,102,113,0},{90,103,109,118}},
    {{85,101,111,0},{81,107,116,0},{87,106,112,0},{96,98,117,0},{80,105,108,119}}};
inline constexpr uint8_t FingerLengths[5]{3,3,3,3,4};
inline bool DescribeFingers(uint32_t checksum,unsigned count,unsigned side,finger_joint::Inventory& out) noexcept {
    if(checksum!=kHalo4BodyIkImportChecksum||count!=kHalo4BodyIkNodeCount||side>1)return false;
    return finger_joint::Build({GameTitle::Halo4,checksum,uint16_t(count),finger_joint::Palette::WorldBody},
        side,side?kHalo4BodyIkRightHand:kHalo4BodyIkLeftHand,kParent,FingerChains[side],FingerLengths,finger_joint::NamedDigits,out);
}
inline bool PoseControllerFingers(Halo4FloatingTransform (&pose)[kHalo4BodyIkNodeCount],
    unsigned side,const ControllerFingerInput& raw) noexcept
{
    if(!raw.valid)return true; // missing physical controls leave authored pose
    if(!std::isfinite(raw.trigger)||!std::isfinite(raw.grip)||raw.trigger<0||raw.trigger>1||raw.grip<0||raw.grip>1)return false;
    const uint32_t wrist=side==0?kHalo4BodyIkLeftHand:kHalo4BodyIkRightHand;
    // Reset only this free hand from its own authored local bind pose before
    // adding flex. Zero input opens the fingers even if the native animation
    // was gripping a gun, and repeated render passes cannot accumulate curl.
    for(const auto& bind:kHalo4BodyFingerBind)if(IsDescendant(bind.node,wrist)) {
        Halo4FloatingTransform local{},world{};
        if(!BuildAnatomicalPalmMarker(bind.position,bind.quaternion,local)||
           !Halo4ComposeFloatingTransforms(pose[bind.parent],local,world))return false;
        pose[bind.node]=world;
    }
    const float p[2][3]{{.00890922f,-.0127749f,.000334086f},{-.00890919f,.0127749f,-.000334197f}};
    const float q[2][4]{{.404408f,.357829f,.639067f,.547728f},{-.130058f,-.173354f,.711616f,.668309f}};
    Halo4FloatingTransform marker{},palm{};
    if(!BuildAnatomicalPalmMarker(p[side],q[side],marker)||
       !Halo4ComposeFloatingTransforms(pose[wrist],marker,palm))return false;
    const float down[3]{-palm.rotation[6],-palm.rotation[7],-palm.rotation[8]};
    finger_joint::Inventory inventory{};
    if(!DescribeFingers(kHalo4BodyIkImportChecksum,kHalo4BodyIkNodeCount,side,inventory))return false;
    Halo4FloatingTransform candidate[kHalo4BodyIkNodeCount]{};
    if(!controller_finger_pose::Apply(pose,kHalo4BodyIkNodeCount,wrist,kParent,
        FingerChains[side],FingerLengths,down,raw,candidate,static_cast<const Halo4FloatingTransform*>(nullptr),&inventory))return false;
    std::memcpy(pose,candidate,sizeof(candidate));return true;
}
}

// Both requested arms form a single candidate transaction. On any malformed
// transform, unsupported graph identity, or unreachable target, no output is
// published. Callers can keep the native palette and their existing hand
// fallback intact.
inline bool Halo4SolveBodyIkTransforms(uint32_t importChecksum,uint32_t nodeCount,
    const Halo4FloatingTransform (&source)[kHalo4BodyIkNodeCount],
    const Halo4BodyIkRequest& request,Halo4BodyIkResult& output) noexcept
{
    if(importChecksum!=kHalo4BodyIkImportChecksum||nodeCount!=kHalo4BodyIkNodeCount)return false;
    Halo4BodyIkResult candidate{};
    for(uint32_t i=0;i<kHalo4BodyIkNodeCount;++i)
    {
        if(!Halo4FloatingTransformValid(source[i]))return false;
        candidate.transforms[i]=source[i];
    }
    if(request.bodyRootDeltaValid)
    {
        if(!Halo4FloatingTransformValid(request.bodyRootDelta))return false;
        for(auto& node:candidate.transforms)
        {
            Halo4FloatingTransform moved{};
            if(!Halo4ComposeFloatingTransforms(request.bodyRootDelta,node,moved))return false;
            node=moved;
        }
    }
    if(request.headTargetValid)
    {
        if(!Halo4FloatingTransformValid(request.desiredWorldHead))return false;
        const auto& head=candidate.transforms[kHalo4BodyIkHead];
        float translationDelta[3];
        for(int i=0;i<3;++i)translationDelta[i]=request.desiredWorldHead.translation[i]-head.translation[i];
        for(auto& node:candidate.transforms)for(int i=0;i<3;++i)node.translation[i]+=translationDelta[i];
        float inverse[9],rotationDelta[9];
        for(int c=0;c<3;++c)for(int r=0;r<3;++r)inverse[c*3+r]=candidate.transforms[kHalo4BodyIkHead].rotation[r*3+c];
        halo4_body_ik_detail::Mul(request.desiredWorldHead.rotation,inverse,rotationDelta);
        if(!halo4_body_ik_detail::RotateSubtree(candidate.transforms,kHalo4BodyIkHead,rotationDelta))return false;
        std::memcpy(candidate.transforms[kHalo4BodyIkHead].rotation,request.desiredWorldHead.rotation,sizeof(request.desiredWorldHead.rotation));
    }
    for(unsigned side=0;side<2;++side)
    {
        if(request.wristValid[side])
        {
            const auto& target=request.desiredWorldWrist[side];
            if(!halo4_body_ik_detail::SolveChain(candidate.transforms,
                side==0?kHalo4BodyIkLeftUpperArm:kHalo4BodyIkRightUpperArm,
                side==0?kHalo4BodyIkLeftForearm:kHalo4BodyIkRightForearm,
                side==0?kHalo4BodyIkLeftHand:kHalo4BodyIkRightHand,
                target,request.elbowPoleWorld[side],1.75f))return false;
        }
        if(request.footValid[side])
        {
            const auto& target=request.desiredWorldFoot[side];
            if(!halo4_body_ik_detail::SolveChain(candidate.transforms,
                side==0?kHalo4BodyIkLeftThigh:kHalo4BodyIkRightThigh,
                side==0?kHalo4BodyIkLeftCalf:kHalo4BodyIkRightCalf,
                side==0?kHalo4BodyIkLeftFoot:kHalo4BodyIkRightFoot,
                target,request.kneePoleWorld[side]))return false;
        }
        if(request.fingerPoseAllowed[side]&&
           !halo4_body_ik_detail::PoseControllerFingers(candidate.transforms,side,request.fingerInput[side]))return false;
    }
    for(const auto& transform:candidate.transforms)
        if(!Halo4FloatingTransformValid(transform))return false;
    output=candidate;
    return true;
}

// Retain native animated feet while the torso follows the HMD. Beyond leg
// reach, follow only the minimum required distance instead of rejecting the
// entire avatar or stretching its legs. This is visual IK, not ground physics.
inline bool Halo4PrepareBodyFootTargets(
    const Halo4FloatingTransform (&source)[kHalo4BodyIkNodeCount],
    Halo4BodyIkRequest& request) noexcept
{
    if(!request.headTargetValid||request.bodyRootDeltaValid)return false;
    const uint32_t thighs[2]{5,6},calves[2]{10,9},feet[2]{18,22};
    auto candidate=request;
    for(unsigned side=0;side<2;++side) {
        const auto& hip=source[thighs[side]];const auto& knee=source[calves[side]];
        const auto& foot=source[feet[side]];
        float displaced[3],direction[3];
        for(int i=0;i<3;++i) {
            displaced[i]=hip.translation[i]+request.desiredWorldHead.translation[i]-source[54].translation[i];
            direction[i]=foot.translation[i]-displaced[i];
        }
        const float upper=halo4_body_ik_detail::Distance(hip.translation,knee.translation);
        const float lower=halo4_body_ik_detail::Distance(knee.translation,foot.translation);
        const float distance=halo4_body_ik_detail::Distance(displaced,foot.translation);
        if(!std::isfinite(upper)||!std::isfinite(lower)||upper<1e-5f||lower<1e-5f||
           !std::isfinite(distance)||!halo4_body_ik_detail::Normalize(direction))return false;
        const float reach=std::clamp(distance,std::fabs(upper-lower)+1e-4f,upper+lower-1e-4f);
        candidate.desiredWorldFoot[side]=foot;
        for(int i=0;i<3;++i) {
            candidate.desiredWorldFoot[side].translation[i]=displaced[i]+direction[i]*reach;
            candidate.kneePoleWorld[side][i]=knee.translation[i]-hip.translation[i];
        }
        // Straight native knees have no positional bend direction. Pick a
        // stable perpendicular from that knee's own animated basis.
        float cross[3];halo4_body_ik_detail::Cross(candidate.kneePoleWorld[side],direction,cross);
        if(halo4_body_ik_detail::Dot(cross,cross)<1e-8f) {
            std::memcpy(candidate.kneePoleWorld[side],knee.rotation,3*sizeof(float));
            halo4_body_ik_detail::Cross(candidate.kneePoleWorld[side],direction,cross);
            if(halo4_body_ik_detail::Dot(cross,cross)<1e-8f)
                std::memcpy(candidate.kneePoleWorld[side],knee.rotation+3,3*sizeof(float));
        }
        candidate.footValid[side]=true;
    }
    request=candidate;return true;
}

// Production packet transaction: calculate the full avatar pose, admit its
// exact body regions, and hide the separately submitted storm_fp duplicate
// only after all body work validates. A failed request leaves the caller's
// mutation object byte-for-byte untouched.
inline bool Halo4BuildBodyIkPacketMutation(uint32_t importChecksum,
    uint32_t nodeCount,
    const Halo4FloatingTransform (&source)[kHalo4BodyIkNodeCount],
    const Halo4BodyIkRequest& request,uint32_t nativeBodyRegionMask,
    uint32_t nativeHandsRegionMask,bool hideLowerBody,
    Halo4BodyIkPacketMutation& output) noexcept
{
    Halo4BodyIkPacketMutation candidate{};
    if(!Halo4SolveBodyIkTransforms(importChecksum,nodeCount,source,request,
            candidate.body))return false;
    candidate.bodyRegionMask=Halo4BuildBodyIkRegionMask(
        nativeBodyRegionMask,hideLowerBody);
    candidate.duplicateHandsRegionMask=nativeHandsRegionMask|UINT32_MAX;
    output=candidate;
    return true;
}
