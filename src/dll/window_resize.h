#pragma once
#include <windows.h>

// UI-thread state only. Fitting a window emits WM_SIZE synchronously through
// SetWindowPos/DefWindowProc. Batch those intermediate notifications before
// delivering the final render-size notification to MCC once.
class WindowResizeBatch
{
    HWND fitting_ = nullptr;
public:
    bool SuppressesSize(HWND window) const
    {
        return window && window == fitting_;
    }

    template<class Fit, class Notify>
    void Apply(HWND window, Fit fit, Notify notify)
    {
        {
            struct Scope
            {
                HWND& current;
                HWND previous;
                ~Scope() { current = previous; }
            } scope{fitting_, fitting_};
            fitting_ = window;
            fit();
        }
        notify();
    }
};
