[CmdletBinding()]
param(
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 12,
    [ValidateRange(0, 540)]
    [int]$MinimumRuntimeSeconds = 0,
    [switch]$KeepRunning,
    [switch]$Display,
    [switch]$Headless,
    [switch]$ShellTest,
    [switch]$SocketTest,
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
    [ValidateRange(64, 4096)]
    [int]$MemoryMiB = 256,
    [ValidateRange(1024, 65535)]
    [int]$MonitorPort = 45454,
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
$DiskImage = Join-Path $RootDir 'kurogane.img'
$IsoImage = Join-Path $RootDir 'kurogane.iso'
$BuildDir = Join-Path $RootDir 'build'
$LogDir = Join-Path $BuildDir 'logs'
$SerialLog = Join-Path $LogDir "$LogName-serial.log"
$StdoutLog = Join-Path $LogDir "$LogName-stdout.log"
$StderrLog = Join-Path $LogDir "$LogName-stderr.log"
$XhciTraceLog = Join-Path $LogDir "$LogName-xhci.trace"

function Resolve-ExistingDiskImage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description,

        [bool]$RequireWritable
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "$Description path cannot be empty."
    }

    try {
        $resolved = @(Resolve-Path -LiteralPath $Path -ErrorAction Stop)
    }
    catch {
        throw "$Description does not exist: $Path"
    }
    if ($resolved.Count -ne 1 -or
        $resolved[0].Provider.Name -ne 'FileSystem') {
        throw "$Description must resolve to exactly one filesystem file: $Path"
    }

    $fullPath = [System.IO.Path]::GetFullPath($resolved[0].ProviderPath)
    $item = Get-Item -LiteralPath $fullPath -Force
    if ($item.PSIsContainer) {
        throw "$Description is a directory, not a disk image: $fullPath"
    }
    if ($item.Length -lt 512 -or ($item.Length % 512) -ne 0) {
        throw "$Description must be a non-empty, 512-byte-aligned raw image: $fullPath"
    }
    $extension = [System.IO.Path]::GetExtension($fullPath)
    if ($extension -notin @('.img', '.raw')) {
        throw "$Description must use the .img or .raw extension: $fullPath"
    }
    if ($RequireWritable -and $item.IsReadOnly) {
        throw "$Description is read-only but writable attachment was requested: $fullPath"
    }

    # QEMU's -drive key/value parser treats commas as separators after Windows
    # command-line quoting has been removed. Refuse an ambiguous path instead
    # of risking attachment of a different file or an altered option set.
    if ($fullPath.Contains(',')) {
        throw "$Description path cannot contain a comma: $fullPath"
    }
    return $fullPath
}

function Test-SameFilePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Left,

        [Parameter(Mandatory = $true)]
        [string]$Right
    )

    return $Left.Equals(
        $Right,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Test-PathInsideDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Directory
    )

    $trimCharacters = [char[]]@(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
    $directoryPath =
        [System.IO.Path]::GetFullPath($Directory).TrimEnd($trimCharacters)
    $prefix = $directoryPath + [System.IO.Path]::DirectorySeparatorChar
    return $Path.StartsWith(
        $prefix,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Format-QemuDriveFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    # Embedded quotes preserve spaces when Start-Process constructs the native
    # Windows command line. Double quotes are not legal in a Win32 file name.
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

function Invoke-MonitorText {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Text
    )
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $writer = [System.IO.StreamWriter]::new(
            $client.GetStream(), [System.Text.Encoding]::ASCII, 256, $true)
        try {
            $writer.AutoFlush = $true
            foreach ($character in $Text.ToCharArray()) {
                $writer.WriteLine(
                    "sendkey $(Get-HmpKeyName -Character $character) 1")
                Start-Sleep -Milliseconds 25
            }
            Start-Sleep -Milliseconds 120
            $writer.WriteLine('sendkey ret 1')
            if ($Text -ceq 'INSTALL') {
                Start-Sleep -Milliseconds 120
                $writer.WriteLine('sendkey ret 1')
            }
        }
        finally { $writer.Dispose() }
    }
    finally { $client.Dispose() }
}

