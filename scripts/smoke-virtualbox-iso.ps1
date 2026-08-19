[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Iso,
    [ValidateRange(15, 180)]
    [int]$TimeoutSeconds = 90
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

$Iso = Resolve-KuroganePath -Path $Iso
if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "ISO not found: $Iso"
}
if ([System.IO.Path]::GetExtension($Iso) -ne '.iso') {
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
        --graphicscontroller vboxsvga --vram 64 `
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

    # Installer contract: blank target disk must be exposed through a real
    # VirtualBox Intel AHCI controller. Keep the controller to exactly one
    # implemented SATA port. VBoxManage otherwise defaults to a much larger
    # port count; KuroganeOS deliberately polls implemented AHCI ports after
    # reset and empty VBox ports can turn a boot smoke into a long false
    # timeout. IDE remains reserved for optical media.
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

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $booted = $false
    $storageReady = $false
    $storageProof = $null
    $fatal = $null
    do {
        Start-Sleep -Milliseconds 500
        if (Test-Path -LiteralPath $serial -PathType Leaf) {
            $text = Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue
            if ($null -ne $text) {
                if ($text -match '\[FATAL\]\[INSTALL\].*') {
                    $fatal = $Matches[0]
                    break
                }
                if ($text -match '\[TEST\] ALL_REQUIRED_TESTS_PASSED: FAIL') {
                    $fatal = '[TEST] ALL_REQUIRED_TESTS_PASSED: FAIL'
                    break
                }
                if ($text -match 'KuroganeOS kernel entry') {
                    $booted = $true
                }

                # Newer installer kernels expose explicit AHCI counters. Keep
                # this as the preferred non-destructive proof when available.
                if ($text -match '\[INFO\]\[AHCI\].*active AHCI controllers=([1-9][0-9]*)') {
                    $storageReady = $true
                    $storageProof = 'explicit kernel AHCI discovery'
                }

                # Older/current installer-mode images initialize AHCI before
                # entering Red Flux Setup but do not print initialize_storage_probe()
                # counters. Reaching stage 1 after confirmation is stronger
                # runtime evidence than a counter: the installer selected the
                # SATA block device and successfully wrote the primary/backup
                # GPT to the temporary VDI through the AHCI block interface.
                if ($text -match '\[TEST\] installer_confirmation: PASS' -and
                    $text -match 'installer stage 1/9: target confirmed and GPT written') {
                    $storageReady = $true
                    $storageProof = 'installer GPT write through SATA/AHCI'
                }

                if ($booted -and $storageReady) {
                    break
                }
            }
        }
    } while ([DateTime]::UtcNow -lt $deadline)

    if ($null -ne $fatal) {
        throw "VirtualBox ISO reached the kernel but failed installer qualification: $fatal"
    }
    if (-not $booted -or -not $storageReady) {
        $tail = ''
        if (Test-Path -LiteralPath $serial -PathType Leaf) {
            $tail = (Get-Content -LiteralPath $serial -Tail 120) -join [Environment]::NewLine
        }
        throw "VirtualBox EFI64 optical boot did not reach kernel + operational SATA/AHCI proof within $TimeoutSeconds seconds.`n$tail"
    }

    Write-Host "[virtualbox-smoke] ISO: $Iso"
    Write-Host '[virtualbox-smoke] firmware EFI64: PASS'
    Write-Host '[virtualbox-smoke] DVD-first optical attachment: PASS'
    Write-Host '[virtualbox-smoke] Intel AHCI target disk configuration: PASS'
    Write-Host '[virtualbox-smoke] BOOTX64.EFI -> kernel: PASS'
    Write-Host "[virtualbox-smoke] SATA/AHCI runtime proof: PASS ($storageProof)"
    Write-Host '[virtualbox-smoke] REAL ORACLE VIRTUALBOX BOOT/STORAGE: PASS'
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
