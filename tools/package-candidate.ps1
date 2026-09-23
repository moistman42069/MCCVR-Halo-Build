[CmdletBinding()]
param(
    # Force a from-scratch compile. Off by default: the packaged identity comes
    # from the git commit check and the SHA-256 of the packaged files, not from
    # discarding object files, and a clean rebuild cost minutes on every single
    # candidate.
    [switch]$Clean,

    # Retained only to reject obsolete callers explicitly. The new complete
    # ModFiles payload is installed by the bundled verified launcher.
    [switch]$Install
)

# Halo MCC VR is one cumulative build: Halo 3 + ODST + Halo: Reach + Halo 4,
# with Halo 2's cadence-gated same-frame stereo + 6DOF and
# generation-scoped post-claim failure quarantine.
# Reach's camera core is permanent while Halo 4 is still an explicitly
# unaccepted bring-up line. Optional player-visible features fail open
# independently. This stages one unaccepted local candidate under out/candidates
# after a passing build and tests. It never installs or launches MCC, and never
# labels rebuilt bytes as an accepted release.

$ErrorActionPreference = 'Stop'
if ($Install) {
    throw 'This QoL package uses the complete ModFiles payload. Package without -Install; use the bundled verified installer for an explicitly requested installation.'
}

# Native build tools (cmake, ctest) write progress and deprecation notices to
# stderr. Under ErrorActionPreference=Stop, PowerShell 5.1 turns any native
# stderr line into a terminating error, so run tool invocations with stderr
# tolerated and rely on the explicit $LASTEXITCODE checks that follow each call.
function Invoke-Tool([scriptblock]$Block) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Block } finally { $ErrorActionPreference = $saved }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot 'out\candidates'))
$expectedCandidateRoot = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot 'out')) + [IO.Path]::DirectorySeparatorChar
$packagePreset = 'release'
$packageBuildDir = 'out\build\release'

