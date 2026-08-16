[CmdletBinding()]
param(
    [string]$OutputPath,
    [string]$WorkingImagePath,
    [switch]$ResetWorkingImage,
    [switch]$NoWorkingImage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$WslBridge = Join-Path $PSScriptRoot 'wsl-path.ps1'
if (-not (Test-Path -LiteralPath $WslBridge -PathType Leaf)) {
    throw "Missing Windows/WSL path bridge: $WslBridge"
}
. $WslBridge

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $RootDir 'build\images\KuroganeOS-base.img'
}
if ([string]::IsNullOrWhiteSpace($WorkingImagePath)) {
    $WorkingImagePath = Join-Path $RootDir 'state\KuroganeOS.img'
}

$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$WorkingImagePath = [System.IO.Path]::GetFullPath($WorkingImagePath)
$EfiPath = Join-Path $RootDir 'build\BOOTX64.EFI'
$KernelPath = Join-Path $RootDir 'build\kernel.elf'
$RootFsPath = Join-Path $RootDir 'rootfs'
$RootFsOverlayPath = Join-Path $RootDir 'build\userspace\rootfs'
$BuilderPath = Join-Path $PSScriptRoot 'build-foundation-image.sh'
$EspUpdaterPath = Join-Path $PSScriptRoot 'update-foundation-esp.sh'

foreach ($required in @($EfiPath, $KernelPath, $BuilderPath, $EspUpdaterPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing Foundation image input: $required"
    }
}
if (-not (Test-Path -LiteralPath $RootFsPath -PathType Container)) {
    throw "Missing root filesystem staging directory: $RootFsPath"
}
if (-not (Test-Path -LiteralPath $RootFsOverlayPath -PathType Container)) {
    throw "Missing generated userspace rootfs overlay: $RootFsOverlayPath"
}

[System.IO.Directory]::CreateDirectory(
    [System.IO.Path]::GetDirectoryName($OutputPath)) | Out-Null

$arguments = @(
    (Convert-ToKuroganeWslPath -Path $BuilderPath),
    '--output', (Convert-ToKuroganeWslPath -Path $OutputPath),
    '--efi', (Convert-ToKuroganeWslPath -Path $EfiPath),
    '--kernel', (Convert-ToKuroganeWslPath -Path $KernelPath),
    '--rootfs', (Convert-ToKuroganeWslPath -Path $RootFsPath),
    '--overlay', (Convert-ToKuroganeWslPath -Path $RootFsOverlayPath)
)
& wsl.exe bash @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Foundation image builder failed with exit code $LASTEXITCODE."
}

if (-not $NoWorkingImage) {
    $workingDirectory = [System.IO.Path]::GetDirectoryName($WorkingImagePath)
    [System.IO.Directory]::CreateDirectory($workingDirectory) | Out-Null
    if (-not (Test-Path -LiteralPath $WorkingImagePath -PathType Leaf)) {
        Copy-Item -LiteralPath $OutputPath -Destination $WorkingImagePath
        Write-Host "[working-image] created $WorkingImagePath"
    } elseif ($ResetWorkingImage) {
        Copy-Item -LiteralPath $OutputPath -Destination $WorkingImagePath -Force
        Write-Host "[working-image] reset explicitly: $WorkingImagePath"
    } else {
        $updateArguments = @(
            (Convert-ToKuroganeWslPath -Path $EspUpdaterPath),
            (Convert-ToKuroganeWslPath -Path $WorkingImagePath),
            (Convert-ToKuroganeWslPath -Path $EfiPath),
            (Convert-ToKuroganeWslPath -Path $KernelPath)
        )
        & wsl.exe bash @updateArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Working-image ESP update failed with exit code $LASTEXITCODE."
        }
        Write-Host "[working-image] root preserved; boot payload updated: $WorkingImagePath"
    }
}
