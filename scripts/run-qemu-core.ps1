[CmdletBinding()]
param(
    [ValidateRange(1, 600)][int]$TimeoutSeconds = 45,
    [ValidateRange(0, 540)][int]$MinimumRuntimeSeconds = 0,
    [switch]$KeepRunning,
    [switch]$Display,
    [switch]$Headless,
    [switch]$ShellTest,
    [switch]$UsbTest,
    [switch]$InstallerTest,
    [string]$InstallerDiskPath,
    [switch]$SafeMode,
    [switch]$DesktopMode,
    [switch]$UseDiskImage,
    [switch]$UseIso,
    [string]$DiskImagePath,
    [switch]$WritableDiskImage,
    [string]$WritableScratchDiskPath,
    [switch]$DebugWait,
    [ValidateRange(1024, 65535)][int]$GdbPort = 1234,
    [ValidateRange(128, 4096)][int]$MemoryMiB = 1024,
    [ValidateRange(1024, 65535)][int]$MonitorPort = 45454,
    [ValidateSet('tcg', 'whpx')][string]$Accelerator = 'tcg',
    [ValidatePattern('^[A-Za-z0-9._-]+$')][string]$LogName = 'qemu'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Qemu = Join-Path $RootDir 'tools\qemu\qemu-system-x86_64.exe'
$Firmware = Join-Path $RootDir 'tools\qemu\share\edk2-x86_64-code.fd'
$FirmwareVars = Join-Path $RootDir 'tools\qemu\share\edk2-i386-vars.fd'
$BuildDir = Join-Path $RootDir 'build'
$LogDir = Join-Path $BuildDir 'logs'
$StageDir = Join-Path $RootDir 'iso'
$Bootloader = Join-Path $StageDir 'EFI\BOOT\BOOTX64.EFI'
$Kernel = Join-Path $StageDir 'kernel.elf'
$FoundationBase = Join-Path $BuildDir 'images\KuroganeOS-base.img'
$WorkingImage = Join-Path $RootDir 'state\KuroganeOS.img'
$LegacyImage = Join-Path $RootDir 'kurogane.img'
$IsoImage = Join-Path $RootDir 'kurogane.iso'
$InstallerIso = Join-Path $BuildDir 'images\KuroganeOS-installer.iso'
$SerialLog = Join-Path $LogDir "$LogName-serial.log"
$StdoutLog = Join-Path $LogDir "$LogName-stdout.log"
$StderrLog = Join-Path $LogDir "$LogName-stderr.log"
$TraceLog = Join-Path $LogDir "$LogName-xhci.trace"

function Resolve-RawImage {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description,
        [bool]$RequireWritable = $false
    )
    if ([string]::IsNullOrWhiteSpace($Path)) { throw "$Description path is empty." }
    $resolved = @(Resolve-Path -LiteralPath $Path -ErrorAction Stop)
    if ($resolved.Count -ne 1 -or $resolved[0].Provider.Name -ne 'FileSystem') {
        throw "$Description must resolve to one filesystem file: $Path"
    }
    $full = [System.IO.Path]::GetFullPath($resolved[0].ProviderPath)
    $item = Get-Item -LiteralPath $full -Force
    if ($item.PSIsContainer) { throw "$Description is a directory: $full" }
    if ($item.Length -lt 512 -or ($item.Length % 512) -ne 0) {
        throw "$Description must be a non-empty 512-byte-aligned raw image: $full"
    }
    if ([System.IO.Path]::GetExtension($full) -notin @('.img', '.raw')) {
        throw "$Description must use .img or .raw: $full"
    }
    if ($RequireWritable -and $item.IsReadOnly) { throw "$Description is read-only: $full" }
    if ($full.Contains(',')) { throw "$Description path cannot contain a comma: $full" }
    return $full
}

