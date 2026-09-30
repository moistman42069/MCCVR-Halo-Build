// Exercise the production analytic elbow solver with the exact H4EK Storm
// link lengths and both anatomical poles. The runtime still admits only a
// verified live Storm palette before applying this geometry.
#include "../src/common/halo4_render_logic.h"
#include "../src/dll/ik.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace
{
int checks=0;
void Check(bool value,const char* name)
{
    ++checks;
    if(!value)
    {
        std::fprintf(stderr,"FAIL: %s\n",name);
        std::exit(1);
    }
}
float Distance(const float* a,const float* b)
{
    const float x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];
    return std::sqrt(x*x+y*y+z*z);
}
void SolveSide(bool left,float scale,bool swapTargets)
{
    constexpr float upper=.0915251f,lower=.116662f;
    const float shoulder[3]{0.f,left?.07045f:-.07045f,0.f};
    const float targetRight[3]{.12f,-.12f,.03f};
    const float targetLeft[3]{-.08f,.15f,.11f};
    const float* target=swapTargets?(left?targetRight:targetLeft):
                                    (left?targetLeft:targetRight);
    float scaledTarget[3]{target[0]*scale,target[1]*scale,target[2]*scale};
    const float scaledShoulder[3]{shoulder[0]*scale,shoulder[1]*scale,
                                  shoulder[2]*scale};
    const float poleRight[3]{0.f,-.6f,-.8f};
    const float poleLeft[3]{0.f,.6f,-.8f};
    const float* pole=left?poleLeft:poleRight;
    Halo4ArmReachPlan reach{};
    const float span=Distance(scaledShoulder,scaledTarget);
    Check(Halo4PlanArmReach(upper*scale,lower*scale,span,reach),
          "H4EK arm links produce a bounded reach plan");
    float elbow[3]{};
    Check(IK_SolveTwoBone(scaledShoulder,scaledTarget,reach.upperLength,
                          reach.lowerLength,pole,elbow),
          "finite controller target produces a title-pole elbow");
    Check(std::fabs(Distance(scaledShoulder,elbow)-reach.upperLength)<.0002f&&
          std::fabs(Distance(elbow,scaledTarget)-reach.lowerLength)<.0002f,
          "both authored links land on the solved controller target");
    for(float v:elbow) Check(std::isfinite(v),"solved elbow remains finite");
}
struct FixtureBone
{
    float marker[4]{};
};
void ProductionTransactionFixture()
{
    FixtureBone source[4]{};
    for(int i=0;i<4;++i) source[i].marker[0]=float(i+10);
    FixtureBone palette[4]{};
    std::memcpy(palette,source,sizeof(source));
    int rightCalls=0,leftCalls=0,rollbackCalls=0,carryCalls=0;
    const auto result=Halo4RunArmPaletteTransaction(
        true,
        [&](){ ++rightCalls; palette[0].marker[0]=101; return true; },
        [&](){ ++leftCalls; palette[1].marker[0]=202; return false; },
        [&](){ ++rollbackCalls; std::memcpy(palette,source,sizeof(source)); return true; },
        [&](){
            ++carryCalls;
            Check(std::memcmp(palette,source,sizeof(source))==0,
                  "partial H4 arm edits are rolled back before hand fallback");
            palette[2].marker[0]=303; // right tracked hand
            palette[3].marker[0]=404; // held weapon relation
            return true;
        });
    Check(result==Halo4ArmTransactionResult::LeftSolveFailed&&
          rightCalls==1&&leftCalls==1&&rollbackCalls==1&&carryCalls==1,
          "runtime arm transaction restores then uses rigid hand fallback");
    Check(palette[0].marker[0]==source[0].marker[0]&&
          palette[1].marker[0]==source[1].marker[0]&&
          palette[2].marker[0]==303&&palette[3].marker[0]==404,
          "fallback retains controller hand and weapon records only");

    std::memcpy(palette,source,sizeof(source));
    const auto committed=Halo4RunArmPaletteTransaction(
        true,
        [&](){ palette[0].marker[0]=11; return true; },
        [&](){ palette[1].marker[0]=12; return true; },
        [&](){ std::memcpy(palette,source,sizeof(source)); return true; },
        [&](){ return false; });
    Check(committed==Halo4ArmTransactionResult::Committed&&
          palette[0].marker[0]==11&&palette[1].marker[0]==12,
          "only two successful arm solves commit visible-arm palette");

    Halo4FloatingTransform stockWrist{},desiredWrist{},stockGun{},delta{},movedGun{};
    stockWrist.translation[0]=.2f; stockWrist.translation[1]=-.1f;
    desiredWrist.translation[0]=-.3f; desiredWrist.translation[1]=.4f;
    stockGun.translation[0]=.24f; stockGun.translation[1]=-.08f;
    stockGun.translation[2]=.03f;
    Check(Halo4BuildFloatingWorldDelta(desiredWrist,stockWrist,delta)&&
          Halo4ComposeFloatingTransforms(delta,stockGun,movedGun),
          "production wrist delta carries held weapon in fallback or arm mode");
    const float expected[3]{.24f-.5f,.42f,.03f};
    for(int i=0;i<3;++i)
        Check(std::fabs(movedGun.translation[i]-expected[i])<1e-5f,
              "held weapon preserves stock wrist-relative offset");
}
void LiveLengthScaleFixture()
{
    // E-H4-23 measured live upper links around 0.212, over twice the official
    // 0.0915251 bind. The runtime solver uses these current palette distances,
    // so this old contradiction must not be normalized back to tag bind units.
    constexpr float upper=.212f,lower=.235f;
    const float shoulder[3]{0.f,-.0552f,0.f};
    const float target[3]{.18f,-.11f,.06f};
    const float pole[3]{0.f,-.6f,-.8f};
    Halo4ArmReachPlan plan{};
    Check(Halo4PlanArmReach(upper,lower,Distance(shoulder,target),plan),
          "live palette link lengths remain solver inputs despite bind ratio");
    float elbow[3]{};
    Check(IK_SolveTwoBone(shoulder,target,plan.upperLength,
                          plan.lowerLength,pole,elbow),
          "live-length elbow solution accepts measured 2.3x bind upper link");
    Check(std::fabs(Distance(shoulder,elbow)-upper)<.0002f&&
          std::fabs(Distance(elbow,target)-lower)<.0002f,
          "live-length solve preserves both actual animated segment lengths");
}
}

