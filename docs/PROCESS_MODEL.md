# Process i Thread Model

## Docelowe obiekty

Process posiada PID/PPID, stan, address space, listę threadów, FD table, CWD/root, exit code, credentials i nazwę. Thread posiada zapisany kontekst CPU, kernel/user stack, stan schedulera i wait reason.

Stany procesu/wątku: `NEW`, `READY`, `RUNNING`, `BLOCKED`, `SLEEPING`, `ZOMBIE`, `TERMINATED`.

## Kolejność implementacji

1. Context structure i przełączenie dwóch kernel threads na osobnych stosach.
2. Preemption, sleep/wakeup, wait queue i cleanup.
3. Per-process page tables oraz user/kernel permissions.
4. Wejście do ring 3 i bezpieczny syscall entry/return.
5. FD 0/1/2, spawn/exit/wait i kernel ELF loader.
6. `/system/init` jako PID 1.

## Stan

Aktualny scheduler jest dispatcherem callbacków i nie spełnia powyższego modelu. `tasks` pokazuje callbacki, nie procesy. Zarezerwowane selektory GDT i opis syscall ABI są przygotowaniem, nie dowodem userspace.
