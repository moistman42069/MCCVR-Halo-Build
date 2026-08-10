[CmdletBinding()]
param(
    # Force a from-scratch compile. Off by default: the packaged identity comes
    # from the git commit check and the SHA-256 of the installed files, not from
    # discarding object files, and a clean rebuild cost minutes on every single
    # candidate.
    [switch]$Clean
)

# Halo MCC VR is one cumulative build: Halo 3 + ODST + Halo: Reach + Halo 4.
# Reach's camera core is permanent while Halo 4 is still an explicitly
# unaccepted bring-up line. Optional player-visible features fail open
# independently. This stages one unaccepted local candidate under out/candidates
# after a passing build and tests, then automatically installs those
# exact manifest-verified bytes into the dedicated MCC mod directory. It never
# launches MCC and never labels rebuilt bytes as an accepted release.

$ErrorActionPreference = 'Stop'

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

    $acceptedSources = [ordered]@{
        'cumulative Halo 3/ODST/Reach' =
            'a5524d3fe58e4ed5507c27429ccca52a3d4fdf7d'
        'accepted Halo 4 C-H4-1' =
            '954359b7f786b78c76824b662ead3c1fc8cd7917'
    }
    foreach ($acceptedLine in $acceptedSources.GetEnumerator()) {
        & git -C $repoRoot merge-base --is-ancestor `
            $acceptedLine.Value $commit
        if ($LASTEXITCODE -ne 0) {
            throw "Refusing to package: HEAD does not descend from $($acceptedLine.Name) source $($acceptedLine.Value)."
        }
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
        throw 'Refusing to package C-H4-7: the Halo 4 camera core is not ON.'
    }

    # Incremental. A clean rebuild was recompiling the whole tree for every
    # candidate, which is minutes per iteration for no safety: the packaged
    # identity is proven by the git commit check above plus the SHA-256 of the
    # exact installed files, not by how the object files were produced. Use
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
        'halo4-c37-free-left-palm-down',
        $createdUtc.ToString("yyyyMMdd-HHmmssfff'Z'")
    $packageDir = Join-Path $candidateRoot $packageId
    if (Test-Path -LiteralPath $packageDir) {
        throw "Refusing to reuse candidate directory: $packageDir"
    }

    Invoke-Tool { & cmake --install $packageBuildDir --config Release `
        --prefix $packageDir --component dist }
    if ($LASTEXITCODE -ne 0) {
        throw 'Candidate staging failed.'
    }

    $dllPath = Join-Path $packageDir 'halo3xr.dll'
    $launcherPath = Join-Path $packageDir 'halo3xr_launcher.exe'
    foreach ($requiredPath in @(
            $dllPath,
            $launcherPath,
            (Join-Path $packageDir 'LICENSE'),
            (Join-Path $packageDir 'MANUAL-README.txt'))) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Candidate package is missing: $requiredPath"
        }
    }

    $dll = Get-Item -LiteralPath $dllPath
    $launcher = Get-Item -LiteralPath $launcherPath
    $dllHash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
    $launcherHash =
        (Get-FileHash -LiteralPath $launcherPath -Algorithm SHA256).Hash

    $manifest = [ordered]@{
        schema_version = 8
        status = 'UNTESTED_LOCAL_CANDIDATE'
        accepted = $false
        package_id = $packageId
        created_utc = $createdUtc.ToString('o')
        source_commit = $commit
        package_preset = $packagePreset
        titles = @('Halo 3', 'Halo 3: ODST', 'Halo: Reach', 'Halo 4')
        embedded_build_identity = [ordered]@{
            source_commit = $commit
            odst = $true
            reach = $true
            reach_render = $true
            halo4 = $true
        }
        deployment_policy = [ordered]@{
            automatic_after_package = $true
            installer = 'tools/install-candidate.ps1'
            launches_mcc = $false
            changes_config = $false
        }
        accepted_halo4_identity = [ordered]@{
            candidate = 'C-H4-1'
            source_commit =
                '954359b7f786b78c76824b662ead3c1fc8cd7917'
        }
        halo4_candidate = [ordered]@{
            id = 'C-H4-37'
            status = 'OFFLINE_PASS_HEADSET_PENDING'
            behavior = 'c36-controller-facing-hands-and-gun-retained-byte-for-byte-for-right-gun-positioning-and-active-two-hand-left-support-while-only-the-free-left-wrist-postmultiplies-a-pi-rotation-about-the-live-h4ek-wrist-to-direct-child-thumb1-ray-so-the-palm-normal-reverses-and-the-stable-thumb-base-direction-remains-outward'
            head_tracking = $true
            six_dof = $true
            headset_owned_pitch = $true
            headset_owned_yaw = $true
            controller_aim = $true
            haptics = $true
            head_relative_locomotion = $true
            # Halo 4's CUI arrives inside the captured scene target, so it is
            # visible without any HUD redirect. User-confirmed 2026-08-08.
            hud = 'native-inside-captured-scene-no-redirect'
            first_person_hands = $true
            arm_ik = $false
            floating_hands = $true
            weapon_follows_hand = $true
            controller_facing_orientation = $true
            orientation_source =
                'live-current-eye-storm-wrist-relation-no-fixed-blender-seed'
            left_presentation_trim =
                'shared-mirrored-gun-angle-trim-applied-once'
            free_left_palm =
                'pi-about-live-storm-wrist-to-direct-child-b_l_thumb1-axis'
            two_hand_left_pose =
                'c36-byte-identical-when-exact-prepared-aim-used-two-hand-solve'
            failure_policy =
                'pre-claim-stock-post-claim-frame-drop-core-remains-armed'
            vrik_failure_policy =
                'base-rigid-invalid-input-leaves-that-palette-stock-while-optional-free-palm-invalid-input-keeps-the-c36-left-target-and-continues-right-hand-held-model-and-camera-core'
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
        reach_projectile_alignment_enabled = $true
        reach_projectile_alignment_scope =
            'exact-local-reach-vehicle-central-line'
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
        development_baseline = 'f4c641f7b1b707991f2bda71ba485090a16f1e9a'
        files = [ordered]@{
            'halo3xr.dll' = [ordered]@{
                bytes = $dll.Length
                sha256 = $dllHash
            }
            'halo3xr_launcher.exe' = [ordered]@{
                bytes = $launcher.Length
                sha256 = $launcherHash
            }
        }
        note = 'C-H4-37 is an unaccepted headset candidate built on the partially successful C-H4-36. In Steam/SteamVR OpenXR on an Oculus headset at 120 Hz, the user explicitly judged C-H4-36 left-hand orientation perfect for the two-hand support grip but rejected the free left hand as upside down. C-H4-37 preserves that exact prepared-frame support pose, the C-H4-36 right hand/gun, every target translation, record routing, rigid held carry, no-IK policy and camera process. Only while the published right aim did not use the two-hand solve, it turns the left palm by 180 degrees around the live H4EK b_l_hand-to-direct-child-b_l_thumb1 ray; that reverses the authored palm-plane normal while preserving the stable thumb-base direction. Invalid optional thumb input retains C-H4-36 left orientation and does not disturb right/gun or camera ownership. Worker telemetry separately reports committed free-palm, exact-support and fallback modes. C-H4-1 remains the accepted rollback pointer until explicit headset acceptance.'
    }

    $manifestPath = Join-Path $packageDir 'CANDIDATE-MANIFEST.json'
    $json = $manifest | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText(
        $manifestPath,
        $json + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))

    Write-Host "Created untested candidate: $packageDir"
    Write-Host "Source:   $commit"
    Write-Host "DLL:      $dllHash"
    Write-Host "Launcher: $launcherHash"

    & powershell -NoProfile -ExecutionPolicy Bypass -File `
        (Join-Path $repoRoot 'tools\install-candidate.ps1') `
        -CandidateDir $packageDir
    if ($LASTEXITCODE -ne 0) {
        throw 'Candidate was packaged but automatic installation failed.'
    }
}
finally {
    Pop-Location
}
