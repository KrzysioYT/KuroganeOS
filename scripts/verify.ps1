[CmdletBinding()]
param(
    [ValidateRange(5, 120)]
    [int]$TimeoutSeconds = 45,

    # VirtualBox is opt-in.  -SkipVirtualBox remains available for callers
    # which construct a shared verification command and explicitly disable it.
    [switch]$SkipVirtualBox,

    [switch]$RunVirtualBox,

    # Preserve this run under a unique name instead of replacing verify-latest logs.
    [switch]$KeepLogs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot 'build.ps1'
$TestScript = Join-Path $PSScriptRoot 'test.sh'
$IsoScript = Join-Path $PSScriptRoot 'build-iso.sh'
$QemuScript = Join-Path $PSScriptRoot 'run-qemu.ps1'
$FoundationImageTest = Join-Path $PSScriptRoot 'test-foundation-image.ps1'
$ScratchImageBuilder = Join-Path $PSScriptRoot 'new-ahci-scratch.ps1'
$VirtualBoxScript = Join-Path $PSScriptRoot 'run-virtualbox.ps1'
$ImagePath = Join-Path $RepoRoot 'kurogane.img'
$IsoPath = Join-Path $RepoRoot 'kurogane.iso'
$FoundationBaseImage = Join-Path $RepoRoot 'build\images\KuroganeOS-base.img'
$ScratchImage = Join-Path $RepoRoot 'build\test-disks\ahci-scratch.img'
$BuildManifest = Join-Path $RepoRoot 'build\build-info.txt'
$StateDir = Join-Path $RepoRoot 'state'
$VerifyLockPath = Join-Path $StateDir '.verify.lock'
$LogRoot = Join-Path $RepoRoot 'build\logs'
$LogRoot = [System.IO.Path]::GetFullPath($LogRoot)
[System.IO.Directory]::CreateDirectory($LogRoot) | Out-Null

$RunId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([Guid]::NewGuid().ToString('N').Substring(0, 8))
$LogStem = if ($KeepLogs) { "verify-$RunId" } else { 'verify-latest' }
$StatusLog = Join-Path $LogRoot "$LogStem-status.log"
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Assert-File {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }
}

function Assert-ScriptParameters {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string[]]$Required
    )

    $command = Get-Command -Name $Path -CommandType ExternalScript -ErrorAction Stop
    foreach ($parameter in $Required) {
        if (-not $command.Parameters.ContainsKey($parameter)) {
            throw "$(Split-Path -Leaf $Path) does not expose the required -$parameter parameter."
        }
    }
}

function Assert-BuildManifestProfile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Expected
    )

    Assert-File -Path $BuildManifest -Description 'Build manifest'
    $manifest = Get-Content -LiteralPath $BuildManifest -Raw
    if ($manifest -notmatch "(?m)^profile=$([regex]::Escape($Expected))\r?$") {
        throw "Build manifest does not describe the expected '$Expected' profile."
    }
}

