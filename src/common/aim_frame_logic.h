// AIM FRAME ZOOM (config.h aim_frame_zoom): the pure parts of showing the game's zoomed picture as a frame on the
// aim that a title with a fixed render aspect needs (Halo: Reach and Halo 4; game.cpp AimFrameThisFrame). Halo 3 and
// ODST write both projection terms themselves and always draw at the flat game's 16:9; these two engines derive the
// horizontal extent from the vertical one and the render surface, so the picture has the surface's shape. The HUD's
// scale in the frame (HudScale) is every title's.
#pragma once

#include <cmath>

namespace aim_frame
{
    inline constexpr float kDegToRad = 0.01745329f;
    inline constexpr float kRadToDeg = 57.29578f;

    // The width to show a picture of the tangents `tanX` x `tanY` at, for the frame-size setting `sizeDeg`, which is
    // the width of a frame of the flat game's `flatAspect`. A squarer picture keeps that frame's height: shown at the
    // full width, a nearly square picture would be as tall as it is wide, with the HUD's top and bottom out of view.
    // A picture at least that wide is shown at the setting.
    inline float ShownWidthDeg(float sizeDeg, float tanX, float tanY, float flatAspect) noexcept
    {
        if (!std::isfinite(sizeDeg) || !(tanX > 0.0f) || !(tanY > 0.0f) || !(flatAspect > 0.0f))
            return sizeDeg;
        const float aspect = tanX / tanY;
        if (!std::isfinite(aspect) || aspect >= flatAspect)
            return sizeDeg;
        return 2.0f * std::atan(std::tan(sizeDeg * 0.5f * kDegToRad) * aspect / flatAspect) * kRadToDeg;
    }

    // Whether what the engine built is the picture that was asked for, within `tolerance` of the tangent. Reach's and
    // Halo 4's lens passes hold their projections to the same check.
    inline bool TangentsAgree(float wanted, float built, float tolerance = 0.01f) noexcept
    {
        return std::isfinite(wanted) && std::isfinite(built) && wanted > 0.0f && built > 0.0f &&
            std::fabs(built / wanted - 1.0f) <= tolerance;
    }

    // Where an eye stands in the frame: the eye's offset from the midpoint (OpenXR view axes: +X right, +Y up, -Z
    // forward) laid along the aim's basis, scaled to world units.
    inline void EyePosition(const float centre[3], const float right[3], const float up[3], const float forward[3],
                            const float eyeOffset[3], float scale, float out[3]) noexcept
    {
        for (int axis = 0; axis < 3; ++axis)
            out[axis] = centre[axis] +
                (right[axis] * eyeOffset[0] + up[axis] * eyeOffset[1] - forward[axis] * eyeOffset[2]) * scale;
    }

    // A title's frame that keeps being refused gives the zoom back to its lens for the rest of the level, rather than
    // leaving a button that seems to do nothing. A frame that goes through clears the run; a single bad frame is only
    // that frame. `Note` answers true once, as the run reaches the limit.
    struct RefusalRun
    {
        static constexpr int kMost = 30;
        int run = 0;
        bool Note(bool refused) noexcept
        {
            run = refused ? run + 1 : 0;
            return run == kMost;
        }
    };

    // Halo 4 answers an observer field of view through a gain it was measured to have (halo4_render_logic.h,
    // Halo4FovCalibration): halfY = observerFov * gain, tanX = tanY * ratio. The game's own view from its observer...
    inline bool Halo4NativeTangents(float observerFov, float gain, float ratio, float& tanX, float& tanY) noexcept
    {
        if (!std::isfinite(observerFov) || !std::isfinite(gain) || !std::isfinite(ratio) || !(ratio > 0.0f))
            return false;
        const float halfY = observerFov * gain;
        if (!(halfY > 1.0e-4f) || !(halfY < 1.5f))
            return false;
        tanY = std::tan(halfY);
        tanX = tanY * ratio;
        return std::isfinite(tanX) && std::isfinite(tanY);
    }

    // The HUD's scale in the frame's picture, apart from the player's HUD layout (config.h aim_frame_hud_size,
    // aim_frame_hud_width_scale, aim_frame_hud_height_scale): 1 x 1 is the game's own layout, as on a flat screen,
    // whose edge art lines the picture's edges. Size scales both axes, width and height one each. A setting out of
    // range is held to it; one that is not a number is the game's own.
    inline constexpr float kHudSizeMin = 0.5f, kHudSizeMax = 1.25f;
    inline constexpr float kHudStretchMin = 0.5f, kHudStretchMax = 1.5f;
    inline void HudScale(float size, float width, float height, float& horizontal, float& vertical) noexcept
    {
        const auto held = [](float value, float lo, float hi)
        {
            return std::isfinite(value) ? (value < lo ? lo : value > hi ? hi : value) : 1.0f;
        };
        const float s = held(size, kHudSizeMin, kHudSizeMax);
        horizontal = s * held(width, kHudStretchMin, kHudStretchMax);
        vertical = s * held(height, kHudStretchMin, kHudStretchMax);
    }

    // ...and the observer field of view that asks for `tanY`.
    inline bool Halo4ObserverFovFor(float tanY, float gain, float& observerFov) noexcept
    {
        if (!std::isfinite(tanY) || !(tanY > 0.0f) || !std::isfinite(gain) || !(gain > 1.0e-3f))
            return false;
        observerFov = std::atan(tanY) / gain;
        return std::isfinite(observerFov) && observerFov > 1.0e-3f && observerFov < 3.14159f;
    }
}
