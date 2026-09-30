#pragma once

#include "virtual_stock_settings.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

enum class VirtualStockTestProfile : uint8_t
{
    Custom = 0,
    A_VsOffControl = 1,
    B_FixedHeadControl = 2,
    C_FixedShoulderControl = 3,
    D_HybridBaseline = 4,
    E_HybridMaxSeat = 5,
    F_HybridForceStockHead = 6,
    G_HybridForceStockShoulder = 7,
    H_HybridForceHip50 = 8,
    I_HybridForceHip100 = 9,
    J_HybridSeat_464_474 = 10,
    K_HybridSeat_440_474 = 11,
    L_HybridSeat_420_474 = 12,
    M_HybridSeat_400_474 = 13,
    N_HybridSeat_380_474 = 14,
};

inline constexpr uint8_t kVirtualStockCoreProfileLastId = 14;
inline constexpr uint8_t kVirtualStockExperimentProfileFirstId = 128;
inline constexpr uint8_t kVirtualStockExperimentProfileLastId = 254;
inline constexpr uint8_t kVirtualStockInvalidProfileId = 255;
inline constexpr size_t kVirtualStockExperimentProfileCapacity =
    static_cast<size_t>(kVirtualStockExperimentProfileLastId) -
    static_cast<size_t>(kVirtualStockExperimentProfileFirstId) + 1;

enum class VirtualStockTestProfileRole : uint8_t
{
    None = 0,
    VsOffControl,
    FixedHeadControl,
    FixedShoulderControl,
    Tuning,
    Diagnostic,
};

struct VirtualStockTestProfileDefinition
{
    VirtualStockTestProfile id = VirtualStockTestProfile::Custom;
    const char* stableName = "Custom";
    const char* uiLabel = "Custom";
    VirtualStockTestProfileRole role = VirtualStockTestProfileRole::None;
    bool useCustomSettings = true;
    VirtualStockAimSettings settings{};
};

struct VirtualStockExperimentProfileSpec
{
    const char* stableName = nullptr;
    const char* uiLabel = nullptr;
    VirtualStockAimSettings settings{};
};

constexpr VirtualStockAimSettings MakeVsOffControlSettings() noexcept
{
    VirtualStockAimSettings settings = CanonicalVirtualStockAimSettings();
    settings.virtualStockProximityRelease = false;
    return settings;
}

constexpr VirtualStockAimSettings MakeFixedControlSettings(
    int rearReference) noexcept
{
    VirtualStockAimSettings settings = CanonicalVirtualStockAimSettings();
    settings.virtualStockEnabled = true;
    settings.virtualStockRearReference = rearReference;
    return settings;
}

constexpr VirtualStockAimSettings MakeHybridProfileSettings(
    float seatFullM = kVirtualStockHybridSeatFullDefaultM,
    float seatReleaseM = kVirtualStockHybridSeatReleaseDefaultM,
    HybridDiagnosticOverride diagnostic = HybridDiagnosticOverride::Normal,
    int adsReference = kVirtualStockHybridAdsReferenceDefault,
    float offhandInfluence = kVirtualStockHybridOffhandInfluenceDefault) noexcept
{
    VirtualStockAimSettings settings = CanonicalVirtualStockAimSettings();
    settings.virtualStockEnabled = true;
    settings.virtualStockRearReference = 3;
    settings.virtualStockHybridOffhandInfluence = offhandInfluence;
    settings.virtualStockHybridAdsReference = adsReference;
    settings.virtualStockHybridSeatFullM = seatFullM;
    settings.virtualStockHybridSeatReleaseM = seatReleaseM;
    settings.hybridDiagnosticOverride = diagnostic;
    settings.virtualStockProximityRelease = false;
    return settings;
}

constexpr VirtualStockAimSettings MakeHybridTuningProfileSettings(
    float seatFullM, float seatReleaseM) noexcept
{
    return MakeHybridProfileSettings(seatFullM, seatReleaseM);
}

constexpr VirtualStockAimSettings MakeHybridHorizontalReleaseProfileSettings(
    float fullM, float releaseM) noexcept
{
    VirtualStockAimSettings settings = MakeHybridProfileSettings(
        kVirtualStockHybridSeatFullDefaultM,
        kVirtualStockHybridSeatReleaseDefaultM,
        HybridDiagnosticOverride::ForceStock,
        kVirtualStockHybridAdsReferenceDefault,
        kVirtualStockHybridOffhandInfluenceDefault);
    settings.hybridHorizontalRearReleaseEnabled = true;
    settings.hybridHorizontalRearReleaseFullM = fullM;
    settings.hybridHorizontalRearReleaseReleaseM = releaseM;
    return settings;
}

