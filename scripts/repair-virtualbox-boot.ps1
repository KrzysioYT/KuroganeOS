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

    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'SilentlyContinue'
        $result = & $VBoxManage showvminfo $VmName --machinereadable 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) {
        throw "Unable to inspect VirtualBox VM '$VmName'.`n$($result -join [Environment]::NewLine)"
    }
    return @($result | ForEach-Object { $_.ToString() })
}

function Invoke-VBoxChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VBoxManage,
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)]
        [string]$FailureMessage
    )

    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'SilentlyContinue'
        $output = & $VBoxManage @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) {
        throw "$FailureMessage`n$($output -join [Environment]::NewLine)"
    }
    return @($output | ForEach-Object { $_.ToString() })
}

function Decode-VBoxMediumPath {
    param([Parameter(Mandatory = $true)][string]$Value)
    return $Value -replace '\\\\', '\'
}

function Test-DiskMediumPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $extension = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    return $extension -in @('.vdi', '.vmdk', '.vhd', '.vhdx', '.hdd', '.raw', '.img')
}

$VBoxCommand = Get-Command VBoxManage.exe -ErrorAction SilentlyContinue
if ($null -ne $VBoxCommand) {
    $VBox = $VBoxCommand.Path
} else {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $VBox = $candidate
    } else {
        throw 'VBoxManage.exe was not found. Install Oracle VirtualBox first.'
    }
}

$info = Get-VmInfo -VBoxManage $VBox -VmName $Name
$stateLine = $info | Where-Object { $_ -like 'VMState=*' } | Select-Object -First 1
if ($stateLine -match '^VMState="([^"]+)"$' -and $Matches[1] -notin @('poweroff', 'aborted')) {
    throw "VM '$Name' must be fully powered off before repair. Current state: $($Matches[1]). Discard only the saved VM state if necessary; do not delete the virtual disk."
}

$IsoPath = $null
if (-not [string]::IsNullOrWhiteSpace($Iso)) {
    $IsoPath = Get-KuroganeFileSystemPath -Path $Iso
    if (-not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) {
        throw "ISO not found: $IsoPath"
    }
    if ([System.IO.Path]::GetExtension($IsoPath).ToLowerInvariant() -ne '.iso') {
        throw "Expected an .iso optical image, got: $IsoPath"
    }
}

Write-Host "[virtualbox-repair] VM: $Name"
Write-Host "[virtualbox-repair] VBoxManage: $VBox"
Write-Host '[virtualbox-repair] enforcing KuroganeOS VirtualBox contract: EFI64 + AHCI HDD + IDE DVD'
if ($null -ne $IsoPath) {
    Write-Host "[virtualbox-repair] ISO: $IsoPath"
}

$null = Invoke-VBoxChecked -VBoxManage $VBox -Arguments @(
    'modifyvm', $Name,
    '--firmware', 'efi64',
    '--ioapic', 'on',
    '--boot1', 'dvd',
    '--boot2', 'disk',
    '--boot3', 'none',
    '--boot4', 'none'
) -FailureMessage 'Failed to switch the VM to EFI64/DVD-first boot.'

$freshInfo = Get-VmInfo -VBoxManage $VBox -VmName $Name
$sataController = $freshInfo |
    Where-Object { $_ -match '^storagecontrollername\d+="SATA"$' } |
    Select-Object -First 1
if ($null -eq $sataController) {
    Write-Host '[virtualbox-repair] SATA controller absent; creating IntelAHCI controller'
    $null = Invoke-VBoxChecked -VBoxManage $VBox -Arguments @(
        'storagectl', $Name,
        '--name', 'SATA',
        '--add', 'sata',
        '--controller', 'IntelAHCI'
    ) -FailureMessage "Could not create the SATA/IntelAHCI controller named 'SATA'."
}

$freshInfo = Get-VmInfo -VBoxManage $VBox -VmName $Name
$sataDiskLine = $freshInfo |
    Where-Object { $_ -match '^"SATA-\d+-\d+"=".+"$' -and $_ -notmatch '="(none|emptydrive)"$' } |
    Select-Object -First 1

