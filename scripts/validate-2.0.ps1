[CmdletBinding()]
param(
    [ValidateRange(60, 600)]
    [int]$TimeoutSeconds = 180,
    [ValidateRange(30, 300)]
    [int]$StabilitySeconds = 120,
    [switch]$SkipBuild,
    [switch]$SkipInstaller
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Build = Join-Path $PSScriptRoot 'build.ps1'
$Runner = Join-Path $PSScriptRoot 'run-qemu.ps1'
$Image = Join-Path $RootDir 'build\images\KuroganeOS-base.img'
$PowerShell = (Get-Command powershell.exe -ErrorAction Stop).Source

function Get-FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    }
    finally { $listener.Stop() }
}

function Invoke-QemuScenario {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [switch]$Shell,
        [switch]$Desktop,
        [switch]$Safe,
        [switch]$Usb,
        [int]$MinimumRuntime = 0
    )
    Write-Host "[scenario] $Name"
    # QEMU/OVMF boot-key injection is stateful in the Windows PowerShell host.
    # A fresh host per VM keeps consecutive desktop/safe selections reliable.
    $runnerArguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Runner,
        '-UseDiskImage', '-DiskImagePath', $Image, '-Headless',
        '-TimeoutSeconds', [string]$TimeoutSeconds,
        '-MinimumRuntimeSeconds', [string]$MinimumRuntime,
        '-MonitorPort', [string](Get-FreePort),
        '-LogName', "qemu-$Name"
    )
    if ($Shell) { $runnerArguments += '-ShellTest' }
    if ($Desktop) { $runnerArguments += '-DesktopMode' }
    if ($Safe) { $runnerArguments += '-SafeMode' }
    if ($Usb) { $runnerArguments += '-UsbTest' }
    & $PowerShell @runnerArguments
    if ($LASTEXITCODE -ne 0) { throw "QEMU scenario failed: $Name" }
}

if (-not $SkipBuild) {
    & $Build -Configuration debug -Rebuild
    if ($LASTEXITCODE -ne 0) { throw 'Clean debug build failed.' }
}
if (-not (Test-Path -LiteralPath $Image -PathType Leaf)) {
    throw "Missing Foundation image: $Image"
}

$WslRoot = (& wsl.exe --exec wslpath -a -u $RootDir).Trim()
if ($LASTEXITCODE -ne 0 -or -not $WslRoot.StartsWith('/')) {
    throw 'Could not resolve the repository path in WSL2.'
}
& wsl.exe bash "$WslRoot/scripts/test.sh"
if ($LASTEXITCODE -ne 0) { throw 'Hosted tests failed.' }
& (Join-Path $PSScriptRoot 'test-foundation-image.ps1') -ImagePath $Image

Invoke-QemuScenario -Name 'boot'
Invoke-QemuScenario -Name 'userspace' -Shell
Invoke-QemuScenario -Name 'multitasking' -Shell
Invoke-QemuScenario -Name 'filesystem' -Shell
Invoke-QemuScenario -Name 'network' -Shell
Invoke-QemuScenario -Name 'desktop' -Shell -Desktop
Invoke-QemuScenario -Name 'full-system' -Shell
Invoke-QemuScenario -Name 'safe' -Safe
Invoke-QemuScenario -Name 'usb' -Usb

& (Join-Path $PSScriptRoot 'test-persistence.ps1') `
    -SourceImage $Image -TimeoutSeconds ([Math]::Min(120, $TimeoutSeconds))
if ($LASTEXITCODE -ne 0) { throw 'Persistence scenario failed.' }

$stabilityTimeout = [Math]::Min(600, $StabilitySeconds + 90)
$previousTimeout = $TimeoutSeconds
$TimeoutSeconds = $stabilityTimeout
Invoke-QemuScenario -Name 'stability' -Shell -Desktop `
    -MinimumRuntime $StabilitySeconds
$TimeoutSeconds = $previousTimeout

if (-not $SkipInstaller) {
    & (Join-Path $PSScriptRoot 'test-installer.ps1') `
        -SkipBuild -InstallTimeoutSeconds ([Math]::Max(240, $TimeoutSeconds))
    if ($LASTEXITCODE -ne 0) { throw 'Installer scenario failed.' }
}

$artifactDescription = if ($SkipBuild) {
    'existing clean-build artifacts'
} else {
    'clean build and package'
}
Write-Host "[pass] KuroganeOS 2.0 $artifactDescription, host tests, QEMU matrix (including safe mode and USB HID), persistence, stability and installer validation passed."
