[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'test')]
    [string]$Configuration = 'debug',
    [switch]$NoBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$WslBridge = Join-Path $PSScriptRoot 'wsl-path.ps1'
if (-not (Test-Path -LiteralPath $WslBridge -PathType Leaf)) {
    throw "Missing Windows/WSL path bridge: $WslBridge"
}
. $WslBridge
Repair-KuroganeShellLineEndings -Directory $PSScriptRoot

$BuildDir = Join-Path $RootDir 'build'
$StageDir = Join-Path $BuildDir 'installer-staging'
$ExpectedStageDir = [System.IO.Path]::GetFullPath(
    (Join-Path $RootDir 'build\installer-staging'))
$ImageDir = Join-Path $BuildDir 'images'
$DistDir = Join-Path $RootDir 'dist'
$EspImage = Join-Path $ImageDir 'KuroganeOS-installer-esp.img'
$IsoImage = Join-Path $ImageDir 'KuroganeOS-installer.iso'
$Package = Join-Path $BuildDir 'install.pkg'
$Efi = Join-Path $BuildDir 'BOOTX64.EFI'
$Kernel = Join-Path $BuildDir 'kernel.elf'
$Overlay = Join-Path $BuildDir 'userspace\rootfs'
$VersionHeader = Join-Path $RootDir 'common\version.h'

if (-not (Test-Path -LiteralPath $VersionHeader -PathType Leaf)) {
    throw "Missing version header: $VersionHeader"
}
$versionText = Get-Content -LiteralPath $VersionHeader -Raw
if ($versionText -notmatch '#define\s+KUROGANE_VERSION_STRING\s+"([^"]+)"') {
    throw "Cannot read KuroganeOS version from $VersionHeader"
}
$Version = $Matches[1]

# Canonical optical release is explicitly VirtualBox-targeted. Do not publish a
# generic x86_64 ISO name: it was too easy to confuse stale experimental media
# with the image qualified against Oracle VirtualBox EFI64.
$ReleaseName = "KuroganeOS-$Version-virtualbox-x86_64.iso"
$ReleaseIso = Join-Path $DistDir $ReleaseName
$LegacyReleaseIso = Join-Path $DistDir "KuroganeOS-$Version-x86_64.iso"
$ChecksumFile = Join-Path $DistDir 'SHA256SUMS.txt'
$CompatibilityIso = Join-Path $RootDir 'kurogane.iso'

if (-not $NoBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') `
        -Configuration $Configuration -NoStage
    if (-not $?) { throw 'Kernel build failed.' }
    & (Join-Path $PSScriptRoot 'build.ps1') `
        -Configuration $Configuration -StageOnly
    if (-not $?) { throw 'Artifact staging failed.' }
}

foreach ($required in @($Efi, $Kernel)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing installer input: $required"
    }
}
if (-not (Test-Path -LiteralPath $Overlay -PathType Container)) {
    throw "Missing generated userspace overlay: $Overlay"
}

& python (Join-Path $PSScriptRoot 'build-install-package.py') `
    --output $Package --efi $Efi --kernel $Kernel `
    --rootfs (Join-Path $RootDir 'rootfs') --overlay $Overlay
if ($LASTEXITCODE -ne 0) { throw 'Installer package construction failed.' }

if ([System.IO.Path]::GetFullPath($StageDir) -ne $ExpectedStageDir) {
    throw "Refusing unexpected installer staging path: $StageDir"
}
if (Test-Path -LiteralPath $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
$BootDir = Join-Path $StageDir 'EFI\BOOT'
[System.IO.Directory]::CreateDirectory($BootDir) | Out-Null
[System.IO.Directory]::CreateDirectory($ImageDir) | Out-Null
[System.IO.Directory]::CreateDirectory($DistDir) | Out-Null
Copy-Item -LiteralPath $Efi -Destination (Join-Path $BootDir 'BOOTX64.EFI')
Copy-Item -LiteralPath $Kernel -Destination (Join-Path $StageDir 'kernel.elf')
Copy-Item -LiteralPath $Kernel -Destination (Join-Path $BootDir 'kernel.elf')
Copy-Item -LiteralPath $Package -Destination (Join-Path $StageDir 'install.pkg')

# Build a bounded FAT EFI System Partition. The VirtualBox ISO builder appends
# this filesystem as GPT ESP #2 and points its EFI El Torito catalog entry at
# that exact partition.
$espScript = Convert-ToKuroganeWslPath (Join-Path $PSScriptRoot 'build-installer-esp.sh')
& wsl.exe bash $espScript `
    (Convert-ToKuroganeWslPath $StageDir) `
    (Convert-ToKuroganeWslPath $EspImage)
if ($LASTEXITCODE -ne 0) { throw 'Installer El Torito ESP construction failed.' }

$isoScript = Convert-ToKuroganeWslPath (Join-Path $PSScriptRoot 'build-installer-iso.sh')
& wsl.exe bash $isoScript `
    (Convert-ToKuroganeWslPath $StageDir) `
    (Convert-ToKuroganeWslPath $EspImage) `
    (Convert-ToKuroganeWslPath $IsoImage)
if ($LASTEXITCODE -ne 0) { throw 'VirtualBox installer ISO construction/verification failed.' }

Copy-Item -LiteralPath $IsoImage -Destination $ReleaseIso -Force
# Keep only a root-level compatibility alias. Remove the old generic versioned
# name so wildcard selection cannot accidentally choose obsolete media.
if (Test-Path -LiteralPath $LegacyReleaseIso -PathType Leaf) {
    Remove-Item -LiteralPath $LegacyReleaseIso -Force
}
Copy-Item -LiteralPath $IsoImage -Destination $CompatibilityIso -Force

$hash = (Get-FileHash -LiteralPath $ReleaseIso -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumLine = "$hash  $ReleaseName"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText(
    $ChecksumFile,
    $checksumLine + [Environment]::NewLine,
    $utf8NoBom)

Write-Host "[installer-internal] $IsoImage"
Write-Host "[virtualbox-release] $ReleaseIso"
Write-Host "[virtualbox-compatibility-alias] $CompatibilityIso"
Write-Host "[sha256] $hash"
Write-Host "[checksums] $ChecksumFile"
Write-Host '[virtualbox] static qualification: GPT ESP #2 + EFI El Torito + AMD64 BOOTX64.EFI PASS'
