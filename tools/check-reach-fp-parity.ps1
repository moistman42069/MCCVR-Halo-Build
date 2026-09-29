[CmdletBinding()]
param()

# Reach consistency check.
#
# Halo 3, ODST and Reach are different engines. Parity here means the PLAYER
# EXPERIENCE matches -- same halomccvr.cfg knobs, same look and feel -- not that
# the implementations are textually identical. Forcing the latter is what this
# script used to do (184 rules asserting exact code text); it never caught a
# defect, it passed every broken Reach build, and it blocked working changes.
#
# What is checked now, and only this:
#   - Reach-only architectures already disproven in-headset are not reintroduced.
#   - Reach's title capabilities and camera-only-bringup contract stay intact.
#   - Evidence-derived constants are still present.
#
# The real safety net is the headset test plus the runtime-log A/B described in
# docs/CURRENT-STATE.md, not this file.

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$gamePath = Join-Path $repoRoot 'src\dll\game.cpp'
$vrPath = Join-Path $repoRoot 'src\dll\vr.cpp'
$logicPath = Join-Path $repoRoot 'src\common\reach_render_logic.h'
$vehicleLogicPath = Join-Path $repoRoot 'src\common\reach_vehicle_logic.h'
$chudLogicPath = Join-Path $repoRoot 'src\common\reach_chud_logic.h'
$titleRegistryPath = Join-Path $repoRoot 'src\common\title_registry.cpp'
$hudLayoutLogicPath = Join-Path $repoRoot 'src\common\hud_layout_logic.h'
$agentsPath = Join-Path $repoRoot 'AGENTS.md'
$packagePath = Join-Path $repoRoot 'tools\package-candidate.ps1'
$installerPath = Join-Path $repoRoot 'tools\install-candidate.ps1'
$cmakePath = Join-Path $repoRoot 'CMakeLists.txt'
$game = [IO.File]::ReadAllText($gamePath)
$vr = [IO.File]::ReadAllText($vrPath)
$logic = [IO.File]::ReadAllText($logicPath)
$vehicleLogic = [IO.File]::ReadAllText($vehicleLogicPath)
$chudLogic = [IO.File]::ReadAllText($chudLogicPath)
$titleRegistry = [IO.File]::ReadAllText($titleRegistryPath)
$hudLayoutLogic = [IO.File]::ReadAllText($hudLayoutLogicPath)
$agents = [IO.File]::ReadAllText($agentsPath)
$package = [IO.File]::ReadAllText($packagePath)
$installer = [IO.File]::ReadAllText($installerPath)
$cmake = [IO.File]::ReadAllText($cmakePath)

