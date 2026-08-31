#!/usr/bin/env python3
from pathlib import Path

path = Path("scripts/dev-apply-dns-service-contract.py")
text = path.read_text()

old_enum = '''replace_once(
    "kernel/net/network.hpp",
    "    NotForUs,\\n    NoRoute,\\n",
    "    NotForUs,\\n    NameNotFound,\\n    NoRoute,\\n",
)
'''
new_enum = '''replace_once(
    "kernel/net/network.hpp",
    "    IterationStopped,\\n    InterfaceError\\n};\\n",
    "    IterationStopped,\\n    InterfaceError,\\n    NameNotFound\\n};\\n",
)
'''
if text.count(old_enum) != 1:
    raise SystemExit(f"enum patch anchor count={text.count(old_enum)}, expected 1")
text = text.replace(old_enum, new_enum, 1)

old_include = '''replace_once(
    "tests/test_sdk_abi.cpp",
    "#include <kurogane/device.h>\\n",
    "#include <kurogane/device.h>\\n#include <kurogane/dns_service.h>\\n",
)
'''
new_include = '''replace_once(
    "tests/test_sdk_abi.cpp",
    "#include <kurogane/audio.h>\\n",
    "#include <kurogane/audio.h>\\n#include <kurogane/dns_service.h>\\n",
)
'''
if text.count(old_include) != 1:
    raise SystemExit(f"SDK include patch anchor count={text.count(old_include)}, expected 1")
text = text.replace(old_include, new_include, 1)

old_host_anchor = '''    '\"$OUT_DIR/test_socket\"\\n\\n',
    '\"$OUT_DIR/test_socket\"\\n\\n'
'''
new_host_anchor = '''    '  -o \"$OUT_DIR/test_socket\"\\n\\n\"$OUT_DIR/test_socket\"\\n\\n',
    '  -o \"$OUT_DIR/test_socket\"\\n\\n\"$OUT_DIR/test_socket\"\\n\\n'
'''
if text.count(old_host_anchor) != 1:
    raise SystemExit(
        f"host socket insertion anchor count={text.count(old_host_anchor)}, expected 1")
text = text.replace(old_host_anchor, new_host_anchor, 1)

status_anchor = "# Pin the public DNS service ABI in the normal SDK regression.\n"
status_patch = '''replace_once(
    "kernel/net/network.cpp",
    "        case Status::IterationStopped: return \\\"iteration stopped\\\";\\n"
    "        case Status::InterfaceError: return \\\"interface error\\\";\\n",
    "        case Status::IterationStopped: return \\\"iteration stopped\\\";\\n"
    "        case Status::InterfaceError: return \\\"interface error\\\";\\n"
    "        case Status::NameNotFound: return \\\"DNS name not found\\\";\\n",
)

'''
if text.count(status_anchor) != 1:
    raise SystemExit(f"status-message insertion anchor count={text.count(status_anchor)}, expected 1")
text = text.replace(status_anchor, status_patch + status_anchor, 1)

path.write_text(text)
print("DNS service patcher corrected for current HEAD")
