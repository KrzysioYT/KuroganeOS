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
$ReleaseName = "KuroganeOS-$Version-x86_64.iso"
$ReleaseIso = Join-Path $DistDir $ReleaseName
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

# Build the same 30 MiB FAT16 El Torito image used on Linux/macOS. The shared
# bridge also handles repositories located on secondary/removable drives such
# as D: or I: by mounting that Windows drive through WSL DrvFs when needed.
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
if ($LASTEXITCODE -ne 0) { throw 'Installer ISO construction/verification failed.' }

Copy-Item -LiteralPath $IsoImage -Destination $ReleaseIso -Force
Copy-Item -LiteralPath $IsoImage -Destination $CompatibilityIso -Force

$hash = (Get-FileHash -LiteralPath $ReleaseIso -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumLine = "$hash  $ReleaseName"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText(
    $ChecksumFile,
    $checksumLine + [Environment]::NewLine,
    $utf8NoBom)

Write-Host "[installer-internal] $IsoImage"
Write-Host "[release] $ReleaseIso"
Write-Host "[compatibility] $CompatibilityIso"
Write-Host "[sha256] $hash"
Write-Host "[checksums] $ChecksumFile"
Write-Host '[virtualbox] ISO passed mandatory 20-pass UEFI/El Torito verification'
