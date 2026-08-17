[CmdletBinding()]
param(
    [string]$Image,
    [ValidateRange(256, 4096)]
    [int]$MemoryMiB = 1024
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Qemu = Join-Path $RootDir 'tools\qemu\qemu-system-x86_64.exe'
$Firmware = Join-Path $RootDir 'tools\qemu\share\edk2-x86_64-code.fd'
$FirmwareVars = Join-Path $RootDir 'tools\qemu\share\edk2-i386-vars.fd'

foreach ($required in @($Qemu, $Firmware, $FirmwareVars)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing QEMU runtime file: $required"
    }
}

if ([string]::IsNullOrWhiteSpace($Image)) {
    $candidate = Get-ChildItem -LiteralPath (Join-Path $RootDir 'dist') `
        -Filter 'KuroganeOS-*-windows-qemu.img' -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw 'No Windows QEMU IMG found in dist. Build with scripts\build-media.ps1 first.'
    }
    $Image = $candidate.FullName
}

$resolvedImage = (Resolve-Path -LiteralPath $Image -ErrorAction Stop).ProviderPath
$resolvedImage = [System.IO.Path]::GetFullPath($resolvedImage)
if ([System.IO.Path]::GetExtension($resolvedImage) -ne '.img') {
    throw "Expected a raw .img system image: $resolvedImage"
}

$RuntimeDir = Join-Path $RootDir 'build\qemu-windows'
[System.IO.Directory]::CreateDirectory($RuntimeDir) | Out-Null
$VarsCopy = Join-Path $RuntimeDir 'edk2-vars.fd'
Copy-Item -LiteralPath $FirmwareVars -Destination $VarsCopy -Force

function Quote-QemuPath([string]$Path) {
    return '"' + $Path + '"'
}

$arguments = @(
    '-machine', 'q35,accel=tcg',
    '-cpu', 'max',
    '-m', "$($MemoryMiB)M",
    '-vga', 'std',
    '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$(Quote-QemuPath $Firmware)",
    '-drive', "if=pflash,format=raw,unit=1,file=$(Quote-QemuPath $VarsCopy)",
    '-drive', "if=none,id=kurogane_system,format=raw,file=$(Quote-QemuPath $resolvedImage),snapshot=on,cache=writeback",
    '-device', 'ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1',
    '-netdev', 'user,id=kurogane_net',
    '-device', 'e1000,netdev=kurogane_net,mac=52:54:00:4b:55:01',
    '-audiodev', 'dsound,id=kurogane_audio',
    '-device', 'AC97,audiodev=kurogane_audio',
    '-serial', 'stdio',
    '-no-reboot',
    '-no-shutdown'
)

Write-Host "[qemu-windows] image: $resolvedImage"
Write-Host '[qemu-windows] network: E1000 + user NAT'
Write-Host '[qemu-windows] audio: Intel AC97 -> DirectSound'

# This visual runner intentionally stays attached to the terminal so serial
# diagnostics remain visible. Close the QEMU window or press Ctrl+C to stop it.
& $Qemu @arguments
if ($LASTEXITCODE -ne 0) {
    throw "QEMU exited with code $LASTEXITCODE"
}
