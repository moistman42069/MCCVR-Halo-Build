[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CandidateDir,

    # Every MCC edition installed on this machine. One build serves both: the
    # per-game modules the DLL hooks are byte-identical between the Steam and
    # Microsoft Store editions apart from their Authenticode signature, and the
    # launcher detects the edition at run time. A requested target whose game
    # install is not present is reported and skipped; a target that IS present
    # must install cleanly or the whole deployment fails.
    [string[]]$GameDir = @(
        'C:\Program Files (x86)\Steam\steamapps\common\Halo The Master Chief Collection\Halo_MCC_VR',
        'N:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\Halo_MCC_VR',
        'N:\XBOX\Halo- The Master Chief Collection\Content\Halo_MCC_VR'
    )
)

# Installs one already-packaged cumulative candidate. The package manifest is
# the authority: no build tree, loose DLL, old backup, or release artifact may
# enter this path. This script never launches MCC and never changes the shared
# configuration.

$ErrorActionPreference = 'Stop'

$steamExeName = 'MCC-Win64-Shipping.exe'
$storeExeName = 'MCCWinStore-Win64-Shipping.exe'
$analyserManifestPath = 'TelemetryAnalyser/analyse_mccvr_telemetry.py'
$analyserRelativePath = 'TelemetryAnalyser\analyse_mccvr_telemetry.py'
$expectedAnalyserBytes = 395283
$expectedAnalyserHash =
    '053CF61671BE281551B89A459E0611490F29D0FFD00387B43668043793BAE5B4'

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Test-ExactBoolean([object]$Value, [bool]$Expected) {
    return ($Value -is [bool]) -and ($Value -eq $Expected)
}

function Test-ExactInt32([object]$Value, [int]$Expected) {
    # Windows PowerShell's ConvertFrom-Json materializes small JSON integers as
    # Int32; PowerShell 7 materializes the same manifest tokens as Int64. Accept
    # those two integral representations only, require Int32 range, and compare
    # exactly so floating-point/string coercion still cannot pass the gate.
    if ($Value -isnot [int] -and $Value -isnot [long]) {
        return $false
    }
    $integer = [long]$Value
    return $integer -ge [int]::MinValue -and
        $integer -le [int]::MaxValue -and
        $integer -eq [long]$Expected
}

# The mod folder is only valid where a real MCC install sits beside it. For the
# Store edition the install root is the package's Content folder.
function Test-InstallRoot([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        return $false
    }
    foreach ($exe in @($steamExeName, $storeExeName)) {
        $path = Join-Path $Root (Join-Path 'MCC\Binaries\Win64' $exe)
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return $true
        }
    }
    return $false
}

function Get-EditionLabel([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        return 'unknown'
    }
    if (Test-Path -LiteralPath (Join-Path $Root 'MicrosoftGame.config') -PathType Leaf) {
        return 'store'
    }
    if (Test-Path -LiteralPath (Join-Path $Root (Join-Path 'MCC\Binaries\Win64' $storeExeName)) -PathType Leaf) {
        return 'store'
    }
    return 'steam'
}

function Assert-FileIdentity(
    [string]$Path,
    [object]$Evidence,
    [string]$Label) {
    if ($null -eq $Evidence) {
        throw "$Label manifest evidence is missing."
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing: $Path"
    }
    $item = Get-Item -LiteralPath $Path
    if ([int64]$Evidence.bytes -ne $item.Length) {
        throw "$Label length mismatch: expected $($Evidence.bytes), got $($item.Length)."
    }
    $expected = ([string]$Evidence.sha256).ToUpperInvariant()
    if ($expected -notmatch '^[0-9A-F]{64}$') {
        throw "$Label manifest SHA-256 is invalid."
    }
    $actual = Get-Sha256 $Path
    if ($actual -cne $expected) {
        throw "$Label SHA-256 mismatch: expected $expected, got $actual."
    }
    return $actual
}

