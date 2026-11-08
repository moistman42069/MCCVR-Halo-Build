#pragma once
#include "runtime_types.h"
#include <windows.h>

enum class WindowFocusAction { PassThrough, KeepActive, SuppressLoss, ActivateMouse };
// `hostTracksFocus`: a host that keeps its game running out of focus and tracks focus its own way (Project Reclaimer
// 0.10). Hiding a focus loss from it left its game deaf to every control after a pause, a switch away and back, and
// the unpause; it gets the genuine transitions.
inline WindowFocusAction DecideWindowFocus(GameTitle title,RuntimeMode mode,UINT message,
                                           bool hostTracksFocus=false) noexcept {
    if(hostTracksFocus)return WindowFocusAction::PassThrough;
    const bool known=title==GameTitle::HaloCE||title==GameTitle::Halo2||title==GameTitle::Halo3||
        title==GameTitle::Halo3ODST||title==GameTitle::HaloReach||title==GameTitle::Halo4;
    // Native shell/authentication dialogs need genuine OS focus transitions.
    // Preserve established headset keepalive for every active gameplay family.
    if(!known||mode==RuntimeMode::Shell||mode==RuntimeMode::Unsupported)return WindowFocusAction::PassThrough;
    switch(message) {
    case WM_ACTIVATEAPP:case WM_ACTIVATE:case WM_NCACTIVATE:return WindowFocusAction::KeepActive;
    case WM_KILLFOCUS:return WindowFocusAction::SuppressLoss;
    case WM_MOUSEACTIVATE:return WindowFocusAction::ActivateMouse;
    default:return WindowFocusAction::PassThrough;
    }
}
