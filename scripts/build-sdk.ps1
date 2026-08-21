[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$WindowsBuildFilesUrl = 'https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing'
$ToolDir = Join-Path $RootDir 'tools\compiler\x86_64-elf\bin'
$BuildDir = Join-Path $RootDir 'build\sdk'
$Sysroot = Join-Path $BuildDir 'sysroot'
$ObjectDir = Join-Path $BuildDir 'obj'
$LibraryDir = Join-Path $Sysroot 'usr\lib'
$IncludeDir = Join-Path $Sysroot 'usr\include'
$ExampleDir = Join-Path $BuildDir 'examples'
$OverlayRoot = Join-Path $RootDir 'build\userspace\rootfs'
$OverlayApps = Join-Path $OverlayRoot 'apps'
$OverlayGui = Join-Path $OverlayRoot 'gui'
$OverlayEtc = Join-Path $OverlayRoot 'etc'
$CC = Join-Path $ToolDir 'x86_64-elf-gcc.exe'
$CXX = Join-Path $ToolDir 'x86_64-elf-g++.exe'
$AR = Join-Path $ToolDir 'x86_64-elf-ar.exe'
$READELF = Join-Path $ToolDir 'x86_64-elf-readelf.exe'

function Invoke-Native {
    param([string]$Tool, [string[]]$Arguments)
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$(Split-Path -Leaf $Tool) failed with exit code $LASTEXITCODE"
    }
}

foreach ($tool in @($CC, $CXX, $AR, $READELF)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Missing Windows SDK tool: $tool`nRequired KuroganeOS Windows build files: $WindowsBuildFilesUrl`nDownload them and copy/extract the contents into the repository root, preserving tools/compiler/x86_64-elf/bin/."
    }
}
if (Test-Path -LiteralPath $Sysroot) {
    Remove-Item -LiteralPath $Sysroot -Recurse -Force
}
foreach ($directory in @($IncludeDir, $LibraryDir, $ObjectDir, $ExampleDir, $OverlayApps, $OverlayGui, $OverlayEtc)) {
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
}
Copy-Item -Path (Join-Path $RootDir 'sdk\include\*') `
    -Destination $IncludeDir -Recurse -Force
Copy-Item -LiteralPath (Join-Path $RootDir 'userspace\linker.ld') `
    -Destination (Join-Path $LibraryDir 'kurogane-user.ld') -Force

$AnvilRepoHost = if ($env:ANVIL_REPO_HOST) { $env:ANVIL_REPO_HOST } else { 'raw.githubusercontent.com' }
$AnvilRepoBase = if ($env:ANVIL_REPO_BASE) { $env:ANVIL_REPO_BASE } else { '/KrzysioYT/KuroganeOS-Packages/main' }
@("HOST=$AnvilRepoHost", "BASE=$AnvilRepoBase") | Set-Content `
    -LiteralPath (Join-Path $OverlayEtc 'anvil.repo') -Encoding ascii

$common = @(
    '-ffreestanding', '-fno-stack-protector', '-m64', '-mno-red-zone',
    '-mno-mmx', '-mno-sse', '-msoft-float', '-fno-pic', '-fno-pie',
    '-mcmodel=large', '-fno-builtin', '-ffunction-sections',
    '-fdata-sections', '-Wa,--noexecstack', '-O2', '-Wall', '-Wextra',
    '-Wpedantic', '-Werror', '-I', $IncludeDir, '-I', (Join-Path $RootDir 'common')
)

$crt = Join-Path $ObjectDir 'crt0.o'
$libc = Join-Path $ObjectDir 'libc.o'
$libkurogane = Join-Path $ObjectDir 'libkurogane.o'
$libui = Join-Path $ObjectDir 'libui.o'
Invoke-Native $CC ($common + @('-c', '-x', 'assembler-with-cpp',
    (Join-Path $RootDir 'sdk\src\crt0.S'), '-o', $crt))
Invoke-Native $CC ($common + @('-std=c11', '-c',
    (Join-Path $RootDir 'sdk\src\libc.c'), '-o', $libc))
Invoke-Native $CC ($common + @('-std=c11', '-c',
    (Join-Path $RootDir 'sdk\src\libkurogane.c'), '-o', $libkurogane))
Invoke-Native $CC ($common + @('-std=c11', '-c',
    (Join-Path $RootDir 'sdk\src\libui.c'), '-o', $libui))
Copy-Item -LiteralPath $crt -Destination (Join-Path $LibraryDir 'crt0.o') -Force
Invoke-Native $AR @('rcs', (Join-Path $LibraryDir 'libc.a'), $libc)
Invoke-Native $AR @('rcs', (Join-Path $LibraryDir 'libkurogane.a'), $libkurogane)
Invoke-Native $AR @('rcs', (Join-Path $LibraryDir 'libui.a'), $libui)

$exampleObject = Join-Path $ExampleDir 'hello.o'
$exampleElf = Join-Path $ExampleDir 'hello'
Invoke-Native $CXX ($common + @(
    '-std=c++17', '-fno-exceptions', '-fno-rtti', '-c',
    (Join-Path $RootDir 'sdk\examples\hello\main.cpp'), '-o', $exampleObject))
Invoke-Native $CXX @(
    '-nostdlib', '-static', '-no-pie',
    '-Wl,--fatal-warnings', '-Wl,--build-id=none', '-Wl,-z,noexecstack',
    '-Wl,-z,separate-code', '-Wl,--gc-sections',
    '-T', (Join-Path $LibraryDir 'kurogane-user.ld'),
    '-o', $exampleElf, (Join-Path $LibraryDir 'crt0.o'), $exampleObject,
    '-L', $LibraryDir, '-Wl,--start-group', '-lc', '-lkurogane', '-lui',
    '-lgcc', '-Wl,--end-group'
)

$header = @(& $READELF '-hW' $exampleElf)
$programs = @(& $READELF '-lW' $exampleElf)
$symbols = @(& $READELF '-sW' $exampleElf)
if (($header -join "`n") -notmatch 'Type:\s+EXEC' -or
    ($header -join "`n") -notmatch 'Machine:\s+Advanced Micro Devices X86-64' -or
    ($programs -join "`n") -match '(?m)^\s*(LOAD|GNU_STACK)\s+.*\bRWE\b' -or
    ($symbols -join "`n") -match '(?m)^\s*[1-9][0-9]*:\s+.*\bUND\b') {
    throw 'External SDK ELF failed ABI, W^X, or undefined-symbol validation.'
}
Copy-Item -LiteralPath $exampleElf -Destination (Join-Path $OverlayApps 'external') -Force

