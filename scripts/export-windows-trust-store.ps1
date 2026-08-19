[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-KuroganePath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Convert-DerToPem {
    param([Parameter(Mandatory = $true)][byte[]]$Der)

    $base64 = [Convert]::ToBase64String($Der)
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append("-----BEGIN CERTIFICATE-----`n")
    for ($offset = 0; $offset -lt $base64.Length; $offset += 64) {
        $count = [Math]::Min(64, $base64.Length - $offset)
        [void]$builder.Append($base64.Substring($offset, $count))
        [void]$builder.Append("`n")
    }
    [void]$builder.Append("-----END CERTIFICATE-----`n")
    return $builder.ToString()
}

$OutputPath = Resolve-KuroganePath -Path $OutputPath
$outputDirectory = [System.IO.Path]::GetDirectoryName($OutputPath)
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$store = New-Object System.Security.Cryptography.X509Certificates.X509Store(
    [System.Security.Cryptography.X509Certificates.StoreName]::Root,
    [System.Security.Cryptography.X509Certificates.StoreLocation]::LocalMachine)

$certificates = @()
try {
    $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
    $certificates = @($store.Certificates)
} finally {
    $store.Close()
}

if ($certificates.Count -eq 0) {
    throw 'Windows LocalMachine Root certificate store is empty.'
}

# Raw certificate SHA-256 is used as the stable identity instead of subject or
# SHA-1 thumbprint. Sorting by this identity makes repeated exports from an
# unchanged Windows trust store byte-for-byte deterministic.
$entries = @()
$seen = @{}
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    foreach ($certificate in $certificates) {
        $raw = [byte[]]$certificate.RawData
        if ($null -eq $raw -or $raw.Length -eq 0) { continue }
        $hashBytes = $sha256.ComputeHash($raw)
        $identity = ([System.BitConverter]::ToString($hashBytes)).Replace('-', '').ToLowerInvariant()
        if ($seen.ContainsKey($identity)) { continue }
        $seen[$identity] = $true
        $entries += [pscustomobject]@{
            Identity = $identity
            Raw = $raw
            Subject = [string]$certificate.Subject
        }
    }
} finally {
    $sha256.Dispose()
}

$entries = @($entries | Sort-Object Identity)
if ($entries.Count -eq 0) {
    throw 'Windows Root certificate store contained no exportable certificates.'
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$writer = New-Object System.IO.StreamWriter($OutputPath, $false, $utf8NoBom)
try {
    $writer.NewLine = "`n"
    foreach ($entry in $entries) {
        $writer.Write((Convert-DerToPem -Der $entry.Raw))
        $writer.Write("`n")
    }
} finally {
    $writer.Dispose()
}

$length = (Get-Item -LiteralPath $OutputPath).Length
if ($length -lt 4096) {
    throw "Generated Windows trust bundle is unexpectedly small: $length bytes"
}

Write-Host "[trust-windows] exported roots: $($entries.Count)"
Write-Host "[trust-windows] bundle bytes: $length"
Write-Host "[trust-windows] output: $OutputPath"
