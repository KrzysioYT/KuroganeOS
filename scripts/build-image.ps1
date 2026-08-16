[CmdletBinding()]
param(
    [Parameter()]
    [string]$StageDirectory,

    [Parameter()]
    [string]$OutputPath,

    [Parameter()]
    [switch]$IncludeKernelInBootDirectory,

    [Parameter()]
    [string]$AdditionalRootFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = Split-Path -Parent $scriptDirectory
if ([string]::IsNullOrWhiteSpace($StageDirectory)) {
    $StageDirectory = Join-Path $repositoryRoot "iso"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repositoryRoot "kurogane.img"
}

# This script intentionally has no dependency on diskpart, mkfs, mtools, WSL, or
# administrator-only block-device APIs.  The image layout is kept fixed so that
# identical staged files produce a byte-for-byte identical image.
$BytesPerSector = [int64]512
$ImageSizeBytes = [int64](64 * 1024 * 1024)
$TotalSectors = [int64]($ImageSizeBytes / $BytesPerSector)
$ReservedSectors = [int64]32
$FatCount = [int64]2
$SectorsPerCluster = [int64]1
$RootCluster = [uint32]2
$BackupBootSector = [int64]6
$FsInfoSector = [int64]1
$MediaDescriptor = [byte]0xF8
$EndOfChain = [uint32]0x0FFFFFFF
$FsInfoTrailSignature = [uint32]2857697280
$FixedFatDate = [uint16](((2026 - 1980) -shl 9) -bor (1 -shl 5) -bor 1)
$FixedFatTime = [uint16]0
$VolumeLabel = "KUROGANEOS "

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Set-UInt16LE {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Buffer,

        [Parameter(Mandatory = $true)]
        [int]$Offset,

        [Parameter(Mandatory = $true)]
        [uint16]$Value
    )

    Assert-Condition (($Offset -ge 0) -and (($Offset + 2) -le $Buffer.Length)) "A 16-bit field is outside its buffer."
    $bytes = [System.BitConverter]::GetBytes($Value)
    [System.Buffer]::BlockCopy($bytes, 0, $Buffer, $Offset, 2)
}

function Set-UInt32LE {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Buffer,

        [Parameter(Mandatory = $true)]
        [int]$Offset,

        [Parameter(Mandatory = $true)]
        [uint32]$Value
    )

    Assert-Condition (($Offset -ge 0) -and (($Offset + 4) -le $Buffer.Length)) "A 32-bit field is outside its buffer."
    $bytes = [System.BitConverter]::GetBytes($Value)
    [System.Buffer]::BlockCopy($bytes, 0, $Buffer, $Offset, 4)
}

function Get-UInt16LE {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Buffer,

        [Parameter(Mandatory = $true)]
        [int]$Offset
    )

    Assert-Condition (($Offset -ge 0) -and (($Offset + 2) -le $Buffer.Length)) "A 16-bit field is outside its buffer."
    return [System.BitConverter]::ToUInt16($Buffer, $Offset)
}

function Get-UInt32LE {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Buffer,

        [Parameter(Mandatory = $true)]
        [int]$Offset
    )

    Assert-Condition (($Offset -ge 0) -and (($Offset + 4) -le $Buffer.Length)) "A 32-bit field is outside its buffer."
    return [System.BitConverter]::ToUInt32($Buffer, $Offset)
}

function Set-Ascii {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Buffer,

        [Parameter(Mandatory = $true)]
        [int]$Offset,

        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    for ($index = 0; $index -lt $Text.Length; $index++) {
        Assert-Condition ([int]$Text[$index] -le 0x7F) "Non-ASCII text cannot be written to a FAT metadata field."
    }

    $bytes = [System.Text.Encoding]::ASCII.GetBytes($Text)
    Assert-Condition (($Offset -ge 0) -and (($Offset + $bytes.Length) -le $Buffer.Length)) "An ASCII field is outside its buffer."
    [System.Buffer]::BlockCopy($bytes, 0, $Buffer, $Offset, $bytes.Length)
}

