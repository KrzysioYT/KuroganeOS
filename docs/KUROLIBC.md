# KuroLibC

KuroLibC będzie biblioteką userspace, odrębną od `kernel/libk`. Nie może wywoływać wewnętrznych funkcji kernela; jedyną granicą uprzywilejowania jest wersjonowane syscall ABI.

## Planowana struktura

```text
userspace/libc/
  stdio/ stdlib/ string/ errno/ unistd/
  dirent/ fcntl/ time/ signal/ pthread/
```

Pierwszy zakres to memory/string, `snprintf/printf`, allocator userspace, `open/close/read/write/lseek`, directory iteration, `sleep`, `exit` i `getpid`. Implementacja wrapperów I/O może rozpocząć się dopiero po działającym syscall transport, FD i procesach.

## Stan

Niezaimplementowane jako runtime. Publiczny SDK i deskryptor ABI kompilują się na hoście, ale zgłaszają `features=0x0` oraz brak transportu. `libk` nie będzie kopiowane ani linkowane do aplikacji.