constexpr VirtualStockAimSettings MakeHybridInverseNeckY3ProfileSettings(
    float correctionStrength, float forwardM, float upM,
    float lateralM) noexcept
{
    VirtualStockAimSettings settings =
        MakeHybridHorizontalReleaseProfileSettings(0.330f, 0.475f);
    settings.virtualStockStrength = 0.80f;
    settings.hybridInverseNeckEnabled = true;
    settings.hybridInverseNeckStrength = correctionStrength;
    settings.hybridInverseNeckForwardM = forwardM;
    settings.hybridInverseNeckUpM = upM;
    settings.hybridInverseNeckLateralM = lateralM;
    return settings;
}

constexpr VirtualStockAimSettings
MakeHybridInverseNeckY3ProductionCandidateSettings(
    float correctionStrength, float forwardM, float upM,
    float lateralM) noexcept
{
    VirtualStockAimSettings settings =
        MakeHybridInverseNeckY3ProfileSettings(
            correctionStrength, forwardM, upM, lateralM);

    // Remove only the ForceStock diagnostic override. Restore the already
    // researched broad seat gate as a last-resort geometry/sanity envelope.
    settings.hybridDiagnosticOverride = HybridDiagnosticOverride::Normal;
    settings.virtualStockHybridSeatFullM = 0.464f;
    settings.virtualStockHybridSeatReleaseM = 0.474f;
    return settings;
}

inline constexpr std::array kVirtualStockCoreTestProfileDefinitions = {
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::Custom,
        "Custom",
        "Custom",
        VirtualStockTestProfileRole::None,
        true,
        CanonicalVirtualStockAimSettings()},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::A_VsOffControl,
        "A_VsOffControl",
        "A - VS OFF Control",
        VirtualStockTestProfileRole::VsOffControl,
        false,
        MakeVsOffControlSettings()},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::B_FixedHeadControl,
        "B_FixedHeadControl",
        "B - Fixed Head Control",
        VirtualStockTestProfileRole::FixedHeadControl,
        false,
        MakeFixedControlSettings(0)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::C_FixedShoulderControl,
        "C_FixedShoulderControl",
        "C - Fixed Shoulder Control",
        VirtualStockTestProfileRole::FixedShoulderControl,
        false,
        MakeFixedControlSettings(1)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::D_HybridBaseline,
        "D_HybridBaseline",
        "D - Hybrid Baseline",
        VirtualStockTestProfileRole::Tuning,
        false,
        MakeHybridProfileSettings()},
    // This is a deterministic maximum-valid stress condition, not a claim to
    // reproduce the earlier approximate headset observation.
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::E_HybridMaxSeat,
        "E_HybridMaxSeat",
        "E - Hybrid Max Seat",
        VirtualStockTestProfileRole::Tuning,
        false,
        MakeHybridProfileSettings(
            kVirtualStockHybridSeatFullMaximumM,
            kVirtualStockHybridSeatReleaseMaximumM)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::F_HybridForceStockHead,
        "F_HybridForceStockHead",
        "F - Hybrid Force Stock Head",
        VirtualStockTestProfileRole::Diagnostic,
        false,
        MakeHybridProfileSettings(
            kVirtualStockHybridSeatFullDefaultM,
            kVirtualStockHybridSeatReleaseDefaultM,
            HybridDiagnosticOverride::ForceStock)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::G_HybridForceStockShoulder,
        "G_HybridForceStockShoulder",
        "G - Hybrid Force Stock Shoulder",
        VirtualStockTestProfileRole::Diagnostic,
        false,
        MakeHybridProfileSettings(
            kVirtualStockHybridSeatFullDefaultM,
            kVirtualStockHybridSeatReleaseDefaultM,
            HybridDiagnosticOverride::ForceStock,
            1)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::H_HybridForceHip50,
        "H_HybridForceHip50",
        "H - Hybrid Force Hip 50%",
        VirtualStockTestProfileRole::Diagnostic,
        false,
        MakeHybridProfileSettings(
            kVirtualStockHybridSeatFullDefaultM,
            kVirtualStockHybridSeatReleaseDefaultM,
            HybridDiagnosticOverride::ForceHip)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::I_HybridForceHip100,
        "I_HybridForceHip100",
        "I - Hybrid Force Hip 100%",
        VirtualStockTestProfileRole::Diagnostic,
        false,
        MakeHybridProfileSettings(
            kVirtualStockHybridSeatFullDefaultM,
            kVirtualStockHybridSeatReleaseDefaultM,
            HybridDiagnosticOverride::ForceHip,
            kVirtualStockHybridAdsReferenceDefault,
            1.0f)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::J_HybridSeat_464_474,
        "J_HybridSeat_464_474",
        "J - Hybrid Seat 0.464 / 0.474",
        VirtualStockTestProfileRole::Tuning,
        false,
        MakeHybridTuningProfileSettings(0.464f, 0.474f)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::K_HybridSeat_440_474,
        "K_HybridSeat_440_474",
        "K - Hybrid Seat 0.440 / 0.474",
        VirtualStockTestProfileRole::Tuning,
        false,
        MakeHybridTuningProfileSettings(0.440f, 0.474f)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::L_HybridSeat_420_474,
        "L_HybridSeat_420_474",
        "L - Hybrid Seat 0.420 / 0.474",
        VirtualStockTestProfileRole::Tuning,
        false,
        MakeHybridTuningProfileSettings(0.420f, 0.474f)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::M_HybridSeat_400_474,
        "M_HybridSeat_400_474",
        "M - Hybrid Seat 0.400 / 0.474",
        VirtualStockTestProfileRole::Tuning,
        false,
        MakeHybridTuningProfileSettings(0.400f, 0.474f)},
    VirtualStockTestProfileDefinition{
        VirtualStockTestProfile::N_HybridSeat_380_474,
        "N_HybridSeat_380_474",
        "N - Hybrid Seat 0.380 / 0.474",
        VirtualStockTestProfileRole::Tuning,
        false,
        MakeHybridTuningProfileSettings(0.380f, 0.474f)},
};

