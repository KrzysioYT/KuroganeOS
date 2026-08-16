[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'test')]
    [string]$Configuration = 'release',
    [switch]$Rebuild,
    [switch]$VirtualBoxSmoke
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$WindowsBuildFilesUrl = 'https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing'
$Toolchain = Join-Path $RootDir 'tools\compiler\x86_64-elf\bin\x86_64-elf-g++.exe'

if (-not (Test-Path -LiteralPath $Toolchain -PathType Leaf)) {
    throw "Windows build toolchain is missing.`nRequired files: $WindowsBuildFilesUrl`nDownload and copy/extract them into the KuroganeOS repository root."
}
if ($null -eq (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL is required by the current Windows image/ISO tooling.'
}

$BuildScript = Join-Path $PSScriptRoot 'build.ps1'
if ($Rebuild) {
    & $BuildScript -Configuration $Configuration -Rebuild
} else {
    & $BuildScript -Configuration $Configuration
}
if (-not $?) { throw 'KuroganeOS Windows build failed.' }

$VersionHeader = Join-Path $RootDir 'common\version.h'
$versionText = Get-Content -LiteralPath $VersionHeader -Raw
if ($versionText -notmatch '#define\s+KUROGANE_VERSION_STRING\s+"([^"]+)"') {
    throw "Cannot read KuroganeOS version from $VersionHeader"
}
$Version = $Matches[1]
$BaseImage = Join-Path $RootDir 'build\images\KuroganeOS-base.img'
$Package = Join-Path $RootDir 'build\install.pkg'
$Iso = Join-Path $RootDir "dist\KuroganeOS-$Version-x86_64.iso"
$Dist = Join-Path $RootDir 'dist'
$Image = Join-Path $Dist "KuroganeOS-$Version-windows-qemu.img"
$Checksums = Join-Path $Dist 'SHA256SUMS.txt'
$InjectScript = Join-Path $PSScriptRoot 'inject-install-package.sh'

foreach ($required in @($BaseImage, $Package, $Iso, $InjectScript)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing media input: $required"
    }
}

function Convert-ToWslPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $converted = & wsl.exe --exec wslpath -a -u ([System.IO.Path]::GetFullPath($Path))
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Cannot convert path for WSL: $Path"
    }
    return $converted.Trim()
}

[System.IO.Directory]::CreateDirectory($Dist) | Out-Null
Copy-Item -LiteralPath $BaseImage -Destination $Image -Force
& wsl.exe bash `
    (Convert-ToWslPath $InjectScript) `
    (Convert-ToWslPath $Image) `
    (Convert-ToWslPath $Package)
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to inject install.pkg into the Windows QEMU IMG.'
}

# The installer ISO builder already runs the mandatory 20-pass structural UEFI
# verifier. For release qualification on an x86-64 Windows host, this optional
# switch adds a second, independent gate: Oracle VirtualBox must actually boot
# the optical media and emit a KuroganeOS kernel marker on COM1.
if ($VirtualBoxSmoke) {
    & (Join-Path $PSScriptRoot 'smoke-virtualbox-iso.ps1') -Iso $Iso
    if (-not $?) { throw 'Real VirtualBox ISO smoke boot failed.' }
}

$imageHash = (Get-FileHash -LiteralPath $Image -Algorithm SHA256).Hash.ToLowerInvariant()
$isoHash = (Get-FileHash -LiteralPath $Iso -Algorithm SHA256).Hash.ToLowerInvariant()
$lines = @(
    "$imageHash  $([System.IO.Path]::GetFileName($Image))",
    "$isoHash  $([System.IO.Path]::GetFileName($Iso))"
)
[System.IO.File]::WriteAllLines(
    $Checksums, $lines, (New-Object System.Text.UTF8Encoding($false)))
[System.IO.File]::WriteAllText(
    "$Image.sha256",
    $lines[0] + [Environment]::NewLine,
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host "[media-windows] KuroganeOS $Version"
Write-Host "[media-windows] live/setup IMG: $Image"
Write-Host "[media-windows] live/setup ISO: $Iso"
Write-Host '[media-windows] ISO structural UEFI verification: 20/20 required'
if ($VirtualBoxSmoke) {
    Write-Host '[media-windows] real VirtualBox EFI optical boot: PASS'
}
Write-Host '[media-windows] both media enter Try / Install setup'
Write-Host "[media-windows] checksums: $Checksums"
