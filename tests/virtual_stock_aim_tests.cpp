#include <openxr/openxr.h>
#include "../src/common/virtual_stock_logic.h"
#include "../src/common/virtual_stock_test_profiles.h"
#include "../src/dll/aim_pose_trace.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include "virtual_stock_fixture.inl"

static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static bool Close(float a, float b) { return std::fabs(a - b) < .00002f; }
static bool Same(XrPosef a, XrPosef b) {
    return Close(a.position.x,b.position.x) && Close(a.position.y,b.position.y) && Close(a.position.z,b.position.z) &&
        Close(a.orientation.x,b.orientation.x) && Close(a.orientation.y,b.orientation.y) && Close(a.orientation.z,b.orientation.z) && Close(a.orientation.w,b.orientation.w);
}
int main() {
    try {
        for (int handed = 0; handed < 2; ++handed) for (int mode = 0; mode < 4; ++mode) for (int sample = 0; sample < 80; ++sample) {
            AimPoseInputs input;
            input.rightValid = input.leftValid = input.twoHandEnabled = input.twoHandLatched = input.headValid = true;
            const float side = handed ? -.15f : .15f;
            input.right.position={side,1.30f,-.25f}; input.left.position={side+(sample-40)*.001f,1.29f,-.60f};
            input.supportPosition=input.left.position; input.headPosition={0,1.60f,0};
            input.virtualStockLeftHanded=handed!=0; input.virtualStockRearReference=mode;
            input.gunPitchDeg=7; input.gunYawDeg=-3; input.gunRollDeg=2;
            const bool hybrid=mode==3;
            const auto ordinary=ComputeAimPose(input); Check(ordinary.valid && ordinary.twoHandActive,"normal two-hand fixture");
            input.virtualStockEnabled=true; input.virtualStockStrength=0;
            const auto zero=ComputeAimPose(input);
            if (!hybrid)
                Check(zero.valid && Same(zero.pose,ordinary.pose),"zero stock matches the shipping controller solver for every non-Hybrid mode/hand");
            else
            {
                // Donor Hybrid (mode 3) keeps its A/B offhand authority active
                // independently of stock strength; the donor's own Hybrid tests
                // assert the exact primary-controller endpoint instead
                // (aim_pose_tests: "Hybrid influence zero with no C preserves
                // the exact A quaternion and active presentation").
                AimPoseInputs zeroInfluence=input; zeroInfluence.virtualStockHybridOffhandInfluence=0;
                AimPoseInputs primaryOnly=input; primaryOnly.twoHandEnabled=false;
                Check(zero.valid && zero.twoHandActive &&
                    zero.pose.position.x==input.right.position.x && zero.pose.position.y==input.right.position.y && zero.pose.position.z==input.right.position.z &&
                    Same(ComputeAimPose(zeroInfluence).pose,ComputeAimPose(primaryOnly).pose),
                    "Hybrid keeps A/B presentation active and preserves the exact primary aim at zero stock strength");
            }
            input.virtualStockStrength=.95f;
            const auto stock=ComputeAimPose(input); Check(stock.valid && stock.twoHandActive,"all virtual stock modes produce valid supported aim");
            Check(stock.pose.position.x==input.right.position.x && stock.pose.position.y==input.right.position.y && stock.pose.position.z==input.right.position.z,"stock must never move the primary controller / raw muzzle anchor");
            input.headValid=false;
            if (!hybrid)
                Check(Same(ComputeAimPose(input).pose,ordinary.pose),"missing coherent head falls back to ordinary two-hand aim");
            else
            {
                // Donor Hybrid's head-invalid endpoint is the exact primary
                // aim, not the legacy two-hand pose (aim_pose_tests: head
                // invalid + zero influence preserves A and stays active).
                AimPoseInputs noHeadInfluence=input; noHeadInfluence.virtualStockHybridOffhandInfluence=0;
                AimPoseInputs primaryOnly=input; primaryOnly.twoHandEnabled=false;
                Check(Same(ComputeAimPose(noHeadInfluence).pose,ComputeAimPose(primaryOnly).pose),
                    "Hybrid without a coherent head preserves the exact primary aim");
            }
            input.headValid=true; input.twoHandLatched=false;
            const auto notLatched=ComputeAimPose(input); input.virtualStockEnabled=false;
            Check(Same(notLatched.pose,ComputeAimPose(input).pose) && !notLatched.twoHandActive,"stock cannot acquire or latch support grip");
            input.virtualStockEnabled=true; input.twoHandLatched=true; input.twoHandEnabled=false;
            const auto independent=ComputeAimPose(input); input.virtualStockEnabled=false;
            Check(Same(independent.pose,ComputeAimPose(input).pose) && !independent.twoHandActive,"physical/independent dual-wield paths retain controller calibration");
        }
        // Retired head-turn sway correction (2026-09-27). With inverse-neck
        // disabled, a stale-but-valid stored neutral orientation (kept far from
        // the current head) must be completely inert: the full result is
        // byte-identical and no inverse-neck evaluation is attempted, for
        // product-like Standard (rear 0/1) and Plus (rear 3) strength.
        for (const int rear : {0, 1, 3})
        {
            AimPoseInputs input;
            input.rightValid = input.leftValid = input.twoHandEnabled = input.twoHandLatched = input.headValid = true;
            input.right.position={.15f,1.30f,-.25f}; input.left.position={.15f,1.29f,-.60f};
            input.supportPosition=input.left.position; input.headPosition={0,1.60f,0};
            input.headOrientation={0,.3826834f,0,.9238795f};
            input.virtualStockEnabled=true; input.virtualStockRearReference=rear;
            input.virtualStockStrength=rear==3?.80f:.95f;
            input.hybridInverseNeckEnabled=false;
            AimPoseTrace withoutNeutralTrace;
            const auto withoutNeutral=ComputeAimPoseImpl<true>(input,&withoutNeutralTrace);
            Check(withoutNeutral.valid && !withoutNeutralTrace.inverseNeckAttempted &&
                !withoutNeutralTrace.inverseNeckValid,
                "disabled inverse-neck never attempts or validates a stored neutral");
            input.inverseNeckNeutralValid=true;
            input.inverseNeckNeutralOrientation={0,.7071068f,0,.7071068f}; // yaw 90 deg from the head
            input.inverseNeckNeutralCaptureSerial=4321;
            input.inverseNeckNeutralCaptureContactSpaceEpoch=7;
            AimPoseTrace staleNeutralTrace;
            const auto withStaleNeutral=ComputeAimPoseImpl<true>(input,&staleNeutralTrace);
            Check(!staleNeutralTrace.inverseNeckAttempted && !staleNeutralTrace.inverseNeckValid &&
                std::memcmp(&withoutNeutral,&withStaleNeutral,sizeof(AimPoseResult))==0,
                "a stale stored neutral is byte-inert while inverse-neck is disabled");
        }
        std::cout<<"640 production aim combinations passed: legacy zero/off parity and missing-head fallback on donor modes 0-2, Hybrid A/B zero-strength and head-invalid endpoints, grip isolation, raw-position invariance; retired sway correction leaves a stale stored neutral byte-inert for Standard and Plus.\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
