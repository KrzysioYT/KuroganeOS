[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [ValidateRange(60, 600)]
    [int]$InstallTimeoutSeconds = 240
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$TestDiskDir = Join-Path $RootDir 'build\test-disks'
$Target = [System.IO.Path]::GetFullPath(
    (Join-Path $TestDiskDir 'installer-target.img'))
$ExpectedTarget = [System.IO.Path]::GetFullPath(
    (Join-Path $RootDir 'build\test-disks\installer-target.img'))
if ($Target -ne $ExpectedTarget) {
    throw "Refusing unexpected installer test target: $Target"
}
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-installer.ps1') -NoBuild
    if (-not $?) { throw 'Installer image build failed.' }
}

[System.IO.Directory]::CreateDirectory($TestDiskDir) | Out-Null
$stream = [System.IO.File]::Open(
    $Target, [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
try { $stream.SetLength(512MB) }
finally { $stream.Dispose() }
Write-Host "[test-disk] created blank virtual disk: $Target"

& (Join-Path $PSScriptRoot 'run-qemu.ps1') `
    -InstallerTest -InstallerDiskPath $Target -Headless `
    -TimeoutSeconds $InstallTimeoutSeconds -MonitorPort 45510 `
    -LogName 'installer-deploy'
if (-not $?) { throw 'Guest installer phase failed.' }

& (Join-Path $PSScriptRoot 'run-qemu.ps1') `
    -UseDiskImage -DiskImagePath $Target -WritableDiskImage `
    -ShellTest -DesktopMode -Headless -TimeoutSeconds 90 -MonitorPort 45511 `
    -LogName 'installer-first-boot'
if (-not $?) { throw 'Installed first boot failed.' }
$firstLog = Get-Content -LiteralPath `
    (Join-Path $RootDir 'build\logs\installer-first-boot-serial.log') -Raw
if ($firstLog -notmatch '\[TEST\] installed_first_boot: PASS') {
    throw 'Installed system did not complete its first-boot transaction.'
}

& (Join-Path $PSScriptRoot 'run-qemu.ps1') `
    -UseDiskImage -DiskImagePath $Target -WritableDiskImage `
    -ShellTest -DesktopMode -Headless -TimeoutSeconds 90 -MonitorPort 45512 `
    -LogName 'installer-second-boot'
if (-not $?) { throw 'Installed persistence boot failed.' }
$secondLog = Get-Content -LiteralPath `
    (Join-Path $RootDir 'build\logs\installer-second-boot-serial.log') -Raw
if ($secondLog -notmatch '\[TEST\] installed_persistence: PASS') {
    throw 'Installed system did not retain its first-boot marker.'
}
Write-Host '[pass] installer ISO -> virtual disk -> first boot -> persistent reboot'
