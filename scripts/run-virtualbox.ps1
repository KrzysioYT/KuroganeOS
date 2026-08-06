[CmdletBinding()]
param(
    [ValidateRange(5, 600)]
    [int]$TimeoutSeconds = 45,

    [ValidateRange(64, 4096)]
    [int]$MemoryMiB = 256,

    # The VM is still powered off and unregistered. This switch only preserves
    # a copy of its VirtualBox diagnostics before the mandatory cleanup.
    [switch]$KeepOnFailure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:VBoxManage = $null

function Get-FullPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return [System.IO.Path]::GetFullPath($Path)
}

function Test-PathInside {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Parent
    )

    $fullPath = Get-FullPath -Path $Path
    $fullParent = (Get-FullPath -Path $Parent).TrimEnd('\', '/')
    $prefix = $fullParent + [System.IO.Path]::DirectorySeparatorChar
    return $fullPath.StartsWith(
        $prefix,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Assert-IsoImage {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing VirtualBox ISO input: $Path"
    }

    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read
    )
    try {
        if ($stream.Length -lt 0x8800) {
            throw "ISO input is too small to contain an ISO-9660 volume descriptor: $Path"
        }
        [void]$stream.Seek(0x8001, [System.IO.SeekOrigin]::Begin)
        $signature = New-Object byte[] 5
        if ($stream.Read($signature, 0, $signature.Length) -ne
            $signature.Length -or
            [System.Text.Encoding]::ASCII.GetString($signature) -ne 'CD001') {
            throw "ISO input has no ISO-9660 CD001 signature: $Path"
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Find-VBoxManage {
    $candidates = [System.Collections.Generic.List[string]]::new()
    $onPath = Get-Command 'VBoxManage.exe' -ErrorAction SilentlyContinue
    if ($onPath -and $onPath.Source) {
        $candidates.Add($onPath.Source)
    }
    if ($env:ProgramFiles) {
        $candidates.Add((Join-Path $env:ProgramFiles 'Oracle\VirtualBox\VBoxManage.exe'))
    }
    if (${env:ProgramFiles(x86)}) {
        $candidates.Add((Join-Path ${env:ProgramFiles(x86)} 'Oracle\VirtualBox\VBoxManage.exe'))
    }

    foreach ($candidate in @($candidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Get-FullPath -Path $candidate)
        }
    }
    throw 'VBoxManage.exe was not found on PATH or in the standard Oracle VirtualBox installation directory.'
}

function Format-VBoxArguments {
    param([string[]]$Arguments)

    return (($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + $_.Replace('"', '\"') + '"'
        }
        else {
            $_
        }
    }) -join ' ')
}

function Invoke-VBoxCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [switch]$Quiet
    )

    if (-not $Quiet) {
        Write-Host ('[vboxmanage] ' + (Format-VBoxArguments -Arguments $Arguments))
    }
    # Windows PowerShell promotes native stderr records according to the
    # caller's ErrorActionPreference. Missing-VM probes are expected to fail,
    # so collect their diagnostics without turning stderr into an exception.
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $rawOutput = @(& $script:VBoxManage @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $output = @($rawOutput | ForEach-Object { $_.ToString() })
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
    }
}

function Invoke-VBoxChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $result = Invoke-VBoxCommand -Arguments $Arguments
    if ($result.ExitCode -ne 0) {
        $details = ($result.Output -join [Environment]::NewLine).Trim()
        if (-not $details) {
            $details = 'no diagnostic output'
        }
        throw "VBoxManage failed with exit code $($result.ExitCode): $details"
    }
    return $result
}

