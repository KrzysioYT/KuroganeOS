[CmdletBinding()]
param(
    [string]$ImagePath,
    [switch]$Writable,
    [string]$ScratchDiskPath,
    [switch]$Headless,
    [switch]$ShellTest,
    [switch]$SafeMode,
    [switch]$DesktopMode,
    [ValidateRange(1, 120)]
    [int]$TimeoutSeconds = 45,
    [ValidateRange(128, 4096)]
    [int]$MemoryMiB = 1024,
    [ValidateRange(1024, 65535)]
    [int]$MonitorPort = 45454,
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$LogName = 'qemu-img'
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
    TimeoutSeconds = $TimeoutSeconds
    MemoryMiB = $MemoryMiB
    MonitorPort = $MonitorPort
    LogName = $LogName
}
if ($Writable) { $runnerParameters.WritableDiskImage = $true }
if (-not [string]::IsNullOrWhiteSpace($ScratchDiskPath)) {
    $runnerParameters.WritableScratchDiskPath = $ScratchDiskPath
}
if ($Headless) { $runnerParameters.Headless = $true }
if ($ShellTest) { $runnerParameters.ShellTest = $true }
if ($SafeMode) { $runnerParameters.SafeMode = $true }
if ($DesktopMode) { $runnerParameters.DesktopMode = $true }

# Visible manual runs are interactive sessions. Bounded headless/ShellTest runs
# are owned by the caller and terminate after their verification condition.
if (-not $Headless -and -not $ShellTest) {
    $runnerParameters.KeepRunning = $true
    $runnerParameters.Display = $true
}

& $Runner @runnerParameters
