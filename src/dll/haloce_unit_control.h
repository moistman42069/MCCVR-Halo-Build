#pragma once
#include <cstddef>
#include <cstdint>
#include "../common/seat_converge_logic.h"

struct HaloCELocalPlayerState;
namespace halo_ce { struct RenderContext; }

bool HaloCEUnitControl_Poll(uintptr_t base,size_t size,uint32_t generation,bool active) noexcept;
// Read-only admission for the optional seated crosshair, using the exact
// vehicle proof and native ownership checks used by controller steering.
bool HaloCEUnitControl_VehicleAimCurrent(const HaloCELocalPlayerState& player,
    const halo_ce::RenderContext& context) noexcept;

// SEAT CONVERGENCE (seat_converge_logic.h; docs/H2-CE-SEAT-CONVERGENCE-2026-10-06.md). The local player's seated
// intent ray, admitted exactly as the seated packet path admits the seat (the verified vehicle group, the local
// biped, its salted vehicle parent and seat, unblocked native input, the coherent tracking receipt), with the
// carrier that packet consumes.
struct HaloCESeatSight
{
    uint32_t generation{},unit{0xffffffffu},parent{0xffffffffu};
    // The vehicle hierarchy's ultimate parent: what CE's own crosshair test ignores (0xB38BA8 at 0xB67D92), and
    // CE skips an ignored object with everything attached to it.
    uint32_t root{0xffffffffu};
    int16_t seat{-1};
    uint32_t key{};                 // seat_converge::StanceKey(parent, seat)
    seat_converge::Role role{};     // from the vehicle's own occupant handles (+0x304 facing, +0x308 aim)
    float head[3]{},dir[3]{};       // the rendered centre eye, along the aiming hand (CE world units, unit dir)
    float unitsPerMeter{};
};
// Simulation thread (the biped tick's trace, the packet hook, the fire). False: not a seat the hand aims now.
bool HaloCEUnitControl_SeatSight(uint32_t unit,HaloCESeatSight& out) noexcept;

struct HaloCESeatAim
{
    HaloCESeatSight sight{};
    seat_converge::Why why{seat_converge::Why::NoHand};
    float eye[3]{};     // the seat's engine eye: CE's deterministic camera for the biped, asked now
    float aim[3]{};     // the converged aim (unit): from that eye through `point`
    float point[3]{};   // head + dir * the range the aim converges on
};
// The seat's converged aim, solved now. True when `why` is Converged; otherwise `why` says what refused (ours, never
// a natural limit) and the seat keeps the controller's own forward, as before.
bool HaloCEUnitControl_SeatAim(uint32_t unit,HaloCESeatAim& out) noexcept;
// A seat's aim was handed to the game OUTSIDE the packet hook (a network session's outgoing action): the seat's
// account (the SIGHT ... seat line) and, converged, the marks the traced reticle and the fire's re-aim key on.
void HaloCEUnitControl_SeatAimApplied(const HaloCESeatAim& aim,uint32_t parent,int16_t seat,bool converged) noexcept;
