#include "title_registry.h"

#include <cwctype>

#ifndef HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
#define HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP 0
#endif
#ifndef HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP
#define HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP 0
#endif

static_assert(HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP == 0 ||
              HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP == 1);
static_assert(HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP == 0 ||
              HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP == 1);

namespace
{
    constexpr uint32_t kHalo3Capabilities =
        TitleCapability_Stereo |
        TitleCapability_ControllerAim |
        TitleCapability_Hud |
        TitleCapability_ArmIk |
        TitleCapability_RuntimeModes |
        TitleCapability_RoomScale |
        TitleCapability_ControllerInput |
        TitleCapability_Haptics |
        TitleCapability_CutsceneTheater;
    constexpr uint32_t kHalo3AdmissionCapabilities =
        TitleCapability_ControllerInput;
    // Reach camera + motion core: stereo, tracked look/turn, locomotion input,
    // room-scale translation, controller aim, articulated first-person arms,
    // and haptics now have title-specific runtime/evidence paths. Native HUD
    // remains withheld until Reach's final CHUD anchor is proven and wired.
    constexpr uint32_t kReachCapabilities =
        TitleCapability_Stereo |
        TitleCapability_ControllerAim |
        TitleCapability_ArmIk |
        TitleCapability_Hud |
        TitleCapability_RuntimeModes |
        TitleCapability_RoomScale |
        TitleCapability_ControllerInput |
        TitleCapability_Haptics |
        TitleCapability_CutsceneTheater;
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
    constexpr uint32_t kOdstAdmissionCapabilities =
        TitleCapability_ControllerInput;
#else
    constexpr uint32_t kOdstAdmissionCapabilities = TitleCapability_None;
#endif
#if HALOMCCVR_EXPERIMENTAL_REACH_BRINGUP
    constexpr uint32_t kReachAdmissionCapabilities =
        TitleCapability_ControllerInput;
#else
    constexpr uint32_t kReachAdmissionCapabilities = TitleCapability_None;
#endif
    // Halo 4 camera + motion core. C-H4-7/8/9 earned stereo, head tracking,
    // 6DOF and headset-owned pitch in the headset; C-H4-10 adds the closed-loop
    // controller aim, VR turn and haptics the other titles already have, on the
    // same shared code paths. Halo 4's CUI arrives inside the captured scene
    // target rather than through a HUD redirect, so TitleCapability_Hud stays
    // out: nothing title-specific is installed for it. C-H4-13 adds the
    // evidence-backed final-palette arm solve. CutsceneTheater remains
    // withheld because it has no Halo 4 evidence yet.
    constexpr uint32_t kHalo4Capabilities =
        TitleCapability_Stereo |
        TitleCapability_ControllerAim |
        TitleCapability_ArmIk |
        TitleCapability_RuntimeModes |
        TitleCapability_RoomScale |
        TitleCapability_ControllerInput |
        TitleCapability_Haptics;
    constexpr uint32_t kHalo4AdmissionCapabilities =
        TitleCapability_ControllerInput;

    constexpr TitleDescriptor kTitles[] = {
        { GameTitle::Halo3, L"halo3.dll", "Halo 3", true,
          kHalo3Capabilities, kHalo3AdmissionCapabilities },
        { GameTitle::Halo3ODST, L"halo3odst.dll", "Halo 3: ODST", false,
          TitleCapability_None, kOdstAdmissionCapabilities },
        { GameTitle::HaloReach, L"haloreach.dll", "Halo: Reach", true,
          kReachCapabilities, kReachAdmissionCapabilities },
        { GameTitle::Halo4, L"halo4.dll", "Halo 4", false,
          kHalo4Capabilities, kHalo4AdmissionCapabilities },
        { GameTitle::HaloCE, L"halo1.dll", "Halo: CE Anniversary", false,
          TitleCapability_None, TitleCapability_None },
        { GameTitle::Halo2, L"halo2.dll", "Halo 2 Anniversary", false,
          TitleCapability_None, TitleCapability_None },
    };

