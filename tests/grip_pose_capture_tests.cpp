#include <Windows.h>
#include <openxr/openxr.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "support_grab_logic.h"
#include "virtual_stock_logic.h"

namespace
{
unsigned checks{}, failures{};
unsigned stateCalls{}, locateCalls{};
XrResult stateResult=XR_SUCCESS, locateResult=XR_SUCCESS;
XrBool32 stateActive=XR_TRUE;
XrSpaceLocationFlags locateFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT;
XrVector3f locatePosition{1.0f, 2.0f, 3.0f};
XrAction lastStateAction=XR_NULL_HANDLE;
XrPath lastStateSubaction=XR_NULL_PATH;
XrSpace lastLocateSpace=XR_NULL_HANDLE, lastLocateBase=XR_NULL_HANDLE;
XrTime lastLocateTime=0;
XrAction g_supportGripPoseAction=reinterpret_cast<XrAction>(uintptr_t{11});
XrSession g_session=reinterpret_cast<XrSession>(uintptr_t{12});
XrSpace g_localSpace=reinterpret_cast<XrSpace>(uintptr_t{13});

void Check(bool value,const char* message)
{
    ++checks;
    if (!value)
    {
        ++failures;
        std::fprintf(stderr,"FAIL: %s\n",message);
    }
}

bool SamePosition(const XrVector3f& value, const XrVector3f& expected)
{
    return value.x==expected.x && value.y==expected.y && value.z==expected.z;
}

void Reset()
{
    stateCalls=locateCalls=0;
    stateResult=locateResult=XR_SUCCESS;
    stateActive=XR_TRUE;
    locateFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT;
    locatePosition={1.0f,2.0f,3.0f};
    lastStateAction=XR_NULL_HANDLE;
    lastStateSubaction=XR_NULL_PATH;
    lastLocateSpace=lastLocateBase=XR_NULL_HANDLE;
    lastLocateTime=0;
}

void CheckFailurePreservesOutput(const char* label, bool result,
    const XrVector3f& output, const XrVector3f& sentinel)
{
    Check(!result,label);
    Check(SamePosition(output,sentinel),"Failed grip capture preserves the output sentinel");
}
}

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(
    XrSession, const XrActionStateGetInfo* info, XrActionStatePose* state)
{
    ++stateCalls;
    lastStateAction=info->action;
    lastStateSubaction=info->subactionPath;
    state->isActive=stateActive;
    return stateResult;
}

extern "C" XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(
    XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* location)
{
    ++locateCalls;
    lastLocateSpace=space;
    lastLocateBase=base;
    lastLocateTime=time;
    location->locationFlags=locateFlags;
    location->pose.position=locatePosition;
    return locateResult;
}

