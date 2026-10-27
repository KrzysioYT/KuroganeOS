# Create or re-use a Flux 8 VirtualBox VM with exact fresh media.
# PowerShell 5.1+, does not delete/unregister any VM or virtual disk.
[CmdletBinding()]
param(
    [string]$Name = 'KuroganeOS-Flux8-Modern',
    [switch]$NoBuild,
    [switch]$SkipGitUpdate
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Creator = Join-Path $PSScriptRoot 'create-virtualbox-vm.ps1'
$Build = Join-Path $PSScriptRoot 'build-media.ps1'
$Branch = 'codex/road-to-15-stability'
$VBox = 'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe'
$VmRoot = Join-Path $HOME 'VirtualBox VMs'

if (-not (Test-Path -LiteralPath $Creator -PathType Leaf) -or
    -not (Test-Path -LiteralPath $Build -PathType Leaf)) {
    throw 'Missing KuroganeOS native build or VM creator script.'
}
if (-not (Test-Path -LiteralPath $VBox -PathType Leaf)) {
    throw "VBoxManage not found: $VBox"
}
Push-Location $Root
try {
    if (-not $SkipGitUpdate) {
        if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) {
            throw 'Git not found. Install Git for Windows or use -SkipGitUpdate.'
        }
        & git.exe switch $Branch
        if ($LASTEXITCODE -ne 0) { throw 'Git branch switch failed; working tree left intact.' }
        & git.exe pull --ff-only origin $Branch
        if ($LASTEXITCODE -ne 0) { throw 'Git fast-forward failed; no reset or force-pull attempted.' }
    }
    if (-not $NoBuild) {
        $started = Get-Date
        & $Build -Configuration release -Rebuild -SkipVirtualBoxSmoke
        if (-not $?) { throw 'KuroganeOS media build failed.' }
    }
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $Root 'dist') -Filter '*virtualbox-x86_64.iso' -File -ErrorAction Stop | Sort-Object LastWriteTimeUtc -Descending)
    if ($candidates.Count -eq 0) { throw 'No VirtualBox ISO exists in dist.' }
    $iso = $candidates[0]
    if (-not $NoBuild -and $iso.LastWriteTime -lt $started.AddSeconds(-5)) {
        throw "ISO is older than this build; refusing stale media: $($iso.FullName)"
    }
    if ($iso.Length -le 0) { throw 'ISO is empty.' }
    Write-Host "[Flux 8] ISO: $($iso.FullName)"
    Write-Host "[Flux 8] SHA256: $((Get-FileHash -LiteralPath $iso.FullName -Algorithm SHA256).Hash)"

    # Unregistered settings files are a recovery case, not permission to delete data.
    # Windows PowerShell 5.1 may surface VBoxManage's expected "not found"
    # stderr as a terminating NativeCommandError under Stop. Suppress only
    # this one probing call, and always restore the caller's preference.
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $null = & $VBox showvminfo $Name --machinereadable 2>$null
        $registered = $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if (-not $registered) {
        $settings = Join-Path $VmRoot ($Name + '\' + $Name + '.vbox')
        if (Test-Path -LiteralPath $settings -PathType Leaf) {
            & $VBox registervm $settings
            if ($LASTEXITCODE -ne 0) { throw "VM registration failed: $settings" }
            $registered = $true
        }
    }
    if ($registered) {
        $details = & $VBox showvminfo $Name --machinereadable
        if ($LASTEXITCODE -ne 0) { throw "Cannot read registered VM '$Name'." }
        $stateLine = @($details | Where-Object { $_ -match '^VMState=' } | Select-Object -First 1)
        if ($stateLine.Count -eq 0 -or $stateLine[0] -notmatch '^VMState="poweroff"$') {
            throw "VM '$Name' is not powered off. Shut it down normally and retry. No changes made."
        }
        & $VBox storageattach $Name --storagectl IDE --port 0 --device 0 --type dvddrive --medium $iso.FullName
        if ($LASTEXITCODE -ne 0) { throw "Could not attach ISO to '$Name'." }
        & $VBox startvm $Name --type gui
        if ($LASTEXITCODE -ne 0) { throw "VM '$Name' failed to start." }
    } else {
        $folder = Join-Path $VmRoot $Name
        if (Test-Path -LiteralPath $folder) {
            # Never overwrite or re-use orphaned VDI data without approval.
            $Name = "$Name-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
            Write-Warning "Old VM directory exists but has no registered VM. Using a new name: $Name"
        }
        & $Creator -Iso $iso.FullName -Name $Name -DiskSizeMb 8192 -Nic pcnet -Start
        if (-not $?) { throw "VM creator failed. See diagnostics; no cleanup performed." }
    }
    Write-Host "[Flux 8] Booted VM: $Name"
    Write-Host ('[Flux 8] After clicking Files: .\scripts\check-flux8-ui.ps1 -Name "' + $Name + '"')
} finally {
    Pop-Location
}
