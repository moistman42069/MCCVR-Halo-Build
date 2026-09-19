#pragma once
#include "haloce_unit_control_logic.h"

namespace halo_ce
{
using PlayerAction=std::array<uint8_t,0x30>;
template<class T> inline T ReadAction(const PlayerAction& action,size_t offset) noexcept
{ T value{};std::memcpy(&value,action.data()+offset,sizeof(value));return value; }
template<class T> inline void WriteAction(PlayerAction& action,size_t offset,T value) noexcept
{ std::memcpy(action.data()+offset,&value,sizeof(value)); }

// Native action yaw/pitch feeds BOTH prediction and authoritative simulation.
// CE has one network direction, unlike its richer local unit-control record.
inline bool BuildNetworkAction(const PlayerAction& source,Vec3 direction,bool onFoot,
    PlayerAction& output) noexcept
{
    const float length=std::sqrt(Dot(direction,direction));
    const float yaw=ReadAction<float>(source,4),pitch=ReadAction<float>(source,8);
    const Vec3 throttle=ReadAction<Vec3>(source,0xc);
    if (!Finite(direction)||!std::isfinite(length)||length<.001f||
        !std::isfinite(yaw)||!std::isfinite(pitch)||!Finite(throttle)||
        Dot(throttle,throttle)>9.001f||(ReadAction<uint32_t>(source,0)&0x100)) return false;
    direction=direction*(1/length);
    // At the pitch pole retain the native azimuth, avoiding a discontinuous
    // movement rotation caused by tiny controller horizontal components.
    const float horizontal=std::sqrt(direction.x*direction.x+direction.y*direction.y);
    float newYaw=horizontal>.0001f?std::atan2(direction.y,direction.x):yaw;
    constexpr float tau=6.28318530718f;
    newYaw=std::fmod(newYaw,tau);
    if (newYaw<0) newYaw+=tau;
    const float newPitch=std::atan2(direction.z,horizontal);
    auto candidate=source;
    WriteAction(candidate,4,newYaw);WriteAction(candidate,8,newPitch);
    if (onFoot)
    {
        // Native throttle X/Y means forward/left. Preserve its intended world
        // direction when expressing it relative to the new action bearing.
        // Simulation remains fully native, including body turn interpolation.
        const float delta=yaw-newYaw,c=std::cos(delta),s=std::sin(delta);
        const Vec3 rotated{c*throttle.x-s*throttle.y,s*throttle.x+c*throttle.y,throttle.z};
        if (!Finite(rotated)) return false;
        WriteAction(candidate,0xc,rotated);
    }
    output=candidate;return true;
}
inline bool InverseSeatDirection(Vec3 world,const Vec3 (&basis)[3],Vec3& local) noexcept
{
    for (int i=0;i<3;++i)
    {
        if (!Finite(basis[i])||std::fabs(Dot(basis[i],basis[i])-1)>.002f) return false;
        for (int j=0;j<i;++j) if (std::fabs(Dot(basis[i],basis[j]))>.002f) return false;
    }
    if (!Finite(world)||Dot(Cross(basis[0],basis[1]),basis[2])<.998f) return false;
    local={Dot(world,basis[0]),Dot(world,basis[1]),Dot(world,basis[2])};return Finite(local);
}
}
