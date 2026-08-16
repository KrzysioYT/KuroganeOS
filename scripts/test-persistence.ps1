[CmdletBinding()]
param(
    [string]$SourceImage = "build\images\KuroganeOS-base.img",
    [ValidateRange(20, 120)]
    [int]$TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCandidate = if ([System.IO.Path]::IsPathRooted($SourceImage)) {
    $SourceImage
} else {
    Join-Path $root $SourceImage
}
$source = [System.IO.Path]::GetFullPath($sourceCandidate)
$testDirectory = Join-Path $root 'build\test-images'
$testImage = Join-Path $testDirectory 'fat32-persistence.img'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Foundation image does not exist: $source"
}
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $testImage -Force

& (Join-Path $PSScriptRoot 'run-qemu.ps1') `
    -UseDiskImage `
    -DiskImagePath $testImage `
    -WritableDiskImage `
    -Headless `
    -TimeoutSeconds $TimeoutSeconds `
    -LogName 'qemu-storage-prepare'

$prepareLog = Join-Path $root 'build\logs\qemu-storage-prepare-serial.log'
if ((Get-Content -LiteralPath $prepareLog -Raw) -notmatch
    '\[TEST\] fat32_persistence_prepare: PASS') {
    throw 'First boot did not create and flush the persistence file.'
}

& (Join-Path $PSScriptRoot 'run-qemu.ps1') `
    -UseDiskImage `
    -DiskImagePath $testImage `
    -WritableDiskImage `
    -Headless `
    -TimeoutSeconds $TimeoutSeconds `
    -LogName 'qemu-storage-verify'

$verifyLog = Join-Path $root 'build\logs\qemu-storage-verify-serial.log'
if ((Get-Content -LiteralPath $verifyLog -Raw) -notmatch
    '\[TEST\] fat32_persistence_verify: PASS') {
    throw 'Second boot did not read back the flushed persistence file.'
}

Write-Host '[pass] FAT32 create/write/flush survived a separate QEMU reboot'
Write-Host "[image] $testImage"
