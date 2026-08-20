[CmdletBinding()]
param(
    [string]$SourceImage = "build\images\KuroganeOS-base.img",
    [ValidateRange(20, 120)]
    [int]$TimeoutSeconds = 120
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceCandidate = if ([System.IO.Path]::IsPathRooted($SourceImage)) {
    $SourceImage
} else {
    Join-Path $root $SourceImage
}
$source = [System.IO.Path]::GetFullPath($sourceCandidate)
$testDirectory = Join-Path $root 'build\test-images'
$logDirectory = Join-Path $root 'build\logs'
$qemu = Join-Path $root 'tools\qemu\qemu-system-x86_64.exe'
$firmware = Join-Path $root 'tools\qemu\share\edk2-x86_64-code.fd'
$firmwareVars = Join-Path $root 'tools\qemu\share\edk2-i386-vars.fd'
$runId = [Guid]::NewGuid().ToString('N')
$testImage = Join-Path $testDirectory "fat32-persistence-$runId.img"
$prepareLog = Join-Path $logDirectory "qemu-storage-prepare-$runId-serial.log"
$verifyLog = Join-Path $logDirectory "qemu-storage-verify-$runId-serial.log"

foreach ($required in @($source, $qemu, $firmware, $firmwareVars)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Persistence-test input does not exist: $required"
    }
}
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $testImage

Write-Host "[persistence] run id: $runId"
Write-Host "[persistence] source: $source"
Write-Host "[persistence] disposable image: $testImage"

function Format-QemuDriveFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    return '"' + $Path + '"'
}

function Invoke-PersistenceBoot {
    param(
        [Parameter(Mandatory = $true)][string]$Marker,
        [Parameter(Mandatory = $true)][string]$SerialLog,
        [Parameter(Mandatory = $true)][string]$Stage
    )

    $stdoutLog = $SerialLog -replace '-serial\.log$', '-stdout.log'
    $stderrLog = $SerialLog -replace '-serial\.log$', '-stderr.log'
    $quotedImage = Format-QemuDriveFile -Path $testImage
    $arguments = @(
        '-machine', 'q35',
        '-m', '256M',
        '-smp', '1',
        '-cpu', 'max',
        '-drive', "if=pflash,format=raw,unit=0,readonly=on,file=$firmware",
        '-drive', "if=pflash,format=raw,unit=1,snapshot=on,file=$firmwareVars",
        '-serial', "file:$SerialLog",
        '-net', 'none',
        '-no-reboot',
        '-no-shutdown',
        '-drive', "if=none,id=kurogane_system,format=raw,file=$quotedImage,snapshot=off,cache=writeback",
        '-device', 'ide-hd,drive=kurogane_system,bus=ide.0,bootindex=1',
        '-monitor', 'none',
        '-display', 'none'
    )

    Write-Host "[persistence] $Stage boot waiting for: $Marker"
    $process = $null
    $passed = $false
    $failureReason = $null
    try {
        $process = Start-Process `
            -FilePath $qemu `
            -ArgumentList $arguments `
            -RedirectStandardOutput $stdoutLog `
            -RedirectStandardError $stderrLog `
            -WindowStyle Hidden `
            -PassThru

        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            $process.Refresh()
            if ($process.HasExited) {
                $failureReason = "QEMU exited before $Stage persistence marker."
                break
            }
            if (Test-Path -LiteralPath $SerialLog -PathType Leaf) {
                $serial = Get-Content -LiteralPath $SerialLog -Raw -ErrorAction SilentlyContinue
                # QEMU creates the serial file before the guest necessarily
                # writes its first byte. PowerShell returns $null for that
                # transient empty-file state, so treat it as "no progress yet"
                # instead of dereferencing it with .Contains().
                if (-not [string]::IsNullOrEmpty($serial)) {
                    if ($serial.Contains($Marker)) {
                        $passed = $true
                        break
                    }
                    if ($serial -match '(?m)^\[TEST\] fat32_persistence_(prepare|verify): FAIL\r?$') {
                        $failureReason = "KuroganeOS reported a FAT32 persistence failure during $Stage boot."
                        break
                    }
                    if ($serial -match 'KERNEL (EXCEPTION|PANIC)|fatal:') {
                        $failureReason = "KuroganeOS reported a fatal kernel error during $Stage boot."
                        break
                    }
                }
            }
            Start-Sleep -Milliseconds 100
        }
        if (-not $passed -and -not $failureReason) {
            $failureReason = "$Stage persistence marker was not reached within $TimeoutSeconds seconds."
        }
    }
    finally {
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
    }

    if (-not $passed) {
        if (Test-Path -LiteralPath $SerialLog -PathType Leaf) {
            Write-Host '--- persistence serial tail ---'
            Get-Content -LiteralPath $SerialLog -Tail 180
        }
        if (Test-Path -LiteralPath $stderrLog -PathType Leaf) {
            Write-Host '--- qemu stderr ---'
            Get-Content -LiteralPath $stderrLog
        }
        throw $failureReason
    }
}

Invoke-PersistenceBoot `
    -Marker '[TEST] fat32_persistence_prepare: PASS' `
    -SerialLog $prepareLog `
    -Stage 'first'
Write-Host '[persistence] first boot create/write/flush: PASS'

Invoke-PersistenceBoot `
    -Marker '[TEST] fat32_persistence_verify: PASS' `
    -SerialLog $verifyLog `
    -Stage 'second'
Write-Host '[persistence] second boot readback: PASS'

Write-Host '[pass] FAT32 create/write/flush survived a separate QEMU reboot'
Write-Host "[image] $testImage"
Write-Host "[prepare-log] $prepareLog"
Write-Host "[verify-log] $verifyLog"
