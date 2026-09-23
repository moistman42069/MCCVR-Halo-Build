#pragma once
#include <cstdint>

namespace dlss
{
    // Render-thread mailbox for a borrowed result. Its resource owner must
    // Reset before releasing/replacing storage. No COM work or allocation.
    template<class T>
    class FrameResult
    {
        T value_{};
        uint64_t serial_ = 0;
    public:
        void Reset() noexcept { value_ = {}; serial_ = 0; }
        void Publish(uint64_t serial, const T& value) noexcept
        {
            value_ = serial ? value : T{};
            serial_ = serial;
        }
        bool Ready(uint64_t serial) const noexcept
        {
            return serial != 0 && serial_ == serial;
        }
        bool Take(uint64_t serial, T& out) noexcept
        {
            const bool ready = Ready(serial);
            out = ready ? value_ : T{};
            Reset();
            return ready;
        }
    };
}
