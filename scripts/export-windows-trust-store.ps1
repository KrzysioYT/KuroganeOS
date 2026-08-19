[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# kernel/net/service.cpp keeps a bounded NUL-terminated PEM cache. Leave one
# byte for the terminator and never silently truncate a trust anchor bundle.
$MaximumBundleBytes = 512KB - 1
$ServerAuthenticationOid = '1.3.6.1.5.5.7.3.1'
$AnyExtendedKeyUsageOid = '2.5.29.37.0'

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

function Get-CertificateExtension {
    param(
        [Parameter(Mandatory = $true)]
        [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [Parameter(Mandatory = $true)]
        [string]$Oid
    )

    foreach ($extension in $Certificate.Extensions) {
        if ($null -ne $extension.Oid -and $extension.Oid.Value -eq $Oid) {
            return $extension
        }
    }
    return $null
}

function Test-WebPkiRoot {
    param(
        [Parameter(Mandatory = $true)]
        [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
        [Parameter(Mandatory = $true)]
        [DateTime]$NowUtc
    )

    if ($Certificate.NotBefore.ToUniversalTime() -gt $NowUtc -or
        $Certificate.NotAfter.ToUniversalTime() -lt $NowUtc) {
        return $false
    }

    # Preserve old explicitly trusted roots that have no BasicConstraints.
    # If the extension exists, it must explicitly mark the certificate as CA.
    $basicExtension = Get-CertificateExtension -Certificate $Certificate -Oid '2.5.29.19'
    if ($null -ne $basicExtension) {
        $basic = New-Object System.Security.Cryptography.X509Certificates.X509BasicConstraintsExtension
        $basic.CopyFrom($basicExtension)
        if (-not $basic.CertificateAuthority) {
            return $false
        }
    }

    # If KeyUsage is constrained, the anchor must be allowed to sign certs.
    $keyUsageExtension = Get-CertificateExtension -Certificate $Certificate -Oid '2.5.29.15'
    if ($null -ne $keyUsageExtension) {
        $keyUsage = New-Object System.Security.Cryptography.X509Certificates.X509KeyUsageExtension
        $keyUsage.CopyFrom($keyUsageExtension)
        $keyCertSign = [System.Security.Cryptography.X509Certificates.X509KeyUsageFlags]::KeyCertSign
        if (($keyUsage.KeyUsages -band $keyCertSign) -eq 0) {
            return $false
        }
    }

    # Roots without EKU are unconstrained. If EKU exists, retain roots allowed
    # for TLS server authentication (or Any Extended Key Usage).
    $ekuExtension = Get-CertificateExtension -Certificate $Certificate -Oid '2.5.29.37'
    if ($null -ne $ekuExtension) {
        $eku = New-Object System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension
        $eku.CopyFrom($ekuExtension)
        $serverAllowed = $false
        foreach ($usage in $eku.EnhancedKeyUsages) {
            if ($usage.Value -eq $ServerAuthenticationOid -or
                $usage.Value -eq $AnyExtendedKeyUsageOid) {
                $serverAllowed = $true
                break
            }
        }
        if (-not $serverAllowed) {
            return $false
        }
    }

    return $true
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

$nowUtc = [DateTime]::UtcNow
$entries = @()
$seen = @{}
$filtered = 0
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    foreach ($certificate in $certificates) {
        if (-not (Test-WebPkiRoot -Certificate $certificate -NowUtc $nowUtc)) {
            ++$filtered
            continue
        }

        $raw = [byte[]]$certificate.RawData
        if ($null -eq $raw -or $raw.Length -eq 0) {
            ++$filtered
            continue
        }
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
    throw 'Windows Root certificate store contained no current TLS server-auth CA roots.'
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
if ($length -gt $MaximumBundleBytes) {
    throw ("Filtered Windows TLS trust bundle is still too large for the current kernel budget: " +
        "$length bytes > $MaximumBundleBytes bytes. Refusing to silently truncate trust roots.")
}

Write-Host "[trust-windows] store certificates: $($certificates.Count)"
Write-Host "[trust-windows] filtered non-Web-PKI/current roots: $filtered"
Write-Host "[trust-windows] exported TLS roots: $($entries.Count)"
Write-Host "[trust-windows] bundle bytes: $length / $MaximumBundleBytes"
Write-Host "[trust-windows] output: $OutputPath"
