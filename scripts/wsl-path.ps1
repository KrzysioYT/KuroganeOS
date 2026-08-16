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