function Invoke-MonitorKey {
    param(
        [Parameter(Mandatory = $true)][int]$Port,
        [Parameter(Mandatory = $true)][string]$Key
    )
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $writer = [System.IO.StreamWriter]::new(
            $client.GetStream(), [System.Text.Encoding]::ASCII, 256, $true)
        try {
            $writer.AutoFlush = $true
            $writer.WriteLine("sendkey $Key 1")
            Start-Sleep -Milliseconds 50
            $writer.WriteLine('sendkey ret 1')
        }
        finally { $writer.Dispose() }
    }
    finally { $client.Dispose() }
}

function Invoke-ShellKeyboardTest {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Port,
        [bool]$Safe,
        [bool]$Desktop,
        [bool]$Socket
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $connected = $false
        for ($attempt = 0; $attempt -lt 50 -and -not $connected; ++$attempt) {
            try {
                $client.Connect('127.0.0.1', $Port)
                $connected = $true
            }
            catch [System.Net.Sockets.SocketException] {
                Start-Sleep -Milliseconds 50
            }
        }
        if (-not $connected) {
            throw "Could not connect to the QEMU monitor on port $Port."
        }

        $writer = [System.IO.StreamWriter]::new(
            $client.GetStream(),
            [System.Text.Encoding]::ASCII,
            1024,
            $true
        )
        try {
            $writer.AutoFlush = $true
            if ($Desktop) {
                # Exercise the real PS/2 -> input -> WindowManager path in the
                # auto-launched desktop before returning to the userspace
                # console. PS/2 motion is split into signed-byte-sized steps.
                $writer.WriteLine('mouse_set 2')
                foreach ($step in 1..8) {
                    $writer.WriteLine('mouse_move -127 -127')
                    Start-Sleep -Milliseconds 25
                }
                $writer.WriteLine('mouse_move 120 75')
                Start-Sleep -Milliseconds 80
                $writer.WriteLine('mouse_button 1')
                Start-Sleep -Milliseconds 120
                foreach ($step in 1..4) {
                    $writer.WriteLine('mouse_move 20 10')
                    Start-Sleep -Milliseconds 40
                }
                $writer.WriteLine('mouse_button 0')
                Start-Sleep -Milliseconds 180
                $writer.WriteLine('sendkey alt-f4 1')
                Start-Sleep -Milliseconds 300
            }
            $commands = if ($Socket) {
                @('run /system/sockprb')
            }
            elseif ($Desktop) {
                @()
            }
            elseif ($Safe) {
                @(
                    'pwd',
                    'cd /home',
                    'pwd',
                    'cat readme.txt',
                    'free',
                    'tasks',
                    'whoami',
                    'echo safemodepass'
                )
            }
            else {
                @(
                    'help',
                    'pid',
                    'files',
                    'monitor',
                    'about',
                    'hello',
                    'external',
                    'echo usershellpass'
                )
            }
            foreach ($command in $commands) {
                foreach ($character in $command.ToCharArray()) {
                    $key = Get-HmpKeyName -Character $character
                    $writer.WriteLine("sendkey $key 1")
                    Start-Sleep -Milliseconds 12
                }
                $writer.WriteLine('sendkey ret 1')
                Start-Sleep -Milliseconds 80
                if ($command -eq 'gui') {
                    Start-Sleep -Milliseconds 180
                    $writer.WriteLine('mouse_set 2')
                    Start-Sleep -Milliseconds 80
                    foreach ($step in 1..8) {
                        $writer.WriteLine('mouse_move -127 -127')
                        Start-Sleep -Milliseconds 25
                    }
                    $writer.WriteLine('mouse_move 120 75')
                    Start-Sleep -Milliseconds 80
                    $writer.WriteLine('mouse_button 1')
                    Start-Sleep -Milliseconds 120
                    foreach ($step in 1..4) {
                        $writer.WriteLine('mouse_move 20 10')
                        Start-Sleep -Milliseconds 40
                    }
                    Start-Sleep -Milliseconds 80
                    $writer.WriteLine('mouse_button 0')
                    Start-Sleep -Milliseconds 180
                    $writer.WriteLine('sendkey q 1')
                    Start-Sleep -Milliseconds 180
                }
            }
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $client.Dispose()
    }
}

