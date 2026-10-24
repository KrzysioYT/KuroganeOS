[CmdletBinding()]
param(
    [string]$Name = '',
    [int]$Tail = 5000
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$vmRoot = Join-Path $HOME 'VirtualBox VMs'
if ([string]::IsNullOrWhiteSpace($Name)) {
    if (-not (Test-Path -LiteralPath $vmRoot -PathType Container)) {
        throw "VirtualBox VM directory not found: $vmRoot"
    }
    $candidates = @(Get-ChildItem -LiteralPath $vmRoot -Directory |
        ForEach-Object {
            Join-Path $_.FullName 'kurogane-serial.log'
        } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        ForEach-Object { Get-Item -LiteralPath $_ } |
        Sort-Object LastWriteTime -Descending)
    if ($candidates.Count -eq 0) {
        throw "No KuroganeOS serial logs found under $vmRoot"
    }
    $log = $candidates[0].FullName
    $Name = $candidates[0].Directory.Name
} else {
    $log = Join-Path $vmRoot ($Name + '\kurogane-serial.log')
}
if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
    throw "Serial log missing: $log. Boot the chosen VM and enable COM1 file logging."
}
$lines = @(Get-Content -LiteralPath $log -Tail $Tail -ErrorAction Stop)
Write-Host "[Flux 8] VM: $Name"
Write-Host "[Flux 8] Log: $log"
Write-Host "[Flux 8] Note: interact with dock and one app tile before interpreting absent markers."

$steps = [ordered]@{
    'Kernel Flux session'           = '[TEST] desktop_session: PASS'
    'Login window presented'        = '[TEST] red_flux_login_surface: PASS'
    'Account lookup verified'       = '[SESSION] flux_account_lookup: PASS'
    'Session service connected'     = '[SESSION] flux_session_connect: PASS'
    'Session created'               = '[SESSION] flux_session_create: PASS'
    'Launcher child spawned'        = '[SESSION] flux_launcher_spawn: PASS'
    'Home window created'           = '[SESSION] flux_home_ui_create: PASS'
    'Home first frame accepted'     = '[SESSION] flux_home_ui_present: PASS'
    'Session owner verified'        = '[TEST] red_flux_owned_session: PASS'
    'Home first real frame'         = '[SESSION] flux_home_first_frame: PASS'
    'Login handoff completed'       = '[TEST] red_flux_login_to_desktop: PASS'
    'Login recovery rendered'       = '[SESSION] flux_login_error_render: PASS'
    'Launcher cleanup'              = '[SESSION] flux_launcher_cleanup: PASS'
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
    $_ -match 'PANIC_BEGIN|FATAL ERROR|flux_.*: FAIL|red_flux_login_to_desktop: FAIL'
})
if ($failed.Count -gt 0) {
    Write-Warning "Potential runtime failure markers:"
    $failed | Select-Object -Last 12 | ForEach-Object { Write-Host $_ }
}
Write-Host '--- Recent relevant serial lines ---'
$lines | Where-Object {
    $_ -match '^\[INPUT\]|^\[SESSION\]|\[TEST\] desktop_launcher|\[TEST\] red_flux_home_surface|PANIC_BEGIN|flux.*FAIL'
} | Select-Object -Last 60 | ForEach-Object { Write-Host $_ }
