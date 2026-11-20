# Foundation: C Strings, Memory, Formatting, Scalar Math and the Console

Status: written (Phase 16).

**Audience:** every engine programmer. For text you own and grow, use `String` and `StringBuilder` ([Strings](foundation-strings.md)); this page is for the
lower level: raw bytes, NUL-terminated C strings, fixed buffers, number parsing, scalar math and the console.

A **C string** is a run of characters ending in a zero byte (the NUL); it carries no length, so every function has to walk it to find the end. A **raw
byte** function (`MemCopy` and friends) is told how many bytes to touch and trusts you completely. Both are fast and both are unforgiving: a missing NUL or a
wrong count writes outside your buffer. Prefer `Span`, `StringView` and `Vector` ([Containers](foundation-containers.md)) and come here when an API or a fixed
buffer needs the raw form.

These headers wrap the C library and `<cmath>`. Each function is an inline forwarder, so the compiler sees the library call it saw before (`-O2`: a constant-size `MemCopy` is still moves). Code outside the wrapper modules does not include `<cstring>`, `<cstdio>`, `<cstdlib>`, `<cmath>`, `<cerrno>`, `<csignal>`, `<cstdarg>` or `<cwchar>`; see `PLAN.md` assumption #23.

## At a glance

| Task | Use | Watch out for |
|---|---|---|
| copy bytes | `MemCopy` (no overlap), `MemMove` (overlap allowed) | the count is in bytes |
| format text into a buffer | `FormatTo(buffer, capacity, "...", ...)` | check the returned length against `capacity` |
| copy a C string safely | `CStrCopyTo(destination, capacity, source)` | a result of `capacity` or more means it was cut |
| parse a number from text | `ParseDoubleExact(first, last)` | check `ok`, and compare `end` with the end of your text |
| write a number that is stored or parsed back | `FormatGeneral(buffer, capacity, value, 9)` | never `FormatTo`'s `%g`, `%f`, `%e`: they follow the host's locale |
| print to the console | `out::Print`, `out::PrintError` | goes to stdout and stderr only; files use `io::File` |
| square root, sine, floor | `Sqrt`, `Sin`, `Floor` | `f32` and `f64` overloads |

## Which header do I use?

| I need | Include | Use |
|---|---|---|
| to copy, move, fill, compare or search raw bytes; the C heap; a raw `qsort` | `cstr/mem_ops.h` | `MemCopy`, `MemMove`, `MemSet`, `MemCompare`, `MemAllocate`, `MemFree`, `SortRaw` |
| to measure, compare, search or copy a NUL-terminated string | `cstr/cstr.h` | `CStrLength`, `CStrCompare`, `CStrFind`, `CStrAppend`, and the `W` forms for `wchar_t` |
| text into a buffer | `cstr/format.h` | `FormatTo(buffer, capacity, "%d", n)`, `VFormatTo`, `WFormatTo`, `ScanFrom` |
| to read or write a number as text | `cstr/number.h` | `CStrToInt`, `CStrToDouble`, `ParseDoubleExact`, `FormatShortest`, `FormatGeneral` |
| a function with `...` arguments | `cstr/varargs.h` | `VarArgs`, `ENGINE_VA_START`, `ENGINE_VA_END` |
| `sqrt`, `sin`, `floor`, `abs`, `isnan`, ... | `lang/scalar.h` | `Sqrt`, `Sin`, `Floor`, `Abs`, `IsNaN`, ... (`f32` and `f64`) |
| to print to the console | `os/console.h` | `out::Print`, `out::PrintError`, `out::PrintText`, `out::PrintLine`, `out::PrintBytes`, `os::Flush` |
| to end the process or read the environment | `os/console.h` | `os::Abort`, `os::Exit` (`os/os.h`), `os::ExitNow`, `os::GetEnvironment` |
| the system error after a failed call | `os/last_error.h` | `os::LastError()`, `os::SetLastError(0)`; the `E*` constants arrive with the header |
| a signal handler | `os/signal.h` | `os::SetSignalHandler(SIGINT, handler)` |

The module split follows the dependency layers (`ARCHITECTURE.md`): `cstr/`, `lang/` and `sync/` are part of the base layer, so `types.h` and the containers can use them; `os/` is the layer above.

## Using them

Format into a fixed buffer, then check that nothing was cut:

```cpp
char name[32];
int needed = FormatTo(name, sizeof(name), "level_%u.map", level_index);
if (needed < 0 || static_cast<usize>(needed) >= sizeof(name)) {
    // the text did not fit (or could not be encoded): report the error
}
```

Parse a number and make sure the whole text was a number:

```cpp
const char* text = "12.5";
ParseDoubleResult r = ParseDoubleExact(text, text + CStrLength(text));
if (r.ok && r.end == text + CStrLength(text)) {
    f64 value = r.value;
}
```

Print to the console:

```cpp
out::Print("loaded %u assets in %.2f ms\n", count, elapsed_ms);
```

## Rules

- `FormatTo` is `snprintf`: the buffer is always terminated, and the result is the length the text needs. The format string of `FormatTo` is checked by the compiler; `WFormatTo` and `ScanFrom` are templates and are not.
- `ParseDoubleExact` is locale-independent and refuses a leading `+`, a leading space and the hex form (it reads the `0` of `0x10`, so compare `end` with the end of your text), and reads `nan` and `inf`, so check finiteness when you need it; `CStrToDouble` keeps `strtod`'s ways.
- **Text that is stored or parsed back never depends on the process locale.** The engine never changes the locale, but a host may: a Qt or GTK program sets it from the environment at startup, and under `nb_NO`, `de_DE` or `fr_FR` printf's `%g` writes `0,5` and `strtod` stops at the `.` of `0.5`. Write such numbers with `FormatGeneral(buffer, capacity, value, precision)` (exactly `%.<precision>g` of the C locale: nine digits read back as the same `f32`, seventeen as the same `f64`) or `FormatShortest`, and read them with `ParseDoubleExact`. `FormatTo`'s floating conversions are for messages and logs only. Tests run code under such a locale with `test::CommaLocale` (`tests/support/comma_locale.h`), which skips when none is installed.
- The console functions write to the C streams `stdout` and `stderr`; for a file use `io::File`.
- `os::LastError()` is per thread. Read it right after the failing call.
- `MemCopy` needs ranges that do not overlap; use `MemMove` when they might. A count of zero is allowed with any pointers, including null.
- `MemAllocate` returns null when the heap is exhausted and `MemFree` accepts null. Engine code normally uses an allocator ([Memory](foundation-memory.md)) and a container instead.
- `CStrCopyTo` is the copy that cannot overflow (it takes the capacity). `CStrCopy`, `CStrAppend` and `CStrCopyN` trust the buffer size, and `CStrCopyN` does not terminate the result when `source` has `count` or more characters.
- `SortRaw` is for C-style tables kept as raw memory; for a typed range use `Sort`.
- A function with `...` arguments starts with `ENGINE_VA_START(args, last)` and must call `ENGINE_VA_END(args)` for every start (and every `ENGINE_VA_COPY`).

## Common mistakes

- **Ignoring the length `FormatTo` returns.** It is the length the full text needs; a value of `capacity` or more means your text was cut.
- **Passing a count of elements where bytes are wanted.** `MemCopy(dst, src, n)` copies `n` bytes; for `u32` that is `n * sizeof(u32)`.
- **Using `CStrCompare` as a boolean.** It returns zero when the strings are equal.
- **Trusting `ParseDoubleExact` on user text without checks.** It reads `nan` and `inf`, stops at the first character that is not part of a number (`0x10` reads as `0`) and refuses a leading `+` or space.
- **Formatting a stored number with `%g`.** Source text, JSON, settings and caches written with `FormatTo("%.9g")` carry a `,` under a host's decimal-comma locale, and the reader refuses them (a host's glTF import failed so, 2026-11-18). Use `FormatGeneral`.
- **Reading `os::LastError()` late.** Another call may have changed it; read it right after the call that failed.
- **Using a C string function on a `StringView`.** A view has a length and is not necessarily NUL-terminated.

## See also

[Standard types](foundation-types.md), [Strings](foundation-strings.md), [Memory](foundation-memory.md), [Synchronization](foundation-sync.md).

## Source of truth

- `engine/foundation/cstr/` (`mem_ops.h`, `cstr.h`, `format.h`, `number.h`, `varargs.h`)
- `engine/foundation/lang/scalar.h`
- `engine/foundation/os/console.h`, `last_error.h`, `signal.h`

Full reference: [Foundation: C strings](../reference/foundation-cstr.md).