if ($null -eq $sataDiskLine) {
    $ideDisk = $null
    foreach ($line in $freshInfo) {
        if ($line -match '^"IDE-(\d+)-(\d+)"="(.+)"$') {
            $mediumPath = Decode-VBoxMediumPath -Value $Matches[3]
            if (Test-DiskMediumPath -Path $mediumPath) {
                $ideDisk = @{
                    Port = [int]$Matches[1]
                    Device = [int]$Matches[2]
                    Medium = $mediumPath
                }
                break
            }
        }
    }

    if ($null -eq $ideDisk) {
        throw 'No virtual HDD was found on SATA/AHCI or IDE. Attach/create a VDI/VMDK/VHD before installing KuroganeOS.'
    }

    Write-Host "[virtualbox-repair] moving HDD from IDE $($ideDisk.Port):$($ideDisk.Device) to SATA 0:0: $($ideDisk.Medium)"
    $null = Invoke-VBoxChecked -VBoxManage $VBox -Arguments @(
        'storageattach', $Name,
        '--storagectl', 'IDE',
        '--port', "$($ideDisk.Port)",
        '--device', "$($ideDisk.Device)",
        '--type', 'hdd',
        '--medium', 'none'
    ) -FailureMessage 'Failed to detach the existing HDD from IDE before AHCI migration.'

    try {
        $null = Invoke-VBoxChecked -VBoxManage $VBox -Arguments @(
            'storageattach', $Name,
            '--storagectl', 'SATA',
            '--port', '0',
            '--device', '0',
            '--type', 'hdd',
            '--medium', $ideDisk.Medium
        ) -FailureMessage 'Failed to attach the existing HDD to SATA/IntelAHCI.'
    } catch {
        Write-Warning 'AHCI migration failed; attempting to restore the HDD to its original IDE slot.'
        try {
            $null = Invoke-VBoxChecked -VBoxManage $VBox -Arguments @(
                'storageattach', $Name,
                '--storagectl', 'IDE',
                '--port', "$($ideDisk.Port)",
                '--device', "$($ideDisk.Device)",
                '--type', 'hdd',
                '--medium', $ideDisk.Medium
            ) -FailureMessage 'Rollback to the original IDE HDD slot failed.'
        } catch {
            Write-Warning $_.Exception.Message
        }
        throw
    }
}

$freshInfo = Get-VmInfo -VBoxManage $VBox -VmName $Name
$ideController = $freshInfo |
    Where-Object { $_ -match '^storagecontrollername\d+="IDE"$' } |
    Select-Object -First 1
if ($null -eq $ideController) {
    Write-Host '[virtualbox-repair] IDE controller absent; creating PIIX4 IDE controller for optical media'
    $null = Invoke-VBoxChecked -VBoxManage $VBox -Arguments @(
        'storagectl', $Name,
        '--name', 'IDE',
        '--add', 'ide',
        '--controller', 'PIIX4'
    ) -FailureMessage "Could not create the PIIX4 IDE controller named 'IDE'."
}

if ($null -ne $IsoPath) {
    $freshInfo = Get-VmInfo -VBoxManage $VBox -VmName $Name
    $slotCandidates = @(
        @{ Port = 0; Device = 0 },
        @{ Port = 1; Device = 0 },
        @{ Port = 0; Device = 1 },
        @{ Port = 1; Device = 1 }
    )

    $selectedSlot = $null
    foreach ($slot in $slotCandidates) {
        $slotPattern = '^"IDE-' + $slot.Port + '-' + $slot.Device + '"="(.*)"$'
        $line = $freshInfo | Where-Object { $_ -match $slotPattern } | Select-Object -First 1
        if ($null -eq $line) {
            $selectedSlot = $slot
            break
        }
        if ($line -match $slotPattern) {
            $existing = Decode-VBoxMediumPath -Value $Matches[1]
            if ($existing -eq 'emptydrive' -or
                [System.IO.Path]::GetExtension($existing).ToLowerInvariant() -eq '.iso') {
                $selectedSlot = $slot
                break
            }
        }
    }

    if ($null -eq $selectedSlot) {
        throw 'No IDE optical slot is available for the VirtualBox ISO. Existing HDD attachments were left untouched.'
    }

    $null = Invoke-VBoxChecked -VBoxManage $VBox -Arguments @(
        'storageattach', $Name,
        '--storagectl', 'IDE',
        '--port', "$($selectedSlot.Port)",
        '--device', "$($selectedSlot.Device)",
        '--type', 'dvddrive',
        '--medium', $IsoPath
    ) -FailureMessage "Failed to attach the ISO to IDE $($selectedSlot.Port):$($selectedSlot.Device)."
    Write-Host "[virtualbox-repair] ISO attached: IDE $($selectedSlot.Port):$($selectedSlot.Device) -> $IsoPath"
}

$finalInfo = Get-VmInfo -VBoxManage $VBox -VmName $Name
$firmware = $finalInfo | Where-Object { $_ -like 'firmware=*' } | Select-Object -First 1
$boot1 = $finalInfo | Where-Object { $_ -like 'boot1=*' } | Select-Object -First 1
$boot2 = $finalInfo | Where-Object { $_ -like 'boot2=*' } | Select-Object -First 1
$ahciPresent = $finalInfo -match 'storagecontrollertype\d+="IntelAhci"'
$sataDiskPresent = $finalInfo -match '^"SATA-\d+-\d+"=".+"$'

if ($firmware -ne 'firmware="EFI64"' -or
    $boot1 -ne 'boot1="dvd"' -or
    $boot2 -ne 'boot2="disk"') {
    throw 'Final VirtualBox firmware/boot-order verification failed.'
}
if (-not $ahciPresent -or -not $sataDiskPresent) {
    throw 'Final VirtualBox storage verification failed: KuroganeOS installer requires a HDD attached through SATA/IntelAHCI.'
}

Write-Host "[virtualbox-repair] $firmware"
Write-Host "[virtualbox-repair] $boot1"
Write-Host "[virtualbox-repair] $boot2"
Write-Host '[virtualbox-repair] SATA/IntelAHCI HDD: PASS'
Write-Host '[virtualbox-repair] PASS: supported KuroganeOS VirtualBox boot/install profile applied.'
Write-Host "[virtualbox-repair] Start with: VBoxManage startvm `"$Name`""
