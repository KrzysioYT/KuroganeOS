#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
path = ROOT / "scripts/smoke-uefi-iso-qemu.sh"
text = path.read_text(encoding="utf-8")


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    text = text.replace(old, new, 1)


replace_once(
    '[--nic none|e1000|pcnet|virtio] [--require-network]',
    '[--nic none|e1000|pcnet|virtio] [--audio none|ac97] [--require-network]',
    "usage audio option")
replace_once(
    'nic_model="none"\nrequire_network=false\n',
    'nic_model="none"\naudio_model="none"\nrequire_network=false\n',
    "audio default")
replace_once(
    '        --nic) [[ $# -ge 2 ]] || usage; nic_model="$2"; shift 2 ;;\n',
    '        --nic) [[ $# -ge 2 ]] || usage; nic_model="$2"; shift 2 ;;\n'
    '        --audio) [[ $# -ge 2 ]] || usage; audio_model="$2"; shift 2 ;;\n',
    "audio parse")
replace_once(
    'case "$nic_model" in\n    none|e1000|pcnet|virtio) ;;\n    *) echo "invalid NIC model: $nic_model" >&2; usage ;;\nesac\n',
    'case "$nic_model" in\n'
    '    none|e1000|pcnet|virtio) ;;\n'
    '    *) echo "invalid NIC model: $nic_model" >&2; usage ;;\n'
    'esac\n'
    'case "$audio_model" in\n'
    '    none|ac97) ;;\n'
    '    *) echo "invalid audio model: $audio_model" >&2; usage ;;\n'
    'esac\n',
    "audio validation")
replace_once(
    'media_args=()\nif [[ "$media_kind" == "disk" ]]; then\n',
    'audio_args=()\n'
    'if [[ "$audio_model" == "ac97" ]]; then\n'
    '    # The null backend keeps CI headless while still exposing a real PCI\n'
    '    # Intel AC97 device to KuroganeOS. Guest DMA/mixer programming remains real.\n'
    '    audio_args=(\n'
    '        -audiodev none,id=kurogane_audio\n'
    '        -device AC97,audiodev=kurogane_audio\n'
    '    )\n'
    'fi\n\n'
    'media_args=()\n'
    'if [[ "$media_kind" == "disk" ]]; then\n',
    "audio QEMU args")
replace_once(
    '    -display none \\\n    "${network_args[@]}" \\\n    -no-reboot \\\n',
    '    -display none \\\n'
    '    "${network_args[@]}" \\\n'
    '    "${audio_args[@]}" \\\n'
    '    -no-reboot \\\n',
    "audio invocation")

path.write_text(text, encoding="utf-8")
print("QEMU AC97 smoke profile applied")
