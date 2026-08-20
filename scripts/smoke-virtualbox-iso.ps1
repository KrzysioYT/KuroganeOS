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
if ($null -ne $VBoxCommand) {
    $VBox = $VBoxCommand.Path
} else {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw 'VBoxManage.exe not found. A real Oracle VirtualBox installation is required to qualify the ISO.'
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

    $stdout = ''
    $stderr = ''
    $exitCode = -1
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

function Get-SerialText {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $text = Get-Content -LiteralPath $Path -Raw -ErrorAction SilentlyContinue
    if ($null -eq $text) { return '' }
    return [string]$text
}

function Get-SerialTail {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [int]$Lines = 180
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    return (Get-Content -LiteralPath $Path -Tail $Lines) -join [Environment]::NewLine
}

function Send-ScanCodes {
    param(
        [Parameter(Mandatory = $true)][string]$VmName,
        [Parameter(Mandatory = $true)][string[]]$Codes
    )
    $arguments = @('controlvm', $VmName, 'keyboardputscancode') + $Codes
    Invoke-VBoxNative -Arguments $arguments | Out-Null
}

function Send-Enter {
    param([Parameter(Mandatory = $true)][string]$VmName)
    Send-ScanCodes -VmName $VmName -Codes @('1c', '9c')
}

function Send-Tab {
    param([Parameter(Mandatory = $true)][string]$VmName)
    Send-ScanCodes -VmName $VmName -Codes @('0f', '8f')
}

function Send-InstallLiteral {
    param([Parameter(Mandatory = $true)][string]$VmName)

    # Set-1 PS/2 make/break pairs for: i n s t a l l.
    # The installer normalizes characters to uppercase before comparison, so
    # Shift is deliberately not involved.  This avoids VirtualBox host-layout
    # translation and keyboardputstring differences between releases.
    Send-ScanCodes -VmName $VmName -Codes @(
        '17', '97',
        '31', 'b1',
        '1f', '9f',
        '14', '94',
        '1e', '9e',
        '26', 'a6',
        '26', 'a6'
    )
}

function Drive-InstallerUi {
    param([Parameter(Mandatory = $true)][string]$VmName)

    # The installer uses deterministic initial selections:
    #   welcome: TRY (0), INSTALL (1)
    #   language: ENGLISH (0)
    #   username: pre-filled "user"
    #   password: NO PASSWORD (0)
    #   disk: first AHCI device (0)
    #
    # Tab is intentionally used for the welcome screen because choose_two()
    # explicitly handles it and it is a plain Set-1 key.  The previous use of
    # an E0-prefixed Down Arrow was unreliable with VirtualBox injected PS/2
    # input on Windows.
    Start-Sleep -Milliseconds 900
    Send-Tab -VmName $VmName
    Start-Sleep -Milliseconds 200
    Send-Enter -VmName $VmName

    Start-Sleep -Milliseconds 900
    Send-Enter -VmName $VmName       # English
    Start-Sleep -Milliseconds 900
    Send-Enter -VmName $VmName       # pre-filled user
    Start-Sleep -Milliseconds 900
    Send-Enter -VmName $VmName       # no password
    Start-Sleep -Milliseconds 900
    Send-Enter -VmName $VmName       # first disk
    Start-Sleep -Milliseconds 900
    Send-InstallLiteral -VmName $VmName
    Start-Sleep -Milliseconds 200
    Send-Enter -VmName $VmName
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
    ('kurogane-vbox-smoke-' + [Guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($temp) | Out-Null
$vm = 'KuroganeOS-ISO-Smoke-' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$disk = Join-Path $temp 'KuroganeOS-smoke.vdi'
$installSerial = Join-Path $temp 'install-serial.log'
$postSerial = Join-Path $temp 'postinstall-serial.log'
$registered = $false
$started = $false

try {
    Invoke-VBoxNative -Arguments @('createvm', '--name', $vm, '--ostype', 'Other_64', '--register') | Out-Null
    $registered = $true

    Invoke-VBoxNative -Arguments @(
        'modifyvm', $vm,
        '--memory', '2048', '--cpus', '1', '--firmware', 'efi64', '--ioapic', 'on',
        '--boot1', 'dvd', '--boot2', 'disk', '--boot3', 'none', '--boot4', 'none',
        '--graphicscontroller', 'vmsvga', '--vram', '128', '--keyboard', 'ps2', '--mouse', 'ps2'
    ) | Out-Null

    $networkResult = Invoke-VBoxNative -Arguments @(
        'modifyvm', $vm, '--nic1', 'nat', '--nic-type1', 'Am79C973', '--cable-connected1', 'on'
    ) -AllowFailure
    if ($networkResult.ExitCode -ne 0) {
        Invoke-VBoxNative -Arguments @(
            'modifyvm', $vm, '--nic1', 'nat', '--nictype1', 'Am79C973', '--cableconnected1', 'on'
        ) | Out-Null
    }

    Invoke-VBoxNative -Arguments @('modifyvm', $vm, '--uart1', '0x3F8', '4') | Out-Null
    Invoke-VBoxNative -Arguments @('modifyvm', $vm, '--uartmode1', 'file', $installSerial) | Out-Null
    Invoke-VBoxNative -Arguments @('createmedium', 'disk', '--filename', $disk, '--size', '2048', '--format', 'VDI') | Out-Null
    Invoke-VBoxNative -Arguments @('storagectl', $vm, '--name', 'SATA', '--add', 'sata', '--controller', 'IntelAHCI', '--portcount', '1') | Out-Null
    Invoke-VBoxNative -Arguments @('storageattach', $vm, '--storagectl', 'SATA', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', $disk) | Out-Null
    Invoke-VBoxNative -Arguments @('storagectl', $vm, '--name', 'IDE', '--add', 'ide', '--controller', 'PIIX4') | Out-Null
    Invoke-VBoxNative -Arguments @('storageattach', $vm, '--storagectl', 'IDE', '--port', '0', '--device', '0', '--type', 'dvddrive', '--medium', $Iso) | Out-Null
    Invoke-VBoxNative -Arguments @('setextradata', $vm, 'VBoxInternal2/EfiGraphicsResolution', '1280x800') | Out-Null

    $machineResult = Invoke-VBoxNative -Arguments @('showvminfo', $vm, '--machinereadable')
    $machineText = $machineResult.StdOut
    if ($machineText -notmatch 'firmware="EFI64"') { throw 'Qualification VM did not retain EFI64 firmware.' }
    if ($machineText -notmatch 'boot1="dvd"') { throw 'Qualification VM did not retain DVD-first boot.' }
    if ($machineText -notmatch 'storagecontrollertype\d+="IntelAhci"|storagecontrollertype\d+="IntelAHCI"') {
        throw 'Qualification VM does not expose Intel AHCI.'
    }
    if ($machineText -notmatch 'nictype1="Am79C973"') {
        throw 'Qualification VM did not retain PCnet-FAST III.'
    }

    Invoke-VBoxNative -Arguments @('startvm', $vm, '--type', 'headless') | Out-Null
    $started = $true

    $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $hardDeadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(600, $TimeoutSeconds * 4))
    $lastLength = 0
    $lastProgress = '<no installer serial output yet>'
    $installerDriven = $false
    $installerDrivenAt = [DateTime]::MinValue
    $booted = $false
    $storageReady = $false
    $installerComplete = $false
    $fatal = $null

    do {
        Start-Sleep -Milliseconds 350
        $text = Get-SerialText -Path $installSerial
        if ($text.Length -gt $lastLength) {
            $lastLength = $text.Length
            $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
            $progress = $text -split "`r?`n" |
                Where-Object { $_ -match '^installer stage ' -or $_ -match '^\[INSTALL\]\[VERIFY\]' -or $_ -match '^\[TEST\] installer_' } |
                Select-Object -Last 1
            if (-not [string]::IsNullOrWhiteSpace($progress)) { $lastProgress = $progress }
        }

        if (-not $installerDriven -and $text -match '\[TEST\] installer_package_preflight: PASS') {
            Write-Host '[virtualbox-smoke] installer preflight reached; injecting deterministic PS/2 setup input'
            Drive-InstallerUi -VmName $vm
            $installerDriven = $true
            $installerDrivenAt = [DateTime]::UtcNow
            $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        }

        if ($text -match '\[FATAL\]\[INSTALL\].*') { $fatal = $Matches[0]; break }
        if ($text -match '\[TEST\] installer_complete: FAIL') { $fatal = '[TEST] installer_complete: FAIL'; break }
        if ($text -match 'KuroganeOS kernel entry') { $booted = $true }
        if ($text -match '\[TEST\] installer_confirmation: PASS') { $storageReady = $true }
        if ($text -match '\[TEST\] installer_complete: PASS') { $installerComplete = $true }

        if ($installerDriven -and -not $storageReady -and
            ([DateTime]::UtcNow - $installerDrivenAt).TotalSeconds -gt 30) {
            $fatal = 'deterministic PS/2 installer input did not reach installer_confirmation within 30 seconds'
            break
        }
        if ($booted -and $storageReady -and $installerComplete) { break }
    } while ([DateTime]::UtcNow -lt $idleDeadline -and [DateTime]::UtcNow -lt $hardDeadline)

    if ($null -ne $fatal) {
        throw "VirtualBox installer qualification failed: $fatal`n$(Get-SerialTail -Path $installSerial)"
    }
    if (-not $booted -or -not $storageReady -or -not $installerComplete) {
        throw "VirtualBox EFI64 install did not reach installer_complete: PASS. Last progress: $lastProgress`n$(Get-SerialTail -Path $installSerial)"
    }

    Write-Host '[virtualbox-smoke] deterministic PS/2 installer automation: PASS'
    Write-Host '[virtualbox-smoke] EFI64 optical boot + AHCI install: PASS'
    Write-Host '[virtualbox-smoke] installer_complete: PASS'

    $poweroff = Invoke-VBoxNative -Arguments @('controlvm', $vm, 'poweroff') -AllowFailure
    if ($poweroff.ExitCode -ne 0) {
        throw "Could not power off installed qualification VM.`n$($poweroff.StdErr)"
    }
    $started = $false
    Start-Sleep -Milliseconds 500

    Invoke-VBoxNative -Arguments @('storageattach', $vm, '--storagectl', 'IDE', '--port', '0', '--device', '0', '--type', 'dvddrive', '--medium', 'none') | Out-Null
    Invoke-VBoxNative -Arguments @('modifyvm', $vm, '--boot1', 'disk', '--boot2', 'none', '--boot3', 'none', '--boot4', 'none') | Out-Null
    Invoke-VBoxNative -Arguments @('modifyvm', $vm, '--uartmode1', 'file', $postSerial) | Out-Null

    Invoke-VBoxNative -Arguments @('startvm', $vm, '--type', 'headless') | Out-Null
    $started = $true

    $postIdleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $postHardDeadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(300, $TimeoutSeconds * 2))
    $postLength = 0
    $postBooted = $false
    $rootMounted = $false
    $initSpawned = $false
    $initOnline = $false
    $dhcpReady = $false
    $gatewayReady = $false
    $dnsReady = $false
    $postFatal = $null

    do {
        Start-Sleep -Milliseconds 350
        $text = Get-SerialText -Path $postSerial
        if ($text.Length -gt $postLength) {
            $postLength = $text.Length
            $postIdleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        }

        if ($text -match '\[FATAL\]\[[^\]]+\].*') { $postFatal = $Matches[0]; break }
        if ($text -match '\[TEST\] dhcp_lease: FAIL') { $postFatal = '[TEST] dhcp_lease: FAIL'; break }
        if ($text -match '\[TEST\] network_gateway_icmp: FAIL') { $postFatal = '[TEST] network_gateway_icmp: FAIL'; break }

        if ($text -match 'KuroganeOS kernel entry') { $postBooted = $true }
        if ($text -match 'persistent FAT32 root mounted read-write') { $rootMounted = $true }
        if ($text -match '\[TEST\] userspace_init_spawn: PASS') { $initSpawned = $true }
        if ($text -match '/system/init: PID 1 online') { $initOnline = $true }
        if ($text -match '\[TEST\] dhcp_lease: PASS') { $dhcpReady = $true }
        if ($text -match '\[TEST\] network_gateway_icmp: PASS') { $gatewayReady = $true }
        if ($text -match '\[TEST\] dns_resolver: PASS') { $dnsReady = $true }

        if ($postBooted -and $rootMounted -and $initSpawned -and $initOnline -and
            $dhcpReady -and $gatewayReady -and $dnsReady) { break }
    } while ([DateTime]::UtcNow -lt $postIdleDeadline -and [DateTime]::UtcNow -lt $postHardDeadline)

    if ($null -ne $postFatal) {
        throw "VirtualBox installed-disk qualification failed: $postFatal`n$(Get-SerialTail -Path $postSerial)"
    }
    if (-not $postBooted -or -not $rootMounted -or -not $initSpawned -or -not $initOnline -or
        -not $dhcpReady -or -not $gatewayReady -or -not $dnsReady) {
        throw "VirtualBox installed VDI did not reach persistent ROOT + PID 1 + DHCP + gateway + DNS.`n$(Get-SerialTail -Path $postSerial)"
    }

    Write-Host '[virtualbox-smoke] installer ISO detached before reboot: PASS'
    Write-Host '[virtualbox-smoke] installed VDI UEFI boot: PASS'
    Write-Host '[virtualbox-smoke] persistent Kurogane Root mount: PASS'
    Write-Host '[virtualbox-smoke] /system/init PID 1 online: PASS'
    Write-Host '[virtualbox-smoke] VirtualBox NAT + DHCP + gateway + DNS: PASS'
    Write-Host '[virtualbox-smoke] REAL ORACLE VIRTUALBOX INSTALL + REBOOT + NETWORK: PASS'
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
