#pragma once
#include "scope_logic.h"
#include "vr_action_mapping.h"

// Scope toggles on release, so a cancelled/rebound input must never look like
// a deliberate release. Keep this admission state separate from the native
// button mapper: scope is a VR action even when no native zoom binding exists.
class ScopeActionInput
{
public:
    ScopeToggleUpdate Update(bool featureAvailable, uint32_t sources, int source,
        uint64_t epoch, unsigned route, bool admitted, bool cancelGesture) noexcept
    {
        const bool pressed = (sources & vr_mapping::Bit(source)) != 0;
        const bool boundary = !m_ready || source != m_source || epoch != m_epoch || route != m_route;
        const bool cancel = !featureAvailable || !admitted || cancelGesture || boundary;
        if (cancel) m_blocked = pressed;
        else m_blocked = m_blocked && pressed;
        m_ready = featureAvailable && admitted && !cancelGesture;
        m_source = source;
        m_epoch = epoch;
        m_route = route;
        return m_toggle.Update(featureAvailable, pressed && !m_blocked && m_ready, cancel);
    }

    // Tracking/controller gate loss cancels a pending release without changing
    // the scope's active state. The next valid sample must re-admit its source.
    void Suspend() noexcept
    {
        m_ready = false;
        m_toggle.Update(true, false, true);
    }

private:
    ScopeToggleDetector m_toggle;
    uint64_t m_epoch{};
    int m_source = vr_mapping::Unbound;
    unsigned m_route{};
    bool m_ready{}, m_blocked{};
};

// The thumb-rest route is exclusive: its published pad sample intentionally
// admits only D-pad directions. The head gesture also consumes the left click.
template<class Pad> uint32_t ScopeAndActionSources(const Pad& pad, bool headDpad) noexcept
{
    using namespace vr_mapping;
    uint32_t sources = Sources(pad);
    if (pad.thumbrestDpad)
    {
        if (pad.exclusiveInput) sources = 0;
        sources |= DpadSources(pad.dpadX, pad.dpadY);
    }
    if (headDpad && !pad.exclusiveInput)
    {
        sources |= DpadSources(pad.moveX, pad.moveY);
        sources &= ~Bit(LeftClick);
    }
    return sources;
}
