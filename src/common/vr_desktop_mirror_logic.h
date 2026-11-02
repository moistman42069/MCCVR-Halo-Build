#pragma once

#include <cmath>

// Where one headset eye goes in the game's backbuffer so the window shows it with its true proportions. The window
// stretches the backbuffer to its own shape (Project Reclaimer's 2912x2100 onto a 3840x2160 window), so the crop is
// worked out against the window's aspect and then expressed in backbuffer pixels. The eye covers the window, centred;
// the overflow on one axis is cropped.
namespace vr_desktop_mirror
{
inline bool CropViewport(float tanX, float tanY, long windowWidth, long windowHeight, unsigned backbufferWidth,
                         unsigned backbufferHeight, float& left, float& top, float& width, float& height) noexcept
{
    if (!std::isfinite(tanX) || !std::isfinite(tanY) || tanX <= 0.0f || tanY <= 0.0f || windowWidth <= 0 ||
        windowHeight <= 0 || !backbufferWidth || !backbufferHeight)
        return false;
    const float eyeAspect = tanX / tanY;
    const float windowAspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    width = static_cast<float>(backbufferWidth);
    height = static_cast<float>(backbufferHeight);
    if (eyeAspect >= windowAspect)
        width *= eyeAspect / windowAspect;
    else
        height *= windowAspect / eyeAspect;
    left = (static_cast<float>(backbufferWidth) - width) * 0.5f;
    top = (static_cast<float>(backbufferHeight) - height) * 0.5f;
    return std::isfinite(width) && std::isfinite(height) && width <= 32767.0f && height <= 32767.0f;
}
} // namespace vr_desktop_mirror
