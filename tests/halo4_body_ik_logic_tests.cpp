#include "../src/common/halo4_body_ik_logic.h"
#include "../src/common/halo4_fp_finger_pose_logic.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
int checks=0;
void Check(bool ok,const char* label)
{
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}
}
float Distance(const float a[3],const float b[3])
{
    const float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return std::sqrt(x*x+y*y+z*z);
}
void SetPoint(Halo4FloatingTransform (&nodes)[120],unsigned i,float x,float y,float z)
{nodes[i].translation[0]=x;nodes[i].translation[1]=y;nodes[i].translation[2]=z;}
}
int main()
{
    const uint32_t nativeMask=(1u<<1)|(1u<<6)|(1u<<31);
    const uint32_t upperOnly=Halo4BuildBodyIkRegionMask(nativeMask,false);
    Check((upperOnly&kHalo4BodyHideHeadRegions)==kHalo4BodyHideHeadRegions&&
          (upperOnly&kHalo4BodyShowUpperRegions)==0&&
          (upperOnly&nativeMask)==(nativeMask&~kHalo4BodyShowUpperRegions),
          "body visibility mask admits verified upper regions while preserving unrelated native bits");
    const uint32_t upperAndHiddenLegs=Halo4BuildBodyIkRegionMask(nativeMask,true);
    Check((upperAndHiddenLegs&kHalo4BodyHideLowerRegions)==kHalo4BodyHideLowerRegions&&
          (upperAndHiddenLegs&kHalo4BodyShowUpperRegions)==0,
          "optional lower-body visibility is independent of torso and arms");
    Halo4FloatingTransform source[120]{};
    for(auto& n:source)n.scale=1.f;
    SetPoint(source,34,0,0,0);SetPoint(source,58,.10f,0,0);SetPoint(source,73,.10f,.12f,0);
    SetPoint(source,38,.5f,0,0);SetPoint(source,50,.60f,0,0);SetPoint(source,68,.60f,-.12f,0);
    SetPoint(source,94,.10f,.15f,0); // authored left index descendant
    SetPoint(source,81,.63f,-.12f,0); // authored right index descendant
    SetPoint(source,107,.66f,-.12f,0);SetPoint(source,116,.69f,-.12f,0);
    SetPoint(source,5,0,.2f,-.3f);SetPoint(source,10,0,.2f,-.45f);SetPoint(source,18,0,.2f,-.6f);
    SetPoint(source,6,.5f,.2f,-.3f);SetPoint(source,9,.5f,.2f,-.45f);SetPoint(source,22,.52f,.2f,-.6f);
    Halo4BodyIkRequest request{};
    request.wristValid[0]=request.wristValid[1]=true;
    request.desiredWorldWrist[0].translation[0]=.14f;
    request.desiredWorldWrist[0].translation[1]=.08f;
    request.desiredWorldWrist[0].translation[2]=.03f;
    request.desiredWorldWrist[1].translation[0]=.64f;
    request.desiredWorldWrist[1].translation[1]=-.05f;
    request.desiredWorldWrist[1].translation[2]=-.03f;
    request.desiredWorldWrist[1].rotation[0]=0.f;
    request.desiredWorldWrist[1].rotation[1]=1.f;
    request.desiredWorldWrist[1].rotation[3]=-1.f;
    request.desiredWorldWrist[1].rotation[4]=0.f;
    request.elbowPoleWorld[0][2]=1.f;
    request.elbowPoleWorld[1][2]=1.f;
    Halo4BodyIkResult result{};
    Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,request,result),
          "exact checksum/count supports both arm transactions");
    Check(Distance(result.transforms[68].translation,request.desiredWorldWrist[1].translation)<1e-5f&&
          Distance(result.transforms[73].translation,request.desiredWorldWrist[0].translation)<1e-5f,
          "controller wrist positions are exact");
    Check(std::fabs(Distance(result.transforms[38].translation,result.transforms[50].translation)-.1f)<2e-4f&&
          std::fabs(Distance(result.transforms[50].translation,result.transforms[68].translation)-.12f)<2e-4f&&
          std::fabs(Distance(result.transforms[34].translation,result.transforms[58].translation)-.1f)<2e-4f&&
          std::fabs(Distance(result.transforms[58].translation,result.transforms[73].translation)-.12f)<2e-4f,
          "both solved chains preserve animated segment lengths");
    Check(Distance(result.transforms[94].translation,result.transforms[73].translation)>0.01f&&
          Distance(result.transforms[81].translation,result.transforms[68].translation)>0.01f,
          "authored finger descendants remain attached under wrist rotation");
    Check(std::fabs(result.transforms[68].rotation[0]-request.desiredWorldWrist[1].rotation[0])<1e-4f&&
          std::fabs(result.transforms[68].rotation[1]-request.desiredWorldWrist[1].rotation[1])<1e-4f&&
          std::fabs(result.transforms[68].rotation[3]-request.desiredWorldWrist[1].rotation[3])<1e-4f&&
          std::fabs(result.transforms[68].rotation[4]-request.desiredWorldWrist[1].rotation[4])<1e-4f,
          "primary wrist basis follows controller orientation");
    Check(std::fabs(result.transforms[81].translation[0]-result.transforms[68].translation[0])<2e-4f&&
          std::fabs((result.transforms[81].translation[1]-result.transforms[68].translation[1])-.03f)<2e-4f,
          "wrist orientation carries the authored finger subtree rigidly");
    Check(std::memcmp(&result.transforms[3],&source[3],sizeof(source[3]))==0&&
          std::memcmp(&result.transforms[24],&source[24],sizeof(source[24]))==0,
          "unsolved torso and head transforms remain byte-identical");

    Halo4BodyIkPacketMutation packet{};
    const uint32_t bodyNativeMask=1u<<1,stormNativeMask=1u<<2;
    Check(Halo4BuildBodyIkPacketMutation(kHalo4BodyIkImportChecksum,120,
              source,request,bodyNativeMask,stormNativeMask,false,packet)&&
          std::memcmp(packet.body.transforms,result.transforms,
              sizeof(result.transforms))==0&&
          (packet.bodyRegionMask&kHalo4BodyShowUpperRegions)==0&&
          (packet.bodyRegionMask&kHalo4BodyHideHeadRegions)==
              kHalo4BodyHideHeadRegions&&
          packet.duplicateHandsRegionMask==UINT32_MAX,
          "production packet planner commits the solved body, camera-safe regions, and hides only duplicate hands after solve");
    Halo4BodyIkPacketMutation untouchedPacket{};
    std::memset(&untouchedPacket,0xA5,sizeof(untouchedPacket));
    const auto untouchedPacketBefore=untouchedPacket;
    Halo4BodyIkRequest packetUnreachable=request;
    packetUnreachable.desiredWorldWrist[0].translation[0]=4.f;
    Check(!Halo4BuildBodyIkPacketMutation(kHalo4BodyIkImportChecksum,120,
              source,packetUnreachable,bodyNativeMask,stormNativeMask,true,
              untouchedPacket)&&
          std::memcmp(&untouchedPacket,&untouchedPacketBefore,
              sizeof(untouchedPacket))==0,
          "failed packet planner preserves both native masks and the output transaction");

    Halo4BodyIkResult untouched{};std::memset(&untouched,0xA5,sizeof(untouched));
    Halo4BodyIkResult before=untouched;
    Check(!Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,80,source,request,untouched)&&
          std::memcmp(&untouched,&before,sizeof(before))==0,
          "80-node first-person graph is rejected without output mutation");
    Halo4BodyIkRequest unreachable=request;
    unreachable.desiredWorldWrist[0].translation[0]=4.f;
    Check(!Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,unreachable,untouched)&&
          std::memcmp(&untouched,&before,sizeof(before))==0,
          "unreachable controller target fails the whole transaction open");
    Halo4BodyIkRequest malformed=request;malformed.elbowPoleWorld[1][0]=NAN;
    Check(!Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,malformed,untouched)&&
          std::memcmp(&untouched,&before,sizeof(before))==0,
          "nonfinite input fails without partial-pose publication");

    Halo4BodyIkRequest follow{};
    follow.bodyRootDeltaValid=true;follow.bodyRootDelta.translation[0]=.2f;
    follow.headTargetValid=true;follow.desiredWorldHead.translation[0]=.3f;
    Halo4BodyIkResult followed{};
    Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,follow,followed)&&
          std::fabs(followed.transforms[54].translation[0]-.3f)<1e-5f&&
          std::fabs(followed.transforms[3].translation[0]-.3f)<1e-5f,
          "root and HMD follow apply one coherent body-space translation");

    Halo4BodyIkRequest feet{};
    feet.footValid[0]=true;feet.desiredWorldFoot[0].translation[0]=0.f;
    feet.desiredWorldFoot[0].translation[1]=.2f;feet.desiredWorldFoot[0].translation[2]=-.55f;
    feet.kneePoleWorld[0][1]=1.f;
    Halo4BodyIkResult legged{};
    Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,feet,legged)&&
          Distance(legged.transforms[18].translation,feet.desiredWorldFoot[0].translation)<1e-5f&&
          std::fabs(Distance(legged.transforms[5].translation,legged.transforms[10].translation)-.15f)<2e-4f&&
          std::fabs(Distance(legged.transforms[10].translation,legged.transforms[18].translation)-.15f)<2e-4f,
          "verified thigh-calf-foot graph follows a valid foot target");

    Halo4BodyIkRequest fingers{};fingers.fingerPoseAllowed[1]=true;
    fingers.fingerInput[1]=ControllerFingerInput{true,1.f,0.f};
    Halo4BodyIkResult fingerPose{};
    Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,fingers,fingerPose)&&
          Distance(fingerPose.transforms[116].translation,source[116].translation)>1e-3f,
          "physical trigger curls unheld index finger from its live chain geometry");
    {
        Halo4BodyIkResult open{},point{},repeat{};
        fingers.fingerInput[1]={true,0.f,0.f};
        Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,fingerPose.transforms,fingers,open),
              "releasing input rebuilds the free fingers from their own authored bind pose");
        fingers.fingerInput[1]={true,0.f,1.f};
        Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,fingers,point)&&
              Distance(point.transforms[116].translation,open.transforms[116].translation)<1e-6f&&
              Distance(point.transforms[112].translation,open.transforms[112].translation)>1e-4f,
              "grip with trigger released keeps index open for pointing while other fingers curl");
        Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,point.transforms,fingers,repeat)&&
              Distance(point.transforms[112].translation,repeat.transforms[112].translation)<1e-6f,
              "repeat application does not accumulate free-finger curl");
    }
    fingers.fingerPoseAllowed[1]=false;
    Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,fingers,fingerPose)&&
          std::memcmp(&fingerPose.transforms[81],&source[81],sizeof(source[81]))==0&&
          std::memcmp(&fingerPose.transforms[107],&source[107],sizeof(source[107]))==0,
          "authored held-weapon grip remains untouched when finger posing is disabled");
    fingers.fingerPoseAllowed[1]=true;fingers.fingerInput[1].trigger=NAN;
    Check(!Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,fingers,untouched)&&
          std::memcmp(&untouched,&before,sizeof(before))==0,
          "malformed physical finger sample cannot partially publish a body pose");
    fingers.fingerInput[1]={};
    Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,fingers,fingerPose)&&
          std::memcmp(&fingerPose.transforms[81],&source[81],sizeof(source[81]))==0,
          "missing finger sample preserves the complete authored hand pose");
    {
        Halo4FloatingTransform hmd{},head{},marker{},located{};
        hmd.translation[0]=.2f;hmd.translation[2]=1.f;
        const float hp[3]{-1.47467e-6f,.0336001f,-.0322376f};
        const float hq[4]{-.492315f,.507569f,.507569f,.492315f};
        Check(Halo4BodyHeadFromHmd(hmd,head)&&BuildAnatomicalPalmMarker(hp,hq,marker)&&
              Halo4ComposeFloatingTransforms(head,marker,located)&&
              Distance(located.translation,hmd.translation)<1e-5f,
              "own semantic head marker, not head bone origin, aligns with HMD");
        for(int i=0;i<9;++i)Check(std::fabs(located.rotation[i]-hmd.rotation[i])<1e-5f,
            "head marker orientation follows the HMD in the own rig frame");
        Halo4FloatingTransform fp[2]{},body[2]{};
        fp[0].translation[0]=.14f;fp[1].translation[1]=-.08f;
        Check(Halo4BodyWristsFromFirstPerson(fp,body),"own FP and world wrist markers map successfully");
        AnatomicalPalmMarkers markers{};Halo4AnatomicalPalmMarkers(markers);
        const float p[2][3]{{.00890922f,-.0127749f,.000334086f},{-.00890919f,.0127749f,-.000334197f}};
        const float q[2][4]{{.404408f,.357829f,.639067f,.547728f},{-.130058f,-.173354f,.711616f,.668309f}};
        for(unsigned side=0;side<2;++side) {
            Halo4FloatingTransform marker{},actual{},expected{};
            BuildAnatomicalPalmMarker(p[side],q[side],marker);
            Halo4ComposeFloatingTransforms(body[side],marker,actual);
            Halo4ComposeFloatingTransforms(fp[side],side?markers.right:markers.left,expected);
            Check(Distance(actual.translation,expected.translation)<1e-5f,"world palm coincides with the visible FP palm");
            for(int i=0;i<9;++i)Check(std::fabs(actual.rotation[i]-expected.rotation[i])<1e-5f,
                "world palm keeps the visible FP grip orientation");
        }
        const auto saved=body[0];fp[1].translation[2]=NAN;
        Check(!Halo4BodyWristsFromFirstPerson(fp,body)&&std::memcmp(&saved,&body[0],sizeof(saved))==0,
            "invalid second wrist cannot partially replace the world-hand targets");
    }
    {
        // Both anatomical arms admit a reachable target even when their
        // native elbow direction is exactly parallel/antiparallel to it.
        for(unsigned side=0;side<2;++side)for(int sign=-1;sign<=1;++sign) {
            const unsigned upper=side?38:34,fore=side?50:58,wrist=side?68:73;
            Halo4BodyIkRequest straight{};straight.wristValid[side]=true;
            straight.desiredWorldWrist[side]=source[upper];
            straight.desiredWorldWrist[side].translation[0]+=.18f;
            straight.elbowPoleWorld[side][0]=float(sign);
            Halo4BodyIkResult rescued{};
            Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,straight,rescued)&&
                Distance(rescued.transforms[wrist].translation,straight.desiredWorldWrist[side].translation)<1e-5f,
                "collinear or zero native elbow pole retains exact tracked wrist");
            Check(std::fabs(Distance(rescued.transforms[upper].translation,rescued.transforms[fore].translation)-.1f)<2e-4f&&
                std::fabs(Distance(rescued.transforms[fore].translation,rescued.transforms[wrist].translation)-.12f)<2e-4f,
                "basis fallback preserves both animated arm lengths");
            Check(std::fabs(rescued.transforms[fore].translation[1]-source[upper].translation[1])>.01f,
                "basis fallback supplies a finite noncollapsed elbow bend");
        }
        Halo4FloatingTransform turned[120];std::memcpy(turned,source,sizeof(turned));
        const float quarterTurn[9]{0,1,0,-1,0,0,0,0,1};
        for(auto& node:turned) {
            const float x=node.translation[0];node.translation[0]=-node.translation[1];node.translation[1]=x;
            std::memcpy(node.rotation,quarterTurn,sizeof(quarterTurn));
        }
        Halo4BodyIkRequest turnedRequest{};turnedRequest.wristValid[1]=true;
        turnedRequest.desiredWorldWrist[1]=turned[38];turnedRequest.desiredWorldWrist[1].translation[1]+=.18f;
        turnedRequest.elbowPoleWorld[1][1]=1;
        Halo4BodyIkResult turnedResult{};
        Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,turned,turnedRequest,turnedResult)&&
            turnedResult.transforms[50].translation[0]<-.01f&&
            Distance(turnedResult.transforms[68].translation,turnedRequest.desiredWorldWrist[1].translation)<1e-5f,
            "singular-pole fallback turns with the native arm basis instead of a fixed world axis");
        Halo4BodyIkRequest extended{};extended.wristValid[1]=true;
        extended.desiredWorldWrist[1]=source[38];extended.desiredWorldWrist[1].translation[0]+=.30f;
        extended.elbowPoleWorld[1][2]=-1;
        Halo4BodyIkResult stretched{};
        Check(Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,extended,stretched)&&
            Distance(stretched.transforms[68].translation,extended.desiredWorldWrist[1].translation)<1e-4f,
            "moderate tracked reach beyond authored arm span stays attached");
        Check(std::fabs(Distance(stretched.transforms[38].translation,stretched.transforms[50].translation)/.1f-
            Distance(stretched.transforms[50].translation,stretched.transforms[68].translation)/.12f)<1e-3f,
            "reach extension is distributed across both arm segments");
    }
    {
        Halo4BodyIkRequest crouch{};crouch.headTargetValid=true;
        crouch.desiredWorldHead=source[54];crouch.desiredWorldHead.translation[2]-=.02f;
        Halo4BodyIkResult planted{};
        Check(Halo4PrepareBodyFootTargets(source,crouch)&&
            Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,crouch,planted),
            "HMD crouch prepares both native-foot targets and solves the body");
        Check(Distance(planted.transforms[18].translation,source[18].translation)<1e-4f&&
            Distance(planted.transforms[22].translation,source[22].translation)<1e-4f,
            "reachable native feet stay planted when the torso follows the headset");
        crouch.desiredWorldHead.translation[0]+=1.f;
        Check(Halo4PrepareBodyFootTargets(source,crouch)&&
            Halo4SolveBodyIkTransforms(kHalo4BodyIkImportChecksum,120,source,crouch,planted),
            "beyond leg reach the feet follow within anatomical reach without losing the avatar");
    }
    {
        Halo4FloatingTransform fp[80]{},open[80]{},point[80]{},fist[80]{},repeat[80]{};
        for(unsigned side=0;side<2;++side) {
            Check(Halo4PoseFreeFpFingers(fp,kHalo4StormFpRuntimeImportChecksum,80,side,{true,0,0},open),
                "H4 own FP profile opens a free hand from its authored bind pose");
            Check(Halo4PoseFreeFpFingers(fp,kHalo4StormFpRuntimeImportChecksum,80,side,{true,0,1},point),
                "H4 free FP hand can point");
            const unsigned index=side?76:75,middle=side?72:69;
            Check(Distance(point[index].translation,open[index].translation)<1e-6f &&
                  Distance(point[middle].translation,open[middle].translation)>1e-4f,
                "FP pointing keeps index open independently of gripped fingers");
            Check(Halo4PoseFreeFpFingers(fp,kHalo4StormFpRuntimeImportChecksum,80,side,{true,1,1},fist) &&
                  Distance(fist[index].translation,point[index].translation)>1e-4f,
                "FP trigger closes the independent index");
            Check(Halo4PoseFreeFpFingers(fist,kHalo4StormFpRuntimeImportChecksum,80,side,{true,1,1},repeat) &&
                  std::memcmp(fist,repeat,sizeof(fist))==0,
                "FP repeated render passes cannot accumulate finger curl");
            const unsigned wrist=side?29:37;
            for(unsigned node=0;node<80;++node)
                if(node==wrist||!controller_finger_pose::IsDescendant(kHalo4FpFingerParents,80,node,wrist))
                    Check(std::memcmp(&fp[node],&fist[node],sizeof(fp[node]))==0,
                        "free FP fingers preserve wrists, arms and opposite held hand");
        }
        const auto beforeFp=point[0];
        Check(!Halo4PoseFreeFpFingers(fp,kHalo4BodyIkImportChecksum,80,0,{true,1,1},point)&&
              std::memcmp(&point[0],&beforeFp,sizeof(beforeFp))==0,
            "world body checksum cannot be used as the FP finger rig");
        Check(!Halo4PoseFreeFpFingers(fp,kHalo4StormFpRuntimeImportChecksum,80,0,{true,NAN,1},point),
            "bad FP physical sample refuses the optional pose");
    }
    std::printf("PASS: %d Halo 4 full-body transform IK checks\n",checks);
    return 0;
}
