[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Revision = '4137589c17766b2c0036332e00ad0d453e342a92'
$Destination = Join-Path $RootDir 'third_party\chromium\src'

if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) {
    throw 'git.exe is required to fetch Chromium.'
}

[System.IO.Directory]::CreateDirectory((Split-Path -Parent $Destination)) | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $Destination '.git'))) {
    Write-Host '[chromium] cloning official Chromium mirror (partial clone)'
    & git.exe clone --filter=blob:none --no-checkout https://github.com/chromium/chromium.git $Destination
    if ($LASTEXITCODE -ne 0) { throw 'Chromium clone failed.' }
}

& git.exe -C $Destination remote set-url origin https://github.com/chromium/chromium.git
& git.exe -C $Destination fetch --depth=1 origin $Revision
if ($LASTEXITCODE -ne 0) { throw 'Chromium revision fetch failed.' }
& git.exe -C $Destination checkout --detach $Revision
if ($LASTEXITCODE -ne 0) { throw 'Chromium checkout failed.' }
& git.exe -C $Destination sparse-checkout init --cone
& git.exe -C $Destination sparse-checkout set content/shell content/public base net url third_party/blink/public
if ($LASTEXITCODE -ne 0) { throw 'Chromium sparse checkout configuration failed.' }

Write-Host '[chromium] source ready'
Write-Host "[chromium] revision: $Revision"
Write-Host "[chromium] path: $Destination"
Write-Host '[chromium] this checkout is developer input and is not bundled into KuroganeOS images'
