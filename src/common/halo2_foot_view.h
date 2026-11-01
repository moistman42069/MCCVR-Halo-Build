#pragma once
#include "halo2_render_logic.h"

// HALO 2 ON FOOT UNDER DIRECT DRIVE: the view's own heading.
//
// Direct drive writes player control's desired angles from the hand, as in Reach, Halo 3, ODST and
// Halo 4, and Halo 2's stock camera is built from those angles: once the drive writes, the stock heading
// follows the HAND. The view, the floating hands and the intent ray all took the stock heading as their
// world reference. Left on it, the camera turns with the hand (C-H2-41, rejected in the headset) and the
// aim, built in a frame that turns with itself, runs away. This is the one on-foot room-to-world heading,
// owned by the mod: seeded from the heading the view had when the drive took over, so nothing moves, and
// then turned only by the right stick, smooth or snap, as ApplyVrTurn turns the other titles.
struct Halo2FootViewState
{
    bool active=false;
    uint32_t generation=0;
    float yaw=0.0f;          // Halo heading, radians: forward = (cos yaw, sin yaw, 0); left-positive
    bool snapLatched=true;   // a stick already held when the heading is taken must be let go first
    // Counts each time the heading is taken: the caller's smooth-turn clock restarts on a change, so the
    // first turn after the view engages does not inherit the whole idle span as one capped 0.1 s step.
    uint32_t engagements=0;

    static float Wrap(float angle) noexcept
    { return std::remainder(angle,6.28318530718f); }

    // Own the heading from `seedYaw`. Already owning it in this generation: keep it.
    bool Engage(uint32_t gen,float seedYaw) noexcept
    {
        if(!gen || !std::isfinite(seedYaw)) return false;
        if(!active || generation!=gen) {
            yaw=Wrap(seedYaw);generation=gen;active=true;snapLatched=true;++engagements;
        }
        return true;
    }

    void Release() noexcept { active=false; }

    // The right stick: right turns right, so the left-positive heading decreases. Smooth: past 0.15, at
    // `smoothDegPerSec` for `dt` seconds (capped at 0.1 s). Snap: one `snapDeg` step (5-90 deg) per push
    // past 0.6, re-armed once the stick is back under 0.3 -- Halo2SnapTurnState's thresholds. A push held
    // across a switch to snap does not snap. Returns true when it snapped.
    bool Turn(bool padValid,float x,bool snapMode,float snapDeg,float smoothDegPerSec,float dt) noexcept
    {
        if(!active) return false;
        if(!padValid || !std::isfinite(x)) { snapLatched=true; return false; }
        if(std::fabs(x)<0.3f) snapLatched=false;
        if(!snapMode) {
            if(std::fabs(x)>0.6f) snapLatched=true;
            if(std::fabs(x)>0.15f && std::isfinite(dt) && dt>0.0f && std::isfinite(smoothDegPerSec))
                yaw=Wrap(yaw-x*smoothDegPerSec*0.0174532925199f*std::min(dt,0.1f));
            return false;
        }
        if(snapLatched || std::fabs(x)<=0.6f || !std::isfinite(snapDeg)) return false;
        snapLatched=true;
        yaw=Wrap(yaw-std::copysign(std::clamp(snapDeg,5.0f,90.0f)*0.0174532925199f,x));
        return true;
    }

    // The view's base on foot: the stock camera at this heading, level (forward (cos, sin, 0), up +Z),
    // the same form Halo2VehicleViewState::Build gives a seat.
    bool Basis(const Halo2CameraBasis& stock,Halo2CameraBasis& output) const noexcept
    {
        if(!active || !std::isfinite(yaw) || !Halo2ValidateCameraBasis(stock)) return false;
        output=stock;
        output.forward[0]=std::cos(yaw);output.forward[1]=std::sin(yaw);output.forward[2]=0.0f;
        output.up[0]=output.up[1]=0.0f;output.up[2]=1.0f;
        return true;
    }
};
