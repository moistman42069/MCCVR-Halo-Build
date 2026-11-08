#pragma once
// THE FITTED WINDOW'S MOUSE MESSAGES (fit_desktop_window). The window is shrunk to the monitor while MCC draws, lays out
// and hit-tests its menus at the full render size. d3d11_hook.cpp hands MCC's cursor reads the render-space point
// (GetCursorPosHook), so hover lands where the ring points; a button or wheel message carries its own position, which
// MCC takes for the press, so it is mapped the same way -- or a press lands about a tenth of the way nearer the
// window's top-left than the item hovered, and the menu drops it. One rule for both: a point inside the window, its
// offset from the window's top-left scaled by render / window, rounded to the nearest pixel; a point outside it is
// left as it is.
#include <cmath>
#include <cstdint>

namespace fit_mouse
{
// `x`, `y`: the point's offset from the window's client top-left. True and the render-space offset, or false (outside
// the window, or no window or render size) and nothing changed.
inline bool MapClientOffset(long x, long y, long clientW, long clientH, uint32_t renderW, uint32_t renderH,
                            long& outX, long& outY) noexcept
{
    if (clientW <= 0 || clientH <= 0 || !renderW || !renderH || x < 0 || y < 0 || x >= clientW || y >= clientH)
        return false;
    outX = static_cast<long>(std::llround(static_cast<double>(x) * renderW / clientW));
    outY = static_cast<long>(std::llround(static_cast<double>(y) * renderH / clientH));
    return true;
}
} // namespace fit_mouse