inline constexpr size_t kVirtualStockCoreTestProfileCount =
    kVirtualStockCoreTestProfileDefinitions.size();

inline constexpr size_t kVirtualStockExperimentProfileCount = 0
#define MCCVR_EXPERIMENT_PROFILE(stableName, uiLabel, settings) + 1
#include "virtual_stock_test_profiles_experiments.inc"
#undef MCCVR_EXPERIMENT_PROFILE
    ;

inline constexpr std::array<
    VirtualStockExperimentProfileSpec, kVirtualStockExperimentProfileCount>
    kVirtualStockExperimentProfileSpecs = {{
#define MCCVR_EXPERIMENT_PROFILE(stableName, uiLabel, settings) \
        VirtualStockExperimentProfileSpec{stableName, uiLabel, settings},
#include "virtual_stock_test_profiles_experiments.inc"
#undef MCCVR_EXPERIMENT_PROFILE
    }};

template <size_t ExperimentCount>
constexpr auto MergeVirtualStockTestProfiles(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    std::array<VirtualStockTestProfileDefinition,
        kVirtualStockCoreTestProfileCount + ExperimentCount> merged{};
    for (size_t index = 0; index < kVirtualStockCoreTestProfileCount; ++index)
        merged[index] = kVirtualStockCoreTestProfileDefinitions[index];
    for (size_t index = 0; index < ExperimentCount; ++index)
    {
        const auto& experiment = experiments[index];
        merged[kVirtualStockCoreTestProfileCount + index] = {
            static_cast<VirtualStockTestProfile>(
                static_cast<uint8_t>(kVirtualStockExperimentProfileFirstId +
                    index)),
            experiment.stableName,
            experiment.uiLabel,
            VirtualStockTestProfileRole::Tuning,
            false,
            experiment.settings};
    }
    return merged;
}

inline constexpr auto kVirtualStockTestProfileDefinitions =
    MergeVirtualStockTestProfiles(kVirtualStockExperimentProfileSpecs);

inline constexpr size_t kVirtualStockTestProfileCount =
    kVirtualStockTestProfileDefinitions.size();

constexpr bool VirtualStockTestProfileStringEqual(
    const char* left, const char* right) noexcept
{
    while (*left != '\0' && *right != '\0')
    {
        if (*left++ != *right++)
            return false;
    }
    return *left == *right;
}

constexpr bool VirtualStockExperimentProfileCountIsValid(
    size_t count) noexcept
{
    return count <= kVirtualStockExperimentProfileCapacity;
}

