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
$repaired = @()

# Strict UTF-8 (throws on invalid bytes) is how we DETECT the encoding defect;
# ISO-8859-1 is how we then decode without losing a byte.
$utf8Strict = New-Object Text.UTF8Encoding($false, $true)

Push-Location $KitRoot
try {
    foreach ($asset in $assets) {
        $id = [string]$asset.id
        $tag = [string]$asset.tag
        $class = [string]$asset.class
        $proven = $asset.proven -eq $true
        $tagPath = Join-Path $tagsRoot ($tag + '.' + $class)
        $outPath = Join-Path $resolvedOutput ($id + '.' + $class + '.xml')
        $rawPath = $outPath + '.raw'   # quarantined: still malformed after repair
        $origPath = $outPath + '.orig' # pristine tool.exe bytes when repaired

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
        foreach ($stale in @($outPath, $rawPath, $origPath)) {
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

        # H4EK tool.exe emits XML with two distinct, separately-fixable
        # defects, both measured 2026-08-06 across an 18-tag run:
        #
        #  1. ENCODING. It writes raw tag bytes - notably the FF FF FF FF of a
        #     NONE tag reference - straight into attribute values, under an
        #     `<?xml version="1.0"?>` declaration that names no encoding and so
        #     defaults to UTF-8. Every one of the 11 affected exports is
        #     therefore invalid UTF-8, which is why XmlDocument.Load (which
        #     reads BYTES and honours the declared encoding) rejected them all,
        #     while parsing the same bytes as an already-decoded string
        #     accepted 8 of them. That difference is an encoding artefact, NOT
        #     malformed markup - do not conflate the two.
        #  2. UNESCAPED AMPERSANDS. Authored string content is written into
        #     attribute values without XML escaping, so a HaloScript-style
        #     expression appears literally as value="a&&b". That IS malformed
        #     markup and it is what genuinely broke 3 of the 18.
        #
        # Both are mechanically repairable, so quarantining on either would
        # throw away usable evidence. The pristine bytes are always preserved
        # next to any repaired file, and every repair is reported - nothing is
        # silently rewritten.
        $rawBytes = [IO.File]::ReadAllBytes($outPath)
        $latin1 = [Text.Encoding]::GetEncoding(28591) # ISO-8859-1, byte-preserving
        $text = $latin1.GetString($rawBytes)
        $repairs = @()

        $isValidUtf8 = $true
        try { [void]$utf8Strict.GetString($rawBytes) } catch { $isValidUtf8 = $false }
        if (-not $isValidUtf8) {
            # Declare the encoding we actually decoded with, so any downstream
            # byte-reading parser agrees with us.
            if ($text -match '^\s*<\?xml[^>]*\?>') {
                $text = [Text.RegularExpressions.Regex]::Replace(
                    $text, '^\s*<\?xml[^>]*\?>',
                    '<?xml version="1.0" encoding="iso-8859-1"?>', 1)
            }
            else {
                $text = '<?xml version="1.0" encoding="iso-8859-1"?>' + "`r`n" + $text
            }
            $repairs += 'declared iso-8859-1 (raw non-UTF-8 tag bytes present)'
        }

        # Escape only ampersands that do not already begin a valid entity.
        $ampPattern = '&(?!(?:amp|lt|gt|quot|apos);|#[0-9]+;|#x[0-9A-Fa-f]+;)'
        $bareAmps = [Text.RegularExpressions.Regex]::Matches($text, $ampPattern).Count
        if ($bareAmps -gt 0) {
            $text = [Text.RegularExpressions.Regex]::Replace($text, $ampPattern, '&amp;')
            $repairs += "escaped $bareAmps bare ampersand(s)"
        }

        $wellFormed = $true
        $parseError = ''
        try {
            $xmlDoc = New-Object System.Xml.XmlDocument
            $xmlDoc.LoadXml($text)
        }
        catch {
            $wellFormed = $false
            $parseError = $_.Exception.Message
        }

        if (-not $wellFormed) {
            Move-Item -LiteralPath $outPath -Destination $rawPath -Force
            Write-Warning ("Still malformed after repair, quarantined: $rawPath" +
                " -- $parseError")
            $quarantined += $id
            continue
        }

        if ($repairs.Count -gt 0) {
            # Preserve the untouched tool.exe bytes beside the repaired file.
            [IO.File]::WriteAllBytes($origPath, $rawBytes)
            $xmlDoc.Save($outPath)
            Write-Host ("  repaired: " + ($repairs -join '; ') +
                " (pristine bytes kept at $([IO.Path]::GetFileName($origPath)))")
            $repaired += $id
        }
        $exported += $id
    }
}
finally {
    Pop-Location
}

Write-Host ("Halo 4 kit source ready: {0} valid XML export(s)" -f $exported.Count)
if ($repaired.Count -gt 0) {
    Write-Host ("Repaired (pristine .orig kept): " + ($repaired -join ', '))
}
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
