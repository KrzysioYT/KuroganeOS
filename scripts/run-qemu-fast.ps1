[CmdletBinding()]
param(
    [string]$DiskImagePath,
    [ValidateRange(128, 4096)]
    [int]$MemoryMiB = 1024,
    [ValidateSet('auto', 'whpx', 'tcg')]
    [string]$Accelerator = 'auto',
    [switch]$WritableDiskImage,
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$LogName = 'qemu-fast'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Qemu = Join-Path $RootDir 'tools\qemu\qemu-system-x86_64.exe'
$Firmware = Join-Path $RootDir 'tools\qemu\share\edk2-x86_64-code.fd'
$FirmwareVars = Join-Path $RootDir 'tools\qemu\share\edk2-i386-vars.fd'
$BuildDir = Join-Path $RootDir 'build'
$LogDir = Join-Path $BuildDir 'logs'
$imageWasExplicit = $PSBoundParameters.ContainsKey('DiskImagePath')

if ([string]::IsNullOrWhiteSpace($DiskImagePath)) {
    $working = Join-Path $RootDir 'state\KuroganeOS.img'
    $base = Join-Path $BuildDir 'images\KuroganeOS-base.img'
    $DiskImagePath = if (Test-Path -LiteralPath $working -PathType Leaf) {
        $working
    } else {
        $base
    }
}
if ($WritableDiskImage -and -not $imageWasExplicit) {
    throw '-WritableDiskImage requires an explicit -DiskImagePath. The default working/base image is protected.'
}

foreach ($required in @($Qemu, $Firmware, $FirmwareVars, $DiskImagePath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing QEMU input: $required"
    }
}

$Image = [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $DiskImagePath).ProviderPath)
$item = Get-Item -LiteralPath $Image -Force
if ($Image.Contains(',')) { throw "Disk image path cannot contain a comma: $Image" }
if ($item.Length -lt 512 -or ($item.Length % 512) -ne 0) {
    throw "Disk image must be non-empty and 512-byte aligned: $Image"
}
if ([System.IO.Path]::GetExtension($Image) -notin @('.img', '.raw')) {
    throw "Disk image must use .img or .raw: $Image"
}
if ($WritableDiskImage -and $item.IsReadOnly) {
    throw "Disk image is read-only: $Image"
}

[System.IO.Directory]::CreateDirectory($LogDir) | Out-Null

function Invoke-KuroganeQemu {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('whpx', 'tcg')]
        [string]$Mode
    )

    $SerialLog = Join-Path $LogDir "$LogName-$Mode-serial.log"
    $StdoutLog = Join-Path $LogDir "$LogName-$Mode-stdout.log"
    $StderrLog = Join-Path $LogDir "$LogName-$Mode-stderr.log"
    foreach ($log in @($SerialLog, $StdoutLog, $StderrLog)) {
        if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
    }

    $snapshot = if ($WritableDiskImage) { 'off' } else { 'on' }
    $quotedImage = '"' + $Image + '"'
    $arguments = @(
        '-machine', 'q35',
        '-accel', $Mode,
        '-m', "$($MemoryMiB)M",
        '-smp', '1',
        '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$Firmware",
        '-drive', "if=pflash,format=raw,unit=1,snapshot=on,file=$FirmwareVars",
        '-drive', "if=none,id=kurogane_system,format=raw,file=$quotedImage,snapshot=$snapshot,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1',
        '-serial', "file:$SerialLog",
        '-netdev', 'user,id=kurogane_net',
        '-device', 'e1000,netdev=kurogane_net,mac=52:54:00:4b:55:01',
        '-monitor', 'none',
        '-no-reboot',
        '-no-shutdown'
    )
    if ($Mode -eq 'tcg') { $arguments += @('-cpu', 'max') }

    Write-Host "[qemu-fast] accelerator=$Mode memory=${MemoryMiB}MiB"
    Write-Host "[image] $Image"
    Write-Host "[image-mode] $(if ($WritableDiskImage) { 'writable' } else { 'protected snapshot' })"
    Write-Host "[serial] $SerialLog"

    $process = Start-Process `
        -FilePath $Qemu `
        -ArgumentList $arguments `
        -RedirectStandardOutput $StdoutLog `
        -RedirectStandardError $StderrLog `
        -WindowStyle Normal `
        -PassThru

    return [pscustomobject]@{
        Process = $process
        Stderr = $StderrLog
        Serial = $SerialLog
        Mode = $Mode
    }
}

function Test-AccelerationFailure {
    param([Parameter(Mandatory = $true)][string]$StderrPath)
    if (-not (Test-Path -LiteralPath $StderrPath)) { return $false }
    $text = Get-Content -LiteralPath $StderrPath -Raw -ErrorAction SilentlyContinue
    if ([string]::IsNullOrWhiteSpace($text)) { return $false }
    return $text -match '(?i)(whpx|hypervisor).*(failed|unavailable|not available|not supported|not implemented)|failed to initialize.*whpx|no accelerator found'
}

$launch = $null
if ($Accelerator -eq 'auto') {
    $launch = Invoke-KuroganeQemu -Mode 'whpx'
    Start-Sleep -Milliseconds 1500
    $launch.Process.Refresh()
    if ($launch.Process.HasExited) {
        if (Test-AccelerationFailure -StderrPath $launch.Stderr) {
            Write-Warning 'WHPX is unavailable; falling back to QEMU TCG.'
            $launch = Invoke-KuroganeQemu -Mode 'tcg'
        } else {
            Write-Warning 'WHPX QEMU exited during startup; stderr follows.'
            if (Test-Path -LiteralPath $launch.Stderr) {
                Get-Content -LiteralPath $launch.Stderr -Tail 40
            }
            exit $launch.Process.ExitCode
        }
    }
} else {
    $launch = Invoke-KuroganeQemu -Mode $Accelerator
}

Write-Host "[active] accelerator=$($launch.Mode)"
Write-Host 'Close the QEMU window to return to PowerShell.'
$launch.Process.WaitForExit()

if ($launch.Process.ExitCode -ne 0) {
    Write-Warning "QEMU exited with code $($launch.Process.ExitCode)."
    if (Test-Path -LiteralPath $launch.Stderr) {
        Get-Content -LiteralPath $launch.Stderr -Tail 40
    }
    exit $launch.Process.ExitCode
}
