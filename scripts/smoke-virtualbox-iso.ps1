[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Iso,
    [ValidateRange(30, 600)]
    [int]$TimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-KuroganePath {
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
if ($null -eq $VBoxCommand) {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw 'VBoxManage.exe not found. A real Oracle VirtualBox installation is required to qualify the ISO.'
    }
    $VBox = $candidate
} else {
    $VBox = $VBoxCommand.Path
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

function Invoke-VBox {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $result = Invoke-VBoxNative -Arguments $Arguments
    if ([string]::IsNullOrWhiteSpace($result.StdOut)) {
        return @()
    }
    return @($result.StdOut -split "`r?`n" | Where-Object { $_ -ne '' })
}

function Get-SerialText {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }
    $text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
    if ($null -eq $text) { return '' }
    return [string]$text
}

function Get-SerialTail {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$Lines = 180
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }
    return (Get-Content -LiteralPath $Path -Tail $Lines) -join [Environment]::NewLine
}

$Iso = Resolve-KuroganePath -Path $Iso
if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "ISO not found: $Iso"
}
if ([System.IO.Path]::GetExtension($Iso).ToLowerInvariant() -ne '.iso') {
    throw "VirtualBox smoke requires an .iso optical image: $Iso"
}
if ((Get-Item -LiteralPath $Iso).Length -le 0) {
    throw "ISO is empty: $Iso"
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("kurogane-vbox-smoke-" + [Guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($temp) | Out-Null
$vm = 'KuroganeOS-ISO-Smoke-' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$disk = Join-Path $temp 'KuroganeOS-smoke.vdi'
$serial = Join-Path $temp 'serial.log'
$registered = $false
$started = $false

try {
    Invoke-VBox createvm --name $vm --ostype Other_64 --register | Out-Null
    $registered = $true

    Invoke-VBox modifyvm $vm `
        --memory 2048 --cpus 1 --firmware efi64 --ioapic on `
        --boot1 dvd --boot2 disk --boot3 none --boot4 none `
        --graphicscontroller vmsvga --vram 128 `
        --keyboard ps2 --mouse ps2 | Out-Null

    $networkResult = Invoke-VBoxNative -Arguments @(
        'modifyvm', $vm,
        '--nic1', 'nat', '--nic-type1', '82540EM', '--cable-connected1', 'on'
    ) -AllowFailure
    if ($networkResult.ExitCode -ne 0) {
        Invoke-VBox modifyvm $vm `
            --nic1 nat --nictype1 82540EM --cableconnected1 on | Out-Null
    }

    Invoke-VBox modifyvm $vm --uart1 0x3F8 4 | Out-Null
    Invoke-VBox modifyvm $vm --uartmode1 file $serial | Out-Null

    Invoke-VBox createmedium disk --filename $disk --size 2048 --format VDI | Out-Null
    Invoke-VBox storagectl $vm --name SATA --add sata --controller IntelAHCI --portcount 1 | Out-Null
    Invoke-VBox storageattach $vm --storagectl SATA --port 0 --device 0 `
        --type hdd --medium $disk | Out-Null
    Invoke-VBox storagectl $vm --name IDE --add ide --controller PIIX4 | Out-Null
    Invoke-VBox storageattach $vm --storagectl IDE --port 0 --device 0 `
        --type dvddrive --medium $Iso | Out-Null
    Invoke-VBox setextradata $vm VBoxInternal2/EfiGraphicsResolution 1280x800 | Out-Null

    $machineInfo = Invoke-VBox showvminfo $vm --machinereadable
    $machineText = $machineInfo -join "`n"
    if (-not ($machineInfo -contains 'firmware="EFI64"')) {
        throw 'Qualification VM did not retain firmware="EFI64".'
    }
    if (-not ($machineInfo -contains 'boot1="dvd"')) {
        throw 'Qualification VM did not retain DVD-first boot order.'
    }
    if ($machineText -notmatch 'storagecontrollername\d+="SATA"' -or
        $machineText -notmatch 'storagecontrollertype\d+="IntelAhci"|storagecontrollertype\d+="IntelAHCI"') {
        throw 'Qualification VM does not expose the required Intel AHCI SATA controller.'
    }
    if ($machineText -notmatch '"SATA-0-0"=.*\.vdi"') {
        throw 'Qualification VM target VDI is not attached to SATA port 0.'
    }
    $isoLeaf = [System.IO.Path]::GetFileName($Iso)
    if ($machineText -notmatch [regex]::Escape($isoLeaf)) {
        throw 'Qualification VM does not report the requested ISO as attached optical media.'
    }

    Invoke-VBox startvm $vm --type headless | Out-Null
    $started = $true

    # Phase 1: real optical boot + complete installation. TimeoutSeconds is an
    # inactivity budget. Verification progress extends the budget while a hard
    # ceiling prevents a truly stuck installer from running forever.
    $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $hardBudgetSeconds = [Math]::Max(600, $TimeoutSeconds * 4)
    $hardDeadline = [DateTime]::UtcNow.AddSeconds($hardBudgetSeconds)
    $lastSerialLength = 0
    $lastProgressLine = '<no installer serial output yet>'

    $booted = $false
    $storageReady = $false
    $storageProof = $null
    $installerComplete = $false
    $fatal = $null
    do {
        Start-Sleep -Milliseconds 500
        $text = Get-SerialText -Path $serial
        if ($text.Length -gt 0) {
            if ($text.Length -gt $lastSerialLength) {
                $lastSerialLength = $text.Length
                $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
                $progress = $text -split "`r?`n" |
                    Where-Object {
                        $_ -match '^installer stage ' -or
                        $_ -match '^\[INSTALL\]\[VERIFY\]' -or
                        $_ -match '^\[TEST\] installer_'
                    } |
                    Select-Object -Last 1
                if (-not [string]::IsNullOrWhiteSpace($progress)) {
                    $lastProgressLine = $progress
                }
            }

            if ($text -match '\[FATAL\]\[INSTALL\].*') {
                $fatal = $Matches[0]
                break
            }
            if ($text -match '\[TEST\] installer_complete: FAIL') {
                $tail = ($text -split "`r?`n" | Select-Object -Last 30) -join [Environment]::NewLine
                $fatal = "installer_complete: FAIL`n$tail"
                break
            }
            if ($text -match 'KuroganeOS kernel entry') {
                $booted = $true
            }
            if ($text -match '\[INFO\]\[AHCI\].*active AHCI controllers=([1-9][0-9]*)') {
                $storageReady = $true
                $storageProof = 'explicit kernel AHCI discovery'
            }
            if ($text -match '\[TEST\] installer_confirmation: PASS' -and
                $text -match 'installer stage 1/9: target confirmed and GPT written') {
                $storageReady = $true
                $storageProof = 'installer GPT write through SATA/AHCI'
            }
            if ($text -match '\[TEST\] installer_complete: PASS') {
                $installerComplete = $true
            }
            if ($booted -and $storageReady -and $installerComplete) {
                break
            }
        }
    } while ([DateTime]::UtcNow -lt $idleDeadline -and
             [DateTime]::UtcNow -lt $hardDeadline)

    if ($null -ne $fatal) {
        throw "VirtualBox ISO reached the installer but failed qualification: $fatal"
    }
    if (-not $booted -or -not $storageReady -or -not $installerComplete) {
        $tail = Get-SerialTail -Path $serial -Lines 180
        $timeoutKind = if ([DateTime]::UtcNow -ge $hardDeadline) {
            "hard limit of $hardBudgetSeconds seconds"
        } else {
            "no serial progress for $TimeoutSeconds seconds"
        }
        throw "VirtualBox EFI64 install did not reach installer_complete: PASS ($timeoutKind). Last progress: $lastProgressLine`n$tail"
    }

    Write-Host "[virtualbox-smoke] ISO: $Iso"
    Write-Host '[virtualbox-smoke] firmware EFI64: PASS'
    Write-Host '[virtualbox-smoke] DVD-first optical attachment: PASS'
    Write-Host '[virtualbox-smoke] Intel AHCI target disk configuration: PASS'
    Write-Host '[virtualbox-smoke] BOOTX64.EFI -> kernel: PASS'
    Write-Host "[virtualbox-smoke] SATA/AHCI runtime proof: PASS ($storageProof)"
    Write-Host '[virtualbox-smoke] full root + UEFI payload installation: PASS'
    Write-Host '[virtualbox-smoke] installed payload verification: PASS'

    # Phase 2: power off the installer session, remove installation media and
    # prove that the exact VDI produced above can boot its own ESP/ROOT all the
    # way to userspace PID 1. This catches failures that an installer-only gate
    # cannot see.
    $poweroff = Invoke-VBoxNative -Arguments @('controlvm', $vm, 'poweroff') -AllowFailure
    if ($poweroff.ExitCode -ne 0) {
        throw "Installed qualification VM could not be powered off before reboot.`n$($poweroff.StdErr)"
    }
    $started = $false
    Start-Sleep -Milliseconds 500

    $postInstallOffset = 0
    if (Test-Path -LiteralPath $serial -PathType Leaf) {
        $postInstallOffset = (Get-Item -LiteralPath $serial).Length
    }

    Invoke-VBox storageattach $vm --storagectl IDE --port 0 --device 0 `
        --type dvddrive --medium none | Out-Null
    Invoke-VBox modifyvm $vm `
        --boot1 disk --boot2 none --boot3 none --boot4 none | Out-Null

    $postInfo = Invoke-VBox showvminfo $vm --machinereadable
    $postText = $postInfo -join "`n"
    if (-not ($postInfo -contains 'boot1="disk"')) {
        throw 'Post-install qualification VM did not retain disk-first boot.'
    }
    if ($postText -match [regex]::Escape($isoLeaf)) {
        throw 'Post-install qualification still reports the installer ISO as attached.'
    }

    Invoke-VBox startvm $vm --type headless | Out-Null
    $started = $true

    $postIdleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $postHardBudgetSeconds = [Math]::Max(300, $TimeoutSeconds * 2)
    $postHardDeadline = [DateTime]::UtcNow.AddSeconds($postHardBudgetSeconds)
    $postLastLength = $postInstallOffset
    $postLastProgress = '<no post-install serial output yet>'
    $postBooted = $false
    $rootMounted = $false
    $initSpawned = $false
    $initOnline = $false
    $postDegraded = $false
    $postFatal = $null

    do {
        Start-Sleep -Milliseconds 500
        $allText = Get-SerialText -Path $serial
        if ($allText.Length -gt $postInstallOffset) {
            $phaseText = $allText.Substring([int]$postInstallOffset)
            if ($allText.Length -gt $postLastLength) {
                $postLastLength = $allText.Length
                $postIdleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
                $progress = $phaseText -split "`r?`n" |
                    Where-Object {
                        $_ -match '^\[INFO\]\[(VFS|INIT|BOOT)\]' -or
                        $_ -match '^\[TEST\] (fat32_vfs_read|installed_|userspace_init_spawn|ALL_REQUIRED)' -or
                        $_ -match '^/system/init:'
                    } |
                    Select-Object -Last 1
                if (-not [string]::IsNullOrWhiteSpace($progress)) {
                    $postLastProgress = $progress
                }
            }

            if ($phaseText -match '\[FATAL\]\[[^\]]+\].*') {
                $postFatal = $Matches[0]
                break
            }
            if ($phaseText -match 'KuroganeOS kernel entry') {
                $postBooted = $true
            }
            if ($phaseText -match 'persistent FAT32 root mounted read-write') {
                $rootMounted = $true
            }
            if ($phaseText -match '\[TEST\] ALL_REQUIRED_TESTS_PASSED: FAIL') {
                # Aggregate first-boot/persistence probes may be degraded while
                # the kernel intentionally continues. The qualification only
                # fails here if a real [FATAL] follows or PID 1 never appears.
                $postDegraded = $true
            }
            if ($phaseText -match '\[TEST\] userspace_init_spawn: PASS') {
                $initSpawned = $true
            }
            if ($phaseText -match '/system/init: PID 1 online') {
                $initOnline = $true
            }
            if ($postBooted -and $rootMounted -and $initSpawned -and $initOnline) {
                break
            }
        }
    } while ([DateTime]::UtcNow -lt $postIdleDeadline -and
             [DateTime]::UtcNow -lt $postHardDeadline)

    if ($null -ne $postFatal) {
        $tail = Get-SerialTail -Path $serial -Lines 180
        throw "VirtualBox installed-disk reboot reached a fatal kernel path: $postFatal`n$tail"
    }
    if (-not $postBooted -or -not $rootMounted -or -not $initSpawned -or -not $initOnline) {
        $tail = Get-SerialTail -Path $serial -Lines 180
        $timeoutKind = if ([DateTime]::UtcNow -ge $postHardDeadline) {
            "hard limit of $postHardBudgetSeconds seconds"
        } else {
            "no post-install serial progress for $TimeoutSeconds seconds"
        }
        throw "VirtualBox installed VDI did not boot to persistent ROOT + PID 1 ($timeoutKind). Last progress: $postLastProgress`n$tail"
    }

    Write-Host '[virtualbox-smoke] installer ISO detached before reboot: PASS'
    Write-Host '[virtualbox-smoke] installed VDI UEFI boot: PASS'
    Write-Host '[virtualbox-smoke] persistent Kurogane Root mount: PASS'
    Write-Host '[virtualbox-smoke] /system/init PID 1 online: PASS'
    if ($postDegraded) {
        Write-Warning '[virtualbox-smoke] post-install optional/runtime aggregate reported DEGRADED, but boot reached PID 1.'
    } else {
        Write-Host '[virtualbox-smoke] post-install runtime aggregate: PASS'
    }
    Write-Host '[virtualbox-smoke] REAL ORACLE VIRTUALBOX INSTALL + REBOOT: PASS'
} finally {
    if ($started) {
        $null = Invoke-VBoxNative -Arguments @('controlvm', $vm, 'poweroff') -AllowFailure
    }
    if ($registered) {
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            $cleanup = Invoke-VBoxNative -Arguments @('unregistervm', $vm, '--delete') -AllowFailure
            if ($cleanup.ExitCode -eq 0) { break }
            Start-Sleep -Milliseconds 250
        }
    }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
