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

path.write_text(text)
print("DNS service patcher corrected for current HEAD")
