#pragma once
#include <algorithm>
#include <array>
#include "anatomical_palette_logic.h"
#include "controller_finger_input.h"
#include "finger_joint_identity.h"

// Own H3EK full-world graphs. Halo4FloatingTransform is only the shared,
// already-used 0x34 math value type; no H4 model indices/layouts are reused.
namespace halo3_avatar
{
constexpr unsigned Capacity=55, RegionCapacity=16;
using Matrix=Halo4FloatingTransform;
using Pose=std::array<Matrix,Capacity>;
using GpuMatrix=std::array<float,12>;
using GpuPose=std::array<GpuMatrix,Capacity>;
struct Profile
{
    uint32_t checksum{},fpChecksum{};
    unsigned count{},fpCount{},regions{},head{},shoulder[2]{},elbow[2]{},wrist[2]{};
    uint16_t headRegions{},lowerRegions{};
    const int16_t* parents{};
    Matrix palm[2]{},headMarker{}; // anatomical left/right, own world markers
};
inline constexpr int16_t ChiefParents[51]{
 -1,0,0,0,1,2,3,4,5,6,6,6,7,8,9,10,11,14,16,17,18,19,19,19,19,19,
 20,20,20,20,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40};
inline constexpr int16_t EliteParents[55]{
 -1,0,0,0,0,1,3,4,5,6,7,7,7,8,9,10,11,12,15,16,17,18,19,19,19,19,
 19,19,19,19,19,19,19,19,19,19,19,19,20,21,21,21,21,38,38,38,38,39,40,41,42,43,44,45,46};

inline bool SelectProfile(uint32_t checksum,unsigned count,Profile& out) noexcept
{
    Profile p{};p.checksum=checksum;p.count=count;
    float lp[3]{},lq[4]{},rp[3]{},rq[4]{},hp[3]{},hq[4]{};
    if(checksum==0x17121B0D&&count==51)
    {
        p.fpChecksum=504041493;p.fpCount=37;p.regions=6;p.head=15;
        p.shoulder[0]=14;p.elbow[0]=17;p.wrist[0]=19;
        p.shoulder[1]=16;p.elbow[1]=18;p.wrist[1]=20;
        p.parents=ChiefParents;p.headRegions=1u<<2;p.lowerRegions=1u<<4;
        const float a[]{.0385863f,.00105648f,.00039771f},b[]{-.993521f,.08508f,-.0736134f,-.0160779f};
        const float c[]{.0465319f,.00432991f,-.00443903f},d[]{-.0160801f,-.0736132f,.0850802f,-.993521f};
        const float e[]{.0395201f,.00404823f,-.000000216344f},f[]{-.5f,-.500001f,-.5f,-.499999f};
        std::memcpy(lp,a,sizeof(lp));std::memcpy(lq,b,sizeof(lq));
        std::memcpy(rp,c,sizeof(rp));std::memcpy(rq,d,sizeof(rq));
        std::memcpy(hp,e,sizeof(hp));std::memcpy(hq,f,sizeof(hq));
    }
    else if((checksum==0x10140D04||checksum==0x17180910)&&count==55)
    {
        const bool elite=checksum==0x10140D04;
        p.fpChecksum=elite?269159697:268439051;p.fpCount=31;
        p.regions=elite?9:2;p.head=19;p.parents=EliteParents;
        p.shoulder[0]=15;p.elbow[0]=18;p.wrist[0]=21;
        p.shoulder[1]=17;p.elbow[1]=20;p.wrist[1]=38;
        p.headRegions=elite?uint16_t((1u<<3)|(1u<<4)|(1u<<5)):uint16_t(1u<<1);
        // Elite/Dervish legs share body meshes. No invented pelvis mask.
        p.lowerRegions=0;
        const float a[]{elite?.0625643f:.0539896f,elite?.0100183f:.0128921f,elite?-.00200744f:.00539522f};
        const float b[]{elite?-.99603f:-.999995f,elite?-.0889815f:-.00183255f,elite?-.00165731f:-.00150211f,elite?.00171596f:.00185326f};
        const float c[]{elite?.0585219f:.0539915f,elite?.00938403f:.012454f,elite?.00351822f:-.00539434f};
        const float d[]{elite?.0018517f:.00185116f,elite?-.00150144f:-.00150207f,elite?-.00183339f:-.00183277f,-.999995f};
        const float e[]{.0206507f,.0427046f,elite?-.00000019281f:-.000000193585f},f[]{-.67438f,-.212632f,-.212631f,-.674379f};
        std::memcpy(lp,a,sizeof(lp));std::memcpy(lq,b,sizeof(lq));
        std::memcpy(rp,c,sizeof(rp));std::memcpy(rq,d,sizeof(rq));
        std::memcpy(hp,e,sizeof(hp));std::memcpy(hq,f,sizeof(hq));
    }
    else return false;
    if(!BuildAnatomicalPalmMarker(lp,lq,p.palm[0])||
       !BuildAnatomicalPalmMarker(rp,rq,p.palm[1])||
       !BuildAnatomicalPalmMarker(hp,hq,p.headMarker))return false;
    out=p;return true;
}

inline bool Descendant(const Profile& p,unsigned node,unsigned ancestor) noexcept
{
    if(node>=p.count||ancestor>=p.count)return false;
    for(unsigned depth=0;depth<p.count;++depth)
    {
        const int parent=p.parents[node];
        if(parent<0)return false;
        if(unsigned(parent)>=p.count)return false;
        if(unsigned(parent)==ancestor)return true;
        node=unsigned(parent);
    }
    return false;
}

// Preserve semantic palm placement across the independently authored FP/world
// wrist frames. Directly copying the FP wrist rotates the Chief left hand wrong.
inline bool WorldWristTarget(const Profile& p,unsigned side,
    const Matrix& fpWrist,Matrix& output) noexcept
{
    if(side>1)return false;
    AnatomicalPalmMarkers fp{};Matrix palm{},inverse{},result{};
    if(!Halo3AnatomicalPalmMarkers(p.fpChecksum,int(p.fpCount),fp)||
       !Halo4ComposeFloatingTransforms(fpWrist,side==0?fp.left:fp.right,palm)||
       !Halo4InvertFloatingTransform(p.palm[side],inverse)||
       !Halo4ComposeFloatingTransforms(palm,inverse,result))return false;
    output=result;return true;
}

namespace math
{
inline float Dot(const float* a,const float* b) noexcept
{return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline void Cross(const float* a,const float* b,float* o) noexcept
{o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0];}
inline float Distance(const float* a,const float* b) noexcept
{const float d[]{a[0]-b[0],a[1]-b[1],a[2]-b[2]};return std::sqrt(Dot(d,d));}
inline bool Normalize(float* v) noexcept
{
    const float n=std::sqrt(Dot(v,v));if(!std::isfinite(n)||n<1e-6f)return false;
    for(unsigned i=0;i<3;++i)v[i]/=n;return true;
}
inline bool Swing(const float* from,const float* to,float out[9]) noexcept
{
    float a[]{from[0],from[1],from[2]},b[]{to[0],to[1],to[2]},axis[3];
    if(!Normalize(a)||!Normalize(b))return false;
    const float c=std::clamp(Dot(a,b),-1.f,1.f);Cross(a,b,axis);
    float s=std::sqrt(Dot(axis,axis));
    if(s<1e-6f)
    {
        if(c>0){const float id[]{1,0,0,0,1,0,0,0,1};std::memcpy(out,id,sizeof(id));return true;}
        const float h[]{std::fabs(a[0])<.8f?1.f:0.f,std::fabs(a[0])<.8f?0.f:1.f,0};
        Cross(a,h,axis);if(!Normalize(axis))return false;s=0;
    }
    else for(float& v:axis)v/=s;
    const float x=axis[0],y=axis[1],z=axis[2],t=1-c;
    out[0]=t*x*x+c;out[1]=t*x*y+s*z;out[2]=t*x*z-s*y;
    out[3]=t*x*y-s*z;out[4]=t*y*y+c;out[5]=t*y*z+s*x;
    out[6]=t*x*z+s*y;out[7]=t*y*z-s*x;out[8]=t*z*z+c;return true;
}
inline bool MoveSubtree(const Profile& p,Pose& pose,unsigned root,
    const Matrix& target) noexcept
{
    Matrix inverse{},delta{};
    if(root>=p.count||!Halo4InvertFloatingTransform(pose[root],inverse)||
       !Halo4ComposeFloatingTransforms(target,inverse,delta))return false;
    for(unsigned i=0;i<p.count;++i)if(i==root||Descendant(p,i,root))
    {Matrix moved{};if(!Halo4ComposeFloatingTransforms(delta,pose[i],moved))return false;pose[i]=moved;}
    return true;
}
// Bounded FABRIK over each model's own chain. The original joint bend seeds
// the solution; foot endpoints retain the native animated reference. No heap,
// simulated body write, fixed-angle elbow, or cross-title bone number.
inline bool Chain(const Profile& p,Pose& pose,const unsigned* nodes,unsigned count,
    const Matrix& target,const float pole[3]) noexcept
{
    if(count<3||count>4||!Halo4FloatingTransformValid(target))return false;
    float points[4][3]{},length[3]{},total=0,root[3]{};
    for(unsigned i=0;i<count;++i)
    {
        if(nodes[i]>=p.count||(i&&p.parents[nodes[i]]!=int(nodes[i-1])))return false;
        std::memcpy(points[i],pose[nodes[i]].translation,sizeof(points[i]));
        if(i){length[i-1]=Distance(points[i-1],points[i]);if(length[i-1]<1e-5f||length[i-1]>2)return false;total+=length[i-1];}
    }
    std::memcpy(root,points[0],sizeof(root));
    const float span=Distance(root,target.translation);
    if(!std::isfinite(span)||span>4||span<1e-5f)return false;
    if(span>=total)
    {
        float direction[]{target.translation[0]-root[0],target.translation[1]-root[1],target.translation[2]-root[2]};
        if(!Normalize(direction))return false;
        const float stretch=span/total;if(stretch>1.75f)return false;
        for(unsigned i=1;i<count;++i)for(unsigned a=0;a<3;++a)
            points[i][a]=points[i-1][a]+direction[a]*length[i-1]*stretch;
    }
    else
    {
        // A perfectly straight authored limb has no bend plane. Seed a small
        // bend toward the caller's body-relative pole before FABRIK; otherwise
        // physical crouch can remain on a singular collinear solution.
        float axis[]{target.translation[0]-root[0],target.translation[1]-root[1],target.translation[2]-root[2]};
        if(!Normalize(axis))return false;
        float bend[]{points[1][0]-root[0],points[1][1]-root[1],points[1][2]-root[2]};
        float along=Dot(bend,axis);for(unsigned a=0;a<3;++a)bend[a]-=along*axis[a];
        if(Dot(bend,bend)<1e-8f)
        {
            std::memcpy(bend,pole,sizeof(bend));along=Dot(bend,axis);
            for(unsigned a=0;a<3;++a)bend[a]-=along*axis[a];
            if(!Normalize(bend))return false;
            for(unsigned i=1;i+1<count;++i)for(unsigned a=0;a<3;++a)
                points[i][a]+=bend[a]*total*.02f;
        }
        for(unsigned iteration=0;iteration<32;++iteration)
        {
            std::memcpy(points[count-1],target.translation,sizeof(points[0]));
            for(unsigned i=count-1;i>0;--i)
            {
                float direction[]{points[i-1][0]-points[i][0],points[i-1][1]-points[i][1],points[i-1][2]-points[i][2]};
                if(!Normalize(direction))return false;
                for(unsigned a=0;a<3;++a)points[i-1][a]=points[i][a]+direction[a]*length[i-1];
            }
            std::memcpy(points[0],root,sizeof(root));
            for(unsigned i=1;i<count;++i)
            {
                float direction[]{points[i][0]-points[i-1][0],points[i][1]-points[i-1][1],points[i][2]-points[i-1][2]};
                if(!Normalize(direction))return false;
                for(unsigned a=0;a<3;++a)points[i][a]=points[i-1][a]+direction[a]*length[i-1];
            }
            if(Distance(points[count-1],target.translation)<1e-5f)break;
        }
    }
    if(Distance(points[count-1],target.translation)>2e-3f)return false;
    for(unsigned i=0;i+1<count;++i)
    {
        const auto node=nodes[i],child=nodes[i+1];
        float from[]{pose[child].translation[0]-pose[node].translation[0],pose[child].translation[1]-pose[node].translation[1],pose[child].translation[2]-pose[node].translation[2]};
        float to[]{points[i+1][0]-points[i][0],points[i+1][1]-points[i][1],points[i+1][2]-points[i][2]};
        Matrix rotation{},turned{},desired{};
        if(!Swing(from,to,rotation.rotation)||!Halo4ComposeFloatingTransforms(rotation,pose[node],turned))return false;
        desired=turned;std::memcpy(desired.translation,points[i],sizeof(points[i]));
        if(!MoveSubtree(p,pose,node,desired))return false;
    }
    return MoveSubtree(p,pose,nodes[count-1],target);
}
}

struct Request
{
    Matrix headWorld{},fpWristWorld[2]{}; // anatomical sides, same tracking sample
    bool pinNativeFeet=true;
    bool fingerPoseAllowed[2]{};
    ControllerFingerInput fingerInput[2]{};
    const Pose* fingerInverseBind=nullptr;
};

// Own world-tag chains: Chief has five three-joint fingers; Elite/Dervish
// have four two-joint fingers. Controller curl is presentation, not optical
// finger tracking or a claim of contact-reactive finger collision.
inline bool DescribeFingers(uint32_t checksum,unsigned count,unsigned side,finger_joint::Inventory& out) noexcept {
    Profile p{};if(side>1||!SelectProfile(checksum,count,p))return false;
    const unsigned chief[2][5][3]{
      {{21,31,41},{22,32,42},{23,33,43},{24,34,44},{25,35,45}},
      {{26,36,46},{27,37,47},{28,38,48},{29,39,49},{30,40,50}}};
    const unsigned elite[2][5][2]{
      {{39,47},{40,48},{41,49},{42,50},{0,0}},{{43,51},{44,52},{45,53},{46,54},{0,0}}};
    using D=finger_joint::Digit;
    const D humanDigits[5]{D::Index,D::Middle,D::Pinky,D::Ring,D::Thumb};
    const D eliteDigits[5]{D::Index,D::Pinky,D::Ring,D::Thumb,D::Unknown};
    const unsigned humanLengths[5]{3,3,3,3,3},eliteLengths[5]{2,2,2,2,0};
    const finger_joint::RigKey key{GameTitle::Halo3,checksum,uint16_t(count),finger_joint::Palette::WorldBody};
    return count==51?finger_joint::Build(key,side,p.wrist[side],p.parents,chief[side],humanLengths,humanDigits,out):
        finger_joint::Build(key,side,p.wrist[side],p.parents,elite[side],eliteLengths,eliteDigits,out);
}
inline bool PoseFingers(const Profile& p,Pose& pose,unsigned side,
    const ControllerFingerInput& input) noexcept
{
    if(side>1)return false;
    if(!input.valid)return true;
    if(!std::isfinite(input.trigger)||!std::isfinite(input.grip)||
       input.trigger<0||input.trigger>1||input.grip<0||input.grip>1)return false;
    Matrix palm{};
    if(!Halo4ComposeFloatingTransforms(pose[p.wrist[side]],p.palm[side],palm))return false;
    // Palm marker's forward/left/up frame supplies a title-authored curl axis.
    float axis[]{palm.rotation[3],palm.rotation[4],palm.rotation[5]};
    if(!math::Normalize(axis))return false;
    Pose candidate=pose;
    finger_joint::Inventory inventory{};if(!DescribeFingers(p.checksum,p.count,side,inventory))return false;
    const bool human=p.count==51;
    for(unsigned finger=0;finger<(human?5u:4u);++finger)
    {
        const float curl=finger==0?input.trigger:input.grip;
        for(unsigned joint=0;joint<(human?3u:2u);++joint)
        {
            const unsigned node=inventory.FindNative(finger,joint)->paletteIndex;
            if(node>=p.count||!Descendant(p,node,p.wrist[side]))return false;
            const float angle=-curl*(joint==0?.42f:(joint==1?.60f:.74f));
            const float c=std::cos(angle),s=std::sin(angle),t=1-c;
            const float x=axis[0],y=axis[1],z=axis[2];
            Matrix rotation{},desired{};
            const float rows[]{t*x*x+c,t*x*y+s*z,t*x*z-s*y,
                t*x*y-s*z,t*y*y+c,t*y*z+s*x,t*x*z+s*y,t*y*z-s*x,t*z*z+c};
            std::memcpy(rotation.rotation,rows,sizeof(rows));
            if(!Halo4ComposeFloatingTransforms(rotation,candidate[node],desired))return false;
            std::memcpy(desired.translation,candidate[node].translation,sizeof(desired.translation));
            if(!math::MoveSubtree(p,candidate,node,desired))return false;
        }
    }
    pose=candidate;return true;
}
inline bool PoseFingersFromBind(const Profile& p,Pose& pose,unsigned side,
    const Pose& inverseBind,const ControllerFingerInput& input) noexcept
{
    if(side>1||p.count>Capacity||!p.parents)return false;
    if(!input.valid)return true;
    Pose candidate=pose;const unsigned wrist=p.wrist[side];
    if(wrist>=p.count||!Halo4FloatingTransformValid(inverseBind[wrist]))return false;
    for(unsigned node=0;node<p.count;++node)if(node!=wrist&&Descendant(p,node,wrist))
    {
        Matrix bind{},relative{};
        if(!Halo4InvertFloatingTransform(inverseBind[node],bind)||
           !Halo4ComposeFloatingTransforms(inverseBind[wrist],bind,relative)||
           !Halo4ComposeFloatingTransforms(pose[wrist],relative,candidate[node]))return false;
    }
    if(!PoseFingers(p,candidate,side,input))return false;
    pose=candidate;return true;
}
inline bool Solve(const Profile& p,const Pose& source,const Request& request,Pose& output) noexcept
{
    if(p.count==0||p.count>Capacity||!p.parents||!Halo4FloatingTransformValid(request.headWorld))return false;
    Pose candidate=source;
    for(unsigned i=0;i<p.count;++i)if(!Halo4FloatingTransformValid(source[i]))return false;
    Matrix nativeHead{},inverseHead{},headTarget{};
    if(!Halo4ComposeFloatingTransforms(source[p.head],p.headMarker,nativeHead)||
       !Halo4InvertFloatingTransform(p.headMarker,inverseHead)||
       !Halo4ComposeFloatingTransforms(request.headWorld,inverseHead,headTarget))return false;
    const float delta[]{request.headWorld.translation[0]-nativeHead.translation[0],request.headWorld.translation[1]-nativeHead.translation[1],request.headWorld.translation[2]-nativeHead.translation[2]};
    if(!std::isfinite(math::Dot(delta,delta))||math::Dot(delta,delta)>4)return false;
    for(unsigned i=0;i<p.count;++i)for(unsigned axis=0;axis<3;++axis)candidate[i].translation[axis]+=delta[axis];
    if(!math::MoveSubtree(p,candidate,p.head,headTarget))return false;
    for(unsigned side=0;side<2;++side)
    {
        Matrix target{};const unsigned chain[]{p.shoulder[side],p.elbow[side],p.wrist[side]};
        const float pole[]{0,0,-1};
        if(!WorldWristTarget(p,side,request.fpWristWorld[side],target)||
           !math::Chain(p,candidate,chain,3,target,pole))return false;
        if(request.fingerPoseAllowed[side]&&(!request.fingerInverseBind||
           !PoseFingersFromBind(p,candidate,side,*request.fingerInverseBind,request.fingerInput[side])))return false;
    }
    if(request.pinNativeFeet)
    {
        const unsigned chief[2][3]{{1,4,7},{2,5,8}};
        const unsigned elite[2][4]{{1,5,8,13},{3,6,9,14}};
        for(unsigned side=0;side<2;++side)
        {
            const bool human=p.count==51;const unsigned* chain=human?chief[side]:elite[side];
            const unsigned count=human?3:4;
            if(!math::Chain(p,candidate,chain,count,source[chain[count-1]],request.headWorld.rotation))return false;
        }
    }
    output=candidate;return true;
}

// Own H3EK7D26E0/7D2DC0 and retail266838: world node times inverse bind,
// then fold scale into transposed 3x4 shader rows. Source remains immutable.
inline bool BuildGpuPose(const Profile& p,const Pose& world,const Pose& inverseBind,
    GpuPose& output) noexcept
{
    if(p.count==0||p.count>Capacity)return false;
    GpuPose candidate{};
    for(unsigned i=0;i<p.count;++i)
    {
        Matrix m{};
        if(!Halo4ComposeFloatingTransforms(world[i],inverseBind[i],m))return false;
        for(unsigned row=0;row<3;++row)
        {
            for(unsigned col=0;col<3;++col)candidate[i][row*4+col]=m.rotation[col*3+row]*m.scale;
            candidate[i][row*4+3]=m.translation[row];
        }
    }
    output=candidate;return true;
}

inline bool MaskRegions(const Profile& p,bool hideLower,unsigned regionCount,
    std::array<int16_t,RegionCapacity>& meshes) noexcept
{
    if(regionCount!=p.regions||regionCount>RegionCapacity||
       (hideLower&&!p.lowerRegions))return false;
    auto result=meshes;
    const uint16_t mask=p.headRegions|(hideLower?p.lowerRegions:0);
    for(unsigned i=0;i<regionCount;++i)if(mask&(1u<<i))result[i]=-1;
    meshes=result;return true;
}
}
