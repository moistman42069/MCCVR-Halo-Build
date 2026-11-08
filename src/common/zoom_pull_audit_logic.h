#pragma once

// EACH PULL OF THE SUPPORT TRIGGER, AND WHAT CAME OF IT (input.cpp). On Project Reclaimer 0.10 a zoom pulled with the
// support hand on the gun went unanswered about one time in three (2026-11-07), and an unanswered pull left no line in
// the log. One line a pull: the hand's state as it began, what the mapper pressed while it lasted, and the game's zoom
// level before and after. A zoom pressed with the level unchanged is a press the game did not take; a grenade where a
// zoom was meant is the hand not counted on the gun.
// Pure logic; unit-tested in tests/core_tests.cpp.

#include <cstdint>

namespace zoom_pull_audit
{
inline constexpr uint64_t kAnswerMs = 500;

struct Report
{
    bool handOnGun, twoHanded, oneHandedGun;
    bool zoom, grenade, equipment;
    bool levelKnownBefore, levelKnownAfter;
    int levelBefore, levelAfter;
    uint64_t heldMs;
};

struct Audit
{
    bool open = false, triggerWas = false;
    uint64_t began = 0, released = 0;
    Report report{};

    // One poll. True when a pull's answer time has passed: `out` then holds its report.
    bool Step(uint64_t now, bool triggerDown, bool handOnGun, bool twoHanded, bool oneHandedGun, bool zoom,
              bool grenade, bool equipment, bool levelKnown, int level, Report& out) noexcept
    {
        const bool starts = triggerDown && !triggerWas;
        triggerWas = triggerDown;
        if (open)
        {
            if (triggerDown || !released)
            {
                report.zoom = report.zoom || zoom;
                report.grenade = report.grenade || grenade;
                report.equipment = report.equipment || equipment;
            }
            if (!triggerDown && !released)
                released = now;
            if (now - began >= kAnswerMs && !triggerDown)
            {
                report.levelKnownAfter = levelKnown;
                report.levelAfter = level;
                report.heldMs = (released ? released : now) - began;
                out = report;
                open = false;
                // A new pull on this very poll starts its own report below.
                if (!starts)
                    return true;
                Begin(now, handOnGun, twoHanded, oneHandedGun, zoom, grenade, equipment, levelKnown, level);
                return true;
            }
            return false;
        }
        if (starts)
            Begin(now, handOnGun, twoHanded, oneHandedGun, zoom, grenade, equipment, levelKnown, level);
        return false;
    }

    // Whether the report shows a zoom pressed that the game's zoom level never answered.
    static constexpr bool Unanswered(const Report& r) noexcept
    {
        return r.zoom && r.levelKnownBefore && r.levelKnownAfter && r.levelBefore == r.levelAfter;
    }

private:
    void Begin(uint64_t now, bool handOnGun, bool twoHanded, bool oneHandedGun, bool zoom, bool grenade,
               bool equipment, bool levelKnown, int level) noexcept
    {
        open = true;
        began = now;
        released = 0;
        report = Report{handOnGun, twoHanded, oneHandedGun, zoom, grenade, equipment, levelKnown, false, level, -1, 0};
    }
};
} // namespace zoom_pull_audit
