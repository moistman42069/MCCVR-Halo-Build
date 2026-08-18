#pragma once

// A planted support hand and analytic arm IK must not compete for ownership of
// the same visible weapon pose. While two-hand aim owns the weapon line, use
// the existing rigid wrist/weapon reconstruction; restore configured arm IK as
// soon as the two-hand grab ends.
inline constexpr bool ShouldApplyArmIk(
    bool armIkConfigured, bool twoHandAimActive) noexcept
{
    return armIkConfigured && !twoHandAimActive;
}
