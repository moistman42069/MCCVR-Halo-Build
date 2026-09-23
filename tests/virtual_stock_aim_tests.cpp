#include <openxr/openxr.h>
#include "../src/common/virtual_stock_logic.h"
#include <iostream>
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
            const auto ordinary=ComputeAimPose(input); Check(ordinary.valid && ordinary.twoHandActive,"normal two-hand fixture");
            input.virtualStockEnabled=true; input.virtualStockStrength=0;
            const auto zero=ComputeAimPose(input); Check(zero.valid && Same(zero.pose,ordinary.pose),"zero stock matches the shipping controller solver for every mode/hand");
            input.virtualStockStrength=.95f;
            const auto stock=ComputeAimPose(input); Check(stock.valid && stock.twoHandActive,"all virtual stock modes produce valid supported aim");
            Check(stock.pose.position.x==input.right.position.x && stock.pose.position.y==input.right.position.y && stock.pose.position.z==input.right.position.z,"stock must never move the primary controller / raw muzzle anchor");
            input.headValid=false;
            Check(Same(ComputeAimPose(input).pose,ordinary.pose),"missing coherent head falls back to ordinary two-hand aim");
            input.headValid=true; input.twoHandLatched=false;
            const auto notLatched=ComputeAimPose(input); input.virtualStockEnabled=false;
            Check(Same(notLatched.pose,ComputeAimPose(input).pose) && !notLatched.twoHandActive,"stock cannot acquire or latch support grip");
            input.virtualStockEnabled=true; input.twoHandLatched=true; input.twoHandEnabled=false;
            const auto independent=ComputeAimPose(input); input.virtualStockEnabled=false;
            Check(Same(independent.pose,ComputeAimPose(input).pose) && !independent.twoHandActive,"physical/independent dual-wield paths retain controller calibration");
        }
        std::cout<<"640 production aim combinations passed: four stock modes, both hands, zero/off parity, missing-head fallback, grip isolation, raw-position invariance.\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