function Test-SamePath {
    param([string]$A, [string]$B)
    return [System.IO.Path]::GetFullPath($A).Equals(
        [System.IO.Path]::GetFullPath($B),
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-InsideDirectory {
    param([string]$Path, [string]$Directory)
    $trim = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $root = [System.IO.Path]::GetFullPath($Directory).TrimEnd($trim)
    return [System.IO.Path]::GetFullPath($Path).StartsWith(
        $root + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Format-DriveFile([string]$Path) { return '"' + $Path + '"' }

function Get-HmpKeyName([char]$Character) {
    if ($Character -ge 'A' -and $Character -le 'Z') {
        return 'shift-' + ([string]$Character).ToLowerInvariant()
    }
    if (($Character -ge 'a' -and $Character -le 'z') -or
        ($Character -ge '0' -and $Character -le '9')) {
        return [string]$Character
    }
    switch ($Character) {
        ' ' { return 'spc' }
        '/' { return 'slash' }
        '.' { return 'dot' }
        '-' { return 'minus' }
        '_' { return 'shift-minus' }
        default { throw "No QEMU key mapping for '$Character'." }
    }
}

function Open-HmpClient([int]$Port) {
    for ($attempt = 0; $attempt -lt 60; ++$attempt) {
        $client = [System.Net.Sockets.TcpClient]::new()
        try {
            $client.Connect('127.0.0.1', $Port)
            return $client
        } catch [System.Net.Sockets.SocketException] {
            $client.Dispose()
            Start-Sleep -Milliseconds 50
        }
    }
    throw "Could not connect to QEMU monitor on port $Port."
}

function Invoke-HmpLines {
    param(
        [int]$Port,
        [string[]]$Lines,
        [int]$DelayMilliseconds = 18
    )
    $client = Open-HmpClient -Port $Port
    try {
        $writer = [System.IO.StreamWriter]::new(
            $client.GetStream(), [System.Text.Encoding]::ASCII, 1024, $true)
        try {
            $writer.AutoFlush = $true
            foreach ($line in $Lines) {
                $writer.WriteLine($line)
                if ($DelayMilliseconds -gt 0) {
                    Start-Sleep -Milliseconds $DelayMilliseconds
                }
            }
        } finally { $writer.Dispose() }
    } finally { $client.Dispose() }
}

function Invoke-HmpText {
    param([int]$Port, [string]$Text)
    $lines = @()
    foreach ($character in $Text.ToCharArray()) {
        $lines += "sendkey $(Get-HmpKeyName $character) 35"
    }
    $lines += 'sendkey ret 45'
    Invoke-HmpLines -Port $Port -Lines $lines -DelayMilliseconds 22
    Start-Sleep -Milliseconds 90
}

function Invoke-BootModeKey([int]$Port, [string]$Key) {
    Write-Host "[boot-input] $Key"
    Invoke-HmpLines -Port $Port -Lines @("sendkey $Key 120") -DelayMilliseconds 0
}

function Invoke-ConsoleTest([int]$Port, [bool]$Safe) {
    $commands = if ($Safe) {
        @('pwd', 'cd /home', 'pwd', 'cat readme.txt', 'free', 'tasks', 'whoami', 'echo safemodepass')
    } else {
        @('help', 'pid', 'files', 'monitor', 'about', 'hello', 'external', 'echo usershellpass')
    }
    foreach ($command in $commands) { Invoke-HmpText -Port $Port -Text $command }
}

function Invoke-GraphicalSessionTest([int]$Port) {
    # Activate the selected Secure Access CTA.
    Invoke-HmpLines -Port $Port -Lines @('sendkey ret 70') -DelayMilliseconds 0
    Start-Sleep -Milliseconds 350

    # The session root is normally minimized after login. Super restores Blade,
    # then its documented T hotkey uses the same launcher backend as a click.
    # Requiring Kurosh below makes integration prove that a real child app can
    # open rather than stopping at "launcher process exists".
    Invoke-HmpLines -Port $Port -Lines @('sendkey meta_l 70') -DelayMilliseconds 0
    Start-Sleep -Milliseconds 160
    Invoke-HmpLines -Port $Port -Lines @('sendkey t 70') -DelayMilliseconds 0
    Start-Sleep -Milliseconds 180

    # Exercise real PS/2 pointer flow too. These are relative moves and do not
    # depend on a fixed guest resolution.
    Invoke-HmpLines -Port $Port -Lines @(
        'mouse_move 24 10',
        'mouse_move 24 10',
        'mouse_move -16 -6') -DelayMilliseconds 20
}

function Test-Patterns([string]$Serial, [string[]]$Patterns) {
    foreach ($pattern in $Patterns) {
        if ($Serial -notmatch $pattern) { return $false }
    }
    return $true
}

function Test-GraphicalPass([string]$Serial, [bool]$Foundation, [bool]$Scratch) {
    if ($Serial -match '(?m)^\[TEST\].*: FAIL\r?$') { return $false }
    $required = @(
        '\[TEST\] kernel_context_switch: PASS',
        '\[TEST\] kernel_preemption: PASS',
        '\[TEST\] userspace_init_spawn: PASS',
        '\[TEST\] userspace_init_pid1: PASS',
        '\[TEST\] desktop_session: PASS',
        '\[TEST\] kurogane5_obsidian_login: PASS',
        '\[TEST\] desktop_userspace_apps: PASS',
        '\[TEST\] userspace_desktop_session: PASS',
        '\[TEST\] kurogane5_login_to_desktop: PASS',
        '\[TEST\] desktop_launcher_ring3: PASS',
        '\[TEST\] kurogane5_blade_launcher: PASS',
        '\[TEST\] desktop_terminal_ring3: PASS',
        '\[TEST\] kurogane5_kurosh_terminal: PASS',
        '\[TEST\] ALL_REQUIRED_TESTS_PASSED')
    if ($Foundation) {
        $required += @(
            '\[TEST\] fat32_vfs_read: PASS',
            '\[TEST\] process_spawn_wait: PASS',
            '\[TEST\] ring3_preemption: PASS',
            '\[TEST\] user_multitasking: PASS',
            '\[TEST\] syscall_process_abi: PASS',
            '\[TEST\] ps2_mouse: PASS',
            '\[TEST\] e1000_link: PASS',
            '\[TEST\] dhcp_lease: PASS',
            '\[TEST\] udp_transport: PASS',
            '\[TEST\] network_gateway_icmp: PASS')
    }
    if ($Scratch) { $required += '\[TEST\] ahci_write_flush_readback_restore: PASS' }
    return Test-Patterns -Serial $Serial -Patterns $required
}

function Test-ConsolePass([string]$Serial, [bool]$Safe) {
    if ($Serial -match '(?m)^\[TEST\].*: FAIL\r?$') { return $false }
    if ($Safe) {
        return Test-Patterns -Serial $Serial -Patterns @(
            'SAFE MODE: minimal drivers',
            '\[TEST\] network_loopback: SKIP',
            '(?m)^safemodepass\r?$')
    }
    return Test-Patterns -Serial $Serial -Patterns @(
        '\[TEST\] userspace_init_spawn: PASS',
        '(?m)^usershellpass\r?$',
        '\[TEST\] ALL_REQUIRED_TESTS_PASSED')
}

if ($MinimumRuntimeSeconds -ge $TimeoutSeconds) {
    throw 'MinimumRuntimeSeconds must be smaller than TimeoutSeconds.'
}
if ($Headless -and $Display) { throw 'Headless and Display are mutually exclusive.' }
if ($SafeMode -and $DesktopMode) { throw 'SafeMode and DesktopMode are mutually exclusive.' }
if ($UseIso -and ($UseDiskImage -or $PSBoundParameters.ContainsKey('DiskImagePath'))) {
    throw 'UseIso and disk-image mode are mutually exclusive.'
}
if ($InstallerTest -and ($UseDiskImage -or $UseIso -or $ShellTest -or $UsbTest -or $SafeMode -or $DesktopMode)) {
    throw 'InstallerTest is a dedicated scenario.'
}
if ($DebugWait -and -not $KeepRunning) { throw 'DebugWait requires KeepRunning.' }

foreach ($required in @($Qemu, $Firmware, $FirmwareVars)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing QEMU input: $required"
    }
}

$diskPathSpecified = $PSBoundParameters.ContainsKey('DiskImagePath')
$diskMode = $UseDiskImage -or $diskPathSpecified
$scratchSpecified = $PSBoundParameters.ContainsKey('WritableScratchDiskPath')
$installerPathSpecified = $PSBoundParameters.ContainsKey('InstallerDiskPath')
if ($WritableDiskImage -and -not $diskPathSpecified) {
    throw 'WritableDiskImage requires an explicit DiskImagePath.'
}
if ($scratchSpecified -and -not $diskMode) {
    throw 'WritableScratchDiskPath requires disk-image mode.'
}

$SystemImage = $null
$InstallerTarget = $null
if ($InstallerTest) {
    if (-not $installerPathSpecified) { throw 'InstallerTest requires InstallerDiskPath.' }
    if (-not (Test-Path -LiteralPath $InstallerIso -PathType Leaf)) {
        throw "Missing installer ISO: $InstallerIso"
    }
    $InstallerTarget = Resolve-RawImage -Path $InstallerDiskPath -Description 'Installer target' -RequireWritable $true
    $testDisks = Join-Path $BuildDir 'test-disks'
    if (-not (Test-InsideDirectory $InstallerTarget $testDisks)) {
        throw "Installer target must live below $testDisks"
    }
    if ((Get-Item -LiteralPath $InstallerTarget).Length -lt 536870912) {
        throw 'Installer target must be at least 512 MiB.'
    }
    $probe = [System.IO.File]::Open(
        $InstallerTarget, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $buffer = New-Object byte[] 1048576
        $read = $probe.Read($buffer, 0, $buffer.Length)
        if ($read -ne $buffer.Length -or
            ($buffer | Where-Object { $_ -ne 0 } | Select-Object -First 1)) {
            throw 'Installer target is not a blank disposable image.'
        }
    } finally { $probe.Dispose() }
} elseif ($diskMode) {
    $candidate = if ($diskPathSpecified) {
        $DiskImagePath
    } elseif (Test-Path -LiteralPath $FoundationBase -PathType Leaf) {
        $FoundationBase
    } else {
        $LegacyImage
    }
    $SystemImage = Resolve-RawImage -Path $candidate -Description 'System image' -RequireWritable $WritableDiskImage.IsPresent
} elseif ($UseIso) {
    if (-not (Test-Path -LiteralPath $IsoImage -PathType Leaf)) { throw "Missing ISO: $IsoImage" }
} else {
    foreach ($required in @($Bootloader, $Kernel)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Missing staged boot input: $required"
        }
    }
}

$ScratchImage = $null
if ($scratchSpecified) {
    $ScratchImage = Resolve-RawImage -Path $WritableScratchDiskPath -Description 'Scratch disk' -RequireWritable $true
    if ($SystemImage -and (Test-SamePath $ScratchImage $SystemImage)) {
        throw 'System image and scratch disk must be different files.'
    }
    if ((Test-InsideDirectory $ScratchImage $RootDir) -and
        -not (Test-InsideDirectory $ScratchImage (Join-Path $BuildDir 'test-disks'))) {
        throw 'Repository-local scratch images are allowed only below build/test-disks.'
    }
}

$expectFoundation = $false
if ($SystemImage) {
    if ((Test-Path -LiteralPath $FoundationBase -PathType Leaf) -and
        (Test-SamePath $SystemImage $FoundationBase)) {
        $expectFoundation = $true
    } elseif ((Test-Path -LiteralPath $WorkingImage -PathType Leaf) -and
              (Test-SamePath $SystemImage $WorkingImage)) {
        $expectFoundation = $true
    } elseif ([System.IO.Path]::GetFileName($SystemImage) -match 'KuroganeOS-.*-qemu.*\.img$') {
        $expectFoundation = $true
    }
}

[System.IO.Directory]::CreateDirectory($LogDir) | Out-Null
foreach ($log in @($SerialLog, $StdoutLog, $StderrLog, $TraceLog)) {
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
}

$args = @(
    '-machine', 'q35',
    '-accel', $Accelerator,
    '-m', "$($MemoryMiB)M",
    '-smp', '1',
    '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$Firmware",
    '-drive', "if=pflash,format=raw,unit=1,snapshot=on,file=$FirmwareVars",
    '-serial', "file:$SerialLog",
    '-netdev', 'user,id=kurogane_net',
    '-device', 'e1000,netdev=kurogane_net,mac=52:54:00:4b:55:01',
    '-no-reboot', '-no-shutdown')
if ($Accelerator -eq 'tcg') { $args += @('-cpu', 'max') }
if ($UsbTest) {
    $args += @(
        '-device', 'qemu-xhci,id=kurogane_xhci',
        '-device', 'usb-kbd,bus=kurogane_xhci.0',
        '-trace', "enable=usb_xhci_*,file=$TraceLog")
}
if ($InstallerTest) {
    $args += @(
        '-drive', "if=none,id=install_target,format=raw,file=$(Format-DriveFile $InstallerTarget),snapshot=off,cache=writeback",
        '-device', 'ide-hd,drive=install_target,bus=ide.0,bootindex=20',
        '-drive', "if=none,id=install_iso,media=cdrom,readonly=on,file=$(Format-DriveFile $InstallerIso)",
        '-device', 'ide-cd,drive=install_iso,bus=ide.1,bootindex=1',
        '-boot', 'order=d,once=d')
} elseif ($diskMode) {
    $snapshot = if ($WritableDiskImage) { 'off' } else { 'on' }
    $args += @(
        '-drive', "if=none,id=kurogane_system,format=raw,file=$(Format-DriveFile $SystemImage),snapshot=$snapshot,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1')
} elseif ($UseIso) {
    $args += @('-cdrom', $IsoImage)
} else {
    $args += @('-drive', "format=raw,file=fat:rw:$StageDir")
}
if ($ScratchImage) {
    $args += @(
        '-drive', "if=none,id=kurogane_scratch,format=raw,file=$(Format-DriveFile $ScratchImage),snapshot=off,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_scratch,bus=ide.1,bootindex=20')
}
$needMonitor = $ShellTest -or $SafeMode -or $DesktopMode -or $UsbTest -or $InstallerTest
if ($needMonitor) { $args += @('-monitor', "tcp:127.0.0.1:$MonitorPort,server=on,wait=off") }
else { $args += @('-monitor', 'none') }
if ($Headless) { $args += @('-display', 'none') }
if ($DebugWait) { $args += @('-S', '-gdb', "tcp:127.0.0.1:$GdbPort") }

Write-Host "[qemu] $Qemu"
Write-Host "[accelerator] $Accelerator"
Write-Host "[memory] ${MemoryMiB} MiB"
if ($InstallerTest) { Write-Host "[installer] $InstallerIso -> $InstallerTarget" }
elseif ($SystemImage) { Write-Host "[image] $SystemImage" }
elseif ($UseIso) { Write-Host "[iso] $IsoImage" }
else { Write-Host "[stage] $StageDir" }
Write-Host "[serial] $SerialLog"

$process = $null
$success = $false
$inputSent = $false
$bootKeySent = $false
$installerIndexSent = $false
$installerConfirmSent = $false
$startedAt = [DateTime]::UtcNow
try {
    $windowStyle = if ($Headless) { 'Hidden' } else { 'Normal' }
    $process = Start-Process -FilePath $Qemu -ArgumentList $args `
        -RedirectStandardOutput $StdoutLog `
        -RedirectStandardError $StderrLog `
        -WindowStyle $windowStyle -PassThru

    if ($DebugWait) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) { throw 'QEMU exited before GDB attach.' }
        Write-Host "[running] QEMU PID $($process.Id), GDB tcp:127.0.0.1:$GdbPort"
        return
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $process.Refresh()
        if ($process.HasExited) { break }
        $serial = if (Test-Path -LiteralPath $SerialLog) {
            Get-Content -LiteralPath $SerialLog -Raw -ErrorAction SilentlyContinue
        } else { '' }

        if ($serial -match 'KERNEL (PANIC|EXCEPTION)|(?m)^fatal:|(?m)^\[TEST\].*: FAIL\r?$') { break }

        if ($InstallerTest) {
            if (-not $installerIndexSent -and $serial -match 'installer: select target disk index:') {
                Invoke-HmpText -Port $MonitorPort -Text '0'
                $installerIndexSent = $true
            }
            if (-not $installerConfirmSent -and $serial -match 'installer: type INSTALL to confirm:') {
                Invoke-HmpText -Port $MonitorPort -Text 'INSTALL'
                $installerConfirmSent = $true
            }
            if ($serial -match '\[TEST\] installer_complete: PASS') { $success = $true; break }
            Start-Sleep -Milliseconds 100
            continue
        }

        if (($SafeMode -or $DesktopMode) -and -not $bootKeySent) {
            if ($SafeMode -and $serial -match 'Press S|SAFE MODE') {
                Invoke-BootModeKey -Port $MonitorPort -Key 's'
                $bootKeySent = $true
            } elseif ($DesktopMode -and $serial -match 'Press D|Default boot=console') {
                Invoke-BootModeKey -Port $MonitorPort -Key 'd'
                $bootKeySent = $true
            }
        }

        if ($UsbTest) {
            if (-not $inputSent -and $serial -match '\[TEST\] xhci_keyboard_enumeration: PASS') {
                Invoke-HmpLines -Port $MonitorPort -Lines @('sendkey f12 70') -DelayMilliseconds 0
                $inputSent = $true
            }
            if ($serial -match '\[TEST\] usb_hid_keyboard_input: PASS') { $success = $true; break }
            Start-Sleep -Milliseconds 100
            continue
        }

        $graphicalReady = $serial -match '\[TEST\] kurogane5_obsidian_login: PASS|\[TEST\] red_flux_login_surface: PASS'
        $safePrompt = $serial -match 'kurogane:/ \$'
        $userPrompt = $serial -match 'kurogane:user\$ '

        if ($ShellTest) {
            if ($graphicalReady) {
                if (-not $inputSent) {
                    Invoke-GraphicalSessionTest -Port $MonitorPort
                    $inputSent = $true
                }
                if (Test-GraphicalPass -Serial $serial -Foundation $expectFoundation -Scratch ($null -ne $ScratchImage)) {
                    if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $MinimumRuntimeSeconds) {
                        $success = $true
                        break
                    }
                }
            } elseif ($SafeMode -and $safePrompt) {
                if (-not $inputSent) { Invoke-ConsoleTest -Port $MonitorPort -Safe $true; $inputSent = $true }
                if (Test-ConsolePass -Serial $serial -Safe $true) { $success = $true; break }
            } elseif ($userPrompt) {
                if (-not $inputSent) { Invoke-ConsoleTest -Port $MonitorPort -Safe $false; $inputSent = $true }
                if (Test-ConsolePass -Serial $serial -Safe $false) { $success = $true; break }
            }
        } else {
            $ready = $graphicalReady -or $safePrompt -or $userPrompt -or
                ($serial -match '\[TEST\] ALL_REQUIRED_TESTS_PASSED')
            if ($ready -and ([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $MinimumRuntimeSeconds) {
                $success = $true
                break
            }
        }
        Start-Sleep -Milliseconds 100
    }

    if ($KeepRunning -and $process -and -not $process.HasExited) {
        Write-Host "[running] QEMU PID $($process.Id)"
        return
    }
} finally {
    if (-not $KeepRunning -and $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}

if (Test-Path -LiteralPath $SerialLog) {
    Write-Host '--- serial tail ---'
    Get-Content -LiteralPath $SerialLog -Tail 180
}
if (-not $success -and (Test-Path -LiteralPath $StderrLog)) {
    Write-Host '--- qemu stderr ---'
    Get-Content -LiteralPath $StderrLog -Tail 80
}

if (-not $success) {
    if ($InstallerTest) { throw "KuroganeOS installer did not complete within $TimeoutSeconds seconds." }
    if ($UsbTest) { throw "KuroganeOS USB HID integration did not pass within $TimeoutSeconds seconds." }
    if ($ShellTest) { throw "KuroganeOS integration test did not pass within $TimeoutSeconds seconds." }
    throw "KuroganeOS did not reach a ready state within $TimeoutSeconds seconds."
}

if ($InstallerTest) { Write-Host '[pass] installer completed.' }
elseif ($UsbTest) { Write-Host '[pass] xHCI/USB HID integration passed.' }
elseif ($ShellTest) { Write-Host '[pass] KuroganeOS console/graphical integration passed (including Kurosh launch).' }
else { Write-Host '[pass] KuroganeOS reached a ready state.' }
