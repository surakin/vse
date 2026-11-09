# Foundation: C Strings, Memory, Formatting, Scalar Math and the Console

Status: draft (Phase 16, Days 211-214: the modules and the port of `engine/`, `game/` and `tools/`; the tests follow on Days 217-218).

These headers wrap the C library and `<cmath>`. Each function is an inline forwarder, so the compiler sees the library call it saw before (`-O2`: a constant-size `MemCopy` is still moves). Code outside the wrapper modules does not include `<cstring>`, `<cstdio>`, `<cstdlib>`, `<cmath>`, `<cerrno>`, `<csignal>`, `<cstdarg>` or `<cwchar>`; see `PLAN.md` assumption #23.

## Which header do I use?

| I need | Include | Use |
|---|---|---|
| to copy, move, fill, compare or search raw bytes; the C heap; a raw `qsort` | `cstr/mem_ops.h` | `MemCopy`, `MemMove`, `MemSet`, `MemCompare`, `MemAllocate`, `MemFree`, `SortRaw` |
| to measure, compare, search or copy a NUL-terminated string | `cstr/cstr.h` | `CStrLength`, `CStrCompare`, `CStrFind`, `CStrAppend`, and the `W` forms for `wchar_t` |
| text into a buffer | `cstr/format.h` | `FormatTo(buffer, capacity, "%d", n)`, `VFormatTo`, `WFormatTo`, `ScanFrom` |
| to read or write a number as text | `cstr/number.h` | `CStrToInt`, `CStrToDouble`, `ParseDoubleExact`, `FormatShortest` |
| a function with `...` arguments | `cstr/varargs.h` | `VarArgs`, `ENGINE_VA_START`, `ENGINE_VA_END` |
| `sqrt`, `sin`, `floor`, `abs`, `isnan`, ... | `lang/scalar.h` | `Sqrt`, `Sin`, `Floor`, `Abs`, `IsNaN`, ... (`f32` and `f64`) |
| to print to the console | `os/console.h` | `os::Print`, `os::PrintError`, `os::PrintText`, `os::PrintLine`, `os::PrintBytes`, `os::Flush` |
| to end the process or read the environment | `os/console.h` | `os::Abort`, `os::Exit` (`os/os.h`), `os::ExitNow`, `os::GetEnvironment` |
| the system error after a failed call | `os/last_error.h` | `os::LastError()`, `os::SetLastError(0)`; the `E*` constants arrive with the header |
| a signal handler | `os/signal.h` | `os::SetSignalHandler(SIGINT, handler)` |

The module split follows the dependency layers (`ARCHITECTURE.md`): `cstr/`, `lang/` and `sync/` are part of the base layer, so `types.h` and the containers can use them; `os/` is the layer above.

## Rules

- `FormatTo` is `snprintf`: the buffer is always terminated, and the result is the length the text needs. The format string of `FormatTo` is checked by the compiler; `WFormatTo` and `ScanFrom` are templates and are not.
- `ParseDoubleExact` is locale-independent and refuses a leading `+`, a leading space and the hex form (it reads the `0` of `0x10`, so compare `end` with the end of your text), and reads `nan` and `inf`, so check finiteness when you need it; `CStrToDouble` keeps `strtod`'s ways.
- The console functions write to the C streams `stdout` and `stderr`; for a file use `io::File`.
- `os::LastError()` is per thread. Read it right after the failing call.

## Source of truth

- `engine/foundation/cstr/` (`mem_ops.h`, `cstr.h`, `format.h`, `number.h`, `varargs.h`)
- `engine/foundation/lang/scalar.h`
- `engine/foundation/os/console.h`, `last_error.h`, `signal.h`

Full reference: [Foundation: C strings](../reference/foundation-cstr.md).