$desktopApplications = @(
    @{ Name = 'login'; InstallName = 'login'; Source = 'userspace\gui\login\main.c' },
    @{ Name = 'launcher'; InstallName = 'launcher'; Source = 'userspace\gui\launcher\main.c' },
    @{ Name = 'terminal'; InstallName = 'terminal'; Source = 'userspace\gui\terminal\main.c' },
    @{ Name = 'files'; InstallName = 'files'; Source = 'userspace\gui\files\main.c' },
    @{ Name = 'anvil'; InstallName = 'anvil'; Source = 'userspace\gui\anvil\main.c' },
    @{ Name = 'sysmon'; InstallName = 'sysmon'; Source = 'userspace\gui\sysmon\main.c' },
    @{ Name = 'performance'; InstallName = 'perf'; Source = 'userspace\gui\performance\main.c' },
    @{ Name = 'browser'; InstallName = 'browser'; Source = 'userspace\gui\browser\main.c' },
    @{ Name = 'about'; InstallName = 'about'; Source = 'userspace\gui\about\main.c' },
    @{ Name = 'settings'; InstallName = 'settings'; Source = 'userspace\gui\settings\main.c' }
)
foreach ($application in $desktopApplications) {
    $name = [string]$application.Name
    $installName = [string]$application.InstallName
    $object = Join-Path $ObjectDir "gui-$name.o"
    $elf = Join-Path $OverlayGui $installName
    Invoke-Native $CC ($common + @(
        '-std=c11', '-I', (Join-Path $RootDir 'userspace\gui'), '-c',
        (Join-Path $RootDir ([string]$application.Source)), '-o', $object))
    Invoke-Native $CC @(
        '-nostdlib', '-static', '-no-pie',
        '-Wl,--fatal-warnings', '-Wl,--build-id=none', '-Wl,-z,noexecstack',
        '-Wl,-z,separate-code', '-Wl,--gc-sections',
        '-T', (Join-Path $LibraryDir 'kurogane-user.ld'),
        '-o', $elf, (Join-Path $LibraryDir 'crt0.o'), $object,
        '-L', $LibraryDir, '-Wl,--start-group', '-lui', '-lc',
        '-lkurogane', '-lgcc', '-Wl,--end-group'
    )
    $desktopHeader = @(& $READELF '-hW' $elf)
    $desktopPrograms = @(& $READELF '-lW' $elf)
    $desktopSymbols = @(& $READELF '-sW' $elf)
    if (($desktopHeader -join "`n") -notmatch 'Type:\s+EXEC' -or
        ($desktopPrograms -join "`n") -match '(?m)^\s*(LOAD|GNU_STACK)\s+.*\bRWE\b' -or
        ($desktopSymbols -join "`n") -match '(?m)^\s*[1-9][0-9]*:\s+.*\bUND\b') {
        throw "Desktop SDK ELF failed validation: $name"
    }
    Write-Host "[sdk] /gui/$installName"
}
Write-Host "[sdk] Anvil repo: https://$AnvilRepoHost$AnvilRepoBase/index.kuro"
Write-Host "[sdk] sysroot: $Sysroot"
Write-Host "[sdk] external ELF: $exampleElf"
Write-Host '[sdk] crt0 + libc + libkurogane + libui + desktop apps: PASS'
