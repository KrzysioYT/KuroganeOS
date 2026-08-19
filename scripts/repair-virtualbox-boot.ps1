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
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $VBox = $candidate
    } else {
        throw 'VBoxManage.exe was not found. Install Oracle VirtualBox first.'
    }
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

function Get-RegisteredVmNames {
    $result = Invoke-VBoxNative -Arguments @('list', 'vms') -AllowFailure
    if ($result.ExitCode -ne 0) {
        return @()
    }
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
    if ($probe.ExitCode -eq 0) {
        return $RequestedName
    }

    $registered = @(Get-RegisteredVmNames)
    $caseInsensitive = @($registered | Where-Object {
        $_.Equals($RequestedName, [System.StringComparison]::OrdinalIgnoreCase)
    })
    if ($caseInsensitive.Count -eq 1) {
        Write-Host "[virtualbox-repair] resolved VM name '$RequestedName' -> '$($caseInsensitive[0])'"
        return $caseInsensitive[0]
    }

    $prefixMatches = @($registered | Where-Object {
        $_.StartsWith($RequestedName, [System.StringComparison]::OrdinalIgnoreCase)
    })
    if ($prefixMatches.Count -eq 1) {
        Write-Host "[virtualbox-repair] resolved VM prefix '$RequestedName' -> '$($prefixMatches[0])'"
        return $prefixMatches[0]
    }

    $nativeError = @($probe.StdOut, $probe.StdErr) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_.TrimEnd() }
    $available = if ($registered.Count -eq 0) {
        '<none reported by VBoxManage list vms>'
    } else {
        ($registered | ForEach-Object { "  - $_" }) -join [Environment]::NewLine
    }
    if ($prefixMatches.Count -gt 1) {
        $matches = ($prefixMatches | ForEach-Object { "  - $_" }) -join [Environment]::NewLine
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
    if ([string]::IsNullOrWhiteSpace($result.StdOut)) {
        return @()
    }
    return @($result.StdOut -split "`r?`n" | Where-Object { $_ -ne '' })
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

$Name = Resolve-VmName -RequestedName $Name
$info = Get-VmInfo -VmName $Name
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
if ($null -ne $IsoPath) {
    Write-Host "[virtualbox-repair] ISO: $IsoPath"
}
Write-Host "[virtualbox-repair] serial log: $SerialLog"

$null = Invoke-VBoxChecked -Arguments @(
    'modifyvm', $Name,
    '--firmware', 'efi64',
    '--ioapic', 'on',
    '--boot1', 'dvd',
    '--boot2', 'disk',
    '--boot3', 'none',
    '--boot4', 'none',
    '--graphicscontroller', 'vmsvga',
    '--vram', '128'
) -FailureMessage 'Failed to switch the VM to the supported EFI64/DVD-first profile.'

$null = Invoke-VBoxChecked -Arguments @(
    'modifyvm', $Name, '--uart1', '0x3F8', '4'
) -FailureMessage 'Failed to enable COM1 serial diagnostics.'
$null = Invoke-VBoxChecked -Arguments @(
    'modifyvm', $Name, '--uartmode1', 'file', $SerialLog
) -FailureMessage 'Failed to route COM1 to the serial log file.'

$freshInfo = Get-VmInfo -VmName $Name
$sataController = $freshInfo |
    Where-Object { $_ -match '^storagecontrollername\d+="SATA"$' } |
    Select-Object -First 1
if ($null -eq $sataController) {
    Write-Host '[virtualbox-repair] SATA controller absent; creating IntelAHCI controller'
    $null = Invoke-VBoxChecked -Arguments @(
        'storagectl', $Name,
        '--name', 'SATA',
        '--add', 'sata',
        '--controller', 'IntelAHCI'
    ) -FailureMessage "Could not create the SATA/IntelAHCI controller named 'SATA'."
}

$freshInfo = Get-VmInfo -VmName $Name
$sataDiskLine = $freshInfo |
    Where-Object { $_ -match '^"SATA-\d+-\d+"=".+"$' -and $_ -notmatch '="(none|emptydrive)"$' } |
    Select-Object -First 1

if ($null -eq $sataDiskLine) {
    $ideDisk = $null
    foreach ($line in $freshInfo) {
        if ($line -match '^"IDE-(\d+)-(\d+)"="(.+)"$') {
            $port = [int]$Matches[1]
            $device = [int]$Matches[2]
            $mediumPath = Decode-VBoxMediumPath -Value $Matches[3]
            if (Test-DiskMediumPath -Path $mediumPath) {
                $ideDisk = @{
                    Port = $port
                    Device = $device
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
    $null = Invoke-VBoxChecked -Arguments @(
        'storageattach', $Name,
        '--storagectl', 'IDE',
        '--port', "$($ideDisk.Port)",
        '--device', "$($ideDisk.Device)",
        '--type', 'hdd',
        '--medium', 'none'
    ) -FailureMessage 'Failed to detach the existing HDD from IDE before AHCI migration.'

    try {
        $null = Invoke-VBoxChecked -Arguments @(
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
            $null = Invoke-VBoxChecked -Arguments @(
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

$freshInfo = Get-VmInfo -VmName $Name
$ideController = $freshInfo |
    Where-Object { $_ -match '^storagecontrollername\d+="IDE"$' } |
    Select-Object -First 1
if ($null -eq $ideController) {
    Write-Host '[virtualbox-repair] IDE controller absent; creating PIIX4 IDE controller for optical media'
    $null = Invoke-VBoxChecked -Arguments @(
        'storagectl', $Name,
        '--name', 'IDE',
        '--add', 'ide',
        '--controller', 'PIIX4'
    ) -FailureMessage "Could not create the PIIX4 IDE controller named 'IDE'."
}

if ($null -ne $IsoPath) {
    $freshInfo = Get-VmInfo -VmName $Name
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

    $null = Invoke-VBoxChecked -Arguments @(
        'storageattach', $Name,
        '--storagectl', 'IDE',
        '--port', "$($selectedSlot.Port)",
        '--device', "$($selectedSlot.Device)",
        '--type', 'dvddrive',
        '--medium', $IsoPath
    ) -FailureMessage "Failed to attach the ISO to IDE $($selectedSlot.Port):$($selectedSlot.Device)."
    Write-Host "[virtualbox-repair] ISO attached: IDE $($selectedSlot.Port):$($selectedSlot.Device) -> $IsoPath"
}

$finalInfo = Get-VmInfo -VmName $Name
$finalText = $finalInfo -join "`n"
$firmware = $finalInfo | Where-Object { $_ -like 'firmware=*' } | Select-Object -First 1
$boot1 = $finalInfo | Where-Object { $_ -like 'boot1=*' } | Select-Object -First 1
$boot2 = $finalInfo | Where-Object { $_ -like 'boot2=*' } | Select-Object -First 1
$graphics = $finalInfo | Where-Object { $_ -like 'graphicscontroller=*' } | Select-Object -First 1
$ahciPresent = $finalText -match 'storagecontrollertype\d+="IntelAhci"|storagecontrollertype\d+="IntelAHCI"'
$sataDiskPresent = @($finalInfo | Where-Object {
    $_ -match '^"SATA-\d+-\d+"=".+"$' -and
    $_ -notmatch '="(none|emptydrive)"$'
}).Count -gt 0

if ($firmware -ne 'firmware="EFI64"' -or
    $boot1 -ne 'boot1="dvd"' -or
    $boot2 -ne 'boot2="disk"') {
    throw 'Final VirtualBox firmware/boot-order verification failed.'
}
if (-not $ahciPresent -or -not $sataDiskPresent) {
    throw 'Final VirtualBox storage verification failed: KuroganeOS installer requires a HDD attached through SATA/IntelAHCI.'
}
if ($null -ne $IsoPath -and $finalText -notmatch [regex]::Escape([System.IO.Path]::GetFileName($IsoPath))) {
    throw 'Final VirtualBox optical verification failed: the requested ISO is not attached.'
}

Write-Host "[virtualbox-repair] $firmware"
Write-Host "[virtualbox-repair] $boot1"
Write-Host "[virtualbox-repair] $boot2"
Write-Host "[virtualbox-repair] $graphics"
Write-Host '[virtualbox-repair] SATA/IntelAHCI HDD: PASS'
if ($null -ne $IsoPath) {
    Write-Host '[virtualbox-repair] IDE optical ISO: PASS'
}
Write-Host "[virtualbox-repair] COM1 serial diagnostics: $SerialLog"
Write-Host '[virtualbox-repair] PASS: supported KuroganeOS VirtualBox boot/install profile applied.'
Write-Host "[virtualbox-repair] Start with: & `"$VBox`" startvm `"$Name`" --type gui"
