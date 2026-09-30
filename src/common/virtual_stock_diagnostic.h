#pragma once

#include <atomic>
#include <cstdint>

enum class HybridDiagnosticOverride : uint8_t
{
    Normal = 0,
    ForceHip = 1,
    ForceStock = 2,
};

constexpr HybridDiagnosticOverride NormalizeHybridDiagnosticOverride(
    uint8_t value) noexcept
{
    switch (value)
    {
    case static_cast<uint8_t>(HybridDiagnosticOverride::Normal):
        return HybridDiagnosticOverride::Normal;
    case static_cast<uint8_t>(HybridDiagnosticOverride::ForceHip):
        return HybridDiagnosticOverride::ForceHip;
    case static_cast<uint8_t>(HybridDiagnosticOverride::ForceStock):
        return HybridDiagnosticOverride::ForceStock;
    default:
        return HybridDiagnosticOverride::Normal;
    }
}

class HybridDiagnosticOverrideState
{
public:
    HybridDiagnosticOverrideState() noexcept
        : value_(static_cast<uint8_t>(HybridDiagnosticOverride::Normal))
    {
    }

    HybridDiagnosticOverride Load() const noexcept
    {
        return NormalizeHybridDiagnosticOverride(
            value_.load(std::memory_order_acquire));
    }

    void Store(HybridDiagnosticOverride value) noexcept
    {
        value_.store(static_cast<uint8_t>(
            NormalizeHybridDiagnosticOverride(static_cast<uint8_t>(value))),
            std::memory_order_release);
    }

private:
    std::atomic<uint8_t> value_;
};