function Write-Status {
    param(
        [Parameter(Mandatory = $true)]
        [string]$State,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    $line = '[{0}] [{1,-5}] {2}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $State, $Message
    Write-Host $line
    [System.IO.Directory]::CreateDirectory(
        [System.IO.Path]::GetDirectoryName($StatusLog)
    ) | Out-Null
    [System.IO.File]::AppendAllText($StatusLog, "$line`r`n", $Utf8NoBom)
}

function Get-StepLogPath {
    param(
        [Parameter(Mandatory = $true)]
        [ValidatePattern('^[a-z0-9-]+$')]
        [string]$Slug
    )

    return Join-Path $LogRoot "$LogStem-$Slug.log"
}

function Ensure-Directory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$DirectoryPath
    )

    if ([string]::IsNullOrWhiteSpace($DirectoryPath)) {
        throw 'Directory path is required.'
    }
    [System.IO.Directory]::CreateDirectory($DirectoryPath) | Out-Null
}

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [ValidatePattern('^[a-z0-9-]+$')]
        [string]$Slug,

        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    $stepLog = Get-StepLogPath -Slug $Slug
    $started = Get-Date
    Write-Status -State 'START' -Message "$Name (log: $stepLog)"

    $previousPreference = $ErrorActionPreference
    $output = @()
    $exitCode = 0
    $failure = $null

    try {
        # Native tools legitimately use stderr for diagnostics, so capture it and
        # decide success from their process exit code instead of PowerShell's stream.
        $ErrorActionPreference = 'Continue'
        $global:LASTEXITCODE = 0
        $output = @(& $Action 2>&1)
        $exitCode = $global:LASTEXITCODE
    }
    catch {
        $failure = $_
        $exitCode = 1
        $output += $_
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }

    $finished = Get-Date
    $duration = $finished - $started
    $logLines = New-Object System.Collections.Generic.List[string]
    $logLines.Add("Step: $Name")
    $logLines.Add("Started: $($started.ToString('o'))")
    $logLines.Add("Finished: $($finished.ToString('o'))")
    $logLines.Add(('Duration: {0:n1} seconds' -f $duration.TotalSeconds))
    $logLines.Add("Exit code: $exitCode")
    $logLines.Add('')
    foreach ($item in $output) {
        $logLines.Add([string]$item)
    }
    $stepDir = [System.IO.Path]::GetDirectoryName($stepLog)
    Ensure-Directory -DirectoryPath $stepDir
    [System.IO.File]::WriteAllLines($stepLog, $logLines, $Utf8NoBom)

    if ($failure -or $exitCode -ne 0) {
        Write-Status -State 'FAIL' -Message "$Name failed after $('{0:n1}' -f $duration.TotalSeconds) seconds (exit $exitCode)."
        $tail = @($output | Select-Object -Last 50)
        if ($tail.Count -gt 0) {
            Write-Host '---- failure log tail ----'
            foreach ($line in $tail) {
                Write-Host ([string]$line)
            }
            Write-Host '---- end failure log ----'
        }
        throw "$Name failed; see $stepLog"
    }

    Write-Status -State 'PASS' -Message "$Name completed in $('{0:n1}' -f $duration.TotalSeconds) seconds."
    return $output
}

function Invoke-WslBash {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WslExecutable,

        [Parameter(Mandatory = $true)]
        [string]$Script
    )

    # Windows PowerShell 5.1 re-quotes native arguments, which can corrupt a
    # multiline `bash -lc` program.  Encode the complete script so the only
    # dynamic shell token uses the restricted Base64 alphabet, then decode it
    # inside WSL and let Bash read the original UTF-8 bytes from stdin.
    $encodedScript = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($Script))
    $bootstrap = "set -o pipefail;printf %s $encodedScript|base64 -d|bash -s --"
    & $WslExecutable --exec bash -c $bootstrap
}

function New-WslRepositoryScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WslRepositoryPath,

        [Parameter(Mandatory = $true)]
        [string]$Body
    )

    # Keep the repository path out of shell syntax as well.  Its encoded value
    # cannot introduce substitutions, delimiters, or additional commands.
    $encodedPath = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($WslRepositoryPath))
    $preamble = @'
set -eu
repo_base64=__KUROGANE_REPO_BASE64__
repo="$(printf %s "$repo_base64" | base64 -d)"
cd -- "$repo"
'@
    return $preamble.Replace('__KUROGANE_REPO_BASE64__', $encodedPath) + "`n" + $Body.TrimStart() + "`n"
}

function Get-FreeMonitorPort {
    $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    }
    finally {
        $listener.Stop()
    }
}

