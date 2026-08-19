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

function Invoke-VBox {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $output = & $VBox @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage failed ($LASTEXITCODE): $($Arguments -join ' ')`n$($output -join [Environment]::NewLine)"
    }
    return $output
}

function Invoke-VBoxCleanup {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    # VBoxManage writes normal progress (for example 0%...100%) to stderr for
    # operations such as unregistervm --delete. Windows PowerShell converts
    # native stderr into ErrorRecord objects and $ErrorActionPreference='Stop'
    # can therefore throw NativeCommandError even when VBoxManage exits 0.
    # Cleanup is best-effort: suppress both streams and judge the native process
    # only by its exit code. A cleanup problem must never overwrite the actual
    # ISO boot qualification result.
    $previousPreference = $ErrorActionPreference
    $exitCode = -1
    try {
        $ErrorActionPreference = 'SilentlyContinue'
        & $VBox @Arguments 1>$null 2>$null
        $exitCode = $LASTEXITCODE
    } catch {
        $exitCode = -1
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    return $exitCode
}

try {
    Invoke-VBox createvm --name $vm --ostype Other_64 --register *> $null
    $registered = $true

    # Match the supported KuroganeOS VirtualBox machine contract exactly.
    Invoke-VBox modifyvm $vm `
        --memory 2048 --cpus 1 --firmware efi64 --ioapic on `
        --boot1 dvd --boot2 disk --boot3 none --boot4 none `
        --graphicscontroller vboxsvga --vram 64 `
        --keyboard ps2 --mouse ps2 *> $null

    # E1000 + NAT is the qualified VirtualBox network profile.
    $networkArgs = @('modifyvm', $vm, '--nic1', 'nat', '--nic-type1', '82540EM', '--cable-connected1', 'on')
    $networkOutput = & $VBox @networkArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        Invoke-VBox modifyvm $vm `
            --nic1 nat --nictype1 82540EM --cableconnected1 on *> $null
    }

    # Serial is the qualification channel; graphical rendering is not required
    # for the smoke to prove that BOOTX64.EFI and kernel.elf were loaded.
    Invoke-VBox modifyvm $vm --uart1 0x3F8 4 *> $null
    Invoke-VBox modifyvm $vm --uartmode1 file $serial *> $null

    Invoke-VBox createmedium disk --filename $disk --size 1024 --format VDI *> $null
    Invoke-VBox storagectl $vm --name SATA --add sata --controller IntelAHCI *> $null
    Invoke-VBox storageattach $vm --storagectl SATA --port 0 --device 0 `
        --type hdd --medium $disk *> $null
    Invoke-VBox storagectl $vm --name IDE --add ide --controller PIIX4 *> $null
    Invoke-VBox storageattach $vm --storagectl IDE --port 0 --device 0 `
        --type dvddrive --medium $Iso *> $null
    Invoke-VBox setextradata $vm VBoxInternal2/EfiGraphicsResolution 1280x800 *> $null

    $machineInfo = Invoke-VBox showvminfo $vm --machinereadable
    if (-not ($machineInfo -contains 'firmware="EFI64"')) {
        throw 'Qualification VM did not retain firmware="EFI64".'
    }
    if (-not ($machineInfo -contains 'boot1="dvd"')) {
        throw 'Qualification VM did not retain DVD-first boot order.'
    }
    $isoLeaf = [System.IO.Path]::GetFileName($Iso)
    if (-not (($machineInfo -join "`n") -match [regex]::Escape($isoLeaf))) {
        throw 'Qualification VM does not report the requested ISO as attached optical media.'
    }

    Invoke-VBox startvm $vm --type headless *> $null
    $started = $true

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $matched = $false
    do {
        Start-Sleep -Milliseconds 500
        if (Test-Path -LiteralPath $serial -PathType Leaf) {
            $text = Get-Content -LiteralPath $serial -Raw -ErrorAction SilentlyContinue
            if ($null -ne $text -and
                ($text -match 'KuroganeOS kernel entry' -or
                 $text -match '\[TEST\] paging: PASS' -or
                 $text -match 'KUROGANE OS')) {
                $matched = $true
                break
            }
        }
    } while ([DateTime]::UtcNow -lt $deadline)

    if (-not $matched) {
        $tail = ''
        if (Test-Path -LiteralPath $serial -PathType Leaf) {
            $tail = (Get-Content -LiteralPath $serial -Tail 120) -join [Environment]::NewLine
        }
        throw "VirtualBox EFI64 optical boot did not reach the KuroganeOS kernel within $TimeoutSeconds seconds.`n$tail"
    }

    Write-Host "[virtualbox-smoke] ISO: $Iso"
    Write-Host '[virtualbox-smoke] firmware EFI64: PASS'
    Write-Host '[virtualbox-smoke] DVD-first optical attachment: PASS'
    Write-Host '[virtualbox-smoke] BOOTX64.EFI -> kernel serial marker: PASS'
    Write-Host '[virtualbox-smoke] REAL ORACLE VIRTUALBOX BOOT: PASS'
} finally {
    if ($started) {
        $null = Invoke-VBoxCleanup -Arguments @('controlvm', $vm, 'poweroff')
    }
    if ($registered) {
        # Give VBoxSVC a short moment to settle the powered-off machine before
        # unregistering and deleting the temporary VDI. Retry only cleanup; the
        # smoke result above remains authoritative.
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            $cleanupExit = Invoke-VBoxCleanup -Arguments @('unregistervm', $vm, '--delete')
            if ($cleanupExit -eq 0) { break }
            Start-Sleep -Milliseconds 250
        }
    }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
