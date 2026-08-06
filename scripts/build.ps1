[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$Configuration = 'debug',
    [switch]$Clean,
    [switch]$Rebuild,
    [switch]$StageOnly,
    [switch]$NoStage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$KernelDir = Join-Path $RootDir 'kernel'
$BuildDir = Join-Path $RootDir 'build'
$ObjectDir = Join-Path $BuildDir 'obj'
$KernelElf = Join-Path $BuildDir 'kernel.elf'
$KernelMap = Join-Path $BuildDir 'kernel.map'
$LinkerScript = Join-Path $RootDir 'linker.ld'
$BootSource = Join-Path $RootDir 'boot\efi\standalone.c'
$BootLinkerScript = Join-Path $RootDir 'boot\efi\standalone-linker.ld'
$BootBuildDir = Join-Path $BuildDir 'boot'
$BootObject = Join-Path $BootBuildDir 'standalone.o'
$BootElf = Join-Path $BootBuildDir 'standalone.elf'
$BootBinary = Join-Path $BootBuildDir 'standalone.bin'
$BuiltEfiBootloader = Join-Path $BuildDir 'BOOTX64.EFI'
$EfiConverter = Join-Path $RootDir 'scripts\elf-to-efi.ps1'
$ImageBuilder = Join-Path $RootDir 'scripts\build-image.ps1'
$DiskImage = Join-Path $RootDir 'kurogane.img'
$IsoImage = Join-Path $RootDir 'kurogane.iso'
$StagingDir = Join-Path $RootDir 'iso'
$EfiBootDir = Join-Path $StagingDir 'EFI\BOOT'
$EfiBootloader = Join-Path $EfiBootDir 'BOOTX64.EFI'
$ToolchainDir = Join-Path $RootDir 'tools\compiler\x86_64-elf\bin'
$tools = @{
    CC      = Join-Path $ToolchainDir 'x86_64-elf-gcc.exe'
    CXX     = Join-Path $ToolchainDir 'x86_64-elf-g++.exe'
    LD      = Join-Path $ToolchainDir 'x86_64-elf-ld.exe'
    OBJCOPY = Join-Path $ToolchainDir 'x86_64-elf-objcopy.exe'
    READELF = Join-Path $ToolchainDir 'x86_64-elf-readelf.exe'
}

function Invoke-NativeTool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Tool,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $toolName = Split-Path -Leaf $Tool
    Write-Host ("[{0}] {1}" -f $toolName, ($Arguments -join ' '))
    & $Tool @Arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "$toolName failed with exit code $exitCode."
    }
}

function Get-RootRelativePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $rootPrefix = $RootDir.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the repository root: $fullPath"
    }

    return $fullPath.Substring($rootPrefix.Length).Replace('\', '/')
}

function Assert-Tool {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing repository-local tool: $Path"
    }
}

function Assert-KernelElf {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Kernel ELF does not exist: $Path"
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 20 -or
        $bytes[0] -ne 0x7f -or
        $bytes[1] -ne [byte][char]'E' -or
        $bytes[2] -ne [byte][char]'L' -or
        $bytes[3] -ne [byte][char]'F' -or
        $bytes[4] -ne 2) {
        throw "Kernel output is not an ELF64 file: $Path"
    }

    $machine = [System.BitConverter]::ToUInt16($bytes, 18)
    if ($machine -ne 62) {
        throw ("Kernel ELF machine is 0x{0:X4}, expected AMD64 (0x003E)." -f $machine)
    }

    $type = [System.BitConverter]::ToUInt16($bytes, 16)
    if ($type -ne 3) {
        throw ("Kernel ELF type is 0x{0:X4}, expected ET_DYN (0x0003)." -f $type)
    }
}

