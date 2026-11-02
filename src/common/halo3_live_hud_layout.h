#pragma once
#include "hud_layout_logic.h"
#include <cmath>
#include <cstddef>
#include <cstring>

namespace halo3_live_hud {
// Verified retail selector and its HUD frame-setup caller. The caller consumes
// the returned 0x64-byte resolution record, including +0x28/+0x2c safe frames.
inline constexpr const char* kSelector =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 33 DB 48 8B 35 ?? ?? ?? ?? 8B EA";
inline constexpr const char* kSelectorTail =
    "48 63 C1 48 6B C0 64 48 03 C3 48 8B 5C 24 30 48 83 C4 20 5F C3";
inline constexpr const char* kCaller =
    "E8 ?? ?? ?? ?? 4C 8B 35 ?? ?? ?? ?? 48 8B D8 F3 0F 10 3D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 48 8B 48 18 48 89 0D ?? ?? ?? ??";
inline constexpr const char* kSafeFrameConsumer =
    "F3 0F 10 4B 28 0F 2E 0D ?? ?? ?? ?? 7A 06 0F 84 ?? ?? ?? ?? F3 0F 10 43 2C";
struct Record { uint32_t flags;float fov,aspect,depth;uint32_t width,height;float fields[19]; };
static_assert(sizeof(Record)==0x64);
static_assert(offsetof(Record,fields)==0x18);
inline bool Transform(const Record& native,float horizontal,float vertical,float curvature,Record& out)
{
    if(native.width<64||native.width>8192||native.height<64||native.height>8192||
       !std::isfinite(native.fov)||!std::isfinite(native.aspect)||!std::isfinite(native.depth)||
       native.depth < -10 || native.depth > 10 || native.fov<=0 || native.fov>=3.14f || native.aspect<=0 || native.aspect>8)
        return false;
    for(float f:native.fields)if(!std::isfinite(f))return false;
    if(!(horizontal>=.15f&&horizontal<=1&&vertical>=.15f&&vertical<=1&&curvature>=0&&curvature<=1))return false;
    // The accepted scanner owns stock records with usable safe frames. A zero
    // pair is legal native data: frame setup bypasses safe-frame scaling then.
    const auto& stock=kHalo3HudLayoutAdapter;
    if(std::memcmp(&native.width,stock.anchor.data(),24)==0 &&
       native.fields[4]>=.15f&&native.fields[4]<=1.05f&&native.fields[5]>=.15f&&native.fields[5]<=1.05f)
        return false;
    if(native.fields[4]<0||native.fields[4]>1.05f||native.fields[5]<0||native.fields[5]>1.05f)return false;
    out=native;
    out.depth=native.depth+.30f-.60f*curvature;
    out.fields[4]=horizontal;out.fields[5]=vertical;
    return true;
}
}
