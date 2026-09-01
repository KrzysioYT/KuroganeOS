[CmdletBinding()]
param(
    [string]$Image,
    [ValidateRange(128, 4096)]
    [int]$MemoryMiB = 1024,
    [ValidateSet('auto', 'whpx', 'tcg')]
    [string]$Accelerator = 'auto',
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$LogName = 'desktop'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$FastRunner = Join-Path $PSScriptRoot 'run-qemu-fast.ps1'
if (-not (Test-Path -LiteralPath $FastRunner -PathType Leaf)) {
    throw "Missing accelerated QEMU runner: $FastRunner"
}

# Compatibility helper for older documentation/bookmarks. The canonical
# interactive implementation is run-qemu-fast.ps1 so desktop launches share
# WHPX fallback, protected-image semantics and the same log layout.
$params = @{
    MemoryMiB = $MemoryMiB
    Accelerator = $Accelerator
    LogName = $LogName
}
if (-not [string]::IsNullOrWhiteSpace($Image)) {
    $params.DiskImagePath = $Image
}

& $FastRunner @params
