[CmdletBinding()]
param(
    [string]$OutputPath,
    [ValidateRange(8, 1024)]
    [int]$SizeMiB = 16,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ScratchRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $RootDir 'build\test-disks'))
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ScratchRoot 'ahci-scratch.img'
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$scratchPrefix = $ScratchRoot.TrimEnd('\', '/') +
    [System.IO.Path]::DirectorySeparatorChar
if (-not $OutputPath.StartsWith(
        $scratchPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Scratch images created by this script must stay below $ScratchRoot"
}
if ([System.IO.Path]::GetExtension($OutputPath) -notin @('.img', '.raw')) {
    throw 'Scratch image must use the .img or .raw extension.'
}
if ((Test-Path -LiteralPath $OutputPath) -and -not $Force) {
    throw "Scratch image already exists; use -Force to replace this disposable file: $OutputPath"
}

[System.IO.Directory]::CreateDirectory(
    [System.IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
$temporary = "$OutputPath.$([Guid]::NewGuid().ToString('N')).tmp"
$stream = $null
try {
    $length = [int64]$SizeMiB * 1024 * 1024
    $sectorCount = [uint64]($length / 512)
    $header = [byte[]]::new(512)
    $magic = [System.Text.Encoding]::ASCII.GetBytes(
        'KUROGANE_AHCI_SCRATCH_V1')
    [System.Array]::Copy($magic, 0, $header, 0, $magic.Length)
    [System.Array]::Copy([BitConverter]::GetBytes([uint32]1), 0, $header, 32, 4)
    [System.Array]::Copy([BitConverter]::GetBytes([uint32]64), 0, $header, 36, 4)
    [System.Array]::Copy([BitConverter]::GetBytes($sectorCount), 0, $header, 40, 8)
    [System.Array]::Copy([BitConverter]::GetBytes([uint64]8), 0, $header, 48, 8)
    [System.Array]::Copy([BitConverter]::GetBytes([uint32]8), 0, $header, 56, 4)
    [System.Array]::Copy(
        [BitConverter]::GetBytes([uint32]0x4B535431), 0, $header, 60, 4)

    $stream = [System.IO.File]::Open(
        $temporary,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None)
    $stream.SetLength($length)
    $stream.Position = 0
    $stream.Write($header, 0, $header.Length)
    $stream.Flush($true)
    $stream.Dispose()
    $stream = $null

    if (Test-Path -LiteralPath $OutputPath) {
        [System.IO.File]::Delete($OutputPath)
    }
    [System.IO.File]::Move($temporary, $OutputPath)
}
finally {
    if ($null -ne $stream) {
        $stream.Dispose()
    }
    if (Test-Path -LiteralPath $temporary) {
        [System.IO.File]::Delete($temporary)
    }
}

$hash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash
Write-Host "Created tagged AHCI scratch image: $OutputPath"
Write-Host "  Size: $SizeMiB MiB ($sectorCount sectors)"
Write-Host '  Writable test range: LBA 8-15'
Write-Host "  SHA-256: $($hash.ToLowerInvariant())"
