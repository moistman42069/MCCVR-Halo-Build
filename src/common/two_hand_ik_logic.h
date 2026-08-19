#pragma once

enum class SupportHandPoseOwner
{
    LeftController,
    WeaponCarrier,
};

// Two-hand aim uses the physical left controller to steer the weapon ray, but
// the visible support hand must retain the title-authored grip on the weapon.
// Free-hand mode continues to put that model hand directly on the controller.
inline constexpr SupportHandPoseOwner ResolveSupportHandPoseOwner(
    bool twoHandAimActive) noexcept
{
    return twoHandAimActive ? SupportHandPoseOwner::WeaponCarrier
                            : SupportHandPoseOwner::LeftController;
}

// Arm IK remains responsible for the shoulders and elbows in both modes. The
// two-hand difference is the LEFT wrist target selected above, not whether the
// entire body is rigidly carried by the right wrist. Disabling IK during a grab
// preserved the hand-to-gun relation but stretched the unsolved arm/body mesh
// across the view, most visibly with ODST's shotgun and SMG.
inline constexpr bool ShouldApplyArmIk(
    bool armIkConfigured, bool /*twoHandAimActive*/) noexcept
{
    return armIkConfigured;
}