function ConvertTo-FatShortName {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    Assert-Condition (($Name -ne ".") -and ($Name -ne "..")) "Dot entries are not normal 8.3 names."
    $parts = $Name.Split([char]'.')
    Assert-Condition (($parts.Length -ge 1) -and ($parts.Length -le 2)) "The FAT file name '$Name' is not an 8.3 name."

    $baseName = $parts[0].ToUpperInvariant()
    $extension = ""
    if ($parts.Length -eq 2) {
        $extension = $parts[1].ToUpperInvariant()
    }

    Assert-Condition (($baseName.Length -ge 1) -and ($baseName.Length -le 8)) "The FAT base name '$baseName' must contain 1-8 characters."
    Assert-Condition ($extension.Length -le 3) "The FAT extension '$extension' must contain at most 3 characters."
    Assert-Condition ($baseName -match "^[A-Z0-9_]+$") "The FAT base name '$baseName' contains an unsupported character."
    Assert-Condition (($extension.Length -eq 0) -or ($extension -match "^[A-Z0-9_]+$")) "The FAT extension '$extension' contains an unsupported character."

    $shortName = $baseName.PadRight(8, [char]' ') + $extension.PadRight(3, [char]' ')
    Assert-Condition ($shortName.Length -eq 11) "Internal error while constructing the 8.3 name '$Name'."
    return $shortName
}

function New-FatDirectoryEntry {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ShortName,

        [Parameter(Mandatory = $true)]
        [byte]$Attributes,

        [Parameter(Mandatory = $true)]
        [uint32]$FirstCluster,

        [Parameter(Mandatory = $true)]
        [uint32]$Size
    )

    Assert-Condition ($ShortName.Length -eq 11) "A FAT directory short name must be exactly 11 bytes."
    $entry = New-Object byte[] 32
    Set-Ascii $entry 0 $ShortName
    $entry[11] = $Attributes
    $entry[12] = 0
    $entry[13] = 0
    Set-UInt16LE $entry 14 $FixedFatTime
    Set-UInt16LE $entry 16 $FixedFatDate
    Set-UInt16LE $entry 18 $FixedFatDate
    Set-UInt16LE $entry 20 ([uint16]([math]::Floor([double]$FirstCluster / 65536.0)))
    Set-UInt16LE $entry 22 $FixedFatTime
    Set-UInt16LE $entry 24 $FixedFatDate
    Set-UInt16LE $entry 26 ([uint16]([uint64]$FirstCluster -band 0xFFFF))
    Set-UInt32LE $entry 28 $Size
    return ,$entry
}

function Copy-DirectoryEntry {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Directory,

        [Parameter(Mandatory = $true)]
        [int]$Index,

        [Parameter(Mandatory = $true)]
        [byte[]]$Entry
    )

    Assert-Condition ($Entry.Length -eq 32) "A FAT directory entry must be 32 bytes."
    $offset = $Index * 32
    Assert-Condition (($offset -ge 0) -and (($offset + 32) -le $Directory.Length)) "The FAT directory has too many entries."
    [System.Buffer]::BlockCopy($Entry, 0, $Directory, $offset, 32)
}

function Set-FatEntry {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Fat,

        [Parameter(Mandatory = $true)]
        [long]$Cluster,

        [Parameter(Mandatory = $true)]
        [uint32]$Value
    )

    Assert-Condition (($Cluster -ge 0) -and ((($Cluster * 4) + 4) -le $Fat.Length)) "FAT cluster $Cluster is outside the FAT."
    Set-UInt32LE $Fat ([int]($Cluster * 4)) ([uint32]($Value -band 0x0FFFFFFF))
}

function Get-ClusterOffset {
    param(
        [Parameter(Mandatory = $true)]
        [long]$Cluster,

        [Parameter(Mandatory = $true)]
        [long]$FirstDataSector
    )

    Assert-Condition ($Cluster -ge 2) "FAT data clusters start at cluster 2."
    $sector = $FirstDataSector + (($Cluster - 2) * $SectorsPerCluster)
    Assert-Condition (($sector -ge $FirstDataSector) -and (($sector + $SectorsPerCluster) -le $TotalSectors)) "Cluster $Cluster is outside the image."
    return [int64]($sector * $BytesPerSector)
}

function Write-BytesAt {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileStream]$Stream,

        [Parameter(Mandatory = $true)]
        [int64]$Offset,

        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes
    )

    Assert-Condition (($Offset -ge 0) -and (($Offset + $Bytes.LongLength) -le $ImageSizeBytes)) "A write would exceed the disk image."
    [void]$Stream.Seek($Offset, [System.IO.SeekOrigin]::Begin)
    $Stream.Write($Bytes, 0, $Bytes.Length)
}

