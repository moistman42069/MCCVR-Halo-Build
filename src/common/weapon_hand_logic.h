#pragma once
#include <cstdint>
#include <atomic>

// A successful local secondary-weapon presentation is enough to forbid a
// support-hand grab. It is NOT inventory authority for aiming or firing.
// One atomic publishes generation and low clock word together; unsigned age
// handles the 49-day clock wrap. Stale title generations never carry a grab.
class RecentSecondaryWeaponPresentation
{
public:
    void Publish(uint32_t generation, uint64_t nowMs) noexcept
    {
        if (generation)
            stamp.store((uint64_t(generation) << 32) | uint32_t(nowMs),
                        std::memory_order_release);
    }
    bool Active(uint32_t generation, uint64_t nowMs) const noexcept
    {
        const uint64_t value = stamp.load(std::memory_order_acquire);
        return generation && uint32_t(value >> 32) == generation &&
            uint32_t(uint32_t(nowMs) - uint32_t(value)) <= 150;
    }
private:
    std::atomic<uint64_t> stamp{0};
};

// After dual wield or a role change, consuming a held grip must not silently
// turn it into a new support grab. Both hold and toggle modes require release.
//
// The durable persistent-support-grip writer shares this one state instead of
// keeping a second copy: it arms on a proven weapon absence, a generation
// replacement or an interaction teardown (all of which must survive the Grip
// still being held) and mirrors the result back, while the physical release
// edge above remains the only thing that clears it. A proven owner REPLACEMENT
// deliberately does not arm: the switched weapon is re-acquirable while the
// Grip stays held.
struct SupportGripAdmission
{
    bool requiresRelease = false;
    bool Observe(bool gripHeld, bool independentWeapons, bool rolesChanged) noexcept
    {
        if (independentWeapons || rolesChanged) requiresRelease = true;
        if (!gripHeld) requiresRelease = false;
        return !independentWeapons && !rolesChanged && !requiresRelease;
    }
    // The durable writer's arming edge. Never clears: only Observe does, on a
    // physical Grip release.
    void Arm() noexcept { requiresRelease = true; }
};

// Two-handed aim is a first-person feature. In a vehicle seat or walk-up turret
// (every title) the aim belongs to the primary controller - the configured aim
// hand - alone: players board expecting that hand to steer the seat.
// vr.cpp feeds this gate fp_view::Gate's verdict (any view that is
// not the player's own first person: first_person_view_logic.h), so "seated"
// below reads "out of first person".
//
// The seat is one more admission blocker beside dual wield and a role change,
// so it shares SupportGripAdmission's release-before-reacquire: an engaged
// hold drops on the first seated update, and a Grip still held when the player
// leaves the seat (a wheel grip, say) cannot re-engage until it is released
// and squeezed again - no surprise snap toward the support hand on exit.
//
// The drop must also skip the 200 ms release blend, or the aim would ease
// away from the support hand after boarding. `dropPresentationHistory` says
// when: on the seat's entry edge, and whenever a hold actually drops while
// seated (a pose outage defers the drop until tracking returns).
struct SeatedSupportGate
{
    bool seated = false;
    struct Step
    {
        bool blocksSupport = false;
        bool enteredSeat = false;
    };
    Step Observe(bool seatedNow) noexcept
    {
        const Step step{seatedNow, seatedNow && !seated};
        seated = seatedNow;
        return step;
    }
    static constexpr bool DropPresentationHistory(const Step& step,
        bool latchedBefore, bool latchedAfter) noexcept
    {
        return step.enteredSeat ||
            (step.blocksSupport && latchedBefore && !latchedAfter);
    }
};

// Full engine handles only. Inventory slots are engine roles: primary=0,
// secondary=1. The physical controller for each role is a user preference.
constexpr int ResolveEquippedWeaponSlot(uint32_t weapon, uint32_t primary,
    uint32_t secondary, bool primaryValid, bool secondaryValid) noexcept
{
    if (weapon == UINT32_MAX || !(weapon >> 16) || primary == secondary)
        return -1;
    if (primaryValid && primary == weapon) return 0;
    if (primaryValid && secondaryValid && secondary == weapon) return 1;
    return -1;
}

constexpr int PhysicalHandForWeaponSlot(int slot, bool leftHanded) noexcept
{
    return slot == 0 ? (leftHanded ? 0 : 1) :
           slot == 1 ? (leftHanded ? 1 : 0) : -1;
}
