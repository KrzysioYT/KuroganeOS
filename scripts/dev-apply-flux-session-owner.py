#!/usr/bin/env python3
"""Apply the Flux 3.6 boot/session ownership and runtime-input qualification slice."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def remove_braced_block(path: str, prefix: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(prefix)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one block prefix, found {count}")
    start = text.index(prefix)
    brace = text.index("{", start)
    depth = 0
    end = None
    for index in range(brace, len(text)):
        character = text[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                break
    if end is None:
        raise SystemExit(f"{path}: unterminated guarded block")
    while end < len(text) and text[end] in " \t":
        end += 1
    if end < len(text) and text[end] == "\r":
        end += 1
    if end < len(text) and text[end] == "\n":
        end += 1
    target.write_text(text[:start] + text[end:], encoding="utf-8")


def main() -> None:
    replace_once(
        "kernel/user/console.cpp",
        "extern \"C\" bool kurogane_start_desktop_session() __attribute__((weak));\n\n",
        "",
    )
    replace_once(
        "kernel/user/console.cpp",
        "\n    // KuroganeOS 2.3: normal userspace boot owns a graphical session.\n"
        "    // The weak hook keeps hosted console tests independent from the desktop\n"
        "    // runtime while the kernel build resolves it from desktop_session.cpp.\n"
        "    if (kurogane_start_desktop_session != nullptr) {\n"
        "        static_cast<void>(kurogane_start_desktop_session());\n"
        "    }\n",
        "\n    // Console initialization owns only the bounded userspace input queue.\n"
        "    // Graphical-session ownership is established explicitly by kernel boot\n"
        "    // before PID1 starts, avoiding hidden GUI side effects and double launch.\n",
    )

    replace_once(
        "kernel/apps/desktop_session.cpp",
        "#include \"../core/log.hpp\"\n",
        "#include \"../core/log.hpp\"\n#include \"../core/string.hpp\"\n",
    )
    replace_once(
        "kernel/apps/desktop_session.cpp",
        "extern \"C\" bool kurogane_start_desktop_session() {\n"
        "    if (applications::running()) {\n"
        "        return true;\n"
        "    }\n",
        "extern \"C\" bool kurogane_start_desktop_session() {\n"
        "    if (applications::running()) {\n"
        "        const char* active = applications::active_name();\n"
        "        const bool already_owner = active != nullptr &&\n"
        "            kstd::streq(active, \"flux-session\");\n"
        "        if (!already_owner) {\n"
        "            log::write(\n"
        "                log::Level::Error, \"GUI\",\n"
        "                \"cannot start Flux session while another application owns the host\");\n"
        "        }\n"
        "        return already_owner;\n"
        "    }\n",
    )

    replace_once(
        "kernel/main.cpp",
        "extern \"C\" unsigned char kernel_stack_top[];\n",
        "extern \"C\" unsigned char kernel_stack_top[];\n"
        "extern \"C\" bool kurogane_start_desktop_session();\n",
    )
    replace_once(
        "kernel/main.cpp",
        "    } else {\n        user::console::initialize();\n        process::ProcessId init_pid = process::INVALID_PROCESS_ID;",
        "    } else {\n"
        "        if (context.force_desktop) {\n"
        "            if (!kurogane_start_desktop_session()) {\n"
        "                boot_failure(\"GUI\", \"cannot establish Flux desktop session owner\");\n"
        "            }\n"
        "            terminal::println(\"[TEST] flux_session_owner: PASS\");\n"
        "        }\n"
        "        user::console::initialize();\n"
        "        process::ProcessId init_pid = process::INVALID_PROCESS_ID;",
    )
    remove_braced_block(
        "kernel/main.cpp",
        "    if (context.force_desktop && !context.safe_mode) {\n"
        "        const auto auto_launch_status = applications::launch(\"desktop\");",
    )

    replace_once(
        "scripts/smoke-uefi-iso-qemu.sh",
        "    echo \"usage: ./scripts/smoke-uefi-iso-qemu.sh MEDIA [--disk] [--persistent-disk] [--timeout SECONDS] [--nic none|e1000|pcnet|virtio] [--require-network] [--require-tls] [--require-marker TEXT ...]\" >&2",
        "    echo \"usage: ./scripts/smoke-uefi-iso-qemu.sh MEDIA [--disk] [--persistent-disk] [--timeout SECONDS] [--nic none|e1000|pcnet|virtio] [--require-network] [--require-tls] [--send-key-after-marker TEXT KEY] [--require-marker TEXT ...]\" >&2",
    )
    replace_once(
        "scripts/smoke-uefi-iso-qemu.sh",
        "require_tls=false\nrequire_markers=()\n",
        "require_tls=false\n"
        "send_key_after_marker=\"\"\n"
        "send_key_name=\"\"\n"
        "send_key_sent=false\n"
        "require_markers=()\n",
    )
    replace_once(
        "scripts/smoke-uefi-iso-qemu.sh",
        "        --require-tls) require_tls=true; require_network=true; shift ;;\n"
        "        --require-marker) [[ $# -ge 2 && -n \"$2\" ]] || usage; require_markers+=(\"$2\"); shift 2 ;;",
        "        --require-tls) require_tls=true; require_network=true; shift ;;\n"
        "        --send-key-after-marker)\n"
        "            [[ $# -ge 3 && -n \"$2\" && -n \"$3\" ]] || usage\n"
        "            [[ -z \"$send_key_after_marker\" ]] || { echo \"only one --send-key-after-marker is supported\" >&2; exit 2; }\n"
        "            send_key_after_marker=\"$2\"\n"
        "            send_key_name=\"$3\"\n"
        "            shift 3\n"
        "            ;;\n"
        "        --require-marker) [[ $# -ge 2 && -n \"$2\" ]] || usage; require_markers+=(\"$2\"); shift 2 ;;",
    )
    replace_once(
        "scripts/smoke-uefi-iso-qemu.sh",
        "case \"$nic_model\" in\n    none|e1000|pcnet|virtio) ;;\n    *) echo \"invalid NIC model: $nic_model\" >&2; usage ;;\nesac\n",
        "case \"$nic_model\" in\n"
        "    none|e1000|pcnet|virtio) ;;\n"
        "    *) echo \"invalid NIC model: $nic_model\" >&2; usage ;;\n"
        "esac\n"
        "if [[ -n \"$send_key_name\" && ! \"$send_key_name\" =~ ^[A-Za-z0-9_-]+$ ]]; then\n"
        "    echo \"invalid QEMU sendkey name: $send_key_name\" >&2\n"
        "    exit 2\n"
        "fi\n",
    )
    replace_once(
        "scripts/smoke-uefi-iso-qemu.sh",
        "serial=\"$tmp/serial.log\"\nqemu_log=\"$tmp/qemu.log\"\npid=\"\"\n",
        "serial=\"$tmp/serial.log\"\n"
        "qemu_log=\"$tmp/qemu.log\"\n"
        "monitor=\"$tmp/qemu-monitor.sock\"\n"
        "pid=\"\"\n",
    )
    replace_once(
        "scripts/smoke-uefi-iso-qemu.sh",
        "trap cleanup EXIT INT TERM\n\nfirmware_code=\"${OVMF_CODE:-}\"",
        "trap cleanup EXIT INT TERM\n\n"
        "send_qemu_key() {\n"
        "    local key=\"$1\"\n"
        "    python3 - \"$monitor\" \"$key\" <<'PY'\n"
        "import socket\n"
        "import sys\n"
        "import time\n\n"
        "path, key = sys.argv[1], sys.argv[2]\n"
        "last_error = None\n"
        "for _ in range(40):\n"
        "    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
        "    try:\n"
        "        client.connect(path)\n"
        "        client.sendall((f\"sendkey {key}\\n\").encode(\"ascii\"))\n"
        "        client.close()\n"
        "        raise SystemExit(0)\n"
        "    except OSError as error:\n"
        "        last_error = error\n"
        "        client.close()\n"
        "        time.sleep(0.05)\n"
        "raise SystemExit(f\"cannot send QEMU key through monitor: {last_error}\")\n"
        "PY\n"
        "}\n\n"
        "firmware_code=\"${OVMF_CODE:-}\"",
    )
    replace_once(
        "scripts/smoke-uefi-iso-qemu.sh",
        "    -serial \"file:$serial\" \\\n    -display none \\\n",
        "    -serial \"file:$serial\" \\\n"
        "    -monitor \"unix:$monitor,server,nowait\" \\\n"
        "    -display none \\\n",
    )
    replace_once(
        "scripts/smoke-uefi-iso-qemu.sh",
        "    if [[ -f \"$serial\" ]]; then\n        if ((${#require_markers[@]} != 0)); then",
        "    if [[ -f \"$serial\" ]]; then\n"
        "        if [[ -n \"$send_key_after_marker\" && \"$send_key_sent\" == false ]] &&\n"
        "           grep -Fq \"$send_key_after_marker\" \"$serial\"; then\n"
        "            send_qemu_key \"$send_key_name\"\n"
        "            send_key_sent=true\n"
        "            echo \"[uefi-qemu] sent key '$send_key_name' after marker: $send_key_after_marker\"\n"
        "        fi\n"
        "        if ((${#require_markers[@]} != 0)); then",
    )

    replace_once(
        ".github/workflows/qualify-flux-runtime.yml",
        "      - 'kernel/user/runtime_base.inc'\n      - '.github/workflows/qualify-flux-runtime.yml'",
        "      - 'kernel/user/runtime_base.inc'\n"
        "      - 'kernel/main.cpp'\n"
        "      - 'kernel/apps/desktop_session.cpp'\n"
        "      - 'kernel/user/console.cpp'\n"
        "      - 'scripts/smoke-uefi-iso-qemu.sh'\n"
        "      - '.github/workflows/qualify-flux-runtime.yml'",
    )
    replace_once(
        ".github/workflows/qualify-flux-runtime.yml",
        "            --disk \\\n            --require-marker '[TEST] flux_retained_surface_present: PASS' \\\n",
        "            --disk \\\n"
        "            --send-key-after-marker '[TEST] live_login_profile: PASS' ret \\\n"
        "            --require-marker '[TEST] flux_session_owner: PASS' \\\n"
        "            --require-marker '[TEST] red_flux_login_to_desktop: PASS' \\\n"
        "            --require-marker '[TEST] flux_retained_surface_present: PASS' \\\n",
    )

    print("[dev-apply-flux-session-owner] applied explicit Flux ownership and keyboard-driven runtime qualification")


if __name__ == "__main__":
    main()
