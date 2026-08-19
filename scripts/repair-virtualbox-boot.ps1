[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Name,
    [string]$Iso
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-KuroganeFileSystemPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    try {
        # Resolve relative paths through PowerShell's provider location rather
        # than the process working directory. The latter can be System32 even
        # while the caller is at E:\KuroganeOS.
        return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    } catch {
        throw "Invalid filesystem path '$Path': $($_.Exception.Message)"
    }
}

function Get-VmInfo {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VBoxManage,
        [Parameter(Mandatory = $true)]
        [string]$VmName
    )

    $result = & $VBoxManage showvminfo $VmName --machinereadable 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect VirtualBox VM '$VmName'.`n$($result -join [Environment]::NewLine)"
    }
    return @($result)
}

$VBoxCommand = Get-Command VBoxManage.exe -ErrorAction SilentlyContinue
if ($null -ne $VBoxCommand) {
    $VBox = $VBoxCommand.Path
} else {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $VBox = [System.IO.Path]::GetFullPath($candidate)
    } else {
        throw 'VBoxManage.exe was not found. Install Oracle VirtualBox first.'
    }
}

$info = Get-VmInfo -VBoxManage $VBox -VmName $Name
$stateLine = $info | Where-Object { $_ -like 'VMState=*' } | Select-Object -First 1
if ($stateLine -match '^VMState="([^"]+)"$' -and $Matches[1] -notin @('poweroff', 'aborted', 'saved')) {
    throw "VM '$Name' must be powered off before repair. Current state: $($Matches[1])"
}

# Validate media before changing the VM so a typo cannot leave a partial repair.
$IsoPath = $null
if (-not [string]::IsNullOrWhiteSpace($Iso)) {
    $IsoPath = Get-KuroganeFileSystemPath -Path $Iso
    if (-not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) {
        throw "ISO not found: $IsoPath"
    }
    if ([System.IO.Path]::GetExtension($IsoPath) -ne '.iso') {
        throw "Expected an .iso optical image, got: $IsoPath"
    }
}

Write-Host "[virtualbox-repair] VM: $Name"
Write-Host "[virtualbox-repair] VBoxManage: $VBox"
Write-Host '[virtualbox-repair] enforcing KuroganeOS x86-64 UEFI boot profile'
if ($null -ne $IsoPath) {
    Write-Host "[virtualbox-repair] ISO: $IsoPath"
}

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

if ($null -ne $IsoPath) {
    $freshInfo = Get-VmInfo -VBoxManage $VBox -VmName $Name
    $ideController = $freshInfo |
        Where-Object { $_ -match '^storagecontrollername\d+="IDE"$' } |
        Select-Object -First 1

    if ($null -eq $ideController) {
        Write-Host '[virtualbox-repair] IDE controller absent; creating PIIX4 IDE controller'
        & $VBox storagectl $Name --name IDE --add ide --controller PIIX4 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Could not create the reference IDE controller named 'IDE'."
        }
        $freshInfo = Get-VmInfo -VBoxManage $VBox -VmName $Name
        $ideController = $freshInfo |
            Where-Object { $_ -match '^storagecontrollername\d+="IDE"$' } |
            Select-Object -First 1
        if ($null -eq $ideController) {
            throw "VirtualBox did not expose the newly created IDE controller named 'IDE'."
        }
    } else {
        Write-Host '[virtualbox-repair] existing IDE controller detected; reusing it'
    }

    # Never replace an existing attachment. A KuroganeOS VDI may already live
    # at IDE 0:0. Prefer secondary master for optical media, then other free IDE
    # slots. Existing disks are never detached or moved by this helper.
    $slotCandidates = @(
        @{ Port = 1; Device = 0 },
        @{ Port = 0; Device = 1 },
        @{ Port = 1; Device = 1 },
        @{ Port = 0; Device = 0 }
    )

    $selectedSlot = $null
    foreach ($slot in $slotCandidates) {
        $slotPattern = '^"IDE-' + $slot.Port + '-' + $slot.Device + '"='
        $occupied = $freshInfo | Where-Object { $_ -match $slotPattern } | Select-Object -First 1
        if ($null -eq $occupied) {
            $selectedSlot = $slot
            break
        }
    }

    if ($null -eq $selectedSlot) {
        throw 'No free IDE slot is available for the repair ISO. Existing storage attachments were left untouched.'
    }

    & $VBox storageattach $Name `
        --storagectl IDE `
        --port $selectedSlot.Port `
        --device $selectedSlot.Device `
        --type dvddrive `
        --medium $IsoPath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to attach ISO to free IDE $($selectedSlot.Port):$($selectedSlot.Device): $IsoPath"
    }
    Write-Host "[virtualbox-repair] ISO attached: IDE $($selectedSlot.Port):$($selectedSlot.Device) -> $IsoPath"
}

$finalInfo = Get-VmInfo -VBoxManage $VBox -VmName $Name
$firmware = $finalInfo | Where-Object { $_ -like 'firmware=*' } | Select-Object -First 1
$boot1 = $finalInfo | Where-Object { $_ -like 'boot1=*' } | Select-Object -First 1
$boot2 = $finalInfo | Where-Object { $_ -like 'boot2=*' } | Select-Object -First 1

Write-Host "[virtualbox-repair] $firmware"
Write-Host "[virtualbox-repair] $boot1"
Write-Host "[virtualbox-repair] $boot2"
Write-Host '[virtualbox-repair] PASS: EFI64 + DVD-first boot profile applied.'
Write-Host "[virtualbox-repair] Start with: VBoxManage startvm `"$Name`""
