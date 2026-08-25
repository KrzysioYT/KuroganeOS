[CmdletBinding()]
param(
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 45,
    [ValidateRange(0, 540)]
    [int]$MinimumRuntimeSeconds = 0,
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
    [ValidateRange(1024, 65535)]
    [int]$GdbPort = 1234,
    [ValidateRange(128, 4096)]
    [int]$MemoryMiB = 1024,
    [ValidateRange(1024, 65535)]
    [int]$MonitorPort = 45454,
    [ValidateSet('tcg', 'whpx')]
    [string]$Accelerator = 'tcg',
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$LogName = 'qemu'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Qemu = Join-Path $RootDir 'tools\qemu\qemu-system-x86_64.exe'
$Firmware = Join-Path $RootDir 'tools\qemu\share\edk2-x86_64-code.fd'
$FirmwareVars = Join-Path $RootDir 'tools\qemu\share\edk2-i386-vars.fd'
$StageDir = Join-Path $RootDir 'iso'
$Bootloader = Join-Path $StageDir 'EFI\BOOT\BOOTX64.EFI'
$Kernel = Join-Path $StageDir 'kernel.elf'
$LegacyDiskImage = Join-Path $RootDir 'kurogane.img'
$FoundationBaseImage = Join-Path $RootDir 'build\images\KuroganeOS-base.img'
$WorkingImage = Join-Path $RootDir 'state\KuroganeOS.img'
$IsoImage = Join-Path $RootDir 'kurogane.iso'
$BuildDir = Join-Path $RootDir 'build'
$LogDir = Join-Path $BuildDir 'logs'
$SerialLog = Join-Path $LogDir "$LogName-serial.log"
$StdoutLog = Join-Path $LogDir "$LogName-stdout.log"
$StderrLog = Join-Path $LogDir "$LogName-stderr.log"
$XhciTraceLog = Join-Path $LogDir "$LogName-xhci.trace"
$InstallerIsoImage = Join-Path $BuildDir 'images\KuroganeOS-installer.iso'

function Resolve-ExistingDiskImage {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Description,
        [bool]$RequireWritable
    )
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Description path cannot be empty."
    }
    $resolved = @(Resolve-Path -LiteralPath $Path -ErrorAction Stop)
    if ($resolved.Count -ne 1 -or $resolved[0].Provider.Name -ne 'FileSystem') {
        throw "$Description must resolve to exactly one filesystem file: $Path"
    }
    $fullPath = [System.IO.Path]::GetFullPath($resolved[0].ProviderPath)
    $item = Get-Item -LiteralPath $fullPath -Force
    if ($item.PSIsContainer) { throw "$Description is a directory: $fullPath" }
    if ($item.Length -lt 512 -or ($item.Length % 512) -ne 0) {
        throw "$Description must be a non-empty 512-byte-aligned raw image: $fullPath"
    }
    if ([System.IO.Path]::GetExtension($fullPath) -notin @('.img', '.raw')) {
        throw "$Description must use .img or .raw: $fullPath"
    }
    if ($RequireWritable -and $item.IsReadOnly) {
        throw "$Description is read-only: $fullPath"
    }
    if ($fullPath.Contains(',')) {
        throw "$Description path cannot contain a comma: $fullPath"
    }
    return $fullPath
}