function ConvertFrom-VBoxMachineReadable {
    param([string[]]$Lines)

    $values = @{}
    foreach ($line in $Lines) {
        if ($line -notmatch '^([^=]+)=(.*)$') {
            continue
        }
        $key = $Matches[1]
        if ($key.Length -ge 2 -and
            $key[0] -eq '"' -and
            $key[$key.Length - 1] -eq '"') {
            $key = $key.Substring(1, $key.Length - 2)
        }
        $value = $Matches[2]
        if ($value.Length -ge 2 -and
            $value[0] -eq '"' -and
            $value[$value.Length - 1] -eq '"') {
            $value = $value.Substring(1, $value.Length - 2)
        }
        $value = $value.Replace('\\', '\').Replace('\"', '"')
        $values[$key] = $value
    }
    return $values
}

function Get-VmInfo {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Target
    )

    $result = Invoke-VBoxCommand -Arguments @(
        'showvminfo', $Target, '--machinereadable'
    ) -Quiet
    if ($result.ExitCode -ne 0) {
        return [pscustomobject]@{
            Exists = $false
            Output = $result.Output
            Values = @{}
        }
    }
    return [pscustomobject]@{
        Exists = $true
        Output = $result.Output
        Values = ConvertFrom-VBoxMachineReadable -Lines $result.Output
    }
}

