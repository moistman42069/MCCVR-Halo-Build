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

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

# The mod folder is only valid where a real MCC install sits beside it. For the
# Store edition the install root is the package's Content folder.
function Test-InstallRoot([string]$Root) {
    foreach ($exe in @($steamExeName, $storeExeName)) {
        $path = Join-Path $Root (Join-Path 'MCC\Binaries\Win64' $exe)
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return $true
        }
    }
    return $false
}

function Get-EditionLabel([string]$Root) {
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
if ([int]$manifest.schema_version -ne 8 -or
        [string]$manifest.status -cne 'UNTESTED_LOCAL_CANDIDATE' -or
        $manifest.accepted -ne $false -or
        [string]$manifest.base_release -cne 'MCC_VR_ALPHA_0.3.3' -or
        [string]$manifest.development_baseline -cne
            'f4c641f7b1b707991f2bda71ba485090a16f1e9a' -or
        [string]$manifest.package_id -cne $packageId -or
        [string]$manifest.source_commit -notmatch '^[0-9a-f]{40}$' -or
        [string]$manifest.source_commit -cne $head -or
        -not $packageId.StartsWith(
            $head.Substring(0, 7) + '-',
            [StringComparison]::Ordinal) -or
        @($manifest.titles).Count -ne 4 -or
        [string]$manifest.titles[0] -cne 'Halo 3' -or
        [string]$manifest.titles[1] -cne 'Halo 3: ODST' -or
        [string]$manifest.titles[2] -cne 'Halo: Reach' -or
        [string]$manifest.titles[3] -cne 'Halo 4' -or
        $manifest.embedded_build_identity.source_commit -cne
            $manifest.source_commit -or
        $manifest.embedded_build_identity.odst -ne $true -or
        $manifest.embedded_build_identity.reach -ne $true -or
        $manifest.embedded_build_identity.reach_render -ne $true -or
        $manifest.embedded_build_identity.halo4 -ne $true -or
        [string]$manifest.accepted_halo4_identity.candidate -cne 'C-H4-1' -or
        [string]$manifest.accepted_halo4_identity.source_commit -cne
            '954359b7f786b78c76824b662ead3c1fc8cd7917' -or
        [string]$manifest.halo4_candidate.id -cne 'C-H4-7' -or
        [string]$manifest.halo4_candidate.status -cne
            'OFFLINE_PASS_HEADSET_PENDING' -or
        [string]$manifest.halo4_candidate.behavior -cne
            'stock-projection-exact-serial-stereo-geometry' -or
        $manifest.halo4_candidate.head_tracking -ne $false -or
        $manifest.halo4_candidate.six_dof -ne $false -or
        $manifest.halo4_candidate.hud -ne $false -or
        [string]$manifest.halo4_candidate.failure_policy -cne
            'pre-claim-stock-post-claim-frame-drop-core-remains-armed' -or
        $manifest.deployment_policy.automatic_after_package -ne $true -or
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
        [string]$manifest.reach_projectile_alignment_scope -cne
            'exact-local-reach-vehicle-central-line' -or
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

$candidateDll = Join-Path $candidatePath 'halo3xr.dll'
$candidateLauncher = Join-Path $candidatePath 'halo3xr_launcher.exe'
$dllHash = Assert-FileIdentity `
    $candidateDll $manifest.files.'halo3xr.dll' 'Candidate DLL'
$launcherHash = Assert-FileIdentity `
    $candidateLauncher $manifest.files.'halo3xr_launcher.exe' 'Candidate launcher'

$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -in @(
        'MCC-Win64-Shipping', 'MCCWinStore-Win64-Shipping', 'halo3xr_launcher')
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

    $installedDll = Join-Path $gamePath 'halo3xr.dll'
    $installedLauncher = Join-Path $gamePath 'halo3xr_launcher.exe'
    $configPath = Join-Path $gamePath 'halomccvr.cfg'
    $logPath = Join-Path $gamePath 'halo3xr.log'
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
        Copy-Item -LiteralPath $candidateDll -Destination $installedDll -Force
        Copy-Item -LiteralPath $candidateLauncher -Destination $installedLauncher -Force
        $newDllHash = Get-Sha256 $installedDll
        $newLauncherHash = Get-Sha256 $installedLauncher
        if ($newDllHash -cne $dllHash -or $newLauncherHash -cne $launcherHash) {
            throw ("First-install hash mismatch at {0}: DLL={1} launcher={2}" -f `
                $gamePath, $newDllHash, $newLauncherHash)
        }
        Write-Host "  installed DLL:      $newDllHash"
        Write-Host "  installed launcher: $newLauncherHash"
        $results += [pscustomobject]@{
            GameDir = $gamePath; Edition = $target.Edition
            Action = 'first-install'; Backup = $null
        }
        continue
    }

    $priorDllHash = Get-Sha256 $installedDll
    $priorLauncherHash = Get-Sha256 $installedLauncher
    if ($priorDllHash -ceq $dllHash -and
            $priorLauncherHash -ceq $launcherHash) {
        Write-Host ("Already installed for the {0} edition: {1}" -f $target.Edition, $gamePath)
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
    Copy-Item -LiteralPath $installedDll -Destination `
        (Join-Path $backupDir 'halo3xr.dll')
    Copy-Item -LiteralPath $installedLauncher -Destination `
        (Join-Path $backupDir 'halo3xr_launcher.exe')
    if (Test-Path -LiteralPath $configPath -PathType Leaf) {
        $configHashBefore = Get-Sha256 $configPath
        Copy-Item -LiteralPath $configPath -Destination `
            (Join-Path $backupDir 'halomccvr.cfg')
    }
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        Copy-Item -LiteralPath $logPath -Destination `
            (Join-Path $backupDir 'halo3xr.log')
    }

    if ((Get-Sha256 (Join-Path $backupDir 'halo3xr.dll')) -cne $priorDllHash -or
            (Get-Sha256 (Join-Path $backupDir 'halo3xr_launcher.exe')) -cne
                $priorLauncherHash) {
        throw 'Deployment backup verification failed; automatic install made no changes.'
    }

    $stagedDll = Join-Path $gamePath ("halo3xr.dll.$packageId.pending")
    $stagedLauncher = Join-Path $gamePath `
        ("halo3xr_launcher.exe.$packageId.pending")
    if ((Test-Path -LiteralPath $stagedDll) -or
            (Test-Path -LiteralPath $stagedLauncher)) {
        throw 'A candidate staging file already exists; refusing to overwrite it.'
    }

    try {
        Copy-Item -LiteralPath $candidateDll -Destination $stagedDll
        Copy-Item -LiteralPath $candidateLauncher -Destination $stagedLauncher
        if ((Get-Sha256 $stagedDll) -cne $dllHash -or
                (Get-Sha256 $stagedLauncher) -cne $launcherHash) {
            throw 'Staged candidate hash verification failed; installed files remain unchanged.'
        }

        Copy-Item -LiteralPath $stagedDll -Destination $installedDll -Force
        Copy-Item -LiteralPath $stagedLauncher -Destination $installedLauncher -Force
    }
    finally {
        if (Test-Path -LiteralPath $stagedDll -PathType Leaf) {
            Remove-Item -LiteralPath $stagedDll -Force
        }
        if (Test-Path -LiteralPath $stagedLauncher -PathType Leaf) {
            Remove-Item -LiteralPath $stagedLauncher -Force
        }
    }

    $installedDllHash = Get-Sha256 $installedDll
    $installedLauncherHash = Get-Sha256 $installedLauncher
    if ($installedDllHash -cne $dllHash -or
            $installedLauncherHash -cne $launcherHash) {
        throw ("Post-install hash mismatch at {0}: DLL={1} launcher={2}" -f `
            $gamePath, $installedDllHash, $installedLauncherHash)
    }
    if ($null -ne $configHashBefore -and
            (Get-Sha256 $configPath) -cne $configHashBefore) {
        throw 'Shared configuration changed during deployment.'
    }

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
        }
        installed = [ordered]@{
            halo3xr_dll_sha256 = $installedDllHash
            halo3xr_launcher_sha256 = $installedLauncherHash
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
foreach ($result in $results) {
    Write-Host ("  [{0,-6}] {1,-22} {2}" -f $result.Edition, $result.Action, $result.GameDir)
}
Write-Host 'MCC was not launched and no existing halomccvr.cfg was changed.'
