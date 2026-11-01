#pragma once
#include "haloce_render_logic.h"

namespace halo_ce
{
// One native center camera and one immutable XR sample/reference. The caller
// supplies the stock camera before first-person or per-eye changes; consumers
// must never reconstruct this from separately sampled global hand/head poses.
struct RenderContext
{
    Tracking tracking;
    Reference reference;
    Camera camera;
    float unitsPerMeter{};
    bool positional{};
    uint64_t referenceRevision{},rendererEpoch{};
    // FLAT PARITY (sight_layer.h SightCameraSample), filled when the gameplay context is published: when `camera` was
    // sampled (the sight clock, us; 0 unknown) and its position's velocity (native units/s) over ~100 ms of samples.
    uint64_t cameraStampUs{};
    bool cameraVelValid{};
    Vec3 cameraVel{};
};
}