function Read-BytesAt {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileStream]$Stream,

        [Parameter(Mandatory = $true)]
        [int64]$Offset,

        [Parameter(Mandatory = $true)]
        [int]$Count
    )

    Assert-Condition (($Offset -ge 0) -and ($Count -ge 0) -and (($Offset + $Count) -le $Stream.Length)) "A read would exceed the disk image."
    $bytes = New-Object byte[] $Count
    [void]$Stream.Seek($Offset, [System.IO.SeekOrigin]::Begin)
    $readTotal = 0
    while ($readTotal -lt $Count) {
        $read = $Stream.Read($bytes, $readTotal, $Count - $readTotal)
        Assert-Condition ($read -gt 0) "Unexpected end of disk image."
        $readTotal += $read
    }
    return ,$bytes
}

function Assert-ByteArraysEqual {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Left,

        [Parameter(Mandatory = $true)]
        [byte[]]$Right,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    Assert-Condition ($Left.Length -eq $Right.Length) $Message
    # Byte-at-a-time loops in Windows PowerShell are prohibitively slow for
    # multi-megabyte installer payloads. Hash both in managed code and compare
    # the fixed-size digests; this retains full readback validation.
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $leftHash = $sha256.ComputeHash($Left)
        $rightHash = $sha256.ComputeHash($Right)
    }
    finally { $sha256.Dispose() }
    for ($index = 0; $index -lt $leftHash.Length; $index++) {
        if ($leftHash[$index] -ne $rightHash[$index]) { throw $Message }
    }
}

function Assert-EfiApplication {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    Assert-Condition ($Bytes.Length -ge 256) "The staged EFI loader '$Path' is too small to be a PE/COFF image."
    Assert-Condition (($Bytes[0] -eq 0x4D) -and ($Bytes[1] -eq 0x5A)) "The staged EFI loader '$Path' has no DOS/PE signature."
    $peOffset = [System.BitConverter]::ToInt32($Bytes, 0x3C)
    Assert-Condition (($peOffset -ge 0x40) -and (($peOffset + 24 + 70) -le $Bytes.Length)) "The staged EFI loader '$Path' has an invalid PE header offset."
    Assert-Condition (($Bytes[$peOffset] -eq 0x50) -and ($Bytes[$peOffset + 1] -eq 0x45) -and ($Bytes[$peOffset + 2] -eq 0) -and ($Bytes[$peOffset + 3] -eq 0)) "The staged EFI loader '$Path' has no PE signature."
    $coffOffset = $peOffset + 4
    Assert-Condition ((Get-UInt16LE $Bytes $coffOffset) -eq 0x8664) "The staged EFI loader '$Path' is not an x86-64 image."
    $optionalHeaderSize = Get-UInt16LE $Bytes ($coffOffset + 16)
    $optionalOffset = $coffOffset + 20
    Assert-Condition (($optionalHeaderSize -ge 70) -and (($optionalOffset + $optionalHeaderSize) -le $Bytes.Length)) "The staged EFI loader '$Path' has a truncated optional header."
    Assert-Condition ((Get-UInt16LE $Bytes $optionalOffset) -eq 0x020B) "The staged EFI loader '$Path' is not a PE32+ image."
    Assert-Condition ((Get-UInt16LE $Bytes ($optionalOffset + 68)) -eq 10) "The staged PE image '$Path' is not an EFI application."
}

function Assert-KernelElf {
    param(
        [Parameter(Mandatory = $true)]
        [byte[]]$Bytes,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    Assert-Condition ($Bytes.Length -ge 64) "The staged kernel '$Path' is too small to be an ELF64 image."
    Assert-Condition (($Bytes[0] -eq 0x7F) -and ($Bytes[1] -eq 0x45) -and ($Bytes[2] -eq 0x4C) -and ($Bytes[3] -eq 0x46)) "The staged kernel '$Path' has no ELF signature."
    Assert-Condition (($Bytes[4] -eq 2) -and ($Bytes[5] -eq 1) -and ($Bytes[6] -eq 1)) "The staged kernel '$Path' is not a little-endian ELF64 v1 image."
    Assert-Condition ((Get-UInt16LE $Bytes 16) -eq 3) "The staged kernel '$Path' is not a relocatable ET_DYN image."
    Assert-Condition ((Get-UInt16LE $Bytes 18) -eq 0x003E) "The staged kernel '$Path' is not an x86-64 ELF image."
}

function Get-FileSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            $hash = $sha256.ComputeHash($stream)
        }
        finally {
            $stream.Dispose()
        }
    }
    finally {
        $sha256.Dispose()
    }

    return ([System.BitConverter]::ToString($hash)).Replace("-", "").ToLowerInvariant()
}