function Test-ShellOutput {
    param(
        [string]$Serial,
        [bool]$Safe,
        [bool]$Desktop,
        [bool]$Socket,
        [bool]$Scratch,
        [bool]$FoundationRoot
    )

    if ($Serial -match '(?m)^\[TEST\].*: FAIL\r?$') {
        return $false
    }

    $patterns = if ($Socket) {
        @(
            '\[TEST\] socket_udp_roundtrip: PASS',
            '\[TEST\] socket_readiness: PASS',
            '\[TEST\] socket_handle_generation: PASS',
            '\[TEST\] socket_exit_cleanup: PASS',
            '\[TEST\] tcp_progression: PASS',
            '\[TEST\] tcp_cleanup: PASS'
        )
    }
    elseif ($Safe) {
        @(
            'Safe mode requested',
            'SAFE MODE: minimal drivers and diagnostic shell',
            '\[TEST\] network_loopback: SKIP',
            '(?m)^/home\r?$',
            'Welcome to KuroganeOS\.',
            'heap total=',
            '(?m)^kernel\r?$',
            '(?m)^safemodepass\r?$'
        )
    }
    elseif ($Desktop) {
        @(
            'x86-64 UEFI kernel 2\.0',
            '\[TEST\] kernel_context_switch: PASS',
            '\[TEST\] kernel_preemption: PASS',
            '\[TEST\] userspace_init_spawn: PASS',
            '\[TEST\] userspace_init_pid1: PASS',
            '\[TEST\] userspace_shell_spawn: PASS',
            'KuroganeOS userspace shell \(Ring 3\)'
        )
    }
    else {
        @(
            'x86-64 UEFI kernel 2\.0',
            '\[TEST\] kernel_context_switch: PASS',
            '\[TEST\] kernel_preemption: PASS',
            '\[TEST\] userspace_init_spawn: PASS',
            '\[TEST\] userspace_init_pid1: PASS',
            '\[TEST\] userspace_shell_spawn: PASS',
            'KuroganeOS userspace shell \(Ring 3\)',
            'help pid echo hello external files monitor about exit',
            'shell pid=[1-9][0-9]* tid=[1-9][0-9]*',
            '\[TEST\] userspace_files_app: PASS',
            '\[TEST\] userspace_monitor_app: PASS',
            '\[TEST\] userspace_about_app: PASS',
            'Hello from external Kurogane application',
            '\[TEST\] external_sdk_application: PASS',
            '(?m)^usershellpass\r?$'
        )
    }
    if ($Desktop -and -not $Safe) {
        $patterns += @(
            'boot=desktop \(DESKTOP ALPHA\)',
            'desktop alpha session started',
            '\[TEST\] ps2_mouse: PASS',
            '\[TEST\] window_manager_multiwindow: PASS',
            '\[TEST\] window_drag_input: PASS',
            '\[TEST\] window_close_input: PASS',
            '\[TEST\] desktop_userspace_apps: PASS',
            '\[TEST\] desktop_terminal_ring3: PASS',
            '\[TEST\] desktop_files_real_vfs: PASS',
            '\[TEST\] desktop_sysmon_ring3: PASS',
            '\[TEST\] desktop_about_ring3: PASS',
            '\[TEST\] desktop_settings_real: PASS'
        )
    } elseif (-not $Safe) {
        $patterns += 'boot=console'
        if ($Serial -match
            'desktop - Desktop Alpha application launcher') {
            return $false
        }
    }
    if ($Scratch -and -not $Safe) {
        $patterns += '\[TEST\] ahci_write_flush_readback_restore: PASS'
    }
    if ($FoundationRoot -and -not $Safe) {
        $patterns += @(
            '\[TEST\] fat32_vfs_read: PASS',
            '\[TEST\] fat32_persistence_(prepare|verify): PASS',
            'hello from ring3 via SYS_WRITE',
            '\[TEST\] ring3_hello_syscalls: PASS',
            '\[TEST\] ring3_fault_isolation: PASS',
            '\[TEST\] process_spawn_wait: PASS',
            '\[TEST\] ring3_preemption: PASS',
            '\[TEST\] user_multitasking: PASS',
            '\[TEST\] syscall_process_abi: PASS',
            '\[TEST\] e1000_link: PASS',
            '\[TEST\] dhcp_lease: PASS',
            '\[TEST\] udp_transport: PASS',
            '\[TEST\] network_gateway_icmp: PASS',
            '\[TEST\] ps2_mouse: PASS',
            'persistent FAT32 root mounted read-write'
        )
    }
    $patterns += '\[TEST\] ALL_REQUIRED_TESTS_PASSED'
    foreach ($pattern in $patterns) {
        if ($Serial -notmatch $pattern) {
            return $false
        }
    }
    return $true
}