function Assert-EfiApplication {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "UEFI bootloader does not exist: $Path"
    }

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "BOOTX64.EFI is not an MZ/PE image: $Path"
    }

    $peOffset = [System.BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or ($peOffset + 96) -gt $bytes.Length) {
        throw "BOOTX64.EFI has an invalid PE header offset: $Path"
    }

    if ($bytes[$peOffset] -ne 0x50 -or
        $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or
        $bytes[$peOffset + 3] -ne 0) {
        throw "BOOTX64.EFI has no PE signature: $Path"
    }

    $machine = [System.BitConverter]::ToUInt16($bytes, $peOffset + 4)
    $optionalHeader = $peOffset + 24
    $optionalMagic = [System.BitConverter]::ToUInt16($bytes, $optionalHeader)
    $subsystem = [System.BitConverter]::ToUInt16($bytes, $optionalHeader + 68)

    if ($machine -ne 0x8664) {
        throw ("BOOTX64.EFI machine is 0x{0:X4}; expected AMD64 (0x8664)." -f $machine)
    }
    if ($optionalMagic -ne 0x020b) {
        throw ("BOOTX64.EFI optional-header magic is 0x{0:X4}; expected PE32+." -f $optionalMagic)
    }
    if ($subsystem -ne 10) {
        throw ("BOOTX64.EFI subsystem is {0}; expected EFI application (10)." -f $subsystem)
    }
}

function Remove-KernelOutputs {
    $targets = @(
        $BuildDir,
        $DiskImage,
        $IsoImage,
        (Join-Path $StagingDir 'kernel.elf'),
        $EfiBootDir
    )

    foreach ($target in $targets) {
        if (Test-Path -LiteralPath $target) {
            Write-Host "[clean] $target"
            Remove-Item -LiteralPath $target -Recurse -Force
        }
    }
}

function Build-EfiBootloader {
    foreach ($tool in @($tools.CC, $tools.LD, $tools.OBJCOPY, $tools.READELF)) {
        Assert-Tool -Path $tool
    }
    foreach ($inputPath in @($BootSource, $BootLinkerScript, $EfiConverter)) {
        if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
            throw "Missing UEFI loader build input: $inputPath"
        }
    }

    [System.IO.Directory]::CreateDirectory($BootBuildDir) | Out-Null
    Push-Location $RootDir
    try {
        $compileArguments = @(
            '-std=c11',
            '-O2',
            '-Wall',
            '-Wextra',
            '-Wpedantic',
            '-Werror',
            '-ffreestanding',
            '-fshort-wchar',
            '-m64',
            '-mno-red-zone',
            '-mno-mmx',
            '-mno-sse',
            '-fno-stack-protector',
            '-fno-omit-frame-pointer',
            '-fPIE',
            '-fno-plt',
            '-fno-builtin',
            '-fno-unwind-tables',
            '-fno-asynchronous-unwind-tables',
            '-ffunction-sections',
            '-fdata-sections',
            '-Wa,--noexecstack',
            '-I', 'boot/efi',
            '-I', 'boot/include',
            '-I', 'common',
            '-frandom-seed=boot/efi/standalone.c',
            '-c', 'boot/efi/standalone.c',
            '-o', 'build/boot/standalone.o'
        )
        Invoke-NativeTool -Tool $tools.CC -Arguments $compileArguments

        $linkArguments = @(
            '--build-id=none',
            '--no-warn-rwx-segments',
            '-T', 'boot/efi/standalone-linker.ld',
            '-o', 'build/boot/standalone.elf',
            'build/boot/standalone.o'
        )
        Invoke-NativeTool -Tool $tools.LD -Arguments $linkArguments

        $relocations = @(& $tools.READELF '-rW' 'build/boot/standalone.elf')
        if ($LASTEXITCODE -ne 0) {
            throw "x86_64-elf-readelf.exe failed while checking the UEFI loader."
        }
        $relocations | ForEach-Object { Write-Host $_ }
        if (($relocations -join "`n") -match '\bR_X86_64_') {
            throw 'UEFI loader contains unresolved runtime relocations.'
        }

        Invoke-NativeTool -Tool $tools.OBJCOPY -Arguments @(
            '-O', 'binary',
            'build/boot/standalone.elf',
            'build/boot/standalone.bin'
        )
        & $EfiConverter `
            -BinaryInput $BootBinary `
            -OutputPath $BuiltEfiBootloader
        if (-not $?) {
            throw 'Failed to convert the standalone loader into a PE32+ EFI application.'
        }
    }
    finally {
        Pop-Location
    }

    Assert-EfiApplication -Path $BuiltEfiBootloader
}