$VerifyLock = $null
try {
    [System.IO.Directory]::CreateDirectory($StateDir) | Out-Null
    try {
        $VerifyLock = [System.IO.File]::Open(
            $VerifyLockPath,
            [System.IO.FileMode]::OpenOrCreate,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None)
    }
    catch [System.IO.IOException] {
        throw 'Another KuroganeOS verification run is already active for this repository.'
    }

    if ($RunVirtualBox -and $SkipVirtualBox) {
        throw '-RunVirtualBox and -SkipVirtualBox cannot be used together.'
    }

    foreach ($requiredFile in @(
        @{ Path = $BuildScript; Description = 'Windows build script' },
        @{ Path = $TestScript; Description = 'WSL host-test script' },
        @{ Path = $IsoScript; Description = 'WSL ISO build script' },
        @{ Path = $QemuScript; Description = 'QEMU runner' },
        @{ Path = $FoundationImageTest; Description = 'Foundation image validator' },
        @{ Path = $ScratchImageBuilder; Description = 'AHCI scratch-image builder' },
        @{ Path = $VirtualBoxScript; Description = 'VirtualBox runner' }
    )) {
        Assert-File -Path $requiredFile.Path -Description $requiredFile.Description
    }

    Assert-ScriptParameters -Path $BuildScript -Required @('Configuration', 'Rebuild')
    Assert-ScriptParameters -Path $QemuScript -Required @(
        'TimeoutSeconds',
        'ShellTest',
        'SafeMode',
        'UseDiskImage',
        'UseIso',
        'MonitorPort',
        'LogName'
    )
    Assert-ScriptParameters -Path $VirtualBoxScript -Required @('TimeoutSeconds', 'MemoryMiB', 'KeepOnFailure')

    $PowerShellExe = (Get-Command powershell.exe -ErrorAction Stop).Source
    $WslExe = (Get-Command wsl.exe -ErrorAction Stop).Source

    [System.IO.Directory]::CreateDirectory($LogRoot) | Out-Null
    [System.IO.File]::WriteAllText(
        $StatusLog,
        "KuroganeOS verification run $RunId`r`nRepository: $RepoRoot`r`nKeep logs: $([bool]$KeepLogs)`r`nVirtualBox enabled: $([bool]$RunVirtualBox)`r`n`r`n",
        $Utf8NoBom
    )

    Write-Status -State 'INFO' -Message "Verification started. Timeout: $TimeoutSeconds seconds."

    $wslProbe = @'
set -eu
kernel="$(uname -r)"
case "$kernel" in
    *microsoft-standard-WSL2*|*WSL2*) ;;
    *) echo "This runner requires WSL2; detected kernel: $kernel" >&2; exit 2 ;;
esac
for command_name in base64 bash fsck.fat g++ make python3 powershell.exe wslpath xorriso; do
    command -v "$command_name" >/dev/null || {
        echo "Missing WSL dependency: $command_name" >&2
        exit 3
    }
done
    echo "WSL2 kernel: $kernel"