constexpr bool VirtualStockSettingInRange(
    float value, float minimum, float maximum) noexcept
{
    return value >= minimum && value <= maximum;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentProfileNamesAreNonEmpty(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        if (experiment.stableName == nullptr || experiment.stableName[0] == '\0')
            return false;
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentProfileLabelsAreNonEmpty(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        if (experiment.uiLabel == nullptr || experiment.uiLabel[0] == '\0')
            return false;
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentProfileNamesAreUnique(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (size_t left = 0; left < ExperimentCount; ++left)
    {
        for (size_t right = left + 1; right < ExperimentCount; ++right)
        {
            if (experiments[left].stableName != nullptr &&
                experiments[right].stableName != nullptr &&
                VirtualStockTestProfileStringEqual(
                    experiments[left].stableName,
                    experiments[right].stableName))
            {
                return false;
            }
        }
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentProfileLabelsAreUnique(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (size_t left = 0; left < ExperimentCount; ++left)
    {
        for (size_t right = left + 1; right < ExperimentCount; ++right)
        {
            if (experiments[left].uiLabel != nullptr &&
                experiments[right].uiLabel != nullptr &&
                VirtualStockTestProfileStringEqual(
                    experiments[left].uiLabel,
                    experiments[right].uiLabel))
            {
                return false;
            }
        }
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentProfileLabelsDoNotCollideWithCore(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        for (const auto& core : kVirtualStockCoreTestProfileDefinitions)
        {
            if (experiment.uiLabel != nullptr &&
                VirtualStockTestProfileStringEqual(
                    experiment.uiLabel, core.uiLabel))
            {
                return false;
            }
        }
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentProfileNamesDoNotCollideWithCore(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        for (const auto& core : kVirtualStockCoreTestProfileDefinitions)
        {
            if (experiment.stableName != nullptr &&
                VirtualStockTestProfileStringEqual(
                    experiment.stableName, core.stableName))
            {
                return false;
            }
        }
    }
    return true;
}

constexpr bool VirtualStockExperimentScalarSettingsAreValid(
    const VirtualStockAimSettings& settings) noexcept
{
    return VirtualStockSettingInRange(settings.virtualStockStrength,
               kVirtualStockStrengthMinimum,
               kVirtualStockStrengthMaximum) &&
        VirtualStockSettingInRange(settings.virtualStockRearHeightM,
            kVirtualStockRearHeightMinimumM,
            kVirtualStockRearHeightMaximumM) &&
        settings.virtualStockRearReference >=
            kVirtualStockRearReferenceMinimum &&
        settings.virtualStockRearReference <=
            kVirtualStockRearReferenceMaximum &&
        VirtualStockSettingInRange(settings.virtualStockShoulderBackM,
            kVirtualStockShoulderBackMinimumM,
            kVirtualStockShoulderBackMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockShoulderSideM,
            kVirtualStockShoulderSideMinimumM,
            kVirtualStockShoulderSideMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockChestHeightM,
            kVirtualStockChestHeightMinimumM,
            kVirtualStockChestHeightMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockChestBackM,
            kVirtualStockChestBackMinimumM,
            kVirtualStockChestBackMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockChestSideM,
            kVirtualStockChestSideMinimumM,
            kVirtualStockChestSideMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockAdaptiveTopHeightM,
            kVirtualStockAdaptiveTopHeightMinimumM,
            kVirtualStockAdaptiveTopHeightMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockAdaptiveBottomHeightM,
            kVirtualStockAdaptiveBottomHeightMinimumM,
            kVirtualStockAdaptiveBottomHeightMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockAdaptiveTopHalfWidthM,
            kVirtualStockAdaptiveTopHalfWidthMinimumM,
            kVirtualStockAdaptiveTopHalfWidthMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockAdaptiveBottomHalfWidthM,
            kVirtualStockAdaptiveBottomHalfWidthMinimumM,
            kVirtualStockAdaptiveBottomHalfWidthMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockHybridOffhandInfluence,
            kVirtualStockHybridOffhandInfluenceMinimum,
            kVirtualStockHybridOffhandInfluenceMaximum) &&
        settings.virtualStockHybridAdsReference >=
            kVirtualStockHybridAdsReferenceMinimum &&
        settings.virtualStockHybridAdsReference <=
            kVirtualStockHybridAdsReferenceMaximum &&
        static_cast<uint8_t>(settings.hybridDiagnosticOverride) <=
            static_cast<uint8_t>(HybridDiagnosticOverride::ForceStock) &&
        VirtualStockSettingInRange(settings.hybridInverseNeckStrength,
            kVirtualStockHybridInverseNeckStrengthMinimum,
            kVirtualStockHybridInverseNeckStrengthMaximum) &&
        VirtualStockSettingInRange(settings.hybridInverseNeckForwardM,
            kVirtualStockHybridInverseNeckForwardMinimumM,
            kVirtualStockHybridInverseNeckForwardMaximumM) &&
        VirtualStockSettingInRange(settings.hybridInverseNeckUpM,
            kVirtualStockHybridInverseNeckUpMinimumM,
            kVirtualStockHybridInverseNeckUpMaximumM) &&
        VirtualStockSettingInRange(settings.hybridInverseNeckLateralM,
            kVirtualStockHybridInverseNeckLateralMinimumM,
            kVirtualStockHybridInverseNeckLateralMaximumM);
}

constexpr bool VirtualStockExperimentSeatSettingsAreValid(
    const VirtualStockAimSettings& settings) noexcept
{
    return VirtualStockSettingInRange(settings.virtualStockHybridSeatFullM,
               kVirtualStockHybridSeatFullMinimumM,
               kVirtualStockHybridSeatFullMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockHybridSeatReleaseM,
            kVirtualStockHybridSeatReleaseMinimumM,
            kVirtualStockHybridSeatReleaseMaximumM) &&
        settings.virtualStockHybridSeatReleaseM -
                settings.virtualStockHybridSeatFullM >=
            kVirtualStockHybridSeatMinimumSeparationM;
}

constexpr bool VirtualStockExperimentHorizontalReleaseSettingsAreValid(
    const VirtualStockAimSettings& settings) noexcept
{
    return VirtualStockSettingInRange(
               settings.hybridHorizontalRearReleaseFullM,
               kVirtualStockProximityFullMinimumM,
               kVirtualStockProximityFullMaximumM) &&
        VirtualStockSettingInRange(
            settings.hybridHorizontalRearReleaseReleaseM,
            kVirtualStockProximityReleaseMinimumM,
            kVirtualStockProximityReleaseMaximumM) &&
        settings.hybridHorizontalRearReleaseReleaseM -
                settings.hybridHorizontalRearReleaseFullM >=
            kVirtualStockProximityMinimumSeparationM;
}

constexpr bool VirtualStockExperimentProximitySettingsAreValid(
    const VirtualStockAimSettings& settings) noexcept
{
    return VirtualStockSettingInRange(settings.virtualStockProximityFullM,
               kVirtualStockProximityFullMinimumM,
               kVirtualStockProximityFullMaximumM) &&
        VirtualStockSettingInRange(settings.virtualStockProximityReleaseM,
            kVirtualStockProximityReleaseMinimumM,
            kVirtualStockProximityReleaseMaximumM) &&
        settings.virtualStockProximityReleaseM -
                settings.virtualStockProximityFullM >=
            kVirtualStockProximityMinimumSeparationM;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentScalarSettingsAreValid(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        if (!VirtualStockExperimentScalarSettingsAreValid(experiment.settings))
            return false;
    }
    return true;
}

template <size_t ExperimentCount, typename Value>
constexpr bool VirtualStockExperimentSettingIsInRange(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments,
    Value VirtualStockAimSettings::*member,
    Value minimum, Value maximum) noexcept
{
    for (const auto& experiment : experiments)
    {
        const Value value = experiment.settings.*member;
        if (value < minimum || value > maximum)
            return false;
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentSettingSeparationIsValid(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments,
    float VirtualStockAimSettings::*fullMember,
    float VirtualStockAimSettings::*releaseMember,
    float minimumSeparation) noexcept
{
    for (const auto& experiment : experiments)
    {
        if (experiment.settings.*releaseMember -
                experiment.settings.*fullMember < minimumSeparation)
        {
            return false;
        }
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentDiagnosticOverridesAreValid(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        if (static_cast<uint8_t>(
                experiment.settings.hybridDiagnosticOverride) >
            static_cast<uint8_t>(HybridDiagnosticOverride::ForceStock))
        {
            return false;
        }
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentSeatSettingsAreValid(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        if (!VirtualStockExperimentSeatSettingsAreValid(experiment.settings))
            return false;
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentHorizontalReleaseSettingsAreValid(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        if (!VirtualStockExperimentHorizontalReleaseSettingsAreValid(
                experiment.settings))
        {
            return false;
        }
    }
    return true;
}

template <size_t ExperimentCount>
constexpr bool VirtualStockExperimentProximitySettingsAreValid(
    const std::array<VirtualStockExperimentProfileSpec, ExperimentCount>&
        experiments) noexcept
{
    for (const auto& experiment : experiments)
    {
        if (!VirtualStockExperimentProximitySettingsAreValid(
                experiment.settings))
        {
            return false;
        }
    }
    return true;
}

template <size_t DefinitionCount>
constexpr const VirtualStockTestProfileDefinition*
FindVirtualStockTestProfileDefinitionInRegistry(
    const std::array<VirtualStockTestProfileDefinition, DefinitionCount>&
        definitions,
    VirtualStockTestProfile profile) noexcept
{
    for (const auto& definition : definitions)
    {
        if (definition.id == profile)
            return &definition;
    }
    return nullptr;
}

constexpr const VirtualStockTestProfileDefinition*
FindVirtualStockTestProfileDefinition(VirtualStockTestProfile profile) noexcept
{
    return FindVirtualStockTestProfileDefinitionInRegistry(
        kVirtualStockTestProfileDefinitions, profile);
}

constexpr const VirtualStockTestProfileDefinition&
VirtualStockTestProfileDefinitionAt(size_t index) noexcept
{
    return index < kVirtualStockTestProfileDefinitions.size()
        ? kVirtualStockTestProfileDefinitions[index]
        : kVirtualStockTestProfileDefinitions[0];
}

constexpr size_t VirtualStockTestProfileIndex(
    VirtualStockTestProfile profile) noexcept
{
    for (size_t index = 0;
         index < kVirtualStockTestProfileDefinitions.size(); ++index)
    {
        if (kVirtualStockTestProfileDefinitions[index].id == profile)
            return index;
    }
    return 0;
}

constexpr VirtualStockTestProfile NormalizeVirtualStockTestProfile(
    uint8_t value) noexcept
{
    const auto profile = static_cast<VirtualStockTestProfile>(value);
    return FindVirtualStockTestProfileDefinition(profile)
        ? profile : VirtualStockTestProfile::Custom;
}

constexpr const char* VirtualStockTestProfileRoleName(
    VirtualStockTestProfileRole role) noexcept
{
    switch (role)
    {
    case VirtualStockTestProfileRole::None:
        return "none";
    case VirtualStockTestProfileRole::VsOffControl:
        return "vs_off_control";
    case VirtualStockTestProfileRole::FixedHeadControl:
        return "fixed_head_control";
    case VirtualStockTestProfileRole::FixedShoulderControl:
        return "fixed_shoulder_control";
    case VirtualStockTestProfileRole::Tuning:
        return "tuning";
    case VirtualStockTestProfileRole::Diagnostic:
        return "diagnostic";
    }
    return "none";
}

constexpr size_t VirtualStockTestProfileRoleCount(
    VirtualStockTestProfileRole role) noexcept
{
    size_t count = 0;
    for (const auto& definition : kVirtualStockTestProfileDefinitions)
    {
        if (definition.role == role)
            ++count;
    }
    return count;
}

constexpr VirtualStockTestProfile ProfileForRole(
    VirtualStockTestProfileRole role) noexcept
{
    for (const auto& definition : kVirtualStockTestProfileDefinitions)
    {
        if (definition.role == role)
            return definition.id;
    }
    return VirtualStockTestProfile::Custom;
}

inline constexpr VirtualStockTestProfile kVsOffControlProfile =
    ProfileForRole(VirtualStockTestProfileRole::VsOffControl);
inline constexpr VirtualStockTestProfile kFixedHeadControlProfile =
    ProfileForRole(VirtualStockTestProfileRole::FixedHeadControl);
inline constexpr VirtualStockTestProfile kFixedShoulderControlProfile =
    ProfileForRole(VirtualStockTestProfileRole::FixedShoulderControl);

template <size_t DefinitionCount>
constexpr bool VirtualStockTestProfileRegistryIsValid(
    const std::array<VirtualStockTestProfileDefinition, DefinitionCount>&
        definitions,
    size_t coreCount) noexcept
{
    if (coreCount != kVirtualStockCoreTestProfileCount ||
        DefinitionCount < coreCount ||
        !VirtualStockExperimentProfileCountIsValid(DefinitionCount - coreCount))
    {
        return false;
    }

    size_t vsOffControls = 0;
    size_t fixedHeadControls = 0;
    size_t fixedShoulderControls = 0;
    for (size_t left = 0; left < DefinitionCount; ++left)
    {
        const auto& definition = definitions[left];
        const uint8_t id = static_cast<uint8_t>(definition.id);
        if (definition.stableName == nullptr || definition.stableName[0] == '\0' ||
            definition.uiLabel == nullptr || definition.uiLabel[0] == '\0' ||
            definition.useCustomSettings !=
                (definition.id == VirtualStockTestProfile::Custom))
        {
            return false;
        }

        if (left < coreCount)
        {
            if (id != left || id > kVirtualStockCoreProfileLastId)
                return false;
        }
        else
        {
            const size_t experimentIndex = left - coreCount;
            if (id != kVirtualStockExperimentProfileFirstId + experimentIndex ||
                id > kVirtualStockExperimentProfileLastId ||
                definition.role != VirtualStockTestProfileRole::Tuning ||
                definition.useCustomSettings)
            {
                return false;
            }
        }

        vsOffControls += definition.role ==
            VirtualStockTestProfileRole::VsOffControl;
        fixedHeadControls += definition.role ==
            VirtualStockTestProfileRole::FixedHeadControl;
        fixedShoulderControls += definition.role ==
            VirtualStockTestProfileRole::FixedShoulderControl;

        for (size_t right = left + 1; right < DefinitionCount; ++right)
        {
            const auto& other = definitions[right];
            if (other.stableName == nullptr ||
                definition.id == other.id ||
                VirtualStockTestProfileStringEqual(
                    definition.stableName, other.stableName))
            {
                return false;
            }
        }
    }
    return FindVirtualStockTestProfileDefinitionInRegistry(
               definitions, VirtualStockTestProfile::Custom) != nullptr &&
        vsOffControls == 1 && fixedHeadControls == 1 &&
        fixedShoulderControls == 1;
}

constexpr bool VirtualStockTestProfileRegistryIsValid() noexcept
{
    return VirtualStockTestProfileRegistryIsValid(
        kVirtualStockTestProfileDefinitions,
        kVirtualStockCoreTestProfileCount);
}

static_assert(VirtualStockExperimentProfileCountIsValid(
        kVirtualStockExperimentProfileCount),
    "Temporary Virtual Stock profile pack exceeds IDs 128-254");
static_assert(VirtualStockExperimentProfileNamesAreNonEmpty(
        kVirtualStockExperimentProfileSpecs),
    "Temporary Virtual Stock profile stable names must not be empty");
static_assert(VirtualStockExperimentProfileLabelsAreNonEmpty(
        kVirtualStockExperimentProfileSpecs),
    "Temporary Virtual Stock profile UI labels must not be empty");
static_assert(VirtualStockExperimentProfileNamesAreUnique(
        kVirtualStockExperimentProfileSpecs),
    "Temporary Virtual Stock profile stable names must be unique");
static_assert(VirtualStockExperimentProfileLabelsAreUnique(
        kVirtualStockExperimentProfileSpecs),
    "Temporary Virtual Stock profile UI labels must be unique");
static_assert(VirtualStockExperimentProfileLabelsDoNotCollideWithCore(
        kVirtualStockExperimentProfileSpecs),
    "Temporary Virtual Stock profile UI labels must not collide with Custom or A-N");
static_assert(VirtualStockExperimentProfileNamesDoNotCollideWithCore(
        kVirtualStockExperimentProfileSpecs),
    "Temporary Virtual Stock profile stable names must not collide with Custom or A-N");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockStrength,
        kVirtualStockStrengthMinimum, kVirtualStockStrengthMaximum),
    "Experiment virtual-stock strength is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockRearHeightM,
        kVirtualStockRearHeightMinimumM, kVirtualStockRearHeightMaximumM),
    "Experiment rear height is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockRearReference,
        kVirtualStockRearReferenceMinimum, kVirtualStockRearReferenceMaximum),
    "Experiment rear reference is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockShoulderBackM,
        kVirtualStockShoulderBackMinimumM, kVirtualStockShoulderBackMaximumM),
    "Experiment shoulder-back is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockShoulderSideM,
        kVirtualStockShoulderSideMinimumM, kVirtualStockShoulderSideMaximumM),
    "Experiment shoulder-side is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockChestHeightM,
        kVirtualStockChestHeightMinimumM, kVirtualStockChestHeightMaximumM),
    "Experiment chest-height is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockChestBackM,
        kVirtualStockChestBackMinimumM, kVirtualStockChestBackMaximumM),
    "Experiment chest-back is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockChestSideM,
        kVirtualStockChestSideMinimumM, kVirtualStockChestSideMaximumM),
    "Experiment chest-side is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockAdaptiveTopHeightM,
        kVirtualStockAdaptiveTopHeightMinimumM,
        kVirtualStockAdaptiveTopHeightMaximumM),
    "Experiment adaptive top-height is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockAdaptiveBottomHeightM,
        kVirtualStockAdaptiveBottomHeightMinimumM,
        kVirtualStockAdaptiveBottomHeightMaximumM),
    "Experiment adaptive bottom-height is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockAdaptiveTopHalfWidthM,
        kVirtualStockAdaptiveTopHalfWidthMinimumM,
        kVirtualStockAdaptiveTopHalfWidthMaximumM),
    "Experiment adaptive top half-width is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockAdaptiveBottomHalfWidthM,
        kVirtualStockAdaptiveBottomHalfWidthMinimumM,
        kVirtualStockAdaptiveBottomHalfWidthMaximumM),
    "Experiment adaptive bottom half-width is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockHybridOffhandInfluence,
        kVirtualStockHybridOffhandInfluenceMinimum,
        kVirtualStockHybridOffhandInfluenceMaximum),
    "Experiment Hybrid offhand influence is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockHybridAdsReference,
        kVirtualStockHybridAdsReferenceMinimum,
        kVirtualStockHybridAdsReferenceMaximum),
    "Experiment Hybrid ADS reference is outside allowed bounds");