function Invoke-BootModeKey {
    param(
        [int]$Port,
        [ValidateSet('s', 'c', 'd')]
        [string]$Key
    )

    Write-Host "[boot-input] requesting boot mode with key '$Key'"
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $writer = [System.IO.StreamWriter]::new(
            $client.GetStream(),
            [System.Text.Encoding]::ASCII,
            256,
            $true
        )
        try {
            $writer.AutoFlush = $true
            # UEFI polls ConIn every 10 ms. A 1 ms monitor pulse can fall
            # completely between polls, so hold the boot-selection key long
            # enough to make the hand-off deterministic.
            $writer.WriteLine("sendkey $Key 100")
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $client.Dispose()
    }
}

function Invoke-UsbKeyboardTest {
    param([int]$Port)

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect('127.0.0.1', $Port)
        $writer = [System.IO.StreamWriter]::new(
            $client.GetStream(),
            [System.Text.Encoding]::ASCII,
            256,
            $true
        )
        try {
            $writer.AutoFlush = $true
            # F12 has no console character. A PASS marker can therefore only
            # originate from the enumerated USB HID report path and cannot be
            # confused with the simultaneous legacy PS/2 keyboard event.
            $writer.WriteLine('sendkey f12 1')
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $client.Dispose()
    }
}

$diskPathSpecified = $PSBoundParameters.ContainsKey('DiskImagePath')
$diskMode = $UseDiskImage -or $diskPathSpecified
$scratchPathSpecified =
    $PSBoundParameters.ContainsKey('WritableScratchDiskPath')
$installerPathSpecified =
    $PSBoundParameters.ContainsKey('InstallerDiskPath')
$InstallerIsoImage = Join-Path $BuildDir 'images\KuroganeOS-installer.iso'

if ($MinimumRuntimeSeconds -ge $TimeoutSeconds) {
    throw 'MinimumRuntimeSeconds must be smaller than TimeoutSeconds.'
}