'@
    Invoke-Step -Name 'WSL2 and toolchain preflight' -Slug 'wsl2-preflight' -Action {
        Invoke-WslBash -WslExecutable $WslExe -Script $wslProbe
    } | Out-Null

    $wslPathOutput = Invoke-Step -Name 'Resolve repository path in WSL2' -Slug 'wsl2-path' -Action {
        & $WslExe --exec wslpath -a -u $RepoRoot
    }
    $WslRepoRoot = @($wslPathOutput | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ })[-1]
    if (-not $WslRepoRoot.StartsWith('/')) {
        throw "wslpath returned an unexpected repository path: $WslRepoRoot"
    }

    $WslImagePath = "$WslRepoRoot/kurogane.img"

    Invoke-Step -Name 'Clean debug build' -Slug 'debug-build' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $BuildScript -Configuration debug -Rebuild
    } | Out-Null
    Assert-File -Path $ImagePath -Description 'Debug disk image'
    Assert-BuildManifestProfile -Expected 'debug'

    Invoke-Step -Name 'Host tests in WSL2' -Slug 'host-tests' -Action {
        $script = New-WslRepositoryScript -WslRepositoryPath $WslRepoRoot -Body @'
bash ./scripts/test.sh
'@
        Invoke-WslBash -WslExecutable $WslExe -Script $script
    } | Out-Null

    Invoke-Step -Name 'FAT image validation (read-only)' -Slug 'fat-fsck' -Action {
        $script = New-WslRepositoryScript -WslRepositoryPath $WslRepoRoot -Body @"
fsck.fat -vn "$WslImagePath"
"@
        Invoke-WslBash -WslExecutable $WslExe -Script $script
    } | Out-Null

    Invoke-Step -Name 'Foundation GPT/FAT image validation' -Slug 'foundation-image' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass `
            -File $FoundationImageTest
    } | Out-Null

    $diskPort = Get-FreeMonitorPort
    Invoke-Step -Name 'QEMU disk ShellTest' -Slug 'qemu-disk' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $QemuScript `
            -UseDiskImage `
            -ShellTest `
            -TimeoutSeconds $TimeoutSeconds `
            -MonitorPort $diskPort `
            -LogName "$LogStem-qemu-disk"
    } | Out-Null

    $safePort = Get-FreeMonitorPort
    Invoke-Step -Name 'QEMU disk SafeMode ShellTest' -Slug 'qemu-safe' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $QemuScript `
            -UseDiskImage `
            -SafeMode `
            -ShellTest `
            -TimeoutSeconds $TimeoutSeconds `
            -MonitorPort $safePort `
            -LogName "$LogStem-qemu-safe"
    } | Out-Null

    Invoke-Step -Name 'Clean test-profile build' -Slug 'test-build' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $BuildScript -Configuration test -Rebuild
    } | Out-Null
    Assert-File -Path $FoundationBaseImage -Description 'Test-profile Foundation GPT image'
    Assert-BuildManifestProfile -Expected 'test'

    Invoke-Step -Name 'Create tagged AHCI scratch image' -Slug 'ahci-scratch' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass `
            -File $ScratchImageBuilder -OutputPath $ScratchImage -Force
    } | Out-Null

    $testPort = Get-FreeMonitorPort
    Invoke-Step -Name 'QEMU test profile AHCI/GPT read-write ShellTest' -Slug 'qemu-test-profile' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $QemuScript `
            -UseDiskImage `
            -DiskImagePath $FoundationBaseImage `
            -WritableScratchDiskPath $ScratchImage `
            -ShellTest `
            -TimeoutSeconds $TimeoutSeconds `
            -MonitorPort $testPort `
            -LogName "$LogStem-qemu-test-profile"
    } | Out-Null

    Invoke-Step -Name 'Release build' -Slug 'release-build' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $BuildScript -Configuration release -Rebuild
    } | Out-Null
    Assert-File -Path $ImagePath -Description 'Release disk image'
    Assert-BuildManifestProfile -Expected 'release'

    Invoke-Step -Name 'Release ISO build in WSL2' -Slug 'release-iso-build' -Action {
        $script = New-WslRepositoryScript -WslRepositoryPath $WslRepoRoot -Body @'
bash ./scripts/build-iso.sh release
'@
        Invoke-WslBash -WslExecutable $WslExe -Script $script
    } | Out-Null
    Assert-File -Path $IsoPath -Description 'Release ISO image'

    $isoPort = Get-FreeMonitorPort
    Invoke-Step -Name 'QEMU ISO smoke and ShellTest' -Slug 'qemu-iso' -Action {
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $QemuScript `
            -UseIso `
            -ShellTest `
            -TimeoutSeconds $TimeoutSeconds `
            -MonitorPort $isoPort `
            -LogName "$LogStem-qemu-iso"
    } | Out-Null

    if (-not $RunVirtualBox -or $SkipVirtualBox) {
        Write-Status -State 'SKIP' -Message 'VirtualBox ISO smoke test skipped (default). Enable with -RunVirtualBox.'
    }
    else {
        $virtualBoxArguments = @(
            '-NoProfile',
            '-ExecutionPolicy', 'Bypass',
            '-File', $VirtualBoxScript,
            '-TimeoutSeconds', [string]$TimeoutSeconds,
            '-MemoryMiB', '256'
        )
        if ($KeepLogs) {
            $virtualBoxArguments += '-KeepOnFailure'
        }

        Invoke-Step -Name 'VirtualBox EFI ISO smoke test' -Slug 'virtualbox-iso' -Action {
            & $PowerShellExe @virtualBoxArguments
        } | Out-Null
    }

    Write-Status -State 'PASS' -Message "All requested verification stages passed. Status log: $StatusLog"
    exit 0
}
catch {
    if (Test-Path -LiteralPath $StatusLog -PathType Leaf) {
        Write-Status -State 'STOP' -Message ([string]$_.Exception.Message)
        Write-Host "Verification stopped at the first failure. Status log: $StatusLog"
    }
    else {
        Write-Error ([string]$_.Exception.Message)
    }
    exit 1
}
finally {
    if ($null -ne $VerifyLock) {
        $VerifyLock.Dispose()
    }
}
