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

$VBoxCommand = Get-Command VBoxManage.exe -ErrorAction SilentlyContinue
if ($null -ne $VBoxCommand) {
    $VBox = $VBoxCommand.Path
} else {
    $candidate = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $VBox = $candidate
    } else {
        throw 'VBoxManage.exe was not found. Install Oracle VirtualBox first.'
    }
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

$Iso = Resolve-KuroganePath -Path $Iso
if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "ISO not found: $Iso"
}
if ([System.IO.Path]::GetExtension($Iso) -ne '.iso') {
    throw "VirtualBox optical boot requires the KuroganeOS .iso, not an .img: $Iso"
}

$existing = Invoke-VBoxNative -Arguments @('showvminfo', $Name) -AllowFailure
if ($existing.ExitCode -eq 0) {
    throw "VirtualBox VM already exists: $Name"
}

if ([string]::IsNullOrWhiteSpace($Disk)) {
    $base = Join-Path $HOME "VirtualBox VMs\$Name"
    [System.IO.Directory]::CreateDirectory($base) | Out-Null
    $Disk = Join-Path $base 'KuroganeOS.vdi'
} else {
    $Disk = Resolve-KuroganePath -Path $Disk
    [System.IO.Directory]::CreateDirectory(
        [System.IO.Path]::GetDirectoryName($Disk)) | Out-Null
}

$nicType = switch ($Nic) {
    'e1000' { '82540EM' }
    'virtio' { 'virtio' }
    'pcnet' { 'Am79C973' }
    default { throw "Unsupported NIC profile: $Nic" }
}

$null = Invoke-VBoxNative -Arguments @('createvm', '--name', $Name, '--ostype', 'Other_64', '--register')
$created = $true
try {
    $null = Invoke-VBoxNative -Arguments @(
        'modifyvm', $Name,
        '--memory', '2048', '--cpus', '2', '--firmware', 'efi64', '--ioapic', 'on',
        '--boot1', 'dvd', '--boot2', 'disk', '--boot3', 'none', '--boot4', 'none',
        '--graphicscontroller', 'vboxsvga', '--vram', '64',
        '--keyboard', 'ps2', '--mouse', 'ps2'
    )

    $networkResult = Invoke-VBoxNative -Arguments @(
        'modifyvm', $Name,
        '--nic1', 'nat', '--nic-type1', $nicType, '--cable-connected1', 'on'
    ) -AllowFailure
    if ($networkResult.ExitCode -ne 0) {
        $null = Invoke-VBoxNative -Arguments @(
            'modifyvm', $Name,
            '--nic1', 'nat', '--nictype1', $nicType, '--cableconnected1', 'on'
        )
    }

    $audioResult = Invoke-VBoxNative -Arguments @(
        'modifyvm', $Name,
        '--audio-enabled', 'on', '--audio-controller', 'ac97', '--audio-out', 'on'
    ) -AllowFailure
    if ($audioResult.ExitCode -ne 0) {
        $null = Invoke-VBoxNative -Arguments @(
            'modifyvm', $Name, '--audio', 'on', '--audiocontroller', 'ac97'
        )
    }

    if (-not (Test-Path -LiteralPath $Disk -PathType Leaf)) {
        $null = Invoke-VBoxNative -Arguments @(
            'createmedium', 'disk', '--filename', $Disk,
            '--size', ([string]$DiskSizeMb), '--format', 'VDI'
        )
    }

    # One VDI needs one implemented SATA port. Keeping portcount bounded avoids
    # wasting KuroganeOS AHCI link-probe time on empty VirtualBox ports.
    $null = Invoke-VBoxNative -Arguments @(
        'storagectl', $Name, '--name', 'SATA', '--add', 'sata',
        '--controller', 'IntelAHCI', '--portcount', '1'
    )
    $null = Invoke-VBoxNative -Arguments @(
        'storageattach', $Name, '--storagectl', 'SATA', '--port', '0', '--device', '0',
        '--type', 'hdd', '--medium', $Disk
    )

    $null = Invoke-VBoxNative -Arguments @(
        'storagectl', $Name, '--name', 'IDE', '--add', 'ide', '--controller', 'PIIX4'
    )
    $null = Invoke-VBoxNative -Arguments @(
        'storageattach', $Name, '--storagectl', 'IDE', '--port', '0', '--device', '0',
        '--type', 'dvddrive', '--medium', $Iso
    )

    $null = Invoke-VBoxNative -Arguments @(
        'setextradata', $Name, 'VBoxInternal2/EfiGraphicsResolution', '1280x800'
    )

    $info = Invoke-VBoxNative -Arguments @('showvminfo', $Name, '--machinereadable')
    $machine = $info.StdOut
    if ($machine -notmatch 'firmware="EFI64"' -or
        $machine -notmatch 'storagecontrollername\d+="SATA"' -or
        $machine -notmatch 'storagecontrollertype\d+="IntelAhci"|storagecontrollertype\d+="IntelAHCI"' -or
        $machine -notmatch '"SATA-0-0"=.*\.vdi"') {
        throw 'Created VM did not retain the required EFI64 + IntelAHCI/SATA disk contract.'
    }

    $created = $false
} finally {
    if ($created) {
        $null = Invoke-VBoxNative -Arguments @('unregistervm', $Name, '--delete') -AllowFailure
    }
}

Write-Host "Created VirtualBox VM: $Name"
Write-Host "ISO: $Iso"
Write-Host "Disk: $Disk"
Write-Host "NIC: $Nic ($nicType), NAT"
Write-Host 'Storage: IntelAHCI/SATA port 0 -> VDI; IDE/PIIX4 -> ISO DVD'
Write-Host 'Firmware: EFI64; Boot order: DVD -> Disk'
Write-Host "Start with: VBoxManage startvm `"$Name`""