function Test-SamePath {
    param([string]$Left, [string]$Right)
    return [System.IO.Path]::GetFullPath($Left).Equals(
        [System.IO.Path]::GetFullPath($Right),
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-PathInsideDirectory {
    param([string]$Path, [string]$Directory)
    $trim = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar)
    $root = [System.IO.Path]::GetFullPath($Directory).TrimEnd($trim)
    return [System.IO.Path]::GetFullPath($Path).StartsWith(
        $root + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Format-QemuDriveFile {
    param([string]$Path)
    return '"' + $Path + '"'
}

function Get-HmpKeyName {
    param([char]$Character)
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

function Invoke-MonitorLines {
    param([int]$Port, [string[]]$Lines)
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $connected = $false
        for ($attempt = 0; $attempt -lt 50 -and -not $connected; ++$attempt) {
            try {
                $client.Connect('127.0.0.1', $Port)
                $connected = $true
            }
            catch [System.Net.Sockets.SocketException] {
                Start-Sleep -Milliseconds 40
            }
        }
        if (-not $connected) { throw "Cannot connect to QEMU monitor on $Port." }
        $writer = [System.IO.StreamWriter]::new(
            $client.GetStream(), [System.Text.Encoding]::ASCII, 1024, $true)
        try {
            $writer.AutoFlush = $true
            foreach ($line in $Lines) {
                $writer.WriteLine($line)
            }
        }
        finally { $writer.Dispose() }
    }
    finally { $client.Dispose() }
}

function Invoke-MonitorText {
    param([int]$Port, [string]$Text)
    $lines = @()
    foreach ($character in $Text.ToCharArray()) {
        $lines += "sendkey $(Get-HmpKeyName -Character $character) 1"
    }
    $lines += 'sendkey ret 1'
    Invoke-MonitorLines -Port $Port -Lines $lines
}

function Invoke-BootModeKey {
    param([int]$Port, [ValidateSet('s', 'd')][string]$Key)
    Write-Host "[boot-input] $Key"
    Invoke-MonitorLines -Port $Port -Lines @("sendkey $Key 120")
}

function Invoke-ConsoleShellTest {
    param([int]$Port, [bool]$Safe)
    $commands = if ($Safe) {
        @('pwd', 'cd /home', 'pwd', 'cat readme.txt', 'free', 'tasks', 'whoami', 'echo safemodepass')
    } else {
        @('help', 'pid', 'files', 'monitor', 'about', 'hello', 'external', 'echo usershellpass')
    }
    foreach ($command in $commands) {
        Invoke-MonitorText -Port $Port -Text $command
        Start-Sleep -Milliseconds 90
    }
}

function Invoke-GraphicalSessionTest {
    param([int]$Port)
    # Foundation/live profiles do not require a password. Enter activates the
    # selected session gate and lets PID1 transition into Blade Launcher.
    Invoke-MonitorLines -Port $Port -Lines @('sendkey ret 1')
    Start-Sleep -Milliseconds 250
    # Exercise real PS/2 motion after desktop entry without relying on fixed
    # widget coordinates. WindowManager/input tests prove click routing in the
    # same boot; this movement catches regressions in the IRQ/input path.
    Invoke-MonitorLines -Port $Port -Lines @(
        'mouse_set 2',
        'mouse_move 30 12',
        'mouse_move 30 12',
        'mouse_move -20 -8')
}

function Invoke-UsbKeyboardTest {
    param([int]$Port)
    Invoke-MonitorLines -Port $Port -Lines @('sendkey f12 1')
}

function Test-RequiredPatterns {
    param([string]$Serial, [string[]]$Patterns)
    foreach ($pattern in $Patterns) {
        if ($Serial -notmatch $pattern) { return $false }
    }
    return $true
}

function Test-GraphicalOutput {
    param([string]$Serial, [bool]$FoundationRoot, [bool]$Scratch)
    if ($Serial -match '(?m)^\[TEST\].*: FAIL\r?$') { return $false }
    $patterns = @(
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
        '\[TEST\] ALL_REQUIRED_TESTS_PASSED')
    if ($FoundationRoot) {
        $patterns += @(
            '\[TEST\] fat32_vfs_read: PASS',
            '\[TEST\] ring3_fault_isolation: PASS',
            '\[TEST\] process_spawn_wait: PASS',
            '\[TEST\] ring3_preemption: PASS',
            '\[TEST\] user_multitasking: PASS',
            '\[TEST\] syscall_process_abi: PASS',
            '\[TEST\] e1000_link: PASS',
            '\[TEST\] dhcp_lease: PASS',
            '\[TEST\] udp_transport: PASS',
            '\[TEST\] network_gateway_icmp: PASS',
            '\[TEST\] ps2_mouse: PASS')
    }
    if ($Scratch) { $patterns += '\[TEST\] ahci_write_flush_readback_restore: PASS' }
    return Test-RequiredPatterns -Serial $Serial -Patterns $patterns
}

function Test-ConsoleOutput {
    param([string]$Serial, [bool]$Safe)
    if ($Serial -match '(?m)^\[TEST\].*: FAIL\r?$') { return $false }
    if ($Safe) {
        return Test-RequiredPatterns -Serial $Serial -Patterns @(
            'SAFE MODE: minimal drivers',
            '\[TEST\] network_loopback: SKIP',
            '(?m)^safemodepass\r?$')
    }
    return Test-RequiredPatterns -Serial $Serial -Patterns @(
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
    throw 'UseDiskImage/DiskImagePath and UseIso are mutually exclusive.'
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
if ($WritableScratchDiskPath -and -not $diskMode) {
    throw 'WritableScratchDiskPath requires disk-image mode.'
}

$SystemDiskImage = $null
$InstallerTargetImage = $null
if ($InstallerTest) {
    if (-not $installerPathSpecified) { throw 'InstallerTest requires InstallerDiskPath.' }
    if (-not (Test-Path -LiteralPath $InstallerIsoImage -PathType Leaf)) {
        throw "Missing installer ISO: $InstallerIsoImage"
    }
    $InstallerTargetImage = Resolve-ExistingDiskImage -Path $InstallerDiskPath -Description 'Installer target' -RequireWritable $true
    $testDisks = Join-Path $BuildDir 'test-disks'
    if (-not (Test-PathInsideDirectory -Path $InstallerTargetImage -Directory $testDisks)) {
        throw "Installer target must live below $testDisks"
    }
    if ((Get-Item -LiteralPath $InstallerTargetImage).Length -lt 536870912) {
        throw 'Installer target must be at least 512 MiB.'
    }
} elseif ($diskMode) {
    $candidate = if ($diskPathSpecified) {
        $DiskImagePath
    } elseif (Test-Path -LiteralPath $FoundationBaseImage -PathType Leaf) {
        $FoundationBaseImage
    } else {
        $LegacyDiskImage
    }
    $SystemDiskImage = Resolve-ExistingDiskImage -Path $candidate -Description 'System disk image' -RequireWritable $WritableDiskImage.IsPresent
} elseif ($UseIso) {
    if (-not (Test-Path -LiteralPath $IsoImage -PathType Leaf)) {
        throw "Missing ISO: $IsoImage"
    }
} else {
    foreach ($required in @($Bootloader, $Kernel)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Missing staged boot input: $required"
        }
    }
}

$ScratchDiskImage = $null
if ($scratchSpecified) {
    $ScratchDiskImage = Resolve-ExistingDiskImage -Path $WritableScratchDiskPath -Description 'Scratch disk' -RequireWritable $true
    if ($SystemDiskImage -and (Test-SamePath $ScratchDiskImage $SystemDiskImage)) {
        throw 'System image and scratch disk must be different files.'
    }
    if ((Test-PathInsideDirectory -Path $ScratchDiskImage -Directory $RootDir) -and
        -not (Test-PathInsideDirectory -Path $ScratchDiskImage -Directory (Join-Path $BuildDir 'test-disks'))) {
        throw 'Repository-local scratch images are allowed only below build/test-disks.'
    }
}

$expectFoundationRoot = $false
if ($SystemDiskImage) {
    if ((Test-Path -LiteralPath $FoundationBaseImage -PathType Leaf) -and
        (Test-SamePath $SystemDiskImage $FoundationBaseImage)) {
        $expectFoundationRoot = $true
    } elseif ((Test-Path -LiteralPath $WorkingImage -PathType Leaf) -and
              (Test-SamePath $SystemDiskImage $WorkingImage)) {
        $expectFoundationRoot = $true
    } elseif ([System.IO.Path]::GetFileName($SystemDiskImage) -match 'KuroganeOS-.*-qemu-x86_64\.img$') {
        $expectFoundationRoot = $true
    }
}

[System.IO.Directory]::CreateDirectory($LogDir) | Out-Null
foreach ($log in @($SerialLog, $StdoutLog, $StderrLog, $XhciTraceLog)) {
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
}

$arguments = @(
    '-machine', 'q35',
    '-accel', $Accelerator,
    '-m', "$($MemoryMiB)M",
    '-smp', '1',
    '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$Firmware",
    '-drive', "if=pflash,format=raw,unit=1,snapshot=on,file=$FirmwareVars",
    '-serial', "file:$SerialLog",
    '-netdev', 'user,id=kurogane_net',
    '-device', 'e1000,netdev=kurogane_net,mac=52:54:00:4b:55:01',
    '-no-reboot',
    '-no-shutdown')
if ($Accelerator -eq 'tcg') { $arguments += @('-cpu', 'max') }
if ($UsbTest) {
    $arguments += @('-device', 'qemu-xhci,id=kurogane_xhci', '-device', 'usb-kbd,bus=kurogane_xhci.0', '-trace', "enable=usb_xhci_*,file=$XhciTraceLog")
}
if ($InstallerTest) {
    $arguments += @(
        '-drive', "if=none,id=kurogane_install_target,format=raw,file=$(Format-QemuDriveFile $InstallerTargetImage),snapshot=off,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_install_target,bus=ide.0,bootindex=20',
        '-drive', "if=none,id=kurogane_installer_cd,media=cdrom,readonly=on,file=$(Format-QemuDriveFile $InstallerIsoImage)",
        '-device', 'ide-cd,drive=kurogane_installer_cd,bus=ide.1,bootindex=1',
        '-boot', 'order=d,once=d')
} elseif ($diskMode) {
    $snapshot = if ($WritableDiskImage) { 'off' } else { 'on' }
    $arguments += @(
        '-drive', "if=none,id=kurogane_system,format=raw,file=$(Format-QemuDriveFile $SystemDiskImage),snapshot=$snapshot,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1')
} elseif ($UseIso) {
    $arguments += @('-cdrom', $IsoImage)
} else {
    $arguments += @('-drive', "format=raw,file=fat:rw:$StageDir")
}
if ($ScratchDiskImage) {
    $arguments += @(
        '-drive', "if=none,id=kurogane_scratch,format=raw,file=$(Format-QemuDriveFile $ScratchDiskImage),snapshot=off,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_scratch,bus=ide.1,bootindex=20')
}
$needsMonitor = $ShellTest -or $UsbTest -or $InstallerTest -or $SafeMode -or $DesktopMode
if ($needsMonitor) {
    $arguments += @('-monitor', "tcp:127.0.0.1:$MonitorPort,server=on,wait=off")
} else {
    $arguments += @('-monitor', 'none')
}
if ($Headless) { $arguments += @('-display', 'none') }
if ($DebugWait) { $arguments += @('-S', '-gdb', "tcp:127.0.0.1:$GdbPort") }

Write-Host "[qemu] $Qemu"
Write-Host "[accelerator] $Accelerator"
Write-Host "[memory] ${MemoryMiB} MiB"
if ($SystemDiskImage) { Write-Host "[image] $SystemDiskImage" }
elseif ($UseIso) { Write-Host "[iso] $IsoImage" }
elseif ($InstallerTest) { Write-Host "[installer] $InstallerIsoImage -> $InstallerTargetImage" }
else { Write-Host "[stage] $StageDir" }
Write-Host "[serial] $SerialLog"

$process = $null
$success = $false
$commandsSent = $false
$bootModeKeySent = $false
$installerIndexSent = $false
$installerConfirmationSent = $false
$graphicalInputSent = $false
$startedAt = [DateTime]::UtcNow

try {
    $windowStyle = if ($Headless) { 'Hidden' } else { 'Normal' }
    $process = Start-Process -FilePath $Qemu -ArgumentList $arguments `
        -RedirectStandardOutput $StdoutLog -RedirectStandardError $StderrLog `
        -WindowStyle $windowStyle -PassThru

    if ($DebugWait) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) { throw 'QEMU exited before GDB could attach.' }
        Write-Host "[running] QEMU PID $($process.Id), GDB tcp:127.0.0.1:$GdbPort"
        exit 0
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $process.Refresh()
        if ($process.HasExited) { break }
        if (Test-Path -LiteralPath $SerialLog) {
            $serial = Get-Content -LiteralPath $SerialLog -Raw -ErrorAction SilentlyContinue
            if ($serial -match 'KERNEL (EXCEPTION|PANIC)|(?m)^fatal:|(?m)^\[TEST\].*: FAIL\r?$') {
                break
            }

            if ($InstallerTest) {
                if (-not $installerIndexSent -and $serial -match 'installer: select target disk index:') {
                    Invoke-MonitorText -Port $MonitorPort -Text '0'
                    $installerIndexSent = $true
                }
                if (-not $installerConfirmationSent -and $serial -match 'installer: type INSTALL to confirm:') {
                    Invoke-MonitorText -Port $MonitorPort -Text 'INSTALL'
                    $installerConfirmationSent = $true
                }
                if ($serial -match '\[TEST\] installer_complete: PASS') {
                    $success = $true
                    break
                }
                Start-Sleep -Milliseconds 100
                continue
            }

            if (($SafeMode -or $DesktopMode) -and -not $bootModeKeySent) {
                if ($SafeMode -and $serial -match 'Press S|safe mode') {
                    Invoke-BootModeKey -Port $MonitorPort -Key 's'
                    $bootModeKeySent = $true
                } elseif ($DesktopMode -and $serial -match 'Press D|Default boot=console') {
                    Invoke-BootModeKey -Port $MonitorPort -Key 'd'
                    $bootModeKeySent = $true
                }
            }

            if ($UsbTest) {
                if (-not $commandsSent -and $serial -match '\[TEST\] xhci_keyboard_enumeration: PASS') {
                    Invoke-UsbKeyboardTest -Port $MonitorPort
                    $commandsSent = $true
                }
                if ($serial -match '\[TEST\] usb_hid_keyboard_input: PASS') {
                    $success = $true
                    break
                }
                Start-Sleep -Milliseconds 100
                continue
            }

            $graphicalReady = $serial -match '\[TEST\] kurogane5_obsidian_login: PASS|\[TEST\] red_flux_login_surface: PASS'
            $safePrompt = $serial -match 'kurogane:/ \$'
            $consolePrompt = $serial -match 'kurogane:user\$ '

            if ($ShellTest) {
                if ($graphicalReady) {
                    if (-not $graphicalInputSent) {
                        Invoke-GraphicalSessionTest -Port $MonitorPort
                        $graphicalInputSent = $true
                    }
                    if (Test-GraphicalOutput -Serial $serial -FoundationRoot $expectFoundationRoot -Scratch ($null -ne $ScratchDiskImage)) {
                        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $MinimumRuntimeSeconds) {
                            $success = $true
                            break
                        }
                    }
                } elseif ($SafeMode -and $safePrompt) {
                    if (-not $commandsSent) {
                        Invoke-ConsoleShellTest -Port $MonitorPort -Safe $true
                        $commandsSent = $true
                    }
                    if (Test-ConsoleOutput -Serial $serial -Safe $true) {
                        $success = $true
                        break
                    }
                } elseif ($consolePrompt) {
                    if (-not $commandsSent) {
                        Invoke-ConsoleShellTest -Port $MonitorPort -Safe $false
                        $commandsSent = $true
                    }
                    if (Test-ConsoleOutput -Serial $serial -Safe $false) {
                        $success = $true
                        break
                    }
                }
            } else {
                $ready = $graphicalReady -or $safePrompt -or $consolePrompt -or
                    ($serial -match '\[TEST\] ALL_REQUIRED_TESTS_PASSED')
                if ($ready -and ([DateTime]::UtcNow - $startedAt).TotalSeconds -ge $MinimumRuntimeSeconds) {
                    $success = $true
                    break
                }
            }
        }
        Start-Sleep -Milliseconds 100
    }

    if ($KeepRunning -and $process -and -not $process.HasExited) {
        Write-Host "[running] QEMU PID $($process.Id)"
        exit 0
    }
}
finally {
    if (-not $KeepRunning -and $process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}

if (Test-Path -LiteralPath $SerialLog) {
    Write-Host '--- serial tail ---'
    Get-Content -LiteralPath $SerialLog -Tail 160
}
if (-not $success -and (Test-Path -LiteralPath $StderrLog)) {
    Write-Host '--- qemu stderr ---'
    Get-Content -LiteralPath $StderrLog -Tail 80
}

if (-not $success) {
    if ($InstallerTest) { throw "KuroganeOS installer did not complete within $TimeoutSeconds seconds." }
    if ($UsbTest) { throw "KuroganeOS USB HID test did not pass within $TimeoutSeconds seconds." }
    if ($ShellTest) { throw "KuroganeOS integration test did not pass within $TimeoutSeconds seconds." }
    throw "KuroganeOS did not reach a ready console/login state within $TimeoutSeconds seconds."
}

if ($InstallerTest) { Write-Host '[pass] installer completed.' }
elseif ($UsbTest) { Write-Host '[pass] xHCI/USB HID integration passed.' }
elseif ($ShellTest) { Write-Host '[pass] KuroganeOS console/graphical integration passed.' }
else { Write-Host '[pass] KuroganeOS reached a ready state.' }