static_assert(VirtualStockExperimentDiagnosticOverridesAreValid(
        kVirtualStockExperimentProfileSpecs),
    "Experiment Hybrid diagnostic override is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::hybridInverseNeckStrength,
        kVirtualStockHybridInverseNeckStrengthMinimum,
        kVirtualStockHybridInverseNeckStrengthMaximum),
    "Experiment inverse-neck strength is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::hybridInverseNeckForwardM,
        kVirtualStockHybridInverseNeckForwardMinimumM,
        kVirtualStockHybridInverseNeckForwardMaximumM),
    "Experiment inverse-neck forward offset is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::hybridInverseNeckUpM,
        kVirtualStockHybridInverseNeckUpMinimumM,
        kVirtualStockHybridInverseNeckUpMaximumM),
    "Experiment inverse-neck up offset is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::hybridInverseNeckLateralM,
        kVirtualStockHybridInverseNeckLateralMinimumM,
        kVirtualStockHybridInverseNeckLateralMaximumM),
    "Experiment inverse-neck lateral offset is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockHybridSeatFullM,
        kVirtualStockHybridSeatFullMinimumM,
        kVirtualStockHybridSeatFullMaximumM),
    "Experiment seat-full is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockHybridSeatReleaseM,
        kVirtualStockHybridSeatReleaseMinimumM,
        kVirtualStockHybridSeatReleaseMaximumM),
    "Experiment seat-release is outside allowed bounds");
