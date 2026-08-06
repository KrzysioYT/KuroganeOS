[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryInput,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-AlignedValue {
    param(
        [Parameter(Mandatory = $true)]
        [UInt64]$Value,

        [Parameter(Mandatory = $true)]
        [UInt64]$Alignment
    )

    if ($Alignment -eq 0 -or ($Alignment -band ($Alignment - 1)) -ne 0) {
        throw "Alignment must be a non-zero power of two."
    }
    if ($Value -gt [UInt64]::MaxValue - ($Alignment - 1)) {
        throw "Alignment arithmetic overflow."
    }

    return ($Value + $Alignment - 1) -band (-bnot ($Alignment - 1))
}

function Set-UInt16 {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [UInt16]$Value
    )

    $encoded = [System.BitConverter]::GetBytes($Value)
    [System.Array]::Copy($encoded, 0, $Buffer, $Offset, 2)
}

function Set-UInt32 {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [UInt32]$Value
    )

    $encoded = [System.BitConverter]::GetBytes($Value)
    [System.Array]::Copy($encoded, 0, $Buffer, $Offset, 4)
}

function Set-UInt64 {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [UInt64]$Value
    )

    $encoded = [System.BitConverter]::GetBytes($Value)
    [System.Array]::Copy($encoded, 0, $Buffer, $Offset, 8)
}

function Set-AsciiName {
    param(
        [byte[]]$Buffer,
        [int]$Offset,
        [string]$Name
    )

    $encoded = [System.Text.Encoding]::ASCII.GetBytes($Name)
    if ($encoded.Length -gt 8) {
        throw "PE section names are limited to eight bytes: $Name"
    }
    [System.Array]::Copy($encoded, 0, $Buffer, $Offset, $encoded.Length)
}

if (-not [System.BitConverter]::IsLittleEndian) {
    throw 'The EFI image converter requires a little-endian host.'
}

$inputFullPath = [System.IO.Path]::GetFullPath($BinaryInput)
$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
if (-not (Test-Path -LiteralPath $inputFullPath -PathType Leaf)) {
    throw "Flat loader image does not exist: $inputFullPath"
}

$image = [System.IO.File]::ReadAllBytes($inputFullPath)
if ($image.Length -eq 0) {
    throw "Flat loader image is empty: $inputFullPath"
}

[UInt64]$fileAlignment = 0x200
[UInt64]$sectionAlignment = 0x1000
[UInt64]$headersSize = 0x200
[UInt64]$imageRva = 0x1000
[UInt64]$imageRawOffset = $headersSize
[UInt64]$imageRawSize = Get-AlignedValue -Value $image.LongLength -Alignment $fileAlignment
[UInt64]$relocRva = Get-AlignedValue `
    -Value ($imageRva + $image.LongLength) `
    -Alignment $sectionAlignment
[UInt64]$relocRawOffset = $imageRawOffset + $imageRawSize
[UInt64]$relocDataSize = 12
[UInt64]$relocRawSize = $fileAlignment
[UInt64]$sizeOfImage = Get-AlignedValue `
    -Value ($relocRva + $relocDataSize) `
    -Alignment $sectionAlignment
[UInt64]$fileSize = $relocRawOffset + $relocRawSize

foreach ($value in @(
    $image.LongLength,
    $imageRawSize,
    $relocRva,
    $relocRawOffset,
    $sizeOfImage,
    $fileSize
)) {
    if ([UInt64]$value -gt [UInt32]::MaxValue) {
        throw 'The loader is too large for the PE32+ image layout.'
    }
}
if ($fileSize -gt [Int32]::MaxValue) {
    throw 'The generated image is too large for a managed byte array.'
}

$pe = New-Object byte[] ([int]$fileSize)

# Minimal DOS header. UEFI uses e_lfanew to locate the PE signature.
$pe[0] = [byte][char]'M'
$pe[1] = [byte][char]'Z'
Set-UInt32 -Buffer $pe -Offset 0x3c -Value 0x80

$peOffset = 0x80
$pe[$peOffset] = [byte][char]'P'
$pe[$peOffset + 1] = [byte][char]'E'

$coff = $peOffset + 4
Set-UInt16 -Buffer $pe -Offset $coff -Value 0x8664
Set-UInt16 -Buffer $pe -Offset ($coff + 2) -Value 2
Set-UInt32 -Buffer $pe -Offset ($coff + 4) -Value 0
Set-UInt32 -Buffer $pe -Offset ($coff + 8) -Value 0
Set-UInt32 -Buffer $pe -Offset ($coff + 12) -Value 0
Set-UInt16 -Buffer $pe -Offset ($coff + 16) -Value 240
Set-UInt16 -Buffer $pe -Offset ($coff + 18) -Value 0x0022

