Set-StrictMode -Version Latest

function Convert-ToKuroganeWslPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if ($null -eq (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        throw 'WSL is required by the current Windows media tooling.'
    }

    $resolved = [System.IO.Path]::GetFullPath($Path)

    # Fast path: standard WSL automount (normally C: -> /mnt/c, etc.).
    $converted = & wsl.exe --exec wslpath -a -u $resolved 2>$null
    if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($converted)) {
        return $converted.Trim()
    }

    # Some Windows installations do not automount secondary/removable drives.
    # This is common when the repository lives on D:, I:, an external SSD, or
    # when /etc/wsl.conf changed the automount policy. Mount that drive through
    # DrvFs as root and retry instead of requiring the repository to be moved.
    if ($resolved -match '^([A-Za-z]):[\\/](.*)$') {
        $driveLetter = $Matches[1].ToLowerInvariant()
        $driveSpec = $Matches[1].ToUpperInvariant() + ':'
        $relative = $Matches[2].Replace('\', '/')
        $mountPoint = "/mnt/$driveLetter"

        & wsl.exe --user root --exec mkdir -p $mountPoint
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to create WSL mount point $mountPoint for $driveSpec"
        }

        & wsl.exe --user root --exec mountpoint -q $mountPoint
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[wsl] mounting Windows drive $driveSpec at $mountPoint"
            & wsl.exe --user root --exec mount -t drvfs $driveSpec $mountPoint
            if ($LASTEXITCODE -ne 0) {
                throw "Unable to mount Windows drive $driveSpec in WSL. Ensure WSL can access this drive or move the repository to a local NTFS drive."
            }
        }

        $candidate = if ([string]::IsNullOrWhiteSpace($relative)) {
            $mountPoint
        } else {
            "$mountPoint/$relative"
        }

        # wslpath should work after the mount, but the manually constructed
        # path is also valid for not-yet-created output files.
        Write-Host "[wsl] path bridge: $resolved -> $candidate"
        return $candidate
    }

    throw "Unable to convert Windows path for WSL: $resolved"
}

function Repair-KuroganeShellLineEndings {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Directory
    )

    $resolvedDirectory = [System.IO.Path]::GetFullPath($Directory)
    if (-not (Test-Path -LiteralPath $resolvedDirectory -PathType Container)) {
        throw "Shell-script directory does not exist: $resolvedDirectory"
    }

    $utf8Strict = [System.Text.UTF8Encoding]::new($false, $true)
    $utf8NoBom = [System.Text.UTF8Encoding]::new($false)
    $normalizedCount = 0

    foreach ($file in Get-ChildItem -LiteralPath $resolvedDirectory -Filter '*.sh' -File -Recurse) {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        if ([System.Array]::IndexOf($bytes, [byte]13) -lt 0) {
            continue
        }

        try {
            $text = $utf8Strict.GetString($bytes)
        }
        catch {
            throw "Shell script is not valid UTF-8 and cannot be normalized safely: $($file.FullName)"
        }

        $normalized = $text.Replace("`r`n", "`n").Replace("`r", "`n")
        if ($normalized -eq $text) {
            continue
        }

        [System.IO.File]::WriteAllText($file.FullName, $normalized, $utf8NoBom)
        ++$normalizedCount
        Write-Host "[wsl] normalized LF: $($file.FullName)"
    }

    if ($normalizedCount -gt 0) {
        Write-Host "[wsl] normalized $normalizedCount shell script(s) before Bash execution"
    }
}
