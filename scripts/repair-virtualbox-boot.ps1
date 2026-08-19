[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Name,
    [string]$Iso,
    [string]$SerialLog
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-KuroganeFileSystemPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    try {
        return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    } catch {
        throw "Invalid filesystem path '$Path': $($_.Exception.Message)"
    }
}

function ConvertTo-WindowsCommandLineArgument {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') {
        return $Value
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashes
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * ($backslashes * 2 + 1)))
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) {
            [void]$builder.Append(('\' * $backslashes))
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($backslashes -gt 0) {
        [void]$builder.Append(('\' * ($backslashes * 2)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

$VBoxCommand = Get-Command VBoxManage.exe -ErrorAction SilentlyContinue
if ($null -ne $VBoxCommand) {
    $VBox = $VBoxCommand.Path
} else {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw 'VBoxManage.exe was not found. Install Oracle VirtualBox first.'
    }
    $VBox = $candidate
}

function Invoke-VBoxNative {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$AllowFailure
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $VBox
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.Arguments = (($Arguments | ForEach-Object {
        ConvertTo-WindowsCommandLineArgument -Value ([string]$_)
    }) -join ' ')

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    try {
        if (-not $process.Start()) {
            throw "Failed to start VBoxManage.exe: $($Arguments -join ' ')"
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result
        $exitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }

    $result = [pscustomobject]@{
        ExitCode = $exitCode
        StdOut = $stdout
        StdErr = $stderr
    }
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        $details = @($stdout, $stderr) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { $_.TrimEnd() }
        throw "VBoxManage failed ($exitCode): $($Arguments -join ' ')`n$($details -join [Environment]::NewLine)"
    }
    return $result
}

function Invoke-VBoxChecked {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$FailureMessage
    )
    $result = Invoke-VBoxNative -Arguments $Arguments -AllowFailure
    if ($result.ExitCode -ne 0) {
        $details = @($result.StdOut, $result.StdErr) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { $_.TrimEnd() }
        throw "$FailureMessage`n$($details -join [Environment]::NewLine)"
    }
    return $result
}

function Get-RegisteredVmNames {
    $result = Invoke-VBoxNative -Arguments @('list', 'vms') -AllowFailure
    if ($result.ExitCode -ne 0) { return @() }

    $names = @()
    foreach ($line in ($result.StdOut -split "`r?`n")) {
        if ($line -match '^"(.+)"\s+\{[0-9a-fA-F-]+\}$') {
            $names += $Matches[1]
        }
    }
    return $names
}

function Resolve-VmName {
    param([Parameter(Mandatory = $true)][string]$RequestedName)

    $probe = Invoke-VBoxNative -Arguments @(
        'showvminfo', $RequestedName, '--machinereadable'
    ) -AllowFailure
    if ($probe.ExitCode -eq 0) { return $RequestedName }

    $registered = @(Get-RegisteredVmNames)
    $exact = @($registered | Where-Object {
        $_.Equals($RequestedName, [System.StringComparison]::OrdinalIgnoreCase)
    })
    if ($exact.Count -eq 1) { return $exact[0] }

    $prefix = @($registered | Where-Object {
        $_.StartsWith($RequestedName, [System.StringComparison]::OrdinalIgnoreCase)
    })
    if ($prefix.Count -eq 1) {
        Write-Host "[virtualbox-repair] resolved VM prefix '$RequestedName' -> '$($prefix[0])'"
        return $prefix[0]
    }

    $nativeError = @($probe.StdOut, $probe.StdErr) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_.TrimEnd() }
    $available = if ($registered.Count -eq 0) {
        '<none reported by VBoxManage list vms>'
    } else {
        ($registered | ForEach-Object { "  - $_" }) -join [Environment]::NewLine
    }
    if ($prefix.Count -gt 1) {
        $matches = ($prefix | ForEach-Object { "  - $_" }) -join [Environment]::NewLine
        throw "VM name '$RequestedName' is ambiguous. Matching registered VMs:`n$matches`nUse the exact -Name value."
    }
    throw "VirtualBox VM '$RequestedName' was not found or could not be inspected.`nVBoxManage:`n$($nativeError -join [Environment]::NewLine)`nRegistered VMs:`n$available"
}

function Get-VmInfo {
    param([Parameter(Mandatory = $true)][string]$VmName)
    $result = Invoke-VBoxNative -Arguments @(
        'showvminfo', $VmName, '--machinereadable'
    ) -AllowFailure
    if ($result.ExitCode -ne 0) {
        $details = @($result.StdOut, $result.StdErr) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { $_.TrimEnd() }
        throw "Unable to inspect VirtualBox VM '$VmName'.`n$($details -join [Environment]::NewLine)"
    }
    return @($result.StdOut -split "`r?`n" | Where-Object { $_ -ne '' })
}

function Get-StorageControllers {
    param([Parameter(Mandatory = $true)][string[]]$Info)

    $controllers = @{}
    foreach ($line in $Info) {
        if ($line -match '^storagecontrollername(\d+)="(.*)"$') {
            $index = [int]$Matches[1]
            if (-not $controllers.ContainsKey($index)) {
                $controllers[$index] = @{ Index = $index; Name = $null; Type = $null }
            }
            $controllers[$index].Name = $Matches[2]
            continue
        }
        if ($line -match '^storagecontrollertype(\d+)="(.*)"$') {
            $index = [int]$Matches[1]
            if (-not $controllers.ContainsKey($index)) {
                $controllers[$index] = @{ Index = $index; Name = $null; Type = $null }
            }
            $controllers[$index].Type = $Matches[2]
        }
    }

    $result = @()
    foreach ($entry in ($controllers.GetEnumerator() | Sort-Object Key)) {
        if (-not [string]::IsNullOrWhiteSpace($entry.Value.Name)) {
            $result += [pscustomobject]@{
                Index = $entry.Value.Index
                Name = [string]$entry.Value.Name
                Type = [string]$entry.Value.Type
            }
        }
    }
    return $result
}

function Get-ControllerAttachments {
    param(
        [Parameter(Mandatory = $true)][string[]]$Info,
        [Parameter(Mandatory = $true)][string]$ControllerName
    )

    $escaped = [regex]::Escape($ControllerName)
    $pattern = '^"' + $escaped + '-(\d+)-(\d+)"="(.*)"$'
    $attachments = @()
    foreach ($line in $Info) {
        if ($line -match $pattern) {
            $attachments += [pscustomobject]@{
                Port = [int]$Matches[1]
                Device = [int]$Matches[2]
                Medium = ($Matches[3] -replace '\\\\', '\')
            }
        }
    }
    return $attachments
}

function Test-DiskMediumPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $extension = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    return $extension -in @('.vdi', '.vmdk', '.vhd', '.vhdx', '.hdd', '.raw', '.img')
}

function Find-AhciController {
    param([Parameter(Mandatory = $true)][object[]]$Controllers)
    return $Controllers |
        Where-Object { $_.Type -match '^(?i:IntelAhci)$' } |
        Select-Object -First 1
}

function Find-IdeController {
    param([Parameter(Mandatory = $true)][object[]]$Controllers)
    return $Controllers |
        Where-Object { $_.Type -match '^(?i:PIIX3|PIIX4|ICH6)$' } |
        Select-Object -First 1
}

$Name = Resolve-VmName -RequestedName $Name
$info = Get-VmInfo -VmName $Name
$stateLine = $info | Where-Object { $_ -like 'VMState=*' } | Select-Object -First 1
if ($stateLine -match '^VMState="([^"]+)"$' -and $Matches[1] -notin @('poweroff', 'aborted')) {
    throw "VM '$Name' must be fully powered off before repair. Current state: $($Matches[1])."
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

if ([string]::IsNullOrWhiteSpace($SerialLog)) {
    $safeName = $Name -replace '[^A-Za-z0-9._-]', '_'
    $SerialLog = Join-Path ([System.IO.Path]::GetTempPath()) "KuroganeOS-$safeName-serial.log"
} else {
    $SerialLog = Get-KuroganeFileSystemPath -Path $SerialLog
}
$serialDirectory = [System.IO.Path]::GetDirectoryName($SerialLog)
if (-not [string]::IsNullOrWhiteSpace($serialDirectory)) {
    [System.IO.Directory]::CreateDirectory($serialDirectory) | Out-Null
}
if (Test-Path -LiteralPath $SerialLog -PathType Leaf) {
    Remove-Item -LiteralPath $SerialLog -Force
}

Write-Host "[virtualbox-repair] VM: $Name"
Write-Host "[virtualbox-repair] VBoxManage: $VBox"
Write-Host '[virtualbox-repair] enforcing KuroganeOS VirtualBox contract: EFI64 + AHCI HDD + IDE DVD'
if ($null -ne $IsoPath) { Write-Host "[virtualbox-repair] ISO: $IsoPath" }
Write-Host "[virtualbox-repair] serial log: $SerialLog"

$null = Invoke-VBoxChecked -Arguments @(
    'modifyvm', $Name,
    '--firmware', 'efi64', '--ioapic', 'on',
    '--boot1', 'dvd', '--boot2', 'disk', '--boot3', 'none', '--boot4', 'none',
    '--graphicscontroller', 'vmsvga', '--vram', '128'
) -FailureMessage 'Failed to switch the VM to the supported EFI64/DVD-first profile.'

$null = Invoke-VBoxChecked -Arguments @(
    'modifyvm', $Name, '--uart1', '0x3F8', '4'
) -FailureMessage 'Failed to enable COM1 serial diagnostics.'
$null = Invoke-VBoxChecked -Arguments @(
    'modifyvm', $Name, '--uartmode1', 'file', $SerialLog
) -FailureMessage 'Failed to route COM1 to the serial log file.'

$freshInfo = Get-VmInfo -VmName $Name
$controllers = @(Get-StorageControllers -Info $freshInfo)
$sataController = Find-AhciController -Controllers $controllers
if ($null -eq $sataController) {
    Write-Host '[virtualbox-repair] IntelAHCI controller absent; creating one'
    $null = Invoke-VBoxChecked -Arguments @(
        'storagectl', $Name, '--name', 'SATA', '--add', 'sata',
        '--controller', 'IntelAHCI', '--portcount', '1'
    ) -FailureMessage 'Could not create a SATA/IntelAHCI controller.'
    $freshInfo = Get-VmInfo -VmName $Name
    $controllers = @(Get-StorageControllers -Info $freshInfo)
    $sataController = Find-AhciController -Controllers $controllers
    if ($null -eq $sataController) {
        throw 'VirtualBox accepted SATA creation but no IntelAHCI controller is visible afterwards.'
    }
} else {
    Write-Host "[virtualbox-repair] existing IntelAHCI controller: $($sataController.Name)"
}
$sataName = [string]$sataController.Name

$sataAttachments = @(Get-ControllerAttachments -Info $freshInfo -ControllerName $sataName)
$sataDisk = $sataAttachments |
    Where-Object { $_.Medium -notin @('none', 'emptydrive') -and (Test-DiskMediumPath -Path $_.Medium) } |
    Select-Object -First 1

if ($null -eq $sataDisk) {
    $ideDisk = $null
    $ideDiskController = $null
    foreach ($controller in $controllers) {
        if ($controller.Type -notmatch '^(?i:PIIX3|PIIX4|ICH6)$') { continue }
        foreach ($attachment in @(Get-ControllerAttachments -Info $freshInfo -ControllerName $controller.Name)) {
            if ($attachment.Medium -notin @('none', 'emptydrive') -and
                (Test-DiskMediumPath -Path $attachment.Medium)) {
                $ideDisk = $attachment
                $ideDiskController = $controller
                break
            }
        }
        if ($null -ne $ideDisk) { break }
    }

    if ($null -eq $ideDisk) {
        throw "No virtual HDD was found on the existing IntelAHCI controller '$sataName' or on IDE. Attach/create a VDI/VMDK/VHD first."
    }

    Write-Host "[virtualbox-repair] moving HDD from '$($ideDiskController.Name)' $($ideDisk.Port):$($ideDisk.Device) to '$sataName' 0:0"
    $null = Invoke-VBoxChecked -Arguments @(
        'storageattach', $Name, '--storagectl', $ideDiskController.Name,
        '--port', ([string]$ideDisk.Port), '--device', ([string]$ideDisk.Device),
        '--type', 'hdd', '--medium', 'none'
    ) -FailureMessage 'Failed to detach the existing HDD from IDE before AHCI migration.'

    try {
        $null = Invoke-VBoxChecked -Arguments @(
            'storageattach', $Name, '--storagectl', $sataName,
            '--port', '0', '--device', '0', '--type', 'hdd', '--medium', $ideDisk.Medium
        ) -FailureMessage 'Failed to attach the existing HDD to the detected IntelAHCI controller.'
    } catch {
        Write-Warning 'AHCI migration failed; attempting rollback to the original IDE slot.'
        $null = Invoke-VBoxNative -Arguments @(
            'storageattach', $Name, '--storagectl', $ideDiskController.Name,
            '--port', ([string]$ideDisk.Port), '--device', ([string]$ideDisk.Device),
            '--type', 'hdd', '--medium', $ideDisk.Medium
        ) -AllowFailure
        throw
    }
}

$freshInfo = Get-VmInfo -VmName $Name
$controllers = @(Get-StorageControllers -Info $freshInfo)
$ideController = Find-IdeController -Controllers $controllers
if ($null -eq $ideController) {
    Write-Host '[virtualbox-repair] IDE controller absent; creating PIIX4 controller for optical media'
    $null = Invoke-VBoxChecked -Arguments @(
        'storagectl', $Name, '--name', 'IDE', '--add', 'ide', '--controller', 'PIIX4'
    ) -FailureMessage 'Could not create a PIIX4 IDE controller.'
    $freshInfo = Get-VmInfo -VmName $Name
    $controllers = @(Get-StorageControllers -Info $freshInfo)
    $ideController = Find-IdeController -Controllers $controllers
    if ($null -eq $ideController) {
        throw 'VirtualBox accepted IDE creation but no IDE controller is visible afterwards.'
    }
} else {
    Write-Host "[virtualbox-repair] existing IDE controller: $($ideController.Name)"
}
$ideName = [string]$ideController.Name

if ($null -ne $IsoPath) {
    $freshInfo = Get-VmInfo -VmName $Name
    $attachments = @(Get-ControllerAttachments -Info $freshInfo -ControllerName $ideName)
    $slotCandidates = @(
        @{ Port = 0; Device = 0 },
        @{ Port = 1; Device = 0 },
        @{ Port = 0; Device = 1 },
        @{ Port = 1; Device = 1 }
    )

    $selectedSlot = $null
    foreach ($slot in $slotCandidates) {
        $existing = $attachments | Where-Object {
            $_.Port -eq $slot.Port -and $_.Device -eq $slot.Device
        } | Select-Object -First 1
        if ($null -eq $existing -or
            $existing.Medium -eq 'emptydrive' -or
            $existing.Medium -eq 'none' -or
            [System.IO.Path]::GetExtension($existing.Medium).ToLowerInvariant() -eq '.iso') {
            $selectedSlot = $slot
            break
        }
    }

    if ($null -eq $selectedSlot) {
        throw "No optical slot is available on IDE controller '$ideName'. Existing HDD attachments were left untouched."
    }

    $null = Invoke-VBoxChecked -Arguments @(
        'storageattach', $Name, '--storagectl', $ideName,
        '--port', ([string]$selectedSlot.Port), '--device', ([string]$selectedSlot.Device),
        '--type', 'dvddrive', '--medium', $IsoPath
    ) -FailureMessage "Failed to attach the ISO to '$ideName' $($selectedSlot.Port):$($selectedSlot.Device)."
    Write-Host "[virtualbox-repair] ISO attached: '$ideName' $($selectedSlot.Port):$($selectedSlot.Device) -> $IsoPath"
}

$finalInfo = Get-VmInfo -VmName $Name
$finalControllers = @(Get-StorageControllers -Info $finalInfo)
$finalSata = Find-AhciController -Controllers $finalControllers
$finalIde = Find-IdeController -Controllers $finalControllers
$firmware = $finalInfo | Where-Object { $_ -like 'firmware=*' } | Select-Object -First 1
$boot1 = $finalInfo | Where-Object { $_ -like 'boot1=*' } | Select-Object -First 1
$boot2 = $finalInfo | Where-Object { $_ -like 'boot2=*' } | Select-Object -First 1
$graphics = $finalInfo | Where-Object { $_ -like 'graphicscontroller=*' } | Select-Object -First 1

if ($firmware -ne 'firmware="EFI64"' -or $boot1 -ne 'boot1="dvd"' -or $boot2 -ne 'boot2="disk"') {
    throw 'Final VirtualBox firmware/boot-order verification failed.'
}
if ($null -eq $finalSata) {
    throw 'Final VirtualBox storage verification failed: IntelAHCI controller is missing.'
}
$finalSataDisks = @(Get-ControllerAttachments -Info $finalInfo -ControllerName $finalSata.Name) |
    Where-Object { $_.Medium -notin @('none', 'emptydrive') -and (Test-DiskMediumPath -Path $_.Medium) }
if ($finalSataDisks.Count -eq 0) {
    throw "Final VirtualBox storage verification failed: no HDD is attached to IntelAHCI controller '$($finalSata.Name)'."
}
if ($null -ne $IsoPath) {
    if ($null -eq $finalIde) {
        throw 'Final VirtualBox optical verification failed: IDE controller is missing.'
    }
    $finalOptical = @(Get-ControllerAttachments -Info $finalInfo -ControllerName $finalIde.Name) |
        Where-Object { [System.IO.Path]::GetExtension($_.Medium).ToLowerInvariant() -eq '.iso' }
    if ($finalOptical.Count -eq 0) {
        throw "Final VirtualBox optical verification failed: no ISO is attached to IDE controller '$($finalIde.Name)'."
    }
}

Write-Host "[virtualbox-repair] $firmware"
Write-Host "[virtualbox-repair] $boot1"
Write-Host "[virtualbox-repair] $boot2"
Write-Host "[virtualbox-repair] $graphics"
Write-Host "[virtualbox-repair] IntelAHCI controller: $($finalSata.Name)"
Write-Host '[virtualbox-repair] SATA/IntelAHCI HDD: PASS'
if ($null -ne $IsoPath) {
    Write-Host "[virtualbox-repair] IDE controller: $($finalIde.Name)"
    Write-Host '[virtualbox-repair] IDE optical ISO: PASS'
}
Write-Host "[virtualbox-repair] COM1 serial diagnostics: $SerialLog"
Write-Host '[virtualbox-repair] PASS: supported KuroganeOS VirtualBox boot/install profile applied.'
Write-Host "[virtualbox-repair] Start with: & `"$VBox`" startvm `"$Name`" --type gui"
