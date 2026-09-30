#pragma once

#include "controller_finger_input.h"
#include "finger_joint_identity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// A title supplies its own exact output-palette hierarchy and finger chains.
// Transform must expose scale, rotation[9] (column-major), translation[3].
// This helper only applies controller-shaped flexion to a private candidate;
// it does not choose local-player ownership or decide whether a hand is free.
namespace controller_finger_pose {
inline bool ValidTransform(const float scale,const float rotation[9],
    const float translation[3]) noexcept
{
    if(!std::isfinite(scale)||std::fabs(scale)<1.0e-6f)return false;
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(translation[axis]))return false;
    for(float value:std::array<float,9>{rotation[0],rotation[1],rotation[2],
        rotation[3],rotation[4],rotation[5],rotation[6],rotation[7],rotation[8]})
        if(!std::isfinite(value))return false;
    return true;
}
inline bool Normalize(float (&value)[3]) noexcept
{
    const float length2=value[0]*value[0]+value[1]*value[1]+value[2]*value[2];
    if(!std::isfinite(length2)||length2<1.0e-10f)return false;
    const float inverse=1.0f/std::sqrt(length2);
    for(float& axis:value)axis*=inverse;
    return true;
}
inline void Cross(const float (&a)[3],const float (&b)[3],float (&out)[3]) noexcept
{
    out[0]=a[1]*b[2]-a[2]*b[1];
    out[1]=a[2]*b[0]-a[0]*b[2];
    out[2]=a[0]*b[1]-a[1]*b[0];
}
inline void Multiply(const float a[9],const float b[9],float out[9]) noexcept
{
    float result[9]{};
    for(int column=0;column<3;++column)
        for(int row=0;row<3;++row)
            for(int k=0;k<3;++k)
                result[column*3+row]+=a[k*3+row]*b[column*3+k];
    std::memcpy(out,result,sizeof(result));
}
inline void RotateVector(const float rotation[9],const float value[3],float out[3]) noexcept
{
    for(int row=0;row<3;++row)
        out[row]=rotation[row]*value[0]+rotation[3+row]*value[1]+rotation[6+row]*value[2];
}
template<class Transform>
inline bool Invert(const Transform& input,Transform& output) noexcept
{
    if(!ValidTransform(input.scale,input.rotation,input.translation))return false;
    Transform result{};
    result.scale=1.0f/input.scale;
    for(int column=0;column<3;++column)
        for(int row=0;row<3;++row)
            result.rotation[column*3+row]=input.rotation[row*3+column];
    const float negative[3]{-input.translation[0],-input.translation[1],-input.translation[2]};
    RotateVector(result.rotation,negative,result.translation);
    for(float& value:result.translation)value*=result.scale;
    if(!ValidTransform(result.scale,result.rotation,result.translation))return false;
    output=result;return true;
}
template<class Transform>
inline bool Compose(const Transform& left,const Transform& right,Transform& output) noexcept
{
    if(!ValidTransform(left.scale,left.rotation,left.translation)||
        !ValidTransform(right.scale,right.rotation,right.translation))return false;
    Transform result{};
    result.scale=left.scale*right.scale;
    Multiply(left.rotation,right.rotation,result.rotation);
    float relative[3]{};RotateVector(left.rotation,right.translation,relative);
    for(int axis=0;axis<3;++axis)
        result.translation[axis]=left.translation[axis]+relative[axis]*left.scale;
    if(!ValidTransform(result.scale,result.rotation,result.translation))return false;
    output=result;return true;
}
inline bool MakeRotation(const float (&axis)[3],float angle,float out[9]) noexcept
{
    if(!std::isfinite(angle))return false;
    const float x=axis[0],y=axis[1],z=axis[2];
    const float c=std::cos(angle),s=std::sin(angle),t=1.0f-c;
    const float result[9]{t*x*x+c,t*x*y+s*z,t*x*z-s*y,
        t*x*y-s*z,t*y*y+c,t*y*z+s*x,
        t*x*z+s*y,t*y*z-s*x,t*z*z+c};
    for(float value:result)if(!std::isfinite(value))return false;
    std::memcpy(out,result,sizeof(result));return true;
}
inline bool IsDescendant(const int16_t* parents,size_t count,size_t node,size_t root) noexcept
{
    if(!parents||node>=count||root>=count)return false;
    for(size_t steps=0;steps<count;++steps)
    {
        if(node==root)return true;
        const int parent=parents[node];
        if(parent<0||static_cast<size_t>(parent)>=count)return false;
        node=static_cast<size_t>(parent);
    }
    return false;
}
template<class Transform,size_t MaxNodes>
inline bool RotateSubtree(Transform (&pose)[MaxNodes],const int16_t* parents,
    size_t count,size_t root,const float (&delta)[9]) noexcept
{
    if(root>=count||!ValidTransform(pose[root].scale,pose[root].rotation,
        pose[root].translation))return false;
    const float pivot[3]{pose[root].translation[0],pose[root].translation[1],
        pose[root].translation[2]};
    for(size_t node=0;node<count;++node)
    {
        if(!IsDescendant(parents,count,node,root))continue;
        if(!ValidTransform(pose[node].scale,pose[node].rotation,
            pose[node].translation))return false;
        float relative[3]{pose[node].translation[0]-pivot[0],
            pose[node].translation[1]-pivot[1],pose[node].translation[2]-pivot[2]};
        float moved[3]{},basis[9]{};
        RotateVector(delta,relative,moved);
        Multiply(delta,pose[node].rotation,basis);
        for(int axis=0;axis<3;++axis)pose[node].translation[axis]=pivot[axis]+moved[axis];
        std::memcpy(pose[node].rotation,basis,sizeof(basis));
        if(!ValidTransform(pose[node].scale,pose[node].rotation,
            pose[node].translation))return false;
    }
    return true;
}