Assert-Condition ([System.BitConverter]::IsLittleEndian) "This image builder requires a little-endian host."
Assert-Condition (($ImageSizeBytes % $BytesPerSector) -eq 0) "The image size must be sector aligned."
Assert-Condition (($BackupBootSector + 1) -lt $ReservedSectors) "The backup boot records must be inside the reserved area."

$stagePath = [System.IO.Path]::GetFullPath($StageDirectory)
$efiPath = [System.IO.Path]::GetFullPath((Join-Path $stagePath "EFI\BOOT\BOOTX64.EFI"))
$kernelPath = [System.IO.Path]::GetFullPath((Join-Path $stagePath "kernel.elf"))
$additionalRootPath = if ([string]::IsNullOrWhiteSpace($AdditionalRootFile)) {
    $null
} else {
    [System.IO.Path]::GetFullPath($AdditionalRootFile)
}
$finalPath = [System.IO.Path]::GetFullPath($OutputPath)

Assert-Condition ([System.IO.File]::Exists($efiPath)) "Missing staged EFI loader: $efiPath"
Assert-Condition ([System.IO.File]::Exists($kernelPath)) "Missing staged kernel: $kernelPath"
Assert-Condition (-not $finalPath.Equals($efiPath, [System.StringComparison]::OrdinalIgnoreCase)) "The output path cannot overwrite the staged EFI loader."
Assert-Condition (-not $finalPath.Equals($kernelPath, [System.StringComparison]::OrdinalIgnoreCase)) "The output path cannot overwrite the staged kernel."
if ($null -ne $additionalRootPath) {
    Assert-Condition ([System.IO.File]::Exists($additionalRootPath)) "Missing additional root file: $additionalRootPath"
    Assert-Condition (-not $finalPath.Equals($additionalRootPath, [System.StringComparison]::OrdinalIgnoreCase)) "The output path cannot overwrite the additional root file."
}

$efiBytes = [System.IO.File]::ReadAllBytes($efiPath)
$kernelBytes = [System.IO.File]::ReadAllBytes($kernelPath)
$additionalRootBytes = if ($null -ne $additionalRootPath) {
    [System.IO.File]::ReadAllBytes($additionalRootPath)
} else { $null }
Assert-Condition (($efiBytes.Length -gt 0) -and ($efiBytes.LongLength -le [uint32]::MaxValue)) "The staged EFI loader has an unsupported size."
Assert-Condition (($kernelBytes.Length -gt 0) -and ($kernelBytes.LongLength -le [uint32]::MaxValue)) "The staged kernel has an unsupported size."
Assert-EfiApplication $efiBytes $efiPath
Assert-KernelElf $kernelBytes $kernelPath
if ($null -ne $additionalRootBytes) {
    Assert-Condition (($additionalRootBytes.Length -gt 0) -and ($additionalRootBytes.LongLength -le [uint32]::MaxValue)) "The additional root file has an unsupported size."
}

# Solve the FAT-size inequality, then minimize the answer.  This avoids the
# off-by-one/oscillation bug common in simple FAT32 size calculators.
$fatSectors = [int64][math]::Ceiling(
    (4.0 * ($TotalSectors - $ReservedSectors + (2 * $SectorsPerCluster))) /
    (($BytesPerSector * $SectorsPerCluster) + (4 * $FatCount))
)
if ($fatSectors -lt 1) {
    $fatSectors = 1
}

while ($true) {
    $candidateDataSectors = $TotalSectors - $ReservedSectors - ($FatCount * $fatSectors)
    Assert-Condition ($candidateDataSectors -gt 0) "The FAT geometry leaves no data area."
    $candidateClusters = [int64][math]::Floor([double]$candidateDataSectors / $SectorsPerCluster)
    $requiredFatSectors = [int64][math]::Ceiling(
        [double](($candidateClusters + 2) * 4) / $BytesPerSector
    )
    if ($fatSectors -ge $requiredFatSectors) {
        break
    }
    $fatSectors++
}

while ($fatSectors -gt 1) {
    $smallerFat = $fatSectors - 1
    $smallerDataSectors = $TotalSectors - $ReservedSectors - ($FatCount * $smallerFat)
    $smallerClusters = [int64][math]::Floor([double]$smallerDataSectors / $SectorsPerCluster)
    $smallerRequired = [int64][math]::Ceiling([double](($smallerClusters + 2) * 4) / $BytesPerSector)
    if ($smallerFat -lt $smallerRequired) {
        break
    }
    $fatSectors = $smallerFat
}

