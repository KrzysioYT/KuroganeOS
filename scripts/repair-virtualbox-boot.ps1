[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Name,
    [string]$Iso
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$VBoxManage = Get-Command VBoxManage.exe -ErrorAction SilentlyContinue
if ($null -eq $VBoxManage) {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $VBoxManage = Get-Item -LiteralPath $candidate
    } else {
        throw 'VBoxManage.exe was not found. Install Oracle VirtualBox first.'
    }
}
$VBox = $VBoxManage.Source

$info = & $VBox showvminfo $Name --machinereadable 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "VirtualBox VM was not found: $Name`n$($info -join [Environment]::NewLine)"
}

$stateLine = $info | Where-Object { $_ -like 'VMState=*' } | Select-Object -First 1
if ($stateLine -match '^VMState="([^"]+)"$' -and $Matches[1] -notin @('poweroff', 'aborted', 'saved')) {
    throw "VM '$Name' must be powered off before repair. Current state: $($Matches[1])"
}

Write-Host "[virtualbox-repair] VM: $Name"
Write-Host '[virtualbox-repair] enforcing KuroganeOS x86-64 UEFI boot profile'

& $VBox modifyvm $Name `
    --firmware efi64 `
    --ioapic on `
    --boot1 dvd `
    --boot2 disk `
    --boot3 none `
    --boot4 none | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to switch the VM to EFI64/DVD-first boot.'
}

if (-not [string]::IsNullOrWhiteSpace($Iso)) {
    $Iso = [System.IO.Path]::GetFullPath($Iso)
    if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
        throw "ISO not found: $Iso"
    }
    if ([System.IO.Path]::GetExtension($Iso) -ne '.iso') {
        throw "Expected an .iso optical image, got: $Iso"
    }

    $controllerReady = $false
    & $VBox storagectl $Name --name IDE --add ide --controller PIIX4 *> $null
    if ($LASTEXITCODE -eq 0) {
        $controllerReady = $true
    } else {
        $freshInfo = & $VBox showvminfo $Name --machinereadable 2>&1
        if ($LASTEXITCODE -eq 0 -and ($freshInfo -match 'storagecontrollername\d+="IDE"')) {
            $controllerReady = $true
        }
    }
    if (-not $controllerReady) {
        throw "Could not create or find the reference IDE optical controller named 'IDE'. Attach the ISO manually and rerun without -Iso."
    }

    & $VBox storageattach $Name `
        --storagectl IDE `
        --port 0 `
        --device 0 `
        --type dvddrive `
        --medium $Iso | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to attach ISO to IDE 0:0: $Iso"
    }
    Write-Host "[virtualbox-repair] ISO attached: $Iso"
}

$finalInfo = & $VBox showvminfo $Name --machinereadable 2>&1
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to verify the repaired VirtualBox VM.'
}

$firmware = $finalInfo | Where-Object { $_ -like 'firmware=*' } | Select-Object -First 1
$boot1 = $finalInfo | Where-Object { $_ -like 'boot1=*' } | Select-Object -First 1
$boot2 = $finalInfo | Where-Object { $_ -like 'boot2=*' } | Select-Object -First 1

Write-Host "[virtualbox-repair] $firmware"
Write-Host "[virtualbox-repair] $boot1"
Write-Host "[virtualbox-repair] $boot2"
Write-Host '[virtualbox-repair] PASS: EFI64 + DVD-first boot profile applied.'
Write-Host "[virtualbox-repair] Start with: VBoxManage startvm `"$Name`""
