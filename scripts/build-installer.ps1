[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'test')]
    [string]$Configuration = 'debug',
    [switch]$NoBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$BuildDir = Join-Path $RootDir 'build'
$StageDir = Join-Path $BuildDir 'installer-staging'
$ExpectedStageDir = [System.IO.Path]::GetFullPath(
    (Join-Path $RootDir 'build\installer-staging'))
$ImageDir = Join-Path $BuildDir 'images'
$EspImage = Join-Path $ImageDir 'KuroganeOS-installer-esp.img'
$IsoImage = Join-Path $ImageDir 'KuroganeOS-installer.iso'
$Package = Join-Path $BuildDir 'install.pkg'
$Efi = Join-Path $BuildDir 'BOOTX64.EFI'
$Kernel = Join-Path $BuildDir 'kernel.elf'
$Overlay = Join-Path $BuildDir 'userspace\rootfs'

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
Copy-Item -LiteralPath $Efi -Destination (Join-Path $BootDir 'BOOTX64.EFI')
Copy-Item -LiteralPath $Kernel -Destination (Join-Path $StageDir 'kernel.elf')
Copy-Item -LiteralPath $Kernel -Destination (Join-Path $BootDir 'kernel.elf')
Copy-Item -LiteralPath $Package -Destination (Join-Path $StageDir 'install.pkg')

& (Join-Path $PSScriptRoot 'build-image.ps1') `
    -StageDirectory $StageDir -OutputPath $EspImage `
    -IncludeKernelInBootDirectory -AdditionalRootFile $Package
if (-not $?) { throw 'Installer ESP construction failed.' }

function Convert-ToWslPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $converted = & wsl.exe --exec wslpath -a -u `
        ([System.IO.Path]::GetFullPath($Path))
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($converted)) {
        throw "Cannot convert path for WSL: $Path"
    }
    return $converted.Trim()
}

$isoScript = Convert-ToWslPath (Join-Path $PSScriptRoot 'build-installer-iso.sh')
& wsl.exe bash $isoScript `
    (Convert-ToWslPath $StageDir) `
    (Convert-ToWslPath $EspImage) `
    (Convert-ToWslPath $IsoImage)
if ($LASTEXITCODE -ne 0) { throw 'Installer ISO construction failed.' }

$hash = (Get-FileHash -LiteralPath $IsoImage -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "[installer] $IsoImage"
Write-Host "[sha256] $hash"