$forbidden = [ordered]@{
    'single Reach interpolation context' =
        'thread_local\s+ReachFpInterpolationContext\s+g_reachFpInterpolation\s*;'
    'separated live-graph hand transform' = 'ReachApplySeparatedHandGraph'
    'Reach-only source ownership enum' = 'ReachFpSourceOwner'
    'body-only palette action' = 'ArticulateExactBody'
    'palette solve truncated to discovery count' =
        'fp\.count\s*=\s*static_cast<int>\(observed\.paletteBodyNodeCount\)'
    'outer stereo workspace reused for FP camera upload' =
        'scope\.workspace\s*\+\s*kReachSecondaryDerivedOffset'
    'legacy unverified FP compact-camera workspace alias' =
        'kReachFpCompactCameraRva'
    'FP camera success published before the uploader returns' =
        'ReachFpCameraRebuildBody[\s\S]*?PublishReachFpCameraUpload\(scope\)[\s\S]*?g_reachFpCameraUpload\(compact,\s*derived\)'
    'hidden left-arm ownership admitted into the visible keep mask' =
        'const\s+uint64_t\s+keep\s*=[^;]*leftControllerOwnedSourceBranch'
    'hidden right-arm ownership admitted into the visible keep mask' =
        'const\s+uint64_t\s+keep\s*=[^;]*rightControllerOwnedSourceBranch'
    'hidden left-arm branch receives the visible-hand rigid delta' =
        'if\s*\(!\(\s*leftControllerOwnedSourceBranch\s*&'
    'prepared controller targets rebased through the render head root' =
        'ReachRebasePreparedControllerTargets'
    'placement base retained for a second controller-target translation' =
        'placementBase(?:Valid)?'
    'prepared wrist translation rebuilt from the palette root' =
        'target\.translation\[axis\]\s*=\s*renderRoot\.translation\[axis\]'
    'Reach projectile-origin hook or relay' =
        'ReachFpProjectileOrigin|fpProjectileOrigin(?:Target|Relay)|kReachProjectile(?:Fire|Origin)'
    'Reach projectile-only weapon-slot gate' =
        '(?:g_|k)reachFpWeaponSlotForDatum|ReachShouldUseNativeWeaponProjectileOrigin'
    'Reach first-person marker-query or marker-composer detour' =
        'ReachFpMarker(?:Query|Compose)|fpMarker(?:Query|Compose)Target|kReachFpMarker(?:Query|Compose)'
    'Reach published marker fallback' =
        'ReachApplyPublishedMarkerQueryTransform|ReachPublishMarkerSharedTransform|g_reachFpMarker(?:SharedTransform|SourceSerial|QueryCorrectedSerial)'
    'Reach primary-trigger firing-frame write' =
        'g_reachFpPrimaryTriggerWorld|frame\+barrelOffset\+0x9F0'
    'Reach projectile tag-policy constants' =
        'kReachBarrelProjectilesUseWeaponOriginMask|kReachBarrelProjectileFiresInMarkerDirectionMask'
}
foreach ($entry in $forbidden.GetEnumerator()) {
    if (($game + "`n" + $logic) -match $entry.Value) {
        throw "Reach FP parity gate rejected: $($entry.Key)."
    }
}

$forbiddenChud = [ordered]@{
    # Optional hook creation/publication is required by the feature-isolation
    # contract. Guard the rejected substitutes and unsafe selection logic, not
    # the fail-open lifecycle that keeps an independent camera core alive.
    'Reach CHUD widget-name selection or fallback' =
        'ReachHudDrawWidgetDetour[\s\S]{0,3600}(?:strcmp|strstr|widgetName|artistName)'
    'Reach procedural reticle generation from the CHUD detour' =
        'ReachHudDrawWidgetDetour[\s\S]{0,3600}(?:PaintReticle|EnsureReticleChain|reticle_r|reticle_g|reticle_b)'
    'Reach procedural or approximate CHUD action' =
        'ReachChudCrosshairAction::(?:Procedural|DrawProcedural|Fallback|Approximate)'
    'Reach projection queued before authored upload result' =
        'layers\.push_back\(\s*reinterpret_cast<XrCompositionLayerBaseHeader\*>\(\s*&projection\s*\)\s*\)[\s\S]{0,2200}?authoredUploadFailed'
    'Reclaimer-derived Reach CHUD binding' =
        'Reclaimer|kReachRetailChud'
    'owned Reach transaction stock rerender helper' =
        'ReachScopedStock(?:Fallback|BeforeOwnership)'
    'failed claimed Reach transaction invokes the stock renderer' =
        'if\s*\(\s*!handled\s*\)[\s\S]{0,260}?(?:ReachScopedStock|g_reachOrigPlayerViewRender)'
}
foreach ($entry in $forbiddenChud.GetEnumerator()) {
    if (($game + "`n" + $chudLogic + "`n" + $vr) -match $entry.Value) {
        throw "Reach authored-crosshair parity gate rejected: $($entry.Key)."
    }
}





# Reach's HUD layout is now HREK-proven and wired through kReachHudLayoutAdapter,
# so the capability is granted rather than withheld. What must stay true is that
# the adapter never claims a depth field: Reach folds its curvature into a
# derived basis at tag-block load, so a runtime write there would be inert.
$reachCapabilities = [regex]::Match(
    $titleRegistry,
    'constexpr\s+uint32_t\s+kReachCapabilities\s*=\s*(?<body>[\s\S]*?);')
