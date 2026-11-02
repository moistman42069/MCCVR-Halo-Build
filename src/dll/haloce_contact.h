#pragma once
#include <cstddef>
#include <cstdint>
#include "../common/haloce_first_person_logic.h"
#include "../common/haloce_frame_context.h"
#include "../common/contact_melee_motion.h"

struct HaloCEContactPublication
{
    contact_melee::Frame frames[2]{};
    uint32_t generation{};
    uint8_t weaponBoundsSamples[2]{};
};

// Independent optional feature; failure never changes camera/render ownership.
bool HaloCEContact_Poll(uintptr_t base,size_t size,uint32_t generation,bool active) noexcept;
void HaloCEContact_ApplyPalette(const halo_ce::RenderContext& context,
    const halo_ce::FirstPersonBinding& binding,const halo_ce::NodeMatrix* authored,
    halo_ce::NodeMatrix* staged,HaloCEContactPublication& publication) noexcept;
// Publish the unblocked pose only after the native palette really commits.
void HaloCEContact_CommitPalette(const halo_ce::RenderContext& context,
    const HaloCEContactPublication& publication) noexcept;
// A line traced with the sight trace's own query (the engine's projectile flags), for the vehicle cameras' "collide":
// `ignore` and everything attached to it are skipped. Only on the game's own thread (the camera code the observer's
// own collision runs in), never the render thread. True when traced, with `hit` and the fraction of the line it went.
bool HaloCEContact_TraceSegment(const float start[3],const float displacement[3],uint32_t ignore,bool& hit,
    float& fraction) noexcept;
