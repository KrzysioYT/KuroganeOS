# KuroPOSIX

KuroPOSIX jest planowaną warstwą zgodności, a nie linuxowym ABI w kernelu.

```text
POSIX application → KuroPOSIX → Kurogane Native API → syscalls → kernel
```

Priorytetowe API: file I/O i stat, `mmap`, spawn/exec/wait, pipe/dup, poll, sockets oraz podstawowe pthread. Każda funkcja ma tłumaczyć semantykę na natywne obiekty KuroganeOS i mapować błędy na `errno` w userspace.

## Stan

Niezaimplementowane. `CONFIG_POSIX=n`. Prace rozpoczną się po procesach, syscallach, FD, KuroLibC i podstawowym programie ring 3. Pierwszym dowodem będzie mały, legalny port testowy, nie sama obecność nagłówków.