$firstFatSector = $ReservedSectors
$firstDataSector = $ReservedSectors + ($FatCount * $fatSectors)
$dataSectors = $TotalSectors - $firstDataSector
$clusterCount = [int64][math]::Floor([double]$dataSectors / $SectorsPerCluster)
$clusterBytes = [int64]($SectorsPerCluster * $BytesPerSector)
$fatEntryCapacity = [int64][math]::Floor([double]($fatSectors * $BytesPerSector) / 4.0)

Assert-Condition ($clusterCount -ge 65525) "The selected geometry is not FAT32 (only $clusterCount data clusters)."
Assert-Condition ($clusterCount -lt 0x0FFFFFF5) "The selected geometry exceeds the FAT32 cluster-number limit."
Assert-Condition ($fatEntryCapacity -ge ($clusterCount + 2)) "The FAT cannot address every data cluster."
Assert-Condition (($firstDataSector + ($clusterCount * $SectorsPerCluster)) -le $TotalSectors) "The calculated FAT32 data area exceeds the image."
Assert-Condition ($fatSectors -le [uint32]::MaxValue) "The FAT is too large for the FAT32 BPB."
Assert-Condition ($TotalSectors -le [uint32]::MaxValue) "The image is too large for the FAT32 BPB."

$efiShortName = ConvertTo-FatShortName "EFI"
$bootShortName = ConvertTo-FatShortName "BOOT"
$bootLoaderShortName = ConvertTo-FatShortName "BOOTX64.EFI"
$kernelShortName = ConvertTo-FatShortName "KERNEL.ELF"
$additionalRootShortName = if ($null -ne $additionalRootPath) {
    ConvertTo-FatShortName ([System.IO.Path]::GetFileName($additionalRootPath))
} else { $null }
Assert-Condition (($efiShortName -ne $kernelShortName) -and ($bootLoaderShortName -ne $kernelShortName)) "A duplicate 8.3 directory name was generated."
Assert-Condition ($VolumeLabel.Length -eq 11) "The FAT32 volume label must be exactly 11 characters."

$allocations = @(
    [pscustomobject]@{
        LogicalPath = "\EFI\BOOT\BOOTX64.EFI"
        Bytes = $efiBytes
        FirstCluster = [uint32]0
        ClusterCount = [int64]0
    },
    [pscustomobject]@{
        LogicalPath = "\kernel.elf"
        Bytes = $kernelBytes
        FirstCluster = [uint32]0
        ClusterCount = [int64]0
    }
)

if ($IncludeKernelInBootDirectory) {
    $allocations += [pscustomobject]@{
        LogicalPath = "\EFI\BOOT\kernel.elf"
        Bytes = $kernelBytes
        FirstCluster = [uint32]0
        ClusterCount = [int64]0
    }
}

$additionalRootAllocation = $null
if ($null -ne $additionalRootBytes) {
    $additionalRootAllocation = [pscustomobject]@{
        LogicalPath = "\" + [System.IO.Path]::GetFileName($additionalRootPath)
        Bytes = $additionalRootBytes
        FirstCluster = [uint32]0
        ClusterCount = [int64]0
    }
    $allocations += $additionalRootAllocation
}

# Clusters 2, 3, and 4 are the root, EFI, and BOOT directories.
$nextCluster = [int64]5
foreach ($allocation in $allocations) {
    $allocation.ClusterCount = [int64][math]::Ceiling([double]$allocation.Bytes.LongLength / $clusterBytes)
    Assert-Condition ($allocation.ClusterCount -ge 1) "Empty files are not expected in the boot image."
    Assert-Condition (($nextCluster + $allocation.ClusterCount) -le ($clusterCount + 2)) "The staged files do not fit in the disk image."
    $allocation.FirstCluster = [uint32]$nextCluster
    $nextCluster += $allocation.ClusterCount
}

$allocatedClusters = $nextCluster - 2
$freeClusters = $clusterCount - $allocatedClusters
Assert-Condition ($freeClusters -ge 0) "The FAT32 free-cluster count underflowed."
$nextFreeCluster = if ($freeClusters -gt 0) { [uint32]$nextCluster } else { [uint32]::MaxValue }