$requiredInputs = @($Qemu, $Firmware, $FirmwareVars)
if ($diskMode -and $UseIso) {
    throw 'UseDiskImage and UseIso are mutually exclusive.'
}
if ($InstallerTest -and
    ($diskMode -or $UseIso -or $UsbTest -or $ShellTest -or $SocketTest -or
     $SafeMode -or $DesktopMode)) {
    throw 'InstallerTest is a dedicated writable-disk scenario.'
}
if ($InstallerTest -and -not $installerPathSpecified) {
    throw 'InstallerTest requires -InstallerDiskPath.'
}
if ($installerPathSpecified -and -not $InstallerTest) {
    throw 'InstallerDiskPath is valid only with -InstallerTest.'
}
if ($SafeMode -and $DesktopMode) {
    throw 'SafeMode and DesktopMode are mutually exclusive.'
}
if ($UsbTest -and ($ShellTest -or $SafeMode -or $DesktopMode)) {
    throw 'UsbTest is a dedicated hardware scenario and cannot be combined with ShellTest, SafeMode, or DesktopMode.'
}
if ($SocketTest -and ($SafeMode -or $DesktopMode -or $UsbTest)) {
    throw 'SocketTest cannot be combined with SafeMode, DesktopMode, or UsbTest.'
}
if ($SocketTest) { $ShellTest = $true }
if ($Headless -and $Display) {
    throw 'Headless and Display are mutually exclusive.'
}
if ($WritableDiskImage -and -not $diskPathSpecified) {
    throw 'WritableDiskImage requires an explicit -DiskImagePath; the default repository image is never made writable implicitly.'
}
if ($WritableDiskImage -and -not $diskMode) {
    throw 'WritableDiskImage is valid only for disk-image boot.'
}
if ($DebugWait -and -not $KeepRunning) {
    throw 'DebugWait requires KeepRunning so the paused VM is not terminated by the runner.'
}
if ($DebugWait -and ($ShellTest -or $UsbTest -or $SafeMode -or $DesktopMode)) {
    throw 'DebugWait cannot be combined with interactive test modes because the CPU starts paused.'
}
$SystemDiskImage = $null
$InstallerTargetImage = $null
if ($InstallerTest) {
    $InstallerTargetImage = Resolve-ExistingDiskImage `
        -Path $InstallerDiskPath `
        -Description 'Disposable installer target image' `
        -RequireWritable $true
    $testDiskDirectory = Join-Path $BuildDir 'test-disks'
    if (-not (Test-PathInsideDirectory `
            -Path $InstallerTargetImage -Directory $testDiskDirectory)) {
        throw "Installer target must be below $testDiskDirectory"
    }
    if ((Get-Item -LiteralPath $InstallerTargetImage).Length -lt 536870912) {
        throw 'Installer target must be at least 512 MiB.'
    }
    $probe = [System.IO.File]::Open(
        $InstallerTargetImage, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $probeBytes = New-Object byte[] 1048576
        $read = $probe.Read($probeBytes, 0, $probeBytes.Length)
        if ($read -ne $probeBytes.Length -or
            ($probeBytes | Where-Object { $_ -ne 0 } | Select-Object -First 1)) {
            throw 'Installer target is not a blank disposable image.'
        }
    }
    finally { $probe.Dispose() }
    $requiredInputs += $InstallerIsoImage
} elseif ($diskMode) {
    $candidate = if ($diskPathSpecified) { $DiskImagePath } else { $DiskImage }
    $SystemDiskImage = Resolve-ExistingDiskImage `
        -Path $candidate `
        -Description 'System disk image' `
        -RequireWritable $WritableDiskImage.IsPresent
} elseif ($UseIso) {
    $requiredInputs += $IsoImage
} else {
    $requiredInputs += @($Bootloader, $Kernel)
}

$ScratchDiskImage = $null
if ($scratchPathSpecified) {
    if (-not $diskMode) {
        throw 'WritableScratchDiskPath requires disk-image boot so SATA port 0 is reserved for the selected system image.'
    }
    $ScratchDiskImage = Resolve-ExistingDiskImage `
        -Path $WritableScratchDiskPath `
        -Description 'Writable scratch disk image' `
        -RequireWritable $true

    $scratchDirectory = Join-Path $BuildDir 'test-disks'
    if ((Test-PathInsideDirectory -Path $ScratchDiskImage -Directory $RootDir) -and
        -not (Test-PathInsideDirectory `
            -Path $ScratchDiskImage `
            -Directory $scratchDirectory)) {
        throw "A repository-local scratch disk is permitted only below $scratchDirectory; this protects source and boot artifacts from guest writes."
    }

    $protectedPaths = @($Qemu, $Firmware, $FirmwareVars, $IsoImage,
        $Bootloader, $Kernel, $DiskImage)
    foreach ($protectedPath in $protectedPaths) {
        $protectedFullPath = [System.IO.Path]::GetFullPath($protectedPath)
        if (Test-SameFilePath -Left $ScratchDiskImage -Right $protectedFullPath) {
            throw "Writable scratch disk must be a separate disposable/data image, not a repository boot input: $ScratchDiskImage"
        }
    }
    if ($SystemDiskImage -and
        (Test-SameFilePath -Left $ScratchDiskImage -Right $SystemDiskImage)) {
        throw 'System disk image and writable scratch disk must be different files.'
    }
}
foreach ($required in $requiredInputs) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing QEMU smoke-test input: $required"
    }
}