static_assert(VirtualStockExperimentSettingSeparationIsValid(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockHybridSeatFullM,
        &VirtualStockAimSettings::virtualStockHybridSeatReleaseM,
        kVirtualStockHybridSeatMinimumSeparationM),
    "Experiment seat-release must exceed seat-full by at least 0.010 m");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::hybridHorizontalRearReleaseFullM,
        kVirtualStockProximityFullMinimumM,
        kVirtualStockProximityFullMaximumM),
    "Experiment horizontal-release full is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::hybridHorizontalRearReleaseReleaseM,
        kVirtualStockProximityReleaseMinimumM,
        kVirtualStockProximityReleaseMaximumM),
    "Experiment horizontal-release release is outside allowed bounds");
static_assert(VirtualStockExperimentSettingSeparationIsValid(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::hybridHorizontalRearReleaseFullM,
        &VirtualStockAimSettings::hybridHorizontalRearReleaseReleaseM,
        kVirtualStockProximityMinimumSeparationM),
    "Experiment horizontal-release release must exceed full by at least 0.010 m");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockProximityFullM,
        kVirtualStockProximityFullMinimumM,
        kVirtualStockProximityFullMaximumM),
    "Experiment proximity full is outside allowed bounds");
static_assert(VirtualStockExperimentSettingIsInRange(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockProximityReleaseM,
        kVirtualStockProximityReleaseMinimumM,
        kVirtualStockProximityReleaseMaximumM),
    "Experiment proximity release is outside allowed bounds");
