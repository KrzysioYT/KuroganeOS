[CmdletBinding()]
param(
    [ValidateRange(1, 120)]
    [int]$TimeoutSeconds = 12,
    [switch]$KeepRunning,
    [switch]$Display,
    [switch]$Headless,
    [switch]$ShellTest,
    [switch]$SafeMode,
    [switch]$UseDiskImage,
    [switch]$UseIso,
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

function Get-HmpKeyName {
    param([char]$Character)

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

function Invoke-ShellKeyboardTest {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Port,
        [bool]$Safe
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
            $commands = if ($Safe) {
                @(
                    'pwd',
                    'cd /home',
                    'pwd',
                    'cat readme.txt',
                    'free',
                    'ps',
                    'whoami',
                    'echo safemodepass'
                )
            }
            else {
                @(
                    'version',
                    'abi',
                    'free',
                    'pwd',
                    'cd home',
                    'pwd',
                    'mkdir smoke',
                    'write smoke/source.txt shelltestdata',
                    'cp smoke/source.txt smoke/copy.txt',
                    'mv smoke/copy.txt smoke/moved.txt',
                    'cat smoke/moved.txt',
                    'stat smoke/moved.txt',
                    'mkdir /tmp/remove',
                    'rmdir /tmp/remove',
                    'whoami',
                    'uptime',
                    'net ping',
                    'apps',
                    'gui',
                    'history',
                    'echo shelltestpass'
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
        [bool]$Safe
    )

    $patterns = if ($Safe) {
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
    else {
        @(
            'KuroganeOS 1\.0 x86_64 UEFI',
            'application ABI 1\.0 descriptor=48 page=4096 features=0x0',
            'transport: unavailable \(ring-3/syscalls not implemented\)',
            'heap total=',
            '(?m)^/home\r?$',
            '(?m)^shelltestdata\r?$',
            '/home/smoke/moved\.txt type=file size=13',
            '(?m)^kernel\r?$',
            '(?m)^[1-9][0-9]* ticks\r?$',
            'reply from 127\.0\.0\.1',
            'desktop - graphical application launcher',
            'KuroganeOS application closed\.',
            '(?m)^shelltestpass\r?$'
        )
    }
    foreach ($pattern in $patterns) {
        if ($Serial -notmatch $pattern) {
            return $false
        }
    }
    return $true
}

function Invoke-SafeModeKey {
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
            $writer.WriteLine('sendkey s 1')
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $client.Dispose()
    }
}

$requiredInputs = @($Qemu, $Firmware, $FirmwareVars)
if ($UseDiskImage -and $UseIso) {
    throw 'UseDiskImage and UseIso are mutually exclusive.'
}
if ($UseDiskImage) {
    $requiredInputs += $DiskImage
} elseif ($UseIso) {
    $requiredInputs += $IsoImage
} else {
    $requiredInputs += @($Bootloader, $Kernel)
}
foreach ($required in $requiredInputs) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing QEMU smoke-test input: $required"
    }
}

[System.IO.Directory]::CreateDirectory($BuildDir) | Out-Null
[System.IO.Directory]::CreateDirectory($LogDir) | Out-Null
foreach ($log in @($SerialLog, $StdoutLog, $StderrLog)) {
    if (Test-Path -LiteralPath $log) {
        Remove-Item -LiteralPath $log -Force
    }
}

$arguments = @(
    '-machine', 'q35',
    '-m', "$($MemoryMiB)M",
    '-cpu', 'max',
    '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$Firmware",
    '-drive', "if=pflash,format=raw,unit=1,snapshot=on,file=$FirmwareVars",
    '-serial', "file:$SerialLog",
    '-nic', 'none',
    '-no-reboot',
    '-no-shutdown'
)
if ($UseDiskImage) {
    $arguments += @(
        '-drive', "format=raw,file=$DiskImage,snapshot=on"
    )
} elseif ($UseIso) {
    $arguments += @('-cdrom', $IsoImage)
} else {
    $arguments += @(
        '-drive', "format=raw,file=fat:rw:$StageDir"
    )
}
if ($ShellTest -or $SafeMode) {
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

Write-Host "[qemu] $Qemu"
Write-Host "[firmware] $Firmware"
if ($UseDiskImage) {
    Write-Host "[image] $DiskImage"
} elseif ($UseIso) {
    Write-Host "[iso] $IsoImage"
} else {
    Write-Host "[stage] $StageDir"
}

$process = $null
$success = $false
$commandsSent = $false
$safeKeySent = $false
try {
    $windowStyle = if ($Headless) { 'Hidden' } else { 'Normal' }
    $process = Start-Process `
        -FilePath $Qemu `
        -ArgumentList $arguments `
        -RedirectStandardOutput $StdoutLog `
        -RedirectStandardError $StderrLog `
        -WindowStyle $windowStyle `
        -PassThru

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process.HasExited) {
            break
        }
        if (Test-Path -LiteralPath $SerialLog) {
            $serial = Get-Content -LiteralPath $SerialLog -Raw -ErrorAction SilentlyContinue
            if ($SafeMode -and -not $safeKeySent -and
                $serial -match 'KuroganeOS loader') {
                Invoke-SafeModeKey -Port $MonitorPort
                $safeKeySent = $true
            }
            if ($serial -match 'kurogane:/ \$') {
                if (-not $ShellTest) {
                    $success = -not $SafeMode -or
                        $serial -match 'SAFE MODE: minimal drivers'
                    break
                }
                if (-not $commandsSent) {
                    Invoke-ShellKeyboardTest `
                        -Port $MonitorPort `
                        -Safe $SafeMode.IsPresent
                    $commandsSent = $true
                }
                if (Test-ShellOutput `
                        -Serial $serial `
                        -Safe $SafeMode.IsPresent) {
                    $success = $true
                    break
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
    if ($ShellTest) {
        throw "KuroganeOS shell integration test did not pass within $TimeoutSeconds seconds."
    }
    throw "KuroganeOS did not reach the shell prompt within $TimeoutSeconds seconds."
}

if ($ShellTest) {
    Write-Host '[pass] KuroganeOS keyboard and shell integration test passed.'
} else {
    Write-Host '[pass] KuroganeOS reached the interactive shell prompt.'
}