int main()
{
    Check(Halo4IsStormMasterchiefBodyIdentity(
              kHalo4StormMasterchiefBodyRuntimeImportChecksum,
              kHalo4StormMasterchiefBodyNodeCount),
          "official Storm Master Chief body identity matches exact checksum and count");
    Check(!Halo4IsStormMasterchiefBodyIdentity(
              kHalo4StormMasterchiefBodyRuntimeImportChecksum, 80) &&
          !Halo4IsStormMasterchiefBodyIdentity(0x150D0000u, 120) &&
          !Halo4IsStormMasterchiefBodyIdentity(0, 120),
          "hands, other models, and unresolved identity cannot masquerade as native body");
    Check(Halo4BodyBelongsToFrozenLocalUnit(true, 0x1234, true, 0x1234),
          "exact native body object matches frozen local unit");
    Check(!Halo4BodyBelongsToFrozenLocalUnit(true, 0x1235, true, 0x1234) &&
          !Halo4BodyBelongsToFrozenLocalUnit(false, 0x1234, true, 0x1234) &&
          !Halo4BodyBelongsToFrozenLocalUnit(true, UINT32_MAX, true, 0x1234) &&
          !Halo4BodyBelongsToFrozenLocalUnit(true, 0x1234, false, 0x1234) &&
          !Halo4BodyBelongsToFrozenLocalUnit(true, 0x1234, true, UINT32_MAX),
          "unmatched, unknown, or invalid body/local handles fail closed");
    Check(kHalo4RightShoulderNode==4&&kHalo4RightElbowNode==16&&
          kHalo4RightHandNode==29&&kHalo4LeftShoulderNode==5&&
          kHalo4LeftElbowNode==8&&kHalo4LeftHandNode==37,
          "solver node indices match H4EK's verified Storm arm chains");
    for(float scale:{.5f,1.f,2.f})
        for(bool swapped:{false,true})
        {
            SolveSide(false,scale,swapped);
            SolveSide(true,scale,swapped);
        }
    Check(Halo4VisibleArmSolveCanCommit(true,true,true)&&
          !Halo4VisibleArmSolveCanCommit(true,false,true)&&
          !Halo4VisibleArmSolveCanCommit(true,true,false)&&
          !Halo4VisibleArmSolveCanCommit(false,true,true),
          "both hand chains must solve before visible arms commit");
    Check(Halo4ShouldSolveVisibleArms(true,true,true,true)&&
          !Halo4ShouldSolveVisibleArms(false,true,true,true)&&
          !Halo4ShouldSolveVisibleArms(true,false,true,true)&&
          !Halo4ShouldSolveVisibleArms(true,true,false,true)&&
          !Halo4ShouldSolveVisibleArms(true,true,true,false),
          "fixed one-hand or two-hand wrist targets can drive visible-arm IK only when all prerequisites pass");
    ProductionTransactionFixture();
    LiveLengthScaleFixture();
    std::printf("PASS: %d Halo 4 tracked-arm IK checks\n",checks);
    return 0;
}
