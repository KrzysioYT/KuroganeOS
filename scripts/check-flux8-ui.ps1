[CmdletBinding()]
param(
    [string]$Name = 'KuroganeOS-Flux8-Modern',
    [int]$Tail = 500
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$log = Join-Path $HOME ("VirtualBox VMs\" + $Name + "\kurogane-serial.log")
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
    throw "Serial log missing: $log. Boot this VM and enable COM1 file logging."
}
$lines = @(Get-Content -LiteralPath $log -Tail $Tail -ErrorAction Stop)
Write-Host "[Flux 8] VM: $Name"
Write-Host "[Flux 8] Log: $log"
Write-Host "[Flux 8] Note: interact with dock and one app tile before interpreting absent markers."

$steps = [ordered]@{
    'Kernel Flux session'           = '[TEST] desktop_session: PASS'
    'Ring-3 launcher created'       = '[TEST] desktop_launcher_ring3: PASS'
    'Home scene presented'          = '[TEST] red_flux_home_surface: PASS'
    'Physical mouse left-click'     = '[INPUT] flux_primary_button_delivered: PASS'
    'Dock shortcut to launcher'     = '[INPUT] flux_dock_shortcut_received: PASS'
    'Home tile clicked'             = '[INPUT] flux_home_tile_hit: PASS'
    'Launcher accepted action'      = '[INPUT] flux_launcher_action_received: PASS'
    'Application process spawned'   = '[INPUT] flux_app_spawn: PASS'
}
foreach ($step in $steps.GetEnumerator()) {
    $found = @($lines | Where-Object { $_.Contains([string]$step.Value) }).Count -gt 0
    $state = if ($found) { 'SEEN' } else { 'NOT SEEN' }
    Write-Host ("{0,-34} {1}" -f $step.Key, $state)
}
$failed = @($lines | Where-Object {
    $_ -match 'PANIC_BEGIN|FATAL ERROR|flux_app_spawn: FAIL|red_flux_login_to_desktop: FAIL'
})
if ($failed.Count -gt 0) {
    Write-Warning "Potential runtime failure markers:"
    $failed | Select-Object -Last 12 | ForEach-Object { Write-Host $_ }
}
Write-Host '--- Recent relevant serial lines ---'
$lines | Where-Object {
    $_ -match '^\[INPUT\]|\[TEST\] desktop_launcher|\[TEST\] red_flux_home_surface|PANIC_BEGIN|flux.*FAIL'
} | Select-Object -Last 60 | ForEach-Object { Write-Host $_ }
