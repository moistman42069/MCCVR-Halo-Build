#pragma once
#include "physical_crouch_logic.h"
#include "runtime_types.h"

// Physical crouching is an additional VR gesture. It deliberately bypasses the
// configurable button source (including Unbound), but uses the same verified
// native crouch transport as a bound button. Native hold/toggle preferences
// remain the game's responsibility; this code never invents action flags.
// The standing reference is NOT this object's: one learner on the OpenXR frame
// thread owns it (PhysicalCrouchReference), so a title or level change re-arms
// the gesture without forgetting how tall the player stands.
class PhysicalCrouchInput
{
public:
    uint32_t Update(GameTitle title,uint32_t generation,float height,float baseline,uint64_t reference,
        uint64_t now,bool enabled,bool available,float depth,uint32_t nativeTransport) noexcept
    {
        if(!enabled||title!=m_title||generation!=m_generation)
        {
            m_gesture.Reset();
            m_title=title;
            m_generation=generation;
        }
        if(!enabled) return 0;
        // Keep the reference sample separate from the complete title
        // generation: XOR/packed truncation could miss simultaneous changes.
        const bool active=m_gesture.Update(height,baseline,reference,now,
            available&&generation!=0&&nativeTransport!=0,depth);
        return active?nativeTransport:0;
    }
    void Suspend(bool enabled) noexcept
    {
        if(!enabled) m_gesture.Reset();
        else (void)m_gesture.Update(0,0,0,0,false,.22f);
    }
    const PhysicalCrouch& Gesture() const noexcept {return m_gesture;}   // diagnostics only
private:
    PhysicalCrouch m_gesture;
    GameTitle m_title=GameTitle::None;
    uint32_t m_generation{};
};
