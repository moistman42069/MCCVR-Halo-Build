#pragma once
#include <atomic>
#include "dlss_logic.h"

namespace dlss
{
    // Loading is an engine resource transition, not an admitted resize point.
    // Paused and shell canvases still resize normally.
    inline bool CanDispatchLiveResize(RuntimeMode mode)
    {
        return mode != RuntimeMode::Loading;
    }

    // Render-thread producer, window-thread consumer. A queued plan must not
    // change the dimensions reported to MCC until the consumer admits it.
    class LiveResizeDispatch
    {
        std::atomic<int> phase_{0};
        RenderPlan queued_{};
    public:
        // Keep the existing public status values: idle/pending/complete/failed.
        // 4 and 5 are internal queued/dispatching phases.
        int Phase() const { return phase_.load(std::memory_order_acquire); }
        bool Pending() const { const int p = Phase(); return p == 1 || p == 4 || p == 5; }
        int Status() const { const int p = Phase(); return p >= 4 ? 1 : p; }
        bool Queue(const RenderPlan& plan, RuntimeMode mode)
        {
            if (!CanDispatchLiveResize(mode) || Pending()) return false;
            queued_ = plan;
            phase_.store(4, std::memory_order_release);
            return true;
        }
        bool Begin(RuntimeMode mode, RenderPlan& plan)
        {
            int expected = 4;
            if (!phase_.compare_exchange_strong(expected, 5,
                    std::memory_order_acq_rel)) return false;
            if (!CanDispatchLiveResize(mode))
            {
                phase_.store(0, std::memory_order_release);
                return false;
            }
            plan = queued_;
            return true;
        }
        void Applied() { phase_.store(1, std::memory_order_release); }
        void Complete() { phase_.store(2, std::memory_order_release); }
        void Failed() { phase_.store(3, std::memory_order_release); }
        void CancelQueued()
        {
            int expected = 4;
            phase_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
        }
    };
}
