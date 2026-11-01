#pragma once

#include "haloce_render_logic.h"
#include <cstring>

namespace halo_ce
{
// The final kind-0 quad samples the complete kind-1 scene and normalizes its
// rectangle to clip space. Its host output need not share the native raster
// size (CE can round the internal dimensions). Only a full primary viewport
// can use this mapping; split/cropped windows still require their own proof.
inline bool ClassicFullRaster(const Window& window) noexcept
{
    return ValidPrimary(window)&&window.raster.viewport.left==0&&
        window.raster.viewport.top==0&&Same(window.raster.viewport,window.raster.window)&&
        Same(window.render.viewport,window.render.window);
}

// E-CE-1: Classic constructs BOTH native frusta from the two Camera values
// in each 0xAC window. Staging one camera would leave visibility and drawing
// with different head positions. This object contains private bytes only.
// A mono pair (tracking.mono) holds ONE camera, at the midpoint of the eyes with no cant, in both slots: the frame
// is one native pass, drawn and captured as eye 0, and shown to both eyes.
struct ClassicViewPair
{
    Window source;
    Window eyes[2];
    Tracking tracking;
    Cover cover;
    uint64_t rendererEpoch{};
    bool mono{};
};

enum class ClassicPairResult : uint8_t
{
    Ready, InvalidWindow, InvalidTracking, InvalidRendererEpoch,
    InvalidCover, InvalidEye
};

inline ClassicPairResult StageClassicViewPair(const Window& source,
    const Tracking& tracking,const Reference& reference,uint64_t rendererEpoch,
    float unitsPerMeter,bool positional,ClassicViewPair& out) noexcept
{
    if (!ValidPrimary(source)) return ClassicPairResult::InvalidWindow;
    if (!tracking.serial||!tracking.generation||!tracking.spaceEpoch||
        tracking.generation!=reference.generation||
        tracking.spaceEpoch!=reference.spaceEpoch)
        return ClassicPairResult::InvalidTracking;
    if (!rendererEpoch) return ClassicPairResult::InvalidRendererEpoch;

    ClassicViewPair staged{};
    staged.source=source;
    staged.tracking=tracking;
    staged.rendererEpoch=rendererEpoch;
    staged.mono=tracking.mono;
    // The cover is the same either way: one symmetric frustum wide enough for either eye.
    if (!BuildCover(tracking,source.raster.viewport,staged.cover))
        return ClassicPairResult::InvalidCover;
    // Mono: eye 0 of a copy is moved to the midpoint of the eyes and turned to the head's own orientation.
    Tracking cameras=tracking;
    if (staged.mono)
    {
        cameras.eyes[0].offset=(tracking.eyes[0].offset+tracking.eyes[1].offset)*0.5f;
        cameras.eyes[0].orientation=tracking.headOrientation;
    }
    for (int eye=0;eye<(staged.mono?1:2);++eye)
    {
        staged.eyes[eye]=source;
        if (!BuildEye(source.render,cameras,reference,eye,unitsPerMeter,
                positional,staged.cover,staged.eyes[eye].render)||
            !BuildEye(source.raster,cameras,reference,eye,unitsPerMeter,
                positional,staged.cover,staged.eyes[eye].raster)||
            !ValidPrimary(staged.eyes[eye]))
            return ClassicPairResult::InvalidEye;
    }
    if (staged.mono) staged.eyes[1]=staged.eyes[0];
    out=staged;
    return ClassicPairResult::Ready;
}

// Stock-camera bytes and all identity fields must still match before native
// execution starts. Mode switches/recenter/title entry revoke the pair even
// if the head is stationary and the camera happens to be bit-identical.
inline bool ClassicPairCurrent(const ClassicViewPair& pair,const Window& source,
    uint32_t generation,uint64_t spaceEpoch,uint64_t serial,
    uint64_t rendererEpoch) noexcept
{
    return generation&&spaceEpoch&&serial&&rendererEpoch&&
        pair.tracking.generation==generation&&pair.tracking.spaceEpoch==spaceEpoch&&
        pair.tracking.serial==serial&&pair.rendererEpoch==rendererEpoch&&
        std::memcmp(&pair.source,&source,sizeof(source))==0;
}
}
