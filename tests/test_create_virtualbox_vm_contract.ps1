# Pure Windows host regression: VBoxManage is not required.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\scripts\lib\virtualbox-vm-contract.ps1')
$disk = 'C:\VMs\KuroganeOS\KuroganeOS.vdi'
$iso = 'D:\KuroganeOS\dist\KuroganeOS-7.0.0-dev-virtualbox-x86_64.iso'
$normal = @'
firmware="EFI64"
boot1="dvd"
boot2="disk"
keyboard="PS2"
mouse="PS2"
storagecontrollername0="SATA"
storagecontrollertype0="IntelAhci"
storagecontrollername1="IDE"
storagecontrollertype1="PIIX4"
"SATA-0-0"="C:\VMs\KuroganeOS\KuroganeOS.vdi"
"IDE-0-0"="D:\KuroganeOS\dist\KuroganeOS-7.0.0-dev-virtualbox-x86_64.iso"
'@
$alternate = @'
firmware="EFI"
boot1="DVD"
boot2="DISK"
keyboard="PS/2 Keyboard"
mouse="PS/2 Mouse"
storagecontrollername0="IDE"
storagecontrollertype0="PIIX4"
storagecontrollername1="SATA"
storagecontrollertype1="IntelAHCI"
"SATA-0-0"="C:\\VMs\\KuroganeOS\\KuroganeOS.vdi"
"IDE-0-0"="D:\\KuroganeOS\\dist\\KuroganeOS-7.0.0-dev-virtualbox-x86_64.iso"
'@
function Assert-Valid {
    param([string]$Snapshot)
    $problems = @(Test-KuroganeVBoxVmContract -MachineInfo $Snapshot -ExpectedDisk $disk -ExpectedIso $iso)
    if ($problems.Count -ne 0) { throw "Expected valid: $($problems -join '; ')" }
}
function Assert-Rejected {
    param([string]$Snapshot, [string]$Contains)
    $problems = @(Test-KuroganeVBoxVmContract -MachineInfo $Snapshot -ExpectedDisk $disk -ExpectedIso $iso)
    if ($problems.Count -eq 0 -or
        ($problems -join '; ') -notmatch [regex]::Escape($Contains)) {
        throw "Expected rejection '$Contains': $($problems -join '; ')"
    }
}
Assert-Valid -Snapshot $normal
Assert-Valid -Snapshot $alternate
# Observed VirtualBox output omits both input fields even when modifyvm
# returned success. Missing keys must not fail the entire VM.
Assert-Valid -Snapshot ($normal -replace '(?m)^keyboard="PS2"\r?\n?', '' -replace '(?m)^mouse="PS2"\r?\n?', '')
Assert-Valid -Snapshot ($normal -replace '(?m)^keyboard="PS2"\r?\n?', '')
Assert-Valid -Snapshot ($normal -replace '(?m)^mouse="PS2"\r?\n?', '')
# An explicitly reported incompatible/empty device is different from missing.
Assert-Rejected -Snapshot ($normal -replace 'keyboard="PS2"', 'keyboard="USB"') -Contains 'PS/2 keyboard'
Assert-Rejected -Snapshot ($normal -replace 'mouse="PS2"', 'mouse=""') -Contains 'PS/2 mouse'
Assert-Rejected -Snapshot ($normal -replace 'mouse="PS2"', 'mouse="USBTablet"') -Contains 'PS/2 mouse'
Assert-Rejected -Snapshot ($normal -replace 'boot2="disk"', 'boot2="dvd"') -Contains 'Boot order'
Assert-Rejected -Snapshot ($normal -replace 'IntelAhci', 'PIIX4') -Contains 'IntelAHCI'
Assert-Rejected -Snapshot ($normal -replace '"SATA-0-0"=', '"SATA-0-1"=') -Contains 'SATA 0:0'
Assert-Rejected -Snapshot ($normal -replace 'KuroganeOS-7.0.0-dev-virtualbox-x86_64.iso', 'old.iso') -Contains 'IDE 0:0'
Write-Host '[virtualbox-contract] 12 host regression scenarios: PASS'