function Confirm-CreatedVmIdentity {
    param(
        [Parameter(Mandatory = $true)]
        $Info,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedName,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedDirectory,

        [string]$ExpectedUuid
    )

    if (-not $Info.Exists) {
        throw "The temporary VM '$ExpectedName' is not registered."
    }
    foreach ($requiredKey in @('name', 'UUID', 'CfgFile')) {
        if (-not $Info.Values.ContainsKey($requiredKey) -or
            [string]::IsNullOrWhiteSpace($Info.Values[$requiredKey])) {
            throw "Temporary VM metadata is missing '$requiredKey'."
        }
    }
    if (-not $Info.Values['name'].Equals(
            $ExpectedName,
            [System.StringComparison]::Ordinal)) {
        throw "Refusing cleanup: registered VM name does not match '$ExpectedName'."
    }

    $registeredUuid = [Guid]::Empty
    if (-not [Guid]::TryParse($Info.Values['UUID'], [ref]$registeredUuid)) {
        throw 'Refusing cleanup: temporary VM has an invalid UUID.'
    }
    $normalizedUuid = $registeredUuid.ToString('D')
    if ($ExpectedUuid -and
        -not $normalizedUuid.Equals(
            $ExpectedUuid,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup: registered VM UUID changed unexpectedly.'
    }

    $configurationPath = Get-FullPath -Path $Info.Values['CfgFile']
    $configurationDirectory = Get-FullPath -Path (
        [System.IO.Path]::GetDirectoryName($configurationPath)
    )
    $expectedFullDirectory = Get-FullPath -Path $ExpectedDirectory
    if (-not $configurationDirectory.Equals(
            $expectedFullDirectory,
            [System.StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-PathInside -Path $configurationPath -Parent $ExpectedDirectory)) {
        throw "Refusing cleanup: VM configuration is outside the expected directory: $configurationPath"
    }

    return [pscustomobject]@{
        Uuid = $normalizedUuid
        ConfigurationPath = $configurationPath
    }
}

function Read-SharedText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }
    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete
    )
    try {
        $reader = [System.IO.StreamReader]::new(
            $stream,
            [System.Text.Encoding]::ASCII,
            $true,
            4096,
            $true
        )
        try {
            return $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Save-FailureDiagnostics {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VmDirectory,

        [Parameter(Mandatory = $true)]
        [string]$Destination,

        $Info
    )

    [void][System.IO.Directory]::CreateDirectory($Destination)
    if ($Info -and $Info.Output) {
        [System.IO.File]::WriteAllLines(
            (Join-Path $Destination 'showvminfo.txt'),
            [string[]]$Info.Output,
            [System.Text.Encoding]::UTF8
        )
    }
    $sourceLogs = Join-Path $VmDirectory 'Logs'
    if (Test-Path -LiteralPath $sourceLogs -PathType Container) {
        Copy-Item -LiteralPath $sourceLogs `
            -Destination (Join-Path $Destination 'Logs') `
            -Recurse -Force
    }
}

$RootDir = Get-FullPath -Path (Join-Path $PSScriptRoot '..')
$BuildDir = Get-FullPath -Path (Join-Path $RootDir 'build')
$VirtualBoxRoot = Get-FullPath -Path (Join-Path $BuildDir 'virtualbox')
$LogDir = Get-FullPath -Path (Join-Path $BuildDir 'logs')
$IsoPath = Get-FullPath -Path (Join-Path $RootDir 'kurogane.iso')
$UniqueId = [Guid]::NewGuid().ToString('N')
$VmName = "KuroganeOS-vbox-$UniqueId"
$VmDirectory = Get-FullPath -Path (Join-Path $VirtualBoxRoot $VmName)
$SerialLog = Get-FullPath -Path (Join-Path $LogDir "$VmName-serial.log")
$DiagnosticDir = Get-FullPath -Path (Join-Path $LogDir "$VmName-diagnostics")

$CreateAttempted = $false
$VmCreated = $false
$VmUuid = $null
$TestSucceeded = $false
$LastSerial = ''
$MainError = $null
$CleanupErrors = [System.Collections.Generic.List[string]]::new()

try {
    $script:VBoxManage = Find-VBoxManage
    Assert-IsoImage -Path $IsoPath

    foreach ($pathCheck in @(
        @{ Path = $VirtualBoxRoot; Parent = $BuildDir },
        @{ Path = $VmDirectory; Parent = $VirtualBoxRoot },
        @{ Path = $LogDir; Parent = $BuildDir },
        @{ Path = $SerialLog; Parent = $LogDir },
        @{ Path = $DiagnosticDir; Parent = $LogDir }
    )) {
        if (-not (Test-PathInside `
                -Path $pathCheck.Path `
                -Parent $pathCheck.Parent)) {
            throw "Unsafe VirtualBox path outside its expected parent: $($pathCheck.Path)"
        }
    }

    [void][System.IO.Directory]::CreateDirectory($VirtualBoxRoot)
    [void][System.IO.Directory]::CreateDirectory($LogDir)
    if (Test-Path -LiteralPath $VmDirectory) {
        throw "Refusing to reuse an existing temporary VM directory: $VmDirectory"
    }
    if (Test-Path -LiteralPath $SerialLog) {
        throw "Refusing to overwrite an existing serial log: $SerialLog"
    }

    $collision = Get-VmInfo -Target $VmName
    if ($collision.Exists) {
        throw "Refusing to reuse an already registered VM name: $VmName"
    }

    Write-Host "[vbox] $script:VBoxManage"
    Write-Host "[iso] $IsoPath"
    Write-Host "[vm] $VmName"
    Write-Host "[serial] $SerialLog"

    $CreateAttempted = $true
    $createResult = Invoke-VBoxChecked -Arguments @(
        'createvm',
        '--name', $VmName,
        '--basefolder', $VirtualBoxRoot,
        '--ostype', 'Other_64',
        '--register'
    )
    $VmCreated = $true

    $createdUuid = $null
    foreach ($line in $createResult.Output) {
        if ($line -match '^UUID:\s*([0-9a-fA-F-]{36})\s*$') {
            $parsed = [Guid]::Empty
            if ([Guid]::TryParse($Matches[1], [ref]$parsed)) {
                $createdUuid = $parsed.ToString('D')
                break
            }
        }
    }
    if (-not $createdUuid) {
        throw 'VBoxManage createvm did not report a valid UUID.'
    }

    $createdInfo = Get-VmInfo -Target $VmName
    $identity = Confirm-CreatedVmIdentity `
        -Info $createdInfo `
        -ExpectedName $VmName `
        -ExpectedDirectory $VmDirectory `
        -ExpectedUuid $createdUuid
    $VmUuid = $identity.Uuid

    [void](Invoke-VBoxChecked -Arguments @(
        'modifyvm', $VmUuid,
        '--memory', "$MemoryMiB",
        '--cpus', '1',
        '--firmware', 'efi64',
        '--firmware-boot-menu', 'disabled',
        '--ioapic', 'on',
        '--x86-long-mode', 'on',
        '--chipset', 'ich9',
        '--boot1', 'dvd',
        '--boot2', 'none',
        '--boot3', 'none',
        '--boot4', 'none',
        '--graphicscontroller', 'vmsvga',
        '--vram', '16',
        '--mouse', 'ps2',
        '--keyboard', 'ps2',
        '--nic1', 'none',
        '--audio-enabled', 'off',
        '--usb-ohci', 'off',
        '--usb-ehci', 'off',
        '--usb-xhci', 'off',
        '--paravirt-provider', 'none',
        '--uart1', '0x3F8', '4',
        '--uart-type1', '16550A',
        '--uart-mode1', 'file', $SerialLog
    ))
    [void](Invoke-VBoxChecked -Arguments @(
        'storagectl', $VmUuid,
        '--name', 'SATA',
        '--add', 'sata',
        '--controller', 'IntelAhci',
        '--portcount', '1',
        '--bootable', 'on'
    ))
    [void](Invoke-VBoxChecked -Arguments @(
        'storageattach', $VmUuid,
        '--storagectl', 'SATA',
        '--port', '0',
        '--device', '0',
        '--type', 'dvddrive',
        '--mtype', 'readonly',
        '--medium', $IsoPath
    ))

    $configuredInfo = Get-VmInfo -Target $VmUuid
    [void](Confirm-CreatedVmIdentity `
        -Info $configuredInfo `
        -ExpectedName $VmName `
        -ExpectedDirectory $VmDirectory `
        -ExpectedUuid $VmUuid)
    if ($configuredInfo.Values['memory'] -ne "$MemoryMiB" -or
        $configuredInfo.Values['cpus'] -ne '1' -or
        $configuredInfo.Values['firmware'] -notmatch '^EFI') {
        throw 'VirtualBox did not retain the requested memory, CPU, or EFI settings.'
    }
    if (-not $configuredInfo.Values.ContainsKey('SATA-0-0')) {
        throw 'VirtualBox did not report the attached SATA optical medium.'
    }
    $reportedIso = Get-FullPath -Path $configuredInfo.Values['SATA-0-0']
    if (-not $reportedIso.Equals(
            $IsoPath,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "VirtualBox attached an unexpected optical medium: $reportedIso"
    }
    $serialPathReported = $false
    foreach ($value in $configuredInfo.Values.Values) {
        if ($value -and $value.IndexOf(
                $SerialLog,
                [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $serialPathReported = $true
            break
        }
    }
    if (-not $serialPathReported) {
        throw 'VirtualBox did not retain the requested COM1 raw-file path.'
    }

    [void](Invoke-VBoxChecked -Arguments @(
        'startvm', $VmUuid, '--type', 'headless'
    ))

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $nextStateCheck = [DateTime]::UtcNow
    $promptSeenAt = $null
    $fatalPattern = '(?im)(fatal:|KERNEL\s+PANIC|KERNEL\s+EXCEPTION|triple\s+fault|^\[TEST\].*:\s*FAIL\s*$)'
    while ([DateTime]::UtcNow -lt $deadline) {
        $LastSerial = Read-SharedText -Path $SerialLog
        if ($LastSerial -match $fatalPattern) {
            throw 'VirtualBox serial output contains a fatal, panic, exception, triple fault, or failed required test.'
        }
        if ($LastSerial -match 'kurogane:/ \$') {
            if (-not $promptSeenAt) {
                $promptSeenAt = [DateTime]::UtcNow
            }
            elseif (([DateTime]::UtcNow - $promptSeenAt).TotalMilliseconds -ge 750) {
                $TestSucceeded = $true
                break
            }
        }

        if ([DateTime]::UtcNow -ge $nextStateCheck) {
            $runtimeInfo = Get-VmInfo -Target $VmUuid
            if (-not $runtimeInfo.Exists) {
                throw 'The temporary VirtualBox VM disappeared while the test was running.'
            }
            $state = $runtimeInfo.Values['VMState']
            if ($state -notin @('running', 'starting')) {
                throw "The temporary VirtualBox VM stopped before reaching the shell prompt (state: $state)."
            }
            $nextStateCheck = [DateTime]::UtcNow.AddSeconds(1)
        }
        Start-Sleep -Milliseconds 200
    }

    if (-not $TestSucceeded) {
        throw "KuroganeOS did not reach a stable shell prompt in VirtualBox within $TimeoutSeconds seconds."
    }
}
catch {
    $MainError = $_.Exception.Message
}
finally {
    if ($CreateAttempted) {
        try {
            $target = if ($VmUuid) { $VmUuid } else { $VmName }
            $cleanupInfo = Get-VmInfo -Target $target
            if ($cleanupInfo.Exists) {
                $cleanupIdentity = Confirm-CreatedVmIdentity `
                    -Info $cleanupInfo `
                    -ExpectedName $VmName `
                    -ExpectedDirectory $VmDirectory `
                    -ExpectedUuid $VmUuid
                $VmUuid = $cleanupIdentity.Uuid
                $VmCreated = $true

                $state = $cleanupInfo.Values['VMState']
                if ($state -notin @('poweroff', 'aborted', 'saved')) {
                    Write-Host "[cleanup] powering off $VmUuid (state: $state)"
                    $powerOff = Invoke-VBoxCommand -Arguments @(
                        'controlvm', $VmUuid, 'poweroff'
                    )
                    if ($powerOff.ExitCode -ne 0) {
                        $CleanupErrors.Add(
                            "VirtualBox poweroff failed: $($powerOff.Output -join '; ')"
                        )
                    }
                    else {
                        $powerOffDeadline = [DateTime]::UtcNow.AddSeconds(10)
                        do {
                            Start-Sleep -Milliseconds 200
                            $cleanupInfo = Get-VmInfo -Target $VmUuid
                            if (-not $cleanupInfo.Exists -or
                                $cleanupInfo.Values['VMState'] -in @(
                                    'poweroff', 'aborted', 'saved'
                                )) {
                                break
                            }
                        } while ([DateTime]::UtcNow -lt $powerOffDeadline)
                    }
                }

                if ($MainError -and $KeepOnFailure) {
                    try {
                        Save-FailureDiagnostics `
                            -VmDirectory $VmDirectory `
                            -Destination $DiagnosticDir `
                            -Info $cleanupInfo
                        Write-Host "[diagnostics] $DiagnosticDir"
                    }
                    catch {
                        $CleanupErrors.Add(
                            "Could not preserve VirtualBox diagnostics: $($_.Exception.Message)"
                        )
                    }
                }

                # Detach the external ISO before --delete so cleanup can never
                # consider the repository artifact owned by the temporary VM.
                [void](Invoke-VBoxCommand -Arguments @(
                    'storageattach', $VmUuid,
                    '--storagectl', 'SATA',
                    '--port', '0',
                    '--device', '0',
                    '--type', 'dvddrive',
                    '--medium', 'none'
                ) -Quiet)

                $finalInfo = Get-VmInfo -Target $VmUuid
                [void](Confirm-CreatedVmIdentity `
                    -Info $finalInfo `
                    -ExpectedName $VmName `
                    -ExpectedDirectory $VmDirectory `
                    -ExpectedUuid $VmUuid)
                Write-Host "[cleanup] unregistering and deleting $VmUuid"
                $unregister = Invoke-VBoxCommand -Arguments @(
                    'unregistervm', $VmUuid, '--delete'
                )
                if ($unregister.ExitCode -ne 0) {
                    $CleanupErrors.Add(
                        "VirtualBox unregister --delete failed: $($unregister.Output -join '; ')"
                    )
                }
                elseif ((Get-VmInfo -Target $VmUuid).Exists) {
                    $CleanupErrors.Add(
                        'Temporary VirtualBox VM is still registered after unregister --delete.'
                    )
                }
            }
        }
        catch {
            $CleanupErrors.Add($_.Exception.Message)
        }
    }

    if (Test-Path -LiteralPath $VmDirectory -PathType Container) {
        try {
            # Never recursively remove an unexpected residue. Only an empty,
            # already validated per-run directory may be removed here.
            [System.IO.Directory]::Delete($VmDirectory, $false)
        }
        catch {
            $CleanupErrors.Add(
                "Temporary VM directory was not empty after VirtualBox cleanup: $VmDirectory"
            )
        }
    }
}

if ($LastSerial) {
    Write-Host '--- VirtualBox serial ---'
    Write-Host $LastSerial
}

if ($CleanupErrors.Count -ne 0) {
    $cleanupText = $CleanupErrors -join [Environment]::NewLine
    if ($MainError) {
        $MainError += [Environment]::NewLine + $cleanupText
    }
    else {
        $MainError = $cleanupText
    }
}

if ($MainError) {
    [Console]::Error.WriteLine("ERROR: $MainError")
    exit 1
}

Write-Host '[pass] KuroganeOS reached the interactive shell prompt in VirtualBox.'
Write-Host "[serial-log] $SerialLog"
exit 0