if (!$reachCapabilities.Success -or
    $reachCapabilities.Groups['body'].Value -notmatch 'TitleCapability_Hud') {
    throw 'Reach parity gate rejected: HUD capability must be granted now that the Reach layout adapter exists.'
}

$reachHudAdapter = [regex]::Match(
    $hudLayoutLogic,
    'kReachHudLayoutAdapter\s*=\s*\{(?<body>[\s\S]*?)\n\};')
if (!$reachHudAdapter.Success -or
    $reachHudAdapter.Groups['body'].Value -notmatch 'kHudLayoutNoDepthField') {
    throw 'Reach parity gate rejected: the Reach HUD adapter must declare its depth field absent, not point at inert data.'
}

$cameraOnlyBringup = [regex]::Match(
    $game,
    'bool\s+Game_IsCameraOnlyBringup\(\)\s*\{(?<body>[\s\S]*?)\n\}')
if (!$cameraOnlyBringup.Success -or
    $cameraOnlyBringup.Groups['body'].Value -notmatch 'OdstCameraOnlyContext|return\s+false' -or
    $cameraOnlyBringup.Groups['body'].Value -match 'HaloReach|g_reach') {
    throw 'Reach authored-crosshair parity gate rejected: procedural opacity admission must remain ODST-only.'
}

