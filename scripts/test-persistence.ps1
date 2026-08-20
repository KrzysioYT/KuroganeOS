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
$runId = [Guid]::NewGuid().ToString('N')
$testImage = Join-Path $testDirectory "fat32-persistence-$runId.img"
$prepareLogName = "qemu-storage-prepare-$runId"
$verifyLogName = "qemu-storage-verify-$runId"
$prepareLog = Join-Path $root "build\logs\$prepareLogName-serial.log"
$verifyLog = Join-Path $root "build\logs\$verifyLogName-serial.log"

if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Foundation image does not exist: $source"
}
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $testImage

Write-Host "[persistence] run id: $runId"
Write-Host "[persistence] source: $source"
Write-Host "[persistence] disposable image: $testImage"

& (Join-Path $PSScriptRoot 'run-qemu.ps1') `
    -UseDiskImage `
    -DiskImagePath $testImage `
    -WritableDiskImage `
    -Headless `
    -TimeoutSeconds $TimeoutSeconds `
    -LogName $prepareLogName

if (-not (Test-Path -LiteralPath $prepareLog -PathType Leaf)) {
    throw "First-boot serial log was not created: $prepareLog"
}
if ((Get-Content -LiteralPath $prepareLog -Raw) -notmatch
    '\[TEST\] fat32_persistence_prepare: PASS') {
    throw 'First boot did not create and flush the persistence file.'
}
Write-Host '[persistence] first boot create/write/flush: PASS'

& (Join-Path $PSScriptRoot 'run-qemu.ps1') `
    -UseDiskImage `
    -DiskImagePath $testImage `
    -WritableDiskImage `
    -Headless `
    -TimeoutSeconds $TimeoutSeconds `
    -LogName $verifyLogName

if (-not (Test-Path -LiteralPath $verifyLog -PathType Leaf)) {
    throw "Second-boot serial log was not created: $verifyLog"
}
if ((Get-Content -LiteralPath $verifyLog -Raw) -notmatch
    '\[TEST\] fat32_persistence_verify: PASS') {
    throw 'Second boot did not read back the flushed persistence file.'
}
Write-Host '[persistence] second boot readback: PASS'

Write-Host '[pass] FAT32 create/write/flush survived a separate QEMU reboot'
Write-Host "[image] $testImage"
Write-Host "[prepare-log] $prepareLog"
Write-Host "[verify-log] $verifyLog"
