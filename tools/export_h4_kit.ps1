param(
    [string]$KitRoot = 'N:\SteamLibrary\steamapps\common\H4EK',
    [string]$OutputRoot = '',
    [switch]$Refresh
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot 'out\h4-kit-source\canonical'
}
$manifestPath = Join-Path $PSScriptRoot 'h4_kit_manifest.json'

if (-not (Test-Path -LiteralPath $KitRoot -PathType Container)) {
    throw "H4EK kit root not found: $KitRoot"
}
# The working tool.exe convention (proven): cwd at the kit root, input tag
# path ABSOLUTE under the kit's tags root. A relative input path fails with
# "Input tag path is not located in the tags directory structure".
$KitRoot = (Resolve-Path -LiteralPath $KitRoot).Path
$tool = Join-Path $KitRoot 'tool.exe'
$tagsRoot = Join-Path $KitRoot 'tags'

if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
    throw "H4EK tool not found: $tool"
}
if (-not (Test-Path -LiteralPath $tagsRoot -PathType Container)) {
    throw "H4EK tags root not found: $tagsRoot"
}
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Kit manifest not found: $manifestPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$assets = @($manifest.exports)
if ($assets.Count -eq 0) {
    throw 'Kit manifest lists no exports.'
}
$seen = @{}
foreach ($asset in $assets) {
    $id = [string]$asset.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        throw 'Kit manifest entry is missing an id.'
    }
    if ($seen.ContainsKey($id)) {
        throw "Duplicate kit manifest id: $id"
    }
    $seen[$id] = $true
    if ([string]::IsNullOrWhiteSpace([string]$asset.tag) -or
        [string]::IsNullOrWhiteSpace([string]$asset.class)) {
        throw "Kit manifest entry $id is missing tag or class."
    }
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$resolvedOutput = (Resolve-Path -LiteralPath $OutputRoot).Path
$repoOut = Join-Path ((Resolve-Path -LiteralPath $repoRoot).Path) 'out'
$outPrefix = $repoOut.TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar
if ($resolvedOutput -ne $repoOut -and
    -not $resolvedOutput.StartsWith(
        $outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output must stay under this repository's out\ directory: $resolvedOutput"
}

function Invoke-H4Tool {
    param([string[]]$Arguments)
    & $tool @Arguments | Out-Host
    return $LASTEXITCODE
}

$exported = @()
$skippedMissing = @()
$failedExport = @()
$quarantined = @()

Push-Location $KitRoot
try {
    foreach ($asset in $assets) {
        $id = [string]$asset.id
        $tag = [string]$asset.tag
        $class = [string]$asset.class
        $proven = $asset.proven -eq $true
        $tagPath = Join-Path $tagsRoot ($tag + '.' + $class)
        $outPath = Join-Path $resolvedOutput ($id + '.' + $class + '.xml')
        $rawPath = $outPath + '.raw'

        if (-not $Refresh) {
            if ((Test-Path -LiteralPath $outPath) -and
                (Get-Item -LiteralPath $outPath).Length -gt 0) {
                $exported += $id
                continue
            }
            if (Test-Path -LiteralPath $rawPath) {
                Write-Warning ("Previously quarantined export still present " +
                    "(re-run with -Refresh to retry): $rawPath")
                $quarantined += $id
                continue
            }
        }

        if (-not (Test-Path -LiteralPath $tagPath -PathType Leaf)) {
            if ($proven) {
                throw "Missing pinned H4EK tag: $tagPath"
            }
            Write-Warning "Missing H4EK target tag (skipped): $tagPath"
            $skippedMissing += $id
            continue
        }

        Write-Host "Exporting Halo 4 authoring source: $id ($class)"
        foreach ($stale in @($outPath, $rawPath)) {
            if (Test-Path -LiteralPath $stale) {
                Remove-Item -LiteralPath $stale -Force
            }
        }
        $exit = Invoke-H4Tool @('export-tag-to-xml', $tagPath, $outPath)
        $produced = (Test-Path -LiteralPath $outPath) -and
            (Get-Item -LiteralPath $outPath).Length -gt 0
        if ($exit -ne 0 -or -not $produced) {
            if ($proven) {
                throw "H4EK tool failed ($exit) or produced no XML: $tagPath"
            }
            Write-Warning "H4EK tool failed ($exit) for target tag (skipped): $tagPath"
            if (Test-Path -LiteralPath $outPath) {
                Remove-Item -LiteralPath $outPath -Force
            }
            $failedExport += $id
            continue
        }

        # tool.exe can emit malformed XML. No scrubber precedent exists in the
        # Reach tooling (export_reach_vehicle_kit.ps1 only checks non-empty),
        # so validate well-formedness and quarantine bad output as .raw
        # instead of failing the whole run.
        $wellFormed = $true
        try {
            $xmlDoc = New-Object System.Xml.XmlDocument
            $xmlDoc.Load($outPath)
        }
        catch {
            $wellFormed = $false
        }
        if (-not $wellFormed) {
            Move-Item -LiteralPath $outPath -Destination $rawPath -Force
            Write-Warning "Malformed XML quarantined: $rawPath"
            $quarantined += $id
            continue
        }
        $exported += $id
    }
}
finally {
    Pop-Location
}

Write-Host ("Halo 4 kit source ready: {0} valid XML export(s)" -f $exported.Count)
if ($skippedMissing.Count -gt 0) {
    Write-Host ("Missing target tags skipped: " + ($skippedMissing -join ', '))
}
if ($failedExport.Count -gt 0) {
    Write-Host ("Failed target exports:       " + ($failedExport -join ', '))
}
if ($quarantined.Count -gt 0) {
    Write-Host ("Quarantined malformed XML:   " + ($quarantined -join ', '))
}
Write-Host $resolvedOutput
