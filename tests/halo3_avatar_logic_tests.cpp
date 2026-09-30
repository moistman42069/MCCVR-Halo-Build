#include "../src/common/halo3_avatar_logic.h"
#include <cstdio>
#include <limits>

namespace
{
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL: %s\n",why);}}
bool Near(const halo3_avatar::Matrix& a,const halo3_avatar::Matrix& b,float tolerance=3e-4f)
{
    const float* x=&a.scale;const float* y=&b.scale;
    for(unsigned i=0;i<13;++i)if(!std::isfinite(x[i])||std::fabs(x[i]-y[i])>tolerance)return false;
    return true;
}
halo3_avatar::Pose Authored(const halo3_avatar::Profile& p)
{
    halo3_avatar::Pose pose{};
    for(unsigned i=0;i<p.count;++i)
    {
        if(p.parents[i]>=0)pose[i]=pose[unsigned(p.parents[i])];
        pose[i].translation[0]+=.018f;pose[i].translation[2]+=.013f;
    }
    for(unsigned side=0;side<2;++side)
    {
        const float sign=side? -1.f:1.f;
        auto& shoulder=pose[p.shoulder[side]];
        shoulder.translation[0]=0;shoulder.translation[1]=sign*.14f;shoulder.translation[2]=.5f;
        auto& elbow=pose[p.elbow[side]];elbow=shoulder;elbow.translation[0]=.10f;elbow.translation[2]=.40f;
        auto& wrist=pose[p.wrist[side]];wrist=elbow;wrist.translation[0]=.23f;wrist.translation[2]=.45f;
        for(unsigned i=0;i<p.count;++i)if(halo3_avatar::Descendant(p,i,p.wrist[side]))
        {pose[i]=wrist;pose[i].translation[0]+=.003f*float(i);}
    }
    const unsigned human[2][3]{{1,4,7},{2,5,8}},elite[2][4]{{1,5,8,13},{3,6,9,14}};
    for(unsigned side=0;side<2;++side)
    {
        const unsigned* chain=p.count==51?human[side]:elite[side];const unsigned count=p.count==51?3:4;
        for(unsigned i=0;i<count;++i)
        {auto& m=pose[chain[i]];m.translation[0]=i==1?.03f:0;m.translation[1]=side?-.06f:.06f;m.translation[2]=.4f-.4f*float(i)/float(count-1);}
    }
    pose[p.head].translation[0]=0;pose[p.head].translation[1]=0;pose[p.head].translation[2]=.6f;
    return pose;
}
halo3_avatar::Request RequestFor(const halo3_avatar::Profile& p,const halo3_avatar::Pose& pose)
{
    halo3_avatar::Request request{};AnatomicalPalmMarkers fp{};
    Check(Halo3AnatomicalPalmMarkers(p.fpChecksum,int(p.fpCount),fp),"FP semantic markers");
    Check(Halo4ComposeFloatingTransforms(pose[p.head],p.headMarker,request.headWorld),"head semantic marker");
    for(unsigned side=0;side<2;++side)
    {
        halo3_avatar::Matrix palm{},inverse{};
        Check(Halo4ComposeFloatingTransforms(pose[p.wrist[side]],p.palm[side],palm),"world semantic palm");
        Check(Halo4InvertFloatingTransform(side?fp.right:fp.left,inverse),"inverse FP marker");
        Check(Halo4ComposeFloatingTransforms(palm,inverse,request.fpWristWorld[side]),"FP wrist targeting identical palm");
    }
    return request;
}
}
int main()
{
    using namespace halo3_avatar;
    const uint32_t checksums[]{0x17121B0D,0x10140D04,0x17180910};
    for(unsigned model=0;model<3;++model)
    {
        Profile p{};Check(SelectProfile(checksums[model],model?55:51,p),"own world identity");
        Profile untouched=p;Check(!SelectProfile(checksums[model],1,p)&&p.count==untouched.count,"bad count leaves profile unchanged");
        Pose source=Authored(p),result{};auto request=RequestFor(p,source);
        for(unsigned side=0;side<2;++side)
        {Matrix target{};Check(WorldWristTarget(p,side,request.fpWristWorld[side],target)&&Near(target,source[p.wrist[side]]),"world/FP marker roundtrip preserves authored wrist");}
        Check(Solve(p,source,request,result),"whole body identity solve");
        for(unsigned side=0;side<2;++side)Check(Near(result[p.wrist[side]],source[p.wrist[side]]),"both wrist endpoints exact");
        for(unsigned side=0;side<2;++side)
        {
            auto fingers=result;const auto original=fingers;
            Check(PoseFingers(p,fingers,side,{true,.8f,.5f}),"own graph controller finger posing");
            Check(Near(fingers[p.wrist[side]],original[p.wrist[side]]),"finger input cannot move the wrist");
            bool changed=false;
            for(unsigned node=0;node<p.count;++node)
            {
                if(!Descendant(p,node,p.wrist[side]))Check(Near(fingers[node],original[node]),"other hand and body remain authored");
                else if(node!=p.wrist[side]&&!Near(fingers[node],original[node]))changed=true;
            }
            Check(changed,"physical curl changes own finger descendants");
            const auto good=fingers;
            Check(!PoseFingers(p,fingers,side,{true,2,0})&&std::memcmp(&good,&fingers,sizeof(good))==0,"invalid finger input refuses atomically");
            Check(PoseFingers(p,fingers,side,{})&&std::memcmp(&good,&fingers,sizeof(good))==0,"unavailable controls keep authored fingers");
        }
        // Left-handed input with Fix Hand Alignment off keeps legacy anatomy.
        auto legacyLeftInput=result;
        Check(PoseFingers(p,legacyLeftInput,FingerAnatomicalSupportSide(false),{true,.6f,.5f}),"left-handed input without anatomical swap poses displayed support");
        for(unsigned node=0;node<p.count;++node)if(Descendant(p,node,p.wrist[1]))
            Check(Near(legacyLeftInput[node],result[node]),"legacy primary anatomy is preserved despite left-handed input");
        Check(FingerAnatomicalSupportSide(true)==1,"completed anatomical swap selects opposite displayed support");
        Pose bindInverse{};
        for(unsigned node=0;node<p.count;++node)
            Check(Halo4InvertFloatingTransform(source[node],bindInverse[node]),"own world pose inverse bind fixture");
        for(unsigned side=0;side<2;++side)
        {
            auto closed=source;
            Check(PoseFingersFromBind(p,closed,side,bindInverse,{true,1,1}),"free world fist starts from own bind");
            auto repeated=closed;
            Check(PoseFingersFromBind(p,repeated,side,bindInverse,{true,1,1})&&std::memcmp(&repeated,&closed,sizeof(closed))==0,"world finger curl does not accumulate");
            Check(PoseFingersFromBind(p,closed,side,bindInverse,{true,0,0}),"released world hand reopens from prior authored grip");
            for(unsigned node=0;node<p.count;++node)Check(Near(closed[node],source[node]),"zero curl restores default finger shape and preserves rest of world body");
            auto pointing=source;
            Check(PoseFingersFromBind(p,pointing,side,bindInverse,{true,0,1}),"world index point independent of grip");
            const unsigned index=p.count==51?(side?26:21):(side?43:39);
            Check(Near(pointing[index],source[index]),"world pointing index stays open while gripping");
        }
        const Pose saved=source;
        request.headWorld.translation[2]-=.03f;
        for(auto& wrist:request.fpWristWorld)wrist.translation[2]-=.03f;
        Check(Solve(p,source,request,result),"physical crouch shifts tracked torso with native feet pinned");
        Check(std::memcmp(&saved,&source,sizeof(source))==0,"source animation remains immutable");
        for(unsigned side=0;side<2;++side)
        {
            const unsigned foot=p.count==51?(side?8:7):(side?14:13);
            Check(Near(result[foot],source[foot]),"native animated foot reference retained");
            Matrix target{};Check(WorldWristTarget(p,side,request.fpWristWorld[side],target)&&Near(result[p.wrist[side]],target),"crouch retains semantic palm endpoints");
        }
        auto before=result;request.headWorld.translation[0]=std::numeric_limits<float>::quiet_NaN();
        Check(!Solve(p,source,request,result)&&std::memcmp(&before,&result,sizeof(result))==0,"late invalid pose leaves complete output untouched");
        std::array<int16_t,RegionCapacity> regions{};for(unsigned i=0;i<regions.size();++i)regions[i]=int16_t(i);
        Check(MaskRegions(p,false,p.regions,regions),"authored head regions mask");
        for(unsigned i=0;i<p.regions;++i)Check(regions[i]==((p.headRegions&(1u<<i))?-1:int16_t(i)),"only authored head region removed");
        auto prior=regions;const bool lower=MaskRegions(p,true,p.regions,regions);
        Check(model==0?lower:(!lower&&regions==prior),"no invented Elite/Dervish lower-body mask");
        Pose inverse{};GpuPose gpu{};Check(BuildGpuPose(p,source,inverse,gpu),"world GPU conversion");
        for(unsigned i=0;i<p.count;++i)for(unsigned row=0;row<3;++row)
        {
            Check(gpu[i][row*4+3]==source[i].translation[row],"GPU world translation independent of eye");
            for(unsigned col=0;col<3;++col)Check(gpu[i][row*4+col]==source[i].rotation[col*3+row]*source[i].scale,"GPU transposition and scale");
        }
        const auto good=gpu;inverse[p.count-1].scale=std::numeric_limits<float>::infinity();
        Check(!BuildGpuPose(p,source,inverse,gpu)&&gpu==good,"late inverse-bind fault is atomic");
    }
    std::printf("H3 avatar logic: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
