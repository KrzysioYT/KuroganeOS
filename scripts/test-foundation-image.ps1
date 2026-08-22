[CmdletBinding()]
param(
    [string]$ImagePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RootDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($ImagePath)) {
    $ImagePath = Join-Path $RootDir 'build\images\KuroganeOS-base.img'
}
$ImagePath = [System.IO.Path]::GetFullPath($ImagePath)
if (-not (Test-Path -LiteralPath $ImagePath -PathType Leaf)) {
    throw "Foundation disk image does not exist: $ImagePath"
}
$item = Get-Item -LiteralPath $ImagePath
if ($item.Length -ne 536870912) {
    throw "Foundation disk image is $($item.Length) bytes; expected 536870912."
}

$beforeHash = (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash
$wslImage = (& wsl.exe --exec wslpath -a -u $ImagePath).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($wslImage)) {
    throw "Unable to convert the image path for WSL: $ImagePath"
}

$encodedPath = [Convert]::ToBase64String(
    [System.Text.Encoding]::UTF8.GetBytes($wslImage))
$script = @'
set -euo pipefail
image="$(printf %s '__IMAGE_BASE64__' | base64 -d)"
for command_name in base64 dd fsck.fat grep mdir mtype sgdisk sha256sum stat; do
    command -v "$command_name" >/dev/null || {
        echo "missing image-test dependency: $command_name" >&2
        exit 2
    }
done

[[ "$(stat -c %s "$image")" == 536870912 ]]
before="$(sha256sum "$image" | awk '{print $1}')"

verify="$(sgdisk --verify "$image" 2>&1)"
printf '%s\n' "$verify"
grep -q 'No problems found' <<<"$verify"

partition1="$(sgdisk --info=1 "$image")"
partition2="$(sgdisk --info=2 "$image")"
grep -q 'First sector: 2048 ' <<<"$partition1"
grep -q 'Last sector: 133119 ' <<<"$partition1"
grep -q 'EFI system partition' <<<"$partition1"
grep -q 'First sector: 133120 ' <<<"$partition2"
grep -q 'Last sector: 1046527 ' <<<"$partition2"
grep -qi '4B55524F-4741-4E45-8F53-524F4F543001' <<<"$partition2"

esp="$image@@$((2048 * 512))"
root="$image@@$((133120 * 512))"
mdir -i "$esp" ::/EFI/BOOT/BOOTX64.EFI ::/kernel.elf >/dev/null
mdir -i "$root" ::/bin ::/boot ::/dev ::/etc ::/home/user \
    ::/proc ::/system/bin ::/tmp ::/var/log >/dev/null
mdir -i "$root" ::/system/init ::/apps/shell ::/apps/files \
    ::/apps/monitor ::/apps/about ::/apps/external \
    ::/gui/terminal ::/gui/files ::/gui/sysmon ::/gui/about \
    ::/gui/settings >/dev/null
config="$(mtype -i "$root" ::/etc/system.cfg | tr -d '\r')"
require_config_line() {
    local expected="$1"
    if ! grep -qxF "$expected" <<<"$config"; then
        echo "foundation-image config mismatch: expected '$expected'" >&2
        echo "---- /etc/system.cfg ----" >&2
        printf '%s\n' "$config" >&2
        echo "---- end config ----" >&2
        return 1
    fi
}
require_config_line 'HOSTNAME=kurogane'
require_config_line 'BOOT_MODE=desktop'
require_config_line 'LOG_LEVEL=info'

temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
dd if="$image" of="$temporary/esp.img" bs=1M skip=1 count=64 status=none
dd if="$image" of="$temporary/root.img" bs=1M skip=65 count=446 status=none
fsck.fat -vn "$temporary/esp.img"
fsck.fat -vn "$temporary/root.img"

after="$(sha256sum "$image" | awk '{print $1}')"
[[ "$before" == "$after" ]]
echo "foundation-image test: PASS"
'@
$script = $script.Replace('__IMAGE_BASE64__', $encodedPath)
# PowerShell here-strings inherit Windows CRLF in common checkouts. Normalize
# the Bash program before encoding so WSL never sees stray carriage returns.
$normalizedScript = $script.Replace("`r`n", "`n").Replace("`r", "`n")
$encodedScript = [Convert]::ToBase64String(
    [System.Text.Encoding]::UTF8.GetBytes($normalizedScript))
$bootstrap = "printf %s $encodedScript | base64 -d | bash"
& wsl.exe --exec bash -lc $bootstrap
if ($LASTEXITCODE -ne 0) {
    throw "Foundation image validation failed with exit code $LASTEXITCODE."
}

$afterHash = (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash
if ($beforeHash -ne $afterHash) {
    throw 'Read-only image validation unexpectedly modified the disk image.'
}
Write-Host "[sha256] $($afterHash.ToLowerInvariant())"
