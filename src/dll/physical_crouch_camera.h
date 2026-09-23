#pragma once
#include "../common/physical_crouch_camera_logic.h"

void PhysicalCrouchCamera_Publish(GameTitle title,uint32_t generation,
    uint64_t trackingEpoch,uint64_t nowMs,bool available,bool requested) noexcept;
void PhysicalCrouchCamera_Invalidate() noexcept;
bool PhysicalCrouchCamera_Read(PhysicalCrouchCameraRequest& request) noexcept;
