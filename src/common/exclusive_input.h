#pragma once
#include <cmath>
#include <cstdint>
#include <atomic>

namespace exclusive_input
{
// Published by XR input; native damage callbacks must obey the same mode as
// XInput. No pose/camera publication is invalidated by this gameplay gate.
inline std::atomic<bool> active{false};
inline bool Active() noexcept { return active.load(std::memory_order_acquire); }
}

// A consumed hold stays consumed until that control returns to rest. Each
// physical source owns its own latch; merging sources must not release it.
struct ExclusiveInputHolds
{
    uint32_t buttons{};
    bool analog[6]{};
    uint32_t Buttons(uint32_t value, bool consume) noexcept
    {
        buttons &= value;
        if (consume) buttons |= value;
        return value & ~buttons;
    }
    float Axis(float value, unsigned index, bool consume, float rest) noexcept
    {
        if (!std::isfinite(value)) { analog[index]=true; return 0; }
        if (std::fabs(value)<=rest) analog[index]=false;
        else if (consume) analog[index]=true;
        return consume||analog[index] ? 0 : value;
    }
    // `allowFaceButtons`: the native menu pointer owns the pad. A, B, X and Y pass to the menu, which acts on them
    // without moving its focus; the sticks, D-pad and clicks would move it under the pointer, so they stay consumed.
    template<class Pad> void ApplyVr(Pad& p, bool consume, bool allowFaceButtons=false) noexcept
    {
        const uint32_t bits=(p.a?1u:0)|(p.b?2u:0)|(p.x?4u:0)|(p.y?8u:0)|
            (p.clickL?16u:0)|(p.clickR?32u:0)|(p.menu?64u:0);
        // Still latch the face buttons' holds so closing the menu cannot turn
        // the same press into gameplay input.
        const auto b=Buttons(bits,consume)|(consume&&allowFaceButtons?(bits&15u):0u);
        p.a=b&1;p.b=b&2;p.x=b&4;p.y=b&8;p.clickL=b&16;p.clickR=b&32;p.menu=b&64;
        // Vector latches preserve direction and require BOTH axes to rest.
        const float m=Axis(std::hypot(p.moveX,p.moveY),0,consume,.2f);
        if (!m) p.moveX=p.moveY=0;
        const float t=Axis(std::hypot(p.turnX,p.turnY),1,consume,.2f);
        if (!t) p.turnX=p.turnY=0;
        p.trigL=Axis(p.trigL,2,consume,.15f);p.trigR=Axis(p.trigR,3,consume,.15f);
        p.gripL=Axis(p.gripL,4,consume,.15f);p.gripR=Axis(p.gripR,5,consume,.15f);
        if (consume) p.weaponButtons=0;
    }
    template<class Pad> void ApplyPhysical(Pad& p, bool consume, bool allowFaceButtons=false) noexcept
    {
        const uint32_t face=consume&&allowFaceButtons?(p.wButtons&0xF000u):0u; // XINPUT_GAMEPAD_A, B, X and Y
        p.wButtons=static_cast<uint16_t>(Buttons(p.wButtons,consume)|face);
        if (!Axis(std::hypot(float(p.sThumbLX),float(p.sThumbLY)),0,consume,7849))
            p.sThumbLX=p.sThumbLY=0;
        if (!Axis(std::hypot(float(p.sThumbRX),float(p.sThumbRY)),1,consume,8689))
            p.sThumbRX=p.sThumbRY=0;
        p.bLeftTrigger=static_cast<uint8_t>(Axis(float(p.bLeftTrigger),2,consume,30));
        p.bRightTrigger=static_cast<uint8_t>(Axis(float(p.bRightTrigger),3,consume,30));
    }
};

// Compensate a radial deadzone without changing the requested direction.
// Per-axis floors turn a 1% lateral correction into ~28% sideways travel.
inline void RadialMoveStick(float x,float y,int16_t& rawX,int16_t& rawY) noexcept
{
    const float length=std::hypot(x,y);
    if (!std::isfinite(length)||length<.001f) { rawX=rawY=0;return; }
    const float magnitude=9000.0f+std::fmin(length,1.0f)*(32767.0f-9000.0f);
    rawX=static_cast<int16_t>(x/length*magnitude);
    rawY=static_cast<int16_t>(y/length*magnitude);
}
