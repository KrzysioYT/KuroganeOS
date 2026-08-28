[CmdletBinding()]
param(
    [ValidateSet('debug','release','test')]
    [string]$Configuration = 'release',
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$BuildScript = Join-Path $PSScriptRoot 'build.ps1'

$previousHost = $env:ANVIL_REPO_HOST
$previousBase = $env:ANVIL_REPO_BASE
try {
    $env:ANVIL_REPO_HOST = 'packages.kuroganeos.147-79-62-37.sslip.io'
    $env:ANVIL_REPO_BASE = '/KrzysioYT/KuroganeOS-Packages/main'

    Write-Host '[official] Anvil endpoint: https://packages.kuroganeos.147-79-62-37.sslip.io/index.kuro'
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$BuildScript,'-Configuration',$Configuration)
    if ($Rebuild) { $arguments += '-Rebuild' }
    & powershell.exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "Official build failed with exit code $LASTEXITCODE" }
} finally {
    $env:ANVIL_REPO_HOST = $previousHost
    $env:ANVIL_REPO_BASE = $previousBase
}

Write-Host "[official] KuroganeOS build complete: $RootDir"
