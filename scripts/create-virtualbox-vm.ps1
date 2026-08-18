[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Iso,
    [string]$Name,
    [string]$Disk,
    [ValidateRange(1024, 1048576)]
    [int]$DiskSizeMb = 2048,
    [ValidateSet('e1000', 'virtio', 'pcnet')]
    [string]$Nic = 'e1000'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

if ([string]::IsNullOrWhiteSpace($Name)) {
    $versionHeader = Join-Path $RootDir 'common\version.h'
    $version = 'DEV'
    if (Test-Path -LiteralPath $versionHeader -PathType Leaf) {
        $versionText = Get-Content -LiteralPath $versionHeader -Raw
        if ($versionText -match '#define\s+KUROGANE_VERSION_STRING\s+"([^"]+)"') {
            $version = $Matches[1]
        }
    }
    $Name = "KuroganeOS $version"
}

$VBoxManage = Get-Command VBoxManage.exe -ErrorAction SilentlyContinue
if ($null -eq $VBoxManage) {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $VBoxManage = Get-Item -LiteralPath $candidate
    } else {
        throw 'VBoxManage.exe was not found. Install Oracle VirtualBox first.'
    }
}
$VBox = $VBoxManage.Source
$Iso = [System.IO.Path]::GetFullPath($Iso)
if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "ISO not found: $Iso"
}
if ([System.IO.Path]::GetExtension($Iso) -ne '.iso') {
    throw "VirtualBox optical boot requires the KuroganeOS .iso, not an .img: $Iso"
}

& $VBox showvminfo $Name *> $null
if ($LASTEXITCODE -eq 0) {
    throw "VirtualBox VM already exists: $Name"
}

if ([string]::IsNullOrWhiteSpace($Disk)) {
    $base = Join-Path $HOME "VirtualBox VMs\$Name"
    [System.IO.Directory]::CreateDirectory($base) | Out-Null
    $Disk = Join-Path $base 'KuroganeOS.vdi'
} else {
    $Disk = [System.IO.Path]::GetFullPath($Disk)
    [System.IO.Directory]::CreateDirectory(
        [System.IO.Path]::GetDirectoryName($Disk)) | Out-Null
}

$nicType = switch ($Nic) {
    'e1000' { '82540EM' }
    'virtio' { 'virtio' }
    'pcnet' { 'Am79C973' }
    default { throw "Unsupported NIC profile: $Nic" }
}

& $VBox createvm --name $Name --register | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'VirtualBox createvm failed.' }
$created = $true
try {
    & $VBox modifyvm $Name `
        --memory 1024 --cpus 2 --firmware efi64 --ioapic on `
        --boot1 dvd --boot2 disk --boot3 none --boot4 none `
        --graphicscontroller vboxsvga --vram 64 `
        --keyboard ps2 --mouse ps2 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'VirtualBox base VM configuration failed.' }

    & $VBox modifyvm $Name `
        --nic1 nat --nic-type1 $nicType --cable-connected1 on *> $null
    if ($LASTEXITCODE -ne 0) {
        & $VBox modifyvm $Name `
            --nic1 nat --nictype1 $nicType --cableconnected1 on | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "VirtualBox network setup failed for NIC profile '$Nic' ($nicType)." }
    }

    & $VBox modifyvm $Name `
        --audio-enabled on --audio-controller ac97 --audio-out on *> $null
    if ($LASTEXITCODE -ne 0) {
        & $VBox modifyvm $Name --audio on --audiocontroller ac97 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'VirtualBox AC97 audio setup failed.' }
    }

    if (-not (Test-Path -LiteralPath $Disk -PathType Leaf)) {
        & $VBox createmedium disk --filename $Disk --size $DiskSizeMb --format VDI | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'VirtualBox VDI creation failed.' }
    }

    & $VBox storagectl $Name --name SATA --add sata --controller IntelAHCI | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'VirtualBox SATA controller creation failed.' }
    & $VBox storageattach $Name --storagectl SATA --port 0 --device 0 `
        --type hdd --medium $Disk | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'VirtualBox disk attach failed.' }

    & $VBox storagectl $Name --name IDE --add ide --controller PIIX4 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'VirtualBox IDE controller creation failed.' }
    & $VBox storageattach $Name --storagectl IDE --port 0 --device 0 `
        --type dvddrive --medium $Iso | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'VirtualBox ISO attach failed.' }

    & $VBox setextradata $Name VBoxInternal2/EfiGraphicsResolution 1280x800 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'VirtualBox EFI resolution setup failed.' }

    $created = $false
} finally {
    if ($created) {
        & $VBox unregistervm $Name --delete *> $null
    }
}

Write-Host "Created VirtualBox VM: $Name"
Write-Host "ISO: $Iso"
Write-Host "Disk: $Disk"
Write-Host "NIC: $Nic ($nicType), NAT"
Write-Host 'Firmware: EFI64; Boot order: DVD -> Disk'
Write-Host "Start with: VBoxManage startvm `"$Name`""
