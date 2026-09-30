#pragma once
#include "visual_hand_offset.h"

#include "haloce_render_logic.h"
#include "controller_finger_pose_logic.h"
#include "finger_joint_identity.h"
#include "two_hand_ik_logic.h"
#include "../dll/ik.h"
#include <array>
#include <cstring>
#include "haloce_hand_alignment.generated.h"

// CE-specific data, established independently in HCEEK and the pinned MCC
// image. See E-CE-FP-1 in docs/HALOCE-FIRST-PERSON-EVIDENCE.md.
namespace halo_ce
{
inline constexpr size_t kFirstPersonMaxNodes=64;
// Disabled September 15 after the e524d21 headset report: collapsing hidden
// arm vertices at the camera spans triangles between the camera and wrist.
// Retain the old behavior inert; its wrist-local replacement is separate.
inline constexpr bool kCeFloatingArmsAtCameraEnabled=false;
struct AnimationNode
{
    char name[32]{};
    int16_t sibling{-1},child{-1},parent{-1};
    uint8_t remaining[26]{};
};
struct NodeMatrix
{
    float scale{1};
    Vec3 forward{1,0,0},left{0,1,0},up{0,0,1},position{};
};
static_assert(sizeof(AnimationNode)==0x40);
static_assert(offsetof(AnimationNode,parent)==0x24);
static_assert(sizeof(NodeMatrix)==0x34);
static_assert(offsetof(NodeMatrix,position)==0x28);

// E-CE-FP-4: Saber converts the native 52-byte bone matrix to this row-major
// 4x4 representation, but its native converter omits the separate scale.
// Skinning consumes the 3x3 basis, so restore scale there without moving the
// already converted world translation or changing the object's root carrier.
struct SaberBoneMatrix { float value[16]{}; };
// E-CE-FP-8: Classic's model setup substitutes a fixed weapon FOV after the
// eye's world frustum has already been built. The native setter accepts -2 to
// retain that frustum. Its separate near/far depth treatment stays native.
inline bool SelectClassicTrackedProjection(float& verticalFov) noexcept
{
    if (!std::isfinite(verticalFov)||verticalFov<=0||verticalFov>=3.14159265f) return false;
    verticalFov=-2.0f;
    return true;
}
inline bool IsClassicFirstPersonLensCallsite(uintptr_t returnRva) noexcept
{
    // Native opaque/transparent models and the first-person effect/particle
    // paths all select the same fixed lens from their first-person flag.
    return returnRva==0xc1769f||returnRva==0xc159b1||returnRva==0xbf4440||
        returnRva==0xc12354||returnRva==0xc12b52;
}
// E-CE-FP-5: native GLT, ZFILL and SFX material writers select their alternate
// fixed-FOV lens from the same first-person model flag. Tracked geometry uses
// the world lens, retaining native depth-range treatment of both matrices.
inline bool SelectSaberTrackedProjection(uint32_t modelFlags,float (&selector)[4]) noexcept
{
    if (!(modelFlags&0x10000000u)) return false;
    for (float value:selector) if (value!=1.0f) return false;
    for (float& value:selector) value=0;
    return true;
}
// E-CE-FP-9: CB_PASS_PARTICLES has 21 header vectors followed by nine
// 20-vector emitter records. Emitter vector 10.x selects the alternate fixed
// first-person lens in the native shaders. Change only that copied selector;
// authored muzzle transforms, particle motion and the source emitter stay native.
inline constexpr size_t kSaberParticleConstantVectors=201;
inline bool SelectSaberTrackedParticleProjection(float* constants,size_t vectors,
    unsigned& changed) noexcept
{
    changed=0;
    if (!constants||vectors!=kSaberParticleConstantVectors) return false;
    for (size_t emitter=0;emitter<9;++emitter)
    {
        const float value=constants[(21+emitter*20+10)*4];
        if (value!=0&&value!=1) return false;
    }
    for (size_t emitter=0;emitter<9;++emitter)
    {
        float& selector=constants[(21+emitter*20+10)*4];
        if (selector==1) { selector=0;++changed; }
    }
    return true;
}
inline bool ApplySaberFirstPersonScale(float scale,SaberBoneMatrix& matrix) noexcept
{
    if (!std::isfinite(scale)||scale<=0.000001f||scale>=100) return false;
    for (float value:matrix.value) if (!std::isfinite(value)) return false;
    if (std::fabs(matrix.value[3])>0.00001f||std::fabs(matrix.value[7])>0.00001f||
        std::fabs(matrix.value[11])>0.00001f||std::fabs(matrix.value[15]-1)>0.00001f)
        return false;
    SaberBoneMatrix candidate=matrix;
    for (size_t row=0;row<3;++row)
        for (size_t column=0;column<3;++column)
        {
            float& value=candidate.value[row*4+column];
            value*=scale;
            if (!std::isfinite(value)) return false;
        }
    matrix=candidate;
    return true;
}

inline bool Valid(const NodeMatrix& m) noexcept
{
    return std::isfinite(m.scale)&&m.scale>0.000001f&&m.scale<100&&
        Finite(m.position)&&Finite(m.forward)&&Finite(m.left)&&Finite(m.up)&&
        std::fabs(Dot(m.forward,m.forward)-1)<0.05f&&
        std::fabs(Dot(m.left,m.left)-1)<0.05f&&
        std::fabs(Dot(m.up,m.up)-1)<0.05f&&
        std::fabs(Dot(m.forward,m.left))<0.05f&&
        std::fabs(Dot(m.forward,m.up))<0.05f&&
        std::fabs(Dot(m.left,m.up))<0.05f&&
        Dot(Cross(m.forward,m.left),m.up)>0.95f;
}
inline Vec3 TransformDirection(const NodeMatrix& m,Vec3 p) noexcept
{ return m.forward*p.x+m.left*p.y+m.up*p.z; }
inline Vec3 InverseDirection(const NodeMatrix& m,Vec3 p) noexcept
{ return {Dot(m.forward,p),Dot(m.left,p),Dot(m.up,p)}; }

// Preserve the authored pose within a hand: each node receives the exact same
// rigid carrier delta. Reload fingers, the weapon, magazine and muzzle retain
// their relationship. Opposite-hand nodes never inherit this delta.
inline bool MoveNode(const NodeMatrix& sourceCarrier,const NodeMatrix& targetCarrier,
    const NodeMatrix& source,NodeMatrix& out) noexcept
{
    if (!Valid(sourceCarrier)||!Valid(targetCarrier)||!Valid(source)) return false;
    NodeMatrix candidate=source;
    candidate.scale=source.scale*targetCarrier.scale/sourceCarrier.scale;
    candidate.forward=TransformDirection(targetCarrier,InverseDirection(sourceCarrier,source.forward));
    candidate.left=TransformDirection(targetCarrier,InverseDirection(sourceCarrier,source.left));
    candidate.up=TransformDirection(targetCarrier,InverseDirection(sourceCarrier,source.up));
    candidate.position=targetCarrier.position+TransformDirection(targetCarrier,
        InverseDirection(sourceCarrier,source.position-sourceCarrier.position))*
        (targetCarrier.scale/sourceCarrier.scale);
    if (!Valid(candidate)) return false;
    out=candidate;
    return true;
}

// CE's official wrist-local digit landmarks are bilateral about local Y.
// The weapon's model-lateral plane is expressed in its own verified root
// frame: Oddball, Needler and plasma cannon do not have identity root bases.
// Reflect the authored opposite wrist relation on both sides, producing a
// proper rotation and an exact scaled offset without mirroring the weapon.
inline bool BuildMirroredGripTarget(const HandAlignmentPlane& plane,
    const NodeMatrix& sourceGun,const NodeMatrix& sourceWrist,
    const NodeMatrix& renderedGun,NodeMatrix& out) noexcept
{
    if (!Finite(plane.normal)||!std::isfinite(plane.offset)||
        std::fabs(Dot(plane.normal,plane.normal)-1)>0.00001f) return false;
    NodeMatrix local{};
    if (!MoveNode(sourceGun,NodeMatrix{},sourceWrist,local)) return false;
    const auto reflect=[&](Vec3 value) { return value-plane.normal*(2*Dot(plane.normal,value)); };
    local.forward=reflect(local.forward);
    local.left=reflect(local.left)*-1; // Opposite anatomical hand's local-Y reflection.
    local.up=reflect(local.up);
    local.position=local.position-plane.normal*(2*(Dot(plane.normal,local.position)+plane.offset));
    return MoveNode(NodeMatrix{},renderedGun,local,out);
}

struct FirstPersonBinding
{
    uint32_t graph{0xffffffffu};
    uint32_t generation{};
    uint16_t count{};
    int16_t rightWrist{-1},leftWrist{-1},gun{-1};
    int16_t shoulder[2]{-1,-1},elbow[2]{-1,-1}; // physical left/right
    uint64_t rightMask{},leftMask{},gunMask{};
    uint64_t armMask[2]{};
    uint64_t nodeIdentity{}; // Complete ordered names/parents, never a title-wide bone guess.
    int16_t fingerNodes[2][5][3]{}; // anatomical left/right, index/middle/pinky/ring/thumb
    uint64_t fingerMasks[2][5]{};
    finger_joint::Inventory fingerInventory[2]{};
    bool fingerPoseSupported{};
};

inline bool ApplyVisibleFirstPersonHandOffsets(const FirstPersonBinding& binding,float units,
    const ControllerRig& rig,NodeMatrix* palette) noexcept
{
    const auto* lo=rig.visualLeftHandOffset;const auto* ro=rig.visualRightHandOffset;
    const visual_hand::Offset left{lo[0],lo[1],lo[2]},right{ro[0],ro[1],ro[2]};
    if(!visual_hand::Active(left)&&!visual_hand::Active(right))return true;
    if(!palette||!binding.count||binding.count>64||binding.leftWrist<0||binding.rightWrist<0||
        binding.leftWrist>=binding.count||binding.rightWrist>=binding.count)return false;
    const auto basis=[](const NodeMatrix& n,float b[9]){
        b[0]=n.forward.x;b[1]=n.forward.y;b[2]=n.forward.z;b[3]=n.left.x;b[4]=n.left.y;b[5]=n.left.z;
        b[6]=n.up.x;b[7]=n.up.y;b[8]=n.up.z;};
    float lb[9]{},rb[9]{},ld[3]{},rd[3]{};basis(palette[binding.leftWrist],lb);basis(palette[binding.rightWrist],rb);
    uint64_t lm=binding.leftMask,rm=binding.rightMask;
    if(rig.floatingHands){lm|=binding.armMask[0];rm|=binding.armMask[1];}
    // The CE binding independently identifies every held-gun descendant.
    lm&=~binding.gunMask;rm&=~binding.gunMask;
    if(!visual_hand::Deltas(lb,rb,units,left,right,rig.leftHanded,rig.handAlignment,ld,rd))return false;
    return visual_hand::Apply(binding.count,lm,rm,ld,rd,[&](size_t node){return &palette[node].position.x;},
        [&](size_t node,const float* v){palette[node].position={v[0],v[1],v[2]};});
}
inline const HandAlignmentPlane* FindHandAlignmentPlane(const FirstPersonBinding& binding) noexcept
{
    if (!binding.nodeIdentity) return nullptr;
    for (const auto& plane:kCeHandAlignmentPlanes)
        if (plane.graphIdentity==binding.nodeIdentity&&plane.graphCount==binding.count&&
            plane.gunNode==binding.gun) return &plane;
    return nullptr;
}
inline uint64_t FirstPersonNodeIdentity(const AnimationNode* nodes,size_t count) noexcept
{
    if (!nodes||!count||count>kFirstPersonMaxNodes) return 0;
    uint64_t hash=14695981039346656037ull;
    const auto append=[&](uint8_t value) { hash=(hash^value)*1099511628211ull; };
    append(uint8_t(count));
    for (size_t i=0;i<count;++i)
    {
        const auto* end=static_cast<const char*>(std::memchr(nodes[i].name,0,32));
        if (!end) return 0;
        for (const char* c=nodes[i].name;c<=end;++c) append(uint8_t(*c));
        const uint16_t parent=uint16_t(nodes[i].parent);
        append(uint8_t(parent));append(uint8_t(parent>>8));
    }
    return hash?hash:1;
}
inline bool NodeName(const char (&name)[32],const char* expected) noexcept
{
    const size_t length=std::strlen(expected);
    if (length>=sizeof(name)) return false;
    return std::memcmp(name,expected,length)==0&&name[length]==0;
}
inline bool IsWrist(const char (&name)[32],bool left) noexcept
{
    // These exact spellings occur in the official HCEEK first-person graphs.
    return NodeName(name,left?"frame l wriste":"frame r wriste")||
        NodeName(name,left?"frame l wrist":"frame r wrist");
}
inline bool DescendsFrom(const AnimationNode* nodes,size_t count,size_t node,
    size_t ancestor) noexcept
{
    for (size_t depth=0;depth<count;++depth)
    {
        if (node==ancestor) return true;
        if (!node) return false;
        const int parent=nodes[node].parent;
        if (parent<0||size_t(parent)>=count||size_t(parent)==node) return false;
        node=size_t(parent);
    }
    return false;
}
inline bool BuildFirstPersonBinding(uint32_t graph,uint32_t generation,
    const AnimationNode* nodes,size_t count,FirstPersonBinding& out) noexcept
{
    if (!nodes||graph==0xffffffffu||!generation||!count||count>kFirstPersonMaxNodes)
        return false;
    FirstPersonBinding candidate{};
    candidate.graph=graph;candidate.generation=generation;candidate.count=uint16_t(count);
    for (size_t i=0;i<count;++i)
    {
        if (!std::memchr(nodes[i].name,0,sizeof(nodes[i].name))) return false;
        if (!i)
        {
            if (nodes[i].parent!=-1&&nodes[i].parent!=0) return false;
        }
        else if (!DescendsFrom(nodes,count,i,0)) return false;
        for (int16_t index:{nodes[i].child,nodes[i].sibling})
            if (index!=-1&&(index<0||size_t(index)>=count||size_t(index)==i)) return false;
        if (IsWrist(nodes[i].name,false))
        { if (candidate.rightWrist!=-1) return false;candidate.rightWrist=int16_t(i); }
        if (IsWrist(nodes[i].name,true))
        { if (candidate.leftWrist!=-1) return false;candidate.leftWrist=int16_t(i); }
        for (size_t side=0;side<2;++side)
        {
            if (NodeName(nodes[i].name,side?"frame r upperarm":"frame l upperarm"))
            { if (candidate.shoulder[side]!=-1) return false;candidate.shoulder[side]=int16_t(i); }
            if (NodeName(nodes[i].name,side?"frame r forearm":"frame l forearm"))
            { if (candidate.elbow[side]!=-1) return false;candidate.elbow[side]=int16_t(i); }
        }
    }
    // The official CE graphs use four weapon-root spellings. Only the direct
    // wrist child is a root: the rocket launcher also has a "frame body"
    // deeper inside its weapon tree, which must never become a second root.
    for (size_t i=0;i<count;++i)
        if (nodes[i].parent==candidate.rightWrist&&
            (NodeName(nodes[i].name,"frame gun")||NodeName(nodes[i].name,"frame body")||
             NodeName(nodes[i].name,"frame pole")||NodeName(nodes[i].name,"frame skull")))
        { if (candidate.gun!=-1) return false;candidate.gun=int16_t(i); }
    if (candidate.rightWrist<0||candidate.leftWrist<0||candidate.gun<0||
        !DescendsFrom(nodes,count,size_t(candidate.gun),size_t(candidate.rightWrist)))
        return false;
    for (size_t i=0;i<count;++i)
    {
        const uint64_t bit=uint64_t{1}<<i;
        if (DescendsFrom(nodes,count,i,size_t(candidate.rightWrist))) candidate.rightMask|=bit;
        if (DescendsFrom(nodes,count,i,size_t(candidate.leftWrist))) candidate.leftMask|=bit;
        if (DescendsFrom(nodes,count,i,size_t(candidate.gun))) candidate.gunMask|=bit;
        for (size_t side=0;side<2;++side)
            if (candidate.shoulder[side]>=0&&
                DescendsFrom(nodes,count,i,size_t(candidate.shoulder[side]))&&
                !DescendsFrom(nodes,count,i,size_t(side?candidate.rightWrist:candidate.leftWrist)))
                candidate.armMask[side]|=bit;
    }
    if ((candidate.rightMask&candidate.leftMask)||!candidate.gunMask) return false;
    if ((candidate.armMask[0]&candidate.armMask[1])||
        ((candidate.armMask[0]|candidate.armMask[1])&
         (candidate.rightMask|candidate.leftMask|candidate.gunMask|1))) return false;
    for (size_t side=0;side<2;++side)
    {
        const int wrist=side?candidate.rightWrist:candidate.leftWrist;
        if (candidate.shoulder[side]>=0&&candidate.elbow[side]>=0&&
            (nodes[wrist].parent!=candidate.elbow[side]||
             nodes[candidate.elbow[side]].parent!=candidate.shoulder[side])) return false;
    }
    candidate.nodeIdentity=FirstPersonNodeIdentity(nodes,count);
    out=candidate;
    return true;
}

inline bool BuildFingerBindings(const AnimationNode* nodes,size_t count,
    FirstPersonBinding& binding) noexcept
{
    // The verified fp_body graph contributes these exact 37 nodes. Weapon
    // graphs may append their own nodes; accept those only when they do not
    // extend any finger chain (the exact-subtree check below enforces that).
    if(!nodes||count<37||count>kFirstPersonMaxNodes||binding.count!=count||
        binding.leftWrist<0||binding.rightWrist<0)
        return false;
    constexpr const char* roots[2][5]{
        {"frame l index low","frame l middlelow","frame l pinky low","frame l ring low","frame l thumb low"},
        {"frame r index low","frame r middle low","frame r pinky low","frame r ring low","frame r thumb low"}};
    constexpr const char* mids[2][5]{
        {"frame l index mid","frame l middle mid","frame l pinky mid","frame l ring mid","frame l thumb mid"},
        {"frame r index mid","frame r middle mid","frame r pinky mid","frame r ring mid","frame r thumb mid"}};
    constexpr const char* tips[2][5]{
        {"frame l index tip","frame l middle tip","frame l pinky tip","frame l ring tip","frame l thumb tip"},
        {"frame r index tip","frame r middle tip","frame r pinky tip","frame r ring tip","frame r thumb tip"}};
    FirstPersonBinding candidate=binding;
    std::memset(candidate.fingerNodes,0,sizeof(candidate.fingerNodes));
    std::memset(candidate.fingerMasks,0,sizeof(candidate.fingerMasks));
    for(unsigned side=0;side<2;++side)
        for(unsigned finger=0;finger<5;++finger)
        {
            int found[3]{-1,-1,-1};
            for(size_t node=0;node<count;++node)
            {
                if(NodeName(nodes[node].name,roots[side][finger]))found[0]=int(node);
                if(NodeName(nodes[node].name,mids[side][finger]))found[1]=int(node);
                if(NodeName(nodes[node].name,tips[side][finger]))found[2]=int(node);
            }
            const int wrist=side?binding.rightWrist:binding.leftWrist;
            if(found[0]<0||found[1]<0||found[2]<0||
                nodes[found[0]].parent!=wrist||nodes[found[1]].parent!=found[0]||
                nodes[found[2]].parent!=found[1])return false;
            for(unsigned joint=0;joint<3;++joint)
                candidate.fingerNodes[side][finger][joint]=int16_t(found[joint]);
            for(size_t node=0;node<count;++node)
                if(DescendsFrom(nodes,count,node,size_t(found[0])))
                    candidate.fingerMasks[side][finger]|=uint64_t{1}<<node;
            const uint64_t expected=(uint64_t{1}<<found[0])|
                (uint64_t{1}<<found[1])|(uint64_t{1}<<found[2]);
            if(candidate.fingerMasks[side][finger]!=expected)return false;
        }
    constexpr unsigned sourceFingerForSlot[5]{4,0,1,3,2}; // thumb,index,middle,ring,pinky
    constexpr finger_joint::Digit digits[5]{finger_joint::Digit::Thumb,
        finger_joint::Digit::Index,finger_joint::Digit::Middle,
        finger_joint::Digit::Ring,finger_joint::Digit::Pinky};
    constexpr uint8_t lengths[5]{3,3,3,3,3};
    int16_t parents[kFirstPersonMaxNodes]{};
    for(size_t node=0;node<count;++node)parents[node]=nodes[node].parent;
    for(unsigned side=0;side<2;++side)
    {
        uint16_t chains[5][4]{};
        for(unsigned slot=0;slot<5;++slot)
            for(unsigned joint=0;joint<3;++joint)
                chains[slot][joint]=static_cast<uint16_t>(
                    candidate.fingerNodes[side][sourceFingerForSlot[slot]][joint]);
        const int wrist=side?binding.rightWrist:binding.leftWrist;
        const finger_joint::RigKey key{GameTitle::HaloCE,binding.nodeIdentity,
            static_cast<uint16_t>(count),finger_joint::Palette::FirstPerson};
        if(!finger_joint::Build(key,side,static_cast<unsigned>(wrist),parents,
            chains,lengths,digits,candidate.fingerInventory[side]))return false;
    }
    candidate.fingerPoseSupported=true;
    binding=candidate;
    return true;
}

inline bool DescribeFirstPersonFingerJoints(const FirstPersonBinding& binding,
    unsigned side,finger_joint::Inventory& out) noexcept
{
    out={};
    if(side>1||!binding.fingerPoseSupported)return false;
    const auto& candidate=binding.fingerInventory[side];
    if(candidate.rig.title!=GameTitle::HaloCE||
       candidate.rig.checksum!=binding.nodeIdentity||
       candidate.rig.nodeCount!=binding.count||
       candidate.rig.palette!=finger_joint::Palette::FirstPerson||!candidate.count)return false;
    out=candidate;return true;
}

inline Vec3 RotateVector(Vec3 value,Vec3 axis,float angle) noexcept
{
    const float c=std::cos(angle),s=std::sin(angle);
    return value*c+Cross(axis,value)*s+axis*(Dot(axis,value)*(1-c));
}
inline bool CurlFingerSubtree(const FirstPersonBinding& binding,
    std::array<NodeMatrix,kFirstPersonMaxNodes>& palette,int joint,Vec3 axis,
    float angle) noexcept
{
    if(joint<0||joint>=binding.count||!Finite(axis)||
        std::fabs(Dot(axis,axis)-1)>0.02f||!std::isfinite(angle))return false;
    const NodeMatrix pivot=palette[joint];
    NodeMatrix target=pivot;
    target.forward=RotateVector(pivot.forward,axis,angle);
    target.left=RotateVector(pivot.left,axis,angle);
    target.up=RotateVector(pivot.up,axis,angle);
    if(!Valid(target))return false;
    NodeMatrix staged[kFirstPersonMaxNodes]{};
    std::memcpy(staged,palette.data(),binding.count*sizeof(NodeMatrix));
    const uint64_t mask=([&]{for(unsigned side=0;side<2;++side)
        for(unsigned finger=0;finger<5;++finger)
            for(unsigned segment=0;segment<3;++segment)
                if(binding.fingerNodes[side][finger][segment]==joint)
                    return binding.fingerMasks[side][finger];
        return uint64_t{};})();
    if(!mask)return false;
    for(size_t node=0;node<binding.count;++node)
        if(mask&(uint64_t{1}<<node))
        {
            NodeMatrix moved{};
            if(!MoveNode(pivot,target,palette[node],moved))return false;
            staged[node]=moved;
        }
    std::memcpy(palette.data(),staged,binding.count*sizeof(NodeMatrix));
    return true;
}

inline bool ApplyFreeHandFingerPose(const FirstPersonBinding& binding,
    const Tracking& tracking,std::array<NodeMatrix,kFirstPersonMaxNodes>& palette) noexcept
{
    const auto& rig=tracking.controllers;
    if(!binding.fingerPoseSupported||rig.supportGripAttached)return true;
    const unsigned side=rig.leftHanded&&rig.handAlignment?1u:0u;
    const ControllerFingerInput& input=tracking.controllerFingers[0]; // semantic support hand
    if(!input.valid)return true;
    if(!std::isfinite(input.trigger)||!std::isfinite(input.grip)||
        input.trigger<0||input.trigger>1||input.grip<0||input.grip>1)return false;
    const int wrist=side?binding.rightWrist:binding.leftWrist;
    int16_t parents[kFirstPersonMaxNodes]{};
    for(auto& parent:parents)parent=-1;
    uint16_t chains[5][4]{};uint8_t lengths[5]{3,3,3,3,3};
    for(unsigned slot=0;slot<5;++slot)
        for(unsigned joint=0;joint<3;++joint)
        {
            const auto* record=binding.fingerInventory[side].FindNative(slot,joint);
            if(!record)return false;
            chains[slot][joint]=record->paletteIndex;
            parents[record->paletteIndex]=static_cast<int16_t>(record->parentIndex);
        }
    struct PoseTransform{float scale{1},rotation[9]{},translation[3]{};};
    PoseTransform source[kFirstPersonMaxNodes]{},posed[kFirstPersonMaxNodes]{};
    for(unsigned node=0;node<binding.count;++node)
    {
        const auto& m=palette[node];auto& t=source[node];t.scale=m.scale;
        const float rotation[]{m.forward.x,m.forward.y,m.forward.z,m.left.x,m.left.y,m.left.z,
            m.up.x,m.up.y,m.up.z};
        std::memcpy(t.rotation,rotation,sizeof(rotation));
        t.translation[0]=m.position.x;t.translation[1]=m.position.y;t.translation[2]=m.position.z;
    }
    const float down[]{palette[wrist].left.x,palette[wrist].left.y,palette[wrist].left.z};
    if(!controller_finger_pose::Apply(source,binding.count,static_cast<size_t>(wrist),
        parents,chains,lengths,down,input,posed,static_cast<const PoseTransform*>(nullptr),&binding.fingerInventory[side]))return false;
    auto candidate=palette;
    for(unsigned node=0;node<binding.count;++node)
    {
        auto& m=candidate[node];const auto& t=posed[node];m.scale=t.scale;
        m.forward={t.rotation[0],t.rotation[1],t.rotation[2]};
        m.left={t.rotation[3],t.rotation[4],t.rotation[5]};
        m.up={t.rotation[6],t.rotation[7],t.rotation[8]};
        m.position={t.translation[0],t.translation[1],t.translation[2]};
    }
    palette=candidate;
    return true;
}

inline bool CollapseFirstPersonArmsAtWrists(const FirstPersonBinding& binding,
    std::array<NodeMatrix,kFirstPersonMaxNodes>& palette) noexcept
{
    if (!binding.count||binding.count>kFirstPersonMaxNodes||
        binding.leftWrist<0||binding.rightWrist<0||
        binding.leftWrist>=binding.count||binding.rightWrist>=binding.count||
        (binding.armMask[0]&binding.armMask[1])||
        ((binding.armMask[0]|binding.armMask[1])&
         (binding.leftMask|binding.rightMask|binding.gunMask|1))) return false;
    const uint64_t inRange=binding.count==64?~uint64_t{}:(uint64_t{1}<<binding.count)-1;
    if ((binding.armMask[0]|binding.armMask[1])&~inRange) return false;
    NodeMatrix collapsed[2]={palette[binding.leftWrist],palette[binding.rightWrist]};
    for (auto& matrix:collapsed)
    {
        if (!Valid(matrix)) return false;
        matrix.scale=0.00001f;
    }
    // Official HCEEK cyborg FP vertices blend each forearm with its own wrist.
    // Shrinking the forearm at the camera leaves an arm-to-camera triangle fan.
    // Collapse both named arm chains at their matching final wrist instead.
    // Wrist/finger/weapon matrices and the native Saber object root stay exact.
    for (size_t i=1;i<binding.count;++i)
        for (size_t side=0;side<2;++side)
            if (binding.armMask[side]&(uint64_t{1}<<i)) palette[i]=collapsed[side];
    return true;
}

// The adapter supplies a single proven camera/reference/controller receipt.
// Missing or stale controller data fails this feature without affecting eyes.
inline bool BuildControllerMatrix(const Camera& native,const Tracking& tracking,
    const Reference& reference,const ControllerPose& controller,float unitsPerMeter,
    bool positional,NodeMatrix& out) noexcept
{
    if (!Valid(native)||!tracking.serial||!tracking.generation||!controller.valid||
        tracking.generation!=reference.generation||tracking.spaceEpoch!=reference.spaceEpoch||
        !Valid(reference.orientation)||!Valid(controller.orientation)||
        !Finite(reference.position)||!Finite(controller.position)||!Finite(tracking.headPosition)||
        !std::isfinite(unitsPerMeter)||unitsPerMeter<=0||unitsPerMeter>10) return false;
    Vec3 delta=controller.position-reference.position;
    if (!positional) delta=controller.position-tracking.headPosition;
    if (!Finite(delta)||Dot(delta,delta)>64) return false;
    Camera frame{};Quat inverse{};
    if (!BuildTrackingFrame(native,reference,frame,inverse)) return false;
    const Quat orientation=Multiply(inverse,controller.orientation);
    NodeMatrix candidate{};
    candidate.position=native.position+ToNative(frame,Rotate(inverse,delta))*unitsPerMeter;
    candidate.forward=ToNative(frame,Rotate(orientation,{0,0,-1}));
    candidate.up=ToNative(frame,Rotate(orientation,{0,1,0}));
    candidate.left=Cross(candidate.up,candidate.forward);
    if (!Valid(candidate)) return false;
    out=candidate;
    return true;
}

inline bool BuildGripCarriers(const FirstPersonBinding& binding,const NodeMatrix* source,
    size_t count,const NodeMatrix& primaryAim,const NodeMatrix& supportAim,
    NodeMatrix& primary,NodeMatrix& support) noexcept
{
    if (!source||binding.count!=count||binding.gun<0||binding.rightWrist<0||binding.leftWrist<0||
        size_t(binding.gun)>=count||size_t(binding.rightWrist)>=count||size_t(binding.leftWrist)>=count||
        !Valid(primaryAim)||!Valid(supportAim)) return false;
    NodeMatrix primaryCandidate{},supportCandidate{};
    const NodeMatrix& gun=source[binding.gun];
    if (!MoveNode(gun,primaryAim,source[binding.rightWrist],primaryCandidate)||
        !MoveNode(gun,supportAim,source[binding.leftWrist],supportCandidate)) return false;
    // Preserve each wrist's authored rotation relative to the gun, while the
    // physical controller supplies the grip's world position. The gun now
    // follows mount-calibrated aim without assuming wrist axes equal gun axes.
    primaryCandidate.position=primaryAim.position;
    supportCandidate.position=supportAim.position;
    primary=primaryCandidate;support=supportCandidate;
    return true;
}

inline bool ApplyVisualMount(const ControllerRig& rig,float unitsPerMeter,
    bool primary,NodeMatrix& matrix) noexcept
{
    const float scale=primary?rig.gunScale:rig.supportScale;
    const float forward=primary?rig.gunForwardM:rig.supportForwardM;
    const float pitchDeg=primary?rig.visualPitchDeg:rig.supportMountPitchDeg;
    const float yawDeg=primary?rig.visualYawDeg:rig.supportMountYawDeg;
    const float rollDeg=primary?rig.visualRollDeg:rig.supportMountRollDeg;
    if (!Valid(matrix)||!std::isfinite(scale)||!std::isfinite(forward)||
        !std::isfinite(unitsPerMeter)||unitsPerMeter<=0||
        !std::isfinite(pitchDeg)||!std::isfinite(yawDeg)||
        !std::isfinite(rollDeg)||!std::isfinite(rig.gunRightM)||
        !std::isfinite(rig.gunUpM)) return false;
    NodeMatrix candidate=matrix;
    {
        constexpr float radians=0.01745329252f;
        const float pitch=pitchDeg*radians,yaw=-yawDeg*radians,
            roll=-rollDeg*radians;
        const float cp=std::cos(pitch),sp=std::sin(pitch),cy=std::cos(yaw),sy=std::sin(yaw),
            cr=std::cos(roll),sr=std::sin(roll);
        const Vec3 f{cp*cy,cp*sy,sp},u{-sp*cy*cr+sy*sr,-sp*sy*cr-cy*sr,cp*cr};
        candidate.forward=TransformDirection(matrix,f);
        candidate.up=TransformDirection(matrix,u);
        candidate.left=Cross(candidate.up,candidate.forward);
    }
    candidate.scale=std::clamp(scale,0.3f,3.0f);
    candidate.position=candidate.position+candidate.forward*
        (std::clamp(forward,primary?-0.3f:-0.15f,primary?0.5f:0.3f)*unitsPerMeter);
    if (primary)
        candidate.position=candidate.position-candidate.left*(std::clamp(rig.gunRightM,-0.3f,0.3f)*unitsPerMeter)+
            candidate.up*(std::clamp(rig.gunUpM,-0.3f,0.3f)*unitsPerMeter);
    if (!Valid(candidate)) return false;
    matrix=candidate;return true;
}

inline bool RotateBoneToward(NodeMatrix source,Vec3 sourceDirection,
    Vec3 targetDirection,Vec3 position,NodeMatrix& out) noexcept
{
    const float fromLength=std::sqrt(Dot(sourceDirection,sourceDirection));
    const float toLength=std::sqrt(Dot(targetDirection,targetDirection));
    if (!Valid(source)||!Finite(position)||!std::isfinite(fromLength)||
        !std::isfinite(toLength)||fromLength<0.00001f||toLength<0.00001f) return false;
    const Vec3 from=sourceDirection*(1/fromLength),to=targetDirection*(1/toLength);
    const float cosine=std::clamp(Dot(from,to),-1.0f,1.0f);
    Quat swing{};
    if (cosine<-0.9999f)
    {
        Vec3 axis=Cross(from,source.up);
        float length=std::sqrt(Dot(axis,axis));
        if (length<0.001f) { axis=Cross(from,source.left);length=std::sqrt(Dot(axis,axis)); }
        if (length<0.001f) return false;
        axis=axis*(1/length);swing={axis.x,axis.y,axis.z,0};
    }
    else
    {
        const Vec3 axis=Cross(from,to);
        const float inverse=1/std::sqrt(2*(1+cosine));
        swing={axis.x*inverse,axis.y*inverse,axis.z*inverse,(1+cosine)*inverse};
    }
    source.forward=Rotate(swing,source.forward);source.left=Rotate(swing,source.left);
    source.up=Rotate(swing,source.up);source.position=position;
    if (!Valid(source)) return false;
    out=source;return true;
}

inline bool SolveFirstPersonArm(const FirstPersonBinding& binding,size_t side,
    const NodeMatrix* source,const Camera& camera,const ControllerRig& rig,
    const NodeMatrix& wrist,std::array<NodeMatrix,kFirstPersonMaxNodes>& palette) noexcept
{
    const int shoulder=binding.shoulder[side],elbow=binding.elbow[side],
        wristIndex=side?binding.rightWrist:binding.leftWrist;
    if (shoulder<0||elbow<0||shoulder>=binding.count||elbow>=binding.count||
        wristIndex<0||wristIndex>=binding.count) return false;
    NodeMatrix center{},torso{};
    center.forward=camera.forward;center.up=camera.up;center.left=Cross(camera.up,camera.forward);
    center.position=camera.position;torso=center;
    if (rig.shoulderLevel)
    {
        // CE's world-up is independently established by the unit ray helper's
        // upright perpendicular construction (E-CE-FP-2), not another title.
        const Vec3 heading{camera.forward.x,camera.forward.y,0};
        const float length=std::sqrt(Dot(heading,heading));
        if (length>0.001f)
        { torso.forward=heading*(1/length);torso.up={0,0,1};torso.left=Cross(torso.up,torso.forward); }
    }
    NodeMatrix upper{},lower{},oldWrist{};
    if (!MoveNode(center,torso,source[shoulder],upper)||
        !MoveNode(center,torso,source[elbow],lower)||
        !MoveNode(center,torso,source[wristIndex],oldWrist)||
        !std::isfinite(rig.shoulderBackM)||!std::isfinite(rig.primaryShoulderDrop)) return false;
    // The shared shoulder_back_m key is historically in native world units,
    // as is right_shoulder_drop; only controller mount standoffs use metres.
    const Vec3 rootOffset=torso.forward*(-std::clamp(rig.shoulderBackM,-1.0f,1.0f))+
        torso.up*((side==((rig.leftHanded&&rig.handAlignment)?0u:1u))?
            -std::clamp(rig.primaryShoulderDrop,0.0f,1.0f):0.0f);
    upper.position=upper.position+rootOffset;
    const Vec3 upperDirection=lower.position-upper.position,lowerDirection=oldWrist.position-lower.position;
    const float upperLength=std::sqrt(Dot(upperDirection,upperDirection));
    const float lowerLength=std::sqrt(Dot(lowerDirection,lowerDirection));
    Vec3 pole=lower.position-(upper.position+oldWrist.position)*0.5f;
    if (Dot(pole,pole)<0.000001f) pole={0,0,-1};
    const Vec3 outward=torso.left*(side?-1.0f:1.0f)-torso.up*0.6f;
    const float poleLength=std::sqrt(Dot(pole,pole)),outwardLength=std::sqrt(Dot(outward,outward));
    if (poleLength>0.0001f&&outwardLength>0.0001f)
        pole=pole*(0.25f/poleLength)+outward*(0.75f/outwardLength);
    float solveUpper=upperLength,solveLower=lowerLength;
    const float reach=upperLength+lowerLength;
    const Vec3 toTarget=wrist.position-upper.position;
    const float targetDistance=std::sqrt(Dot(toTarget,toTarget));
    if (reach>0.0001f&&targetDistance>reach)
    {
        const float stretch=std::min(targetDistance/reach,1.8f);
        solveUpper*=stretch;solveLower*=stretch;
    }
    Vec3 solved{};
    if (!IK_SolveTwoBone(&upper.position.x,&wrist.position.x,solveUpper,solveLower,&pole.x,&solved.x)||
        !RotateBoneToward(upper,upperDirection,solved-upper.position,upper.position,palette[shoulder])||
        !RotateBoneToward(lower,lowerDirection,wrist.position-solved,solved,palette[elbow])) return false;
    return true;
}

// Preserve the shared controller-role presentation by default. The explicit
// hand-alignment option keeps physical left/right geometry on matching hands
// in left-handed mode. The weapon receives its own primary carrier either way.
inline bool BuildTrackedFirstPersonPalette(const FirstPersonBinding& binding,
    const NodeMatrix* source,const Camera& camera,const Tracking& tracking,const Reference& reference,
    float unitsPerMeter,bool positional,std::array<NodeMatrix,kFirstPersonMaxNodes>& out) noexcept
{
    const auto& rig=tracking.controllers;
    if (rig.controlsPresentationBlocked||!source||binding.generation!=tracking.generation||!binding.count||
        binding.count>kFirstPersonMaxNodes||binding.rightWrist<0||binding.leftWrist<0||binding.gun<0||
        binding.rightWrist>=binding.count||binding.leftWrist>=binding.count||binding.gun>=binding.count) return false;
    NodeMatrix primaryAim{},supportAim{};
    if (!BuildControllerMatrix(camera,tracking,reference,rig.primaryAim,unitsPerMeter,positional,primaryAim)||
        !BuildControllerMatrix(camera,tracking,reference,rig.support,unitsPerMeter,positional,supportAim)||
        !ApplyVisualMount(rig,unitsPerMeter,true,primaryAim)||
        !ApplyVisualMount(rig,unitsPerMeter,false,supportAim)) return false;
    NodeMatrix weaponGrip{},unused{},left{},right{};
    if (!BuildGripCarriers(binding,source,binding.count,primaryAim,supportAim,weaponGrip,unused)) return false;
    const NodeMatrix& gun=source[binding.gun];
    const bool anatomical=rig.leftHanded&&rig.handAlignment;
    const NodeMatrix& rightAim=anatomical?supportAim:primaryAim;
    const NodeMatrix& leftAim=anatomical?primaryAim:supportAim;
    if (!MoveNode(gun,rightAim,source[binding.rightWrist],right)||
        !MoveNode(gun,leftAim,source[binding.leftWrist],left)) return false;
    right.position=rightAim.position;left.position=leftAim.position;
    const HandAlignmentPlane* alignmentPlane=anatomical?FindHandAlignmentPlane(binding):nullptr;
    NodeMatrix renderedGun{};
    if (alignmentPlane)
    {
        // Use the identical carrier/operation as the unchanged gun-node loop
        // below. Primary-left must inherit the primary-right authored grip,
        // not the unrelated native support wrist animation.
        if (!MoveNode(source[binding.rightWrist],weaponGrip,gun,renderedGun)||
            !BuildMirroredGripTarget(*alignmentPlane,gun,source[binding.rightWrist],renderedGun,left)) return false;
    }
    if (rig.supportGripAttached)
    {
        // Match the accepted planted support-grip policy; releasing grip
        // immediately restores the independent support controller target.
        // Persistent support grip: presentation follows the valid weapon
        // relationship even when support aim authority is reduced/rejected.
        NodeMatrix& support=anatomical?right:left;
        NodeMatrix supportPosition{};
        if (!MoveNode(source[binding.rightWrist],weaponGrip,source[binding.leftWrist],supportPosition)) return false;
        if (alignmentPlane)
        {
            if (!BuildMirroredGripTarget(*alignmentPlane,gun,source[binding.leftWrist],renderedGun,support)) return false;
        }
        else if (anatomical)
        {
            if (!MoveNode(gun,primaryAim,source[binding.rightWrist],support)) return false;
            support.position=supportPosition.position;
        }
        else support=supportPosition;
    }
    std::array<NodeMatrix,kFirstPersonMaxNodes> candidate{};
    for (size_t i=0;i<binding.count;++i)
    {
        if (!Valid(source[i])) return false;
        candidate[i]=source[i];const uint64_t bit=uint64_t{1}<<i;
        if (binding.gunMask&bit)
        { if (!MoveNode(source[binding.rightWrist],weaponGrip,source[i],candidate[i])) return false; }
        else if (binding.rightMask&bit)
        { if (!MoveNode(source[binding.rightWrist],right,source[i],candidate[i])) return false; }
        else if (binding.leftMask&bit)
        { if (!MoveNode(source[binding.leftWrist],left,source[i],candidate[i])) return false; }
    }
    for (size_t side=0;side<2;++side)
    {
        const NodeMatrix& target=side?right:left;
        if (ShouldApplyArmIk(rig.armIk,rig.supportGripAttached))
        {
            if (!SolveFirstPersonArm(binding,side,source,camera,rig,target,candidate)) return false;
        }
        else
        {
            const NodeMatrix& carrier=source[side?binding.rightWrist:binding.leftWrist];
            for (const int joint:{binding.shoulder[side],binding.elbow[side]})
                if (joint>=0&&joint<binding.count&&!MoveNode(carrier,target,source[joint],candidate[joint])) return false;
        }
    }
    if (rig.floatingHands&&kCeFloatingArmsAtCameraEnabled)
        // Saber also uses the remapped model's root matrix to position the
        // entire first-person object (native bridge 0x7AC60). Keep the graph
        // root intact so hiding arm geometry cannot collapse that carrier.
        for (size_t i=1;i<binding.count;++i)
            if (!((binding.rightMask|binding.leftMask|binding.gunMask)&(uint64_t{1}<<i)))
            { candidate[i].scale=0.00001f;candidate[i].position=camera.position; }
    if (rig.floatingHands&&!kCeFloatingArmsAtCameraEnabled&&
        !CollapseFirstPersonArmsAtWrists(binding,candidate)) return false;
    out=candidate;return true;
}

// All-or-nothing private palette staging. Runtime code must verify graph and
// generation, then publish this palette in the engine's own FP consumption
// scope. This helper alone does not install or claim a renderer integration.
inline bool RelocateFirstPersonPalette(const FirstPersonBinding& binding,
    uint32_t graph,uint32_t generation,const NodeMatrix* source,size_t count,
    const NodeMatrix& primaryCarrier,const NodeMatrix& supportCarrier,
    bool twoHandAim,std::array<NodeMatrix,kFirstPersonMaxNodes>& out) noexcept
{
    if (!source||graph!=binding.graph||generation!=binding.generation||
        !generation||count!=binding.count||count>kFirstPersonMaxNodes||
        binding.rightWrist<0||binding.leftWrist<0||binding.gun<0||
        size_t(binding.rightWrist)>=count||size_t(binding.leftWrist)>=count||
        size_t(binding.gun)>=count||!Valid(primaryCarrier)||!Valid(supportCarrier)) return false;
    std::array<NodeMatrix,kFirstPersonMaxNodes> candidate{};
    for (size_t i=0;i<count;++i)
    {
        if (!Valid(source[i])) return false;
        candidate[i]=source[i];
        const uint64_t bit=uint64_t{1}<<i;
        if (binding.rightMask&bit)
        {
            if (!MoveNode(source[binding.rightWrist],primaryCarrier,source[i],candidate[i]))
                return false;
        }
        else if (binding.leftMask&bit)
        {
            const NodeMatrix& sourceCarrier=source[twoHandAim?binding.rightWrist:binding.leftWrist];
            const NodeMatrix& targetCarrier=twoHandAim?primaryCarrier:supportCarrier;
            if (!MoveNode(sourceCarrier,targetCarrier,source[i],candidate[i])) return false;
        }
    }
    out=candidate;
    return true;
}
}