$optional = $coff + 20
Set-UInt16 -Buffer $pe -Offset $optional -Value 0x020b
$pe[$optional + 2] = 1
$pe[$optional + 3] = 0
Set-UInt32 -Buffer $pe -Offset ($optional + 4) -Value ([UInt32]$imageRawSize)
Set-UInt32 -Buffer $pe -Offset ($optional + 8) -Value ([UInt32]$relocRawSize)
Set-UInt32 -Buffer $pe -Offset ($optional + 12) -Value 0
Set-UInt32 -Buffer $pe -Offset ($optional + 16) -Value ([UInt32]$imageRva)
Set-UInt32 -Buffer $pe -Offset ($optional + 20) -Value ([UInt32]$imageRva)
Set-UInt64 -Buffer $pe -Offset ($optional + 24) -Value 0
Set-UInt32 -Buffer $pe -Offset ($optional + 32) -Value ([UInt32]$sectionAlignment)
Set-UInt32 -Buffer $pe -Offset ($optional + 36) -Value ([UInt32]$fileAlignment)
Set-UInt16 -Buffer $pe -Offset ($optional + 40) -Value 0
Set-UInt16 -Buffer $pe -Offset ($optional + 42) -Value 0
Set-UInt16 -Buffer $pe -Offset ($optional + 44) -Value 1
Set-UInt16 -Buffer $pe -Offset ($optional + 46) -Value 0
Set-UInt16 -Buffer $pe -Offset ($optional + 48) -Value 2
Set-UInt16 -Buffer $pe -Offset ($optional + 50) -Value 0
Set-UInt32 -Buffer $pe -Offset ($optional + 52) -Value 0
Set-UInt32 -Buffer $pe -Offset ($optional + 56) -Value ([UInt32]$sizeOfImage)
Set-UInt32 -Buffer $pe -Offset ($optional + 60) -Value ([UInt32]$headersSize)
Set-UInt32 -Buffer $pe -Offset ($optional + 64) -Value 0
Set-UInt16 -Buffer $pe -Offset ($optional + 68) -Value 10
Set-UInt16 -Buffer $pe -Offset ($optional + 70) -Value 0x0140
Set-UInt64 -Buffer $pe -Offset ($optional + 72) -Value 0x100000
Set-UInt64 -Buffer $pe -Offset ($optional + 80) -Value 0x1000
Set-UInt64 -Buffer $pe -Offset ($optional + 88) -Value 0x100000
Set-UInt64 -Buffer $pe -Offset ($optional + 96) -Value 0x1000
Set-UInt32 -Buffer $pe -Offset ($optional + 104) -Value 0
Set-UInt32 -Buffer $pe -Offset ($optional + 108) -Value 16

# IMAGE_DIRECTORY_ENTRY_BASERELOC.
$relocDirectory = $optional + 112 + (5 * 8)
Set-UInt32 -Buffer $pe -Offset $relocDirectory -Value ([UInt32]$relocRva)
Set-UInt32 -Buffer $pe -Offset ($relocDirectory + 4) -Value ([UInt32]$relocDataSize)

$sectionTable = $optional + 240
Set-AsciiName -Buffer $pe -Offset $sectionTable -Name '.image'
Set-UInt32 -Buffer $pe -Offset ($sectionTable + 8) -Value ([UInt32]$image.LongLength)
Set-UInt32 -Buffer $pe -Offset ($sectionTable + 12) -Value ([UInt32]$imageRva)
Set-UInt32 -Buffer $pe -Offset ($sectionTable + 16) -Value ([UInt32]$imageRawSize)
Set-UInt32 -Buffer $pe -Offset ($sectionTable + 20) -Value ([UInt32]$imageRawOffset)
Set-UInt32 -Buffer $pe -Offset ($sectionTable + 36) -Value 0x60000060

$relocSection = $sectionTable + 40
Set-AsciiName -Buffer $pe -Offset $relocSection -Name '.reloc'
Set-UInt32 -Buffer $pe -Offset ($relocSection + 8) -Value ([UInt32]$relocDataSize)
Set-UInt32 -Buffer $pe -Offset ($relocSection + 12) -Value ([UInt32]$relocRva)
Set-UInt32 -Buffer $pe -Offset ($relocSection + 16) -Value ([UInt32]$relocRawSize)
Set-UInt32 -Buffer $pe -Offset ($relocSection + 20) -Value ([UInt32]$relocRawOffset)
Set-UInt32 -Buffer $pe -Offset ($relocSection + 36) -Value 0x42000040

[System.Array]::Copy(
    $image,
    0,
    $pe,
    [int]$imageRawOffset,
    $image.Length
)

# A valid relocation block containing two IMAGE_REL_BASED_ABSOLUTE padding
# entries. The loader itself is entirely RIP-relative, so no fixups are needed.
Set-UInt32 -Buffer $pe -Offset ([int]$relocRawOffset) -Value 0
Set-UInt32 -Buffer $pe -Offset ([int]$relocRawOffset + 4) -Value 12
Set-UInt16 -Buffer $pe -Offset ([int]$relocRawOffset + 8) -Value 0
Set-UInt16 -Buffer $pe -Offset ([int]$relocRawOffset + 10) -Value 0

$outputDirectory = Split-Path -Parent $outputFullPath
if ($outputDirectory) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}
[System.IO.File]::WriteAllBytes($outputFullPath, $pe)

Write-Host (
    '[efi] {0} ({1} bytes, entry RVA 0x{2:X}, image size 0x{3:X})' -f
    $outputFullPath,
    $pe.Length,
    $imageRva,
    $sizeOfImage
)
