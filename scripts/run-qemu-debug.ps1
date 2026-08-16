[CmdletBinding()]
param(
    [string]$ImagePath,
    [switch]$Writable,
    [string]$ScratchDiskPath,
    [switch]$Headless,
    [ValidateRange(1024, 65535)]
    [int]$GdbPort = 1234,
    [ValidateRange(64, 4096)]
    [int]$MemoryMiB = 256,
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$LogName = 'qemu-debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Runner = Join-Path $PSScriptRoot 'run-qemu.ps1'
$imageWasExplicit = $PSBoundParameters.ContainsKey('ImagePath')
if ([string]::IsNullOrWhiteSpace($ImagePath)) {
    $workingImage = Join-Path $RootDir 'state\KuroganeOS.img'
    $baseImage = Join-Path $RootDir 'build\images\KuroganeOS-base.img'
    $ImagePath = if (Test-Path -LiteralPath $workingImage -PathType Leaf) {
        $workingImage
    } else {
        $baseImage
    }
}
if ($Writable -and -not $imageWasExplicit) {
    throw '-Writable requires an explicit -ImagePath. This prevents an accidental write to the repository default image.'
}

$runnerParameters = @{
    UseDiskImage = $true
    DiskImagePath = $ImagePath
    DebugWait = $true
    KeepRunning = $true
    GdbPort = $GdbPort
    MemoryMiB = $MemoryMiB
    LogName = $LogName
}
if ($Writable) {
    $runnerParameters.WritableDiskImage = $true
}
if (-not [string]::IsNullOrWhiteSpace($ScratchDiskPath)) {
    $runnerParameters.WritableScratchDiskPath = $ScratchDiskPath
}
if ($Headless) {
    $runnerParameters.Headless = $true
}

& $Runner @runnerParameters
