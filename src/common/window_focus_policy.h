#pragma once
#include "runtime_types.h"
#include <windows.h>

enum class WindowFocusAction { PassThrough, KeepActive, SuppressLoss, ActivateMouse };
inline WindowFocusAction DecideWindowFocus(GameTitle title,RuntimeMode mode,UINT message) noexcept {
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