$expectFoundationRoot = $false
if ($SystemDiskImage) {
    $knownFoundationImages = @(
        (Join-Path $BuildDir 'images\KuroganeOS-base.img'),
        (Join-Path $RootDir 'state\KuroganeOS.img')
    )
    foreach ($knownImage in $knownFoundationImages) {
        if (Test-SameFilePath `
                -Left $SystemDiskImage `
                -Right ([System.IO.Path]::GetFullPath($knownImage))) {
            $expectFoundationRoot = $true
            break
        }
    }
}

[System.IO.Directory]::CreateDirectory($BuildDir) | Out-Null
[System.IO.Directory]::CreateDirectory($LogDir) | Out-Null
foreach ($log in @($SerialLog, $StdoutLog, $StderrLog, $XhciTraceLog)) {
    if (Test-Path -LiteralPath $log) {
        Remove-Item -LiteralPath $log -Force
    }
}

$arguments = @(
    '-machine', 'q35',
    '-m', "$($MemoryMiB)M",
    '-smp', '1',
    '-cpu', 'max',
    '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$Firmware",
    '-drive', "if=pflash,format=raw,unit=1,snapshot=on,file=$FirmwareVars",
    '-serial', "file:$SerialLog",
    '-netdev', 'user,id=kurogane_net',
    '-device', 'e1000,netdev=kurogane_net,mac=52:54:00:4b:55:01',
    '-no-reboot',
    '-no-shutdown'
)
if ($UsbTest) {
    $arguments += @(
        '-device', 'qemu-xhci,id=kurogane_xhci',
        '-device', 'usb-kbd,bus=kurogane_xhci.0',
        '-trace', "enable=usb_xhci_*,file=$XhciTraceLog"
    )
}
$quotedSystemDisk = if ($SystemDiskImage) {
    Format-QemuDriveFile -Path $SystemDiskImage
} else {
    $null
}
if ($InstallerTest) {
    $quotedInstallerTarget = Format-QemuDriveFile -Path $InstallerTargetImage
    $quotedInstallerIso = Format-QemuDriveFile -Path $InstallerIsoImage
    $arguments += @(
        '-drive', "if=none,id=kurogane_install_target,format=raw,file=$quotedInstallerTarget,snapshot=off,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_install_target,bus=ide.0,bootindex=20',
        '-drive', "if=none,id=kurogane_installer_cd,media=cdrom,readonly=on,file=$quotedInstallerIso",
        '-device', 'ide-cd,drive=kurogane_installer_cd,bus=ide.1,bootindex=1',
        '-boot', 'order=d,once=d'
    )
} elseif ($diskMode) {
    $systemSnapshot = if ($WritableDiskImage) { 'off' } else { 'on' }
    $arguments += @(
        '-drive', "if=none,id=kurogane_system,format=raw,file=$quotedSystemDisk,snapshot=$systemSnapshot,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1'
    )
} elseif ($UseIso) {
    $arguments += @('-cdrom', $IsoImage)
} else {
    $arguments += @(
        '-drive', "format=raw,file=fat:rw:$StageDir"
    )
}
if ($ScratchDiskImage) {
    $quotedScratchDisk = Format-QemuDriveFile -Path $ScratchDiskImage
    $arguments += @(
        '-drive', "if=none,id=kurogane_scratch,format=raw,file=$quotedScratchDisk,snapshot=off,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_scratch,bus=ide.1,bootindex=20'
    )
}
if ($ShellTest -or $SafeMode -or $DesktopMode -or $UsbTest -or
    $InstallerTest) {
    $arguments += @(
        '-monitor',
        "tcp:127.0.0.1:$MonitorPort,server=on,wait=off"
    )
} else {
    $arguments += @('-monitor', 'none')
}
if ($Headless) {
    $arguments += @('-display', 'none')
}
if ($DebugWait) {
    $arguments += @(
        '-S',
        '-gdb', "tcp:127.0.0.1:$GdbPort"
    )
}

