#!/usr/bin/env python3
"""Extend the QEMU smoke harness with count-aware input and marker gates."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    path = "scripts/smoke-uefi-iso-qemu.sh"
    replace_once(
        path,
        '[--send-key-after-marker TEXT KEY] [--require-marker TEXT ...]" >&2',
        '[--send-key-after-marker TEXT KEY] [--send-key-after-marker-count TEXT COUNT KEY ...] '
        '[--require-marker TEXT ...] [--require-marker-count TEXT COUNT ...]" >&2',
    )
    replace_once(
        path,
        'send_key_after_marker=""\nsend_key_name=""\nsend_key_sent=false\nrequire_markers=()\n',
        'send_key_after_marker=""\n'
        'send_key_name=""\n'
        'send_key_sent=false\n'
        'send_key_count_markers=()\n'
        'send_key_count_targets=()\n'
        'send_key_count_names=()\n'
        'send_key_count_sent=()\n'
        'require_markers=()\n'
        'require_marker_count_markers=()\n'
        'require_marker_count_targets=()\n',
    )
    replace_once(
        path,
        '        --require-marker) [[ $# -ge 2 && -n "$2" ]] || usage; require_markers+=("$2"); shift 2 ;;\n',
        '        --send-key-after-marker-count)\n'
        '            [[ $# -ge 4 && -n "$2" && "$3" =~ ^[1-9][0-9]*$ && -n "$4" ]] || usage\n'
        '            send_key_count_markers+=("$2")\n'
        '            send_key_count_targets+=("$3")\n'
        '            send_key_count_names+=("$4")\n'
        '            send_key_count_sent+=(false)\n'
        '            shift 4\n'
        '            ;;\n'
        '        --require-marker) [[ $# -ge 2 && -n "$2" ]] || usage; require_markers+=("$2"); shift 2 ;;\n'
        '        --require-marker-count)\n'
        '            [[ $# -ge 3 && -n "$2" && "$3" =~ ^[1-9][0-9]*$ ]] || usage\n'
        '            require_marker_count_markers+=("$2")\n'
        '            require_marker_count_targets+=("$3")\n'
        '            shift 3\n'
        '            ;;\n',
    )
    replace_once(
        path,
        'if [[ -n "$send_key_name" && ! "$send_key_name" =~ ^[A-Za-z0-9_-]+$ ]]; then\n'
        '    echo "invalid QEMU sendkey name: $send_key_name" >&2\n'
        '    exit 2\n'
        'fi\n',
        'if [[ -n "$send_key_name" && ! "$send_key_name" =~ ^[A-Za-z0-9_-]+$ ]]; then\n'
        '    echo "invalid QEMU sendkey name: $send_key_name" >&2\n'
        '    exit 2\n'
        'fi\n'
        'for key in "${send_key_count_names[@]}"; do\n'
        '    if [[ ! "$key" =~ ^[A-Za-z0-9_-]+$ ]]; then\n'
        '        echo "invalid QEMU sendkey name: $key" >&2\n'
        '        exit 2\n'
        '    fi\n'
        'done\n',
    )
    replace_once(
        path,
        'send_qemu_key() {\n',
        'marker_occurrences() {\n'
        '    local marker="$1"\n'
        '    local count\n'
        '    count="$(grep -F -c -- "$marker" "$serial" 2>/dev/null || true)"\n'
        '    printf \'%s\\n\' "${count:-0}"\n'
        '}\n\n'
        'send_qemu_key() {\n',
    )
    replace_once(
        path,
        '        if ((${#require_markers[@]} != 0)); then\n'
        '            all_markers_ready=true\n',
        '        for index in "${!send_key_count_markers[@]}"; do\n'
        '            if [[ "${send_key_count_sent[$index]}" == true ]]; then\n'
        '                continue\n'
        '            fi\n'
        '            marker="${send_key_count_markers[$index]}"\n'
        '            target="${send_key_count_targets[$index]}"\n'
        '            if (( $(marker_occurrences "$marker") >= target )); then\n'
        '                key="${send_key_count_names[$index]}"\n'
        '                send_qemu_key "$key"\n'
        '                send_key_count_sent[$index]=true\n'
        '                echo "[uefi-qemu] sent key \'$key\' after marker occurrence $target: $marker"\n'
        '            fi\n'
        '        done\n'
        '        if ((${#require_markers[@]} != 0 || ${#require_marker_count_markers[@]} != 0)); then\n'
        '            all_markers_ready=true\n',
    )
    replace_once(
        path,
        '            if $all_markers_ready; then\n'
        '                echo "[uefi-qemu] required runtime markers: PASS"\n',
        '            for index in "${!require_marker_count_markers[@]}"; do\n'
        '                marker="${require_marker_count_markers[$index]}"\n'
        '                target="${require_marker_count_targets[$index]}"\n'
        '                if (( $(marker_occurrences "$marker") < target )); then\n'
        '                    all_markers_ready=false\n'
        '                fi\n'
        '            done\n'
        '            if $all_markers_ready; then\n'
        '                echo "[uefi-qemu] required runtime markers: PASS"\n',
    )
    replace_once(
        path,
        '                for require_marker in "${require_markers[@]}"; do\n'
        '                    echo "[uefi-qemu] marker: $require_marker"\n'
        '                done\n',
        '                for require_marker in "${require_markers[@]}"; do\n'
        '                    echo "[uefi-qemu] marker: $require_marker"\n'
        '                done\n'
        '                for index in "${!require_marker_count_markers[@]}"; do\n'
        '                    echo "[uefi-qemu] marker count: ${require_marker_count_markers[$index]} >= ${require_marker_count_targets[$index]}"\n'
        '                done\n',
    )

    print("[dev-apply-flux-session-harness] added count-aware QEMU session actions")


if __name__ == "__main__":
    main()