static_assert(VirtualStockExperimentSettingSeparationIsValid(
        kVirtualStockExperimentProfileSpecs,
        &VirtualStockAimSettings::virtualStockProximityFullM,
        &VirtualStockAimSettings::virtualStockProximityReleaseM,
        kVirtualStockProximityMinimumSeparationM),
    "Experiment proximity release must exceed full by at least 0.010 m");
static_assert(VirtualStockTestProfileRegistryIsValid(),
    "Virtual Stock profile registry identity or role contract is invalid");
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::Custom) == 0);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::A_VsOffControl) == 1);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::B_FixedHeadControl) == 2);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::C_FixedShoulderControl) == 3);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::D_HybridBaseline) == 4);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::E_HybridMaxSeat) == 5);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::F_HybridForceStockHead) == 6);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::G_HybridForceStockShoulder) == 7);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::H_HybridForceHip50) == 8);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::I_HybridForceHip100) == 9);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::J_HybridSeat_464_474) == 10);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::K_HybridSeat_440_474) == 11);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::L_HybridSeat_420_474) == 12);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::M_HybridSeat_400_474) == 13);
static_assert(static_cast<uint8_t>(VirtualStockTestProfile::N_HybridSeat_380_474) == 14);
static_assert(kVirtualStockCoreTestProfileCount == 15);

