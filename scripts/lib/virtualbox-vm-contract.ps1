# Pure parser/validation for VBoxManage showvminfo --machinereadable.
# It must remain testable without a local VirtualBox installation.
function ConvertFrom-KuroganeVBoxMachineInfo {
    param([Parameter(Mandatory = $true)][string]$Text)
    $fields = @{}
    foreach ($line in ($Text -split '[\r\n]+')) {
        if ($line -notmatch '^\s*("[^"]+"|[^=]+)=(.*)$') { continue }
        $key = $Matches[1].Trim().Trim('"')
        $value = $Matches[2].Trim()
        if ($value.Length -ge 2 -and $value[0] -eq '"' -and
            $value[$value.Length - 1] -eq '"') {
            $value = $value.Substring(1, $value.Length - 2)
        }
        if ($key.Length -gt 0) { $fields[$key] = $value }
    }
    return $fields
}

function Normalize-KuroganeVBoxMediumPath {
    param([string]$Path)
    if ($null -eq $Path) { return '' }
    # VirtualBox may escape backslashes in machine-readable medium paths.
    return $Path.Replace('\\', '\').Replace('/', '\').Trim()
}

function Test-KuroganeVBoxVmContract {
    param(
        [Parameter(Mandatory = $true)][string]$MachineInfo,
        [Parameter(Mandatory = $true)][string]$ExpectedDisk,
        [Parameter(Mandatory = $true)][string]$ExpectedIso
    )
    $fields = ConvertFrom-KuroganeVBoxMachineInfo -Text $MachineInfo
    $errors = New-Object 'System.Collections.Generic.List[string]'

    $firmware = [string]$fields['firmware']
    if ($firmware -notmatch '^(?i:EFI64|EFI)$') {
        $errors.Add("Firmware: expected EFI64/EFI, got '$firmware'.")
    }
    foreach ($entry in @(@('boot1','dvd'), @('boot2','disk'))) {
        $value = [string]$fields[$entry[0]]
        if ($value -ine $entry[1]) {
            $errors.Add("Boot order: $($entry[0]) expected '$($entry[1])', got '$value'.")
        }
    }

    $keyboardRaw = [string]$fields['keyboard']
    $mouseRaw = [string]$fields['mouse']
    $keyboard = ($keyboardRaw -replace '[^A-Za-z0-9]', '').ToLowerInvariant()
    $mouse = ($mouseRaw -replace '[^A-Za-z0-9]', '').ToLowerInvariant()
    if ($keyboard -notin @('ps2','ps2keyboard')) {
        $errors.Add("Input: expected PS/2 keyboard, got '$keyboardRaw'.")
    }
    if ($mouse -notin @('ps2','ps2mouse')) {
        $errors.Add("Input: expected PS/2 mouse, got '$mouseRaw'.")
    }

    $sataFound = $false
    $ideFound = $false
    foreach ($key in $fields.Keys) {
        if ($key -notmatch '^storagecontrollername([0-9]+)$') { continue }
        $index = $Matches[1]
        $name = [string]$fields[$key]
        $type = [string]$fields["storagecontrollertype$index"]
        if ($name -eq 'SATA' -and $type -match '^(?i:IntelAHCI)$') {
            $sataFound = $true
        }
        if ($name -eq 'IDE' -and $type -match '^(?i:PIIX4)$') {
            $ideFound = $true
        }
    }
    if (-not $sataFound) {
        $errors.Add('Storage: expected SATA controller of type IntelAHCI.')
    }
    if (-not $ideFound) {
        $errors.Add('Optical: expected IDE controller of type PIIX4.')
    }

    $actualDisk = Normalize-KuroganeVBoxMediumPath -Path ([string]$fields['SATA-0-0'])
    $actualIso = Normalize-KuroganeVBoxMediumPath -Path ([string]$fields['IDE-0-0'])
    $expectedDiskPath = Normalize-KuroganeVBoxMediumPath -Path $ExpectedDisk
    $expectedIsoPath = Normalize-KuroganeVBoxMediumPath -Path $ExpectedIso
    if (-not [string]::Equals($actualDisk, $expectedDiskPath,
                             [System.StringComparison]::OrdinalIgnoreCase)) {
        $errors.Add("Storage: SATA 0:0 expected '$expectedDiskPath', got '$actualDisk'.")
    }
    if (-not [string]::Equals($actualIso, $expectedIsoPath,
                             [System.StringComparison]::OrdinalIgnoreCase)) {
        $errors.Add("Optical: IDE 0:0 expected '$expectedIsoPath', got '$actualIso'.")
    }
    return $errors.ToArray()
}
