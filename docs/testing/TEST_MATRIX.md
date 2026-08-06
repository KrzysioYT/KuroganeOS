# Test matrix

| Test | Timeout | Expected result | Command |
|---|---:|---|---|
| allocator | host | exit 0 | `./scripts/test.sh` |
| RAMFS | host | exit 0 | `./scripts/test.sh` |
| scheduler | host | exit 0 | `./scripts/test.sh` |
| network algorithms | host | exit 0 | `./scripts/test.sh` |
| UEFI boot | 12 s | shell prompt | `./scripts/run-qemu.sh smoke` |
| shell/keyboard/GUI | 30 s | all patterns found | `./scripts/run-qemu.sh system` |

Each command writes logs under `build/logs`. Missing storage and process
facilities prevent installer, persistence, isolation, and full-system tests.