class VirtualStockTestProfileState
{
public:
    VirtualStockTestProfileState() noexcept
        : value_(static_cast<uint8_t>(VirtualStockTestProfile::Custom))
    {
    }

    VirtualStockTestProfile Load() const noexcept
    {
        return NormalizeVirtualStockTestProfile(
            value_.load(std::memory_order_acquire));
    }

    void Store(VirtualStockTestProfile value) noexcept
    {
        value_.store(static_cast<uint8_t>(NormalizeVirtualStockTestProfile(
            static_cast<uint8_t>(value))), std::memory_order_release);
    }

private:
    std::atomic<uint8_t> value_;
};

inline VirtualStockAimSettings ResolveVirtualStockTestProfile(
    VirtualStockTestProfile profile,
    const VirtualStockAimSettings& custom) noexcept
{
    const auto normalized = NormalizeVirtualStockTestProfile(
        static_cast<uint8_t>(profile));
    const auto* definition = FindVirtualStockTestProfileDefinition(normalized);
    return definition == nullptr || definition->useCustomSettings
        ? custom : definition->settings;
}

constexpr const char* VirtualStockTestProfileName(
    VirtualStockTestProfile profile) noexcept
{
    const auto* definition = FindVirtualStockTestProfileDefinition(
        NormalizeVirtualStockTestProfile(static_cast<uint8_t>(profile)));
    return definition ? definition->stableName : "Custom";
}

constexpr const char* VirtualStockTestProfileLabel(
    VirtualStockTestProfile profile) noexcept
{
    const auto* definition = FindVirtualStockTestProfileDefinition(
        NormalizeVirtualStockTestProfile(static_cast<uint8_t>(profile)));
    return definition ? definition->uiLabel : "Custom";
}
