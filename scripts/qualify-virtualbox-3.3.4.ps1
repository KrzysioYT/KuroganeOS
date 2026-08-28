[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Iso,
    [ValidateRange(30, 600)]
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$trySmoke = Join-Path $PSScriptRoot 'smoke-virtualbox-try.ps1'
$installSmoke = Join-Path $PSScriptRoot 'smoke-virtualbox-iso.ps1'

if (-not (Test-Path -LiteralPath $trySmoke -PathType Leaf)) {
    throw "Missing 3.3.4 Try qualification script: $trySmoke"
}
if (-not (Test-Path -LiteralPath $installSmoke -PathType Leaf)) {
    throw "Missing VirtualBox install qualification script: $installSmoke"
}

Write-Host '=== KuroganeOS 3.3.4-dev Oracle VirtualBox qualification ==='
Write-Host 'Phase 1/2: ISO -> Try -> Login -> Red Flux Desktop'
& $trySmoke -Iso $Iso -TimeoutSeconds $TimeoutSeconds

Write-Host 'Phase 2/2: ISO -> Install -> SATA VDI -> detach ISO -> installed boot'
& $installSmoke -Iso $Iso -TimeoutSeconds $TimeoutSeconds

Write-Host '[virtualbox-3.3.4] Try/Login/Desktop: PASS'
Write-Host '[virtualbox-3.3.4] Install/SATA-VDI/reboot: PASS'
Write-Host '[virtualbox-3.3.4] REAL ORACLE VIRTUALBOX ACCEPTANCE: PASS'
