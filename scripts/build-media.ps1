[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'test')]
    [string]$Configuration = 'release',
    [switch]$Rebuild,
    [switch]$SkipVirtualBoxSmoke
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$WindowsBuildFilesUrl = 'https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing'
$Toolchain = Join-Path $RootDir 'tools\compiler\x86_64-elf\bin\x86_64-elf-g++.exe'
$WslBridge = Join-Path $PSScriptRoot 'wsl-path.ps1'
$TrustSource = Join-Path $RootDir 'rootfs\etc\ssl\certs.pem'
$TrustOutput = Join-Path $RootDir 'build\userspace\rootfs\etc\ssl\certs.pem'
$TrustVerifier = Join-Path $PSScriptRoot 'verify-trust-store.py'
$FoundationBuilder = Join-Path $PSScriptRoot 'build-foundation-image.ps1'
$InstallerBuilder = Join-Path $PSScriptRoot 'build-installer.ps1'

if (-not (Test-Path -LiteralPath $Toolchain -PathType Leaf)) {
    throw "Windows build toolchain is missing.`nRequired files: $WindowsBuildFilesUrl`nDownload and copy/extract them into the KuroganeOS repository root."
}
if ($null -eq (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL is required by the current Windows image/ISO tooling.'
}
foreach ($requiredScript in @($WslBridge, $TrustVerifier, $FoundationBuilder, $InstallerBuilder)) {
    if (-not (Test-Path -LiteralPath $requiredScript -PathType Leaf)) {
        throw "Missing Windows media helper: $requiredScript"
    }
}
. $WslBridge
Repair-KuroganeShellLineEndings -Directory $PSScriptRoot

$BuildScript = Join-Path $PSScriptRoot 'build.ps1'
if ($Rebuild) {
    & $BuildScript -Configuration $Configuration -Rebuild
} else {
    & $BuildScript -Configuration $Configuration
}
if (-not $?) { throw 'KuroganeOS Windows build failed.' }

# Build media must not inherit security policy from the host. Validate and
# stage the reviewed repository bundle into the userspace overlay.
& python.exe $TrustVerifier $TrustSource
if (-not $?) { throw 'KuroganeOS Web PKI trust-store validation failed.' }
[System.IO.Directory]::CreateDirectory(
    [System.IO.Path]::GetDirectoryName($TrustOutput)) | Out-Null
Copy-Item -LiteralPath $TrustSource -Destination $TrustOutput -Force

& $FoundationBuilder -NoWorkingImage
if (-not $?) { throw 'Foundation image rebuild with KuroganeOS trust roots failed.' }

& $InstallerBuilder -Configuration $Configuration -NoBuild
if (-not $?) { throw 'Installer ISO/package rebuild with KuroganeOS trust roots failed.' }

$VersionHeader = Join-Path $RootDir 'common\version.h'
$versionText = Get-Content -LiteralPath $VersionHeader -Raw
if ($versionText -notmatch '#define\s+KUROGANE_VERSION_STRING\s+"([^"]+)"') {
    throw "Cannot read KuroganeOS version from $VersionHeader"
}
$Version = $Matches[1]
$BaseImage = Join-Path $RootDir 'build\images\KuroganeOS-base.img'
$Package = Join-Path $RootDir 'build\install.pkg'
$Dist = Join-Path $RootDir 'dist'

# Canonical release media are intentionally hypervisor-specific. The QEMU
# artifact below is setup/install media because install.pkg is injected into it.
# The Foundation image remains package-free and is the correct live image for
# QEMU NIC/runtime qualification.
$VirtualBoxIso = Join-Path $Dist "KuroganeOS-$Version-virtualbox-x86_64.iso"
$QemuImage = Join-Path $Dist "KuroganeOS-$Version-qemu-x86_64.img"
$LegacyQemuImage = Join-Path $Dist "KuroganeOS-$Version-windows-qemu.img"
$LegacyGenericIso = Join-Path $Dist "KuroganeOS-$Version-x86_64.iso"
$Checksums = Join-Path $Dist 'SHA256SUMS.txt'
$InjectScript = Join-Path $PSScriptRoot 'inject-install-package.sh'

foreach ($required in @($BaseImage, $Package, $VirtualBoxIso, $InjectScript, $TrustOutput)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing media input: $required"
    }
}

[System.IO.Directory]::CreateDirectory($Dist) | Out-Null
Copy-Item -LiteralPath $BaseImage -Destination $QemuImage -Force
& wsl.exe bash `
    (Convert-ToKuroganeWslPath $InjectScript) `
    (Convert-ToKuroganeWslPath $QemuImage) `
    (Convert-ToKuroganeWslPath $Package)
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to inject install.pkg into the canonical QEMU IMG.'
}

# Delete old ambiguous names from the current version. They caused repeated
# accidental tests of stale media and make it impossible to tell which
# hypervisor contract an artifact was built for.
foreach ($stale in @($LegacyQemuImage, $LegacyGenericIso)) {
    if (Test-Path -LiteralPath $stale -PathType Leaf) {
        Remove-Item -LiteralPath $stale -Force
        Write-Host "[media-windows] removed stale ambiguous artifact: $stale"
    }
}

# Full-install qualification intentionally waits beyond the old boot-only
# budget. Stage 8 performs byte-for-byte readback through the production FAT32
# and AHCI paths, so a real VirtualBox run may legitimately need more than 90s.
if (-not $SkipVirtualBoxSmoke) {
    & (Join-Path $PSScriptRoot 'smoke-virtualbox-iso.ps1') -Iso $VirtualBoxIso -TimeoutSeconds 180
    if (-not $?) { throw 'Real Oracle VirtualBox full-install qualification failed.' }
} else {
    Write-Warning 'VirtualBox real-boot qualification was explicitly skipped. ISO is NOT release-qualified.'
}

$qemuHash = (Get-FileHash -LiteralPath $QemuImage -Algorithm SHA256).Hash.ToLowerInvariant()
$isoHash = (Get-FileHash -LiteralPath $VirtualBoxIso -Algorithm SHA256).Hash.ToLowerInvariant()
$lines = @(
    "$qemuHash  $([System.IO.Path]::GetFileName($QemuImage))",
    "$isoHash  $([System.IO.Path]::GetFileName($VirtualBoxIso))"
)
[System.IO.File]::WriteAllLines(
    $Checksums, $lines, (New-Object System.Text.UTF8Encoding($false)))
[System.IO.File]::WriteAllText(
    "$QemuImage.sha256",
    $lines[0] + [Environment]::NewLine,
    (New-Object System.Text.UTF8Encoding($false)))
[System.IO.File]::WriteAllText(
    "$VirtualBoxIso.sha256",
    $lines[1] + [Environment]::NewLine,
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host "[media-windows] KuroganeOS $Version"
Write-Host "[media-windows] QEMU setup/install IMG:  $QemuImage"
Write-Host "[media-windows] QEMU live/test IMG:     $BaseImage"
Write-Host "[media-windows] VirtualBox install ISO: $VirtualBoxIso"
Write-Host "[media-windows] Windows Web PKI roots:  $TrustOutput"
Write-Host '[media-windows] network smoke must use the package-free QEMU live/test IMG'
Write-Host '[media-windows] ISO static verification: GPT ESP #2 + EFI El Torito + PE32+ AMD64'
if (-not $SkipVirtualBoxSmoke) {
    Write-Host '[media-windows] ISO real Oracle VirtualBox full install: PASS'
}
Write-Host '[media-windows] no generic versioned .iso/.img aliases are published'
Write-Host "[media-windows] checksums: $Checksums"
