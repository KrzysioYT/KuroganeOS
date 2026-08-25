[CmdletBinding()]
param(
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 45,
    [ValidateRange(0, 540)]
    [int]$MinimumRuntimeSeconds = 0,
    [switch]$KeepRunning,
    [switch]$Display,
    [switch]$Headless,
    [switch]$ShellTest,
    [switch]$UsbTest,
    [switch]$InstallerTest,
    [string]$InstallerDiskPath,
    [switch]$SafeMode,
    [switch]$DesktopMode,
    [switch]$UseDiskImage,
    [switch]$UseIso,
    [string]$DiskImagePath,
    [switch]$WritableDiskImage,
    [string]$WritableScratchDiskPath,
    [switch]$DebugWait,
    [ValidateRange(1024, 65535)]
    [int]$GdbPort = 1234,
    [ValidateRange(128, 4096)]
    [int]$MemoryMiB = 1024,
    [ValidateRange(1024, 65535)]
    [int]$MonitorPort = 45454,
    [ValidateSet('tcg', 'whpx')]
    [string]$Accelerator = 'tcg',
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$LogName = 'qemu'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Public compatibility entry point. The actual runner is kept separate so the
# graphical Foundation contract can evolve without duplicating the parameter
# surface consumed by verify.ps1 and the platform-specific wrappers.
$CoreRunner = Join-Path $PSScriptRoot 'run-qemu-next.ps1'
if (-not (Test-Path -LiteralPath $CoreRunner -PathType Leaf)) {
    throw "Missing canonical QEMU runner core: $CoreRunner"
}

& $CoreRunner @PSBoundParameters
