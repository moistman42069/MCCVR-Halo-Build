#pragma once
#include <cstddef>
#include <cstdint>
#include "../common/haloce_frame_context.h"

// HALO CE: THE MOTION TRACKER FOLLOWS THE HEAD (src/common/haloce_head_relative_logic.h has the mechanism;
// docs/HALOCE-HEAD-RELATIVE-2026-10-07.md the write-up). RETAIL-DERIVED LEAD, NOT HCEEK-VERIFIED.
// An optional transaction of its own: its admission and installation never own or revoke stereo, gameplay
// controls or OpenXR, and a refusal is the game's own tracker.

// Worker only. Installs while CE is armed, retires otherwise, reports every two seconds.
bool HaloCEHeadRelative_Poll(uintptr_t base,size_t size,uint32_t generation,bool active) noexcept;
// The render path, once per published gameplay context (haloce_stereo_core.cpp, PublishGameplayContext): the
// frame's stock centre camera, tracking sample and reference, already stamped on the sight clock
// (context.cameraStampUs). `anniversary`: the frame was prepared under Anniversary graphics.
void HaloCEHeadRelative_PublishHead(const halo_ce::RenderContext& context,bool anniversary) noexcept;