    bool EqualsModuleName(std::wstring_view left, std::wstring_view right)
    {
        size_t slash = left.find_last_of(L'/');
        const size_t backslash = left.find_last_of(static_cast<wchar_t>(92));
        if (backslash != std::wstring_view::npos &&
            (slash == std::wstring_view::npos || backslash > slash))
            slash = backslash;
        if (slash != std::wstring_view::npos)
            left.remove_prefix(slash + 1);
        if (left.size() != right.size())
            return false;
        for (size_t i = 0; i < left.size(); ++i)
        {
            if (std::towlower(left[i]) != std::towlower(right[i]))
                return false;
        }
        return true;
    }
}

const TitleDescriptor* TitleRegistry_All(size_t& count)
{
    count = sizeof(kTitles) / sizeof(kTitles[0]);
    return kTitles;
}

const TitleDescriptor* TitleRegistry_Find(GameTitle title)
{
    for (const auto& descriptor : kTitles)
    {
        if (descriptor.title == title)
            return &descriptor;
    }
    return nullptr;
}

const TitleDescriptor* TitleRegistry_FromModuleName(std::wstring_view moduleName)
{
    for (const auto& descriptor : kTitles)
    {
        if (EqualsModuleName(moduleName, descriptor.moduleName))
            return &descriptor;
    }
    return nullptr;
}

TitleHookPlan TitleRegistry_HookPlan(GameTitle title)
{
    switch (title)
    {
    case GameTitle::Halo3:
        return TitleHookPlan::Halo3Full;
    case GameTitle::Halo3ODST:
#if HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP
        return TitleHookPlan::OdstExperimentalCameraCore;
#else
        return TitleHookPlan::None;
#endif
    case GameTitle::HaloReach:
        return TitleHookPlan::ReachCameraCore;
    default:
        return TitleHookPlan::None;
    }
}

bool TitleRegistry_AllowsSharedGameplayFeatures(
    GameTitle activeTitle, bool halo3CameraOwned, bool cameraOnlyOwned)
{
    if (cameraOnlyOwned)
        return false;
    if (activeTitle == GameTitle::None || activeTitle == GameTitle::Halo3)
        return true;
    return activeTitle == GameTitle::Unknown && halo3CameraOwned;
}

bool TitleRegistry_AllowsSharedControllerInput(
    GameTitle activeTitle, bool resolvedOwnerAllowsControllerInput,
    bool cameraOnlyOwned,
    bool allowAmbiguousFrontend, bool explicitTitleAllowsControllerInput)
{
    if (activeTitle == GameTitle::Unknown)
        return resolvedOwnerAllowsControllerInput ||
            (allowAmbiguousFrontend && !cameraOnlyOwned);
    if (activeTitle == GameTitle::Halo3ODST)
        return explicitTitleAllowsControllerInput;
    if (cameraOnlyOwned)
        return false;
    if (activeTitle == GameTitle::None)
        return true;
    return explicitTitleAllowsControllerInput;
}

bool TitleRegistry_Halo3CameraOwnsAmbiguousState(
    uint64_t now, uint64_t lastCamera, uint64_t titleTransition)
{
    return titleTransition != 0 && lastCamera > titleTransition &&
        now >= lastCamera &&
        now - lastCamera < kHalo3AmbiguousCameraOwnershipMs;
}

const char* RuntimeModeName(RuntimeMode mode)
{
    switch (mode)
    {
    case RuntimeMode::Shell: return "shell";
    case RuntimeMode::Loading: return "loading";
    case RuntimeMode::Gameplay: return "gameplay";
    case RuntimeMode::Paused: return "paused";
    case RuntimeMode::Cutscene: return "cutscene";
    case RuntimeMode::Vehicle: return "vehicle";
    case RuntimeMode::Turret: return "turret";
    case RuntimeMode::Dead: return "death";
    case RuntimeMode::Unsupported: return "unsupported";
    default: return "unknown";
    }
}
