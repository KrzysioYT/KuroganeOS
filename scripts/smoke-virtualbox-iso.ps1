[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Iso,
    [ValidateRange(15, 180)]
    [int]$TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$VBoxCommand = Get-Command VBoxManage.exe -ErrorAction SilentlyContinue
if ($null -eq $VBoxCommand) {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw 'VBoxManage.exe not found. Install Oracle VirtualBox before using -VirtualBoxSmoke.'
    }
    $VBox = $candidate
} else {
    $VBox = $VBoxCommand.Source
}

$Iso = [System.IO.Path]::GetFullPath($Iso)
if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "ISO not found: $Iso"
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
    & $VBox @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "VBoxManage failed ($LASTEXITCODE): $($Arguments -join ' ')"
    }
}

try {
    Invoke-VBox createvm --name $vm --register *> $null
    $registered = $true

    Invoke-VBox modifyvm $vm `
        --memory 1024 --cpus 1 --firmware efi64 --ioapic on `
        --boot1 dvd --boot2 disk --boot3 none --boot4 none `
        --graphicscontroller vboxsvga --vram 64 `
        --keyboard ps2 --mouse ps2 *> $null

    & $VBox modifyvm $vm `
        --nic1 nat --nic-type1 82540EM --cable-connected1 on *> $null
    if ($LASTEXITCODE -ne 0) {
        Invoke-VBox modifyvm $vm `
            --nic1 nat --nictype1 82540EM --cableconnected1 on *> $null
    }

    & $VBox modifyvm $vm `
        --audio-enabled on --audio-controller ac97 --audio-out on *> $null
    if ($LASTEXITCODE -ne 0) {
        Invoke-VBox modifyvm $vm --audio on --audiocontroller ac97 *> $null
    }

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
            $tail = (Get-Content -LiteralPath $serial -Tail 100) -join [Environment]::NewLine
        }
        throw "VirtualBox EFI smoke did not reach the KuroganeOS kernel within $TimeoutSeconds seconds.`n$tail"
    }

    Write-Host '[virtualbox-smoke] EFI optical boot: PASS'
    Write-Host '[virtualbox-smoke] BOOTX64.EFI -> kernel serial marker: PASS'
    Write-Host '[virtualbox-smoke] VIRTUALBOX REAL BOOT VERIFIED'
} finally {
    if ($started) {
        & $VBox controlvm $vm poweroff *> $null
    }
    if ($registered) {
        & $VBox unregistervm $vm --delete *> $null
    }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