# The binaries were renamed from halo3xr.* to HaloMCCVR.*. A game folder that
# keeps the old pair next to the new one lets an old launcher inject a stale
# DLL while a hash-verified candidate looks like it is under test, so obsolete
# legacy files are moved (never deleted) into the deploy-backup area, loudly.
function Move-LegacyModFiles([string]$GamePath, [string]$QuarantineDir) {
    $moved = @()
    foreach ($name in @(
            'halo3xr.dll', 'halo3xr_launcher.exe',
            'halo3xr.log', 'halo3xr.log.prev', 'halo3xr_launcher.log')) {
        $path = Join-Path $GamePath $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            if (-not (Test-Path -LiteralPath $QuarantineDir -PathType Container)) {
                New-Item -ItemType Directory -Path $QuarantineDir | Out-Null
            }
            Move-Item -LiteralPath $path `
                -Destination (Join-Path $QuarantineDir $name)
            $moved += $name
        }
    }
    if ($moved.Count -ne 0) {
        Write-Host ("  quarantined obsolete legacy files ({0}) into {1}" -f `
            ($moved -join ', '), $QuarantineDir)
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$candidateRoot = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot 'out\candidates'))
$candidatePrefix = $candidateRoot.TrimEnd(
    [IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$candidatePath = [IO.Path]::GetFullPath($CandidateDir)
if (-not $candidatePath.StartsWith(
        $candidatePrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate escaped the repository candidate directory: $candidatePath"
}
if (-not (Test-Path -LiteralPath $candidatePath -PathType Container)) {
    throw "Candidate directory does not exist: $candidatePath"
}

$manifestPath = Join-Path $candidatePath 'CANDIDATE-MANIFEST.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Candidate manifest is missing: $manifestPath"
}
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$packageId = [IO.Path]::GetFileName(
    $candidatePath.TrimEnd([IO.Path]::DirectorySeparatorChar))
$head = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $head -notmatch '^[0-9a-f]{40}$') {
    throw 'Could not resolve current source commit for deployment.'
}
$repoStatus = @(& git -C $repoRoot status --porcelain=v1 --untracked-files=normal)
if ($LASTEXITCODE -ne 0 -or $repoStatus.Count -ne 0) {
    throw 'Repository is dirty; refusing automatic deployment.'
}
if (-not (Test-ExactInt32 $manifest.schema_version 55) -or
        [string]$manifest.status -cne 'UNTESTED_LOCAL_CANDIDATE' -or
        $manifest.accepted -ne $false -or
        [string]$manifest.base_release -cne 'MCC_VR_ALPHA_0.3.3' -or
        [string]$manifest.package_preset -cne 'release' -or
        [string]$manifest.development_baseline -cne
            'fa1f6422de2de2e94e4d36ee4c731606c30371aa' -or
        [string]$manifest.package_id -cne $packageId -or
        [string]$manifest.source_commit -notmatch '^[0-9a-f]{40}$' -or
        [string]$manifest.source_commit -cne $head -or
        -not $packageId.StartsWith(
            $head.Substring(0, 7) + '-refinement-audit-',
            [StringComparison]::Ordinal) -or
        @($manifest.titles).Count -ne 8 -or
        [string]$manifest.titles[0] -cne 'Halo 3' -or
        [string]$manifest.titles[1] -cne 'Halo 3: ODST' -or
        [string]$manifest.titles[2] -cne 'Halo: Reach' -or
        [string]$manifest.titles[3] -cne 'Halo 4' -or
        [string]$manifest.titles[4] -cne 'Halo 2 Anniversary' -or
        [string]$manifest.titles[5] -cne 'Halo 2 Classic' -or
        [string]$manifest.titles[6] -cne 'Halo CE Anniversary' -or
        [string]$manifest.titles[7] -cne 'Halo CE Classic' -or
        $manifest.embedded_build_identity.source_commit -cne
            $manifest.source_commit -or
        $manifest.embedded_build_identity.odst -ne $true -or
        $manifest.embedded_build_identity.reach -ne $true -or
        $manifest.embedded_build_identity.reach_render -ne $true -or
        $manifest.embedded_build_identity.halo4 -ne $true -or
        [string]$manifest.embedded_build_identity.halo2 -cne
            'BOTH_MODES_STEREO_6DOF' -or
        [string]$manifest.accepted_halo4_identity.candidate -cne 'C-H4-56' -or
        [string]$manifest.accepted_halo4_identity.source_commit -cne
            '271f6dffb8cf2e13dc4feafd85b9b4c61440ff25' -or
        # Producer and installer advance together. This prevents a package for
        # the new source from silently carrying the preceding Halo 4 candidate's
        # behavior block, which happened repeatedly during bring-up.
        [string]$manifest.halo4_candidate.id -cne 'H4-WORLD-CONTACT-STAGE9-RIGHT-GRIP-MELEE' -or
        [string]$manifest.halo4_candidate.status -cne
            'USER_REQUESTED_WIP_CHECKPOINT_UNACCEPTED' -or
        [string]$manifest.halo4_candidate.behavior -notmatch '\S' -or
        $manifest.halo4_candidate.parity_diagnostic.player_visible_behavior_changed -ne
            $true -or
        $manifest.halo4_candidate.parity_diagnostic.automatic_for_this_candidate -ne
            $true -or
        [int]$manifest.halo4_candidate.parity_diagnostic.command_bucket_count -ne
            256 -or
        [int]$manifest.halo4_candidate.parity_diagnostic.transform_identity_slots -ne
            32 -or
        [string]$manifest.halo4_candidate.parity_diagnostic.hot_path -cne
            'bounded-reads-and-atomic-updates-only' -or
        [string]$manifest.halo4_candidate.parity_diagnostic.worker_output -cne
            'HaloMCCVR.log H4DIAG lines' -or
        [string]$manifest.halo4_candidate.parity_diagnostic.overflow_policy -cne
            'explicit-incomplete-census-never-merge-identities' -or
        [string]$manifest.halo4_candidate.failure_policy -cne
            'pre-claim-stock-post-claim-frame-drop-core-remains-armed' -or
        $manifest.halo4_candidate.authored_crosshair -ne $true -or
        $manifest.halo4_candidate.native_face_crosshair_suppressed -ne $true -or
        [string]$manifest.halo4_candidate.reticle_capture_boundary -cne
            'bounded-capture-eye-full-gameplay-cui-replay-into-shared-authored-texture' -or
        [string]$manifest.halo4_candidate.reticle_capture_hud_transform -cne
            'stock-affine-and-stock-curvature' -or
        [string]$manifest.halo4_candidate.reticle_capture_canvas -cne
            'private-replay-live-base-with-visible-pass-fallback' -or
        [string]$manifest.halo4_candidate.reticle_visible_pass_hud_transform -cne
            'stage3x-adjustable-affine-and-curvature' -or
        [string]$manifest.halo4_candidate.reticle_visible_transform_discriminator -cne
            'all-h4ek-type-0x28-payload-size-0x0c-markers-as-bda7-headset-confirmed' -or
        [string]$manifest.halo4_candidate.reticle_failure_policy -cne
            'stock-or-procedural-feature-fallback-camera-hands-stereo-and-openxr-remain-armed' -or
        [string]$manifest.halo4_candidate.hud_layout -cne
            'stage3x-native-complete-gameplay-cui-frontend-affine-and-prop-curvature-consumer' -or
        [string]$manifest.halo4_candidate.hud_failure_policy -cne
            'stock-halo4-cui-layout-camera-effects-and-openxr-remain-armed' -or
        @($manifest.halo4_candidate.hud_controls).Count -ne 4 -or
        [string]$manifest.halo4_candidate.hud_controls[0] -cne 'hud_size' -or
        [string]$manifest.halo4_candidate.hud_controls[1] -cne 'hud_aspect' -or
        [string]$manifest.halo4_candidate.hud_controls[2] -cne 'hud_curvature' -or
        [string]$manifest.halo4_candidate.hud_controls[3] -cne
            'hud_vertical_offset' -or
        [string]$manifest.halo4_candidate.pause_reason_getter_rva -cne
            '0x000A0AE4' -or
        -not (Test-ExactInt32 $manifest.halo4_candidate.pause_reason 3) -or
        [string]$manifest.halo4_candidate.pause_presentation -cne
            'native-reason-authoritative-headlocked-2d-stock-wrapper' -or
        $manifest.halo4_candidate.local_fp_effect_suppression -ne $true -or
        [string]$manifest.halo4_candidate.effect_negative_route_rva -cne
            '0x001059A2' -or
        [string]$manifest.halo4_candidate.effect_helper_route_rva -cne
            '0x00100EE8' -or
        [string]$manifest.halo4_candidate.effect_transient_route_rva -cne
            '0x001012D5' -or
        [string]$manifest.halo4_candidate.effect_mode_one_gate_rva -cne
            '0x0027BD36' -or
        [string]$manifest.halo4_candidate.effect_cave_rva -cne
            '0x00B79C10' -or
        $manifest.halo4_candidate.effect_cave_restored_on_cleanup -ne $true -or
        [string]$manifest.halo4_candidate.effect_policy -cne
            'stage3ai-selected-local-first-person-finite-far' -or
        $manifest.halo4_candidate.helmet_default_visible -ne $true -or
        [string]$manifest.halo4_candidate.helmet_control -cne
            'halo4_helmet' -or
        [string]$manifest.halo4_candidate.helmet_binding -cne
            'exact-3dmigoto-pixel-shader-4BE62AC49C2BF210' -or
        [string]$manifest.halo4_candidate.helmet_hidden_policy -cne
            'pssetshader-null-only-exact-visor-shader' -or
        [string]$manifest.halo4_candidate.helmet_geometry -cne
            'h4ek-container-visor-and-container-visor-glow-sibling-cui-polyart' -or
        [string]$manifest.halo4_candidate.helmet_geometry_transform -cne
            'complete-gameplay-cui-frontend-depth-excluding-private-reticle-replay-and-pause' -or
        $manifest.halo4_candidate.screen_effect_blackout_fix -ne $true -or
        [string]$manifest.halo4_candidate.screen_effect_shader -cne
            'screen-motion-suck' -or
        [string]$manifest.halo4_candidate.screen_effect_shader_hash -cne
            '0x47668A1953271934' -or
        [string]$manifest.halo4_candidate.screen_effect_scope -cne
            'halo4-active-and-stereo-enabled-exact-pixel-shader-only' -or
        [string]$manifest.halo4_candidate.screen_effect_policy -cne
            'pssetshader-null-preserve-already-rendered-eye' -or
        [string]$manifest.halo4_candidate.screen_effect_kept_native -cne
            'm30-speed-line-tint-alpha-and-all-other-shaders' -or
        [string]$manifest.halo4_candidate.screen_effect_evidence -cne
            'h4ek-screen-material-shader-bank-full-dxbc-byte-identical-retail-m30-cryptum-map' -or
        [string]$manifest.halo4_candidate.screen_effect_failure_policy -cne
            'stock-screen-effect-camera-hud-reticle-helmet-stereo-and-openxr-remain-armed' -or
        [string]$manifest.halo2_candidate.id -cne 'H2-WC-3' -or
        [string]$manifest.halo2_candidate.status -cne
            'READY_FOR_HEADSET_TEST_UNACCEPTED' -or
        [string]$manifest.halo2_candidate.module -cne 'halo2.dll' -or
        [string]$manifest.halo2_candidate.scope -cne
            'campaign-both-renderers-groundhog-excluded' -or
        [string]$manifest.halo2_candidate.behavior -cne
            'h2ek-native-final-packet-hand-weapon-world-contact-plus-right-grip-route-physical-melee' -or
        $manifest.halo2_candidate.classic_muzzle_suppression -ne $true -or
        [string]$manifest.halo2_candidate.classic_muzzle_particle_renderer_rva -cne
            '0x0076DC90' -or
        [string]$manifest.halo2_candidate.classic_muzzle_live_renderer_gate_rva -cne
            '0x00E70CF8' -or
        [string]$manifest.halo2_candidate.classic_muzzle_predicate -cne
            'current-user-first-person-nonzero-and-live-classic-gate-zero' -or
        [string]$manifest.halo2_candidate.anniversary_muzzle_behavior -cne
            'stock' -or
        @($manifest.halo2_candidate.classic_alignment_controls).Count -ne 2 -or
        [string]$manifest.halo2_candidate.classic_alignment_controls[0] -cne
            'halo2_classic_gun_yaw_deg' -or
        [string]$manifest.halo2_candidate.classic_alignment_controls[1] -cne
            'halo2_classic_gun_pitch_deg' -or
        [double]$manifest.halo2_candidate.classic_alignment_default_yaw_deg -ne
            1.0 -or
        [double]$manifest.halo2_candidate.classic_alignment_default_pitch_deg -ne
            -9.5 -or
        [string]$manifest.halo2_candidate.aim_assist_calculation_rva -cne
            '0x00759260' -or
        [string]$manifest.halo2_candidate.aim_assist_view_direction_rva -cne
            '0x006C0DF0' -or
        [string]$manifest.halo2_candidate.melee_targeting_scope -cne
            'vr-owned-user0-central-calculation-only-controller-ray' -or
        [string]$manifest.halo2_candidate.melee_targeting_failure_policy -cne
            'c-h2-90-neutral-camera-and-no-target-other-features-remain-armed' -or
        [string]$manifest.halo2_candidate.heavy_eye_validation -cne
            'bounded-source-discovery-only' -or
        $manifest.halo2_candidate.render_topology_probe -ne $false -or
        $manifest.halo2_candidate.render_topology_probe_changes_behavior -ne
            $false -or
        $manifest.halo2_candidate.anniversary_stereo -ne $true -or
        [string]$manifest.halo2_candidate.anniversary_stereo_hook_rva -cne
            '0x002DF190' -or
        [string]$manifest.halo2_candidate.anniversary_stereo_eye_camera -cne
            'view-record-embedded-camera-rebuilt-by-engine' -or
        $manifest.halo2_candidate.observer_6dof -ne $true -or
        [string]$manifest.halo2_candidate.observer_6dof_hook_rva -cne
            '0x006F0250' -or
        -not (Test-ExactInt32 $manifest.halo2_candidate.observer_6dof_owned_user 0) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.observer_6dof_owned_span_count 3) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.observer_6dof_owned_span_bytes 12) -or
        $manifest.halo2_candidate.observer_6dof_writes_field_of_view -ne $false -or
        $manifest.halo2_candidate.observer_6dof_engine_transform_runs_first -ne
            $true -or
        $manifest.halo2_candidate.observer_6dof_requires_restore -ne $false -or
        $manifest.halo2_candidate.observer_6dof_reaches_both_renderers -ne $true -or
        [string]$manifest.halo2_candidate.hand_mesh_context_builder_rva -cne
            '0x008181F0' -or
        [string]$manifest.halo2_candidate.hand_mesh_visible_consumer_rva -cne
            '0x0006BB40' -or
        [string]$manifest.halo2_candidate.hand_mesh_ownership -cne
            'renderer-selected-single-transaction' -or
        [string]$manifest.halo2_candidate.anniversary_hand_mesh_ownership -cne
            'registered-render-model-callback-before-internal-copy' -or
        [string]$manifest.halo2_candidate.classic_packet_caller_rva -cne
            '0x007E5430' -or
        $manifest.halo2_candidate.classic_publish_to_renderer -ne $false -or
        [string]$manifest.halo2_candidate.classic_hand_mesh_ownership -cne
            'persistent-packet-post-builder-plus-per-eye-draw-first-person-compensation' -or
        [string]$manifest.halo2_candidate.free_left_hand_presentation -cne
            'restored-c63-controller-reroot' -or
        [string]$manifest.halo2_candidate.two_hand_support_presentation -cne
            'restored-c63-authored-rigid-grip' -or
        [string]$manifest.halo2_candidate.right_hand_gun_transform -cne
            'unchanged-c63-controller-barrel-alignment' -or
        [string]$manifest.halo2_candidate.native_aim_update_rva -cne
            '0x008FDF50' -or
        [string]$manifest.halo2_candidate.native_aim_ownership -cne
            'desired-and-current-unit-aiming-vectors' -or
        $manifest.halo2_candidate.aim_assist_disabled -ne $true -or
        [string]$manifest.halo2_candidate.aim_assist_method -cne
            'central-camera-control-suppression-plus-scoped-controller-view-targeting' -or
        [string]$manifest.halo2_candidate.aim_assist_calculation_rva -cne
            '0x00759260' -or
        [string]$manifest.halo2_candidate.aim_assist_scope -cne
            'vr-owned-halo2-local-user-0-both-renderers' -or
        [string]$manifest.halo2_candidate.aim_assist_effect -cne
            'neutral-camera-assist-control-native-target-acquisition-along-controller-ray-no-tag-patching' -or
        [string]$manifest.halo2_candidate.aim_assist_identity -cne
            'official-h2ek-central-caller-and-player-control-view-helper-plus-unique-retail-loaded-image-signatures' -or
        [string]$manifest.halo2_candidate.aim_assist_restore -cne
            'central-and-view-hooks-disabled-drained-and-removed-on-core-teardown' -or
        $manifest.halo2_candidate.aim_assist_melee_patch -ne $false -or
        [string]$manifest.halo2_candidate.melee_target_policy -cne
            'engine-selected-target-from-controller-ray-only-inside-owned-central-calculation' -or
        [string]$manifest.halo2_candidate.melee_execution_path -cne
            'stock-unit-action-system-and-character-physics-mode-melee' -or
        [string]$manifest.halo2_candidate.aim_assist_failure_policy -cne
            'stock-aim-assist-camera-stereo-hands-hud-reticle-weapons-and-openxr-remain-armed' -or
        $manifest.halo2_candidate.rejected_post_return_packet_enabled -ne
            $false -or
        $manifest.halo2_candidate.rejected_firing_helper_enabled -ne $false -or
        $manifest.halo2_candidate.live_renderer_report -ne $true -or
        [string]$manifest.halo2_candidate.live_renderer_source -cne
            'unique-signature-decoded-classic-render-gate' -or
        [string]$manifest.halo2_candidate.classic_render_gate_rva -cne
            '0x00E70CF8' -or
        [string]$manifest.halo2_candidate.applied_render_mode_rva -cne
            '0x00E21280' -or
        [string]$manifest.halo2_candidate.observer_result_rva -cne
            '0x015F297C' -or
        [string]$manifest.halo2_candidate.observer_stride -cne '0x368' -or
        $manifest.halo2_candidate.stereo_arms_only_when_classic_render_tree_runs -ne
            $false -or
        [string]$manifest.halo2_candidate.remastered_mode_stereo_presentation -cne
            'simultaneous-stereo' -or
        $manifest.halo2_candidate.remastered_mode_engine_writes -ne $true -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.identity_anchor_count 6) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.liveness_anchor_count 2) -or
        -not (Test-ExactInt32 $manifest.halo2_candidate.hook_count 2) -or
        -not (Test-ExactInt32 $manifest.halo2_candidate.caller_edge_count 2) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.both_eye_renders_per_game_frame 2) -or
        -not (Test-ExactInt32 $manifest.halo2_candidate.fresh_eye_count 2) -or
        [string]$manifest.halo2_candidate.both_eye_serial_policy -cne
            'current-prepared-serial' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.both_eye_render_serials_equal_current_prepared_serial `
            $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.both_eye_capture_serials_equal_current_prepared_serial `
            $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.same_game_frame_pair $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.same_generation_resource_epoch_and_attempt_required `
            $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.same_generation_level_rehook $true) -or
        [string]$manifest.halo2_candidate.same_generation_rebind_source -cne
            'cached-image-proof-rebinds-graphics-mode-observer-result-after-level-gate-reopens' -or
        [string]$manifest.halo2_candidate.same_generation_rebind_retry_policy -cne
            'once-per-level-gate-epoch' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.duplicate_or_missing_eye_allowed `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.temporal_previous_eye_allowed $false) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.temporal_eye_gap_frames 0) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.intentional_cadence_divisor 1) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.per_eye_render_rate_equals_game_frame_rate `
            $true) -or
        @($manifest.halo2_candidate.supported_refresh_hz).Count -ne 5 -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.supported_refresh_hz[0] 72) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.supported_refresh_hz[1] 80) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.supported_refresh_hz[2] 90) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.supported_refresh_hz[3] 120) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.supported_refresh_hz[4] 144) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.foreign_pause_cleared_before_claim `
            $true) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.app_cadence_gate_hz.min 72) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.app_cadence_gate_hz.max 144) -or
        [string]$manifest.halo2_candidate.app_cadence_gate_hz.source -cne
            'current xrWaitFrame predictedDisplayPeriod and same prepared serial predictedDisplayTime delta' -or
        [string]$manifest.halo2_candidate.app_cadence_gate_hz.target_period_source -cne
            'current xrWaitFrame predictedDisplayPeriod' -or
        [string]$manifest.halo2_candidate.app_cadence_gate_hz.delivered_delta_source -cne
            'same prepared serial predictedDisplayTime delta' -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.app_cadence_gate_hz.period_ns_min `
            6944444) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.app_cadence_gate_hz.period_ns_max `
            13888889) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.app_cadence_gate_hz.both_witnesses_required `
            $true) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.app_cadence_gate_hz.hz_tolerance 0) -or
        [string]$manifest.halo2_candidate.app_cadence_gate_hz.at_45_hz -cne
            'unclaimed-stock-screen-before-eye-render' -or
        [string]$manifest.halo2_candidate.app_cadence_gate_hz.at_60_hz -cne
            'unclaimed-stock-screen-before-eye-render' -or
        [string]$manifest.halo2_candidate.app_cadence_gate_hz.unknown_or_outside -cne
            'unclaimed-stock-screen-before-eye-render' -or
        [string]$manifest.halo2_candidate.app_cadence_gate_hz.target_90_hz_delta_22222222_ns -cne
            'unclaimed-stock-screen-before-eye-render' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.runtime_targeted_below_72_h2_stereo_allowed `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.measured_gpu_fps_statically_guaranteed `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.headset_actual_72_144_validation_required `
            $true) -or
        [string]$manifest.halo2_candidate.post_first_complete_serial_policy -cne
            'previous-completed-serial-plus-one' -or
        [string]$manifest.halo2_candidate.serial_gap_quarantine_reason -cne
            'CorePreparedSerialGap' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.serial_gap_quarantines_before_eye_render `
            $true) -or
        [string]$manifest.halo2_candidate.serial_gap_frame_presentation -cne
            'unclaimed-stock-screen' -or
        [string]$manifest.halo2_candidate.unclaimed_no_pair_presentation -cne
            'stock-screen' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.unclaimed_no_pair_intentional_zero_layer `
            $false) -or
        [string]$manifest.halo2_candidate.claimed_partial_pair_presentation -cne
            'drop-frame' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.unclaimed_no_pair_fallback_counts_as_eye `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.unclaimed_no_pair_fallback_publishes_stereo_or_gameplay_heartbeat `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.pre_stereo_screen_path_counts_as_eye `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.pre_stereo_screen_path_counts_as_stereo_success `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.pre_stereo_screen_path_publishes_stereo_or_gameplay_heartbeat `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.post_claim_failure_quarantine $true) -or
        [string]$manifest.halo2_candidate.quarantine_scope -cne
            'module-generation' -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.max_claimed_failed_frames_before_quarantine `
            1) -or
        [string]$manifest.halo2_candidate.touched_failure_frame -cne 'drop' -or
        [string]$manifest.halo2_candidate.subsequent_untouched_frames -cne
            'stock-screen' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.openxr_remains_available_for_structural_halo2_failure `
            $true) -or
        [string]$manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.acquire_result -cne
            'XR_SUCCESS' -or
        [string]$manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.wait_result -cne
            'XR_SUCCESS' -or
        [string]$manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.release_result -cne
            'XR_SUCCESS' -or
        [string]$manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.end_frame_result -cne
            'XR_SUCCESS' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.source_texture_required `
            $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.destination_texture_required `
            $true) -or
        [string]$manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.fast_copy_resource.eligibility -cne
            'equal-size-equal-format-family-single-sample' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.fast_copy_resource.target_rtv_required `
            $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.shader_blit.target_rtv_required `
            $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.local_d3d_validation_failure_terminates_openxr `
            $false) -or
        [string]$manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.local_d3d_validation_failure_presentation -cne
            'drop-current-frame-keep-session' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.local_d3d_next_frame_retry_allowed `
            $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.blit_success_required `
            $true) -or
        [string]$manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.named_session_recovery -cne
            'EnterFrameWaitFatalDrain' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.strict_unclaimed_stock_screen_transaction.unresolved_xr_transaction_repeated_same_session_retry `
            $false) -or
        [string]$manifest.halo2_candidate.expected_cold_pass_line -cne
            'Halo 2 cold observation PASS (C-H2-1)' -or
        [string]$manifest.halo2_candidate.expected_stereo_line -cne
            'Halo 2 C-H2-6 simultaneous stereo + 6DOF active' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.active_line_requires_complete_exact_current_pair_survived_xr_end_frame `
            $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.controller_input $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.stereo $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.six_dof $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.headset_rotation $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.headset_translation $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.controller_aim $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.floating_hands $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.physical_right_stick_preserved $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.controller_camera_writes $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.controller_xinput_synthesis $false) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.controller_owned_first_person_slot 0) -or
        [string]$manifest.halo2_candidate.first_person_palette_source -cne
            'h2ek-first-person-final-render-packet-builder-retail-rva-0x008181f0' -or
        [string]$manifest.halo2_candidate.hand_binding_source -cne
            'animation-graph-hand-flags-mapped-through-weapon-data-authored-hands-remap' -or
        [string]$manifest.halo2_candidate.left_controller_nodes -cne
            'exact-remapped-left-wrist-descendant-subtree-free-left-controller-or-two-hand-right-wrist-rigid-support-lock' -or
        [string]$manifest.halo2_candidate.right_controller_nodes -cne
            'exact-remapped-right-wrist-descendant-subtree-plus-separate-primary-held-gun-packet' -or
        [string]$manifest.halo2_candidate.collapsed_nodes -cne
            'every-final-hands-packet-node-outside-left-and-right-hand-subtrees' -or
        [string]$manifest.halo2_candidate.controller_rotation_space -cne
            'final-world-wrist-target-retains-authored-root-to-wrist-rotation-with-physical-controller-translation-and-affine-scale-about-wrist' -or
        [string]$manifest.halo2_candidate.projectile_aim_source -cne
            'h2ek-firing-helper-completed-direction-stock-muzzle-to-stable-recenter-mapped-fresh-presented-crosshair-point-with-material-deflection-telemetry' -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.projectile_presented_reticle_freshness_ms 250) -or
        [string]$manifest.halo2_candidate.interpolation_reset_policy -cne
            'stock-reset-preserved-rejected-interpolator-controller-path-remains-disabled' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.same_module_hook_lease $false) -or
        [string]$manifest.halo2_candidate.same_module_hook_lease_scope -cne
            'rejected-c-h2-53-session-long-loader-reference-lease-reverted' -or
        [string]$manifest.halo2_candidate.loader_refcount_policy -cne
            'non-owning-exact-base-validation-no-game-dll-refcount-increments' -or
        [string]$manifest.halo2_candidate.physical_hook_teardown_policy -cne
            'retire-at-level-liveness-boundary-while-title-mapping-is-current' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.hud $true) -or
        [string]$manifest.halo2_candidate.hud_layout -cne
            'exact-field-proven-pixel-shader-viewport-scissor-affine' -or
        [string]$manifest.halo2_candidate.native_chud_draw_rva -cne
            '0x007FFD70' -or
        [string]$manifest.halo2_candidate.hud_shader_hash_contract -cne
            '3dmigoto-unseeded-fnv1' -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.hud_gameplay_shader_count 15) -or
        @($manifest.halo2_candidate.hud_controls).Count -ne 3 -or
        [string]$manifest.halo2_candidate.hud_controls[0] -cne
            'hud_size' -or
        [string]$manifest.halo2_candidate.hud_controls[1] -cne
            'hud_aspect' -or
        [string]$manifest.halo2_candidate.hud_controls[2] -cne
            'hud_vertical_offset' -or
        [string]$manifest.halo2_candidate.hud_crosshair_shader_hash -cne
            '0x0a9b60d8f40268f6' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.native_crosshair $true) -or
        [string]$manifest.halo2_candidate.hud_crosshair_layout -cne
            'native-authored-controller-aim-quad' -or
        [string]$manifest.halo2_candidate.native_crosshair_fallback -cne
            'procedural-until-first-current-generation-capture' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.hud_curvature $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.hud_shared_across_renderer_switch `
            $true) -or
        [string]$manifest.halo2_candidate.hud_failure_policy -cne
            'unknown-or-unavailable-shader-draws-stock-procedural-crosshair-fallback-stereo-remains-armed' -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.haptics $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.scene_target_redirect $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.native_symmetric_fov_cover $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.native_asymmetric_fov_writes $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.simultaneous_stereo $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.runtime_hooks $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.engine_writes $true) -or
        [string]$manifest.halo2_candidate.engine_write_scope -cne
            'render-and-raster-position-forward-up-six-12-byte-spans-plus-vertical-fov-two-4-byte-spans-restored' -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.engine_write_span_count 8) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.engine_pose_write_span_count 6) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.engine_pose_write_span_bytes 12) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.engine_vertical_fov_write_span_count 2) -or
        -not (Test-ExactInt32 `
            $manifest.halo2_candidate.engine_vertical_fov_write_span_bytes 4) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.engine_write_restore_required $true) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.generic_draw_distance_write $false) -or
        -not (Test-ExactBoolean `
            $manifest.halo2_candidate.halo3_regression_required $true) -or
        [string]$manifest.halo2_candidate.failure_policy -cne
            'floaty-or-aim-failure-leaves-that-feature-stock-camera-stereo-and-openxr-remain-armed' -or
        [string]$manifest.halo2_candidate.evidence -cne
            'docs/HALO2-SIGNATURE-EVIDENCE.md' -or
        [string]$manifest.gen3_world_contact_candidate.id -cne 'GEN3-WC-5' -or
        [string]$manifest.gen3_world_contact_candidate.halo3_active_scheduler_rva -cne '0x001FD748' -or
        [string]$manifest.gen3_world_contact_candidate.odst_active_scheduler_rva -cne '0x0022F80C' -or
        [string]$manifest.gen3_world_contact_candidate.halo2_tag_data_slot_rva -cne '0x015E4B38' -or
        [string]$manifest.gen3_world_contact_candidate.held_model_identity_scope -cne
            'same-source-graph-and-title-generation-revalidated-tag-checksum-and-bounded-root-index' -or
        -not (Test-ExactInt32 $manifest.gen3_world_contact_candidate.query_interval_ms 33) -or
        -not (Test-ExactInt32 $manifest.gen3_world_contact_candidate.held_model_identity_max_age_ms 150) -or
        $manifest.gen3_world_contact_candidate.default_threshold_metres_per_second -ne 5.0 -or
        $manifest.deployment_policy.automatic_after_package -ne $false -or
        [string]$manifest.deployment_policy.installer -cne
            'tools/install-candidate.ps1' -or
        $manifest.deployment_policy.launches_mcc -ne $false -or
        $manifest.deployment_policy.changes_config -ne $false -or
        $manifest.reach_fp_h3_odst_transaction_parity_gate -ne $true -or
        $manifest.reach_fp_nested_camera_workspace -ne $true -or
        $manifest.reach_fp_world_projection_execution_status -ne $true -or
        $manifest.reach_forced_floating_hands -ne $true -or
        $manifest.reach_vehicle_view_follow_off_preserved -ne $true -or
        $manifest.reach_vehicle_view_follow_render_matched_enabled -ne $true -or
        $manifest.reach_vehicle_view_follow_refresh_invariant -ne $true -or
        $manifest.reach_vehicle_exact_seat_entry_playspace_recenter_enabled -ne
            $true -or
        $manifest.reach_vehicle_entry_recenter_view_follow_independent -ne
            $true -or
        $manifest.reach_vehicle_entry_recenter_refresh_invariant -ne $true -or
        [string]$manifest.reach_vehicle_entry_recenter_heading_policy -cne
            'render-matched-root-or-carrier' -or
        $manifest.reach_vehicle_entry_recenter_openxr_present_owned -ne $true -or
        $manifest.reach_vehicle_entry_recenter_outer_commit_staged -ne $true -or
        $manifest.reach_vehicle_camera_proof_miss_preserves_occupation -ne
            $true -or
        $manifest.reach_vehicle_yaw_reference_atomic_pair -ne $true -or
        $manifest.reach_vehicle_yaw_reference_requires_committed_frame -ne
            $true -or
        $manifest.reach_vehicle_exit_recenter_position_only -ne $true -or
        $manifest.reach_vehicle_blender_camera_defaults_enabled -ne $true -or
        $manifest.reach_vehicle_retail_camera_aliases_enabled -ne $true -or
        $manifest.reach_projectile_alignment_enabled -ne $true -or
        -not (Test-ExactInt32 $manifest.reach_on_foot_shot_freshness_ms 100) -or
        $manifest.reach_on_foot_shot_maximum_clip_metres -ne 1.5 -or
        -not (Test-ExactBoolean $manifest.reach_on_foot_marker_origin_override $false) -or
        [string]$manifest.gen3_world_contact_candidate.reach_active_scheduler_rva -cne '0x0012969C' -or
        [string]$manifest.gen3_world_contact_candidate.reach_query_pose -cne 'raw-desired-pose-unapply-exact-prepared-pair-correction' -or
        [string]$manifest.gen3_world_contact_candidate.halo2_compression_count_offset -cne '0x14' -or
        [string]$manifest.gen3_world_contact_candidate.halo2_compression_address_offset -cne '0x18' -or
        -not (Test-ExactBoolean $manifest.gen3_world_contact_candidate.shared_melee_telemetry $true) -or
        [string]$manifest.gen3_world_contact_candidate.direct_hand_npc_damage -cne
            'experimental-incomplete-unarmed-and-secondary-response-selection' -or
        [string]$manifest.reach_projectile_alignment_scope -cne
            'exact-local-reach-vehicle-central-line-plus-clipped-on-foot-controller-ray' -or
        $manifest.reach_vehicle_body_hide_interval_lease_enabled -ne $false -or
        $manifest.reach_vehicle_unit_camera_scoped_body_hide_enabled -ne $true -or
        $manifest.reach_vehicle_native_fp_body_seated_legs_enabled -ne $true -or
        $manifest.reach_vehicle_fp_body_centered_authored_pose -ne $true -or
        $manifest.reach_vehicle_fp_body_failure_isolated -ne $true -or
        [string]$manifest.reach_vehicle_fp_body_identity_policy -cne
            'hrek-checksum-count-exact-tag-next-pair' -or
        [string]$manifest.reach_vehicle_fp_body_spartan_identity -cne
            '0x10041201/82' -or
        [string]$manifest.reach_vehicle_fp_body_elite_identity -cne
            '0x1404030E/67' -or
        $manifest.reach_native_seated_aim_reticle_enabled -ne $false -or
        $manifest.reach_controller_vehicle_reticle_enabled -ne $true -or
        $manifest.reach_personal_weapon_rendered_eye_origin_enabled -ne $true -or
        $manifest.reach_vehicle_barrel_origin_alignment_enabled -ne $false -or
        [string]$manifest.reach_vehicle_barrel_origin_policy -cne 'stock' -or
        $manifest.reach_vehicle_selected_barrel_direction_alignment_enabled -ne
            $true -or
        [string]$manifest.reach_vehicle_shot_direction_policy -cne
            'native-selected-origin-to-presented-controller-reticle' -or
        [int]$manifest.reach_vehicle_shot_freshness_ms -ne 50 -or
        $manifest.reach_workshop_content_dependency -ne $false -or
        $manifest.reach_runtime_hooks_enabled -ne $true) {
    throw 'Candidate manifest identity or cumulative-title contract is invalid.'
}

$analyserProperties = @($manifest.files.PSObject.Properties | Where-Object {
    $_.Name -ceq $analyserManifestPath
})
if ($analyserProperties.Count -ne 1) {
    throw "Candidate manifest must contain exactly one $analyserManifestPath entry."
}
$analyserEvidence = $analyserProperties[0].Value
if (-not (Test-ExactInt32 $analyserEvidence.bytes $expectedAnalyserBytes) -or
        [string]$analyserEvidence.sha256 -cne $expectedAnalyserHash) {
    throw 'Candidate manifest does not identify the approved telemetry analyser.'
}

$candidateDll = Join-Path $candidatePath 'HaloMCCVR.dll'
$candidateLauncher = Join-Path $candidatePath 'HaloMCCVRLauncher.exe'
$candidateConfig = Join-Path $candidatePath 'halomccvr.cfg'
$candidateAnalyser = [IO.Path]::GetFullPath(
    (Join-Path $candidatePath $analyserRelativePath))
$candidateContentPrefix = $candidatePath.TrimEnd(
    [IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $candidateAnalyser.StartsWith(
        $candidateContentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Candidate telemetry analyser path escaped the package: $candidateAnalyser"
}
$dllHash = Assert-FileIdentity `
    $candidateDll $manifest.files.'HaloMCCVR.dll' 'Candidate DLL'
$launcherHash = Assert-FileIdentity `
    $candidateLauncher $manifest.files.'HaloMCCVRLauncher.exe' 'Candidate launcher'
$configHash = Assert-FileIdentity `
    $candidateConfig $manifest.files.'halomccvr.cfg' 'Candidate seed config'
$analyserHash = Assert-FileIdentity `
    $candidateAnalyser $analyserEvidence 'Candidate telemetry analyser'

$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -in @(
        'MCC-Win64-Shipping', 'MCCWinStore-Win64-Shipping', 'HaloMCCVRLauncher', 'halo3xr_launcher')
})
if ($running.Count -ne 0) {
    $owners = ($running | ForEach-Object {
        '{0}:{1}' -f $_.ProcessName, $_.Id
    }) -join ', '
    throw "MCC or its launcher is running ($owners); automatic install made no changes."
}

$backupRoot = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot 'out\deploy-backups'))
$expectedBackupPrefix = [IO.Path]::GetFullPath(
    (Join-Path $repoRoot 'out')) + [IO.Path]::DirectorySeparatorChar
if (-not $backupRoot.StartsWith(
        $expectedBackupPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Backup root escaped the repository out directory: $backupRoot"
}

# Resolve every requested target up front. A target whose MCC install is absent
# is reported and skipped - that edition simply is not on this machine - but at
# least one must be present, and every present one must install cleanly.
$targets = @()
foreach ($requested in $GameDir) {
    $resolved = [IO.Path]::GetFullPath($requested)
    $installRoot = Split-Path -Parent $resolved
    $targets += [pscustomobject]@{
        GameDir     = $resolved
        InstallRoot = $installRoot
        Present     = (Test-InstallRoot $installRoot)
        Edition     = (Get-EditionLabel $installRoot)
    }
}
if (@($targets | Where-Object { $_.Present }).Count -eq 0) {
    throw ('No Halo: MCC install was found for any requested deployment target: ' +
        (($targets | ForEach-Object { $_.GameDir }) -join ', '))
}

# A mod folder created for the first time inherits the live configuration from
# an edition that already has one, so both editions run identical settings. An
# existing halomccvr.cfg is never touched.
$seedConfig = $null
foreach ($target in $targets) {
    $existingConfig = Join-Path $target.GameDir 'halomccvr.cfg'
    if ($target.Present -and (Test-Path -LiteralPath $existingConfig -PathType Leaf)) {
        $seedConfig = $existingConfig
        break
    }
}
if ($null -eq $seedConfig) {
    $seedConfig = $candidateConfig
}

$createdUtc = [DateTime]::UtcNow
$results = @()

foreach ($target in $targets) {
    $gamePath = $target.GameDir
    if (-not $target.Present) {
        Write-Warning ("SKIPPED - no Halo: MCC install under {0}, so nothing was written to {1}" -f `
            $target.InstallRoot, $gamePath)
        $results += [pscustomobject]@{
            GameDir = $gamePath; Edition = $target.Edition
            Action = 'skipped-not-installed'; Backup = $null
        }
        continue
    }

    $installedDll = Join-Path $gamePath 'HaloMCCVR.dll'
    $installedLauncher = Join-Path $gamePath 'HaloMCCVRLauncher.exe'
    $installedAnalyserDir = Join-Path $gamePath 'TelemetryAnalyser'
    $installedAnalyser = [IO.Path]::GetFullPath(
        (Join-Path $gamePath $analyserRelativePath))
    $gameContentPrefix = $gamePath.TrimEnd(
        [IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $installedAnalyser.StartsWith(
            $gameContentPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Installed telemetry analyser path escaped the mod folder: $installedAnalyser"
    }
    $configPath = Join-Path $gamePath 'halomccvr.cfg'
    $logPath = Join-Path $gamePath 'HaloMCCVR.log'
    $hasExistingPair =
        (Test-Path -LiteralPath $gamePath -PathType Container) -and
        (Test-Path -LiteralPath $installedDll -PathType Leaf) -and
        (Test-Path -LiteralPath $installedLauncher -PathType Leaf)

    if (-not $hasExistingPair) {
        # First install into a verified MCC install root. There is no prior pair
        # to preserve, so the folder is seeded instead of refused.
        Write-Host ("First install for the {0} edition: {1}" -f $target.Edition, $gamePath)
        if (-not (Test-Path -LiteralPath $gamePath -PathType Container)) {
            New-Item -ItemType Directory -Path $gamePath | Out-Null
        }
        foreach ($extra in @('LICENSE', 'MANUAL-README.txt')) {
            $extraSource = Join-Path $candidatePath $extra
            if (Test-Path -LiteralPath $extraSource -PathType Leaf) {
                Copy-Item -LiteralPath $extraSource `
                    -Destination (Join-Path $gamePath $extra) -Force
            }
        }
        if ($null -ne $seedConfig -and
                -not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
            Copy-Item -LiteralPath $seedConfig -Destination $configPath
            Write-Host "  seeded halomccvr.cfg from $seedConfig"
        }
        if (-not (Test-Path -LiteralPath $installedAnalyserDir -PathType Container)) {
            New-Item -ItemType Directory -Path $installedAnalyserDir | Out-Null
        }
        Copy-Item -LiteralPath $candidateDll -Destination $installedDll -Force
        Copy-Item -LiteralPath $candidateLauncher -Destination $installedLauncher -Force
        Copy-Item -LiteralPath $candidateAnalyser `
            -Destination $installedAnalyser -Force
        $newDllHash = Get-Sha256 $installedDll
        $newLauncherHash = Get-Sha256 $installedLauncher
        $newAnalyserHash = Assert-FileIdentity `
            $installedAnalyser $analyserEvidence 'Installed telemetry analyser'
        if ($newDllHash -cne $dllHash -or
                $newLauncherHash -cne $launcherHash -or
                $newAnalyserHash -cne $analyserHash) {
            throw ("First-install hash mismatch at {0}: DLL={1} launcher={2} analyser={3}" -f `
                $gamePath, $newDllHash, $newLauncherHash, $newAnalyserHash)
        }
        Write-Host "  installed DLL:      $newDllHash"
        Write-Host "  installed launcher: $newLauncherHash"
        Write-Host "  installed analyser: $newAnalyserHash"
        Move-LegacyModFiles $gamePath (Join-Path $backupRoot `
            ('legacy-{0}-{1}' -f $target.Edition,
                $createdUtc.ToString("yyyyMMdd-HHmmssfff'Z'")))
        $results += [pscustomobject]@{
            GameDir = $gamePath; Edition = $target.Edition
            Action = 'first-install'; Backup = $null
        }
        continue
    }

    $priorDllHash = Get-Sha256 $installedDll
    $priorLauncherHash = Get-Sha256 $installedLauncher
    $installedAnalyserMatches =
        (Test-Path -LiteralPath $installedAnalyser -PathType Leaf) -and
        ((Get-Item -LiteralPath $installedAnalyser).Length -eq
            [int64]$analyserEvidence.bytes) -and
        ((Get-Sha256 $installedAnalyser) -ceq $analyserHash)
    if ($priorDllHash -ceq $dllHash -and
            $priorLauncherHash -ceq $launcherHash -and
            $installedAnalyserMatches) {
        $verifiedInstalledAnalyserHash = Assert-FileIdentity `
            $installedAnalyser $analyserEvidence 'Installed telemetry analyser'
        Write-Host ("Already installed for the {0} edition: {1}" -f $target.Edition, $gamePath)
        Write-Host "  installed analyser: $verifiedInstalledAnalyserHash"
        Move-LegacyModFiles $gamePath (Join-Path $backupRoot `
            ('legacy-{0}-{1}' -f $target.Edition,
                $createdUtc.ToString("yyyyMMdd-HHmmssfff'Z'")))
        $results += [pscustomobject]@{
            GameDir = $gamePath; Edition = $target.Edition
            Action = 'already-installed'; Backup = $null
        }
        continue
    }

    $backupId = '{0}-{1}-before-{2}-{3}' -f `
        $priorDllHash.Substring(0, 7).ToLowerInvariant(),
        $target.Edition,
        $manifest.source_commit.Substring(0, 7),
        $createdUtc.ToString("yyyyMMdd-HHmmssfff'Z'")
    $backupDir = Join-Path $backupRoot $backupId
    if (Test-Path -LiteralPath $backupDir) {
        throw "Refusing to reuse deployment backup directory: $backupDir"
    }
    New-Item -ItemType Directory -Path $backupDir | Out-Null

    $configHashBefore = $null
    $priorAnalyserHash = $null
    Copy-Item -LiteralPath $installedDll -Destination `
        (Join-Path $backupDir 'HaloMCCVR.dll')
    Copy-Item -LiteralPath $installedLauncher -Destination `
        (Join-Path $backupDir 'HaloMCCVRLauncher.exe')
    if (Test-Path -LiteralPath $configPath -PathType Leaf) {
        $configHashBefore = Get-Sha256 $configPath
        Copy-Item -LiteralPath $configPath -Destination `
            (Join-Path $backupDir 'halomccvr.cfg')
    }
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        Copy-Item -LiteralPath $logPath -Destination `
            (Join-Path $backupDir 'HaloMCCVR.log')
    }
    if (Test-Path -LiteralPath $installedAnalyser -PathType Leaf) {
        $priorAnalyserHash = Get-Sha256 $installedAnalyser
        $backupAnalyserDir = Join-Path $backupDir 'TelemetryAnalyser'
        New-Item -ItemType Directory -Path $backupAnalyserDir | Out-Null
        Copy-Item -LiteralPath $installedAnalyser -Destination `
            (Join-Path $backupAnalyserDir 'analyse_mccvr_telemetry.py')
        if ((Get-Sha256 (Join-Path $backupAnalyserDir `
                    'analyse_mccvr_telemetry.py')) -cne $priorAnalyserHash) {
            throw 'Telemetry analyser backup verification failed; automatic install made no changes.'
        }
    }

    if ((Get-Sha256 (Join-Path $backupDir 'HaloMCCVR.dll')) -cne $priorDllHash -or
            (Get-Sha256 (Join-Path $backupDir 'HaloMCCVRLauncher.exe')) -cne
                $priorLauncherHash) {
        throw 'Deployment backup verification failed; automatic install made no changes.'
    }

    $stagedDll = Join-Path $gamePath ("HaloMCCVR.dll.$packageId.pending")
    $stagedLauncher = Join-Path $gamePath `
        ("HaloMCCVRLauncher.exe.$packageId.pending")
    if (-not (Test-Path -LiteralPath $installedAnalyserDir -PathType Container)) {
        New-Item -ItemType Directory -Path $installedAnalyserDir | Out-Null
    }
    $stagedAnalyser = Join-Path $installedAnalyserDir `
        ("analyse_mccvr_telemetry.py.$packageId.pending")
    if ((Test-Path -LiteralPath $stagedDll) -or
            (Test-Path -LiteralPath $stagedLauncher) -or
            (Test-Path -LiteralPath $stagedAnalyser)) {
        throw 'A candidate staging file already exists; refusing to overwrite it.'
    }

    try {
        Copy-Item -LiteralPath $candidateDll -Destination $stagedDll
        Copy-Item -LiteralPath $candidateLauncher -Destination $stagedLauncher
        Copy-Item -LiteralPath $candidateAnalyser -Destination $stagedAnalyser
        if ((Get-Sha256 $stagedDll) -cne $dllHash -or
                (Get-Sha256 $stagedLauncher) -cne $launcherHash -or
                (Assert-FileIdentity $stagedAnalyser $analyserEvidence `
                    'Staged telemetry analyser') -cne $analyserHash) {
            throw 'Staged candidate hash verification failed; installed files remain unchanged.'
        }

        Copy-Item -LiteralPath $stagedDll -Destination $installedDll -Force
        Copy-Item -LiteralPath $stagedLauncher -Destination $installedLauncher -Force
        Copy-Item -LiteralPath $stagedAnalyser `
            -Destination $installedAnalyser -Force
    }
    finally {
        if (Test-Path -LiteralPath $stagedDll -PathType Leaf) {
            Remove-Item -LiteralPath $stagedDll -Force
        }
        if (Test-Path -LiteralPath $stagedLauncher -PathType Leaf) {
            Remove-Item -LiteralPath $stagedLauncher -Force
        }
        if (Test-Path -LiteralPath $stagedAnalyser -PathType Leaf) {
            Remove-Item -LiteralPath $stagedAnalyser -Force
        }
    }

    $installedDllHash = Get-Sha256 $installedDll
    $installedLauncherHash = Get-Sha256 $installedLauncher
    $installedAnalyserHash = Assert-FileIdentity `
        $installedAnalyser $analyserEvidence 'Installed telemetry analyser'
    if ($installedDllHash -cne $dllHash -or
            $installedLauncherHash -cne $launcherHash -or
            $installedAnalyserHash -cne $analyserHash) {
        throw ("Post-install hash mismatch at {0}: DLL={1} launcher={2} analyser={3}" -f `
            $gamePath, $installedDllHash, $installedLauncherHash,
            $installedAnalyserHash)
    }
    if ($null -ne $configHashBefore -and
            (Get-Sha256 $configPath) -cne $configHashBefore) {
        throw 'Shared configuration changed during deployment.'
    }
    Move-LegacyModFiles $gamePath (Join-Path $backupDir 'legacy')

    $deployment = [ordered]@{
        schema_version = 2
        deployed_utc = $createdUtc.ToString('o')
        package_id = $packageId
        source_commit = [string]$manifest.source_commit
        game_dir = $gamePath
        edition = $target.Edition
        previous = [ordered]@{
            halo3xr_dll_sha256 = $priorDllHash
            halo3xr_launcher_sha256 = $priorLauncherHash
            telemetry_analyser_sha256 = $priorAnalyserHash
        }
        installed = [ordered]@{
            halo3xr_dll_sha256 = $installedDllHash
            halo3xr_launcher_sha256 = $installedLauncherHash
            telemetry_analyser_sha256 = $installedAnalyserHash
            config_sha256 = $configHashBefore
        }
        launched = $false
    }
    $deploymentPath = Join-Path $backupDir 'DEPLOYMENT-MANIFEST.json'
    [IO.File]::WriteAllText(
        $deploymentPath,
        ($deployment | ConvertTo-Json -Depth 5) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))

    Write-Host ("Installed for the {0} edition: {1}" -f $target.Edition, $gamePath)
    Write-Host "  installed DLL:      $installedDllHash"
    Write-Host "  installed launcher: $installedLauncherHash"
    Write-Host "  installed analyser: $installedAnalyserHash"
    Write-Host "  preserved previous: $backupDir"
    $results += [pscustomobject]@{
        GameDir = $gamePath; Edition = $target.Edition
        Action = 'installed'; Backup = $backupDir
    }
}

Write-Host ''
Write-Host "Automatically installed candidate: $packageId"
Write-Host "Installed source:   $($manifest.source_commit)"
Write-Host "Candidate DLL:      $dllHash"
Write-Host "Candidate launcher: $launcherHash"
Write-Host "Candidate analyser: $analyserHash"
foreach ($result in $results) {
    Write-Host ("  [{0,-6}] {1,-22} {2}" -f $result.Edition, $result.Action, $result.GameDir)
}
Write-Host 'MCC was not launched and no existing halomccvr.cfg was changed.'