$bootSector = New-Object byte[] ([int]$BytesPerSector)
$bootSector[0] = 0xEB
$bootSector[1] = 0x58
$bootSector[2] = 0x90
Set-Ascii $bootSector 3 "KRGN2.0 "
Set-UInt16LE $bootSector 11 ([uint16]$BytesPerSector)
$bootSector[13] = [byte]$SectorsPerCluster
Set-UInt16LE $bootSector 14 ([uint16]$ReservedSectors)
$bootSector[16] = [byte]$FatCount
Set-UInt16LE $bootSector 17 0
Set-UInt16LE $bootSector 19 0
$bootSector[21] = $MediaDescriptor
Set-UInt16LE $bootSector 22 0
Set-UInt16LE $bootSector 24 63
Set-UInt16LE $bootSector 26 255
Set-UInt32LE $bootSector 28 0
Set-UInt32LE $bootSector 32 ([uint32]$TotalSectors)
Set-UInt32LE $bootSector 36 ([uint32]$fatSectors)
Set-UInt16LE $bootSector 40 0
Set-UInt16LE $bootSector 42 0
Set-UInt32LE $bootSector 44 $RootCluster
Set-UInt16LE $bootSector 48 ([uint16]$FsInfoSector)
Set-UInt16LE $bootSector 50 ([uint16]$BackupBootSector)
$bootSector[64] = 0x80
$bootSector[65] = 0
$bootSector[66] = 0x29
Set-UInt32LE $bootSector 67 ([uint32]0x4B52474E)
Set-Ascii $bootSector 71 $VolumeLabel
Set-Ascii $bootSector 82 "FAT32   "
$bootSector[510] = 0x55
$bootSector[511] = 0xAA

$fsInfo = New-Object byte[] ([int]$BytesPerSector)
Set-UInt32LE $fsInfo 0 ([uint32]0x41615252)
Set-UInt32LE $fsInfo 484 ([uint32]0x61417272)
Set-UInt32LE $fsInfo 488 ([uint32]$freeClusters)
Set-UInt32LE $fsInfo 492 $nextFreeCluster
Set-UInt32LE $fsInfo 508 $FsInfoTrailSignature

$fatByteLength = [int]($fatSectors * $BytesPerSector)
$fat = New-Object byte[] $fatByteLength
Set-FatEntry $fat 0 ([uint32](0x0FFFFF00 -bor $MediaDescriptor))
Set-FatEntry $fat 1 $EndOfChain
Set-FatEntry $fat 2 $EndOfChain
Set-FatEntry $fat 3 $EndOfChain
Set-FatEntry $fat 4 $EndOfChain

foreach ($allocation in $allocations) {
    for ($clusterIndex = [int64]0; $clusterIndex -lt $allocation.ClusterCount; $clusterIndex++) {
        $cluster = [int64]$allocation.FirstCluster + $clusterIndex
        $value = if ($clusterIndex -eq ($allocation.ClusterCount - 1)) {
            $EndOfChain
        } else { [uint32]($cluster + 1) }
        $offset = [int]($cluster * 4)
        $fat[$offset] = [byte]($value -band 0xFF)
        $fat[$offset + 1] = [byte](($value -shr 8) -band 0xFF)
        $fat[$offset + 2] = [byte](($value -shr 16) -band 0xFF)
        $fat[$offset + 3] = [byte](($value -shr 24) -band 0x0F)
    }
}

$rootDirectory = New-Object byte[] ([int]$clusterBytes)
$efiDirectory = New-Object byte[] ([int]$clusterBytes)
$bootDirectory = New-Object byte[] ([int]$clusterBytes)
$dotName = ".".PadRight(11, [char]' ')
$dotDotName = "..".PadRight(11, [char]' ')

Copy-DirectoryEntry $rootDirectory 0 (New-FatDirectoryEntry $VolumeLabel 0x08 0 0)
Copy-DirectoryEntry $rootDirectory 1 (New-FatDirectoryEntry $efiShortName 0x10 3 0)
Copy-DirectoryEntry $rootDirectory 2 (New-FatDirectoryEntry $kernelShortName 0x20 $allocations[1].FirstCluster ([uint32]$kernelBytes.LongLength))
if ($null -ne $additionalRootAllocation) {
    Copy-DirectoryEntry $rootDirectory 3 (New-FatDirectoryEntry $additionalRootShortName 0x20 $additionalRootAllocation.FirstCluster ([uint32]$additionalRootBytes.LongLength))
}

Copy-DirectoryEntry $efiDirectory 0 (New-FatDirectoryEntry $dotName 0x10 3 0)
# FAT32 encodes a subdirectory's parent as cluster 0 when that parent is the
# root directory. Some firmware accepts RootCluster here, but fsck.fat and
# stricter readers correctly reject it as a malformed '..' entry.
Copy-DirectoryEntry $efiDirectory 1 (New-FatDirectoryEntry $dotDotName 0x10 0 0)
Copy-DirectoryEntry $efiDirectory 2 (New-FatDirectoryEntry $bootShortName 0x10 4 0)