if ($logic -notmatch 'ArticulateKnownTransaction') {
    throw 'Reach FP parity gate missing: every known final palette action.'
}
if ($agents -notmatch 'must never take down a working VR path') {
    throw 'Reach consistency check missing: feature-isolation contract in AGENTS.md.'
}
if ($logic -notmatch 'kReachFpWeaponIkDisableValueRva' -or
    $logic -notmatch 'kReachFpWeaponIkDisabledEpilogueRva') {
    throw 'Reach FP parity gate missing: exact Reach native weapon-IK proof anchors.'
}
if ($logic -notmatch 'kReachFpCameraRebuildAob' -or
    $logic -notmatch 'kReachFpCameraUploadAob' -or
    $logic -notmatch 'exactFpCameraFlowEdges') {
    throw 'Reach FP parity gate missing: exact Reach camera rebuild/upload proof anchors.'
}
if ($logic -notmatch 'kReachCameraStackCallbackAob' -or
    $logic -notmatch 'kReachCameraStackCallbackBodySha256' -or
    $logic -notmatch 'cameraStackCallbackBodyHash') {
    throw 'Reach camera evidence gate missing: exact outer-camera callback proof anchors.'
}
if ($vehicleLogic -notmatch
        'kReachR_V20SeatBitLeaseEnabled\s*=\s*false' -or
    $vehicleLogic -notmatch
        'kReachR_V22NativeVehicleReticleEnabled\s*=\s*false') {
    throw 'Reach vehicle gate rejected: a headset-disproven seat-bit/native-reticle path was re-enabled.'
}
$manifestContracts = @(
    'reach_vehicle_body_hide_interval_lease_enabled\s*=\s*\$false',
    'reach_vehicle_unit_camera_scoped_body_hide_enabled\s*=\s*\$true',
    'reach_vehicle_exact_seat_entry_playspace_recenter_enabled\s*=\s*\$true',
    'reach_vehicle_entry_recenter_view_follow_independent\s*=\s*\$true',
    'reach_vehicle_entry_recenter_refresh_invariant\s*=\s*\$true',
    'reach_vehicle_entry_recenter_openxr_present_owned\s*=\s*\$true',
    'reach_vehicle_entry_recenter_outer_commit_staged\s*=\s*\$true',
    'reach_vehicle_camera_proof_miss_preserves_occupation\s*=\s*\$true',
    'reach_vehicle_yaw_reference_atomic_pair\s*=\s*\$true',
    'reach_vehicle_yaw_reference_requires_committed_frame\s*=\s*\$true',
    'reach_vehicle_exit_recenter_position_only\s*=\s*\$true',
    'reach_vehicle_native_fp_body_seated_legs_enabled\s*=\s*\$true',
    'reach_vehicle_fp_body_centered_authored_pose\s*=\s*\$true',
    'reach_vehicle_fp_body_failure_isolated\s*=\s*\$true',
    'hrek-checksum-count-exact-tag-next-pair',
    'reach_native_seated_aim_reticle_enabled\s*=\s*\$false',
    'reach_controller_vehicle_reticle_enabled\s*=\s*\$true',
    'reach_vehicle_selected_barrel_direction_alignment_enabled\s*=\s*\$true',
    'reach_vehicle_shot_freshness_ms\s*=\s*50'
)
foreach ($contract in $manifestContracts) {
    if ($package -notmatch $contract) {
        throw 'Reach candidate manifest no longer describes the active R-V23 vehicle contract.'
    }
}
$installerContracts = @(
    'reach_vehicle_body_hide_interval_lease_enabled\s*-ne\s*\$false',
    'reach_vehicle_unit_camera_scoped_body_hide_enabled\s*-ne\s*\$true',
    'reach_vehicle_exact_seat_entry_playspace_recenter_enabled\s*-ne',
    'reach_vehicle_entry_recenter_view_follow_independent\s*-ne',
    'reach_vehicle_entry_recenter_refresh_invariant\s*-ne',
    'reach_vehicle_entry_recenter_openxr_present_owned\s*-ne',
    'reach_vehicle_entry_recenter_outer_commit_staged\s*-ne',
    'reach_vehicle_camera_proof_miss_preserves_occupation\s*-ne',
    'reach_vehicle_yaw_reference_atomic_pair\s*-ne',
    'reach_vehicle_yaw_reference_requires_committed_frame\s*-ne',
    'reach_vehicle_exit_recenter_position_only\s*-ne',
    'reach_vehicle_native_fp_body_seated_legs_enabled\s*-ne',
    'reach_vehicle_fp_body_centered_authored_pose\s*-ne',
    'reach_vehicle_fp_body_failure_isolated\s*-ne',
    'hrek-checksum-count-exact-tag-next-pair',
    'reach_native_seated_aim_reticle_enabled\s*-ne\s*\$false',
    'reach_controller_vehicle_reticle_enabled\s*-ne\s*\$true',
    'reach_vehicle_selected_barrel_direction_alignment_enabled\s*-ne',
    'reach_vehicle_shot_freshness_ms\s*-ne\s*50'
)
foreach ($contract in $installerContracts) {
    if ($installer -notmatch $contract) {
        throw 'Reach candidate installer no longer enforces the active R-V23 vehicle contract.'
    }
}
$analyserDeliveryContracts = [ordered]@{
    'CMake dist staging' = @($cmake,
        'DESTINATION\s+TelemetryAnalyser[\s\S]{0,80}COMPONENT\s+dist')
    'package manifest schema' = @($package,
        'schema_version\s*=\s*55')
    'package analyser manifest path' = @($package,
        "'TelemetryAnalyser/analyse_mccvr_telemetry\.py'\s*=\s*\[ordered\]@\{")
    'package analyser self-test' = @($package,
        '\$analyserPath\s+--self-test')
    'package analyser pinned hash' = @($package,
        '053CF61671BE281551B89A459E0611490F29D0FFD00387B43668043793BAE5B4')
    'package analyser pinned size' = @($package,
        'analyserSource\.Length\s+-ne\s+395283')
    'installer manifest schema' = @($installer,
        'Test-ExactInt32\s+\$manifest\.schema_version\s+55')
    'installer analyser pinned hash' = @($installer,
        '053CF61671BE281551B89A459E0611490F29D0FFD00387B43668043793BAE5B4')
    'installer analyser pinned size' = @($installer,
        '\$expectedAnalyserBytes\s*=\s*395283')
    'installer analyser identity' = @($installer,
        "Assert-FileIdentity[\s\S]{0,100}'Installed telemetry analyser'")
}
foreach ($contract in $analyserDeliveryContracts.GetEnumerator()) {
    if ($contract.Value[0] -notmatch $contract.Value[1]) {
        throw "Candidate analyser delivery contract missing: $($contract.Key)."
    }
}
Write-Host 'Reach consistency check passed: no disproven Reach-only architecture reintroduced, Reach capabilities intact, evidence constants and candidate manifest present.'