function Stage-Artifacts {
    Assert-KernelElf -Path $KernelElf
    Assert-EfiApplication -Path $BuiltEfiBootloader

    [System.IO.Directory]::CreateDirectory($StagingDir) | Out-Null
    [System.IO.Directory]::CreateDirectory($EfiBootDir) | Out-Null

    $rootKernel = Join-Path $StagingDir 'kernel.elf'
    $efiKernel = Join-Path $EfiBootDir 'kernel.elf'
    Copy-Item -LiteralPath $BuiltEfiBootloader -Destination $EfiBootloader -Force
    Copy-Item -LiteralPath $KernelElf -Destination $rootKernel -Force
    Copy-Item -LiteralPath $KernelElf -Destination $efiKernel -Force

    Write-Host "[stage] $EfiBootloader"
    Write-Host "[stage] $rootKernel"
    Write-Host "[stage] $efiKernel"
    Write-Host "[stage] generated and validated AMD64 EFI application"
}

function Build-DiskImage {
    if (-not (Test-Path -LiteralPath $ImageBuilder -PathType Leaf)) {
        throw "Missing FAT32 image builder: $ImageBuilder"
    }
    & $ImageBuilder `
        -StageDirectory $StagingDir `
        -OutputPath $DiskImage
    if (-not $?) {
        throw 'Failed to create the KuroganeOS FAT32 disk image.'
    }
}