Copy-DirectoryEntry $bootDirectory 0 (New-FatDirectoryEntry $dotName 0x10 4 0)
Copy-DirectoryEntry $bootDirectory 1 (New-FatDirectoryEntry $dotDotName 0x10 3 0)
Copy-DirectoryEntry $bootDirectory 2 (New-FatDirectoryEntry $bootLoaderShortName 0x20 $allocations[0].FirstCluster ([uint32]$efiBytes.LongLength))
if ($IncludeKernelInBootDirectory) {
    Copy-DirectoryEntry $bootDirectory 3 (New-FatDirectoryEntry $kernelShortName 0x20 $allocations[2].FirstCluster ([uint32]$kernelBytes.LongLength))
}

$outputDirectory = [System.IO.Path]::GetDirectoryName($finalPath)
Assert-Condition (-not [string]::IsNullOrWhiteSpace($outputDirectory)) "The output path has no parent directory."
[void][System.IO.Directory]::CreateDirectory($outputDirectory)
$temporaryName = "." + [System.IO.Path]::GetFileName($finalPath) + "." + [System.Guid]::NewGuid().ToString("N") + ".tmp"
$temporaryPath = Join-Path $outputDirectory $temporaryName
$replacementBackupPath = $null

try {
    $writeStream = New-Object System.IO.FileStream(
        $temporaryPath,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::ReadWrite,
        [System.IO.FileShare]::None
    )
    try {
        $writeStream.SetLength($ImageSizeBytes)
        Write-BytesAt $writeStream 0 $bootSector
        Write-BytesAt $writeStream ($FsInfoSector * $BytesPerSector) $fsInfo
        Write-BytesAt $writeStream ($BackupBootSector * $BytesPerSector) $bootSector
        Write-BytesAt $writeStream (($BackupBootSector + 1) * $BytesPerSector) $fsInfo

        for ($fatIndex = [int64]0; $fatIndex -lt $FatCount; $fatIndex++) {
            $fatOffset = ($firstFatSector + ($fatIndex * $fatSectors)) * $BytesPerSector
            Write-BytesAt $writeStream $fatOffset $fat
        }

        Write-BytesAt $writeStream (Get-ClusterOffset 2 $firstDataSector) $rootDirectory
        Write-BytesAt $writeStream (Get-ClusterOffset 3 $firstDataSector) $efiDirectory
        Write-BytesAt $writeStream (Get-ClusterOffset 4 $firstDataSector) $bootDirectory

        foreach ($allocation in $allocations) {
            Write-BytesAt $writeStream (Get-ClusterOffset $allocation.FirstCluster $firstDataSector) $allocation.Bytes
        }

        $writeStream.Flush($true)
    }
    finally {
        $writeStream.Dispose()
    }

    # Read the completed temporary image back before it can replace a known-good
    # output.  Besides catching truncated writes, these checks cover the metadata
    # that firmware relies upon when mounting the superfloppy.
    $verifyStream = New-Object System.IO.FileStream(
        $temporaryPath,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read
    )
    try {
        Assert-Condition ($verifyStream.Length -eq $ImageSizeBytes) "The temporary disk image has the wrong size."
        $primaryBoot = Read-BytesAt $verifyStream 0 ([int]$BytesPerSector)
        $backupBoot = Read-BytesAt $verifyStream ($BackupBootSector * $BytesPerSector) ([int]$BytesPerSector)
        Assert-ByteArraysEqual $primaryBoot $backupBoot "The backup FAT32 boot sector differs from the primary."
        Assert-Condition (($primaryBoot[510] -eq 0x55) -and ($primaryBoot[511] -eq 0xAA)) "The FAT32 boot signature is missing."
        Assert-Condition ((Get-UInt16LE $primaryBoot 11) -eq $BytesPerSector) "The FAT32 bytes-per-sector field is incorrect."
        Assert-Condition ($primaryBoot[13] -eq $SectorsPerCluster) "The FAT32 sectors-per-cluster field is incorrect."
        Assert-Condition ((Get-UInt16LE $primaryBoot 14) -eq $ReservedSectors) "The FAT32 reserved-sector count is incorrect."
        Assert-Condition ($primaryBoot[16] -eq $FatCount) "The FAT32 copy count is incorrect."
        Assert-Condition ((Get-UInt32LE $primaryBoot 36) -eq $fatSectors) "The FAT32 size field is incorrect."
        Assert-Condition ((Get-UInt32LE $primaryBoot 44) -eq $RootCluster) "The FAT32 root cluster is incorrect."

        $primaryFsInfo = Read-BytesAt $verifyStream ($FsInfoSector * $BytesPerSector) ([int]$BytesPerSector)
        $backupFsInfo = Read-BytesAt $verifyStream (($BackupBootSector + 1) * $BytesPerSector) ([int]$BytesPerSector)
        Assert-ByteArraysEqual $primaryFsInfo $backupFsInfo "The backup FAT32 FSInfo sector differs from the primary."
        Assert-Condition ((Get-UInt32LE $primaryFsInfo 0) -eq 0x41615252) "The FAT32 FSInfo lead signature is incorrect."
        Assert-Condition ((Get-UInt32LE $primaryFsInfo 484) -eq 0x61417272) "The FAT32 FSInfo structure signature is incorrect."
        Assert-Condition ((Get-UInt32LE $primaryFsInfo 488) -eq $freeClusters) "The FAT32 FSInfo free-cluster count is incorrect."
        Assert-Condition ((Get-UInt32LE $primaryFsInfo 492) -eq $nextFreeCluster) "The FAT32 FSInfo next-free hint is incorrect."
        Assert-Condition ((Get-UInt32LE $primaryFsInfo 508) -eq $FsInfoTrailSignature) "The FAT32 FSInfo trailing signature is incorrect."

        $firstFat = Read-BytesAt $verifyStream ($firstFatSector * $BytesPerSector) $fatByteLength
        $secondFat = Read-BytesAt $verifyStream (($firstFatSector + $fatSectors) * $BytesPerSector) $fatByteLength
        Assert-ByteArraysEqual $firstFat $secondFat "The two FAT32 copies differ."
        Assert-ByteArraysEqual $firstFat $fat "The on-disk FAT32 allocation table differs from the planned table."

        $actualRoot = Read-BytesAt $verifyStream (Get-ClusterOffset 2 $firstDataSector) ([int]$clusterBytes)
        $actualEfi = Read-BytesAt $verifyStream (Get-ClusterOffset 3 $firstDataSector) ([int]$clusterBytes)
        $actualBoot = Read-BytesAt $verifyStream (Get-ClusterOffset 4 $firstDataSector) ([int]$clusterBytes)
        Assert-ByteArraysEqual $actualRoot $rootDirectory "The FAT32 root directory is corrupt."
        Assert-ByteArraysEqual $actualEfi $efiDirectory "The FAT32 EFI directory is corrupt."
        Assert-ByteArraysEqual $actualBoot $bootDirectory "The FAT32 BOOT directory is corrupt."

        foreach ($allocation in $allocations) {
            $actualFile = Read-BytesAt $verifyStream (Get-ClusterOffset $allocation.FirstCluster $firstDataSector) $allocation.Bytes.Length
            Assert-ByteArraysEqual $actualFile $allocation.Bytes "The staged file '$($allocation.LogicalPath)' was not copied correctly."
        }
    }
    finally {
        $verifyStream.Dispose()
    }

    if ([System.IO.File]::Exists($finalPath)) {
        # File.Replace is atomic on the normal Windows workspace filesystem.  If
        # it fails, the old image remains intact and the finally block removes
        # only this invocation's uniquely named temporary file.
        $replacementBackupName = "." + [System.IO.Path]::GetFileName($finalPath) + "." + [System.Guid]::NewGuid().ToString("N") + ".bak"
        $replacementBackupPath = Join-Path $outputDirectory $replacementBackupName
        [System.IO.File]::Replace($temporaryPath, $finalPath, $replacementBackupPath, $true)
        try {
            [System.IO.File]::Delete($replacementBackupPath)
            $replacementBackupPath = $null
        }
        catch {
            Write-Warning "The new image is complete, but its replacement backup could not be removed: $replacementBackupPath"
        }
    }
    else {
        [System.IO.File]::Move($temporaryPath, $finalPath)
    }

    $imageHash = Get-FileSha256 $finalPath
    Write-Host "Created deterministic FAT32 image: $finalPath"
    Write-Host "  Size:       $ImageSizeBytes bytes (64 MiB)"
    Write-Host "  Geometry:   $TotalSectors sectors, $fatSectors sectors/FAT, $clusterCount clusters"
    Write-Host "  Free:       $freeClusters clusters"
    Write-Host "  SHA-256:    $imageHash"
}
finally {
    if ([System.IO.File]::Exists($temporaryPath)) {
        [System.IO.File]::Delete($temporaryPath)
    }
}
