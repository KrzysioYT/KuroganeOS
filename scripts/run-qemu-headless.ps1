[CmdletBinding()]
param(
    [string]$ImagePath,
    [switch]$Writable,
    [string]$ScratchDiskPath,
    [switch]$ShellTest,
    [switch]$SafeMode,
    [switch]$DesktopMode,
    [ValidateRange(1, 180)]
    [int]$TimeoutSeconds = 60,
    [ValidateRange(128, 4096)]
    [int]$MemoryMiB = 1024,
    [ValidateRange(1024, 65535)]
    [int]$MonitorPort = 45454,
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$LogName = 'qemu-headless'
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
if ($SafeMode -and $DesktopMode) {
    throw '-SafeMode and -DesktopMode are mutually exclusive.'
}

$runnerParameters = @{
    UseDiskImage = $true
    DiskImagePath = $ImagePath
    Headless = $true
    TimeoutSeconds = $TimeoutSeconds
    MemoryMiB = $MemoryMiB
    MonitorPort = $MonitorPort
    LogName = $LogName
}
if ($Writable) { $runnerParameters.WritableDiskImage = $true }
if (-not [string]::IsNullOrWhiteSpace($ScratchDiskPath)) {
    $runnerParameters.WritableScratchDiskPath = $ScratchDiskPath
}
if ($ShellTest) { $runnerParameters.ShellTest = $true }
if ($SafeMode) { $runnerParameters.SafeMode = $true }
if ($DesktopMode) { $runnerParameters.DesktopMode = $true }

& $Runner @runnerParameters