Write-Host "[qemu] $Qemu"
Write-Host "[firmware] $Firmware"
if ($InstallerTest) {
    Write-Host "[installer-iso] $InstallerIsoImage"
    Write-Warning 'Only the disposable QEMU target below build/test-disks is writable.'
    Write-Host "[installer-target] $InstallerTargetImage"
} elseif ($diskMode) {
    Write-Host "[image] $SystemDiskImage"
    if ($WritableDiskImage) {
        Write-Warning 'The selected system image is attached writable; guest sector writes modify this exact file.'
        Write-Host '[image-mode] writable (snapshot=off)'
    }
    else {
        Write-Host '[image-mode] protected snapshot (backing file is not modified)'
    }
} elseif ($UseIso) {
    Write-Host "[iso] $IsoImage"
} else {
    Write-Host "[stage] $StageDir"
}
if ($ScratchDiskImage) {
    Write-Warning 'The scratch/data disk is attached writable; guest sector writes modify this exact file.'
    Write-Host "[scratch] $ScratchDiskImage"
    Write-Host '[scratch-mode] writable (snapshot=off, SATA port 1)'
}
if ($DebugWait) {
    Write-Host "[gdb] tcp:127.0.0.1:$GdbPort (CPU starts paused)"
}

$process = $null
$success = $false
$commandsSent = $false
$bootModeKeySent = $false
$installerIndexSent = $false
$installerConfirmationSent = $false
$installerModeSent = $false
$installerLanguageSent = $false
$installerUsernameSent = $false
$installerPasswordModeSent = $false
$startedAt = [DateTime]::UtcNow
try {
    $windowStyle = if ($Headless) { 'Hidden' } else { 'Normal' }
    $process = Start-Process `
        -FilePath $Qemu `
        -ArgumentList $arguments `
        -RedirectStandardOutput $StdoutLog `
        -RedirectStandardError $StderrLog `
        -WindowStyle $windowStyle `
        -PassThru

    if ($DebugWait) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) {
            throw 'QEMU exited before the debugger could attach.'
        }
        Write-Host "[running] QEMU PID $($process.Id), waiting for GDB on port $GdbPort"
        exit 0
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process.HasExited) {
            break
        }
        if (Test-Path -LiteralPath $SerialLog) {
            $serial = Get-Content -LiteralPath $SerialLog -Raw -ErrorAction SilentlyContinue
            if ($InstallerTest) {
                if (-not $installerModeSent -and
                    $serial -match 'installer: select setup mode:') {
                    Invoke-MonitorKey -Port $MonitorPort -Key 'down'
                    $installerModeSent = $true
                }
                if (-not $installerLanguageSent -and
                    $serial -match 'installer: select language:') {
                    Invoke-MonitorKey -Port $MonitorPort -Key 'ret'
                    $installerLanguageSent = $true
                }
                if (-not $installerUsernameSent -and
                    $serial -match 'installer: enter username:') {
                    Invoke-MonitorKey -Port $MonitorPort -Key 'ret'
                    $installerUsernameSent = $true
                }
                if (-not $installerPasswordModeSent -and
                    $serial -match 'installer: select password mode:') {
                    Invoke-MonitorKey -Port $MonitorPort -Key 'ret'
                    $installerPasswordModeSent = $true
                }
                if (-not $installerIndexSent -and
                    $serial -match 'installer: select target disk index:') {
                    Invoke-MonitorKey -Port $MonitorPort -Key 'ret'
                    $installerIndexSent = $true
                }
                if (-not $installerConfirmationSent -and
                    $serial -match 'installer: type INSTALL to confirm:') {
                    Invoke-MonitorText -Port $MonitorPort -Text 'INSTALL'
                    $installerConfirmationSent = $true
                }
                if ($serial -match '\[TEST\] installer_complete: PASS') {
                    if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge
                        $MinimumRuntimeSeconds) {
                        $success = $true
                        break
                    }
                }
                if ($serial -match '\[TEST\] installer_complete: FAIL') {
                    break
                }
                Start-Sleep -Milliseconds 100
                continue
            }
            if (($SafeMode -or $DesktopMode -or $SocketTest) -and
                -not $bootModeKeySent -and
                $serial -match 'Press S or F8 for Safe Mode.*(C for Console, )?D to continue now') {
                Invoke-BootModeKey `
                    -Port $MonitorPort `
                    -Key $(if ($SafeMode) { 's' } elseif ($SocketTest) { 'c' } else { 'd' })
                $bootModeKeySent = $true
            }
            if ($DesktopMode -and
                $serial -match '(?m)^boot=console\r?$') {
                break
            }
            if ($SafeMode -and
                $serial -match '(?m)^boot=desktop\r?$') {
                break
            }
            if ($DesktopMode -and
                $serial -match '\[TEST\] userspace_desktop_session: PASS') {
                if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge
                    $MinimumRuntimeSeconds) {
                    $success = $true
                    break
                }
            }
            $promptPattern = if ($SocketTest) {
                '(?m)^KRG:[^\r\n]* > '
            } elseif ($SafeMode) {
                'kurogane:/ \$'
            } else {
                'kurogane:user\$ '
            }
            if ($serial -match $promptPattern) {
                if ($UsbTest) {
                    if (-not $commandsSent) {
                        Invoke-UsbKeyboardTest -Port $MonitorPort
                        $commandsSent = $true
                    }
                    if ($serial -match
                        '\[TEST\] xhci_keyboard_enumeration: PASS' -and
                        $serial -match
                        '\[TEST\] usb_hid_keyboard_input: PASS') {
                        if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge
                            $MinimumRuntimeSeconds) {
                            $success = $true
                            break
                        }
                    }
                    Start-Sleep -Milliseconds 100
                    continue
                }
                if (-not $ShellTest) {
                    $bootReady = -not $SafeMode -or
                        $serial -match 'SAFE MODE: minimal drivers'
                    if ($bootReady -and
                        ([DateTime]::UtcNow - $startedAt).TotalSeconds -ge
                            $MinimumRuntimeSeconds) {
                        $success = $true
                        break
                    }
                    Start-Sleep -Milliseconds 100
                    continue
                }
                if (-not $commandsSent) {
                    Invoke-ShellKeyboardTest `
                        -Port $MonitorPort `
                        -Safe $SafeMode.IsPresent `
                        -Desktop $DesktopMode.IsPresent `
                        -Socket $SocketTest.IsPresent
                    $commandsSent = $true
                }
                if (Test-ShellOutput `
                        -Serial $serial `
                        -Safe $SafeMode.IsPresent `
                        -Desktop $DesktopMode.IsPresent `
                        -Socket $SocketTest.IsPresent `
                        -Scratch ($null -ne $ScratchDiskImage) `
                        -FoundationRoot $expectFoundationRoot) {
                    if (([DateTime]::UtcNow - $startedAt).TotalSeconds -ge
                        $MinimumRuntimeSeconds) {
                        $success = $true
                        break
                    }
                }
            }
            if ($serial -match 'KERNEL (EXCEPTION|PANIC)|fatal:') {
                break
            }
        }
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }

    if ($KeepRunning -and -not $process.HasExited) {
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
    Write-Host '--- serial ---'
    Get-Content -LiteralPath $SerialLog
}
if (-not $success -and (Test-Path -LiteralPath $StderrLog)) {
    Write-Host '--- qemu stderr ---'
    Get-Content -LiteralPath $StderrLog
}

if (-not $success) {
    if ($InstallerTest) {
        throw "KuroganeOS installer did not complete within $TimeoutSeconds seconds."
    }
    if ($UsbTest) {
        throw "KuroganeOS xHCI/USB HID hardware test did not pass within $TimeoutSeconds seconds."
    }
    if ($ShellTest) {
        if ($SocketTest) {
            throw "KuroganeOS Ring-3 socket probe did not pass within $TimeoutSeconds seconds."
        }
        throw "KuroganeOS shell integration test did not pass within $TimeoutSeconds seconds."
    }
    throw "KuroganeOS did not reach the shell prompt within $TimeoutSeconds seconds."
}

if ($InstallerTest) {
    Write-Host '[pass] KuroganeOS installed to the disposable QEMU disk.'
} elseif ($UsbTest) {
    Write-Host '[pass] KuroganeOS xHCI and USB HID hardware test passed.'
} elseif ($ShellTest) {
    if ($SocketTest) {
        Write-Host '[pass] KuroganeOS Ring-3 socket probe passed.'
        return
    }
    Write-Host '[pass] KuroganeOS keyboard and shell integration test passed.'
} else {
    Write-Host '[pass] KuroganeOS reached the interactive shell prompt.'
}