try {
    if ($StageOnly -and ($Clean -or $Rebuild)) {
        throw '-StageOnly cannot be combined with -Clean or -Rebuild.'
    }

    if ($Clean -or $Rebuild) {
        Remove-KernelOutputs
        if ($Clean -and -not $Rebuild) {
            Write-Host 'KuroganeOS kernel outputs removed.'
            exit 0
        }
    }

    if (-not $StageOnly) {
        foreach ($tool in $tools.Values) {
            Assert-Tool -Path $tool
        }
        if (-not (Test-Path -LiteralPath $LinkerScript -PathType Leaf)) {
            throw "Missing linker script: $LinkerScript"
        }

        $cppSources = @(
            Get-ChildItem -LiteralPath $KernelDir -Recurse -File -Filter '*.cpp' |
                Sort-Object FullName
        )
        if ($cppSources.Count -eq 0) {
            throw "No C++ kernel sources found below $KernelDir."
        }

        $entrySource = Join-Path $KernelDir 'arch\x86_64\entry.asm'
        if (-not (Test-Path -LiteralPath $entrySource -PathType Leaf)) {
            throw "Missing kernel entry source: $entrySource"
        }
        $assemblySources = @(
            Get-ChildItem -LiteralPath $KernelDir -Recurse -File -Filter '*.asm' |
                Where-Object {
                    -not $_.FullName.Equals(
                        $entrySource,
                        [System.StringComparison]::OrdinalIgnoreCase
                    )
                } |
                Sort-Object FullName |
                ForEach-Object { $_.FullName }
        )

        [System.IO.Directory]::CreateDirectory($ObjectDir) | Out-Null
        if (-not $env:SOURCE_DATE_EPOCH) {
            $env:SOURCE_DATE_EPOCH = '0'
        }

        $commonFlags = @(
            '-ffreestanding',
            '-fno-stack-protector',
            '-fno-omit-frame-pointer',
            '-fPIE',
            '-fno-plt',
            '-Wa,--noexecstack',
            '-m64',
            '-mno-red-zone',
            '-mno-mmx',
            '-mno-sse',
            '-msoft-float'
        )
        $includeFlags = @(
            '-I', 'kernel',
            '-I', 'kernel/include',
            '-I', 'kernel/memory',
            '-I', 'kernel/fs',
            '-I', 'sdk/include'
        )
        $cxxFlags = @(
            '-std=c++17',
            '-Wall',
            '-Wextra',
            '-Wpedantic',
            '-Wshadow',
            '-Wconversion',
            '-Wundef',
            '-Werror=return-type',
            '-fno-exceptions',
            '-fno-rtti',
            '-fno-threadsafe-statics',
            '-fno-use-cxa-atexit',
            '-fno-unwind-tables',
            '-fno-asynchronous-unwind-tables',
            '-fvisibility=hidden'
        )
        if ($Configuration -eq 'debug') {
            $cxxFlags += @(
                '-O0',
                '-g3',
                '-DKUROGANE_DEBUG=1'
            )
        }
        else {
            $cxxFlags += @(
                '-O2',
                '-g1',
                '-DNDEBUG',
                '-DKUROGANE_DEBUG=0'
            )
        }

        $kernelLinkFlags = @(
            '--fatal-warnings',
            '--build-id=none',
            '-pie',
            '--no-dynamic-linker',
            '-z', 'noexecstack',
            '-z', 'text',
            '-z', 'max-page-size=0x1000',
            '-T', 'linker.ld'
        )
        $sourceManifest = [System.Collections.Generic.List[string]]::new()
        $sourceManifest.Add((Get-RootRelativePath -Path $entrySource))
        foreach ($assemblySource in $assemblySources) {
            $sourceManifest.Add((Get-RootRelativePath -Path $assemblySource))
        }
        foreach ($source in $cppSources) {
            $sourceManifest.Add((Get-RootRelativePath -Path $source.FullName))
        }
        $toolHashes = @(
            foreach ($toolPath in @($tools.CC, $tools.CXX, $tools.LD)) {
                '{0}={1}' -f $toolPath, (
                    Get-FileHash -LiteralPath $toolPath -Algorithm SHA256
                ).Hash.ToLowerInvariant()
            }
        )
        $linkerHash = (
            Get-FileHash -LiteralPath $LinkerScript -Algorithm SHA256
        ).Hash.ToLowerInvariant()

        $fingerprintPath = Join-Path $BuildDir 'compile-config.sha256'
        $fingerprintText = (@(
            'cc=' + $tools.CC
            'cxx=' + $tools.CXX
            'configuration=' + $Configuration
            'common=' + ($commonFlags -join [char]0x1f)
            'include=' + ($includeFlags -join [char]0x1f)
            'cxxflags=' + ($cxxFlags -join [char]0x1f)
            'linkflags=' + ($kernelLinkFlags -join [char]0x1f)
            'linker=' + $linkerHash
            'tools=' + ($toolHashes -join [char]0x1f)
            'sources=' + ($sourceManifest.ToArray() -join [char]0x1f)
        ) -join "`n")
        $fingerprintBytes = [System.Text.Encoding]::UTF8.GetBytes($fingerprintText)
        $fingerprintHash = [System.BitConverter]::ToString(
            [System.Security.Cryptography.SHA256]::Create().ComputeHash(
                $fingerprintBytes
            )
        ).Replace('-', '').ToLowerInvariant()
        $storedFingerprint = if (Test-Path -LiteralPath $fingerprintPath) {
            (Get-Content -LiteralPath $fingerprintPath -Raw).Trim()
        } else {
            ''
        }
        $configurationChanged = $storedFingerprint -ne $fingerprintHash

        $headerRoots = @(
            (Join-Path $RootDir 'kernel'),
            (Join-Path $RootDir 'common'),
            (Join-Path $RootDir 'sdk\include')
        )
        $newestHeaderWriteTime = [DateTime]::MinValue
        foreach ($headerRoot in $headerRoots) {
            if (-not (Test-Path -LiteralPath $headerRoot -PathType Container)) {
                continue
            }
            Get-ChildItem -LiteralPath $headerRoot -Recurse -File |
                Where-Object { $_.Extension -in @('.h', '.hpp', '.inc') } |
                ForEach-Object {
                    if ($_.LastWriteTimeUtc -gt $newestHeaderWriteTime) {
                        $newestHeaderWriteTime = $_.LastWriteTimeUtc
                    }
                }
        }

        function Test-ObjectNeedsBuild {
            param(
                [Parameter(Mandatory = $true)]
                [string]$SourcePath,
                [Parameter(Mandatory = $true)]
                [string]$ObjectPath
            )
            if ($configurationChanged -or
                -not (Test-Path -LiteralPath $ObjectPath -PathType Leaf)) {
                return $true
            }
            $objectTime = (Get-Item -LiteralPath $ObjectPath).LastWriteTimeUtc
            return (Get-Item -LiteralPath $SourcePath).LastWriteTimeUtc -gt
                       $objectTime -or
                   $newestHeaderWriteTime -gt $objectTime
        }

        $objects = [System.Collections.Generic.List[string]]::new()
        $objectsChanged = $false
        Push-Location $RootDir
        try {
            $orderedAssemblySources = @($entrySource) + $assemblySources
            foreach ($assemblySource in $orderedAssemblySources) {
                $assemblyRelative = Get-RootRelativePath -Path $assemblySource
                $kernelRelative = $assemblyRelative.Substring('kernel/'.Length)
                $assemblyObjectRelative = 'build/obj/' +
                    [System.IO.Path]::ChangeExtension($kernelRelative, '.o').Replace('\', '/')
                $assemblyDependencyRelative =
                    [System.IO.Path]::ChangeExtension($assemblyObjectRelative, '.d').Replace('\', '/')
                [System.IO.Directory]::CreateDirectory(
                    (Split-Path -Parent (Join-Path $RootDir $assemblyObjectRelative))
                ) | Out-Null

                $assemblyArguments = @()
                $assemblyArguments += $commonFlags
                $assemblyArguments += $includeFlags
                $assemblyArguments += @(
                    '-MMD', '-MP',
                    '-MF', $assemblyDependencyRelative,
                    '-MT', $assemblyObjectRelative,
                    '-c', '-x', 'assembler-with-cpp',
                    $assemblyRelative,
                    '-o', $assemblyObjectRelative
                )
                $assemblyObjectPath =
                    Join-Path $RootDir $assemblyObjectRelative
                if (Test-ObjectNeedsBuild `
                        -SourcePath $assemblySource `
                        -ObjectPath $assemblyObjectPath) {
                    Invoke-NativeTool -Tool $tools.CC -Arguments $assemblyArguments
                    $objectsChanged = $true
                } else {
                    Write-Host "[up-to-date] $assemblyObjectRelative"
                }
                $objects.Add($assemblyObjectRelative)
            }

            foreach ($source in $cppSources) {
                $sourceRelative = Get-RootRelativePath -Path $source.FullName
                $kernelRelative = $sourceRelative.Substring('kernel/'.Length)
                $objectRelative = 'build/obj/' +
                    [System.IO.Path]::ChangeExtension($kernelRelative, '.o').Replace('\', '/')
                $dependencyRelative = [System.IO.Path]::ChangeExtension($objectRelative, '.d').Replace('\', '/')
                $objectParent = Split-Path -Parent (Join-Path $RootDir $objectRelative)
                [System.IO.Directory]::CreateDirectory($objectParent) | Out-Null

                $compileArguments = @()
                $compileArguments += $commonFlags
                $compileArguments += $cxxFlags
                $compileArguments += $includeFlags
                $compileArguments += @(
                    '-MMD', '-MP',
                    '-MF', $dependencyRelative,
                    '-MT', $objectRelative,
                    "-frandom-seed=$sourceRelative",
                    '-c', $sourceRelative,
                    '-o', $objectRelative
                )
                $objectPath = Join-Path $RootDir $objectRelative
                if (Test-ObjectNeedsBuild `
                        -SourcePath $source.FullName `
                        -ObjectPath $objectPath) {
                    Invoke-NativeTool -Tool $tools.CXX -Arguments $compileArguments
                    $objectsChanged = $true
                } else {
                    Write-Host "[up-to-date] $objectRelative"
                }
                $objects.Add($objectRelative)
            }

            $linkArguments = @($kernelLinkFlags) + @(
                '-Map', 'build/kernel.map',
                '-o', 'build/kernel.elf'
            )
            $linkArguments += $objects.ToArray()
            $linkRequired =
                $objectsChanged -or
                -not (Test-Path -LiteralPath $KernelElf -PathType Leaf) -or
                (Get-Item -LiteralPath $LinkerScript).LastWriteTimeUtc -gt
                    (Get-Item -LiteralPath $KernelElf).LastWriteTimeUtc
            if ($linkRequired) {
                Invoke-NativeTool -Tool $tools.LD -Arguments $linkArguments
            } else {
                Write-Host '[up-to-date] build/kernel.elf'
            }

            Assert-KernelElf -Path $KernelElf

            $programHeaders = @(& $tools.READELF '-lW' 'build/kernel.elf')
            $readelfExit = $LASTEXITCODE
            if ($readelfExit -ne 0) {
                throw "x86_64-elf-readelf.exe failed with exit code $readelfExit."
            }
            $programHeaders | ForEach-Object { Write-Host $_ }
            if (($programHeaders -join "`n") -match '(?m)^\s*(LOAD|GNU_STACK)\s+.*\bRWE\b') {
                throw 'Linked kernel contains an executable writable segment or stack.'
            }
            if (($programHeaders -join "`n") -notmatch '(?m)^\s*DYNAMIC\s+') {
                throw 'Linked PIE kernel has no PT_DYNAMIC program header.'
            }

            $relocations = @(& $tools.READELF '-rW' 'build/kernel.elf')
            $readelfExit = $LASTEXITCODE
            if ($readelfExit -ne 0) {
                throw "x86_64-elf-readelf.exe failed with exit code $readelfExit."
            }
            $unsupportedRelocations = @(
                $relocations |
                    Select-String '\bR_X86_64_(?!RELATIVE\b)[A-Z0-9_]+\b'
            )
            if ($unsupportedRelocations.Count -ne 0) {
                throw 'PIE kernel contains relocations unsupported by the UEFI loader.'
            }
            $relocationSections = @(
                $relocations |
                    Select-String "^Relocation section '([^']+)'"
            )
            $relativeRelocations = @(
                $relocations |
                    Select-String '\bR_X86_64_RELATIVE\b'
            )
            if ($relocationSections.Count -ne 1 -or
                $relocationSections[0].Matches[0].Groups[1].Value -ne
                    '.rela.dyn' -or
                $relativeRelocations.Count -eq 0) {
                throw 'PIE kernel relocation layout does not match the loader contract.'
            }
            Write-Host (
                '[verify] {0} supported R_X86_64_RELATIVE relocations' -f
                $relativeRelocations.Count
            )

            $symbols = @(& $tools.READELF '-sW' 'build/kernel.elf')
            $readelfExit = $LASTEXITCODE
            if ($readelfExit -ne 0) {
                throw "x86_64-elf-readelf.exe failed with exit code $readelfExit."
            }
            $undefinedSymbols = @(
                $symbols |
                    Select-String '^\s*[1-9][0-9]*:\s+.*\bUND\b'
            )
            if ($undefinedSymbols.Count -ne 0) {
                throw 'PIE kernel contains undefined symbols.'
            }
            Set-Content -LiteralPath $fingerprintPath `
                -Value $fingerprintHash -Encoding ascii
        }
        finally {
            Pop-Location
        }
    }

    if (-not $NoStage) {
        Build-EfiBootloader
        Stage-Artifacts
        Build-DiskImage
    }

    if (Test-Path -LiteralPath $KernelElf -PathType Leaf) {
        $hash = (Get-FileHash -LiteralPath $KernelElf -Algorithm SHA256).Hash.ToLowerInvariant()
        Write-Host "[done] $KernelElf"
        Write-Host "[sha256] $hash"
    }
}
catch {
    Write-Error $_
    exit 1
}