// Finger order is thumb, index, middle, ring, pinky. A missing digit has a
// zero chain length. Each chain is proximal-to-distal, with its exact parent
// indices supplied by that title's official render-model tag.
template<class Transform,size_t MaxNodes>
inline bool Apply(const Transform* source,size_t nodeCount,size_t wrist,
    const int16_t* parents,const uint16_t (&chains)[5][4],
    const uint8_t (&chainLengths)[5],const float (&palmDown)[3],
    const ControllerFingerInput& input,Transform (&output)[MaxNodes],
    const Transform* inverseBind=nullptr,
    const finger_joint::Inventory* inventory=nullptr) noexcept
{
    if(!source||!parents||nodeCount==0||nodeCount>MaxNodes||wrist>=nodeCount)
        return false;
    Transform candidate[MaxNodes]{};
    std::memcpy(candidate,source,nodeCount*sizeof(Transform));
    if(!input.valid)
    {
        std::memcpy(output,candidate,nodeCount*sizeof(Transform));
        return true;
    }
    if(!std::isfinite(input.trigger)||!std::isfinite(input.grip)||
        input.trigger<0.0f||input.trigger>1.0f||input.grip<0.0f||input.grip>1.0f)
        return false;
    if(!ValidTransform(candidate[wrist].scale,candidate[wrist].rotation,
        candidate[wrist].translation))return false;
    float down[3]{palmDown[0],palmDown[1],palmDown[2]};
    if(!Normalize(down))return false;
    constexpr float flex[4]{0.42f,0.60f,0.74f,0.78f};
    Transform currentWristInverse{};
    if(inverseBind)
    {
        currentWristInverse=inverseBind[wrist];
        if(!ValidTransform(currentWristInverse.scale,currentWristInverse.rotation,
            currentWristInverse.translation))return false;
    }
    for(unsigned finger=0;finger<5;++finger)
    {
        const unsigned joints=chainLengths[finger];
        if(!joints)continue;
        if(joints>4)return false;
        for(unsigned joint=0;joint<joints;++joint)
        {
            const unsigned node=chains[finger][joint];
            if(inventory) {
                const auto* identity=inventory->FindNative(finger,joint);
                if(node>=nodeCount||inventory->rig.nodeCount!=nodeCount||!identity||
                   !(identity->rig==inventory->rig)||identity->hand!=inventory->hand||
                   identity->paletteIndex!=node||identity->parentIndex!=parents[node])return false;
            }
            if(node>=nodeCount||parents[node]<0||
                (joint==0&&static_cast<size_t>(parents[node])!=wrist)||
                (joint>0&&static_cast<size_t>(parents[node])!=chains[finger][joint-1]))
                return false;
        }
        if(inverseBind)
            for(unsigned joint=0;joint<joints;++joint)
            {
                const size_t node=chains[finger][joint];
                Transform bind{},relative{},posed{};
                if(!Invert(inverseBind[node],bind)||
                    !Compose(currentWristInverse,bind,relative)||
                    !Compose(candidate[wrist],relative,posed))return false;
                candidate[node]=posed;
            }
        // Native chain order is title-specific (CE index is slot zero), and
        // an unknown H2 digit must not be guessed from its ordinal slot.
        const bool isIndex=inventory
            ? inventory->FindNative(finger,0)->digit==finger_joint::Digit::Index
            : finger==1;
        const float curl=isIndex?input.trigger:input.grip;
        if(curl<=0.0f)continue;
        for(unsigned joint=0;joint<joints;++joint)
        {
            const size_t root=inventory?inventory->FindNative(finger,joint)->paletteIndex:chains[finger][joint];
            const size_t next=joint+1<joints?chains[finger][joint+1]:root;
            float segment[3]{candidate[next].translation[0]-candidate[root].translation[0],
                candidate[next].translation[1]-candidate[root].translation[1],
                candidate[next].translation[2]-candidate[root].translation[2]};
            if(joint+1==joints)
            {
                const int parent=parents[root];
                if(parent<0||static_cast<size_t>(parent)>=nodeCount)return false;
                for(int axis=0;axis<3;++axis)
                    segment[axis]=candidate[root].translation[axis]-
                        candidate[static_cast<size_t>(parent)].translation[axis];
            }
            float axis[3]{};
            Cross(segment,down,axis);
            if(!Normalize(axis))return false;
            float delta[9]{};
            if(!MakeRotation(axis,curl*flex[joint],delta)||
                !RotateSubtree(candidate,parents,nodeCount,root,delta))return false;
        }
    }
    std::memcpy(output,candidate,nodeCount*sizeof(Transform));
    return true;
}
} // namespace controller_finger_pose
