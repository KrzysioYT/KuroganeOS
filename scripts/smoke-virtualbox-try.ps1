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
        throw 'VBoxManage.exe not found. A real Oracle VirtualBox installation is required for 3.3.4 qualification.'
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
    Invoke-VBoxNative -Arguments (@('controlvm', $VmName, 'keyboardputscancode') + $Codes) | Out-Null
}

function Send-Enter {
    param([Parameter(Mandatory = $true)][string]$VmName)
    Send-ScanCodes -VmName $VmName -Codes @('1c', '9c')
}

$Iso = Resolve-KuroganePath -Path $Iso
if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "ISO not found: $Iso"
}
if ([System.IO.Path]::GetExtension($Iso).ToLowerInvariant() -ne '.iso') {
    throw "VirtualBox Try smoke requires an .iso optical image: $Iso"
}
if ((Get-Item -LiteralPath $Iso).Length -le 0) {
    throw "ISO is empty: $Iso"
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('kurogane-vbox-try-' + [Guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($temp) | Out-Null
$vm = 'KuroganeOS-Try-Smoke-' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$disk = Join-Path $temp 'KuroganeOS-try.vdi'
$serial = Join-Path $temp 'try-serial.log'
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
    Invoke-VBoxNative -Arguments @('modifyvm', $vm, '--uartmode1', 'file', $serial) | Out-Null
    Invoke-VBoxNative -Arguments @('createmedium', 'disk', '--filename', $disk, '--size', '2048', '--format', 'VDI') | Out-Null
    Invoke-VBoxNative -Arguments @('storagectl', $vm, '--name', 'SATA', '--add', 'sata', '--controller', 'IntelAHCI', '--portcount', '1') | Out-Null
    Invoke-VBoxNative -Arguments @('storageattach', $vm, '--storagectl', 'SATA', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', $disk) | Out-Null
    Invoke-VBoxNative -Arguments @('storagectl', $vm, '--name', 'IDE', '--add', 'ide', '--controller', 'PIIX4') | Out-Null
    Invoke-VBoxNative -Arguments @('storageattach', $vm, '--storagectl', 'IDE', '--port', '0', '--device', '0', '--type', 'dvddrive', '--medium', $Iso) | Out-Null
    Invoke-VBoxNative -Arguments @('setextradata', $vm, 'VBoxInternal2/EfiGraphicsResolution', '1280x800') | Out-Null

    $machine = (Invoke-VBoxNative -Arguments @('showvminfo', $vm, '--machinereadable')).StdOut
    if ($machine -notmatch 'firmware="EFI64"') { throw 'Try qualification VM did not retain EFI64 firmware.' }
    if ($machine -notmatch 'boot1="dvd"') { throw 'Try qualification VM did not retain DVD-first boot.' }
    if ($machine -notmatch 'storagecontrollertype\d+="IntelAhci"|storagecontrollertype\d+="IntelAHCI"') {
        throw 'Try qualification VM does not expose Intel AHCI.'
    }
    if ($machine -notmatch 'nictype1="Am79C973"') {
        throw 'Try qualification VM did not retain PCnet-FAST III.'
    }

    Invoke-VBoxNative -Arguments @('startvm', $vm, '--type', 'headless') | Out-Null
    $started = $true

    $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $hardDeadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(600, $TimeoutSeconds * 4))
    $lastLength = 0
    $trySelected = $false
    $loginSurface = $false
    $liveProfile = $false
    $loginActivated = $false
    $loginToDesktop = $false
    $desktopLauncher = $false
    $desktopSession = $false
    $fatal = $null

    do {
        Start-Sleep -Milliseconds 350
        $text = Get-SerialText -Path $serial
        if ($text.Length -gt $lastLength) {
            $lastLength = $text.Length
            $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        }

        if ($text -match '\[FATAL\]\[[^\]]+\].*') { $fatal = $Matches[0]; break }
        if ($text -match '\[TEST\] desktop_session_fallback: PASS') {
            $fatal = '[TEST] desktop_session_fallback: PASS'; break
        }

        if (-not $trySelected -and $text -match '\[TEST\] installer_package_preflight: PASS') {
            Write-Host '[virtualbox-try] setup preflight reached; selecting default TRY path'
            Start-Sleep -Milliseconds 900
            Send-Enter -VmName $vm
            $trySelected = $true
            $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        }

        if ($text -match '\[TEST\] red_flux_login_surface: PASS') { $loginSurface = $true }
        if ($text -match '\[TEST\] live_login_profile: PASS') { $liveProfile = $true }

        if ($trySelected -and $loginSurface -and $liveProfile -and -not $loginActivated) {
            Write-Host '[virtualbox-try] live Login reached; activating no-password live session'
            Start-Sleep -Milliseconds 700
            Send-Enter -VmName $vm
            $loginActivated = $true
            $idleDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        }

        if ($text -match '\[TEST\] red_flux_login_to_desktop: PASS') { $loginToDesktop = $true }
        if ($text -match '\[TEST\] desktop_launcher_ring3: PASS') { $desktopLauncher = $true }
        if ($text -match '\[TEST\] userspace_desktop_session: PASS') { $desktopSession = $true }

        if ($trySelected -and $loginSurface -and $liveProfile -and $loginActivated -and
            $loginToDesktop -and $desktopLauncher -and $desktopSession) {
            break
        }
    } while ([DateTime]::UtcNow -lt $idleDeadline -and [DateTime]::UtcNow -lt $hardDeadline)

    if ($null -eq $fatal -and
        (-not $trySelected -or -not $loginSurface -or -not $liveProfile -or
         -not $loginActivated -or -not $loginToDesktop -or -not $desktopLauncher -or
         -not $desktopSession)) {
        Start-Sleep -Milliseconds 500
        $text = Get-SerialText -Path $serial
        if ($text -match '\[FATAL\]\[[^\]]+\].*') { $fatal = $Matches[0] }
        if ($text -match '\[TEST\] desktop_session_fallback: PASS') { $fatal = '[TEST] desktop_session_fallback: PASS' }
        if ($text -match '\[TEST\] installer_package_preflight: PASS') { $trySelected = $trySelected }
        if ($text -match '\[TEST\] red_flux_login_surface: PASS') { $loginSurface = $true }
        if ($text -match '\[TEST\] live_login_profile: PASS') { $liveProfile = $true }
        if ($text -match '\[TEST\] red_flux_login_to_desktop: PASS') { $loginToDesktop = $true }
        if ($text -match '\[TEST\] desktop_launcher_ring3: PASS') { $desktopLauncher = $true }
        if ($text -match '\[TEST\] userspace_desktop_session: PASS') { $desktopSession = $true }
    }

    if ($null -ne $fatal) {
        throw "VirtualBox Try qualification failed: $fatal`n$(Get-SerialTail -Path $serial)"
    }
    if (-not $trySelected -or -not $loginSurface -or -not $liveProfile -or
        -not $loginActivated -or -not $loginToDesktop -or -not $desktopLauncher -or
        -not $desktopSession) {
        throw "VirtualBox Try path did not reach live Login -> Red Flux Desktop.`n$(Get-SerialTail -Path $serial)"
    }

    Write-Host '[virtualbox-try] EFI optical boot: PASS'
    Write-Host '[virtualbox-try] ISO -> Try selection: PASS'
    Write-Host '[virtualbox-try] Try -> live Login: PASS'
    Write-Host '[virtualbox-try] live Login -> Red Flux Desktop: PASS'
    Write-Host '[virtualbox-try] Ring-3 desktop launcher: PASS'
    Write-Host '[virtualbox-try] userspace desktop session supervision: PASS'
    Write-Host '[virtualbox-try] VIRTUALBOX TRY/LOGIN/DESKTOP VERIFIED'
} finally {
    if ($started) {
        Invoke-VBoxNative -Arguments @('controlvm', $vm, 'poweroff') -AllowFailure | Out-Null
        $started = $false
        Start-Sleep -Milliseconds 300
    }
    if ($registered) {
        Invoke-VBoxNative -Arguments @('unregistervm', $vm, '--delete') -AllowFailure | Out-Null
        $registered = $false
    }
    if (Test-Path -LiteralPath $temp) {
        Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
    }
}