namespace
{
// The fixture's acquisition helpers read the shipping config members the same
// way the shipping call sites do. The real Config lives in the product; this
// TU stands in the two values they consume, exactly like the OpenXR globals
// above are stood in, so the fixture needs no engine link.
struct AcquisitionTestConfig
{
    float two_hand_zone_right_m = 0.0f;
    float left_grip_forward_m = 0.097f;
};
AcquisitionTestConfig g_config;

// Exact copy of the shipping quaternion rotate (vr.cpp) so the extracted
// acquisition helpers run against the same geometry they run against in game.
XrVector3f Rotate(const XrQuaternionf& q, const XrVector3f& v)
{
    const XrVector3f u{q.x,q.y,q.z};
    auto cross=[](const XrVector3f& a,const XrVector3f& b){
        return XrVector3f{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    };
    XrVector3f c1=cross(u,v);
    c1.x+=q.w*v.x; c1.y+=q.w*v.y; c1.z+=q.w*v.z;
    const XrVector3f c2=cross(u,c1);
    return {v.x+2*c2.x,v.y+2*c2.y,v.z+2*c2.z};
}

XrQuaternionf AxisAngle(const XrVector3f& axis, float degrees)
{
    const double radians=
        static_cast<double>(degrees)*3.14159265358979323846/180.0;
    const float halfSine=static_cast<float>(std::sin(radians*0.5));
    return XrQuaternionf{axis.x*halfSine,axis.y*halfSine,axis.z*halfSine,
        static_cast<float>(std::cos(radians*0.5))};
}

#include "grip_pose_capture_functions.inl"

void TestNoQueryShortCircuits()
{
    const XrVector3f sentinel{9.0f,8.0f,7.0f};
    XrVector3f output=sentinel;
    const XrPath hand=static_cast<XrPath>(21);
    const XrSpace space=reinterpret_cast<XrSpace>(uintptr_t{22});
    const XrTime time=123456;

    // The locator has no enable/config parameter: with a live action and space
    // it always samples, because the free two-hand production geometry needs
    // both grip positions regardless of any Virtual Stock or support-rotation
    // selection. Only availability and tracked validity may reject a sample.
    Reset();
    g_supportGripPoseAction=XR_NULL_HANDLE;
    output=sentinel;
    CheckFailurePreservesOutput("Missing grip action is rejected",
        TryLocateSupportGripPosition(hand,space,time,output),output,sentinel);
    Check(stateCalls==0&&locateCalls==0,"Missing grip action makes no OpenXR calls");
    g_supportGripPoseAction=reinterpret_cast<XrAction>(uintptr_t{11});

    Reset();
    output=sentinel;
    CheckFailurePreservesOutput("Missing grip space is rejected",
        TryLocateSupportGripPosition(hand,XR_NULL_HANDLE,time,output),output,sentinel);
    Check(stateCalls==0&&locateCalls==0,"Missing grip space makes no OpenXR calls");
}

void TestStateShortCircuitsLocate()
{
    const XrPath hand=static_cast<XrPath>(21);
    const XrSpace space=reinterpret_cast<XrSpace>(uintptr_t{22});
    const XrTime time=123456;
    const XrVector3f sentinel{9.0f,8.0f,7.0f};
    XrVector3f output=sentinel;

    Reset();
    stateResult=XR_ERROR_RUNTIME_FAILURE;
    CheckFailurePreservesOutput("Grip action-state failure is rejected",
        TryLocateSupportGripPosition(hand,space,time,output),output,sentinel);
    Check(stateCalls==1&&locateCalls==0,"Action-state failure prevents locate");

    Reset();
    stateActive=XR_FALSE;
    output=sentinel;
    CheckFailurePreservesOutput("Inactive grip action is rejected",
        TryLocateSupportGripPosition(hand,space,time,output),output,sentinel);
    Check(stateCalls==1&&locateCalls==0,"Inactive action prevents locate");
}

void TestLocateAdmission()
{
    const XrPath hand=static_cast<XrPath>(21);
    const XrSpace space=reinterpret_cast<XrSpace>(uintptr_t{22});
    const XrTime time=123456;
    const XrVector3f sentinel{9.0f,8.0f,7.0f};
    XrVector3f output=sentinel;

    Reset();
    locateResult=XR_ERROR_RUNTIME_FAILURE;
    CheckFailurePreservesOutput("Locate failure is rejected",
        TryLocateSupportGripPosition(hand,space,time,output),output,sentinel);
    Check(stateCalls==1&&locateCalls==1,"Locate failure reaches locate exactly once");

    Reset();
    locateFlags=0;
    output=sentinel;
    CheckFailurePreservesOutput("Missing position validity is rejected",
        TryLocateSupportGripPosition(hand,space,time,output),output,sentinel);

    Reset();
    locateFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT;
    output=sentinel;
    const XrVector3f expected{4.5f,-2.25f,8.75f};
    locatePosition=expected;
    Check(TryLocateSupportGripPosition(hand,space,time,output),
        "Position-valid grip capture succeeds without orientation validity");
    Check(SamePosition(output,expected),"Successful grip capture copies the exact position");
    Check(lastStateAction==g_supportGripPoseAction&&lastStateSubaction==hand,
        "Grip query uses the support action and requested physical hand");
    Check(lastLocateSpace==space&&lastLocateBase==g_localSpace&&lastLocateTime==time,
        "Grip locate uses the requested space, local base, and predicted display time");
}

void TestFiniteValues()
{
    const XrPath hand=static_cast<XrPath>(21);
    const XrSpace space=reinterpret_cast<XrSpace>(uintptr_t{22});
    const XrTime time=123456;
    const XrVector3f sentinel{9.0f,8.0f,7.0f};
    const float invalid[]{NAN,INFINITY,NAN};
    for (int axis=0;axis<3;++axis)
    {
        Reset();
        locatePosition={1.0f,2.0f,3.0f};
        if (axis==0) locatePosition.x=invalid[0];
        if (axis==1) locatePosition.y=invalid[1];
        if (axis==2) locatePosition.z=invalid[2];
        XrVector3f output=sentinel;
        CheckFailurePreservesOutput("Non-finite grip position is rejected",
            TryLocateSupportGripPosition(hand,space,time,output),output,sentinel);
        Check(stateCalls==1&&locateCalls==1,"Non-finite position is rejected after one query and locate");
    }
}

// Independent copy of the BASE helper's pre-fix arithmetic: the rule PG off
// must keep. Origin nudged along the primary right, palm-depth sample shifted
// along the SUPPORT controller's own forward, strict along/lateral thresholds.
// This is deliberately not a call into the shipping helper.
bool BaseAcquisitionReference(const XrPosef& rpose, const XrPosef& lpose)
{
    const XrVector3f rfwd=Rotate(rpose.orientation,{0,0,-1});
    const float zoneRight=std::clamp(g_config.two_hand_zone_right_m,-0.10f,0.10f);
    const XrVector3f rright=Rotate(rpose.orientation,{1,0,0});
    const XrVector3f origin{rpose.position.x+rright.x*zoneRight,
                            rpose.position.y+rright.y*zoneRight,
                            rpose.position.z+rright.z*zoneRight};
    const XrVector3f supportForward=Rotate(lpose.orientation,{0,0,-1});
    const virtual_stock::Point3 grabPoint=virtual_stock::SupportGrabPoint(
        virtual_stock::Point3{lpose.position.x,lpose.position.y,lpose.position.z},
        virtual_stock::Point3{supportForward.x,supportForward.y,supportForward.z},
        g_config.left_grip_forward_m);
    const XrVector3f v{grabPoint.x-origin.x,grabPoint.y-origin.y,grabPoint.z-origin.z};
    const float along=v.x*rfwd.x+v.y*rfwd.y+v.z*rfwd.z;
    const XrVector3f perp{v.x-along*rfwd.x,v.y-along*rfwd.y,v.z-along*rfwd.z};
    const float lateral=sqrtf(perp.x*perp.x+perp.y*perp.y+perp.z*perp.z);
    return along>0.08f&&along<0.80f&&lateral<0.09f;
}

// F1 seam against the shipping acquisition helpers themselves. The base helper
// (PG off / unwired titles) must keep the pre-fix geometry verbatim, so a pure
// support-wrist rotation can still swing eligibility on a frozen hand position.
// The persistent helper (PG on, wired titles only) must evaluate the pure A003
// predicate on the primary acquisition axis and ignore the support wrist
// orientation entirely. Both rules share the shipped thresholds, the zone-right
// nudge and the palm-depth value.
void TestSupportGrabAcquisitionSeam()
{
    const XrQuaternionf identity{0.0f,0.0f,0.0f,1.0f};
    const XrVector3f primary{0.30f,1.40f,-0.25f};
    // along 0.40 m, lateral 0.089 m in the primary frame with a neutral wrist
    // and the shipped 0.097 m palm depth (the construction used by
    // tests/support_grab_tests.cpp).
    const XrVector3f rawSupport{0.389f,1.40f,-0.553f};
    const XrPosef rpose{identity,primary};
    const XrPosef lposeNeutral{identity,rawSupport};

    struct Wrist
    {
        const char* name;
        XrQuaternionf orientation;
    };
    const Wrist wrists[]={
        {"neutral",identity},
        {"roll +60",AxisAngle({0.0f,0.0f,1.0f},60.0f)},
        {"roll -60",AxisAngle({0.0f,0.0f,1.0f},-60.0f)},
        {"pitch +45",AxisAngle({1.0f,0.0f,0.0f},45.0f)},
        {"pitch -45",AxisAngle({1.0f,0.0f,0.0f},-45.0f)},
        {"yaw +45",AxisAngle({0.0f,1.0f,0.0f},45.0f)},
        {"yaw -45",AxisAngle({0.0f,1.0f,0.0f},-45.0f)},
    };

    g_config.two_hand_zone_right_m=0.0f;
    g_config.left_grip_forward_m=0.097f;

    bool baseMatchesReference=true;
    bool persistentMatchesPredicate=true;
    bool neutralBase=false,neutralPersistent=false;
    unsigned baseFlips=0,ruleDifferences=0;
    for (unsigned index=0;index<sizeof(wrists)/sizeof(wrists[0]);++index)
    {
        const Wrist& wrist=wrists[index];
        const XrPosef lpose{wrist.orientation,rawSupport};
        const bool base=TwoHandGrabZoneHit(rpose,lpose);
        const bool persistent=PersistentSupportGrabZoneHit(rpose,lpose);
        if (base!=BaseAcquisitionReference(rpose,lpose)) baseMatchesReference=false;
        const support_grab::SupportGrabZone zone=
            support_grab::EvaluateSupportGrabZone(
                virtual_stock::Point3{primary.x,primary.y,primary.z},
                virtual_stock::Point3{0.0f,0.0f,-1.0f},
                virtual_stock::Point3{1.0f,0.0f,0.0f},
                g_config.two_hand_zone_right_m,
                virtual_stock::Point3{rawSupport.x,rawSupport.y,rawSupport.z},
                g_config.left_grip_forward_m);
        if (persistent!=zone.inZone) persistentMatchesPredicate=false;
        if (base!=persistent) ++ruleDifferences;
        if (index==0) // the neutral wrist is the reference orientation
        {
            neutralBase=base;
            neutralPersistent=persistent;
        }
        else if (base!=neutralBase)
        {
            ++baseFlips;
        }
    }
    Check(baseMatchesReference,
        "shipping base helper matches the pre-fix acquisition arithmetic for every support wrist");
    Check(neutralBase,
        "neutral support wrist is inside the base acquisition zone");
    Check(neutralPersistent,
        "neutral support wrist is inside the persistent acquisition zone");
    Check(baseFlips>=1,
        "support wrist rotation still moves the BASE helper's eligibility");
    Check(persistentMatchesPredicate,
        "the persistent helper is the pure A003 predicate on every support wrist");
    Check(ruleDifferences>=1,
        "the base and persistent helpers are genuinely different acquisition rules");
    Check(PersistentSupportGrabZoneHit(rpose,lposeNeutral)&&
            TwoHandGrabZoneHit(rpose,lposeNeutral),
        "both rules agree on the neutral frozen sample");

    // The shipped palm depth moves the acquisition sample in both rules. A
    // neutral wrist makes the support forward and the primary acquisition axis
    // coincide, so both helpers must agree on both sides of the 0.80 m bound.
    const XrVector3f deepSupport{0.32f,1.40f,-1.00f}; // 0.75 m along at zero depth
    const XrPosef deepPose{identity,deepSupport};
    g_config.left_grip_forward_m=0.0f;
    const bool deepZeroBase=TwoHandGrabZoneHit(rpose,deepPose);
    const bool deepZeroPersistent=PersistentSupportGrabZoneHit(rpose,deepPose);
    g_config.left_grip_forward_m=0.097f;
    const bool deepPalmBase=TwoHandGrabZoneHit(rpose,deepPose);
    const bool deepPalmPersistent=PersistentSupportGrabZoneHit(rpose,deepPose);
    Check(deepZeroBase&&deepZeroPersistent,
        "at 0.75 m along both rules accept the hand with no palm depth");
    Check(!deepPalmBase&&!deepPalmPersistent,
        "the shipped palm depth pushes the same hand past the 0.80 m bound in both rules");

    // The shipped zone-right nudge still moves the base boundary (PG-off must
    // change nothing) and the persistent helper reads the same value.
    const XrVector3f edgeSupport{0.455f,1.40f,-0.553f}; // 0.155 m lateral
    const XrPosef edgePose{identity,edgeSupport};
    g_config.two_hand_zone_right_m=0.0f;
    const bool edgeZeroBase=TwoHandGrabZoneHit(rpose,edgePose);
    const bool edgeZeroPersistent=PersistentSupportGrabZoneHit(rpose,edgePose);
    g_config.two_hand_zone_right_m=0.075f;
    const bool edgeNudgedBase=TwoHandGrabZoneHit(rpose,edgePose);
    const bool edgeNudgedPersistent=PersistentSupportGrabZoneHit(rpose,edgePose);
    Check(!edgeZeroBase&&!edgeZeroPersistent,
        "the 0.155 m lateral edge is outside both zones at zero nudge");
    Check(edgeNudgedBase&&edgeNudgedPersistent,
        "the shipped 0.075 m zone-right nudge accepts the edge in both rules");

    // Non-finite primary geometry fails closed in the base helper too.
    const XrPosef nanPose{identity,{NAN,1.40f,-0.25f}};
    Check(!TwoHandGrabZoneHit(nanPose,lposeNeutral)&&
            !PersistentSupportGrabZoneHit(nanPose,lposeNeutral),
        "non-finite primary geometry fails closed in both acquisition helpers");

    g_config.two_hand_zone_right_m=0.0f;
    g_config.left_grip_forward_m=0.097f;
}
}

int main()
{
    TestNoQueryShortCircuits();
    TestStateShortCircuitsLocate();
    TestLocateAdmission();
    TestFiniteValues();
    TestSupportGrabAcquisitionSeam();
    std::printf("Support grip capture fixture: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