if (-not $candidateRoot.StartsWith(
        $expectedCandidateRoot,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate path escaped the repository out directory: $candidateRoot"
}

Push-Location $repoRoot
try {
    # Production code extracted into included fragments is still part of the
    # translation unit. Keep historical behavioral gates after these extractions.
    function Read-SourceWithFragments([string]$path) {
        $text = [IO.File]::ReadAllText($path)
        foreach ($include in [regex]::Matches($text, '(?m)^\s*#include\s+"([^"\r\n]+\.inl)"')) {
            $fragment = Join-Path (Split-Path $path) $include.Groups[1].Value
            $text += "`n" + (Read-SourceWithFragments $fragment)
        }
        return $text
    }
    $status = @(& git -C $repoRoot status --porcelain=v1 --untracked-files=normal)
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not inspect Git worktree state.'
    }
    if ($status.Count -ne 0) {
        throw ("Refusing to package a dirty worktree. Commit the candidate first:`n" +
            ($status -join "`n"))
    }

    $commit = (& git -C $repoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') {
        throw 'Could not resolve the candidate source commit.'
    }

    # Every unaccepted continuation must descend from the authoritative
    # user-accepted C-H4-56 source pointer.
    $acceptedC56Baseline =
        '271f6dffb8cf2e13dc4feafd85b9b4c61440ff25'
    & git -C $repoRoot merge-base --is-ancestor $acceptedC56Baseline $commit
    if ($LASTEXITCODE -ne 0) {
        throw "Refusing to package: HEAD does not descend from accepted C-H4-56 source $acceptedC56Baseline."
    }

    # C-H2-55 observer identity is explicitly non-owning. Do not apply that
    # rule globally: accepted ODST/Reach cores deliberately own loader pins,
    # and Halo 2 stereo owns a short cleanup pin while its hooks drain.
    $halo2ObserverSource = Read-SourceWithFragments (
        (Join-Path $repoRoot 'src\dll\halo2_observer_6dof.cpp'))
    if ($halo2ObserverSource -match '(?m)^\s*FreeLibrary\s*\(') {
        throw 'C-H2-55 gate failed: the Halo 2 observer released a non-owning module identity.'
    }
    $moduleHandleCalls = [regex]::Matches(
        $halo2ObserverSource,
        'GetModuleHandleExW\s*\((?<args>[\s\S]{0,320}?)\)')
    foreach ($call in $moduleHandleCalls) {
        $argsText = $call.Groups['args'].Value
        if ($argsText -match 'GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS' -and
                $argsText -notmatch
                    'GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT') {
            throw 'C-H2-55 gate failed: a Halo 2 observer FROM_ADDRESS lookup increments the loader refcount.'
        }
    }
    $gameSource = Read-SourceWithFragments (
        (Join-Path $repoRoot 'src\dll\game.cpp'))
    $inputSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\dll\input.cpp'))
    $titleRuntimeSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\title_runtime_state.h'))
    $coreTestsSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'tests\core_tests.cpp'))
    $guardSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\dll\halo2_render_mode_guard.cpp'))
    if ($titleRuntimeSource -notmatch
            'ResolveTitleLevelGateAction' -or
        $titleRuntimeSource -notmatch
            'TitleLevelGateAction::HoldEvidence' -or
        $gameSource -notmatch
            'ResolveTitleLevelGateAction\s*\(\s*soleReachTitle\s*,\s*installed\s*,\s*levelRunning\s*\)' -or
        $coreTestsSource -notmatch
            'for\s*\(\s*uint32_t bits\s*=\s*0\s*;\s*bits\s*<\s*8\s*;' -or
        $coreTestsSource -notmatch
            'ResolveTitleLevelGateAction\(true, false, false\)\s*==\s*\r?\n\s*TitleLevelGateAction::HoldEvidence' -or
        $gameSource -notmatch
            '!soleHalo4Title\s*\|\|\s*!levelRunning' -or
        $gameSource -notmatch
            'hookRefreshPending\s*=\s*!RemoveInstalledGameHooks\(\)' -or
        $gameSource -notmatch
            'Halo 3 hook epoch retirement at level-liveness boundary' -or
        $gameSource -notmatch
            'ODST level-liveness boundary: retiring hooks' -or
        $guardSource -notmatch
            'activeAndRange\s*&&\s*\r?\n\s*levelRunning\s*&&\s*coldPassed') {
        throw 'C-H2-55 gate failed: a required title hook epoch is not level-liveness scoped.'
    }
    $halo2WorldCollisionLogicSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo2_world_collision_logic.h'))
    if ($halo2ObserverSource -notmatch
            'kHalo2CollisionVectorRva\s*=\s*0x0075B850' -or
        $halo2ObserverSource -notmatch
            'kHalo2ObjectSetLocalVelocityRva\s*=\s*0x0090A000' -or
        $halo2ObserverSource -notmatch
            'kHalo2CollisionFlags\s*=\s*0x2480000F' -or
        $halo2ObserverSource -notmatch 'InstallHalo2WorldCollision' -or
        $halo2ObserverSource -notmatch
            'Halo2PublishFinalPacketCollisionVolumes' -or
        $halo2ObserverSource -notmatch
            'Halo2Observer6Dof_WorldCollisionActive' -or
        $halo2WorldCollisionLogicSource -notmatch
            'Halo2SelectWorldCollisionExtrema' -or
        $halo2WorldCollisionLogicSource -notmatch
            'Halo2ResolveWorldCollision' -or
        $gameSource -notmatch
            'Halo2Observer6Dof_Armed\s*\(\s*\)\s*&&\s*Halo2Observer6Dof_FinalPaletteArmed\s*\(\s*\)' -or
        $coreTestsSource -notmatch
            'Halo 2 visible packets select stable root and model extrema') {
        throw 'Halo 2 world-contact gate failed: H2EK collision/object identities, final-packet volume, fail-open admission, or pure-logic coverage is missing.'
    }
    $halo2ColdSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\dll\halo2_cold_observation.cpp'))
    $halo2LogicSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo2_render_logic.h'))
    if ($halo2LogicSource -notmatch
            'Halo2ColdObservationNeedsDerivedRebind' -or
        $halo2ColdSource -notmatch 'cached image proof rebound the') {
        throw 'C-H2-55 gate failed: the same-generation Halo 2 derived-binding rearm is missing.'
    }
    if ($halo2LogicSource -notmatch
            'kHalo2RejectedInterpolatorControllerOwnershipEnabled\s*=\s*false' -or
        $halo2LogicSource -notmatch
            'kHalo2FinalPaletteControllerOwnershipEnabled\s*=\s*false' -or
        $halo2LogicSource -notmatch
            'kHalo2StableFinalPacketControllerOwnershipEnabled\s*=\s*false' -or
        $halo2LogicSource -notmatch
            'kHalo2VisibleConsumerControllerOwnershipEnabled\s*=\s*true') {
        throw 'C-H2-65 gate failed: every rejected Halo 2 hand path must stay off and only the renderer-selected callback/persistent-packet transaction may be enabled.'
    }
    if ($halo2LogicSource -notmatch
            'kHalo2C64GenericLeftPresentationEnabled\s*=\s*false') {
        throw 'C-H2-65 gate failed: the rejected C-H2-64 generic alignment is not disabled.'
    }
    $configHeaderSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\config.h'))
    $menuSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\dll\menu.cpp'))
    $vrSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\dll\vr.cpp'))
    if ($halo2ObserverSource -notmatch
            'kHalo2ParticleRendererRva\s*=\s*0x0076DC90' -or
        $halo2ObserverSource -notmatch
            'Halo2ShouldSuppressClassicFirstPersonParticle' -or
        $halo2LogicSource -notmatch
            'currentUserFirstPerson\s*!=\s*0\s*&&\s*\r?\n\s*Halo2ClassicRenderTreeRuns' -or
        $menuSource -notmatch 'H2 Classic gun yaw \(deg\)' -or
        $menuSource -notmatch 'H2 Classic gun pitch \(deg\)' -or
        $configHeaderSource -notmatch
            'bool fit_desktop_window\s*=\s*true' -or
        $configHeaderSource -notmatch 'float hud_size\s*=\s*0\.43f' -or
        $configHeaderSource -notmatch 'bool show_welcome\s*=\s*true' -or
        $vrSource -notmatch 'Halo 2 Stage 3AM performance gate') {
        throw 'C-H2-88 gate failed: Classic muzzle isolation, the two alignment controls, V5 defaults, welcome, or bounded Stage 3AM diagnostics are missing.'
    }
    $halo4RestoreLogicSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo4_restoration_logic.h'))
    $halo4WorldCollisionSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo4_world_collision_logic.h'))
    $halo4RestoreAsmSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\dll\halo4_restoration.asm'))
    $halo4CuiSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo4_cui_reticle_logic.h'))
    $halo4HelmetShaderSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo4_helmet_shader_logic.h'))
    $halo4ScreenEffectShaderSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo4_screen_effect_shader_logic.h'))
    $d3dSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\dll\d3d11_hook.cpp'))
    if ($gameSource -notmatch 'InstallHalo4Restoration' -or
        $gameSource -notmatch 'kHalo4EffectNegRva\s*=\s*0x1059A2' -or
        $gameSource -notmatch 'kHalo4PauseReasonRva\s*=\s*0xA0AE4' -or
        $gameSource -notmatch 'kHalo4HudRootRva\s*=\s*0x3F313C' -or
        $gameSource -notmatch 'kHalo4CurvatureRva\s*=\s*0x420D7E' -or
        $gameSource -notmatch 'VR_IsPausePresentationTarget\(\)' -or
        $halo4RestoreLogicSource -notmatch
            'Halo4PauseReasonGetterMatches' -or
        $halo4RestoreLogicSource -notmatch
            'Halo4ComputeNativeHudAffine' -or
        $halo4RestoreLogicSource -notmatch
            'Halo4NativeHudAdmitsCuiRoot' -or
        $gameSource -notmatch
            'Halo4NativeHudAdmitsCuiRoot\([\s\S]{0,240}g_halo4CuiFrontendCallbackDepth' -or
        $halo4CuiSource -notmatch 'Halo4SelectCuiCaptureCanvas' -or
        $gameSource -notmatch 'cuiReticleCaptureBaseX' -or
        $gameSource -notmatch 'cuiReticleCaptureBaseY' -or
        $gameSource -notmatch
            'scope\.captureReplay\s*=\s*true;[\s\S]{0,800}g_halo4HudGameplayThreadId\s*=\s*0;' -or
        $halo4HelmetShaderSource -notmatch
            'kVisorFramingHash\s*=\s*0x4BE62AC49C2BF210ULL' -or
        $d3dSource -notmatch 'PixelShaderSetHook' -or
        $d3dSource -notmatch 'D3D_Halo4HelmetShaderPathAvailable' -or
        $halo4RestoreAsmSource -notmatch 'Halo4EffectTransientWrapper' -or
        $halo4RestoreAsmSource -notmatch 'Halo4CurvatureBridge' -or
        $configHeaderSource -notmatch 'bool halo4_helmet\s*=\s*true' -or
        $menuSource -notmatch 'Show Halo 4 helmet frame') {
        throw 'C-H4-56 gate failed: the full-frontend visor geometry admission, native-reticle replay canvas, exact visor-shader toggle, pause, effects, or adjustable HUD source is missing.'
    }
    if ($halo4ScreenEffectShaderSource -notmatch
            'kMotionSuckHash\s*=\s*0x47668A1953271934ULL' -or
        $halo4ScreenEffectShaderSource -notmatch 'ShouldSuppress' -or
        $d3dSource -notmatch 'RegisterHalo4MotionSuckShader' -or
        $d3dSource -notmatch 'VR_IsStereoEnabled\(\)' -or
        $d3dSource -notmatch 'D3D_Halo4ScreenEffectShaderPathAvailable' -or
        $gameSource -notmatch 'screen-fx=%s' -or
        $coreTestsSource -notmatch
            'Halo 4 motion-suck suppression is exact, feature-local, title-local, and stereo-only') {
        throw 'C-H4-57 gate failed: exact H4EK/retail motion-suck identity, Halo-4/stereo isolation, telemetry, or unit coverage is missing.'
    }
    if ($gameSource -notmatch 'Halo4EffectCavePatch' -or
        $gameSource -notmatch
            'Halo4RestoreOwnedPatch\s*\(\s*base \+ kHalo4EffectCaveRva,\s*cave,\s*caveStock\)' -or
        $gameSource -notmatch
            'Halo4PatchMatches\s*\(\s*base \+ kHalo4EffectCaveRva,\s*caveStock\)') {
        throw 'C-H4-58 gate failed: Stage 3AI entry routes and their owned cave do not have symmetric teardown verification.'
    }
    if ($halo2LogicSource -notmatch
            'kHalo2DebugGlobalAimAssistOverrideEnabled\s*=\s*false' -or
        $halo2LogicSource -notmatch
            'kHalo2AimAssistCalculateRva\s*=\s*0x00759260' -or
        $halo2LogicSource -notmatch
            'kHalo2AimAssistViewDirectionRva\s*=\s*0x006C0DF0' -or
        $halo2LogicSource -notmatch
            'kHalo2ControllerAimAssistTargetingEnabled\s*=\s*true' -or
        $halo2LogicSource -notmatch 'Halo2WriteNeutralAimAssistResults' -or
        $halo2LogicSource -notmatch 'Halo2SuppressCameraAimAssist' -or
        $halo2LogicSource -notmatch 'Halo2OverrideAimAssistViewDirection' -or
        $halo2ObserverSource -notmatch 'kAimAssistCalculatePattern' -or
        $halo2ObserverSource -notmatch 'kAimAssistViewDirectionPattern' -or
        $halo2ObserverSource -notmatch 'Halo2AimAssistCalculateDetour' -or
        $halo2ObserverSource -notmatch
            'Halo2AimAssistViewDirectionDetour' -or
        $halo2ObserverSource -notmatch
            'Halo 2 controller melee targeting Installed' -or
        $halo2ObserverSource -notmatch 'g_aimAssistTargetSelected' -or
        $halo2ObserverSource -notmatch
            'camera/stereo/aim/hands/HUD/OpenXR' -or
        $coreTestsSource -notmatch
            'scoped aim-assist view helper accepts the normalized') {
        throw 'C-H2-92 gate failed: the rejected debug global is not dormant or controller-scoped native target selection, accepted camera suppression, fail-open isolation, telemetry, or unit coverage is missing.'
    }
    if ($configHeaderSource -notmatch
            'halo2_classic_gun_pitch_deg\s*=\s*-9\.5f' -or
        $configHeaderSource -notmatch
            'halo2_classic_gun_yaw_deg\s*=\s*1\.0f' -or
        $coreTestsSource -notmatch
            'fresh\.halo2_classic_gun_yaw_deg\s*==\s*1\.0f' -or
        $coreTestsSource -notmatch
            'fresh\.halo2_classic_gun_pitch_deg\s*==\s*-9\.5f') {
        throw 'Halo 2 Classic default-alignment gate failed: yaw +1.0 / pitch -9.5 or unit coverage is missing.'
    }
    $halo2StereoSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\dll\halo2_stereo_core.cpp'))
    $halo2HudLogicSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo2_hud_logic.h'))
    $halo2HudShaderSource = [IO.File]::ReadAllText(
        (Join-Path $repoRoot 'src\common\halo2_hud_shader_logic.h'))
    if ($halo2HudShaderSource -notmatch
            'kCrosshairHash\s*=\s*0x0a9b60d8f40268f6ULL' -or
        $halo2HudShaderSource -notmatch
            'kGameplayHudHashes' -or
        $halo2HudShaderSource -notmatch
            'MigotoFnv1' -or
        $d3dSource -notmatch 'CreatePixelShaderHook' -or
        $d3dSource -notmatch 'Halo2NativeHud_GetRasterLayout' -or
        $halo2StereoSource -notmatch
            'VR_PrepareAuthoredReticleResources' -or
        # A retirement/quiescence reference is not hook installation.
        $halo2StereoSource -match 'MH_CreateHook\s*\([^;]*&NativeHudAnchorBasisDetour' -or
        $halo2StereoSource -notmatch
            'Halo2NativeHud_DrawPlayer\s*\(' -or
        $coreTestsSource -notmatch
            'Halo 2 field-proven HUD/crosshair shader identities' -or
        $coreTestsSource -notmatch
            'Halo 2 HUD size/aspect/vertical sliders materially alter') {
        throw 'C-H2-77 gate failed: the proven shader identities, D3D raster transform, native-crosshair capture, shared replay, or zero-callback anchor rejection is missing.'
    }

    if ($gameSource -notmatch
            'kEnableHalo4WorldCollisionStage1\s*=\s*false' -or
        $gameSource -notmatch
            'kEnableHalo4WorldCollisionStage2\s*=\s*false' -or
        $gameSource -notmatch
            'kEnableHalo4WorldCollisionStage3\s*=\s*false' -or
        $gameSource -notmatch
            'kEnableHalo4WorldCollisionStage4\s*=\s*true' -or
        $gameSource -notmatch
            'kEnableHalo4WeaponWorldCollisionStage4\s*=\s*false' -or
        $gameSource -notmatch
            'kEnableHalo4WeaponWorldCollisionStage5\s*=\s*false' -or
        $gameSource -notmatch
            'kEnableHalo4WeaponWorldCollisionStage6\s*=\s*true' -or
        $gameSource -notmatch
            'kEnableHalo4PhysicalMeleeStage7\s*=\s*false' -or
        $gameSource -notmatch
            'kEnableHalo4PhysicalMeleeStage8\s*=\s*false' -or
        $gameSource -notmatch
            'kEnableHalo4PhysicalMeleeStage9\s*=\s*true' -or
        $halo4WorldCollisionSource -notmatch
            'kHalo4WeaponCollisionBoundsCount\s*==\s*39' -or
        $halo4WorldCollisionSource -notmatch
            'Halo4BuildWeaponCollisionBoundsSamples' -or
        $coreTestsSource -notmatch
            'Halo 4 weapon bounds publish eight corners and six face centres' -or
        $gameSource -notmatch
            'kHalo4WorldLineTestRva\s*=\s*0x0F4218' -or
        $gameSource -notmatch
            'kHalo4WorldLineFilterGlobalRva\s*=\s*0x2FFB0B8' -or
        $gameSource -notmatch
            'kHalo4WorldLineCollisionFlags\s*=\s*0x0002D009' -or
        $gameSource -notmatch
            'kHalo4WorldLineAdditionalFlags\s*=\s*0x00005185' -or
        $gameSource -notmatch
            'liveWorldLineFilter\s*==\s*kHalo4WorldLineFilterPair' -or
        $gameSource -notmatch 'Halo4PhysicsRayCastDetour' -or
        $gameSource -notmatch
            'originalRayCast\s*\(\s*&input\s*,\s*&output\s*\)' -or
        $gameSource -notmatch
            'originalFlags\s*&\s*kHalo4CollisionFixedObjectsOnly' -or
        $gameSource -notmatch
            'kHalo4ObjectSetVelocitiesRva\s*=\s*0x5D1580' -or
        $gameSource -notmatch 'Halo4PublishAuthoredHandCollisionVolumes' -or
        $gameSource -notmatch 'Halo4PublishAuthoredWeaponCollisionVolume' -or
        $gameSource -notmatch 'Game_Halo4PhysicalMeleePulseActive' -or
        $gameSource -notmatch
            'bool\s+Game_PhysicalMeleePulseActive\(uint64_t\)\s*\{\s*return false;\s*\}' -or
        $inputSource -notmatch
            'Game_GestureMeleeInput\s*\(\s*inputNow\s*\)' -or
        $halo4WorldCollisionSource -notmatch
            'Halo4UpdatePhysicalMeleeVelocityLatch' -or
        $vrSource -notmatch 'XrSpaceVelocity' -or
        $vrSource -notmatch 'VR_GetControllerLinearVelocity' -or
        $coreTestsSource -notmatch
            'physical melee fires once per tracked swing, supports the five metre ceiling') {
        throw 'H4 world-contact gate failed: accepted geometry, dormant legacy melee transport, active-binding gesture dispatcher, velocity sampling, or safety proofs are missing.'
    }
    if ($halo2WorldCollisionLogicSource -notmatch
            'kHalo2WeaponCollisionBoundsSampleCount\s*=\s*14' -or
        $halo2WorldCollisionLogicSource -notmatch
            'kHalo2WorldCollisionMaxSamples' -or
        $halo2ObserverSource -notmatch
            'Halo2BuildAuthoredWeaponCollisionSamples' -or
        $halo2ObserverSource -notmatch
            'g_halo2TagDataBaseSlot' -or
        $gameSource -notmatch
            'kLegacyCollisionMaxSamples\s*=\s*21' -or
        $gameSource -notmatch 'LegacyBuildAuthoredWeaponBounds' -or
        $gameSource -notmatch
            '0x001FFD18' -or
        $gameSource -notmatch
            '0x00231EC4' -or
        $gameSource -notmatch
            '0x0012C5D4' -or
        $gameSource -notmatch 'ReachCollisionVectorDetour' -or
        $gameSource -notmatch 'kReachCollisionVectorSignature' -or
        $halo2ObserverSource -notmatch 'kEnableRuntimeVerifiedCompressionLayout = true' -or
        $gameSource -notmatch
            'kReachCollisionResolveSignature' -or
        $gameSource -notmatch
            'feature\.fourArgumentResolver' -or
        $gameSource -notmatch
            'GameTitle::Halo3ODST' -or
        $gameSource -notmatch
            'GameTitle::HaloReach') {
        throw 'All-title world-contact gate failed: H2 authored weapon bounds, title-native H3/ODST/Reach collision wrappers, Reach ABI isolation, or shared melee admission is missing.'
    }

    Invoke-Tool { & cmake --preset $packagePreset }
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for preset $packagePreset."
    }

    $cachePath = Join-Path $repoRoot "$packageBuildDir\CMakeCache.txt"
    $cache = [IO.File]::ReadAllText($cachePath)
    if ($cache -notmatch
            '(?m)^HALOMCCVR_EXPERIMENTAL_ODST_BRINGUP:BOOL=ON\r?$') {
        throw 'Refusing to package: ODST is not ON in the cumulative build.'
    }
    if ($cache -notmatch
            '(?m)^HALOMCCVR_EXPERIMENTAL_HALO4_CAMERA:BOOL=ON\r?$') {
        throw 'Refusing to package C-H4-57: the Halo 4 camera core is not ON.'
    }
    if ($cache -notmatch
            '(?m)^HALOMCCVR_EXPERIMENTAL_HALO2_COLD_OBSERVATION:BOOL=ON\r?$') {
        throw 'Refusing to package C-H2-6: prerequisite Halo 2 cold observation is not ON.'
    }
    if ($cache -notmatch
            '(?m)^HALOMCCVR_EXPERIMENTAL_HALO2_TEMPORAL_STEREO:BOOL=OFF\r?$') {
        throw 'Refusing to package C-H2-6: rejected Halo 2 temporal stereo is not OFF.'
    }
    if ($cache -notmatch
            '(?m)^HALOMCCVR_HALO2_STEREO6DOF:BOOL=ON\r?$') {
        throw 'Refusing to package C-H2-6: Halo 2 same-frame stereo + 6DOF is not ON.'
    }
    if ($cache -notmatch '(?m)^BUILD_TESTING:BOOL=ON\r?$') {
        throw 'Refusing to package: BUILD_TESTING is not ON.'
    }

    # Incremental. A clean rebuild was recompiling the whole tree for every
    # candidate, which is minutes per iteration for no safety: the packaged
    # identity is proven by the git commit check above plus the SHA-256 of the
    # exact packaged files, not by how the object files were produced. Use
    # -Clean when a build-system change genuinely needs a from-scratch compile.
    $buildArgs = @('--build', '--preset', $packagePreset)
    if ($Clean) { $buildArgs += '--clean-first' }
    Invoke-Tool { & cmake @buildArgs }
    if ($LASTEXITCODE -ne 0) {
        throw 'Release build failed.'
    }

    Invoke-Tool { & ctest --preset $packagePreset }
    if ($LASTEXITCODE -ne 0) {
        throw 'Core tests failed.'
    }

    Invoke-Tool {
        & powershell -NoProfile -ExecutionPolicy Bypass -File `
            (Join-Path $repoRoot 'tools\check-reach-fp-parity.ps1')
    }
    if ($LASTEXITCODE -ne 0) {
        throw 'Reach consistency check failed.'
    }

    $finalCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
    $finalStatus =
        @(& git -C $repoRoot status --porcelain=v1 --untracked-files=normal)
    if ($LASTEXITCODE -ne 0 -or $finalCommit -ne $commit -or
            $finalStatus.Count -ne 0) {
        throw 'Repository state changed during build/test; refusing to label the artifacts.'
    }

    $createdUtc = [DateTime]::UtcNow
    $packageId = '{0}-{1}-{2}' -f $commit.Substring(0, 7),
        'qol-candidate',
        $createdUtc.ToString("yyyyMMdd-HHmmssfff'Z'")
    $packageDir = Join-Path $candidateRoot $packageId
    if (Test-Path -LiteralPath $packageDir) {
        throw "Refusing to reuse candidate directory: $packageDir"
    }
    $payloadDir = Join-Path $packageDir 'ModFiles'

    Invoke-Tool { & cmake --install $packageBuildDir --config Release `
        --prefix $payloadDir --component dist }
    if ($LASTEXITCODE -ne 0) {
        throw 'Candidate staging failed.'
    }

    $configGenerator = Join-Path $repoRoot `
        "$packageBuildDir\Release\halomccvr-config-defaults.exe"
    $configPath = Join-Path $payloadDir 'halomccvr.cfg'
    if (-not (Test-Path -LiteralPath $configGenerator -PathType Leaf)) {
        throw "Default-config generator is missing: $configGenerator"
    }
    Invoke-Tool { & $configGenerator $configPath }
    if ($LASTEXITCODE -ne 0) {
        throw 'Default config generation failed.'
    }

    $dllPath = Join-Path $payloadDir 'HaloMCCVR.dll'
    $launcherPath = Join-Path $payloadDir 'HaloMCCVRLauncher.exe'
    foreach ($requiredPath in @(
            $dllPath,
            $launcherPath,
            $configPath,
            (Join-Path $payloadDir 'LICENSE'),
            (Join-Path $payloadDir 'MANUAL-README.txt'),
            (Join-Path $payloadDir 'nvngx_dlss.dll'),
            (Join-Path $payloadDir 'licenses/NVIDIA-DLSS/LICENSE.txt'),
            (Join-Path $payloadDir 'assets/fonts/Oxanium.ttf'))) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Candidate package is missing: $requiredPath"
        }
    }

    $dll = Get-Item -LiteralPath $dllPath
    $launcher = Get-Item -LiteralPath $launcherPath
    $config = Get-Item -LiteralPath $configPath
    $dllHash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
    $launcherHash =
        (Get-FileHash -LiteralPath $launcherPath -Algorithm SHA256).Hash
    $configHash =
        (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash

    $manifest = [ordered]@{
        schema_version = 55
        payload_directory = 'ModFiles'
        status = 'UNTESTED_LOCAL_CANDIDATE'
        accepted = $false
        package_id = $packageId
        created_utc = $createdUtc.ToString('o')
        source_commit = $commit
        package_preset = $packagePreset
        titles = @(
            'Halo 3', 'Halo 3: ODST', 'Halo: Reach', 'Halo 4',
            'Halo 2 Anniversary', 'Halo 2 Classic',
            'Halo CE Anniversary', 'Halo CE Classic')
        embedded_build_identity = [ordered]@{
            source_commit = $commit
            odst = $true
            reach = $true
            reach_render = $true
            halo4 = $true
            halo2 = 'BOTH_MODES_STEREO_6DOF'
        }
        deployment_policy = [ordered]@{
            automatic_after_package = $false
            installer = 'HaloMCCVRLauncher.exe'
            launches_mcc = $false
            changes_config = $false
            interactive_installer_config_policy = 'retain-existing-and-append-missing-defaults-by-default; explicit-reset-available; backup-before-replacement'
        }
        accepted_halo4_identity = [ordered]@{
            candidate = 'C-H4-56'
            source_commit =
                '271f6dffb8cf2e13dc4feafd85b9b4c61440ff25'
        }
        halo4_candidate = [ordered]@{
            id = 'H4-WORLD-CONTACT-STAGE9-RIGHT-GRIP-MELEE'
            status = 'USER_REQUESTED_WIP_CHECKPOINT_UNACCEPTED'
            behavior = 'accepted-stage6-world-contact-plus-opt-in-openxr-velocity-native-melee'
            head_tracking = $true
            six_dof = $true
            headset_owned_pitch = $true
            headset_owned_yaw = $true
            controller_aim = $true
            haptics = $true
            head_relative_locomotion = $true
            # Exact accepted C-H4-43 player-visible behavior.
            hud = 'native-inside-captured-scene-no-redirect'
            hud_layout =
                'stage3x-native-complete-gameplay-cui-frontend-affine-and-prop-curvature-consumer'
            hud_controls = @(
                'hud_size', 'hud_aspect', 'hud_curvature',
                'hud_vertical_offset')
            hud_failure_policy =
                'stock-halo4-cui-layout-camera-effects-and-openxr-remain-armed'
            pause_reason_getter_rva = '0x000A0AE4'
            pause_reason = 3
            pause_presentation = 'native-reason-authoritative-headlocked-2d-stock-wrapper'
            pause_failure_policy = 'raw-edge-fallback-other-h4-features-remain-armed'
            local_fp_effect_suppression = $true
            effect_negative_route_rva = '0x001059A2'
            effect_helper_route_rva = '0x00100EE8'
            effect_transient_route_rva = '0x001012D5'
            effect_mode_one_gate_rva = '0x0027BD36'
            effect_cave_rva = '0x00B79C10'
            effect_cave_restored_on_cleanup = $true
            effect_policy = 'stage3ai-selected-local-first-person-finite-far'
            effect_failure_policy = 'stock-effects-camera-hud-and-openxr-remain-armed'
            helmet_default_visible = $true
            helmet_control = 'halo4_helmet'
            helmet_binding =
                'exact-3dmigoto-pixel-shader-4BE62AC49C2BF210'
            helmet_hidden_policy =
                'pssetshader-null-only-exact-visor-shader'
            helmet_geometry =
                'h4ek-container-visor-and-container-visor-glow-sibling-cui-polyart'
            helmet_geometry_transform =
                'complete-gameplay-cui-frontend-depth-excluding-private-reticle-replay-and-pause'
            helmet_failure_policy = 'stock-authored-helmet-art'
            screen_effect_blackout_fix = $true
            screen_effect_shader = 'screen-motion-suck'
            screen_effect_shader_hash = '0x47668A1953271934'
            screen_effect_scope =
                'halo4-active-and-stereo-enabled-exact-pixel-shader-only'
            screen_effect_policy =
                'pssetshader-null-preserve-already-rendered-eye'
            screen_effect_kept_native =
                'm30-speed-line-tint-alpha-and-all-other-shaders'
            screen_effect_evidence =
                'h4ek-screen-material-shader-bank-full-dxbc-byte-identical-retail-m30-cryptum-map'
            screen_effect_failure_policy =
                'stock-screen-effect-camera-hud-reticle-helmet-stereo-and-openxr-remain-armed'
            authored_crosshair = $true
            native_face_crosshair_suppressed = $true
            reticle_capture_boundary =
                'bounded-capture-eye-full-gameplay-cui-replay-into-shared-authored-texture'
            reticle_capture_hud_transform =
                'stock-affine-and-stock-curvature'
            reticle_capture_canvas =
                'private-replay-live-base-with-visible-pass-fallback'
            reticle_visible_pass_hud_transform =
                'stage3x-adjustable-affine-and-curvature'
            reticle_visible_transform_discriminator =
                'all-h4ek-type-0x28-payload-size-0x0c-markers-as-bda7-headset-confirmed'
            reticle_failure_policy =
                'stock-or-procedural-feature-fallback-camera-hands-stereo-and-openxr-remain-armed'
            parity_diagnostic = [ordered]@{
                player_visible_behavior_changed = $true
                automatic_for_this_candidate = $true
                command_bucket_count = 256
                transform_identity_slots = 32
                hot_path = 'bounded-reads-and-atomic-updates-only'
                worker_output = 'HaloMCCVR.log H4DIAG lines'
                overflow_policy = 'explicit-incomplete-census-never-merge-identities'
                protocol = 'docs/HALO4-PARITY-DIAGNOSTIC.md'
            }
            first_person_hands = $true
            arm_ik = $false
            floating_hands = $true
            weapon_follows_hand = $true
            controller_facing_orientation = $true
            orientation_source =
                'free-official-halo4-left_hand-marker-inverse-under-h3-odst-reach-mounted-controller-support-exact-c38-frozen-right-aim'
            left_presentation_trim =
                'free-minus-yaw-plus-pitch-minus-roll-matching-h3-odst-reach-support-exact-c38-shared-prepared-right-aim'
            free_left_palm =
                'official-left_hand-marker-frame-parity-no-c39-c40-c41-c42-layer'
            two_hand_left_pose =
                'byte-identical-c38-right-aim-shared-rotational-parent-with-live-left-wrist-relation-left-translation-unchanged'
            world_contact = [ordered]@{
                capability_enabled = $true
                default_enabled = $false
                config_key = 'world_collision'
                stage = 9
                rejected_stage1_enabled = $false
                rejected_stage2_enabled = $false
                rejected_stage3_enabled = $false
                rejected_stage4_weapon_publication_enabled = $false
                rejected_stage5_weapon_node_proxy_enabled = $false
                scope = 'halo4-authored-hand-and-h4ek-render-model-bounds'
                query = 'h4ek-physics-ray-cast'
                retail_query_rva = '0x001C1D4C'
                clear_line_wrapper_rva = '0x000F4218'
                clear_line_filter_global_rva = '0x02FFB0B8'
                collision_flags = '0x0002D009'
                additional_flags = '0x00005185'
                filter_identity = 'h4ek-and-retail-clear-line-wrapper-plus-live-global-value'
                environment_calibration = 'first-real-authored-volume-hit-is-reported'
                execution_context = 'post-original-live-physics-raycast-dynamic-inclusive-only'
                arbitrary_worker_physics_calls = $false
                query_interval_ms = 33
                hit_fraction_resolution = 'native-result-fraction-with-world-scaled-skin'
                render_thread_work = 'lock-free-target-and-correction-publication-only'
                collision_shape = 'fixed-seven-storm-hand-samples-plus-checksum-selected-h4ek-bounds-eight-corners-six-face-centres'
                held_weapon_behavior = 'one-stable-combined-hand-and-authored-bounds-volume-per-held-record-shares-right-hand-correction'
                h4ek_weapon_model_bounds = 39
                unknown_weapon_policy = 'hand-only-stock-weapon-fail-open'
                object_motion_rva = '0x005D1580'
                object_impulses = 'bounded-native-linear-velocity-no-angular-or-damage-write'
                ragdoll_impulses = 'same-optional-native-object-motion-path-when-ray-result-has-object-index'
                physical_melee = [ordered]@{
                    available = $true
                    default_enabled = $false
                    config_key = 'physical_melee'
                    threshold_config_key = 'physical_melee_swing_speed'
                    default_threshold_metres_per_second = 5.0
                    rejected_stage7_contact_identity_trigger_enabled = $false
                    rejected_stage8_b_crouch_route_enabled = $false
                    trigger = 'either-controller-native-openxr-velocity-or-bounded-pose-delta-fallback-threshold-crossing'
                    velocity_policy = 'meaningful-native-preferred-pose-delta-only-for-missing-or-advertised-zero-runtime-velocity'
                    proximity_and_target = 'native-halo4-melee-action'
                    locomotion_false_trigger_policy = 'runtime-tracking-space-velocity-excludes-game-world-motion'
                    hysteresis_release_ratio = 0.55
                    action = 'legacy-dormant-see-melee-candidate-note'
                    pulse_ms = 120
                    cooldown_ms = 600
                    engine_ownership = 'native-halo4-melee-damage-animation-audio-ragdoll-networking'
                    failure_isolation = 'no-camera-render-openxr-or-stage6-collision-dependency'
                }
                haptic_amplitude = 0.18
                haptic_policy = 'per-hand-peak-merged-by-max-with-stock-game-rumble'
                failure_policy = 'stock-floating-targets-only-camera-openxr-and-all-restorations-remain-armed'
                evidence = 'docs/HALO4-WORLD-COLLISION-EVIDENCE.md'
            }
            failure_policy =
                'pre-claim-stock-post-claim-frame-drop-core-remains-armed'
            vrik_failure_policy =
                'base-rigid-or-state-parent-invalid-input-leaves-that-palette-stock-while-optional-marker-parity-invalid-input-keeps-the-valid-c38-free-reroot-and-continues-right-hand-held-model-and-camera-core'
        }
        halo2_candidate = [ordered]@{
            id = 'H2-WC-3'
            status = 'READY_FOR_HEADSET_TEST_UNACCEPTED'
            module = 'halo2.dll'
            scope = 'campaign-both-renderers-groundhog-excluded'
            behavior =
                'h2ek-native-final-packet-hand-weapon-world-contact-plus-right-grip-route-physical-melee'
            world_contact = [ordered]@{
                available = $true
                default_enabled = $false
                config_key = 'world_collision'
                renderers = @('Classic', 'Anniversary')
                collision_test_vector_rva = '0x0075B850'
                collision_flags = '0x2480000F'
                query_interval_ms = 33
                publication_max_age_ms = 150
                shape = 'fixed-seven-hand-samples-plus-byte-relative-loaded-h2ek-render-model-compression-bounds-eight-corners-six-face-centres'
                object_local_velocity_rva = '0x0090A000'
                haptic_amplitude = 0.18
                physical_melee = [ordered]@{
                    available = $true
                    default_enabled = $false
                    threshold_range_metres_per_second = '0.30-10.00'
                    action = 'native-contact-or-separate-active-binding-gesture-see-melee-candidate-note'
                    velocity_policy = 'meaningful-native-openxr-preferred-bounded-pose-delta-fallback'
                }
                failure_policy = 'stock-halo2-collision-camera-stereo-packets-aim-hud-and-openxr-remain-armed'
                evidence = 'docs/HALO2-WORLD-COLLISION-EVIDENCE.md'
            }
            classic_muzzle_suppression = $true
            classic_muzzle_particle_renderer_rva = '0x0076DC90'
            classic_muzzle_live_renderer_gate_rva = '0x00E70CF8'
            classic_muzzle_predicate =
                'current-user-first-person-nonzero-and-live-classic-gate-zero'
            anniversary_muzzle_behavior = 'stock'
            classic_alignment_controls = @(
                'halo2_classic_gun_yaw_deg',
                'halo2_classic_gun_pitch_deg')
            classic_alignment_default_yaw_deg = 1.0
            classic_alignment_default_pitch_deg = -9.5
            aim_assist_view_direction_rva = '0x006C0DF0'
            melee_targeting_scope =
                'vr-owned-user0-central-calculation-only-controller-ray'
            melee_targeting_failure_policy =
                'c-h2-90-neutral-camera-and-no-target-other-features-remain-armed'
            heavy_eye_validation = 'bounded-source-discovery-only'
            # C-H2-7, E-H2-3: halo2.dll ships two renderers. The live one is
            # resolved read-only from a unique signature and reported, and the
            # classic stereo core arms only where its hooks can actually fire.
            # C-H2-8, E-H2-4: the observer is the single camera root both
            # halo2.dll renderers consume, so one write owns the headset
            # pose in the classic and the remastered mode alike.
            render_topology_probe = $false
            render_topology_probe_changes_behavior = $false
            anniversary_stereo = $true
            anniversary_stereo_hook_rva = '0x002DF190'
            anniversary_stereo_eye_camera = 'view-record-embedded-camera-rebuilt-by-engine'
            observer_6dof = $true
            observer_6dof_hook_rva = '0x006F0250'
            observer_6dof_owned_user = 0
            observer_6dof_owned_span_count = 3
            observer_6dof_owned_span_bytes = 12
            observer_6dof_writes_field_of_view = $false
            observer_6dof_engine_transform_runs_first = $true
            observer_6dof_requires_restore = $false
            observer_6dof_reaches_both_renderers = $true
            hand_mesh_context_builder_rva = '0x008181F0'
            hand_mesh_visible_consumer_rva = '0x0006BB40'
            hand_mesh_ownership = 'renderer-selected-single-transaction'
            anniversary_hand_mesh_ownership =
                'registered-render-model-callback-before-internal-copy'
            classic_packet_caller_rva = '0x007E5430'
            classic_publish_to_renderer = $false
            classic_hand_mesh_ownership =
                'persistent-packet-post-builder-plus-per-eye-draw-first-person-compensation'
            free_left_hand_presentation = 'restored-c63-controller-reroot'
            two_hand_support_presentation = 'restored-c63-authored-rigid-grip'
            right_hand_gun_transform = 'unchanged-c63-controller-barrel-alignment'
            native_aim_update_rva = '0x008FDF50'
            native_aim_ownership = 'desired-and-current-unit-aiming-vectors'
            aim_assist_disabled = $true
            aim_assist_method =
                'central-camera-control-suppression-plus-scoped-controller-view-targeting'
            aim_assist_calculation_rva = '0x00759260'
            aim_assist_scope = 'vr-owned-halo2-local-user-0-both-renderers'
            aim_assist_effect =
                'neutral-camera-assist-control-native-target-acquisition-along-controller-ray-no-tag-patching'
            aim_assist_identity =
                'official-h2ek-central-caller-and-player-control-view-helper-plus-unique-retail-loaded-image-signatures'
            aim_assist_restore =
                'central-and-view-hooks-disabled-drained-and-removed-on-core-teardown'
            aim_assist_melee_patch = $false
            melee_target_policy =
                'engine-selected-target-from-controller-ray-only-inside-owned-central-calculation'
            melee_execution_path =
                'stock-unit-action-system-and-character-physics-mode-melee'
            aim_assist_failure_policy =
                'stock-aim-assist-camera-stereo-hands-hud-reticle-weapons-and-openxr-remain-armed'
            rejected_post_return_packet_enabled = $false
            rejected_firing_helper_enabled = $false
            live_renderer_report = $true
            live_renderer_source = 'unique-signature-decoded-classic-render-gate'
            classic_render_gate_rva = '0x00E70CF8'
            applied_render_mode_rva = '0x00E21280'
            observer_result_rva = '0x015F297C'
            observer_stride = '0x368'
            stereo_arms_only_when_classic_render_tree_runs = $false
            remastered_mode_stereo_presentation = 'simultaneous-stereo'
            remastered_mode_engine_writes = $true
            identity_anchor_count = 6
            liveness_anchor_count = 2
            hook_count = 2
            caller_edge_count = 2
            both_eye_renders_per_game_frame = 2
            fresh_eye_count = 2
            both_eye_serial_policy = 'current-prepared-serial'
            both_eye_render_serials_equal_current_prepared_serial = $true
            both_eye_capture_serials_equal_current_prepared_serial = $true
            same_game_frame_pair = $true
            same_generation_resource_epoch_and_attempt_required = $true
            same_generation_level_rehook = $true
            same_generation_rebind_source =
                'cached-image-proof-rebinds-graphics-mode-observer-result-after-level-gate-reopens'
            same_generation_rebind_retry_policy = 'once-per-level-gate-epoch'
            duplicate_or_missing_eye_allowed = $false
            temporal_previous_eye_allowed = $false
            temporal_eye_gap_frames = 0
            intentional_cadence_divisor = 1
            per_eye_render_rate_equals_game_frame_rate = $true
            supported_refresh_hz = @(72, 80, 90, 120, 144)
            foreign_pause_cleared_before_claim = $true
            app_cadence_gate_hz = [ordered]@{
                min = 72
                max = 144
                source = 'current xrWaitFrame predictedDisplayPeriod and same prepared serial predictedDisplayTime delta'
                target_period_source = 'current xrWaitFrame predictedDisplayPeriod'
                delivered_delta_source = 'same prepared serial predictedDisplayTime delta'
                period_ns_min = 6944444
                period_ns_max = 13888889
                both_witnesses_required = $true
                hz_tolerance = 0
                at_45_hz = 'unclaimed-stock-screen-before-eye-render'
                at_60_hz = 'unclaimed-stock-screen-before-eye-render'
                unknown_or_outside = 'unclaimed-stock-screen-before-eye-render'
                target_90_hz_delta_22222222_ns =
                    'unclaimed-stock-screen-before-eye-render'
            }
            runtime_targeted_below_72_h2_stereo_allowed = $false
            measured_gpu_fps_statically_guaranteed = $false
            headset_actual_72_144_validation_required = $true
            post_first_complete_serial_policy =
                'previous-completed-serial-plus-one'
            serial_gap_quarantine_reason = 'CorePreparedSerialGap'
            serial_gap_quarantines_before_eye_render = $true
            serial_gap_frame_presentation = 'unclaimed-stock-screen'
            unclaimed_no_pair_presentation = 'stock-screen'
            unclaimed_no_pair_intentional_zero_layer = $false
            claimed_partial_pair_presentation = 'drop-frame'
            unclaimed_no_pair_fallback_counts_as_eye = $false
            unclaimed_no_pair_fallback_publishes_stereo_or_gameplay_heartbeat = $false
            pre_stereo_screen_path_counts_as_eye = $false
            pre_stereo_screen_path_counts_as_stereo_success = $false
            pre_stereo_screen_path_publishes_stereo_or_gameplay_heartbeat = $false
            post_claim_failure_quarantine = $true
            quarantine_scope = 'module-generation'
            max_claimed_failed_frames_before_quarantine = 1
            touched_failure_frame = 'drop'
            subsequent_untouched_frames = 'stock-screen'
            openxr_remains_available_for_structural_halo2_failure = $true
            strict_unclaimed_stock_screen_transaction = [ordered]@{
                acquire_result = 'XR_SUCCESS'
                wait_result = 'XR_SUCCESS'
                release_result = 'XR_SUCCESS'
                end_frame_result = 'XR_SUCCESS'
                source_texture_required = $true
                destination_texture_required = $true
                fast_copy_resource = [ordered]@{
                    eligibility =
                        'equal-size-equal-format-family-single-sample'
                    target_rtv_required = $false
                }
                shader_blit = [ordered]@{
                    target_rtv_required = $true
                }
                local_d3d_validation_failure_terminates_openxr = $false
                local_d3d_validation_failure_presentation =
                    'drop-current-frame-keep-session'
                local_d3d_next_frame_retry_allowed = $true
                blit_success_required = $true
                named_session_recovery = 'EnterFrameWaitFatalDrain'
                unresolved_xr_transaction_repeated_same_session_retry = $false
            }
            expected_cold_pass_line = 'Halo 2 cold observation PASS (C-H2-1)'
            expected_stereo_line = 'Halo 2 C-H2-6 simultaneous stereo + 6DOF active'
            active_line_requires_complete_exact_current_pair_survived_xr_end_frame =
                $true
            controller_input = $true
            stereo = $true
            six_dof = $true
            headset_rotation = $true
            headset_translation = $true
            controller_aim = $true
            floating_hands = $true
            physical_right_stick_preserved = $true
            controller_camera_writes = $false
            controller_xinput_synthesis = $false
            controller_owned_first_person_slot = 0
            first_person_palette_source =
                'h2ek-first-person-final-render-packet-builder-retail-rva-0x008181f0'
            hand_binding_source =
                'animation-graph-hand-flags-mapped-through-weapon-data-authored-hands-remap'
            left_controller_nodes =
                'exact-remapped-left-wrist-descendant-subtree-free-left-controller-or-two-hand-right-wrist-rigid-support-lock'
            right_controller_nodes =
                'exact-remapped-right-wrist-descendant-subtree-plus-separate-primary-held-gun-packet'
            collapsed_nodes =
                'every-final-hands-packet-node-outside-left-and-right-hand-subtrees'
            controller_rotation_space =
                'final-world-wrist-target-retains-authored-root-to-wrist-rotation-with-physical-controller-translation-and-affine-scale-about-wrist'
            projectile_aim_source =
                'h2ek-firing-helper-completed-direction-stock-muzzle-to-stable-recenter-mapped-fresh-presented-crosshair-point-with-material-deflection-telemetry'
            projectile_presented_reticle_freshness_ms = 250
            interpolation_reset_policy =
                'stock-reset-preserved-rejected-interpolator-controller-path-remains-disabled'
            same_module_hook_lease = $false
            same_module_hook_lease_scope =
                'rejected-c-h2-53-session-long-loader-reference-lease-reverted'
            loader_refcount_policy =
                'non-owning-exact-base-validation-no-game-dll-refcount-increments'
            physical_hook_teardown_policy =
                'retire-at-level-liveness-boundary-while-title-mapping-is-current'
            hud = $true
            hud_layout = 'exact-field-proven-pixel-shader-viewport-scissor-affine'
            native_chud_draw_rva = '0x007FFD70'
            hud_shader_hash_contract = '3dmigoto-unseeded-fnv1'
            hud_gameplay_shader_count = 15
            hud_controls = @(
                'hud_size', 'hud_aspect', 'hud_vertical_offset')
            hud_crosshair_shader_hash = '0x0a9b60d8f40268f6'
            native_crosshair = $true
            hud_crosshair_layout = 'native-authored-controller-aim-quad'
            native_crosshair_fallback = 'procedural-until-first-current-generation-capture'
            hud_curvature = $false
            hud_shared_across_renderer_switch = $true
            hud_failure_policy =
                'unknown-or-unavailable-shader-draws-stock-procedural-crosshair-fallback-stereo-remains-armed'
            haptics = $true
            scene_target_redirect = $false
            native_symmetric_fov_cover = $true
            native_asymmetric_fov_writes = $false
            simultaneous_stereo = $true
            runtime_hooks = $true
            engine_writes = $true
            engine_write_scope =
                'render-and-raster-position-forward-up-six-12-byte-spans-plus-vertical-fov-two-4-byte-spans-restored'
            engine_write_span_count = 8
            engine_pose_write_span_count = 6
            engine_pose_write_span_bytes = 12
            engine_vertical_fov_write_span_count = 2
            engine_vertical_fov_write_span_bytes = 4
            engine_write_restore_required = $true
            generic_draw_distance_write = $false
            halo3_regression_required = $true
            failure_policy =
                'floaty-or-aim-failure-leaves-that-feature-stock-camera-stereo-and-openxr-remain-armed'
            evidence = 'docs/HALO2-SIGNATURE-EVIDENCE.md'
        }
        gen3_world_contact_candidate = [ordered]@{
            id = 'GEN3-WC-5'
            status = 'READY_FOR_HEADSET_TEST_UNACCEPTED'
            titles = @('Halo 3', 'Halo 3: ODST', 'Halo: Reach')
            default_enabled = $false
            config_key = 'world_collision'
            physical_melee_config_key = 'physical_melee'
            threshold_range_metres_per_second = '0.30-10.00'
            default_threshold_metres_per_second = 5.0
            halo2_tag_data_slot_rva = '0x015E4B38'
            halo2_compression_count_offset = '0x14'
            halo2_compression_address_offset = '0x18'
            shared_melee_telemetry = $true
            direct_hand_npc_damage = 'experimental-incomplete-unarmed-and-secondary-response-selection'
            gesture_melee_config_key = 'gesture_melee'
            halo2_tag_data_decoder = 'exact-add-rip-disp32-at-getter-plus-0x12-displacement-plus-0x15-end-plus-0x19'
            collision_shape = 'fixed-seven-visible-hand-samples-plus-checksum-selected-editing-kit-render-model-bounds-eight-corners-six-face-centres'
            query_interval_ms = 33
            publication_max_age_ms = 150
            held_model_publication = 'actual-weapon-boneMap-zero-index-selects-solved-source-root-for-catalog-bounds'
            held_model_identity_scope = 'same-source-graph-and-title-generation-revalidated-tag-checksum-and-bounded-root-index'
            held_model_identity_max_age_ms = 150
            unknown_or_changed_model_policy = 'expire-to-hand-only'
            halo3_wrapper_rva = '0x001FFD18'
            odst_wrapper_rva = '0x00231EC4'
            reach_wrapper_rva = '0x0012C5D4'
            reach_active_scheduler_rva = '0x0012969C'
            reach_query_pose = 'raw-desired-pose-unapply-exact-prepared-pair-correction'
            halo3_active_scheduler_rva = '0x001FD748'
            odst_active_scheduler_rva = '0x0022F80C'
            halo3_odst_abi = 'five-argument-start-desired-accepted-ignore-a-ignore-b'
            halo3_odst_scheduler_abi = 'eight-argument-start-vector-test'
            reach_abi = 'four-argument-retail-specialization-ignore-b-none'
            reach_correction_consumer = 'outer-frame-explicit-prepared-wrist-targets'
            action = 'native-contact-or-separate-active-binding-gesture-see-melee-candidate-note'
            velocity_policy = 'meaningful-native-openxr-preferred-bounded-pose-delta-fallback-for-wmr-vive-style-runtimes'
            haptic_amplitude = 0.18
            failure_policy = 'feature-local-stock-fallback-camera-render-input-and-openxr-remain-armed'
            evidence = 'docs/ALL-TITLE-WORLD-COLLISION-EVIDENCE.md'
        }
        # Reach support is permanent, while player-visible optional features
        # fail open independently and never disarm the working camera core.
        reach_permanent = $true
        reach_controller_input_enabled = $true
        reach_render_candidate_compiled = $true
        reach_loaded_image_preflight_enabled = $true
        reach_display_copy_readiness_enabled = $true
        reach_camera_core_enabled = $true
        reach_controller_aim_enabled = $true
        reach_two_arm_ik_guarded = $true
        reach_fp_interpolation_palette_transaction = $true
        reach_fp_h3_odst_transaction_parity_gate = $true
        reach_hrek_authored_crosshair_enabled = $true
        reach_hrek_authored_crosshair_mandatory = $true
        reach_flat_crosshair_substitute_enabled = $false
        reach_procedural_crosshair_substitute_enabled = $false
        reach_native_hud_layout_enabled = $false
        reach_on_foot_shot_freshness_ms = 100
        reach_on_foot_shot_maximum_clip_metres = 1.5
        reach_on_foot_marker_origin_override = $false
        reach_projectile_alignment_enabled = $true
        reach_projectile_alignment_scope =
            'exact-local-reach-vehicle-central-line-plus-clipped-on-foot-controller-ray'
        reach_vehicle_view_follow_off_preserved = $true
        reach_vehicle_view_follow_render_matched_enabled = $true
        reach_vehicle_view_follow_refresh_invariant = $true
        reach_vehicle_exact_seat_entry_playspace_recenter_enabled = $true
        reach_vehicle_entry_recenter_view_follow_independent = $true
        reach_vehicle_entry_recenter_refresh_invariant = $true
        reach_vehicle_entry_recenter_heading_policy =
            'render-matched-root-or-carrier'
        reach_vehicle_entry_recenter_openxr_present_owned = $true
        reach_vehicle_entry_recenter_outer_commit_staged = $true
        reach_vehicle_camera_proof_miss_preserves_occupation = $true
        reach_vehicle_yaw_reference_atomic_pair = $true
        reach_vehicle_yaw_reference_requires_committed_frame = $true
        reach_vehicle_exit_recenter_position_only = $true
        reach_vehicle_blender_camera_defaults_enabled = $true
        reach_vehicle_retail_camera_aliases_enabled = $true
        reach_vehicle_body_hide_interval_lease_enabled = $false
        reach_vehicle_unit_camera_scoped_body_hide_enabled = $true
        reach_vehicle_native_fp_body_seated_legs_enabled = $true
        reach_vehicle_fp_body_centered_authored_pose = $true
        reach_vehicle_fp_body_failure_isolated = $true
        reach_vehicle_fp_body_identity_policy =
            'hrek-checksum-count-exact-tag-next-pair'
        reach_vehicle_fp_body_spartan_identity = '0x10041201/82'
        reach_vehicle_fp_body_elite_identity = '0x1404030E/67'
        reach_native_seated_aim_reticle_enabled = $false
        reach_controller_vehicle_reticle_enabled = $true
        reach_personal_weapon_rendered_eye_origin_enabled = $true
        reach_vehicle_barrel_origin_alignment_enabled = $false
        reach_vehicle_barrel_origin_policy = 'stock'
        reach_vehicle_selected_barrel_direction_alignment_enabled = $true
        reach_vehicle_shot_direction_policy =
            'native-selected-origin-to-presented-controller-reticle'
        reach_vehicle_shot_freshness_ms = 50
        reach_workshop_content_dependency = $false
        reach_fp_nested_camera_workspace = $true
        reach_fp_world_projection_execution_status = $true
        reach_forced_floating_hands = $true
        reach_copyresource_enabled = $true
        reach_engine_memory_writes_enabled = $true
        reach_runtime_hooks_enabled = $true
        base_release = 'MCC_VR_ALPHA_0.3.3'
        development_baseline = 'fa1f6422de2de2e94e4d36ee4c731606c30371aa'
        files = [ordered]@{
            'HaloMCCVR.dll' = [ordered]@{
                bytes = $dll.Length
                sha256 = $dllHash
            }
            'HaloMCCVRLauncher.exe' = [ordered]@{
                bytes = $launcher.Length
                sha256 = $launcherHash
            }
            'halomccvr.cfg' = [ordered]@{
                bytes = $config.Length
                sha256 = $configHash
            }
        }
        handedness_and_dual_aim_candidate = [ordered]@{
            accepted_baseline = '4e01f28b3ec5f5f8f533ac66d94978509cbcea54'
            status = 'READY_FOR_HEADSET_TEST_UNACCEPTED'
            left_handed_config_key = 'left_handed'
            left_handed_default = $false
            handedness_scope = 'anatomical-presentation-and-primary-support-pose-trigger-grip-velocity-haptic-routing'
            anatomical_mesh_mirroring = 'implemented-unaccepted-title-specific-anatomical-routing'
            halo2_dual_controller_rays = $true
            halo3_dual_controller_rays = $true
            halo3_dual_controller_ray_scope = 'new-optional-native-per-hand-acquisition-and-homing; old-failed-experiment-remains-disabled; headset-pending'
            support_grip_dual_exclusion_halo2_halo3_odst = $true
            menu_slider_last_displayed_digit_arrows = $true
            physical_melee_maximum_metres_per_second = 10.0
            world_contact_release_smoothing = 'unaccepted-10mm-120ms-bound'
            physical_melee_non_biped_targets = 'native-damageability-unaccepted'
            halo3_secondary_unarmed_melee_selection = 'implemented-unaccepted'
            snap_turn_restoration = 'implemented-all-supported-titles-headset-validation-pending'
            all_title_flat_mode_resolution = 'all-title-recovery-candidate; headset-verification-pending'
            halo2_dual_aim_publication_max_age_ms = 100
            odst_secondary_bounds_and_contact = $true
            halo3_stale_camera_retirement_ms = 2000
            crosshair_trajectory_controls = $true
            visual_support_hand_offsets_change_aim = $false
            ordinary_campaign_dual_acquisition_odst_reach_halo4 = 'removed-from-scope-by-user; not-enabled'
            independent_secondary_firing_odst_reach_halo4 = 'removed-from-scope-by-user; not-enabled'
        }
        roomscale_candidate = [ordered]@{
            config_key = 'roomscale_movement'
            default_enabled = $false
            implementation = 'native-walking-with-observed-horizontal-camera-reference-consumption'
            title_coverage = 'H2-Classic-Anniversary-H3-ODST-Reach-H4'
            head_relative_walking = $true
            controller_aim_preserved = $true
            independent_head_following_body_yaw = 'deferred-by-user-for-this-package'
            camera_and_input_freshness_ms = 100
            movement_deadband_metres = 0.02
            headset_accepted = $false
            halo2_cinematic_gate = 'fresh-first-person-packets-and-native-on-foot-sample-no-cinematic-publisher'
            halo4_on_foot_proof = 'existing-H4EK-contact-binding-unparented-local-biped'
        }
        left_hand_alignment = [ordered]@{
            default_presentation_reference = 'MCCVR-d77c9dd'
            config_key = 'experimental_hand_alignment'
            default_enabled = $false
            requires_left_handed = $true
            current_scope = 'correct-opt-in-anatomical-hand-placement-against-title-authored-grip-and-mount-data; preserve-weapon-transform'
            title_coverage = 'CE-Original-Anniversary; H2-Classic-Anniversary; H3; ODST; Reach; H4'
            headset_accepted = $false
        }
        current_accepted_source = '1a9766ca971a9e5f09b942d508abecd9753bdb35'
        weapon_interactions = [ordered]@{
            base_with_preserved_comfort_fixes = '918e2f23fb519c00aa920fde2970af0dc7df70eb'
            titles = 'CE-Original-and-Anniversary; Halo2-Classic-and-Anniversary; Halo3; ODST; Reach; Halo4'
            default_enabled = $false
            manual_reload = 'support-hip-grip-pickup; authored-per-model-insertion-and-release-haptic; native-reload-request'
            holsters = 'primary-side-shoulder-or-hip-grip; configurable-slide-or-click; independent-radii; native-carried-weapon-exchange'
            magazine_visuals = '42-authored-textured-reload-parts; native-UV-BC1-surfaces; Promethean-orange-token; shared-stereo-compositor; both-CE-H2-graphics-modes'
            custom_weapons = 'live-held-model-observation-in-all-six-titles; optional-generic-blue-reload-item-for-unfamiliar-valid-models; no-custom-ammo-or-mesh-guessing'
            needle_shake = 'optional-default-off; each-title-Needler-plus-Reach-Needle-Rifle; one-rapid-out-and-back-any-direction; no-grip; settle-to-rearm'
            input_mapping = 'user-selected-MCC-reload-and-switch-buttons-saved-per-title; initial-X-and-Y'
            admission = 'focused-tracked-on-foot-single-weapon; same-title-generation-space-options-and-bindings'
            limits = 'no-world-occlusion; native-gun-animation-magazine-retained; native-ammo-inventory-retained; no-holstered-gun-model-extra-inventory-or-empty-hand-state'
            shared_input_tests = 'gesture-timelines; both-hands; all-six-titles; cancellation-and-held-grip-release; production-pad-reader; config-roundtrip'
            evidence = 'docs/RELOAD-ACCESSORIES-2026-09-16.md'
            headset_accepted = $false
        }
        beam_cortana_comfort = [ordered]@{
            baseline_source = '35a4d096b1c535582134ab7be211eda739564aff'
            tester_source_is_latest = $true
            ce_anniversary = 'bound-residual-offscreen-source-halos-at-native-.8-width-envelope; preserve-in-raster-and-near-peripheral-flares'
            halo3 = 'automatic-facing-rebase-requires-valid-scene-and-shot; unknown-is-not-exit; generation-scoped-history'
            evidence = 'docs/CE-BEAM-H3-CORTANA-2026-09-16.md'
            headset_accepted = $false
            scope_limit = 'no-synchronized-beam-capture-or-new-headset-reproduction; symptom-resolution-and-regression-await-testing'
        }
        ce_vehicle_controls = [ordered]@{
            baseline_source = '8d9638139f02bdceae46e13c4fb8fe6966bfa0f7'
            graphics = 'Original-and-Anniversary'
            behavior = 'controller-directed-native-vehicle-steering-and-aim; preserve-native-throttle-and-on-foot-controls'
            evidence = 'docs/HALOCE-VEHICLE-CONTROL-2026-09-16.md'
            headset_accepted = $true
            acceptance_scope = '115778a-user-confirms-controller-steering-and-no-other-regression; seated-crosshair-defect-excluded; Original-log-only'
        }
        ce_vehicle_crosshair = [ordered]@{
            baseline_source = '115778af3f71f4665fe1ca1eae715884002a7405'
            graphics = 'Original-and-Anniversary'
            behavior = 'capture-native-seated-reticle-through-verified-vehicle-owner; existing-controller-ray-compositor; preserve-main-HUD-and-steering'
            evidence = 'docs/CE-VEHICLE-CROSSHAIR-2026-09-16.md'
            headset_accepted = $false
        }
        ce_community_refinement = [ordered]@{
            evidence = 'docs/CE-COMMUNITY-REPORTS-2026-09-16.md'
            classic = 'independent-native-raster-and-host-output-sizes; pinned-full-quad-scaling; cold-native-clock-install-gate'
            loading_evidence = 'docs/HALOCE-CURSED-LOADING-2026-09-16.md'
            loading_result = 'same-native-missing-beavercreek-map-loop-with-and-without-VR; user-confirmed-resolved-after-installing-required-CE-Multiplayer-content'
            loading_requirement = 'Cursed-Halo-Again-requires-both-CE-Campaign-and-CE-Multiplayer; follow-each-mod-dependency-list'
            preserved_runtime_source = 'd7dbfcbfdb4f204a72721c936d69e9f38ecda95e'
            runtime_source_changed = $true
            loading_limit = 'scoped-user-confirmation; full-custom-weapon-rig-contact-and-long-session-transition-coverage-unverified'
            frame_recovery = 'two-cold-allocated-eye-pair-banks; retain-last-complete-pair-with-original-pose-and-existing-freshness-guards; retry-failed-resource-replacement'
            controls_recovery = 'real-unwindable-turn-entry; independent-feature-retirement-and-reinstall'
            orientation_comfort = 'CE-native-body-and-grenade-packet-with-private-motion-input-basis; headset-audio-with-native-world-velocity-preserved; native-camera-effect-output-suppression; see-current-release-notes-for-exact-proof-and-limits'
            lighting = 'optional-native-lens-flare-projection-guard; native-lighting-remains-enabled'
            melee = 'CE-only-20cm-extension-of-speed-qualified-hand-and-gun-sweeps; first-obstruction-and-exact-native-damage-requery-preserved'
            edition_scope = 'both-editions-preserved; new-Game-Pass-specific-investigation-user-deferred-pending-log'
            headset_accepted = $false
        }
        dpad_controls = [ordered]@{
            head_radius = '10-50cm; existing-30cm-default; CE-left-side-proof-and-Back-click-preserved'
            quest_thumbrest = 'optional-default-OFF; physical-left-thumbrest-touch-plus-physical-right-stick'
            binding = '/user/hand/left/input/thumbrest/touch'
            optional_failure = 'retry-complete-original-Touch-bindings; no-camera-or-input-teardown'
            consumption = 'before-shared-turn-scope-and-title-snapshots; both-physical-sticks-keep-handedness'
            accepted_runtime_preserved = '7ff9697685e78389dad16126e4d1ec7188a3acbe'
            evidence = 'docs/DPAD-CONTROLS-2026-09-15.md'
            headset_accepted = $false
        }
        ce_haptics = [ordered]@{
            behavior = 'enable-existing-native-XInput-to-OpenXR-vibration-for-both-controllers'
            graphics = 'Original-and-Anniversary'
            runtime_change = 'CE-descriptor-and-armed-runtime-Haptics-capability-only'
            reference = 'unchanged-Halo3-motor-blend-short-pulse-intensity-and-stop-policy'
            accepted_runtime_preserved = '5ac02f53a7896ffd6b8dff37ddc5bc4700890559'
            evidence = 'docs/CE-HAPTICS-2026-09-15.md'
            headset_accepted = $true
        }
        halo_ce_candidate = [ordered]@{
            previous_headset_result = '5ac02f5-all-campaign-runtime-user-accepted-as-flawless-except-absent-CE-haptics'
            rejected_previous_candidate = '2cf002b-switch-crash; prepared-HUD-disabled-separately-in-8b6fd06; dump-later-proves-native-shader-lifecycle-failure-before-target-preparation; Original-feature-confirmation-preserved'
            correction = 'preserve-native-HUD-resource-lifetime-through-full-resolution-rebuild; guard-disposed-native-HUD-even-on-ordinary-fallback; repair-owned-partial-target-cleanup; Reach-native-height-control'
            graphics = 'Classic-and-Anniversary-558fb2c-headset-accepted-engine-implementation-preserved'
            native_views = 'Anniversary-prepared-before-culling; Classic-native-render-only-eye-replay'
            submitted_pose = 'exact-native-preparation-receipt'
            source_storage = 'owned-D3D11-per-eye-textures'
            graphics_gesture = 'physical-left-hand-at-left-head-side-and-movement-stick-click'
            controller_aim_hands_hud_parity = '558fb2c-both-mode-CE-implementation-user-accepted-and-preserved; shared-recovery-regression-test-pending'
            world_render_failure = 'earlier-58f71a4-copy-shape-failure-absent-in-22cb813-supplied-run; user-confirms-working-both-mode-VR; long-session-and-all-relaunch-scenarios-not-established'
            anniversary_resolution = 'full-native-height-independent-color-depth-eye-targets; double-height-packed-output; native-HUD-canvas-kept-separate; managed-owner-thread-reallocation'
            native_resolution_verifier = 'tools/re/test_ce_resolution_native.py'
            native_resolution_lifecycle_verifier = 'tools/re/test_ce_resolution_lifecycle_native.py'
            native_contact_verifier = 'tools/re/test_ce_contact_native.py'
            scene_refresh = 'native-request-only-at-one-two-camera-transition; authored-hidden-and-geometric-culling-preserved'
            desktop_mirror = 'one-completed-eye-aspect-and-gamma-correct; both-eye-submissions-preserved'
            native_scene_refresh_verifier = 'tools/re/test_ce_scene_refresh_native.py'
            anniversary_hud_replay = 'old-manual-unprepared-and-prepared-enables-remain-disabled; separate-native-ready-path-retains-coherent-late-targets-both-eye-regions; optional-failure-preserves-world'
            floating_hands = 'official-CE-arm-geometry-collapses-at-own-tracked-wrist; old-camera-origin-collapse-disabled-in-separate-commit'
            anniversary_skin_scale = 'native-copied-bone-source-owns-its-scale-through-next-prepare; stock-scale-one-byte-identical'
            anniversary_weapon_lens = 'native-material-worker-policy; current-generation-reference-gameplay-and-native-FP-selector-proof; each-eye-later-selects-own-world-matrix'
            anniversary_first_person_visibility = 'private-full-list-source-player-remap; exact-active-or-native-post-copy-worker-receipt; native-hidden-and-auxiliary-view-rules-preserved'
            native_first_person_visibility_verifier = 'tools/re/test_ce_first_person_visibility_native.py'
            native_late_hud_sequence_verifier = 'tools/re/test_ce_late_hud_sequence_native.py'
            native_hud_attachment_verifier = 'tools/re/test_ce_hud_attachment_native.py'
            recenter = 'yaw-and-position-only-reference; native-world-up; consistent-eyes-hands-movement-and-shot-direction'
            anniversary_muzzle = '22cb813-user-confirms-correct-alignment-in-both-modes; native-particle-lens-and-attachments-preserved'
            native_particle_commit_verifier = 'tools/re/test_ce_particle_commit_native.py'
            native_particle_shader_verifier = 'tools/re/test_ce_particle_shader_native.py'
            classic_startup = 'stock-Anniversary-preparation-no-longer-claims-Original-camera-guard; no-graphics-roundtrip-required-by-this-path'
            authored_crosshair = 'CE-native-RGB-only-write-mask-proven; RGB-visible-ink-and-authored-only-alpha-repair; actual-native-art-replaces-temporary-marker-after-successful-upload; existing-receipt-and-source-lifetime-guards-retained'
            native_pause = 'native-pause-retained; CE-ownership-loss-clears-head-lock-even-with-resident-module; shared-YB-shortcut'
            classic_weapon_effect_lens = 'optional-proven-first-person-consumers-retain-tracked-world-lens; native-depth-and-restore-preserved'
            performance = '22cb813-user-confirms-smooth-equal-quality-both-mode-VR; log-still-records-missed-display-deadlines; no-native-90-FPS-claim'
            transition_crashes = 'PID26776-dump-proves-null-hud_meters-after-native-resource-disposal; native-initialized-lifetime-preserved-for-authored-reload; ordinary-HUD-fallback-guarded; broad-long-session-recovery-unproven'
            physical_melee_world_collision = 'all-12-stock-CE-model-envelopes-from-21180-weapon-vertices-and-58-weighted-bones; 14-weapon-samples-plus-hand-nodes; every-weapon-sample-enters-world-and-melee-sweeps; native-biped-damage; new-headset-test-required'
            weapon_geometry_limit = 'stock-CE-physical-envelope-shared-deliberately-by-Original-and-Anniversary; triangle-exact-Saber-replacement-and-custom-model-surfaces-not-established; unknown-graphs-retain-logged-node-contact'
            native_reticle_verifier = 'tools/re/test_ce_reticle_blend_native.py'
            weapon_geometry_verifier = 'tools/re/verify_ce_weapon_mesh.py'
            physical_roomscale_body_following = 'disabled-for-CE-deferred'
            headset_accepted = $false
            editions = 'Steam-and-Microsoft-Store'
        }
        reach_hud_height = [ordered]@{
            implementation = 'HREK-proven-six-argument-native-anchor-basis; one-height-translation-after-parent-composition; captured-aim-reticle-excluded'
            native_verifier = 'tools/re/verify_reach_hud_height.py'
            evidence = 'docs/REACH-HUD-HEIGHT-2026-09-15.md'
            headset_accepted = $false
        }
        all_title_recovery = [ordered]@{
            behavior = 'selected-title-Reach-display-admission; native-CE-clock-reentry; all-title-manual-recovery-with-normal-proofs'
            titles = 'CE-Original-and-Anniversary; Halo2-Classic-and-Anniversary; Halo3; ODST; Reach; Halo4'
            preserved_ce_source = '558fb2c2237492c0458b5cee02c690be652285fe'
            ce_scoped_headset_accepted = $true
            new_recovery_headset_accepted = $true
            evidence = 'docs/ALL-TITLE-REENTRY-2026-09-15.md'
        }
        native_reload_policy = [ordered]@{
            options = 'manual_reload_disable_auto; manual_reload_skip_animations; manual_reload_shortened_animation; all-default-off-require-Manual-Reload; full-disable-takes-priority'
            titles = 'CE; Halo2; Halo3; ODST; Reach; Halo4; both editions'
            behavior = 'exact empty-trigger caller suppression; scoped first-person reload/ready playback suppression; native countdown shortening; native ammo transfer retained'
            evidence = 'docs/NATIVE-RELOAD-POLICY-2026-09-16.md'
            bindings = 'docs/NATIVE-RELOAD-BINDINGS-2026-09-16.json'
            headset_accepted = $false
        }
        vehicle_first_person = [ordered]@{
            evidence = 'docs/NATIVE-VEHICLE-FIRST-PERSON-2026-09-19.md'
            added_titles = @('Halo CE Classic/Anniversary', 'Halo 2 Classic/Anniversary', 'Halo 4')
            existing_toggle = 'vehicle_first_person'
            anchor = 'verified local seated head marker with universal trims'
            headset_accepted = $false
        }
        coop_firing_audit = [ordered]@{
            evidence = 'docs/COOP-FIRING-AUDIT-2026-09-19.md'
            reported_crash_root_cause_proven = $false
            confirmed_correction = 'H3-native-collision-exceptions-no-longer-swallowed-as-misses'
            diagnostic_log = 'HaloMCCVR-native-faults.log'
            all_feature_network_validation_complete = $false
            headset_accepted = $false
        }
        ce_multiplayer = [ordered]@{
            evidence = 'docs/CE-MULTIPLAYER-AUDIT-2026-09-19.md'
            menu = 'explicit-Menu-Start-request-retained-by-native-input-suppression-independent-of-simulation-pause'
            grenade_tracking = 'local-controller-aim-in-native-outgoing-action-before-client-prediction-and-host-submission'
            movement = 'on-foot-action-bearing-rebase; native-replicated-body-and-motion; no-later-local-only-pose-rewrites'
            vehicle_aim = 'inverse-native-seat-direction-transform; native-drive-throttle-retained'
            network_body = 'CE-protocol-shares-facing-aim-look; body-follows-controller; HMD-camera-independent'
            headset_accepted = $false
        }
        refinement_audit = [ordered]@{ evidence = 'docs/REFINEMENT-WORK-2026-09-18.md'; full_requested_scope_complete = $false; headset_accepted = $false }
        circular_zoom = [ordered]@{
            implemented_titles = 'H2-Classic-Anniversary; H3; ODST; Reach; H4'
            output_pixels = '1024x1024'
            refresh = 'every-admitted-render-frame; old-divisor-key-ignored'
            ce_separate_lens = 'unfinished; native-zoom-retained; third-view-depth-alias-proven-offline'
            headset_quality_and_timing_accepted = $false
            evidence = 'docs/ZOOM-AND-CE-FLARES-2026-09-18.md'
        }
        ce_flare_workaround = [ordered]@{
            config_key = 'ce_anniversary_disable_lens_flares'
            default_enabled = $false
            menu = 'F1-Picture'
            behavior = 'suppress-proven-Anniversary-flare-draws-only; native-world-lighting-preserved'
            reported_streak_root_cause_proven = $false
            headset_accepted = $false
        }
        current_notes = 'RELEASE-NOTES.md'
        implementation_ledger = 'IMPLEMENTATION-STATUS.md'
        launcher = [ordered]@{
            automatic_install = $false
            automatic_launch = $false
            auto_detect_editions = @('Steam', 'Microsoft Store')
            preserve_and_extend_config = $true
            per_file_sha256_manifest = 'ModFiles/INSTALL-MANIFEST.sha256'
            update_repository = 'moistman42069/MCCVR-Halo-Build'
            update_policy = 'explicit-latest-public-release-with-asset-digest-and-safe-extraction'
        }
        historical_metadata_notice = 'Older stage/profile IDs and feature results below describe inherited work and retain their original coverage limits. Current accepted baseline is Alpha 0.4.2 source 1a9766c. This unaccepted refinement audit candidate does not complete the full standing scope. Current changes and unresolved items are listed in RELEASE-NOTES.md. Earlier standing and deferred scope is preserved.'
        halo4_new_damage_blackout_report = 'deferred-unresolved-distinct-from-earlier-cryptum-shader-suppression'
        note = 'Unaccepted major QoL candidate above Alpha0.4.2 source1a9766c. Current implementation and unresolved reports are enumerated in RELEASE-NOTES.md and IMPLEMENTATION-STATUS.md. New launcher/install/update payload; both editions; no game launch or installation during preparation. Older nested stage metadata is historical, not a claim of current headset acceptance or complete community-backlog resolution.'

    }

    Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/QOL-RELEASE-NOTES-2026-09-23.md') -Destination (Join-Path $packageDir 'RELEASE-NOTES.md')
    Copy-Item -LiteralPath (Join-Path $repoRoot 'docs/QOL-IMPLEMENTATION-STATUS-2026-09-23.md') -Destination (Join-Path $packageDir 'IMPLEMENTATION-STATUS.md')
    Copy-Item -LiteralPath (Join-Path $packageDir 'RELEASE-NOTES.md') -Destination $payloadDir
    Copy-Item -LiteralPath (Join-Path $packageDir 'IMPLEMENTATION-STATUS.md') -Destination $payloadDir
    Copy-Item -LiteralPath (Join-Path $payloadDir 'MANUAL-README.txt') -Destination (Join-Path $packageDir 'README.txt')
    Copy-Item -LiteralPath $launcherPath -Destination (Join-Path $packageDir 'HaloMCCVRLauncher.exe')
    [IO.File]::WriteAllLines((Join-Path $payloadDir 'BUILD-IDENTITY.txt'),
        @("source_commit=$commit", 'build_kind=UNTESTED_LOCAL_CANDIDATE', 'release_tag='),
        [Text.UTF8Encoding]::new($false))
    # Hash the complete manual/install payload, including config, runtime,
    # fonts, licenses and identity. The manifest itself is deliberately excluded.
    $payloadPrefix = [IO.Path]::GetFullPath($payloadDir) + [IO.Path]::DirectorySeparatorChar
    $installHashes = @()
    foreach ($item in @(Get-ChildItem -LiteralPath $payloadDir -File -Recurse | Sort-Object FullName)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            -not $item.FullName.StartsWith($payloadPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe candidate payload path: $($item.FullName)"
        }
        $relative = $item.FullName.Substring($payloadPrefix.Length).Replace('\','/')
        $hash = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        $installHashes += "$hash  $relative"
        $manifest.files[$relative] = [ordered]@{bytes=$item.Length;sha256=$hash}
    }
    [IO.File]::WriteAllLines((Join-Path $payloadDir 'INSTALL-MANIFEST.sha256'),
        $installHashes, [Text.UTF8Encoding]::new($false))
    if ((Get-FileHash -LiteralPath (Join-Path $packageDir 'HaloMCCVRLauncher.exe') -Algorithm SHA256).Hash -cne $launcherHash) {
        throw 'Root and manual-payload launchers differ.'
    }

    $manifestPath = Join-Path $packageDir 'CANDIDATE-MANIFEST.json'
    $json = $manifest | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText(
        $manifestPath,
        $json + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))

    $buildZip = Join-Path $candidateRoot ("HaloMCCVR-$packageId-Build.zip")
    $sourceZip = Join-Path $candidateRoot ("HaloMCCVR-$packageId-Source.zip")
    Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $buildZip
    # Archive committed bytes, independent of this machine's Windows checkout
    # line-ending preference. This permits exact source/blob verification.
    Invoke-Tool { & git -c core.autocrlf=false -C $repoRoot archive --format=zip --prefix=Halo-MCC-VR/ `
        "--output=$sourceZip" $commit }
    if ($LASTEXITCODE -ne 0) { throw 'Matching source archive failed.' }
    $hashLines = @("Source commit: $commit")
    foreach ($archive in @($buildZip, $sourceZip)) {
        $hashLines += (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash +
            '  ' + [IO.Path]::GetFileName($archive)
    }
    [IO.File]::WriteAllLines((Join-Path $candidateRoot ("HaloMCCVR-$packageId-SHA256.txt")),
        $hashLines, [Text.UTF8Encoding]::new($false))
    Write-Host "Build ZIP:  $buildZip"
    Write-Host "Source ZIP: $sourceZip"

    Write-Host "Created untested candidate: $packageDir"
    Write-Host "Source:   $commit"
    Write-Host "DLL:      $dllHash"
    Write-Host "Launcher: $launcherHash"
    Write-Host "Config:   $configHash"

    Write-Host 'Package-only mode: no MCC installation was performed.'
}
finally {
    Pop-Location
}
