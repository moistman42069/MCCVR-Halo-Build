[CmdletBinding()]
param(
    [string]$ModulePath =
        'N:\SteamLibrary\steamapps\common\Halo The Master Chief Collection\halo4\halo4.dll'
)

$ErrorActionPreference = 'Stop'

# Pinned Halo 4 retail module identity (development baseline f4c641f).
# Either retail SHA-256 is admissible: the Steam and Microsoft Store editions
# differ only by their Authenticode signing, never by game code.
# No signature/RVA table exists yet for Halo 4; this preflight validates the
# module identity ONLY and must not invent further checks.
$expectedFileSize = 17829336
$expectedSha256Steam =
    '7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84'
$expectedSha256Store =
    '5767CD564C1E8E8D012D002A8DE8E92960A3DE46442399ED054E3C4EF44AA496'
$expectedTimestamp = 0x68A0E7BF
$expectedImageSize = 0x04A3F000
$expectedMachine = 0x8664

function Get-PeIdentity {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Label
    )

    $stream = [IO.File]::OpenRead($Path)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "$Label is not an MZ executable."
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0x40 -or $peOffset -gt $stream.Length - 256) {
            throw "$Label has an invalid PE header offset."
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "$Label has no PE signature."
        }
        $machine = $reader.ReadUInt16()
        $stream.Position = $peOffset + 8
        $timestamp = $reader.ReadUInt32()
        $optionalHeaderOffset = $peOffset + 24
        $stream.Position = $optionalHeaderOffset
        if ($reader.ReadUInt16() -ne 0x020B) {
            throw "$Label is not PE32+ (x64)."
        }
        $stream.Position = $optionalHeaderOffset + 56
        $sizeOfImage = $reader.ReadUInt32()
        return [pscustomobject]@{
            Machine = $machine
            Timestamp = $timestamp
            SizeOfImage = $sizeOfImage
        }
    }
    finally {
        $stream.Dispose()
    }
}

if (-not (Test-Path -LiteralPath $ModulePath -PathType Leaf)) {
    throw "Required Halo 4 module is missing: $ModulePath"
}
if ([IO.Path]::GetFileName($ModulePath) -ine 'halo4.dll') {
    throw "Halo 4 module filename must be halo4.dll: $ModulePath"
}

$actualSize = (Get-Item -LiteralPath $ModulePath).Length
if ($actualSize -ne $expectedFileSize) {
    throw ("Halo 4 module size mismatch: {0} bytes (expected {1})." -f
        $actualSize, $expectedFileSize)
}

$actualHash = (Get-FileHash -LiteralPath $ModulePath -Algorithm SHA256).Hash
$edition = $null
if ($actualHash -ieq $expectedSha256Steam) {
    $edition = 'Steam'
}
elseif ($actualHash -ieq $expectedSha256Store) {
    $edition = 'Microsoft Store'
}
if ($null -eq $edition) {
    throw "Halo 4 module SHA-256 matches neither pinned retail signing: $actualHash"
}

$identity = Get-PeIdentity -Path $ModulePath -Label 'Halo 4 module'
if ($identity.Machine -ne $expectedMachine) {
    throw ('Halo 4 module machine mismatch: 0x{0:X4}' -f $identity.Machine)
}
if ($identity.Timestamp -ne $expectedTimestamp) {
    throw ('Halo 4 PE timestamp mismatch: 0x{0:X8}' -f $identity.Timestamp)
}
if ($identity.SizeOfImage -ne $expectedImageSize) {
    throw ('Halo 4 SizeOfImage mismatch: 0x{0:X8}' -f $identity.SizeOfImage)
}

Write-Host 'Halo 4 evidence preflight passed.'
Write-Host "Module:           $ModulePath"
Write-Host ('Module size:      {0:N0} bytes' -f $actualSize)
Write-Host "Module SHA-256:   $actualHash ($edition signing; either edition is admissible)"
Write-Host ('PE identity:      machine 0x{0:X4}, timestamp 0x{1:X8}, SizeOfImage 0x{2:X8}, PE32+' -f
    $identity.Machine, $identity.Timestamp, $identity.SizeOfImage)
Write-Host 'Signature tables: no signature/RVA table pinned yet for Halo 4; module identity checks only'
Write-Host 'Hook eligibility: none (identity-only preflight; hooks require a pinned signature table)'
